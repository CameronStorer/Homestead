"""Build Homestead's real-hardware sysmodule patch templates.

    python patches/homestead/build_patches.py --dumps <dir> [--armips <exe>] [--out <dir>]

<dir> holds the decompressed code.bin of each module, as written by
TraceEntertains' dump_modules.gm9: <dir>/{act,friends,http,socket,ssl}/code.bin
(firmware 11.17 modules; the addresses below are for that version).

Writes two template sets (IPS files named by title ID) into
app/romfs/homestead_ips/ by default:

  standard/  Nimbus's patches unchanged. Installed when switching back to
             Nintendo or Pretendo after using the custom server.
  custom/    Nimbus's patches plus Homestead's additions, with 64-byte slots
             holding "homestead.invalid" that the app fills with the server:
               http   - for the Dev-environment account, any URL whose host is
                        or ends in nintendowifi.net / nintendo.net /
                        pokemon-gl.com / pretendo.cc is sent to the server
               socket - the NNCS (NAT test) hosts resolve to the server

No Nintendo code ends up in the output: records hold only patch bytes.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PATCHES = os.path.dirname(HERE)
REPO = os.path.dirname(PATCHES)
BASE = 0x100000

MODULES = {
    "act": "0004013000003802",
    "friends": "0004013000003202",
    "http": "0004013000002902",
    "socket": "0004013000002E02",
    "ssl": "0004013000002F02",
}

# http sysmodule, firmware 11.17
HTTP_URL_FUNC = 0x113820          # Result f(Context*, const char* url)
HTTP_URL_FUNC_FIRST = bytes.fromhex("f84f2de9")  # push {r3-r11, lr}
HTTP_GET_NASC_ENV = 0x11ACCC      # Nimbus helper (returns environment + 1)
HTTP_CAVE_START = 0x11ADF0        # after Nimbus's code and strings
HTTP_CAVE_END = 0x11B000          # end of the executable page
PLACEHOLDER = b"homestead.invalid"
SLOT_SIZE = 64


def run(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"command failed: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}")
    return r.stdout


def ips_records(orig: bytes, patched: bytes):
    """Records (offset, data) for every run of differing bytes."""
    recs, i, n = [], 0, len(patched)
    while i < n:
        if i < len(orig) and orig[i] == patched[i]:
            i += 1
            continue
        start = i
        while i < n and not (i < len(orig) and orig[i] == patched[i]):
            i += 1
        recs.append((start, patched[start:i]))
    return recs


def ips_build(records):
    out = bytearray(b"PATCH")
    for off, data in sorted(records):
        assert off != 0x454F46 and len(data) <= 0xFFFF
        out += off.to_bytes(3, "big") + len(data).to_bytes(2, "big") + data
    return bytes(out + b"EOF")


def with_forced(records, forced):
    """Drop records overlapping `forced` ranges, then add the forced ones whole
    (so NUL padding inside address slots is part of the patch)."""
    kept = []
    for off, data in records:
        end = off + len(data)
        if any(off < f_off + len(f_data) and f_off < end for f_off, f_data in forced):
            # split around forced ranges
            for k in range(len(data)):
                a = off + k
                if not any(f_off <= a < f_off + len(f_data) for f_off, f_data in forced):
                    kept.append((a, data[k:k + 1]))
        else:
            kept.append((off, data))
    return kept + list(forced)


def symbols(sym_file):
    syms = {}
    for line in open(sym_file, encoding="utf-8", errors="replace"):
        parts = line.split()
        if len(parts) == 2 and not parts[1].startswith("."):
            syms[parts[1].lower()] = int(parts[0], 16)
    return syms


def armips(exe, module_dir, source, sym_name):
    os.makedirs(os.path.join(module_dir, "build"), exist_ok=True)
    run([exe, source, "-sym", os.path.join("build", sym_name)], cwd=module_dir)
    with open(os.path.join(module_dir, "build", "patched_code.bin"), "rb") as f:
        return f.read()


def build_http_addition(work, devkitarm):
    gcc = os.path.join(devkitarm, "bin", "arm-none-eabi-gcc")
    objcopy = os.path.join(devkitarm, "bin", "arm-none-eabi-objcopy")
    nm = os.path.join(devkitarm, "bin", "arm-none-eabi-nm")
    ld_script = os.path.join(work, "hs.ld")
    with open(ld_script, "w") as f:
        f.write(f"""
ENTRY(hs_entry)
SECTIONS {{
  . = {HTTP_CAVE_START:#x};
  .text : {{
    *(.text.hs_entry)
    *(.text.hs_rewrite)
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    KEEP(*(.hs_slot))   /* last, so the slot ends where the patch record ends */
  }}
  /DISCARD/ : {{ *(.data*) *(.bss*) *(.comment) *(.ARM.*) *(.note*) }}
}}
""")
    elf = os.path.join(work, "hs_http.elf")
    # The rewriter is Thumb to fit the cave; the entry wrapper is ARM.
    flags = ["-mcpu=mpcore", "-mfloat-abi=soft", "-Os", "-ffreestanding", "-fno-builtin",
             "-nostdlib", "-fno-pic", "-ffunction-sections", "-fno-asynchronous-unwind-tables",
             "-Wall", "-Wextra", "-Werror"]
    entry_o, rewrite_o = os.path.join(work, "entry.o"), os.path.join(work, "rewrite.o")
    run([gcc, *flags, "-marm", "-c", "-o", entry_o, os.path.join(HERE, "http_entry.S")])
    run([gcc, *flags, "-mthumb", "-c", "-o", rewrite_o, os.path.join(HERE, "http_rewrite.c")])
    run([gcc, "-nostdlib", "-T", ld_script,
         f"-Wl,--defsym,GET_NASC_ENVIRONMENT={HTTP_GET_NASC_ENV:#x}",
         f"-Wl,--defsym,ORIGINAL_CONTINUE={HTTP_URL_FUNC + 4:#x}",
         "-o", elf, entry_o, rewrite_o])
    blob_path = os.path.join(work, "hs_http.bin")
    run([objcopy, "-O", "binary", "-j", ".text", elf, blob_path])
    blob = open(blob_path, "rb").read()
    syms = {}
    for line in run([nm, elf]).splitlines():
        addr, _kind, name = line.split()
        syms[name] = int(addr, 16)
    if HTTP_CAVE_START + len(blob) > HTTP_CAVE_END:
        sys.exit(f"http addition is {len(blob)} bytes, only {HTTP_CAVE_END - HTTP_CAVE_START} fit")
    return blob, syms


def arm_branch(src, dst):
    offset = (dst - (src + 8)) >> 2
    return (0xEA000000 | (offset & 0xFFFFFF)).to_bytes(4, "little")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dumps", required=True)
    ap.add_argument("--armips", default=shutil.which("armips") or "armips")
    ap.add_argument("--devkitarm", default=os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM"))
    ap.add_argument("--out", default=os.path.join(REPO, "app", "romfs", "homestead_ips"))
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as work:
        orig, nimbus = {}, {}
        shutil.copytree(os.path.join(PATCHES, "utils"), os.path.join(work, "utils"), dirs_exist_ok=True)
        for module in MODULES:
            src_dir = os.path.join(PATCHES, module)
            dst_dir = os.path.join(work, module)
            shutil.copytree(src_dir, dst_dir, ignore=shutil.ignore_patterns("build", "out", "code.bin"))
            with open(os.path.join(args.dumps, module, "code.bin"), "rb") as f:
                orig[module] = f.read()
            with open(os.path.join(dst_dir, "code.bin"), "wb") as f:
                f.write(orig[module])
            nimbus[module] = armips(args.armips, dst_dir, os.path.join("src", "main.s"), f"{module}.sym")

        # --- http: Nimbus + entry wrapper + rewriter + slot
        http = bytearray(nimbus["http"])
        f_off = HTTP_URL_FUNC - BASE
        if bytes(http[f_off:f_off + 4]) != HTTP_URL_FUNC_FIRST:
            sys.exit("http: unexpected code at the URL function; wrong firmware version?")
        cave = bytes(http[HTTP_CAVE_START - BASE:HTTP_CAVE_END - BASE])
        if any(cave):
            sys.exit("http: the code cave is not empty; did Nimbus's patch grow?")
        blob, hs_syms = build_http_addition(work, args.devkitarm)
        branch = arm_branch(HTTP_URL_FUNC, hs_syms["hs_entry"])
        slot_off = hs_syms["hs_server"] - BASE
        http[f_off:f_off + 4] = branch
        http[HTTP_CAVE_START - BASE:HTTP_CAVE_START - BASE + len(blob)] = blob
        assert http[slot_off:slot_off + len(PLACEHOLDER)] == PLACEHOLDER
        assert hs_syms["hs_server"] + SLOT_SIZE == HTTP_CAVE_START + len(blob), "slot must end the blob"
        http_custom = ips_build(with_forced(
            ips_records(orig["http"], bytes(http)),
            [(f_off, branch), (HTTP_CAVE_START - BASE, blob)]))

        # --- socket: Nimbus hooks with the custom-server replacement
        sock_dir = os.path.join(work, "socket")
        shutil.copy(os.path.join(HERE, "socket_custom.s"), os.path.join(sock_dir, "src", "homestead.s"))
        sock = armips(args.armips, sock_dir, os.path.join("src", "homestead.s"), "socket_custom.sym")
        s_slot = symbols(os.path.join(sock_dir, "build", "socket_custom.sym"))["hs_server"] - BASE
        assert sock[s_slot:s_slot + len(PLACEHOLDER)] == PLACEHOLDER
        socket_custom = ips_build(with_forced(
            ips_records(orig["socket"], sock),
            [(s_slot, sock[s_slot:s_slot + SLOT_SIZE])]))

        standard = {m: ips_build(ips_records(orig[m], nimbus[m])) for m in MODULES}
        custom = dict(standard, http=http_custom, socket=socket_custom)

        for name, files in (("standard", standard), ("custom", custom)):
            d = os.path.join(args.out, name)
            os.makedirs(d, exist_ok=True)
            for module, data in files.items():
                with open(os.path.join(d, MODULES[module] + ".ips"), "wb") as f:
                    f.write(data)

        print(f"http addition: {len(blob)} bytes at {HTTP_CAVE_START:#x} "
              f"(entry {hs_syms['hs_entry']:#x}, rewrite {hs_syms['hs_rewrite']:#x}, slot {hs_syms['hs_server']:#x})")
        print(f"socket slot at {s_slot + BASE:#x}")
        print(f"wrote {args.out}/standard and {args.out}/custom")


if __name__ == "__main__":
    main()
