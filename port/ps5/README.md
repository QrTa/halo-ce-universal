# PS5

`ninja ps5` builds the game for a jailbroken PlayStation 5, as a homebrew
title. This is the port's second phase (`PS5.md`, the plan): the game runs
headless. From a data folder holding only the game's disc image it extracts
its maps, loads the main menu and runs its loop at 60 frames a second, with
nothing on the TV, no pad and no sound yet (the pad and sound are phase 3,
the picture phase 4).

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

## What runs, and what does not yet

- Runs: the game's start-up, the first start's extraction, the main
  menu's map and its loop, paced to the display's 59.94 Hz; settings,
  saves and the game's files in `/data/halo`; the game's sound mixer, on a
  device's clock with nothing played.
- Not yet: the picture (every draw is dropped until the Vulkan renderer is
  brought up, phase 4), the pad and sound (phase 3), the network (phase 6:
  the game starts with it down), and the desktop's self-updater, which a
  console title does not have (it is updated by installing it again).

For the deploy's tests, two files in the title's folder change a run:
`hold.txt` (`--hold`) keeps the title for 120 s after the game ends, and
`environment.txt`, `NAME=VALUE` lines, gives the game settings through its
environment (`HALO_EXIT_AFTER=60` ends it after a minute; the settings'
variables are in `port/linux/README.md`).
