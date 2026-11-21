"""Verifies the built sysmodule templates against real module dumps.

    python tests/verify_patches.py <dumps dir> <official nimbus update dir> <fill_cli>

1. standard/ must produce exactly what the official Nimbus IPS produce.
2. custom/ is filled with test servers by the app's own slot filler
   (fill_cli), applied, and the patched http entry point is run in Unicorn:
   Dev-environment URLs for Nintendo/Pretendo hosts must reach the original
   function with the host replaced; everything else must pass through
   untouched, with callee-saved registers and the stack preserved.
3. The patched socket hostname replacement is run the same way.
"""
import os
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATES = os.path.join(HERE, "..", "app", "romfs", "homestead_ips")
BASE = 0x100000
MODULES = {"act": "0004013000003802", "friends": "0004013000003202", "http": "0004013000002902",
           "socket": "0004013000002E02", "ssl": "0004013000002F02"}

failures = 0


def check(cond, msg):
    global failures
    print(("ok   " if cond else "FAIL ") + msg)
    if not cond:
        failures += 1


def apply_ips(orig, ips):
    out = bytearray(orig)
    assert ips[:5] == b"PATCH"
    p = 5
    while ips[p:p + 3] != b"EOF":
        off = int.from_bytes(ips[p:p + 3], "big")
        size = int.from_bytes(ips[p + 3:p + 5], "big")
        p += 5
        if size == 0:
            count = int.from_bytes(ips[p:p + 2], "big")
            data = ips[p + 2:p + 3] * count
            p += 3
        else:
            data = ips[p:p + size]
            p += size
        out[off:off + len(data)] = data
    return bytes(out)


def fill(fill_cli, path, server):
    out = tempfile.mktemp(suffix=".ips")
    r = subprocess.run([*fill_cli, path, out, server], capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr.strip()
    data = open(out, "rb").read()
    os.remove(out)
    return data, int(r.stdout.strip())


STACK_TOP = 0x0FFF0000
URL_ADDR = 0x08000000
RET_MAGIC = 0x0F000000


def run_http(image, url, env):
    """Calls the patched function at 0x113820 with (ctx, url). The original
    body at 0x113824 is replaced by a stub that returns the url it received,
    and Nimbus's environment helper is stubbed to return `env`."""
    img = bytearray(image)
    # get_nasc_environment -> mov r0, #env ; bx lr
    img[0x11ACCC - BASE:0x11ACCC - BASE + 8] = bytes([env, 0x00, 0xA0, 0xE3, 0x1E, 0xFF, 0x2F, 0xE1])
    # original body at 0x113824 (after its push) -> mov r0, r1 ; pop {r3-r11, pc}
    img[0x113824 - BASE:0x113824 - BASE + 8] = bytes.fromhex("0100a0e1f88fbde8")
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    mu.mem_map(BASE, (len(img) + 0xFFF) & ~0xFFF)
    mu.mem_write(BASE, bytes(img))
    mu.mem_map(URL_ADDR, 0x1000)
    mu.mem_write(URL_ADDR, url.encode() + b"\0")
    mu.mem_map(STACK_TOP - 0x10000, 0x10000)
    mu.mem_map(RET_MAGIC, 0x1000)
    sp = STACK_TOP - 0x100
    mu.reg_write(UC_ARM_REG_SP, sp)
    mu.reg_write(UC_ARM_REG_R0, 0xC0C0C0C0)
    mu.reg_write(UC_ARM_REG_R1, URL_ADDR)
    saved = {r: 0x1000 + i for i, r in enumerate([UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
                                                  UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11])}
    for r, v in saved.items():
        mu.reg_write(r, v)
    mu.reg_write(UC_ARM_REG_LR, RET_MAGIC)
    seen_ctx = []

    def hook(uc, addr, size, _):
        if addr == 0x113824:
            seen_ctx.append(uc.reg_read(UC_ARM_REG_R0))
    mu.hook_add(UC_HOOK_CODE, hook, begin=0x113824, end=0x113824)
    mu.emu_start(0x113820, RET_MAGIC, count=100000)
    result_ptr = mu.reg_read(UC_ARM_REG_R0)
    s = bytes(mu.mem_read(result_ptr, 0x200)).split(b"\0")[0].decode()
    regs_ok = all(mu.reg_read(r) == v for r, v in saved.items()) and mu.reg_read(UC_ARM_REG_SP) == sp
    return s, regs_ok, seen_ctx


def run_socket(image, hostname):
    """Calls the patched socket handle_replacements(hostname) -> (r0, r1)."""
    mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    mu.mem_map(BASE, (len(image) + 0xFFF) & ~0xFFF)
    mu.mem_write(BASE, image)
    mu.mem_map(URL_ADDR, 0x1000)
    mu.mem_write(URL_ADDR, hostname.encode() + b"\0")
    mu.mem_map(STACK_TOP - 0x10000, 0x10000)
    mu.mem_map(RET_MAGIC, 0x1000)
    mu.reg_write(UC_ARM_REG_SP, STACK_TOP - 0x100)
    mu.reg_write(UC_ARM_REG_R0, URL_ADDR)
    mu.reg_write(UC_ARM_REG_LR, RET_MAGIC)
    mu.emu_start(0x1310C8, RET_MAGIC, count=200000)
    r0, r1 = mu.reg_read(UC_ARM_REG_R0), mu.reg_read(UC_ARM_REG_R1)
    s = bytes(mu.mem_read(r0, 0x100)).split(b"\0")[0].decode()
    return s, r1


def main():
    dumps, official, fill_cli = sys.argv[1], sys.argv[2], sys.argv[3:]
    orig = {m: open(os.path.join(dumps, m, "code.bin"), "rb").read() for m in MODULES}

    print("== standard set matches official Nimbus")
    for m, tid in MODULES.items():
        ours = apply_ips(orig[m], open(os.path.join(TEMPLATES, "standard", tid + ".ips"), "rb").read())
        theirs = apply_ips(orig[m], open(os.path.join(official, tid + ".ips"), "rb").read())
        check(ours == theirs, f"{m}: standard == official Nimbus 2.2.0")

    print("== custom set: unchanged modules")
    for m in ("act", "friends", "ssl"):
        tid = MODULES[m]
        check(open(os.path.join(TEMPLATES, "custom", tid + ".ips"), "rb").read() ==
              open(os.path.join(TEMPLATES, "standard", tid + ".ips"), "rb").read(), f"{m}: custom == standard")

    for server in ("100.99.192.29", "isle.example.org", "a" * 30 + "." + "b" * 32):
        print(f"== custom set, server = {server!r}")
        images = {}
        for m in ("http", "socket"):
            data, n = fill(fill_cli, os.path.join(TEMPLATES, "custom", MODULES[m] + ".ips"), server)
            check(data is not None and n == 1, f"{m}: app fills exactly one slot ({n})")
            images[m] = apply_ips(orig[m], data)
            # outside the cave and hooks, the module must equal Nimbus's
        std_http = apply_ips(orig["http"], open(os.path.join(TEMPLATES, "standard", MODULES["http"] + ".ips"), "rb").read())
        diffs = [i + BASE for i in range(len(std_http)) if std_http[i] != images["http"][i]]
        check(all(a in range(0x113820, 0x113824) or a in range(0x11ADF0, 0x11B000) for a in diffs),
              "http: only the entry hook and the cave differ from Nimbus")

        cases = [
            ("https://nasc.dev.nintendowifi.net/ac", 3, f"https://{server}/ac"),
            ("https://account.pretendo.cc/v1/api/people/@me", 3, f"https://{server}/v1/api/people/@me"),
            ("https://ACCOUNT.NINTENDO.NET:443/v1/api/x", 3, f"https://{server}:443/v1/api/x"),
            ("http://conntest.nintendowifi.net", 3, f"http://{server}"),
            ("https://pokemon-gl.com/?q=1", 3, f"https://{server}/?q=1"),
            ("https://example.com/nintendo.net", 3, "https://example.com/nintendo.net"),
            ("https://notnintendo.net/x", 3, "https://notnintendo.net/x"),
            ("https://nasc.test.nintendowifi.net/ac", 2, "https://nasc.test.nintendowifi.net/ac"),
            ("https://nasc.nintendowifi.net/ac", 1, "https://nasc.nintendowifi.net/ac"),
            ("https://nasc.nintendowifi.net/" + "x" * 0x170, 3, "https://nasc.nintendowifi.net/" + "x" * 0x170),
        ]
        for url, env, want in cases:
            got, regs_ok, ctx = run_http(images["http"], url, env)
            check(got == want and regs_ok and ctx == [0xC0C0C0C0],
                  f"http env={env} {url[:48]!r} -> {got[:60]!r}")

        for host, want, want_len in [("nncs1.app.nintendowifi.net", server, len(server)),
                                     ("nncs2.app.nintendowifi.net", server, len(server)),
                                     ("example.com", "example.com", 0)]:
            got, n = run_socket(images["socket"], host)
            check(got == want and n == want_len, f"socket {host} -> {got!r} len {n}")

    data, err = fill(fill_cli, os.path.join(TEMPLATES, "custom", MODULES["http"] + ".ips"), "x" * 64)
    check(data is None, f"a 64-character server is rejected ({err})")

    print("all passed" if failures == 0 else f"{failures} FAILED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
