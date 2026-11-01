# C64 Mesh

A Commodore 64 running on a **Heltec WiFi LoRa 32 V3** (the Meshtastic board), played in any
**web browser** on your Wi‑Fi, with **SID sound**. Works on a PC, Mac or iPhone; nothing to install.

- 6502 CPU (passes the Klaus Dormann functional test), VIC-II text/bitmap/multicolour modes,
  sprites with collisions, raster interrupts, both CIAs (timers, keyboard, joystick, clock)
- SID sound: 3 voices, triangle/saw/pulse/noise, ring mod and sync, real ADSR timing, filter,
  and the `$D418` volume trick used for sampled speech/drums
- The board serves the C64 as a web page: screen and sound stream over a WebSocket
- Your PC keyboard, plus an on-screen C64 keyboard and touch joystick for phones and tablets
- **Your own ROMs**: upload BASIC / KERNAL / CHARACTER ROM files from the settings page
- Virtual disk on the board's flash: `LOAD"$",8`, `LOAD"GAME",8,1`, `SAVE"PROG",8`
- OLED shows the address to open (or a mini view of the C64 screen)

![Typing in the browser and listing the virtual disk](docs/browser-directory.png)
![Raster split and sprite test](docs/raster-sprite-test.png)

> Flashing this replaces Meshtastic. To go back, reflash Meshtastic from flasher.meshtastic.org.

## 1. Flash it

**Easiest:** open the **[C64 Mesh Flasher](https://hardwaremack-prog.github.io/c64mesh/flasher/)** in
Chrome or Edge on a computer, plug the board in with USB-C, click **INSTALL C64 MESH**, pick its
port and click Install. Tick *Erase* the first time; leave it unticked when updating so your ROMs and
disk files are kept. Press the board's RST button when it's done.

**Or with esptool:**
```
esptool --chip esp32s3 write-flash 0x0 firmware/C64Mesh-HeltecV3.bin
```
If the board doesn't show up, hold **PRG**, tap **RST**, release **PRG**, then try again.

Re-flashing an update keeps your uploaded ROMs and disk files (unless you choose Erase).

## 2. Get it on Wi-Fi

First boot, the board makes a hotspot: **C64-Setup**, password **commodore64**.
Join it and open **http://192.168.4.1/settings**, scan/pick your network under *Wi-Fi*, save.
The board reboots onto your network and the OLED shows its address.

## 3. Play

Open **http://\<board-ip\>/** (or **http://c64.local/**) in a browser and tap/click the screen
once; browsers only allow sound after a tap. One viewer at a time; opening it somewhere else
takes over.

Toolbar: 🔊 sound on/off · ⌨ on-screen C64 keyboard · 🎮 touch joystick · Port 1/2 (which
joystick port games read) · 📋 type or paste text · ↻ reset · ⛶ full screen · ⚙ settings.

| PC key | C64 key |
|---|---|
| Esc | RUN/STOP |
| Page Up | RESTORE |
| Tab or Left Ctrl | CTRL |
| Alt / Windows / Cmd | C= (Commodore) |
| Home | CLR/HOME |
| Insert / Delete, Backspace | INST / DEL |
| Arrows | cursor keys |
| F1–F8 | F1–F8 |
| `\` or `£` | £ ; `_` or `` ` `` = ←, `^` = ↑ |
| Numpad 8/2/4/6 (7/9/1/3 diagonals), Numpad 0/5 or Right Ctrl | Joystick |

Typing is symbolic: type `"` and the C64 gets SHIFT+2. Pasting (Ctrl+V / Cmd+V) types the text
into the C64, handy for BASIC listings. No numpad on your laptop? In the 📋 dialog tick
**Arrow keys + Space = joystick**.

On an iPhone the on-screen keyboard opens automatically: SHIFT, C= and CTRL latch until the
next key. Tip: Share → *Add to Home Screen* gives it a full-screen app icon.

## 4. Use your own ROMs

Open **⚙ Settings** → **ROMs**. For each slot pick your file and click **Upload**:

| Slot | Size | Usual chip |
|---|---|---|
| BASIC | 8192 bytes | 901226-01 |
| KERNAL | 8192 bytes | 901227-03 (or -01/-02, JiffyDOS works too) |
| CHARACTER | 4096 bytes | 901225-01 |

A 16 KB BASIC+KERNAL combo file is accepted in either slot and split automatically. Known
Commodore dumps are recognised by checksum. Then set each slot to **My ROM file** and click
**Apply & restart C64**. Switch back to the built-in set any time.

The built-in fallback is the free **Open ROMs** (MEGA65 project, LGPL-3). Its KERNAL boots and
loads programs, but its BASIC is unfinished (no maths: `PRINT 2+2` gives NOT IMPLEMENTED),
so your real ROMs are the full experience.

You can also bake different fallback ROMs into the firmware:
`python3 tools/make_roms.py basic.bin kernal.bin chargen.bin C64Mesh/builtin_roms.h` and rebuild.

## 5. Programs and the virtual disk

- Settings → **Virtual disk** → upload a `.prg` (tick *load & run now* to start it straight away)
- On the C64: `LOAD"$",8` then `LIST` for the directory, `LOAD"NAME",8,1` then `RUN`
- `SAVE"NAME",8` saves your BASIC program to the board; download it from the settings page
- Single-file `.prg` games work; programs that need a real 1541 (fast loaders, multi-load disk
  games, `.d64` images) don't, because there's no disk-drive emulation

## Board buttons and lights

- **PRG** tap: switch the OLED between status and a mini view of the C64 screen
- **PRG** hold 2 s: reset the C64
- White LED: on while a browser is connected

## Limitations

- Sound plays about 0.1 s behind the picture (network buffering).
- On iPhone, if there's no sound, check the ringer switch and volume.
- PAL only. Not cycle-exact: BASIC and ordinary games run fine; timing-critical demos won't.
- The SID is a compact emulation: it sounds right, but isn't a sample-exact reSID.
- If the ESP32 falls behind, it draws fewer frames to keep the C64 and its sound at full speed.
  The status line shows the frame rate; Settings shows the speed.

## Build from source

Arduino IDE with the **esp32** core 3.x, board **Heltec WiFi LoRa 32(V3)**. Open
`C64Mesh/C64Mesh.ino`; `partitions.csv` in the sketch folder sets up a 3 MB app + ~4.8 MB of
storage. No extra libraries needed.

| File | What it is |
|---|---|
| `c64.*` | CPU, memory, VIC-II, CIAs, LOAD/SAVE traps |
| `sid.*` | SID sound chip |
| `stream.*` | WebSocket server: screen tiles, sound, keys |
| `keymap.*` | PC / on-screen keys to the C64 keyboard matrix and joystick |
| `player.h` | the browser page that is the C64 |
| `webui.h` | settings page (ROMs, disk, Wi-Fi) |

`host/` has a PC build of the same emulator and stream code, with tests:
```
cd host
g++ -O2 -std=c++17 -o c64host hostmain.cpp ../C64Mesh/c64.cpp ../C64Mesh/sid.cpp ../C64Mesh/keymap.cpp ../C64Mesh/stream.cpp -lpthread
./c64host --disk disk                          # stream on ws://localhost:8181
python3 test_stream.py video                   # screen, typing, paste
./c64host --frames 400 --sys examples/A440.prg --wav out.wav   # headless run, record the SID to a WAV
```

## Licences

C64 Mesh code: yours to use. Built-in Open ROMs: © Paul Gardner-Stephen, Roman Standzikowski
and contributors, LGPL-3 (see `roms/LICENSE-OpenROMs-LGPL3.txt`; source at
github.com/MEGA65/open-roms). Commodore ROMs are not included.
