# Homestead
## Personal server switcher for the 3DS

Homestead is a fork of [Pretendo Network's Nimbus](https://github.com/PretendoNetwork/nimbus). Next to the
Nintendo and Pretendo accounts it adds a **Custom server** account that points at any server address you type
in, so you can run your own private online service instead of a mainstream one.

It is not affiliated with Pretendo Network or Nintendo. Please don't ask Pretendo for help with Homestead.

### Status

| | |
| --- | --- |
| Azahar (HLE HTTP) | Custom server works with any address |
| Real 3DS | **Not yet.** The sysmodule patches with long-address slots still need to be built (see below) |
| Nintendo / Pretendo accounts | Unchanged from Nimbus |

### How it works

- The custom server uses its own friend account (slot 3, NASC environment Dev), so switching never deletes your
  Nintendo or Pretendo account.
- Press **SELECT** to enter the address: a hostname (`isle.example.org`) or an IPv4 address (`100.64.0.10`),
  up to 63 characters. It is saved to `/3ds/homestead/server.txt`.
- Every Nintendo host name (`*.nintendowifi.net`, `*.nintendo.net`, `*.pokemon-gl.com`) is sent to that one
  address. Your server tells the services apart by URL path, so no wildcard DNS is needed.
- On real hardware, Homestead fills your address into sysmodule IPS templates and installs them to
  `/luma/sysmodules` (anything already there is backed up once to `/3ds/homestead/ips-backup`).

> **Security:** like Nimbus, the SSL patch turns off certificate checking. Only connect to servers you trust,
> on networks you trust.

### Building

Same as Nimbus below, plus `3ds-jansson`. The app builds on its own (`make -C app`); the real-hardware
templates go in `app/romfs/homestead_ips/` once built. Host-side tests: `sh tests/run.sh`.

---

# Nimbus
## Pretendo account manager for the 3DS

## Usage

1. Grab the latest app and IPS patches from the [Releases](https://github.com/PretendoNetwork/nimbus/releases) page
2. Extract to the root of your 3DS SD card
3. Install the Nimbus homebrew using FBI (or FBI Reloaded) if using the CIA build
4. Run the Nimbus homebrew and select either to use a Pretendo or Nintendo account
     - If it doesn't work, reboot your 3DS while holding SELECT and ensure that "Enable loading external FIRMs and modules" and "Enable game patching" are both turned on, as well as ensuring that your Luma3DS version is 13.0 or higher.
5. Enable the Nimbus plugin by entering into the Rosalina menu and setting the "Plugin Loader" to "Enabled"

## Building

1. Clone the repository recursively using `git clone https://github.com/PretendoNetwork/nimbus --recursive`
    - If you have cloned the repository previously, please run `git pull` and `make clean` while in the nimbus folder to avoid errors and broken files
    - On top of that, if you cloned it before 1.0.2 released, you might also need to run `git submodule update --init --recursive` while in the nimbus folder
2. Install devkitARM, libctru 2.5.0 or later, `3ds-curl`, `3ds-mbedtls`, `3ds-zlib`, [CTRPluginFramework](https://gitlab.com/thepixellizeross/ctrpluginframework), [3gxtool](https://gitlab.com/thepixellizeross/3gxtool), [armips](https://github.com/Kingcom/armips), [makerom](https://github.com/3DSGuy/Project_CTR), [bannertool](https://github.com/Steveice10/bannertool) and [flips](https://github.com/Alcaro/Flips)
3. Copy [decompressed `code.bin`](https://github.com/PretendoNetwork/nimbus/blob/main/DECOMPRESSING.md) files from the act, friends, http, miiverse, socket and ssl sysmodules in their respective `patches` directories (any Miiverse code.bin works for the miiverse module)
4. Run `make`

## Credits

Thanks to:

- [pinklimes](https://github.com/gitlimes) for the CIA version banner
- [TraceEntertains](https://github.com/TraceEntertains) for making a CIA version of Nimbus and maintaining the project
- [DaniElectra](https://github.com/DaniElectra) for making the 3DS HTTP and Socket patches and maintaining the project
- [SciresM](https://github.com/SciresM) for making the 3DS SSL patches
- [zaksabeast](https://github.com/zaksabeast) for the original 3ds-Friend-Account-Manager and all the research into the friends and act system titles
- [shutterbug2000](https://github.com/shutterbug2000) for the GUI
- [libctru](https://github.com/devkitPro/libctru) for the `frda.c` base, homebrew template, and other library functions (and thanks to citro2d for part of a system font function)
- [Universal-Core](https://github.com/Universal-Team/Universal-Core) for the string drawing functions
- [Fangal-Airbag](https://github.com/Fangal-Airbag) for making the account switcher GUI support button controls
- All other 3DS researchers
