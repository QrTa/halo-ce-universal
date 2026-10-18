# PS5

`ninja ps5` builds the game for a jailbroken PlayStation 5, as a homebrew
title. This is the port's first phase (`PS5.md`, the plan): the build, the
deploy, and a host that loads the game and follows its start-up as far as
the first thing it asks of SDL. The game does not run yet.

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

## Game data and the log

The title's data folder is `/data/halo`, made over FTP. In this phase the
host only lists it. The log is `/data/halo/halo.log` (or `halo.log` in the
title's folder when there is no `/data/halo`), and every line goes to the
kernel log too, prefixed `halo: `.
