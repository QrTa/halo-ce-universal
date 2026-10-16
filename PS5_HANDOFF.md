# PS5 port: handoff

What was found out about PS5 homebrew on 2026-10-16, before any work started on
this branch (`ps5`, from `main` at Build 101, `d271f7b32`). Nothing here has
been tried on a console. The projects' claims below are from their READMEs, not
checked against their code.

## Graphics: Vulkan exists, from the community

There is no Sony SDK in homebrew. Three community Vulkan drivers target the
PS5's GPU (RDNA 2 class, `gfx1013`), all for jailbroken consoles:

| project | Vulkan | state |
|---|---|---|
| [mpereiraesaa/PS5_Vulkan](https://github.com/mpereiraesaa/PS5_Vulkan) | reports 1.4 (RADV); conformanceVersion 0.0.0.0 | the most mature; Mesa's RADV with a PS5 winsys |
| [mpereiraesaa/ps5-vulkan](https://github.com/mpereiraesaa/ps5-vulkan) | reports 1.3, "non-conformant" | older, separate implementation |
| [Swordpdf/HB_Vulkan](https://github.com/Swordpdf/HB_Vulkan) | 1.0 | early; Mesa-derived, drives the console's AGC command API |

### PS5_Vulkan (RADV), the one to use

- Working on hardware: clears, depth test and write, blending,
  render-to-texture, 4x MSAA with resolve, compute and transfer, and a
  swapchain through `VK_KHR_display` on the console's VideoOut (flips at vblank,
  120 Hz where the display allows).
- Conformance: a first full CTS run on the console passed 1,098,388 of
  2,786,062 cases with 12,005 failures; a second run was in progress.
- Users: vkQuake (about 119.88 fps at 4K), PS5 RetroArch (PPSSPP, Dolphin,
  LRPS2 cores; Dolphin stutters while it compiles ubershaders on first run),
  ProsperoEden (a Switch emulator port, Vulkan by default).
- Known gaps that matter to this game:
  - occlusion queries are coarse (`occlusionQueryPrecise` false): the game's
    lens flares and sun glow use visibility counts, so they would have to cope
    with imprecise ones;
  - line lists do not draw correctly on hardware (check what the game draws
    with lines: debug geometry, possibly the motion tracker);
  - no buffer device address; no concurrent queues.
- The driver is linked into the program: the console refuses to load a graphics
  library at run time (`dlopen`). Linking is through its `tools/radv-link.sh`.
- License: GPL-3.0-or-later. A PS5 build linking it is a GPL work as a whole.
  This repository is CC0, which allows that; the PS5 build's source must be
  published with it, as it already is.

### ps5-vulkan (older)

Runtime SPIR-V compilation, native 1080p presentation, no loader/ICD; support
"deliberately bounded" to some formats, stages and resource shapes. Also
GPL-3.0-or-later (static `libps5vk.a`).

### OpenGL ES

No standalone GL ES SDK was found for PS5 homebrew. PS5_Vulkan's build mentions
a "PS5 OpenGL SDK 0.3.0", only as where its shader compiler comes from.

## Toolchain and console setup

- [ps5-payload-dev SDK](https://github.com/Cryptogenic/sdk) (PS5_Vulkan pins
  v0.42) with its own clang 18 and lld (`prospero-clang`). PS5_Vulkan's author
  keeps a fork, PS5_PayloadSDK, for its shared platform layer, and builds Mesa
  26.2.0 on an Arch/CachyOS host (`make doctor` checks the setup).
- The console: jailbroken, with a homebrew environment (PS5_Vulkan was
  validated with etaHEN, ShadowMountPlus, ftpsrv on port 2121 and klogsrv on
  port 3232). Homebrew runs as a 64-bit ELF payload loaded into a process by an
  ELF loader. Supported firmware versions are not stated in what was read.

## The CPU side is the bigger job

The PS5 is x86-64 (Zen 2) running a FreeBSD-based system. The game is a 32-bit
program, and the port runs it three ways today:

- Windows and Linux: a plain 32-bit x86 program (`i686-pc-windows-msvc`,
  `tools/windows_build.py`; the Linux build likewise). On 64-bit Windows it
  runs under WOW64; the port does nothing special.
- Android and the Switch: no 32-bit userland, so a guest/host split. The game
  (the guest) is built for arm64_32 (64-bit ARM instructions, 32-bit
  pointers); a 64-bit host keeps its memory below 4 GB, widens its pointers on
  the way to the system and answers its system calls
  (`port/switch/host/host_syscall.c`, `host_memory.c`, `host_thread.c`,
  `host_loader.c`).

WOW64's way (a 32-bit code segment, 32-bit system calls) needs the kernel's
help: FreeBSD's `COMPAT_FREEBSD32`. Nothing found says the PS5's kernel has it,
homebrew is 64-bit only as far as is known, and there are no 32-bit system
libraries. Not confirmed; it would take a test on a console.

The workable route is the Switch's: x86's arm64_32 is the x32 ABI (64-bit x86
instructions, 32-bit pointers), which runs in ordinary 64-bit mode and needs
nothing from the kernel. Upstream's open PR #359 ("64-bit x86 Linux build as an
x32 guest", `ninja linux_x86_64`, OpenCommunityEdition/OpenCE) builds exactly
that guest. A PS5 host would do what the Switch's does: reserve the guest's
memory below 4 GB, widen pointers, answer system calls, with the payload SDK in
place of libnx.

## What to reuse

- Renderer: the Android port's Vulkan renderer is the closest start
  (`port/android/guest/d3d8_vk.c` and the host side `port/android/host/host_vk*.c`).
  It would present through `VK_KHR_display` on VideoOut instead of Android's
  surface, and its shader cache would have to suit runtime compilation on the
  console.
- Host: `port/switch/host` is the model for a non-Linux host (system calls,
  threads, futexes, memory, files, the map cache kept in memory:
  `host_memory_files.c`). The Android host is the other reference.
- Build: `tools/switch_build.py` shows how a second guest/host target was made
  from the Android one.

## Open questions, in the order to answer them

1. Can a PS5 homebrew process map memory below 4 GB at fixed addresses, and run
   code there? **Yes** (2026-10-16, `port/ps5/probe/mem`, two runs on a
   console reporting firmware 0x13600007, payload sent to the ELF loader on
   port 9021):
   - every megabyte from 64 KB to 4 GB maps, but for what the loader placed
     there; the image (0x3c000000), Custom Edition's tag cache, the Xbox
     window at 0x80000000 (128 MB and 512 MB), a 256 MB pool and 1 GB from
     0x40000000 all map whole; an address hint without MAP_FIXED is taken;
   - code there runs whichever way it is made executable (PROT_EXEC at
     mmap, mprotect, a shared memory object mapped twice, kernel_mprotect),
     stays readable, loads through 32-bit pointers, jumps to the host's code
     above 4 GB and runs on a stack below 4 GB; a pthread runs on a stack
     below 4 GB. (The payload has the loader's privileges: it was itself
     loaded into rwx anonymous memory.)
   - MAP_EXCL is ignored (it mapped over an existing page) and MAP_32BIT
     places memory above 4 GB. The host must read the map
     (sysctl KERN_PROC_VMMAP) before every fixed mapping.
   - about 1.5 MB of rwx memory from the loader sits below 4 GB at an address
     that moves between runs (0x778c8000, then 0x39664000). It could land in
     the guest's ranges, so the host has to check for it at start-up.
2. Does PR #359's x32 guest build with a toolchain usable for the PS5, and can
   the payload SDK's clang link a host that loads it?
3. How much of `port/switch/host` ports to the payload SDK's libc and FreeBSD
   system calls (threads, futex equivalent `_umtx_op`, mmap, sockets, files)?
4. Input, audio and networking on the payload SDK: what the RetroArch and
   ProsperoEden ports use.
5. Which firmware versions the jailbreak and the Vulkan driver support.

## Sources

- https://github.com/mpereiraesaa/PS5_Vulkan
- https://github.com/mpereiraesaa/ps5-vulkan
- https://github.com/mpereiraesaa/ps5-vulkan/pull/4 (runtime vertex/fragment compilation)
- https://github.com/Swordpdf/HB_Vulkan
- https://github.com/mihawk-99/PS5_RetroArch
- https://github.com/griches/PS5_Dusklight (RADV, Dawn WebGPU, SDL3)
- https://github.com/leolobato/playden-ps5 (Windows games via DXVK/Zink on Vulkan)
- https://github.com/Cryptogenic/sdk (ps5-payload SDK)
- https://github.com/ymatrix1/PortPS5/wiki
