# PS5

`ninja ps5` builds the game for a jailbroken PlayStation 5, as a homebrew
title. This is the port's fourth phase (`PS5.md`, the plan): the game is on
the TV, drawn by the Android port's Vulkan renderer on RADV, with the pad
and its sound. From a data folder holding only the game's disc image it
extracts its maps (with nothing on the screen yet: phase 5), then plays the
menus and the campaign at 60 frames a second, or 120 on a TV that takes it;
the pad and the sound go through pacbrew's SDL2.

The game is the x32 guest image of `ninja linux_x86_64`
(`port/linux/README.md`, "64-bit x86"): the decompilation compiled as 32-bit
code for x86-64, with the Xbox window movable (`HALO_MOVABLE_WINDOW`: the
console loads a system module at `0x80000000`) and the Vulkan renderer
(`port/android/VULKAN.md`). The host is a PS5 title built with PS5_Vulkan's
toolchain, with RADV (the Vulkan driver), glslang and pacbrew's SDL2 in its
link. `PS5_FINDINGS.md` at the repository's root is what the probes found
about the console and the toolchain.

## Requirements

- The tools of `ninja linux_x86_64` (Python, ninja, a clang with the x32
  target, the OpenGL headers); `--ps5-guest-cc` selects the clang, which is
  the host's clang 22 by default.
- PS5_Vulkan (github.com/mpereiraesaa/PS5_Vulkan) at the commit
  `tools/ps5_build.py` pins, set up by its
  `tools/setup-native-dependencies.sh`, which puts its payload SDK fork and
  zlib under `.deps/native`. `configure.py` looks for it in `--ps5-vulkan`,
  then `PS5_VULKAN`, then `~/projects/ps5-deps/PS5_Vulkan`.
- PS5_Mesa (github.com/mpereiraesaa/PS5_Mesa) beside it (`--ps5-mesa`), and
  RADV's release archive built from it into PS5_Vulkan's
  `.deps/native/radv-release`: `port/ps5/probe/vk/build.sh` builds it. That
  needs meson 1.4 or newer, LLVM 18 with libclc and the SPIR-V translator,
  and glslang, which `--ps5-hostdeps` (default
  `~/projects/ps5-deps/hostdeps/env.sh`) puts on the path.
- Clang 22 for the host (`--ps5-llvm-config`, default
  `/usr/bin/llvm-config-22`), as RADV's archive is built with it.
- CMake, and a network connection for the first `configure.py --ps5`: it
  downloads musl and SDL3 to `build/ps5/third_party`, glslang (or takes the
  Android build's clone), and pacbrew's SDL2 (346 MB, once, into
  PS5_Vulkan's `.deps/pacbrew`).

## Building

1. Go to the root folder of the repository.
2. Enter `python3 configure.py --ps5`, with the options of your other builds.
3. Enter `ninja ps5`.

The title is `build/ps5/title/PPSA99500`: `eboot.bin` (the host),
`sce_sys/param.json` and `icon0.png`, `sce_module/libc.prx` (PS5_Vulkan's),
`halo_guest.elf` (the game) and `NOTICE.txt`, which names what the title is
built from: PS5_Vulkan, its SDK fork, PS5_Mesa, pacbrew and glslang, at
their commits. PS5_Vulkan and its fork are GPL-3.0-or-later, so the title
is a GPL work as a whole (glslang has its own licences, named in its
repository). The
host's ELF with its symbols, to look a crash's addresses up in, is
`build/ps5/host/eboot.elf`.

## Installing and running

The console needs etaHEN, ShadowMountPlus (which lists the folders under
`/data/homebrew` on the home screen), ftpsrv on port 2121 and klogsrv on
port 3232. To start the title from the PC, PS5_Vulkan's `ps5vkctl` payload
must also be resident (port 9111); load it once after each boot of the
console, with the payload manager (itsPLK's ps5-payload-manager, port 8084)
running:

```
python3 tools/ps5_deploy.py load-ps5vkctl
```

Then one command installs the title, starts it, follows its log and fetches
its files:

```
python3 tools/ps5_deploy.py --run
```

The run's files go to `build/ps5/logs/<time>`: `halo.log` and `klog.txt`,
the kernel log of the run. `--host` names another console;
`tools/ps5_deploy.py --help` lists the actions one at a time (`upload`,
`launch`, `kill`, `status`, `follow`, `fetch`).

## Game data

The game's data folder is `/data/halo`; make it over FTP and put the game's
Xbox disc image (`.iso` or `.xiso`) in it. On the first start the game finds
no `maps` folder there and extracts it from the image (about 1.8 GB, half a
minute): the desktop game asks whether to and which image, and on the PS5
the host answers "Yes" and the first `.iso` or `.xiso` in `/data/halo`.
After that the folder holds:

- `maps/`, the extracted maps (the image can then be removed);
- `config.toml`, the game's settings, written with the defaults on the
  first start (`port/linux/README.md` describes them);
- `save/`, the saves;
- `debug.txt`, the game's own log, and `halo.log`, the host's.

## The log

The host's log is `/data/halo/halo.log` (or `halo.log` in the title's folder
when there is no `/data/halo`), and every line goes to the kernel log too,
prefixed `halo: `. It has the ranges of memory the game was given, the
game's own output (`game: `), the files the extraction writes, the maps
opened, and every 5 s a heartbeat: the frames swapped, their mean and
longest time, and every minute the host's heap and the console's free
direct memory. When the game ends it lists the system calls it asked for
and did not get, the OpenGL names it called, and the memory figures, then
`the host ends`. After that the kernel log reports a `SIGSYS` for the
title: every PS5 title that exits gets one (`PS5.md`, phase 0 B), and it
means nothing more.

A crash is reported in the log with the signal, the registers and the
return addresses of the stack; an address in the game is looked up with
`addr2line -e build/ps5/halo_guest_vk.elf`, one in the host in
`build/ps5/host/eboot.elf.map`.

## The picture

The game draws through the Vulkan renderer of the Android port
(`port/android/VULKAN.md`), whose host half is compiled for the console as it
is, on RADV, the Vulkan driver PS5_Vulkan builds from PS5_Mesa, linked into
the title. The log's `renderer:` line names it; a console where it does not
come up ends the title (there is no other renderer), saying why.

The game draws at 1920x1080 and the picture is scaled to the console's
display, 3840x2160, at 59.94 Hz, or at 119.88 Hz: the title declares
high-frame-rate output (`param.json`), and RADV takes the 120 Hz mode when
the console's output does (Settings, Screen and Video, Video Output, Enable
120 Hz Output, with a TV that takes it). RADV checks the switch once and
sometimes logs `59.94 Hz restored` while the display goes on at 120 Hz; the
heartbeat's frames a second say which it is. The system's menu may bring
the display back to 59.94 Hz for the rest of the run.

The renderer compiles the game's shaders on a thread of its own the first
time it meets them, and keeps them, with its pipelines, in
`/data/halo/vk_cache`; RADV keeps its own in `/data/halo/radv-shader-cache`.
A second start compiles nothing it has met before. Removing either folder
only makes the next start compile again.

For testing, through `environment.txt` (`tools/ps5_deploy.py --env`):

- `HALO_VK_SELF_TEST=1`: the renderer's and the game's self-tests at start
  (`clears self-test: ok`, `data self-test: ... ok`);
- `HALO_VK_PRESENT_MARKER=1`: a red and a green square at the picture's top
  corners, to see which way up it is;
- `HALO_GPU_STATS=1`: the draws and what skipped them, every second;
- `HALO_PS5_SCREENSHOT=<seconds>`: the picture as it went to the TV, written
  that often to `/data/halo/screenshots` (the last 20 kept);
  `tools/ps5_deploy.py`'s `fetch` brings them to the run's folder as PNG
  and deletes them from the console.

RADV's own variables (`RADV_DEBUG`, `RADV_PERFTEST`, `MESA_VK_*`) reach the
driver as they are; `MESA_VK_ABORT_ON_DEVICE_LOSS=1` stops at a GPU fault
with Mesa's message in the kernel log, where RADV's lines go, prefixed
`[radv]`. The game's own frame counter is a console command: a line
`display_framerate 1` in `/data/halo/init.txt`.

## The pad and the sound

The game's pads are SDL2's game controllers: the DualSense, listed under
the signed-in user's name, as a PS5 controller. The console also lists its
own "PS5 Remote Control" as a controller; the game is never given it, and
the log says so (`gamepads: ... left out`). A pad switched off and on is
listed again (`SDL: gamepad removed`, `added`). The pad rumbles through
SDL2. Every minute in which the pad was used, a `pad:` line lists the
buttons pressed, each stick's and trigger's range and the rumble asked for.

The sound is SDL2's device on the console's audio output: 48 kHz, float,
stereo, 1024 frames a callback (21.3 ms), the format the game asks for.
SDL2 calls for sound on a thread that cannot run the game, so each call is
handed to a thread that can. Every minute an `audio:` line gives the
callbacks, the gaps between them (one longer than two periods is late)
and the time the hand-off took; the end of the run gives the totals. No
microphone is used yet, so voice chat is off.

SDL2's video is never started: its PS5 driver crashes a title (it calls
the keyboard library, which a title does not load). The log's `SDL_Init`
line says `video driver none`.

The PS button's menu and the Quick Menu do not stop the game; the host
reads the system's flags and gives the game "focus lost" while either is
up, and "focus gained" after, as a desktop window would (`overlay:` lines).
The system also sets the flag for about 6 s after the title is launched,
so every run starts with the focus lost.

## What runs, and what does not yet

- Runs: the game's start-up, the first start's extraction, the menus and
  the campaign on the TV, paced to the display's 59.94 or 119.88 Hz;
  settings, saves and the game's files in `/data/halo`; the pad, its rumble
  and the sound through SDL2; the system's menus as focus events (the level
  goes on under them).
- Not yet: a picture during the first start's extraction, native 4K
  drawing, pausing under the system's menu, the microphone and voice chat
  (phase 5), the network (phase 6: the game starts with it down, so the
  network test's matches cannot be hosted), and the desktop's self-updater,
  which a console title does not have (it is updated by installing it
  again).

For the deploy's tests, two files in the title's folder change a run:
`hold.txt` (`--hold`) keeps the title for 120 s after the game ends, and
`environment.txt`, `NAME=VALUE` lines (`--env NAME=VALUE`, again for each;
an upload without `--env` removes the file), gives the game settings
through its environment (`HALO_EXIT_AFTER=60` ends it after a minute; the
settings' variables are in `port/linux/README.md`).
