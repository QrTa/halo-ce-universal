# PS5 port — plan

The game on a jailbroken PlayStation 5, as a homebrew title: the x32 guest of
`ninja linux_x86_64` run by a host built with PS5_Vulkan's toolchain, drawing
through the Android port's Vulkan renderer on RADV. It follows the plans of
the Switch's deko3d renderer (`port/switch/DEKO3D.md`) and Android's Vulkan
renderer (`port/android/VULKAN.md`), which went from nothing to a working
backend in phases each tried on the device before the next. Read "How this
plan is worked" there before this.

What the console is, what the probes measured and the toolchain's traps are
in `PS5_FINDINGS.md` (the record) and `PS5_HANDOFF.md` (the summary, written
before this plan; its "Plan: the host" is superseded by the phases below).

Status: phase 0, part A is done (the probes: memory, the title, pad, audio,
RADV, the guest's CPU side up to its first SDL call; `PS5_FINDINGS.md`).
Phase 0, part B is done and reviewed (the host, SDL2 and glslang probes,
"Progress"); every "(to confirm)" row of "Decisions" is settled. Phase 1 is
done and reviewed: `ninja ps5 && python3 tools/ps5_deploy.py --run` builds,
installs and starts the title, and its log shows the game's start-up to the
first SDL call. Phase 2 is done and
reviewed: the game runs headless from a data folder holding only the disc
image, through the extraction, to the main menu's map at 59.94 frames a
second ("Progress"). Phase 3's code is done and reviewed (the pad, the sound
and the system's overlay over SDL2); its runs with a person at the console
are moved to the end of phase 4, to be made with the picture. Phase 4 is
written out in detail and ready to be handed over. The probes are in
`port/ps5/probe`.

---

## Why

The PS5 is an x86-64 machine with a Vulkan driver (RADV, through PS5_Vulkan)
and 12 GB of memory the title can use, so it can run the game the way the
Switch and Android do - a 32-bit guest in a 64-bit host - with the strongest
GPU and CPU of the ports. What stands in the way is not the hardware but the
system: no Linux kernel (FreeBSD's, with Sony's calls), no SDL3, no 32-bit
userland, a memory budget split between flexible and direct memory, a
display that only presents through `VK_KHR_display`, and a toolchain that
is someone else's and GPL.

The probes answered the questions that could have stopped the port:

- the guest's memory maps below 4 GB at fixed addresses, code runs there,
  threads run on stacks there (as a payload and as a title);
- the x32 image loads where it is linked, with its window moved to
  0x90000000, and runs musl's start, its window reservation and its
  allocator before it asks for SDL;
- the pad, audio at 48 kHz float and RADV (102 of 102 checks, a 4K display
  at 59.94 Hz) all work on this firmware (13.59).

What is left is engineering: a host, an SDL, a build, and the renderer
brought up on a new driver.

---

## Decisions

Rows marked **(to confirm)** are proposals for the user; phase 0, part B
settles the ones it names.

| Question | Decision |
|---|---|
| Payload or title | **A title** (`eboot.bin`, `sce_sys/param.json`, `sce_module/libc.prx` under `/data/homebrew/<ID>/`): VideoOut, the GPU, the pad and audio are a title's. Settled by the probes. |
| The guest | **The x32 image of `ninja linux_x86_64`**, with the desktop's code paths (no `HALO_ANDROID`), the movable window (`HALO_MOVABLE_WINDOW`, `--linux-x86-64-movable-window`) and **the Vulkan renderer** (`generate_guest_image(..., vulkan=True)`: `d3d8_vk.c` and its files in place of `d3d8_gl.c`). One image: the console has no OpenGL, so there is no GL fallback and no `display.renderer`. |
| Where the guest's memory goes | The image at 0x70000000 (64 MB, read, write and execute), the Xbox window of 512 MB at 0x80000000 if free, else at the next 256 MB boundary where it fits (0x90000000 on this console, where `libc.prx` holds 0x80000000), Custom Edition's range at 0x40440000, and pools for the guest's other mappings: **all direct memory**, mapped fixed and zeroed by the host (`sceKernelAllocateDirectMemory`, `sceKernelMapDirectMemory`, `sceKernelMprotect` for execute). Direct memory is also the GPU's: RADV's winsys takes `vkAllocateMemory`'s memory from it (PS5_Vulkan's README), and so is the host's own `malloc`: the RADV link recipe wraps the allocator into the platform layer's heap, which maps direct memory and never gives it back (phase 0 B: 8 GB of `malloc` freed left the heap at 8 GB; glslang's 32 MB stayed after `glslang_finalize_process`). So **one budget of 12 GB** holds the guest's ranges (855 MB), RADV's device and host-visible allocations (size for size), glslang and the host's heap at its peak. The 448 MB of flexible memory are unused (417 MB free throughout the probes) and are not counted on. The host watches the heap's peak, not flexible memory. |
| The host | **The Android host library** (`port/android/host`) with the Linux host's desktop half (`port/linux/host`: the guest is the desktop build), and the PS5's own files in place of the ones that touch the system: entry point, memory, system calls, futexes, SDL, the Vulkan driver and presentation. Built as a title with PS5_Vulkan's toolchain. |
| Shared code | **Used as it is, not copied** (the user's decision, 2026-10-17): the Android host files and the Vulkan renderer (`host_vk*.c`, `d3d8_vk.c` and its files) are the same files on Android, Linux and the PS5. A PS5 difference goes behind `HALO_PS5` (host) or into a PS5 file that replaces a shared one. Android and Linux must keep working: their tests, and the phone when a change touches what it runs (ask before using it). The deko3d and Vulkan plans copied because they forked a renderer; here the renderer is the one being reused, and copying 7,000 lines of it still under repair would make two to fix. |
| SDL | **Pacbrew's SDL2 (route 1) for the pad, audio, events and timers, never its video**, answered the Switch's way (`host_sdl2.c`, `host_sdl3_events.c` as the model) under the guest's 69 SDL3 calls; the window, the GL context, the display modes, the 2D renderer, dialogs and message boxes are stand-ins for a fixed console. Why: phase 0 B's SDL2 probe ran pacbrew v0.40.2's SDL 2.30.12 in a PS5_Vulkan title, and the pad (every button and axis, rumble), audio (48 kHz float, callbacks 5.3 ms apart, `SDL_QueueAudio`) and the clocks all worked, as long as two conditions hold. It links only with the RADV link recipe's libc bindings plus a `___mb_cur_max` of the host's own, because its libiconv wants two names no system module exports. And `SDL_INIT_VIDEO` must never be called: it jumps through the unloaded `sceKeyboardInit` (see "Progress"). |
| Shader compiler | **glslang on the console**, glslang 16.6.0 built static with the SDK fork's CMake toolchain (its default clang 18) and linked into the title with the RADV link recipe, called as `host_vk_glslang_compile` calls it; RADV's own shader and pipeline caches are kept too. Why: the renderer generates GLSL from the game's shader keys at run time, and on the console glslang compiles the renderer's six probe shaders in 1.3 to 7.1 ms each (the first compile of all, cold, 161 ms) into SPIR-V that `spirv-val` passes. A shipped corpus would leave keys undrawn. |
| Texture write tracking | **`host_memory_watch_*`'s page protection and `SIGSEGV`**, with libc's `mprotect` on the window's direct memory (`sceKernelMprotect` behaves the same) and the handler on a `sigaltstack`. Why: on a thread whose stack is guest memory, a title gets the write fault with its full context (`si_code` 2, `si_addr` and `mc_addr` the address written, `mc_err` 7, `mc_trapno` 12), on the alternate stack, and the write completes after the handler unprotects the page; a protect, fault and unprotect cycle costs 63 µs a page; a read of unmapped memory comes with `si_code` 1, so `host_debug.c` can tell a crash from a watched page. |
| Futexes | **`_umtx_op` with `UMTX_OP_WAIT_UINT_PRIVATE` (15) and `UMTX_OP_WAKE_PRIVATE` (16)**, the guest's futex operations mapped onto them as the table under "Progress" sets out. `UMTX_OP_WAIT` (2) compares 64 bits and is never used on the guest's 32-bit words. Why: `libkernel` exports `_umtx_op` to a title. The 32-bit wait and wake work on a word in the guest's pool from threads whose stacks are guest memory, and a wake reaches its waiter in 5 to 6 µs. The relative and absolute timeouts end in `ETIMEDOUT` at 100 ms. The private forms behave as the shared ones do, and need no shared key. |
| A shader or pipeline not ready at a draw | **The draw is skipped** while it compiles on another thread, as the renderer already does. |
| Game data | **`/data/halo`**, a folder the user fills over FTP: the disc image, from which the first start extracts `maps/` as the desktop build does (`sdl_platform.c`, `xiso.c`). The desktop asks through a message box and a file picker; on the PS5 the host answers the message box "Yes" and the picker with the `.iso` or `.xiso` it finds in `/data/halo`, so the extraction runs with no change to the guest and no screen (phase 2), and phase 5 only adds a progress display. `config.toml`, `save/` and the caches are beside the maps. `/app0` (the title's folder) holds only the build. |
| Display | VideoOut's one mode, **3840x2160 at 59.94 Hz**, as the swapchain; the game renders at its `display` width (1080p by default) and the present blits and scales, as Android's does. Native 4K is a setting to try in phase 5. |
| Logs | **A file in the data folder** (`halo.log`, as the Switch) and the kernel log (`sceKernelDebugOutText`, read with klogsrv on port 3232); faults reported with their registers as the Linux host does. |
| Self-updater | **None**: `host_update_*` answer "unavailable", as a build without it. |
| Networking | **Last** (phase 6): FreeBSD sockets through the host's system calls, the Switch's `host_net.c` the model. |
| Licensing | PS5_Vulkan and its SDK fork are GPL-3.0-or-later; RADV comes from PS5_Mesa, whose licence (Mesa's, and PS5_Vulkan's additions) phase 7 reads and names. **Nothing of theirs is copied into this repository**; they are a build dependency at pinned commits, and the PS5 build is a GPL work as a whole. Its source is this repository **and** those three at their pinned commits: a released binary ships with a notice naming each repository, its commit and where to get it, and the pins in `tools/ps5_build.py` are what the notice is generated from. |
| Testing | **The user's console** (192.168.2.157, system software 13.59) is what every phase is accepted on. Runs are started through **`ps5vkctl`** (PS5_Vulkan's resident payload, port 9111), which the user approved on 2026-10-17 after phase 0 B's item 9 proved it. It is loaded once per console boot through the user's payload manager (itsPLK's, port 8084: `/manage:upload`, then `/loadpayload:`), since the console's ELF loader on 9021 only takes local connections. Phase 1's deploy uploads, then launches and kills through it. A run that needs the user's hands (the pad, the PS button) is still announced first, so the user is at the console. |

---

## Where the port lives

The game is 32-bit and the PS5 has no 32-bit userland, so the port is the
Switch's arrangement on x86: the game is compiled as an x32 guest (64-bit
instructions, 32-bit pointers, below 4 GB), and a 64-bit host loads it,
keeps its memory where its pointers can reach, answers its system calls and
its `host_*` imports, and owns everything that is the system's: the display,
the pad, audio, files, threads. Upstream PR #359 made that guest for Linux
(`tools/linux_guest_build.py`); the Linux host that runs it is the Android
host library plus `port/linux/host`. The PS5 host is that host again, with
its system layer replaced.

What is shared, what is the PS5's:

| Part | Where it comes from |
|---|---|
| The guest image | `tools/linux_guest_build.py`'s x32 target, with the movable window and the Vulkan renderer; the same objects as the Linux x86-64 build's, so `ninja linux_x86_64` keeps testing them on the PC |
| The guest's desktop half | `port/linux/host/guest_desktop.c`, as it is |
| Loading, threads, faults, the boot block, logging's shape | `port/android/host`: `host_loader.c`, `host_thread.c`, `host_debug.c`, `host_arch.c`, `host_common.c` |
| Guest memory bookkeeping | `host_memory.c`, over a PS5 `host_mman.c` that gives it `mmap`'s interface on direct memory, as the Switch's does over its services |
| The renderer, both halves | `port/android/guest/d3d8_vk.c` and its files; `port/android/host/host_vk*.c`, with the driver and presentation files replaced (RADV is linked in; the surface is a display plane) |
| SDL | a PS5 file, after the Switch's `host_sdl2.c` (phase 0 B chooses what is under it) |
| System calls, futexes, the entry point | PS5 files, after the Switch's |
| The build and the deploy | `tools/ps5_build.py`, `tools/ps5_deploy.py`, after `tools/switch_build.py` and `tools/switch_deploy.py`; the title packaging from `port/ps5/probe/title/build.sh` |

The rule from the other plans holds: **no command names a host address in a
32-bit field**, and the host reads guest memory directly (it is below 4 GB).

---

## Files

New, PS5 only (the names follow the Switch's; the detailed phases may
rename):

| File | After | What |
|---|---|---|
| `tools/ps5_build.py` | `tools/switch_build.py` | `ninja ps5`: the guest image, the host as a title (clang 22 through the SDK fork, RADV's archive, glslang), `param.json`, the icon, `libc.prx`; `configure.py --ps5` options for the PS5_Vulkan checkout |
| `tools/ps5_deploy.py` | `tools/switch_deploy.py`, `probe/title/deploy.sh` | upload over FTP (port 2121), `eboot.bin` last; start, stop and restart through `ps5vkctl`; fetch the log |
| `port/ps5/host/host_main.c` | `port/switch/host/host_main.c` | title start-up: `ps5_fp_ieee()`, the user service, the data folder, the environment, the log file and the kernel log, the game thread |
| `port/ps5/host/host_memory.c` | `port/android/host/host_memory.c`, as the Switch rewrote its own | the guest's ranges and pools as direct memory, the map read through `sceKernelVirtualQuery`, the write watch and the crash report as Android's; `__wrap_mmap` for the loader's one mapping |
| `port/ps5/host/host_syscall.c`, `host_syscall_tables.c`, `freebsd_abi.h` | `port/android/host/host_syscall.c` (the shape), `port/switch/host/host_syscall.c` (the inventory) | the guest's Linux system calls on FreeBSD's libc: files (`stat` layouts, open flags), clocks, `writev`, directories, exit; the pure translations in their own file, tested on the PC, FreeBSD's numbers as literals asserted against the SDK's headers |
| `port/ps5/host/host_futex.c` | `port/switch/host/host_futex.c` | futexes over `_umtx_op`, or over a mutex and condition variable per address |
| `port/ps5/host/host_sdl_ps5.c`, `host_sdl_ps5_input.c`, `host_sdl_ps5_audio.c` | `port/switch/host/host_sdl2.c`, `host_sdl3_events.c` | SDL3's 69 calls answered: `host_sdl_ps5.c` with SDL3's headers (the events written as SDL3's, the ticks, the stand-ins for the window, context, displays, 2D renderer and dialogs, and the checks of the SDL3 values the other two use); `_input.c` (start-up, the pad, its events, the system's overlay) and `_audio.c` (the device and the hand-off to a guest stack) with pacbrew's SDL2's, which cannot share a file with SDL3's (phase 3) |
| `port/ps5/host/host_vk_driver_ps5.c` | `port/android/host/host_vk_driver.c` | RADV linked in: its ICD entry point `vk_icdGetInstanceProcAddr` called directly (the archive has no `vkGetInstanceProcAddr`), the driver's name for the log |
| `port/android/host/host_vk_present.c` (a `HALO_PS5` branch, not a PS5 file: phase 4) | — | the surface through `VK_KHR_display` on VideoOut's one mode; the FIFO swapchain, the letterboxed blit and the re-creation are the shared code's |
| `port/ps5/host/host_vk_screenshot_ps5.c` | — | the presented image written to `/data/halo/screenshots` on request, for the audit's eyes |
| `port/ps5/host/host_update_none.c` | — | `host_update_*` unavailable |
| `port/ps5/host/host_vk_null.c` | — | **phases 2 and 3 only**: the renderer's nine imports answered so that every draw is dropped and nothing waits; deleted in phase 4 |
| `port/ps5/host/host_gl_none.c` | — | no OpenGL on the console: every `hostgl_*` name resolved to one counting no-op, the nine `host_gl_*` imports as no-ops |
| `port/ps5/host/host_debug_ps5.c` | `port/android/host/host_debug.c` | in its place: the thread id (`pthread_getthreadid_np`), each guest thread's floating point mode, the thread bookkeeping; the sampler and profiler answer "not on the PS5" (phase 2: the Linux ones are most of the file) |
| `port/ps5/host/host_posix_none.c` | `port/linux/src/posix_net.c`, `posix_upnp.c`, `posix_dtls.c` | **until phase 6**: `posix.h`'s network, UPnP, DTLS and Discord helpers answering as a machine whose network is down; `posix_files.c` is compiled as it is |
| `port/ps5/host/host_local.map` | — | the link's version script: the wrapped names (`mmap`, `SDL_ShowSimpleMessageBox`) kept local, since the title converter refuses an export |
| `port/ps5/tests/`, `tools/test_ps5_host.py` | `port/android/tests/`, `tools/test_host_units.py` | the PS5 host's pure parts tested on the PC |
| `port/ps5/host/host_bootstrap.c`, `host_bootstrap.S` | the guest probe's section 6 | **temporary** (phase 1): the ranges, the loader, the thunks and the trivial answers that prove the build pipeline; phase 2 deletes them |
| `port/ps5/host/host_libc_extra.c` | the SDL2 probe's `libc_extra.c` | what pacbrew's archives want of libc and no system module exports (`___mb_cur_max`) |
| `port/ps5/host/ps5_host.h` | — | what the PS5's own host files share: the log, and (phase 1) the bootstrap's entry |
| `tools/ps5_title.sh` | `port/ps5/probe/title/build.sh` | the title's link (`radv-link.sh` sourced, `prospero-lld`, `ps5-native-tool link`) and signing as a script the ninja rules call |
| `tools/test_ps5_build.py` | `tools/test_linux_guest_build.py` | the configure test: not asked for, unavailable with a reason, generated against a fake checkout |
| `port/ps5/sce_sys/param.json`, `icon0.png` | the probes' | a title ID of our own (PPSA and five digits), the name, the icon from the Android master |
| `port/ps5/README.md` | `port/switch/README.md` | requirements, building, installing, game data, settings, how the port operates |

Unchanged and shared: everything else of `port/android/host`,
`port/android/guest`, `port/linux/host/guest_desktop.c`, `port/linux/src`.
Changes to them go behind `HALO_PS5` and are argued for under "Progress".

---

## Phase 0 — the probes

Settles the unknowns before the bulk of the work. Part A is done; part B is
the questions the probes did not ask, each a small program in
`port/ps5/probe` or an extension of one there, its results in "Progress"
and the "(to confirm)" rows of "Decisions" settled before phase 1.

### Part A — the console, memory, pad, audio, RADV, the guest (done)

`port/ps5/probe/mem` (a payload), `io`, `vk`, `guest` (titles PPSA99101,
PPSA99014, PPSA99102) and `title/build.sh`, which builds a title. Their
reports are in `port/ps5/probe/results/`, read in `PS5_FINDINGS.md`. In
short: memory below 4 GB is the guest's at fixed addresses; code and
threads run there; the x32 image runs to its first SDL call; the pad,
audio and RADV work on 13.59; the window has to move to 0x90000000, and
the guest can now take it there.

### Part B — what the host will stand on

Written to be picked up by an agent that has not seen the work so far. Read
first, in this order: `PS5_FINDINGS.md` whole (the console, the toolchain's
traps, what a title's memory looks like, the three guest runs);
`port/ps5/probe/guest/main.c` (a title probe: direct memory at fixed
addresses, code and a thread below 4 GB, the fault handler and the report)
and `port/ps5/probe/io/main.c` (the pad and audio, with the declarations
written by hand from PS5 RetroArch's); `port/ps5/probe/title/build.sh` and
`deploy.sh`, and `probe_report.h`; then, in the PS5_Vulkan checkout
(`~/projects/ps5-deps/PS5_Vulkan`), `docs/PLATFORM_NOTES.md`,
`docs/PACBREW.md`, `docs/DEPLOYMENT.md` ("Autonomous run loop"),
`tools/radv-link.sh` and `tools/build-radv-title.sh` (how a title links
RADV), `payload/ps5vkctl/main.c` (its header comment) and
`tools/ps5_console.py`; and in this repository
`port/android/host/host_vk_shaders.c` (how the renderer drives glslang: the
calls and the input it builds), `port/android/probe/glslang/CMakeLists.txt`
(how glslang is built for Android), `port/switch/host/host_futex.c` (what a
futex answer has to cover) and `host_memory.c`'s watch (the page protection
and the handler, from `host_memory_watch_protect` down). The work is
reviewed against the acceptance list at the end of this part.

#### What this part is, and is not

- **It is** three probe titles, their build, their runs on the console,
  their reports in `port/ps5/probe/results/`, the answers written into
  "Progress", and the "(to confirm)" rows the probes decide rewritten in
  "Decisions".
- **It is not** the host, the build of phase 1 or anything under
  `port/android`, `port/linux`, `port/switch` or `source`. Nothing those
  trees hold changes. Nothing of PS5_Vulkan's or the SDK fork's is copied
  into this repository: their scripts are called or sourced where they are,
  and their code is read, not copied (it is GPL; this repository is CC0).
  A Vulkan sequence this part needs (an instance, a device, a swapchain) is
  written from the Vulkan specification, not from `radv_smoke.c`.
- **The console is the user's.** Titles are uploaded with `deploy.sh`; the
  user starts them and says when a run is done, unless the user has said
  `ps5vkctl` may start them (item 9, below). Nothing is put on the console
  but the three titles and what item 5 names. `/data/homebrew/PPSA99014`,
  `PPSA99101` and `PPSA99102` (part A's titles) stay.

#### Files

| File | What |
|---|---|
| `port/ps5/probe/host/main.c`, `param.json` | the host probe, title PPSA99103, "Halo PS5 host probe": sections 1 to 6 below (futexes, faults, the data folder, memory, the display, leaving and returning), RADV linked in |
| `port/ps5/probe/sdl2/main.c`, `param.json` | the SDL2 probe, title PPSA99104, "Halo PS5 SDL2 probe": item 1 |
| `port/ps5/probe/glslang/main.c`, `param.json`, `title-files.list` | the glslang probe, title PPSA99105, "Halo PS5 glslang probe": item 2; the list copies the six shaders of `port/android/probe/` into the title |
| `port/ps5/probe/title/build.sh` | extended: a probe folder may hold `build-extra.sh`, which `build.sh` sources after it has set its variables and before it compiles, and which may append to arrays `extra_cflags`, `extra_cxxflags`, `extra_objects` and `extra_link` (inputs and flags for `prospero-lld`) and may replace the linker script. That is how the host probe takes RADV's link recipe (`radv-link.sh`'s `radv_link_recipe`, sourced, as `build-radv-title.sh` does), the SDL2 probe takes pacbrew's `pkg-config --static` flags, and the glslang probe takes its static archives and libc++. The probes of part A build as before. |
| `port/ps5/probe/results/host-probe.txt`, `sdl2-probe.txt`, `glslang-probe.txt` | the reports, copied from `/data/homebrew/<ID>/` over FTP |
| `port/ps5/PS5.md` | "Progress" and "Decisions" |

Every probe uses `probe_report.c` (`report_open`, `say`, `heading`,
`notify`): one fact a line, numbers with their units, a result code in hex
with the call that gave it. A title that does not start is a result too:
the kernel log (klogsrv, `nc 192.168.2.157 3232`, read while the title is
started) names the import the loader refused; copy that line into the
report's place in "Progress".

**The crash guard**, in the host probe and the SDL2 probe (as
`vk_probe_skip.txt` on Android, `deko3d_probe_skip.txt` on the Switch):
before a section that may kill the process, write its name to
`/app0/<probe>-skip.txt`; delete the file when the section ends. At start,
if the file names a section, skip that section, say so in the report, and
go on. A run that crashes therefore leaves a report up to the crash and a
second run finishes the rest.

#### The host probe (PPSA99103), section by section

The title's memory first, as the guest probe's section 4 does and as the
host will: the image's 64 MB at 0x70000000 (read, write and execute), the
window's 512 MB at the first 256 MB boundary from 0x80000000 where it fits,
Custom Edition's 23 MB at 0x40440000, a 256 MB pool at 0x10000000, all
direct memory, mapped fixed and zeroed. Everything below runs with those
in place, because that is the state the host will be in. Reuse the guest
probe's code for it (copy `map_direct` and `reserve_exactly`; the probes
are not a library).

**1. Futexes** (`_umtx_op`, declared in the SDK's `sys/umtx.h` and
exported by `libkernel`; the link proves the export). On a `uint32_t` in
the pool (below 4 GB, as the guest's words are), with waiters on threads
whose stacks are in the pool (`pthread_attr_setstack`, as the guest
probe's section 5):

- `UMTX_OP_WAIT_UINT` when the word already differs from the value:
  what it returns, and in how many microseconds (musl's futex wait
  expects `EAGAIN`; the host translates whatever this is);
- one waiter in `UMTX_OP_WAIT_UINT`, the main thread changes the word and
  calls `UMTX_OP_WAKE` with 1: the waiter returns; the time from the wake
  to the waiter's return, ten times, min and median;
- a wait with a timeout: the relative `struct timespec` form (`uaddr` the
  size of the structure, `uaddr2` its address) of 100 ms on a word nobody
  changes: the return value and `errno` (`ETIMEDOUT` expected), and the
  time it took; then the `struct _umtx_time` form with `UMTX_ABSTIME`, if
  the header has it;
- four waiters, `UMTX_OP_WAKE` with `INT_MAX`: all four return, how many
  the call says it woke;
- the `_PRIVATE` forms (`UMTX_OP_WAIT_UINT_PRIVATE`, `UMTX_OP_WAKE_PRIVATE`)
  of the same, and whether they behave the same;
- `UMTX_OP_WAIT` (the 64-bit compare) on the same 32-bit word, once, to
  show what goes wrong when the word's neighbour is not zero: it is not to
  be used, and the report should say why in one line.

Write under "Progress" the table musl's `FUTEX_WAIT`, `FUTEX_WAKE`,
`FUTEX_WAIT_BITSET`, `FUTEX_WAKE_BITSET` and the timeout map onto, as the
Switch's `host_futex.c` documents its own.

**2. Faults.** A `sigaltstack` for the thread (64 KB from the title's own
`malloc`; the faulting thread's stack is guest memory), `sigaction` for
`SIGSEGV` and `SIGBUS` with `SA_SIGINFO | SA_ONSTACK`. Then, on a thread
whose stack is in the pool:

- `sceKernelMprotect` of one 16 KB page of the window to read-only (the
  page size a title reports; check `sysconf(_SC_PAGESIZE)` and say what it
  is); a write to it. The handler logs, field by field: the signal number,
  `si_code`, `si_addr`; from the `ucontext_t`'s `uc_mcontext`
  (`x86/ucontext.h` in the SDK): `mc_rip`, `mc_rsp`, `mc_rbp`, `mc_rax`,
  `mc_rdi`, `mc_err`, `mc_trapno`, `mc_addr`, `mc_flags`; and whether its
  own stack pointer is inside the alternate stack. It reprotects the page
  read-write and returns; the thread's write completes and the value is
  read back.
- The same with libc's `mprotect` in place of `sceKernelMprotect`, to
  know whether the two are one call on direct memory.
- The same on a page of flexible memory (an anonymous `mmap` of the title's
  own, above 4 GB), for comparison.
- 1,000 cycles of protect, fault, reprotect on one page, timed: the cost
  of the watch per written page.
- A read of an unmapped address below 4 GB (a page the probe never mapped,
  say 0x08000000), for the signal and `si_code` a plain fault gives,
  because `host_debug.c` has to tell a watch fault from a crash.

If no signal arrives (the process dies), the kernel log says so and the
crash guard leaves the section out next time: that answer is "no faults
for a title", and the texture tracking is `host_watch_hash.c`'s.

**3. The data folder.** The user makes `/data/halo` over FTP before the
run (an empty folder; the probe asks for nothing else). The probe, in
`/data/halo` and then the same in `/app0` for comparison:

- `stat` of the folder, `opendir`/`readdir` (names and types), `mkdir
  probe`, `chmod 0777` of it;
- write a 16 MB file in 1 MB `fwrite`s, `fflush`, `fsync`, time and MB/s;
  `chmod 0666`;
- write a 100 MB file the same way (the size of a map), time;
- read the 100 MB file in one `read` after `open`, and again in 64 KB
  `read`s, and again through `fread` with a 64 KB buffer: time and MB/s
  each, the first byte and the last compared with what was written;
- `stat` of the file: `st_size`, `st_mode`, `st_mtime` against the clock,
  `st_blksize`; `rename` to another name; `unlink`; `rmdir probe`;
- `access(..., F_OK | R_OK | W_OK)` on the folder and a file, and on a
  missing name (part A's smoke test found `access` answering on 13.59
  where PS5_Vulkan's notes say `EPERM` on 12.09);
- `realpath` of `/data/halo/probe`, and `getcwd`;
- whether `/download0` exists and is writable (`param.json` has
  `downloadDataSize`; PS5_Vulkan's notes name it as the writable place).

**If `/data/halo` is not visible to the title**, say so first thing in
"Progress": the "Game data" decision then has to change (to a folder beside
the title, or `/download0`), and that is the user's call. Leave the 100 MB
file in place only if the user asked for it; delete the probe's files
otherwise.

**4. The host's memory.** Before anything: `sceKernelAvailableFlexibleMemorySize`
and `sceKernelAvailableDirectMemorySize` (the largest free block, as the
guest probe prints it). After the guest's ranges are mapped: both again.
Then `malloc` of 16 MB at a time, each block written through (every
page), until `malloc` returns NULL or 8 GB are taken, whichever first:
the total, and both figures after; free it all; both figures again.
Then Vulkan, through RADV linked in (`vkGetInstanceProcAddr` is a plain
symbol of the archive; no loader, no `dlopen`): an instance, the one
physical device, its name and `VkPhysicalDeviceMemoryProperties` (every
heap's size and flags, every type's heap and flags, one line each), a
device with one graphics queue. Then `vkAllocateMemory` of 256 MB from a
`DEVICE_LOCAL` type and of 64 MB from a `HOST_VISIBLE | HOST_COHERENT`
type, each followed by the two kernel figures: which pool each came from
and how much it took. Map the 64 MB, write it, read it back. Free both
and read the figures again. Keep the instance and device for sections 5
and 6.

**5. The display.** `vkGetPhysicalDeviceDisplayPropertiesKHR` (every
display: name, physical dimensions and resolution, transforms),
`vkGetDisplayModePropertiesKHR` (every mode: region, refresh in mHz),
`vkGetPhysicalDeviceDisplayPlanePropertiesKHR` and each plane's displays.
Then `vkCreateDisplayModeKHR` for 1920x1080 at 59,940 mHz, 1920x1080 at
60,000, 3840x2160 at 60,000 and 1280x720 at 59,940: the result code of
each. For every mode that exists or was created,
`vkGetDisplayPlaneCapabilitiesKHR` (alpha, source and destination
positions and extents, min and max). A plane surface on the native mode:
`vkGetPhysicalDeviceSurfaceCapabilitiesKHR` (image counts, current, min
and max extents, transforms, composite alpha, usage), every surface format
and colour space, every present mode. A FIFO swapchain on it, 60 frames
acquired, cleared to a changing gray and presented, the last 50 timed
(part A's smoke test got 0.834 s). If a 1920x1080 mode exists or was
created: a second surface and swapchain on it, the same 60 frames, so the
answer is whether the console scans out 1080p or the renderer scales to
4K itself. Keep the native swapchain for section 6.

**6. Leaving and returning.** The user is asked, by a notification and in
the report, to: watch the gray cycle for ten seconds; press the PS button,
stay in the system's menu for ten seconds, return to the title; open the
Quick Menu (hold the PS button), close it; wait. The probe runs for 90
seconds, presenting a frame every acquire with the pad open
(`scePadOpen` for the initial user, as the io probe), and logs once a
second: the number of frames presented and the interval's min, median and
max since the last line; every acquire or present result that is not
`VK_SUCCESS`, as it happens, with the time; the pad's `connected` and the
`intercepted` bit (0x80000000) whenever either changes, with the time;
and whether `sceSystemServiceReceiveEvent` (declared by hand from a public
source, as the io probe's declarations were; if it cannot be declared with
confidence, leave it out and say so) delivers an event, with its type.
After the 90 seconds: whether the swapchain still presents (ten more
frames) and, if it reported out of date, whether a new one on the same
surface does. The process keeping its timestamps continuous while the user
is away, or not, is the first answer.

#### The SDL2 probe (PPSA99104)

pacbrew's SDL2 (ps5-payload-dev's port, v0.40.2) in a title built with
PS5_Vulkan's toolchain and SDK fork, with `libc.prx` under it. In
PS5_Vulkan: `make pacbrew` fetches the pinned sysroot into `.deps/pacbrew/`
(346 MB, once); `bash tools/setup-pacbrew-dependencies.sh --environment`
prints how `pkg-config` is pointed at it; `tools/build.sh` (lines 130 on)
shows how the flags of `PACBREW_PACKAGES` reach the compile and the link.
`build-extra.sh` does the same for this probe with `sdl2` (and its
transitive archives, in `pkg-config --static --libs` order).

Two outcomes are possible before anything runs, and both are results: the
link fails (an SDL2 symbol the shim's libc or the system libraries do not
have: name them), or the title does not start (the loader refuses an
import: the kernel log names it). In either case item 1's answer is
"route (2)", and the report is the error.

If it starts, with the crash guard around each step:

1. `SDL_GetVersion`, `SDL_GetRevision`.
2. `SDL_InitSubSystem` one flag at a time, each with `SDL_GetError` on
   failure: `SDL_INIT_TIMER`, `SDL_INIT_EVENTS`, `SDL_INIT_GAMECONTROLLER`
   (which brings `JOYSTICK`), `SDL_INIT_AUDIO`. Not `SDL_INIT_VIDEO` yet.
3. The pad: `SDL_NumJoysticks`, for each `SDL_IsGameController`, its name,
   GUID and mapping string; `SDL_GameControllerOpen(0)`; then 15 seconds
   of `SDL_PollEvent` logging every `SDL_CONTROLLERBUTTONDOWN`/`UP` by
   button name and every `SDL_CONTROLLERAXISMOTION` by axis name and value
   (once per 10 units of change, so the report stays readable), while the
   user presses everything, as the io probe asked; `SDL_GameControllerRumble`
   for one second at the end, and whether the pad shook (ask in the
   notification; the user answers in the run's notes).
4. Audio: `SDL_GetNumAudioDrivers` and `SDL_GetCurrentAudioDriver`;
   `SDL_OpenAudioDevice(NULL, 0, &spec, &got, 0)` with 48,000 Hz,
   `AUDIO_F32SYS`, 2 channels, 256 samples and a callback that writes a
   440 Hz tone: the spec SDL gave back; two seconds of sound, the
   callback's count and the gaps between calls (min, median, max); then
   the same device opened without a callback and fed with
   `SDL_QueueAudio`, `SDL_GetQueuedAudioSize` sampled, two seconds.
5. Timing: `SDL_GetTicks64` and `SDL_GetPerformanceCounter` against the
   probe's TSC clock over five seconds.
6. Last, guarded: `SDL_InitSubSystem(SDL_INIT_VIDEO)`: the driver's name,
   whether `SDL_CreateWindow(..., 0)` returns a window, and
   `SDL_GetNumVideoDisplays`. The host does not want SDL's video (the
   renderer presents through `VK_KHR_display`); this says what it would
   cost to leave it out, and whether the pad and audio work without it
   (steps 3 and 4 ran before it).
7. `SDL_Quit`, and the report's last line.

#### The glslang probe (PPSA99105)

glslang at the Android build's pin (`GLSLANG_TAG` in
`tools/android_build.py`, 16.6.0), cloned into `build/ps5/third_party/glslang`
(the Android build's clone under `build/android/third_party/glslang` may be
reused if it is there), built static with CMake and the SDK fork's
`toolchain/prospero.cmake` (read it first: what compiler it names and how
the fork's `prospero-clang18` wrapper and `LLVM_CONFIG` reach it; try the
fork's default clang first, and clang 22 as `probe/vk/build.sh` does if
the link later refuses weak symbols, and say which was needed), with
`port/android/probe/glslang/CMakeLists.txt`'s options
(`ENABLE_OPT OFF`, `ENABLE_HLSL OFF`, no binaries, no tests) but
`BUILD_SHARED_LIBS OFF` and no shared `halo_glslang` target: the archives
glslang 16 produces (`glslang`, `MachineIndependent`, `GenericCodeGen`,
`OSDependent`, `SPIRV`, `glslang-default-resource-limits`) go to the
title's link whole, with libc++ as `build-radv-title.sh` links it for ACO.
The build steps go in `build-extra.sh` (or a script it calls), so
`title/build.sh port/ps5/probe/glslang` is the whole build.

The program, in C, through `glslang/Include/glslang_c_interface.h`, the
calls and the `glslang_input_t` `host_vk_shaders.c` builds (its client,
client version, target language and version, the default resource limits,
its messages flags), so the console compiles what the renderer will ask it
to:

1. `glslang_initialize_process`: its time, and
   `sceKernelAvailableFlexibleMemorySize` before and after.
2. For each of the six shaders in `/app0` (`pass.vert`, `pass.frag`,
   `vs_typical.vert`, `ps_typical.frag`, `vs_large.vert`, `ps_large.frag`),
   ten times: create, preprocess, parse, a program, link,
   `glslang_program_SPIRV_generate`, delete. The first compile of the
   first shader on its own; then per shader the min and median of the
   rest, wall time from the TSC. The SPIR-V's size in words. On any
   failure, the info logs, whole.
3. The SPIR-V of each shader's last compile written to
   `/app0/spirv/<name>.spv` (chmod 0666), so it can be pulled and checked
   on the PC with `spirv-val` (the Android work's tooling;
   `tools/android_build.py` says where the SPIR-V tools come from, or the
   system's `spirv-tools` package).
4. Flexible memory available after all compiles, and after
   `glslang_finalize_process`.

If glslang does not build with the title toolchain, the report is the
build's errors, and "Progress" says which (a missing header of the SDK's
libc, a C++ library feature, a linker refusal), because that decides
whether the fallback is a fix to the build or the shipped SPIR-V corpus.

#### Item 9: starting a title from the PC (`ps5vkctl`)

Only if the user has said so for this part. PS5_Vulkan's
`payload/ps5vkctl/ps5vkctl.elf` is built (`tools/build-ps5vkctl.sh`);
`docs/DEPLOYMENT.md`'s "Autonomous run loop" and `tools/ps5_console.py`
show the loop. Load it once (the ELF loader on port 9021), then over TCP
to port 9111: `ping`, `status`, `users`, `launch PPSA99103`, the kernel
log read from port 3232 while the probe runs, `status`, `kill PPSA99103`.
Record each command, its answer and how long `launch` took to the probe's
first report line in the kernel log. This is what phase 1's
`tools/ps5_deploy.py` wraps.

#### Testing on the console

1. Build the three titles: `port/ps5/probe/title/build.sh port/ps5/probe/<folder>`
   for `glslang`, `sdl2`, `host`. A title that does not build is a result:
   the errors go into "Progress" and the run goes on with the others.
2. The user makes `/data/halo` over FTP (empty).
3. Deploy each: `port/ps5/probe/title/deploy.sh 192.168.2.157 <ID>`. Open
   the kernel log first (`nc 192.168.2.157 3232 | tee <folder>.klog`) and
   keep it open through every run: a title that dies before its report
   opens speaks only there.
4. The user starts each title from the home screen (or item 9 does) and
   says when it is done: the glslang probe first, the SDL2 probe second
   (the user presses the pad's buttons and says whether it shook), the host
   probe last (the user does section 6's steps when the notification asks).
   A probe that crashed is started again: the guard skips the section.
5. Fetch each report over FTP (`curl ftp://192.168.2.157:2121/data/homebrew/<ID>/<name>.txt`)
   into `port/ps5/probe/results/`, with the kernel log's lines for a crash
   beside it.

#### Acceptance

The review checks each of these:

1. The three titles build from `title/build.sh` on this machine (PS5_Vulkan
   set up as `PS5_FINDINGS.md` describes), and part A's probes still build
   unchanged. Nothing of PS5_Vulkan, the SDK fork, pacbrew or glslang is
   copied into the repository; `build-extra.sh` sources or calls their
   scripts where they are, and the pins are named (pacbrew v0.40.2 and
   its SHA-256, glslang's tag, PS5_Vulkan's and the fork's commits).
2. The three reports are in `port/ps5/probe/results/`, each with the
   system software's line first, and every section either answered or
   marked skipped with the kernel log's line that says why.
3. "Progress" has, per item 1 to 9, the answer in one sentence and then
   the lines of the report that show it: the futex table and times; the
   fault's fields; the data folder's speeds and whether `/data/halo` is
   visible; the memory figures before and after each step and which pool
   RADV allocates from; the modes, what `vkCreateDisplayModeKHR` returned
   and both swapchains' pacing; what happened while the user was away;
   SDL2's outcome at every step; glslang's build, its times and the
   `spirv-val` result; `ps5vkctl`'s commands and answers if run.
4. "Decisions": the SDL, shader compiler, texture write tracking and
   futexes rows rewritten as decisions with one line of why each, their
   "(to confirm)" removed; the "Shared code" and `ps5vkctl` rows left as
   they are and listed in the report to the user as theirs to answer. If
   `/data/halo` was not visible, the "Game data" row is marked
   "(to confirm)" with the alternatives.
5. Nothing outside `port/ps5/probe` and `port/ps5/PS5.md` changes.
6. The Switch's and Android's trees are untouched, and the user's console
   holds nothing new but the three titles and `/data/halo` (left as the
   user wants it).

Don't commit; the review does that. Note anything unexpected under
"Progress", even if it was dealt with.

**Done when:** the acceptance list holds, and the user has the report
with the two rows that are theirs to decide.

## Phase 1 — the build and the deploy

Written to be picked up by an agent that has not seen the work so far. Read
first, in this order: this plan's "Decisions", "Where the port lives",
"Files" and "How this plan is worked", then phase 0 B's entry under
"Progress" whole (what the console does and does not do, and the pins);
`PS5_FINDINGS.md`'s "Building a title" and "Building the Vulkan driver";
`port/ps5/probe/title/build.sh` and the three `build-extra.sh` under
`port/ps5/probe/host`, `sdl2` and `glslang` (the title's compile, link and
packaging, and the three link recipes this phase joins into one);
`tools/switch_build.py` whole (the model: a guest image and a host built
with another toolchain, packaged for a console) and `tools/switch_deploy.py`;
`tools/linux_guest_build.py` (`_generate`: how the x32 guest is asked for
and how its host is built) and `generate_guest_image`'s docstring and
`vulkan` branch in `tools/android_build.py`; `port/ps5/probe/guest/main.c`
(sections 4 and 6: the ranges, the ELF loader, the thunks and the trivial
answers, which the bootstrap host grows from) with `guest_probe.S`;
`port/linux/host/host_main.c` and `port/switch/host/host_main.c` (the two
entry points' shape); PS5_Vulkan's `tools/ps5_console.py`
(`ps5vkctl_command`, `deploy_payload`, `capture_runs`) and
`docs/DEPLOYMENT.md`; `tools/test_linux_guest_build.py` (the shape of a
configure test). The work is reviewed against the acceptance list at the
end of this phase.

### What this phase is, and is not

- **It is** `ninja ps5`: the x32 guest with the Vulkan renderer and the
  movable window, a host compiled and linked with PS5_Vulkan's toolchain
  and the three link recipes together, packaged as a title with an identity
  of its own; `tools/ps5_deploy.py`, which installs, starts, follows and
  fetches a run from one shell; a test of the build's configure; the first
  `port/ps5/README.md`; and a **bootstrap host** that proves the pipeline
  end to end on the console.
- **It is not** the host. The bootstrap host's loader, thunks and import
  answers are the guest probe's section 6 moved into one file,
  `host_bootstrap.c`, which says at its top that phase 2 replaces it with
  the Android host's loader and the real import table. Only `host_main.c`
  (the title's start-up and log) is written to stay.
- **Nothing changes** under `port/android`, `port/linux`, `port/switch`,
  `source` or the probes. `configure.py` gains the PS5's options and one
  call; `tools/` gains the PS5's scripts. Nothing of PS5_Vulkan's, the SDK
  fork's, pacbrew's or glslang's is copied in: the build sources
  `radv-link.sh` and calls the fork's tools where they are, compiles
  PS5_Vulkan's `tooling/native/*.cpp` and its AGC stub sources from its
  tree, and pins the checkouts by commit.
- **Full link from the start.** RADV's archive, glslang's archives and
  pacbrew's SDL2 go into this phase's link although the bootstrap host
  barely calls them, because the three recipes have been proved one at a
  time and never together; a symbol clash between them is this phase's to
  find, not phase 4's.

### Files

| File | What |
|---|---|
| `tools/ps5_build.py` | `generate_ps5_build(n, sln)` and `ps5_configure_inputs()`, called from `configure.py` as the Switch's are. The pins as constants at the top: `PS5_VULKAN_COMMIT`, `PS5_PAYLOAD_SDK_REVISION`, `PS5_MESA_COMMIT`, `PACBREW_VERSION` and its SHA-256, `GLSLANG_TAG` (imported from `android_build`), with the values phase 0 B recorded. The guest image, glslang's build, the `ps5-native-tool` build, the host's compile, the link and the packaging as ninja rules (below). |
| `tools/ps5_title.sh` | the shell the ninja rules call for what is a bash recipe: `link` (sources `radv-link.sh`, runs `prospero-lld`, then `ps5-native-tool link` with the AGC stubs) and `self` (`ps5-native-tool self --sign`), grown from `probe/title/build.sh`'s lines, taking its inputs and outputs as arguments so ninja owns the dependencies. The probes keep their own `build.sh`; they are not changed. |
| `tools/ps5_deploy.py` | upload, launch, kill, restart, status, follow the kernel log, fetch a run's files; `--run` does the whole loop (below) |
| `tools/test_ps5_build.py` | the configure test (below) |
| `configure.py` | `--ps5` (generate the build, as `--linux-x86-64` does for its target), `--ps5-vulkan PATH` (the PS5_Vulkan checkout; default `$PS5_VULKAN`, then `~/projects/ps5-deps/PS5_Vulkan`), `--ps5-mesa PATH` (default beside it, `PS5_Mesa`), `--ps5-llvm-config PATH` (default `/usr/bin/llvm-config-22`), `--ps5-hostdeps ENV` (default `~/projects/ps5-deps/hostdeps/env.sh`, sourced by the rules that need meson's and LLVM's tools), `--ps5-guest-cc` (default `clang`, as `--linux-x86-64-cc`); the settings go into `sln` with the `ps5_` prefix |
| `port/ps5/host/host_main.c` | the title's entry point, written to stay: `ps5_fp_ieee()`, the log (a file and the kernel log), the data folder, the build's identity logged, then the bootstrap (this phase) or the Android host's start-up (phase 2) |
| `port/ps5/host/host_bootstrap.c`, `host_bootstrap.S` | **temporary**: the guest's ranges as direct memory, `/app0/halo_guest.elf` loaded where it is linked, every import a thunk into `import_dispatch`, the probe's trivial answers, a stop at the first import not answered. The probe's code, moved and trimmed; the header comment says phase 2 deletes both files |
| `port/ps5/sce_sys/param.json` | the title: `titleId` **PPSA99500**, `contentId` `UP9000-PPSA99500_00-HALOCOMBATEVOLVE`, `titleName` "Halo: Combat Evolved", the other fields as the probes' (`downloadDataSize` 256 kept) |
| `port/ps5/sce_sys/icon0.png` | 512x512, made once from `port/assets/icon/opence-icon.png` with Pillow (the command in a comment in `ps5_build.py`), committed as the Switch's `icon.jpg` is |
| `port/ps5/README.md` | first version: requirements, building, installing, running a build, where the log is; grows with each phase |

### The build (`ninja ps5`)

`generate_ps5_build` follows `tools/linux_guest_build.py`'s shape: it is
generated only when asked for (`--ps5`, or `--ps5-vulkan`, or the sources
already fetched under `build/ps5/third_party`), it writes itself aside and,
on any failure to generate, leaves a `ps5` target that stops with the
reason (`_unavailable`'s pattern), never configure.py. The reasons it
checks, each a plain sentence: no PS5_Vulkan checkout at the path; no SDK
fork under its `.deps/native/ps5-payload-sdk` (say to run its
`tools/setup-native-dependencies.sh`); no PS5_Mesa beside it; no RADV
release archive (say to run `port/ps5/probe/vk/build.sh`, which builds it,
or PS5_Vulkan's `tools/build-radv.sh release` with clang 22); no hostdeps
environment; no `GL/glcorearb.h` (the desktop guest's GL stubs need it, as
`linux_x86_64`'s do); a checkout whose `HEAD` is not the pinned commit is a
**warning** printed by configure.py, not a stop. All paths in the build file
are absolute, as the Switch's are (its comment says why).

1. **The guest image.** `generate_guest_image(n, sln, config, arch=X32,
   prefix="ps5", label="PS5", build=Path("build/ps5"),
   third_party=Path("build/ps5/third_party"), ...)` with the x32 target's
   tools and flags as `linux_guest_build._generate` passes them for
   `linux_x86_64` (the compiler's `llvm-ar`, `ld.lld`, `llvm-objdump` with
   its version suffix; `environment="gnu"`; `extra_runtime` the desktop's
   `guest_desktop.c`; `extra_imports` `port/linux/host/host_imports.list`;
   `updater_cflags` as there), `platform_defines=["-DHALO_MOVABLE_WINDOW=1"]`
   (the desktop's code paths: no `HALO_ANDROID`, no `HALO_GLES`),
   `desktop_gl=True` and **`vulkan=True`**. The title takes the result's
   `vk_image` (`build/ps5/halo_guest_vk.elf`) as its `halo_guest.elf`; the
   GL image the generator also links is a by-product and is not packaged.
   The guest is thus the `ninja linux_x86_64` guest with the window movable
   and the Vulkan renderer, built from the same sources into `build/ps5`,
   so the PC build goes on testing the sources. `fetch_third_party` fetches
   musl and SDL3 for it as `linux_x86_64` does.

2. **glslang**, as `port/ps5/probe/glslang/build-extra.sh` builds it, as a
   ninja rule: the clone at `GLSLANG_TAG` (the Android build's clone reused
   when it is there at that tag, else `build/ps5/third_party/glslang`),
   CMake with the fork's `toolchain/prospero.cmake` and the probe's options,
   `ninja glslang glslang-default-resource-limits`, outputs
   `build/ps5/glslang/glslang/libglslang.a` and
   `libglslang-default-resource-limits.a`.

3. **pacbrew's SDL2**, resolved at configure time (only when the build is
   generated): `setup-pacbrew-dependencies.sh --resolve sdl2`, which fetches
   the pinned sysroot once (346 MB, into PS5_Vulkan's `.deps/pacbrew`) and
   answers the include folders and the archives in link order, as the SDL2
   probe's `build-extra.sh` reads them.

4. **`ps5-native-tool`**, built from PS5_Vulkan's `tooling/native/*.cpp`
   with the system's `clang++` as `probe/title/build.sh` builds it, as a
   rule with those sources as inputs.

5. **The host's compile.** Rule `ps5_host_cc`: `PS5_PAYLOAD_SDK=<sdk> sh
   <vulkan>/tooling/prospero-clang18`, with `LLVM_CONFIG` set to
   `--ps5-llvm-config` (clang 22: Mesa's weak entry points, `PS5_FINDINGS.md`),
   `-MMD` for ninja's dependencies, flags as `build.sh`'s (`-std=c11 -O2 -g
   -Wall -Wextra -Werror -ffunction-sections -fdata-sections`) plus
   `-DHALO_PS5=1`, `-I port/ps5/host -I port/android/include -I
   port/android/host -isystem <PS5_Mesa>/include` (Vulkan's headers), the
   SDL2 include folders as `-isystem`, and `-I <glslang source>`. The
   `.S` files are assembled with the same wrapper. PS5_Vulkan's
   `app_crt.cpp` and `app_cpp_runtime.cpp` compile as C++20 as `build.sh`
   does. The AGC stub libraries (`libSceAgc.so`, `libSceAgcDriver.so`) are
   compiled from PS5_Vulkan's `vendor/ps5/sdk/stubs/*.c` as the host
   probe's `build-extra.sh` does, as rules.

6. **The link**, `tools/ps5_title.sh link`: sources `radv-link.sh`, calls
   `radv_link_recipe`, and runs `prospero-lld` with the recipe's linker
   script, `--eh-frame-hdr`, `--version-script app-symbols.map`,
   `--exclude-libs=ALL`, `-e _start`, the C runtime objects, the host's
   objects, the recipe's flags (the `--wrap`s and `--defsym`s), the AGC
   stubs, the recipe's inputs (RADV's archive whole, libc++), glslang's two
   archives whole, SDL2's archives, `libps5platform.a`, `--as-needed` the
   SDK's `.so` files; then `ps5-native-tool link --stub` the AGC stubs
   `--module-sdk 0x02000009 --companion-sdk 0x08050001`. The host
   provides `___mb_cur_max` (the SDL2 probe's `libc_extra.c`, moved to
   `port/ps5/host/host_libc_extra.c` and kept). To prove SDL2 is really in
   the link, `host_main.c` logs `SDL_GetVersion`'s answer; to prove RADV
   and glslang, it logs `vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance")`
   being non-null and `glslang_initialize_process`'s return, then
   `glslang_finalize_process` (nothing more of either in this phase).

7. **The package**, `build/ps5/title/PPSA99500/`: `eboot.bin` from
   `tools/ps5_title.sh self` (`--sign --magic 0x1D3D154F`, then
   `--inspect` as `build.sh` does), `sce_sys/param.json` and `icon0.png`
   copied from `port/ps5/sce_sys`, `sce_module/libc.prx` from PS5_Vulkan's
   `runtime/` after its sha256 check (built by its `tools/rebuild-libc.sh`
   if absent), `halo_guest.elf` (the Vulkan image), and `NOTICE.txt`, a
   first version of phase 7's notice: the pins, the repositories and their
   URLs, written by `ps5_build.py` from its constants at configure time.
   The phony target `ps5` depends on all of them.

8. **Log the pins.** `host_main.c` logs the build's git hash (as the Linux
   host does for the game; look for how the desktop's host learns it, or
   pass `-DHALO_PS5_BUILD=\"<hash>\"` from the build), the system software,
   and the pins compiled in as a string, so a report names what ran.

### The bootstrap host

`host_main.c`, in this order: `ps5_fp_ieee()`; the log sink: a line goes to
`/data/halo/halo.log` if `/data/halo` exists (else `/app0/halo.log`, and the
log's first line says which), `chmod 0666` on creation, flushed per line (a
crash leaves everything up to it), and to the kernel log
(`sceKernelDebugOutText`, prefixed `halo: `), with a notification at start
and at the end as the probes do; the build's identity and the pins; the
three link proofs of step 6; `/data/halo` found or not (and what is in it,
one line); then `host_bootstrap_run()`. A `--hold` for the deploy's kill
test: if `/app0/hold.txt` exists the host sleeps 120 s after its last line
before ending, so a running title can be killed from the PC.

`host_bootstrap.c`: the guest probe's `map_direct`, `reserve_exactly`, the
window's placement loop, the pool, `load_guest`, the thunks and
`import_dispatch` (in `host_bootstrap.S`, the probe's `guest_probe.S`
trimmed to it), `guest_system_call` and the trivial imports, the 60 s
limit, the fault report with its registers. Trimmed of the probe's
sections 1 to 5 and their report. Its last line is `bootstrap: the first
import not answered: <name>(...)` or the fault. A `/app0/halo_guest.elf`
that is not an x32 image is said so and ends the run.

The host ends with `_exit(0)` and the kernel log's `SIGSYS` (phase 0 B;
phase 2 looks at it); the deploy's follower treats the host's own last line
as the end, not the signal.

### The deploy (`tools/ps5_deploy.py`)

Defaults from `tools/switch_deploy.py`'s shape and PS5_Vulkan's
`ps5_console.py`'s protocol (read, not copied): `--host 192.168.2.157`,
`--ftp-port 2121`, `--ctl-port 9111`, `--klog-port 3232`, `--manager-port
8084`, `--title PPSA99500`, `--title-dir build/ps5/title/PPSA99500`,
`--logs build/ps5/logs`. Actions, one or more in the order given:

- `fetch`: `/data/homebrew/<ID>/halo.log` if there, else `/data/halo/halo.log`
  (the host says which it wrote; fetch both when both exist), plus any
  `*.txt` report in the title's folder, into `build/ps5/logs/<UTC stamp>/`;
  prints the folder.
- `upload`: every file of the title folder over FTP (`--ftp-create-dirs`'s
  equivalent in `ftplib`: make `sce_sys` and `sce_module`), `eboot.bin` and
  `sce_sys/param.json` last, each size compared after the transfer as
  `switch_deploy.py` does (an FTP download of `eboot.bin` is the decrypted
  ELF, `PS5_FINDINGS.md`: compare the others by size, the eboot against the
  unsigned ELF's size or not at all). Refuses while `status` says the
  title is running, unless `--force`.
- `status`, `launch`, `kill`, `restart`: one command to `ps5vkctl` each,
  one connection a command, the answer printed; a `launch` of a title that
  `status` says is running is refused; `kill` only of the title named.
- `follow [--timeout S]`: connects to klogsrv, skips the replayed backlog
  as `capture_runs` does, de-duplicates the doubled lines (phase 0 B:
  klogsrv sends each line twice), prints the `halo: ` lines and any fatal
  signal block, and returns when the host's last line arrives (the
  bootstrap's "first import not answered" or "ends" line), on a fatal
  signal after it, or at the timeout (default 180 s). The whole capture is
  saved beside the fetched files.
- `--run`: `fetch` (the previous run's files first: phase 0 B lost one),
  `kill` if running, `upload`, `launch` with `follow` started before it,
  then `fetch`. Exit status 0 only if the host's last line was seen.
- `load-ps5vkctl`: when `ping` gets no answer: PS5_Vulkan's
  `build/ps5vkctl/ps5vkctl.elf` (built by its `tools/build-ps5vkctl.sh`,
  which needs `PS5_PAYLOAD_SDK` pointed at a payload SDK: the fork works,
  say if it did not) sent to the user's payload manager (itsPLK's
  ps5-payload-manager 0.5.2 on port 8084: an upload, then a load; phase 0 B
  did it by hand and did not record the requests, so read the manager's
  protocol from its source or its web page and record the exact requests
  under "Progress"), then `ping` again. Done only when the user says the
  manager is up.

Every action prints what it does in one line each; errors name the host and
port that did not answer. The user is told before anything is launched when
a person is needed at the console; nothing in this phase needs one.

### The test (`tools/test_ps5_build.py`)

After `tools/test_linux_guest_build.py`: in a temporary tree with
`port/linux/port.json` and fake GL headers, (1) not asked for, configure
writes no `ps5` build and fetches nothing; (2) asked for with a
`--ps5-vulkan` path that does not exist, the `ps5` target is unavailable
with a reason naming the path, and configure succeeds; (3) asked for with a
fake checkout tree holding the files the generator checks (the SDK fork's
`.ps5-sdk-revision`, the native tooling sources' names, the RADV archive, a
`PS5_Mesa/include/vulkan/vulkan.h`), with the fetches and the pacbrew
resolution monkeypatched, the build file names `halo_guest_vk.elf`, the
host objects, the link, `eboot.bin`, `param.json`, `icon0.png`, `libc.prx`
and `NOTICE.txt`; (4) a fake checkout whose `HEAD` differs from the pin
prints the warning and still generates. Added to `AGENTS.md`'s testing list
in phase 7, not now.

### The compile survey, for phase 2

Not a build step and not blocking: with the host's compile flags of step 5,
compile each file of `port/android/host/*.c` (less `host_main.c`,
`host_touch.c`, the `host_vk*` files), `port/linux/host/host_desktop.c` and
`host_main.c`, and `port/linux/src/posix_files.c`, with `-fsyntax-only`,
and write under "Progress" a table: the file, compiles or not, the first
error if not (a missing header, a Linux-only call, a type). That is what
phase 2 is written from.

### Testing on the console

1. `python3 configure.py --ps5` (the other options at their defaults), then
   `ninja ps5`, from a clean `build/ps5` (`rm -rf build/ps5` first: the
   phase 0 B probes' `build/ps5/probe` and `glslang` folders are not this
   build's). Note the time of the first build and of a null rebuild.
2. `python3 tools/ps5_deploy.py --run`. The user has `/data/halo` on the
   console already (empty, from phase 0 B). Expected in the log: the
   identity lines, the three link proofs, `/data/halo` found, the ranges,
   the image's segments and header, the start-up's host calls as the guest
   probe's run 3 printed them, and `bootstrap: the first import not
   answered: host_sdl_get_base_path(...)`, within a few seconds of the
   launch. (The Vulkan image's start-up is the GL image's up to that call;
   if it stops earlier or elsewhere, that is the result to report, not to
   fix in this phase.)
3. `--run` again, so the relaunch path (fetch, then kill-if-running, upload,
   launch) is seen once with a previous run's files in place.
4. The kill test: put `hold.txt` in the title's folder over FTP (the deploy
   gets a `--hold` that does), `--run` with `follow`'s timeout short, then
   `status` (running) and `kill`, then `status` (idle). Record the answers.
5. `ninja linux_x86_64` still builds, and `python3 -m pytest -q
   tools/test_ps5_build.py tools/test_linux_guest_build.py
   tools/test_guest_layout.py` passes.

### Acceptance

The review checks each of these:

1. `python3 configure.py --ps5 && ninja ps5` builds the title from a clean
   `build/ps5` on this machine; `python3 configure.py` without `--ps5`
   generates no PS5 build and prints nothing about it; `--ps5-vulkan
   /nonexistent` gives an unavailable `ps5` target and a successful
   configure. `ninja linux_x86_64` builds, and the three tests pass.
2. The eboot's ELF (`build/ps5/host/eboot.elf`, before signing) defines
   `vk_icdGetInstanceProcAddr`, `glslang_initialize_process` and
   `SDL_GetVersion` (`llvm-nm` from the SDK), and the console's log shows
   the three link proofs.
3. One `tools/ps5_deploy.py --run` installs, starts, follows and fetches,
   and the fetched log ends at the game's first SDL import, as the guest
   probe's run 3 did. The run's files are under `build/ps5/logs/`.
4. The relaunch path fetched the previous run's files first; `kill` of a
   running title was tried and its answers recorded.
5. "Progress" has: the pins and the checkouts' actual commits, the eboot's
   size, the first build's and a rebuild's times, a deploy's time, the
   payload manager's requests (if `load-ps5vkctl` was needed), the compile
   survey's table, and anything unexpected.
6. Nothing changed outside `tools/ps5_build.py`, `tools/ps5_title.sh`,
   `tools/ps5_deploy.py`, `tools/test_ps5_build.py`, `configure.py` and
   `port/ps5/` (the probes untouched); nothing of PS5_Vulkan's, the fork's,
   pacbrew's or glslang's copied in.
7. `port/ps5/README.md` says how to build, install and run what exists,
   and no more.

Don't commit; the review does that. Record anything unexpected under
"Progress", even if it was dealt with.

**Ends with:** `ninja ps5 && python3 tools/ps5_deploy.py --run` builds,
installs and starts the title from one shell, and its log shows the game's
start-up to the first SDL call.

## Phase 2 — the host's core, no graphics

Written to be picked up by an agent that has not seen the work so far. Read
first, in this order: this plan's "Decisions", "Where the port lives",
"Files" and "How this plan is worked"; phase 0 B's and phase 1's entries
under "Progress" whole (the console's memory, faults, futexes, files and
`exit()`; the compile survey; what the bootstrap host logs); then the code.
The Android host library: `port/android/host/host.h` whole (the interface
this phase implements the PS5's side of), `host_common.c`, `host_thread.c`,
`host_loader.c`, `host_debug.c`, `host_arch.c`, `host_memory.c` whole (the
file this phase rewrites for the PS5: its bookkeeping stays, its system
calls go) and `host_syscall.c` whole (the model of a system call layer with
the guest's layouts translated); the Linux host, `port/linux/host/host_main.c`
(the entry point the PS5's follows) and `host_desktop.c` (the desktop's 35
imports, to be answered by stand-ins here); the Switch host,
`port/switch/host/host_memory.c` and `host_mman.c` (a rewrite of the memory
layer for a system without Linux's calls, which is what this phase does
over direct memory), `host_syscall.c` (the inventory of the guest's calls
on a non-Linux libc), `host_futex.c` (what a futex layer must cover) and
`host_sdl2.c`'s stand-in parts (`standin_window`, the swap's pacing); the
guest's side, `port/android/guest/runtime/guest_sdl.c` and
`port/linux/host/guest_desktop.c` (what each `host_sdl_*` import is asked
and expects back), `port/android/include/halo_guest_syscall.h` (the guest's
numbers, layouts and flags) and `halo_android_abi.h`; the phase 1 files
under `port/ps5/host`; the SDK fork's headers under
`~/projects/ps5-deps/PS5_Vulkan/.deps/native/ps5-payload-sdk/target/include`
(`sys/mman.h`, `sys/stat.h`, `sys/umtx.h`, `x86/ucontext.h`, `fcntl.h`,
`errno.h`, `dirent.h`, `time.h`, and `ps5platform/kernel.h` for the direct
memory calls the probes used) and PS5_Vulkan's `docs/PLATFORM_NOTES.md`; the
phase 0 B probes `port/ps5/probe/host/main.c` (sections 1 to 4: the direct
memory recipe, `_umtx_op`'s forms, the fault handler and its `mcontext`,
the files) as working code to lift from. The work is reviewed against the
acceptance list at the end of this phase.

### What this phase is, and is not

- **It is** the PS5's system layer under the Android host library: memory
  (the guest's ranges and pools as direct memory, the write watch, the
  crash report), system calls on FreeBSD's libc, futexes over `_umtx_op`,
  threads, the entry point's real start-up; and stand-ins for everything
  the game asks of SDL, OpenGL, the renderer's host half and the updater,
  so that the game runs headless from a fresh data folder to its main
  menu's map and keeps running. At the end `host_bootstrap.c` and `.S` are
  deleted.
- **It is not** the pad, audio or events (phase 3), the renderer (phase 4),
  or anything on the TV. The stand-ins written here are kept: phase 3 fills
  the pad, audio and event parts of `host_sdl_ps5.c` with SDL2, phase 4
  replaces `host_vk_null.c` with the real renderer files.
- **Shared files.** `host_loader.c`, `host_thread.c`, `host_watch_hash.c`
  and `host_common.c` compile for the PS5 as they are and are used as they
  are. `host_arch.c` and `host_debug.c` need a PS5 branch each (the
  FreeBSD `mcontext`; the thread id, and no sampler), behind `HALO_PS5`,
  argued under "Progress"; if a file would have more than a third of its
  lines under the define, a PS5 file replaces it instead, and the write-up
  says so. Android's `host_memory.c`, `host_syscall.c`, `host_sdl.c`,
  `host_gl.c`, `host_main.c`, `host_touch.c`, `host_probe.c` and the
  `host_vk*` files are **not** compiled for the PS5: the PS5's own files
  take their places (below). Nothing under `port/linux`, `port/switch`,
  `source` or the probes changes; Android's and Linux's objects must not
  change for the files that are shared (`HALO_PS5` is the only gate).
- **Nothing of PS5_Vulkan's is copied in.** Its platform layer is called
  (`ps5platform/*.h`), not copied; FreeBSD's constants are written as
  numbers in a header of our own and checked against the SDK's headers at
  compile time.

### Files

| File | What |
|---|---|
| `port/ps5/host/host_main.c` | grows into the real entry point (below); the bootstrap call goes |
| `port/ps5/host/host_memory.c` | **new**: Android's `host_memory.c` with its system calls replaced by the console's: `sceKernelReserveVirtualRange`, `sceKernelAllocateDirectMemory`, `sceKernelMapDirectMemory`, `sceKernelMprotect` and `sceKernelVirtualQuery` in place of `mmap`, `MAP_FIXED_NOREPLACE` and `/proc/self/maps`. The pools, the guest page bookkeeping, `host_guest_mmap`'s rules, the write watch and the crash report stay as Android's are. Also `__wrap_mmap`, below |
| `port/ps5/host/host_syscall.c` | **new**: the guest's 91 system calls (`halo_guest_syscall.h`) on FreeBSD's libc, after Android's structure and the Switch's inventory |
| `port/ps5/host/host_syscall_tables.c`, `freebsd_abi.h` | **new**: the pure translations (open flags both ways, `errno`, clock ids, `AT_*`, the `kstat` and `dirent64` layouts filled from fields passed in) and the FreeBSD numbers they map to, written as literals and checked by `_Static_assert` against the SDK's headers in `host_syscall.c`; the PC test compiles the tables alone |
| `port/ps5/host/host_futex.c` | **new**: the guest's futex operations over `_umtx_op`, as phase 0 B's table sets out |
| `port/ps5/host/host_sdl_ps5.c` | **new**: the 69 `host_sdl_*` imports; in this phase every one a stand-in (below) |
| `port/ps5/host/host_vk_null.c` | **new**, until phase 4: the renderer's nine imports and `host_vk_exit`, `host_renderer_vulkan`, `host_vk_presenting`, `host_vk_window`, answering so that the game runs with every draw dropped |
| `port/ps5/host/host_gl_none.c` | **new**: `host_gl_resolve` answering every `hostgl_*` name with one no-op that returns 0 and counts the name; `host_gl_frame` reporting the names called every 10 s under `debug.gpu_stats`; the eight `host_gl_*` imports as no-ops |
| `port/ps5/host/host_update_none.c` | **new**: the six `host_update_*` imports answering "unavailable" |
| `port/android/host/host_arch.c` | a `HALO_PS5` branch: the registers from FreeBSD's `mcontext` (`mc_rip`, `mc_rsp`, `mc_rbp`, `mc_rax` to `mc_r15`, `mc_err`, `mc_trapno`), as phase 0 B's probe read them |
| `port/android/host/host_debug.c` | a `HALO_PS5` branch: the thread id from the console (what the SDK's libc offers, `pthread_getthreadid_np` or the kernel's call; say which), and the sampler and profiler answering "not on the PS5" in the log when asked, their Linux bodies compiled out |
| `tools/ps5_build.py` | the host's sources: `port/ps5/host/*.c`, the six shared Android files named above, `port/third_party/tomlc17/tomlc17.c`, the guest build's `host_import_table.c`, and a generated `host_import_stubs.c` (below); `-I port/linux/src -I port/third_party/tomlc17` added; `--wrap=mmap` in the link; `host_bootstrap.c` and `.S` and the syscall-names rule removed |
| `tools/ps5_title.sh` | takes extra linker flags after `--` as it takes inputs (the wrap) |
| `port/ps5/tests/host_syscall_tables_test.c`, `tools/test_ps5_host.py` | **new**: the tables tested on the PC |
| `port/ps5/README.md` | the data folder, `config.toml`, the log, what runs and what does not yet |

### The entry point (`host_main.c`)

After phase 1's start (`ps5_fp_ieee`, the log, the identity, the three
link proofs, `/data/halo`), in this order, as `port/linux/host/host_main.c`
and Android's `host_main.c` do it:

1. `host_log_set_sink` to the PS5 log (the file and the kernel log; one
   `write` a line, no stdio: signal handlers log through it).
2. `/data/halo` must exist: without it the host logs why and ends. With it,
   `/data/halo/save` is made if missing.
3. `host_memory_reserve_early()`: on the PS5 this claims the guest's three
   fixed ranges as direct memory at once (the image's at
   `HALO_GUEST_IMAGE_BASE` for `HALO_GUEST_IMAGE_RESERVE`, Custom Edition's
   at 0x40440000, the window at the first 256 MB boundary from 0x80000000
   where 512 MB are free: the probe's recipe), before any pool exists, so
   no pool can land on them. Each range logged with its placement time, as
   the probe logs them.
4. `host_install_signal_handlers()`, `host_signal_stack_install()`.
5. The game thread: `host_native_thread_create(game_main, NULL, 16 MB)`;
   the main thread waits forever (`pause()` or a sleep loop).
6. `game_main`: the environment (`HOME=/data/halo`, `HALO_DATA_ROOT=/data/halo`,
   `HALO_SAVE_ROOT=/data/halo/save`, `XDG_DATA_HOME=/data/halo`, `TZ` from
   `host_time_zone`, and `HALO_*` settings the host wants to pass); the
   image read whole from `/app0/halo_guest.elf` (its size logged);
   `host_load_image(file, size, NULL, 0)` (no relocations: x32 does not
   move); `host_apply_debug_settings("/data/halo/config.toml", NULL)`;
   `host_run_guest_main(host_make_boot(&environment, "halo"))`.
7. The hold for the deploy's kill test stays, moved to where the game
   ends (`host_exit`): `/app0/hold.txt` makes `host_exit` sleep 120 s
   before `_exit`.

`host_exit` (`host_common.c`) ends with `_exit`, which the kernel log
follows with `SIGSYS` (phase 0 B). Before `_exit`, the PS5's sink flushes
nothing (each line is a write) and closes the file; the `SIGSYS` is
explained once in the README. If a cleaner end is found (PS5_Vulkan's
`docs` may say; `sceSystemServiceLoadExec` with no path is one thing to
try), say so under "Progress"; it is not required.

### Memory (`host_memory.c`, the PS5's)

Copy Android's file and keep its shape: the same `host.h` functions, the
pools (`struct pool`, `pool_take`, `pool_release`), `guest_pages`,
`host_guest_mmap`'s rules (`MAP_FIXED` over the guest's own pages replaces,
`MAP_FIXED_NOREPLACE` over used pages is `-EEXIST`, a range outside the
guest's is `-ENOMEM`), the watch (`page_protected`, `page_generation`,
`mark_written`, `host_memory_watch_*`) and `report_crash`. Replace the
system underneath:

- **Reserving.** `reserve(address, size)` is `sceKernelReserveVirtualRange`
  at the address, checked to have landed there (the probe's
  `reserve_exactly`: a reservation placed elsewhere is unmapped and counts
  as taken), then `sceKernelAllocateDirectMemory` and
  `sceKernelMapDirectMemory` fixed, read-write, so a reserved range is
  backed memory. The three fixed ranges and every pool are reserved this
  way; a pool is `POOL_SIZE` of direct memory from its creation. Direct
  memory comes back holding what it held: a range is zeroed when it is
  given out (`host_low_map`, a `MAP_FIXED` mapping, a pool's pages), not
  when it is reserved, so the cost is paid for what is used.
- **The map.** `find_gap`, `log_conflicts` and `host_memory_report_low_mappings`
  read `sceKernelVirtualQuery` with the find-next flag, as the host probe's
  section 2 walks the map (the 72-byte info struct, 15 regions on this
  console), instead of `/proc/self/maps`. A pool goes in the lowest gap of
  `POOL_SIZE` at or above 16 MB that does not touch the three fixed
  ranges.
- **Protection.** `host_low_map`'s protection and the guest's `mprotect`
  are libc's `mprotect` (one call with `sceKernelMprotect` on direct
  memory, phase 0 B); `pool_release` sets `PROT_NONE` and keeps the memory
  (the address space stays reserved, as Android's does; the memory is
  direct memory, which the heap would not reuse anyway). `madvise` on
  direct memory is answered 0 without a call unless the SDK's libc has one
  that works; say which.
- **Execute.** The loader's `mprotect(PROT_READ | PROT_EXEC)` of the
  image's code is libc's and works on direct memory (phase 0 B ran code so
  protected in the guest probe). Nothing asks for execute at map time.
- **`__wrap_mmap`.** `host_loader.c` maps the image's range itself
  (`mmap(low, size, RW, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED)`), which on
  the PS5 would put flexible memory over the direct memory reserved for it.
  The link wraps `mmap`: `__wrap_mmap` answers a `MAP_FIXED` anonymous
  mapping that lies inside a range this file reserved by zeroing it and
  setting its protection, and hands everything else to `__real_mmap`
  (RADV's, SDL2's and glslang's own mappings, above 4 GB, go through
  untouched). Log the wrapped calls at `HOST_LOG_INFO` the first ten
  times, so phase 4 sees whether RADV ever maps below 4 GB.
- **`host_memory_initialize(preferred_base, size, base)`**: the image is
  already reserved by `host_memory_reserve_early`; this checks the range
  asked for lies within it, writes `*base = preferred_base` (x32 never
  moves; a request that does not fit is `-1` with the log saying so), and
  records `image_base`/`image_end` for the bookkeeping. The window's base
  and size answer `host_memory_window_base`/`_size` from the early
  reservation (the guest reads them through the boot block, as Android's
  does). `host_memory_fixed_unavailable` is 1 if any of the three ranges
  could not be reserved.
- **File mappings.** A guest `mmap` with a descriptor (`fd != -1`) is
  answered as the Switch's is: the range from a pool or the fixed address,
  then `pread` into it (the game maps nothing large this way; say under
  "Progress" what it did map, if anything).
- **The watch** stays as Android's: `host_memory_watch_protect` is
  `mprotect(PROT_READ)` a page at a time; the fault handler recognises a
  protected page of the window, marks it and reprotects it read-write. On
  the PS5 the handler is installed for `SIGSEGV` and `SIGBUS` both with
  `SA_SIGINFO | SA_NODEFER | SA_ONSTACK`, and both handlers check for a
  watched page first (FreeBSD may raise either; phase 0 B saw `SIGSEGV`
  with `si_code 2`). `report_crash` reads the registers through
  `host_arch_context` (the PS5 branch) and walks frames as Android's does;
  the `addr2line` line says `build/ps5/halo_guest_vk.elf`. `chain` to the
  previous handler stays.
- **`host_memory_watch_use_hashes`** stays callable and does what it does
  (no ARM translation here; it is never called).

### System calls (`host_syscall.c`, the PS5's)

Android's file is the structure (one `switch` on the guest number, each
call translated and made with libc, results as the guest's 32-bit `long`
with Linux's negative `errno`); the Switch's is the inventory of what the
guest asks on a non-Linux system. Every one of the 91 numbers in
`halo_guest_syscall.h` is either answered or returns `-ENOSYS` on purpose,
with a comment saying which calls take the fallback and why (as the test of
the numbering expects: musl falls back from `statx` to `newfstatat`, from
`clone3` to `clone`, which the guest never reaches because threads go
through `host_thread_create`).

The translations, in `host_syscall_tables.c` with their numbers in
`freebsd_abi.h` (each number a literal with a comment naming the SDK
header it came from, and `host_syscall.c` asserting each against that
header with `_Static_assert`, so the PC test and the console agree):

- **Open flags** both ways: the guest's are AArch64 Linux's
  (`GUEST_O_*` in the header); FreeBSD's differ for `O_CREAT` (0x200),
  `O_TRUNC` (0x400), `O_EXCL` (0x800), `O_APPEND` (8), `O_NONBLOCK` (4),
  `O_DIRECTORY` (0x20000), `O_NOFOLLOW` (0x100), `O_CLOEXEC` (0x100000),
  `O_DIRECT` (0x10000), `O_SYNC`; `O_LARGEFILE`, `O_NOATIME`, `O_PATH`,
  `O_TMPFILE` have no FreeBSD flag and are dropped (`O_PATH` makes the open
  `O_RDONLY | O_DIRECTORY`-free; say what musl asks it for). `fcntl`'s
  `F_GETFL` answers in the guest's flags.
- **`AT_*`**: `AT_FDCWD` is -100 on both; `AT_SYMLINK_NOFOLLOW` 0x100 to
  0x200, `AT_REMOVEDIR` 0x200 to 0x800, `AT_EMPTY_PATH` has no FreeBSD
  flag (musl's `fstat` uses it on an empty path: answer with `fstat`).
- **`errno`**: FreeBSD's number to Linux's for every value the calls can
  return (`EAGAIN` 35 to 11, `ETIMEDOUT` 60 to 110, `ENOSYS` 78 to 38,
  `ENOTEMPTY` 66 to 39, `EOVERFLOW` 84 to 75 and the rest; the first 34
  agree).
- **`struct halo_guest_kstat`** from FreeBSD's `struct stat` field by
  field (device, inode, mode, links, uid, gid, rdev, size, blksize,
  blocks, the three times as the guest's 32-bit `timespec`); the mode's
  type bits agree, the permission bits agree.
- **`getdents64`**: FreeBSD's `getdirentries` records (`d_fileno`,
  `d_reclen`, `d_type`, `d_namlen`, `d_name`) repacked as Linux's
  `linux_dirent64` (inode, offset, reclen, type, name, 8-byte aligned),
  `d_type` values agreeing; the offset the next call continues from kept
  per descriptor (`lseek` on the directory, as FreeBSD's `readdir` does).
- **Clocks**: `CLOCK_REALTIME` 0, `CLOCK_MONOTONIC` 1 to FreeBSD's 4,
  `CLOCK_BOOTTIME` 7 to `CLOCK_UPTIME` 5, `CLOCK_PROCESS_CPUTIME_ID` 2 and
  `CLOCK_THREAD_CPUTIME_ID` 3 to FreeBSD's 15 and 14; the guest's
  `timespec` is 32-bit seconds (the header says why).
- **`pollfd`, `iovec`**: `pollfd` is 8 bytes on both and the event bits
  agree for `POLLIN`, `POLLOUT`, `POLLERR`, `POLLHUP`, `POLLNVAL`; `iovec`
  is converted as Android's does.

The calls, by family (follow Android's answers for the semantics and the
Switch's file for what a non-Linux libc needs):

- **Files**: `openat` (flags translated, mode as is), `close`, `read`,
  `write`, `readv`, `writev`, `pread64`, `pwrite64`, `preadv`, `pwritev`
  (the offset in two halves, as Android's), `_llseek` (the result written
  back as 64 bits), `fsync`, `fdatasync` (`fsync`), `ftruncate`,
  `newfstatat`, `fstat`, `faccessat` (the platform layer's `access`, phase
  0 B: it answers), `readlinkat` (`/proc/self/exe` answered
  `/data/halo/halo`, the "executable's folder" the game searches for its
  data; anything else through libc), `mkdirat`, `unlinkat` (with
  `AT_REMOVEDIR` as `rmdir`), `renameat`, `renameat2` (flags 0 only),
  `fchmod`, `fchmodat`, `utimensat`, `getdents64`, `getcwd` (answered
  `/data/halo` by the host: libc's crashes a title, phase 0 B), `chdir`
  (accepted for `/data/halo` only), `statfs`/`fstatfs` (from `statvfs`'s
  figures into the guest's layout, or `-ENOSYS` if the game never asks;
  say which), `statx` (`-ENOSYS`), `dup`, `dup3`, `pipe2`, `fcntl`
  (`F_GETFL`, `F_SETFL`, `F_GETFD`, `F_SETFD`, `F_DUPFD_CLOEXEC`; locks
  `-ENOSYS`), `ioctl` (`-ENOTTY`), `flock`, `umask`.
- **Standard streams**: `write` and `writev` to 1 and 2 go to the host's
  log, prefixed `game: ` (Android's puts them in logcat), a line at a time.
- **Memory**: `mmap`, `munmap`, `mprotect`, `madvise` to `host_memory.c`;
  `mremap` `-ENOSYS` (musl's `realloc` falls back); `brk` `-ENOMEM`
  (musl then uses `mmap`; the bootstrap answered 0, which also works; use
  Android's answer).
- **Threads and synchronisation**: `futex` to `host_futex.c`;
  `set_tid_address`, `gettid` (the thread id the PS5 branch of
  `host_debug.c` uses), `getpid`, `getppid`; `sched_yield`;
  `nanosleep`, `clock_nanosleep` (relative and `TIMER_ABSTIME`, the clock
  translated), `clock_gettime`, `clock_getres`, `gettimeofday`, `times`,
  `getrusage` (zeros are acceptable, logged once), the `*itimer` and
  `timer_*` calls `-ENOSYS`; `clone`, `clone3`, `execve`, `wait4`,
  `waitid` `-ENOSYS`; `tkill`, `tgkill`, `kill` `-ENOSYS` (the guest's
  signals are the host's).
- **Signals**: `rt_sigaction`, `rt_sigprocmask`, `sigaltstack`,
  `rt_sigtimedwait` answered as Android's does (the host owns the
  handlers).
- **Identity and limits**: `getuid` and the rest 1000; `uname` with
  `Linux`, a release of `5.15.0` and `x86_64` (the guest is a Linux
  program and reads these); `sysinfo` with the direct memory's size and
  free block as total and free RAM; `getrlimit`/`prlimit64` with
  `RLIMIT_STACK` 8 MB, `RLIMIT_NOFILE` 1024 and infinity elsewhere.
- **Networking** (`sendmsg`, `recvmsg`, `epoll_pwait`, `ppoll`, `pselect6`)
  `-ENOSYS` in this phase, with phase 6 named in the comment; the game must
  start without them (the desktop guest starts with the network down).
- **Random**: `getrandom` from `arc4random_buf`. `membarrier` `-ENOSYS`.
- **Exit**: `exit_group` is `host_exit`; `exit` of a thread is the thread's
  end, as Android's handles it.

Every call the guest makes that answers `-ENOSYS` is counted by number,
and the counts logged when the game ends, so the write-up lists what the
game asked for and did not get.

### Futexes (`host_futex.c`)

Phase 0 B's table, as written under "Progress": `FUTEX_WAIT` compares first
and answers `-EAGAIN` on a mismatch, else `UMTX_OP_WAIT_UINT_PRIVATE` with
a relative `struct _umtx_time` on `CLOCK_MONOTONIC` or none;
`FUTEX_WAIT_BITSET` the same with `UMTX_ABSTIME` and the clock the guest
named; `FUTEX_WAKE` and `_BITSET` as `UMTX_OP_WAKE_PRIVATE`, answering 0;
`FUTEX_REQUEUE` as wake `n` then wake all on the second word; the
priority-inheritance operations `-ENOSYS`; `ETIMEDOUT` and `EINTR` to
Linux's numbers. The word is a guest address below 4 GB, used as it is.
`FUTEX_PRIVATE_FLAG` and `FUTEX_CLOCK_REALTIME` stripped from the
operation first.

### Threads

`host_thread.c` is used as it is: stacks from `host_low_map` (a pool, direct
memory), `pthread_create` with `pthread_attr_setstack` (under the RADV
recipe's `--wrap=pthread_create`, which the host probe's waiters already
went through), the thread pointer in `__thread` storage, the reaper, the
signal stacks. Two things to check and log once: that `__thread` variables
work on a thread the platform layer's wrapper made (`host_get_tp` after
`host_set_tp` on a new thread), and that the thread id `host_debug.c`'s PS5
branch reports is distinct per thread.

### The stand-ins (`host_sdl_ps5.c`, `host_vk_null.c`, `host_gl_none.c`, `host_update_none.c`)

The game must reach its menu and run its loop with no display, no pad and
no sound. Its calls cross as `host_sdl.c`'s do (integers, floats, strings,
flat structures; handles for SDL objects): read `guest_sdl.c` and
`guest_desktop.c` for what each expects back, and the Switch's `host_sdl2.c`
for the stand-in window and context. `host_sdl_ps5.c` keeps a small handle
table (the Switch's `handle_new`/`handle_get` shape).

- **Start-up**: `host_sdl_init` 1; `set_hint` 1; `get_error` ""; `ticks`
  and `ticks_ns` from `CLOCK_MONOTONIC`; `thread_id` a small per-thread
  number; `get_base_path` `/data/halo/` (so the game's search finds
  `maps/` and `config.toml` there); `get_hint_boolean` the default passed;
  `has_screen_keyboard_support` 0; `text_input_*` no-ops.
- **The window and the context**: `create_window` one handle, 1920x1080,
  fullscreen; `window_size_in_pixels` and `get_window_size` 1920x1080;
  `set_window_size`, `set_window_fullscreen`, `_mode`, `destroy_window`,
  `warp_mouse_in_window`, `set_relative_mouse` accepted; `get_window_flags`
  fullscreen and shown; `gl_set_attribute` 1; `gl_create_context` a
  handle; `gl_make_current` 1; `gl_set_swap_interval` kept; **`gl_swap_window`
  paces the game**: with a swap interval above 0, sleep until the next
  tick of 1/59.94 s (`clock_nanosleep` with `TIMER_ABSTIME` on the
  monotonic clock, the Switch's way under deko3d before it presents), and
  count frames: every 5 s one log line with the frames and the mean and
  longest frame, which is the headless run's heartbeat.
- **Displays**: `get_display_for_window` and `get_primary_display` 1;
  `get_display_mode` 1920x1080 at 59.94 Hz, 32-bit; `get_fullscreen_display_modes`
  that one mode; `get_closest_fullscreen_display_mode` it;
  `get_display_usable_bounds` 1920x1080.
- **Events**: `poll_event` 0 events, `pump_events` nothing; `push_event`
  keeps the pushed event in a small queue that `poll_event` returns (the
  guest pushes its own); `get_gamepads` none; `open_gamepad`,
  `gamepad_from_id` 0; `gamepad_axis`, `_button`, `_type` 0;
  `rumble_gamepad`, `close_gamepad` accepted; `scancode_name` and
  `_from_name` from SDL3's own table (a string array; the desktop guest
  maps keys by name at start).
- **Audio**: `get_audio_devices` one device, `get_audio_device_name`
  "PS5"; `open_audio_stream` a handle with the stream's format and rate;
  `put_audio_stream_data` accepts and counts the bytes;
  `get_audio_stream_available` answers the bytes queued less what a device
  at the stream's rate would have played since the stream was resumed, not
  below 0, so the game's mixer keeps time and never fills without end;
  `get_audio_stream_data` (recording) 0; `resume_audio_stream_device`,
  `destroy_audio_stream` accepted. Phase 3 puts SDL2 under this.
- **The desktop's dialogs**: `show_message_box` logs the title, the
  message and the buttons, and chooses the button whose text is "Yes"
  (else the first), so the first start's question about the disc image is
  answered; `open_file_dialog` logs the filters and answers the first
  `.iso` or `.xiso` (either case) found in `/data/halo`, else fails, so the
  extraction gets its image; `show_simple_message_box` and `show_toast`
  log; the clipboard a static string; the 2D renderer's ten calls
  (`create_renderer` to `render_present`) accepted, a handle, no drawing,
  logged once per name.
- **`host_vk_null.c`**: `host_vk_submit` reads nothing and advances a
  counter; `host_vk_retired` returns it (every submission retired at once,
  so the game never waits); `host_vk_shader_find` returns 0 with the
  status "compiling" (`vk_commands.h`'s value), so every draw is skipped
  as the renderer does for a shader not ready; `host_vk_shader_compile`
  accepted; `host_vk_format_supported`, `host_vk_bc_supported` 1,
  `host_vk_samples_supported` 1, `host_vk_visibility` 0,
  `host_vk_driver_name` "none (phase 2)"; `host_vk_exit` nothing;
  `host_renderer_vulkan` 1 (the image is the Vulkan one),
  `host_vk_presenting` 0, `host_vk_window` NULL.
- **`host_gl_none.c`**: as in the table above. The vk image's shared
  files still make some GL calls (Android's `host_gl.c` reports them under
  `debug.gpu_stats`); here each name is counted, and the count logged at
  the end, so phase 4 knows what they are.
- **`host_update_none.c`**: `host_update_executable_path` writes
  `/app0/eboot.bin`; the other five return failure with a log line once.

### The generated import stubs (`host_import_stubs.c`)

`tools/ps5_build.py` writes, from the same import lists the guest's table
is made of, a C file with one **weak** function per import name: `long
name(long a, long b, long c, long d, long e, long f)` that calls
`host_fatal("import %s is not answered (%lx, %lx, %lx, %lx)", ...)`. A real
definition in any host object overrides it at link time, so the link
succeeds while the host is incomplete and a missing import ends the run
with its name and arguments in the log, as the bootstrap did. The file
stays for good (a safety net), and the acceptance checks that **no stub
is left** that the game reaches in this phase: `llvm-nm` on `eboot.elf`
lists the weak symbols still standing, and the write-up lists them (they
should be imports the game cannot reach without a pad or a display, or
none).

### The build

- `tools/ps5_build.py`: the host's sources are `port/ps5/host/*.c`,
  `port/android/host/{host_arch,host_common,host_debug,host_loader,host_thread,host_watch_hash}.c`,
  `port/third_party/tomlc17/tomlc17.c`, the guest build's
  `host_import_table.c` and the generated `host_import_stubs.c`; the flags
  gain `-I port/linux/src -I port/third_party/tomlc17 -I port/android/include`
  (already) and keep `-DHALO_PS5=1`; the link gains `--wrap=mmap`
  (`tools/ps5_title.sh` passes flags after `--` as it passes inputs). The
  `host_bootstrap.*` files and the syscall-names rule go; the host's
  compile stays `-Werror`, and a shared file that warns under the PS5
  toolchain is fixed behind `HALO_PS5` or argued.
- `tools/test_ps5_host.py`: compiles `port/ps5/tests/host_syscall_tables_test.c`
  with `host_syscall_tables.c` using the PC's compiler and runs it: the
  open flags map both ways and round-trip, the `errno` table covers every
  Linux value the calls return, `AT_*` and the clocks map, a `kstat` filled
  from given fields has the header's layout, a `dirent64` packs and aligns.
  `pytest -q tools/test_ps5_host.py tools/test_ps5_build.py`.

### Testing on the console

The user puts the disc image (`.iso` or `.xiso`) in `/data/halo` over FTP
before the first run, and nothing else (the user owns it; ask, do not copy
one yourself). `debug.exit_after = 60` is not yet in `config.toml` (the
game writes the file on its first run); the host passes it through the
environment for the runs that need it (`HALO_EXIT_AFTER=60`, read by
`port_config.c` as the comment in its table says).

1. **The first start**: `--run` with `/data/halo` holding only the disc
   image. Expected in the log, in order: the three ranges reserved; the
   image loaded, `274 imports (0 unavailable)` or the `hostgl_*` names
   unavailable only; musl's start (the same first calls as phase 1's); SDL
   init, the window and the context stand-ins; the game's own output
   (`game: ` lines: it prints its start-up); the message box about the disc
   image answered "Yes"; the file dialog answering the image; the
   extraction's progress (the guest draws it through the 2D renderer:
   count the `render_present` calls and log the files the guest opens
   under `maps.partial/`, so the log shows it moving) and its time;
   `maps.partial` renamed `maps`; the main menu's map loaded (the game
   opens `maps/ui.map` and more); the heartbeat lines at 60 frames a
   second; the exit after 60 s with `the game exited (0)` and the counts
   (the `-ENOSYS` calls, the GL names, the stubs reached, if any).
2. **The second start**: no question, no extraction, the menu's map at once;
   the time from launch to the first heartbeat.
3. **Without `exit_after`**: five minutes of the loop at the menu (the menu
   cycles its attract mode on its own), the heartbeat steady, the host's
   heap peak and the direct memory free block logged every minute (phase
   0 B's figures, in the heartbeat line), then `kill` from the deploy.
4. **A crash on purpose**, once: `HALO_PS5_CRASH_TEST=1` in the environment
   makes the host write to an unmapped guest address from the game thread
   after the image is loaded; the log must show `report_crash`'s lines
   (the signal, the registers, the frames, the `addr2line` line) before
   the process dies. Then the setting is removed.
5. The console is left with the title, `/data/halo/{maps,save,config.toml,halo.log}`
   and the disc image.

### Acceptance

The review checks each of these:

1. `ninja ps5` builds with `-Werror`; `ninja linux_x86_64` builds; the
   Android and Switch objects of `host_arch.c` and `host_debug.c` are
   unchanged (compare them before and after, as phase 1 did);
   `pytest -q tools/test_ps5_host.py tools/test_ps5_build.py
   tools/test_host_units.py tools/test_host_syscall.py` passes.
2. The first start's log from a folder holding only the disc image shows
   the extraction and the menu's map loaded, and the exit at 60 s; the
   second start's log shows the menu's map without the question.
3. Five minutes at the menu: the heartbeat at 60 frames a second
   throughout (within a frame), the heap's peak and the free block
   logged, no fault.
4. The crash test's log shows the registers and frames.
5. `llvm-nm` on `eboot.elf`: the weak stubs still standing are listed
   under "Progress", and none of them appears in any run's log.
6. "Progress" has: the `-ENOSYS` counts and what asked for them, the GL
   names the vk image called, the `__wrap_mmap` calls seen, the thread
   checks, the extraction's time and the first map's load time, the heap
   and direct memory figures over five minutes, the `HALO_PS5` branches
   argued, and anything unexpected.
7. `host_bootstrap.c`, `host_bootstrap.S` and the syscall-names rule are
   gone; `host_main.c`'s header comment says what it does now.
8. Nothing changed outside `port/ps5/`, `tools/ps5_build.py`,
   `tools/ps5_title.sh`, `tools/test_ps5_host.py`, and the two shared
   files' `HALO_PS5` branches; nothing of PS5_Vulkan's copied in; FreeBSD's
   numbers are literals in our header, asserted against the SDK's.

Don't commit; the review does that. Record anything unexpected under
"Progress", even if it was dealt with.

**Ends with:** the game, started from `ninja ps5` against a `/data/halo`
holding only the disc image, extracts `maps/`, loads the main menu's map and
runs its loop at 60 frames a second with nothing drawn, for as long as it is
left, and exits when told.

## Phase 3 — SDL: pad, audio, events

**State (2026-10-18):** the code is written and run 1 below passed on the
console; the user moved the rest of "Testing on the console" (runs 2 to 7)
to the end of phase 4, so that the pad, the sound and the focus events are
tried with the picture on the TV (see phase 4, "Phase 3's runs"). Runs 2
and 4 need a match, which the network test cannot host before phase 6
(the server needs a socket: "Progress"). Two statements below were wrong
and are corrected in place: SDL2's touch pad button is 20, as SDL3's; and
the Quick Menu sets the status's second flag byte for as long as it is up,
not for 0.1 s.

Written to be picked up by an agent that has not seen the work so far. Read
first, in this order: this plan's "Decisions" (the SDL row: pacbrew's SDL2,
never its video), "Where the port lives" and "How this plan is worked";
phase 0 B's SDL2 probe answers and phase 2's entry under "Progress" whole
(what the stand-ins do today, the audio callback stream, the system's
overlay flags, the two joysticks the console lists); then the code.
`port/ps5/host/host_sdl_ps5.c` whole (the file this phase changes: its
handle table, its stand-ins, its heartbeat and the clock thread that drives
the mixer), `port/ps5/host/host_main.c` (where SDL would be initialised) and
`ps5_host.h`; the Switch's `port/switch/host/host_sdl2.c` whole and
`host_sdl3_events.c` (the model: SDL3's calls answered by SDL2, the event
translation, the gamepad handles, and the audio callback handed from SDL's
thread to one with a guest stack: `audio_binding`, `audio_callback`,
`audio_thread`); Android's `port/android/host/host_sdl.c` (the same bridge
over SDL3, and the audio hand-off there); the guest's side,
`port/android/guest/runtime/guest_sdl.c` (every `host_sdl_*` call's
arguments and what it expects back, the SDL3 layouts it copies),
`port/linux/src/xinput_sdl.c` (how the game lists, opens and reads
gamepads: `SDL_GetGamepads`, `SDL_GetGamepadFromID`, `SDL_GetGamepadAxis`,
`SDL_GetGamepadButton`, `SDL_GetGamepadType`, `SDL_RumbleGamepad`, in two
passes by type), `port/linux/src/dsound_sdl.c` (the audio: one callback
stream, `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` 1024 or 512, 48 kHz float
stereo) and `port/linux/src/sdl_platform.c`'s event loop (the events the
game reads: quit, focus lost and gained, gamepad added and removed; the
rest are a keyboard's, a mouse's and a touch screen's, which the console
has none of); the phase 0 B probe `port/ps5/probe/sdl2/main.c` (working
SDL2 on the console: the init flags, the controller listing and mapping,
the device open with a callback, the rumble); pacbrew's SDL2 headers under
`~/projects/ps5-deps/PS5_Vulkan/.deps/pacbrew/v0.40.2/sysroot/user/homebrew/include/SDL2/`
(`SDL_gamecontroller.h`, `SDL_audio.h`, `SDL_events.h`) and the SDL3
headers under `build/ps5/third_party/SDL3/include` (the guest's values).
The work is reviewed against the acceptance list at the end of this phase.

### What this phase is, and is not

- **It is** the pad, audio and events of `host_sdl_ps5.c` made real over
  pacbrew's SDL2: the game's gamepads from SDL2's game controllers, its
  sound through SDL2's audio device, its events from SDL2's and from the
  console's overlay flag; and the proofs on the console, by ear and by the
  game's own logs, since nothing is drawn yet.
- **It is not** the picture (phase 4) or the PS button's handling beyond a
  focus event (phase 5), split screen's several users (phase 5), the
  microphone or any keyboard. The stand-ins for the window, the context,
  the displays, the 2D renderer and the dialogs stay as they are; the swap's
  pacing stays until phase 4's swapchain takes it over.
- **SDL2's video is never initialised** (phase 0 B: it crashes in
  `libSceKeyboard`). `SDL_Init` is asked for `SDL_INIT_TIMER | SDL_INIT_EVENTS
  | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO` and nothing else, and the
  code says why.
- **Nothing changes** outside `port/ps5/host` (and `tools/ps5_build.py` if
  a flag is needed); no shared file, no probe. SDL2's headers are pacbrew's
  (read, linked against), SDL3's the guest's; nothing of either is copied.

### Files

| File | What |
|---|---|
| `port/ps5/host/host_sdl_ps5.c` | the pad, audio and event parts over SDL2 (below); the stand-ins kept. If the file passes about 1,500 lines, the pad and events may move to `host_sdl_ps5_input.c` and the audio to `host_sdl_ps5_audio.c`, with what they share in `ps5_host.h`; say so under "Progress" |
| `port/ps5/host/host_main.c` | nothing, unless SDL2 has to be initialised before the game's thread (it does not: `host_sdl_init` is the game's first SDL call, on its thread) |
| `port/ps5/README.md` | the pad and the sound: what works, the system's overlay, what the log says |

### SDL2 under the bridge

`host_sdl_init(flags)`: the guest asks for SDL3's init flags (`guest_sdl.c`
passes them through); the host calls `SDL_Init` with SDL2's timer, events,
game controller and audio flags, whatever the guest asked for, once (a
second call adds nothing), logs `SDL_GetVersion` and the drivers
(`SDL_GetCurrentAudioDriver`: "ps5"), and returns 1; on failure it logs
`SDL_GetError` and returns 0, and the game says what it does without SDL.
`host_sdl_get_error` returns `SDL_GetError`'s text. `host_sdl_set_hint`
keeps `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` for the device's period as
today and passes the rest to `SDL_SetHint` (the names agree between the
versions for the hints the game sets; log any SDL2 refuses).

**The pad.** SDL2's game controller API, as the Switch's file uses it:

- `host_sdl_get_gamepads`: the joysticks `SDL_IsGameController` lists,
  **less the console's own "PS5 Remote Control"** (phase 0 B: the console
  lists it beside the pad as a controller of type `SDL_CONTROLLER_TYPE_UNKNOWN`,
  the pad as `SDL_CONTROLLER_TYPE_PS5`, 7): a controller whose type is
  unknown and whose name is that is left out, logged once. Each one's
  instance id (`SDL_JoystickGetDeviceInstanceID`) is the id the guest sees;
  the game opens them in two passes by type (`xinput_sdl.c`), so the ids'
  order does not matter.
- `host_sdl_open_gamepad(id)`, `host_sdl_gamepad_from_id(id)`:
  `SDL_GameControllerOpen` by the device index of that instance id (the
  Switch's lookup), a handle in the table; `close_gamepad` closes it.
- `host_sdl_gamepad_axis(handle, axis)`: `SDL_GameControllerGetAxis` with
  SDL3's axis number, which is SDL2's (left x, left y, right x, right y,
  left trigger, right trigger: 0 to 5 in both), the value as it is
  (-32768 to 32767; the game applies its own dead zone).
- `host_sdl_gamepad_button(handle, button)`: SDL3's button numbers are
  SDL2's for the fifteen the game reads (south, east, west, north, back,
  guide, start, the sticks, the shoulders, the d-pad: 0 to 14), and on to
  the touch pad click, 20 in both (misc 1 and the four paddles between are
  the same too; corrected: this said SDL2's was 15). SDL3's 21 on (misc 2
  to 6) are not SDL2's and read 0. The pad's mapping on the console (phase 0 B) has no back, guide or
  misc button: they read 0.
- `host_sdl_gamepad_type`: SDL2's type to SDL3's as the Switch's
  `host_sdl3_gamepad_type` maps them (PS5 to `SDL_GAMEPAD_TYPE_PS5`,
  unknown to unknown).
- `host_sdl_rumble_gamepad(handle, low, high, ms)`:
  `SDL_GameControllerRumble` (phase 0 B: the pad shook).
- `host_sdl_gamepad_gyro` is not among the desktop guest's imports; nothing
  to do.

**Events.** `host_sdl_poll_event` polls SDL2 (`SDL_PollEvent`) and
translates what the game reads into an SDL3 `SDL_Event` (128 bytes, the
guest's layout; the Switch's `host_sdl3_events.c` is the model, written
again here in the PS5's file with only the kinds the console produces):
`SDL_CONTROLLERDEVICEADDED` and `REMOVED` to `SDL_EVENT_GAMEPAD_ADDED` and
`REMOVED` with the instance id (the game then lists its pads again, so a
pad switched off and on comes back); `SDL_QUIT` to `SDL_EVENT_QUIT` (SDL2
sends none here; kept for form). Button and axis events are dropped: the
game polls the pad's state each frame. The guest's own pushed events
(`host_sdl_push_event`) keep their queue and are returned first.

**The system's overlay** (phase 0 B: the title runs on under the PS button's
menu, and only the pad's `intercepted` bit and `isSystemUiOverlaid` from
`sceSystemServiceGetStatus` tell; the Quick Menu sets the second flag byte
instead of the first, for as long as it is up: phase 0 B's run 3 shows
`00 01` from 19.875 s to 23.780 s, so either byte is the overlay). Each `poll_event` asks
`sceSystemServiceGetStatus` (declared as the host probe declared it, with
its 16 KB buffer, the first byte read; a few microseconds) and, when the
flag changes, returns `SDL_EVENT_WINDOW_FOCUS_LOST` or `FOCUS_GAINED` as
the desktop's window would: the game then clears its held inputs
(`sdl_platform.c`) and `input_state.focused` follows. What the game does
beyond that while overlaid (keep playing, pause) is phase 5's; this phase
only reports it. The pad's state while intercepted is whatever SDL2
reports (the probe showed no buttons); log once what it was.

**Audio.** The guest's stream is a callback stream (`dsound_sdl.c`: 48 kHz,
float, stereo, a callback that mixes a period); the Switch's
`host_sdl_open_audio_stream` and its `audio_binding` are the model:

- `SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0)` with the guest's
  format (SDL3's `SDL_AUDIO_F32` is SDL2's `AUDIO_F32SYS`, 0x8120: pass the
  value), channels and rate, `samples` the period the guest set through
  the hint (1024, or 512 for `audio.low_latency`), a callback of the
  host's; the obtained spec logged (phase 0 B: SDL2 gives back exactly what
  is asked, 48000 Hz, 2 channels, 256 samples when 256 were asked). A device
  that will not open is logged and the stream fails (the game runs mute, as
  the desktop does without a device).
- **The callback runs on SDL2's audio thread, which has no guest stack**, so
  it cannot call the guest. As the Switch and Android do: a thread of the
  stream's own with a guest stack (`host_native_thread_create`, 256 KB)
  waits on a condition; SDL's callback hands it the buffer and the length,
  wakes it and waits for it to finish (the Switch's `requested` and `done`
  conditions); the guest's callback (`host_call_guest(callback, userdata,
  handle, length, length)`, as phase 2's clock thread calls it) fills the
  buffer; the audio thread returns it to SDL. The hand-off's cost is
  measured: the time from SDL's callback to the guest's return, min, median
  and max over each minute, in the log.
- `host_sdl_resume_audio_stream_device`: `SDL_PauseAudioDevice(device, 0)`
  (the device starts paused, so no callback runs before the handle exists);
  `destroy_audio_stream`: pause, stop the thread, `SDL_CloseAudioDevice`.
- `put_audio_stream_data`, `get_audio_stream_available` and
  `get_audio_stream_data`: the game uses a callback stream, so these are
  not called on it; keep phase 2's answers for a stream without a callback
  (phase 0 B's `SDL_QueueAudio` path is there if one appears; say under
  "Progress" if the game called them).
- **A stream without a callback is the microphone** (voice chat,
  `voice_audio.c`: 48 kHz mono floats, read by the game). The console's
  pad has one, through the system's voice services, not SDL2's recording
  (pacbrew's SDL2 may not record: try `SDL_GetNumAudioDevices(1)` and say).
  In this phase it returns 0 and voice chat is off, logged once; the row is
  phase 5's.
- The callback's cadence: the host counts SDL's callbacks and their gaps
  (min, median, max) every minute, as phase 0 B's probe did; a gap over
  twice the period is counted as a late callback. These lines are the
  proof of sound where nobody is listening.

**Timing.** `host_sdl_ticks` and `ticks_ns` stay on the monotonic clock
(phase 0 B: `SDL_GetTicks64` runs 1 ms behind in 5 s; the game's own
timing is the TSC's through the guest). `host_sdl_thread_id` stays.

**What stays a stand-in**, unchanged: `create_window` and the window
calls, the GL context, the displays, `gl_swap_window`'s pacing and
heartbeat, the 2D renderer, the dialogs, the clipboard, text input, the
scancode names (SDL2's table, already in use). `has_screen_keyboard_support`
stays 0.

### Testing on the console

Nothing is drawn, so the pad is proved by what the game logs and the sound
by what the user hears. The user is at the console with the TV's sound on
for runs 2 to 5; announce each.

1. **Start-up** (`--run`, `HALO_EXIT_AFTER=30`): `SDL_Init` and the drivers
   logged; `gamepads: SDL lists 1` in the game's output (the remote control
   left out, logged); the game opens the pad (its type PS5 in its log); the
   audio device opened with the obtained spec and the callbacks counted at
   the exit (about 47 a second at 1024 frames: 1400 in 30 s), no late
   callbacks.
2. **The pad, without a screen**: `HALO_NETWORK_TEST=host:bloodgulch`
   (`port/linux/NETCODE.md`, "Testing": the game hosts a match with no
   menus and logs every player's position, aim, movement, weapons and
   shots every second). The user, told what to do by notification and by
   the deploy's printout: hold the left stick forward for ten seconds, then
   turn with the right stick, fire with the right trigger, jump with cross,
   crouch with circle, switch weapons with triangle, melee with square,
   throw a grenade with the left trigger. The game's per-second lines must
   show the position moving while the stick is held, the facing turning,
   the shots, the jump and the weapon change; the user hears the match's
   sound (the ambience, the footsteps, the shots). Record the lines that
   show each.
3. **The menus, heard**: a plain start to the main menu; the user presses
   cross and circle a few times and up and down on the d-pad; the menu's
   sounds are heard on each (the user says). `debug.exit_after` ends it.
4. **Rumble**: in run 2's match a melee or a shot rumbles the pad (the game
   asks for it: `xinput_sdl.c`); the user says whether it did.
5. **The pad switched off and on** during run 2: `SDL_EVENT_GAMEPAD_REMOVED`
   and `ADDED` in the log, the game listing its pads again and the
   movement resuming.
6. **The PS button** during run 2: the focus events in the log when the
   menu opens and closes, the game's `input_state.focused` following (its
   log says what it did), the pad reading nothing meanwhile; then back to
   the match.
7. **Five minutes** of run 2's match with the user playing: the heartbeat
   steady at 59.94, the callback gaps' max and the late count, the hand-off's
   max, at the end.

### Acceptance

The review checks each of these:

1. `ninja ps5` builds with `-Werror`; the four test files of phase 2 still
   pass; nothing outside `port/ps5/host`, `port/ps5/README.md` and (if
   needed) `tools/ps5_build.py` changed; SDL2's video is never initialised
   (the init flags in the source, and no video driver in the log).
2. Run 2's log shows the position moving, the facing turning, the shots
   and the weapon change on the user's inputs, with the lines quoted under
   "Progress"; the user heard the match.
3. Run 3: the menu's sounds heard on the pad's presses.
4. Runs 5 and 6: the gamepad removed and added events and the focus events
   are in the log with what the game did.
5. Audio: the device's obtained spec is the game's (48000 Hz, 2 channels,
   float, the period asked); over run 7's five minutes the late callbacks
   are 0 and the hand-off's maximum is under a period; the user heard no
   gaps.
6. The remote control is left out of the game's pads, logged.
7. "Progress" has: the SDL2 init line and drivers, the controller listing
   with types, the mapping of SDL3 to SDL2 values the file relies on (axes,
   buttons, the touch pad, formats, types), the audio hand-off's figures,
   the callback cadence, what the game did on focus loss, whether any
   non-callback stream was asked for, and anything unexpected.

Don't commit; the review does that. Record anything unexpected under
"Progress", even if it was dealt with.

**Ends with:** the menus driven by the pad and heard through the TV, with
nothing drawn, and a match played blind by the user for five minutes with
its sound, the game's own log showing their inputs.

## Phase 4 — the Vulkan renderer on RADV

Written to be picked up by an agent that has not seen the work so far. Read
first, in this order: this plan's "Decisions" ("Shared code", "Shader
compiler", "Display", "Where the guest's memory goes"), "Where the port
lives" and "How this plan is worked"; phase 0 B's entry under "Progress"
(RADV's smoke test, the display's one mode, `vk_icdGetInstanceProcAddr`,
the memory figures, glslang's times), phase 2's (the null renderer, the
`__wrap_mmap` counts, the GL names) and phase 3's (the overlay, the runs
moved here); then `port/android/VULKAN.md`: "Decisions", "Where the
renderer lives", phases 1, 2, 5 and 6 and their "Progress" entries (the
renderer this phase brings up was built there, step by step, and its
pitfalls are written there); then the code. The renderer's host half,
`port/android/host/host_vk.h`, `host_vk_driver.h`, `host_vk.c`,
`host_vk_render.c`, `host_vk_present.c`, `host_vk_shaders.c` whole, and
`host_vk_data.c`, `host_vk_draw.c`, `host_vk_texture.c`,
`host_vk_visibility.c`, `host_vk_post.c` as far as their header comments
and what they ask of the platform; `host_vk_driver.c`'s header comment (the
file the PS5 replaces) and `host_vk_probe.c`'s first lines (the file it
does not compile); the guest's `port/android/guest/d3d8_vk.c`'s header and
`vk_commands.h`, `vk_shaders.h`; the PS5's `port/ps5/host/host_main.c`,
`host_vk_null.c` (what goes), `host_sdl_ps5.c` (the swap's pacing and
`host_vk_presenting`), `host_memory.c` (`__wrap_mmap`) and
`tools/ps5_build.py`; the phase 0 B host probe `port/ps5/probe/host/main.c`
sections 4 and 5 (a working instance, device and display-plane swapchain
on RADV, written from the specification: the sequence this phase's present
branch follows); PS5_Mesa's `src/amd/vulkan/winsys/ps5/radv_ps5_platform.c`
(what the driver does at start: its shader cache folder, AGC) and
PS5_Vulkan's `README.md` sections on RADV, the shader cache and the known
gaps, and `docs/HARDWARE_FINDINGS.md` where it names device loss. The work
is reviewed against the acceptance list at the end of this phase.

### What this phase is, and is not

- **It is** the Android renderer's host half compiled for the console and
  run on RADV: the driver opened as the linked ICD, the swapchain on the
  console's display plane, glslang linked in instead of loaded, the caches
  in the data folder; the game's picture on the TV, from its clears to the
  menus to a campaign level; and, at the end, phase 3's runs with a person
  at the console. It also gives the audit eyes: a screenshot of what was
  presented, written on request and fetched by the deploy.
- **It is not** performance work, the PS button's handling, native 4K
  rendering, split screen or the first start's picture (phase 5), nor the
  network (phase 6). RADV's known gaps are seen and recorded, not worked
  around, unless a one-line setting the renderer already has covers one.
- **Shared code is used as it is.** The renderer's files are compiled from
  `port/android/host`; what the PS5 needs of them goes behind `HALO_PS5`
  in small branches (the list below, each argued under "Progress", none
  near a third of its file), and the one file that is Android through and
  through, `host_vk_driver.c`, is replaced by a PS5 file. The guest's
  `d3d8_vk.c` is not touched. Android's and Linux's objects of the shared
  files must not change (compare them, as phases 1 and 2 did). Nothing of
  PS5_Vulkan's or PS5_Mesa's is copied in.

### Files

| File | What |
|---|---|
| `port/ps5/host/host_vk_driver_ps5.c` | **new**, in place of `host_vk_driver.c`: `host_vk_driver_open` returns RADV's `vk_icdGetInstanceProcAddr` (the archive is linked; no library, no `dlopen`), after setting RADV's environment (below); `host_vk_driver_verify` says "RADV, linked" and returns 1; `host_vk_driver_close` does nothing. The description names the pins |
| `port/ps5/host/host_vk_screenshot_ps5.c` | **new**: on request, the presented image written to a file (below) |
| `port/ps5/host/host_vk_null.c` | **deleted** |
| `port/ps5/host/host_main.c` | starts the renderer (below), reads the renderer's debugging switches from the environment, captures RADV's standard error into the kernel log |
| `port/android/host/host_vk.h` | `HALO_PS5`: no `VK_USE_PLATFORM_ANDROID_KHR`; the instance function list without `vkCreateAndroidSurfaceKHR` and with the display's (`vkCreateDisplayPlaneSurfaceKHR`, `vkGetPhysicalDeviceDisplayPropertiesKHR`, `vkGetDisplayModePropertiesKHR`, `vkGetPhysicalDeviceDisplayPlanePropertiesKHR`, `vkGetDisplayPlaneCapabilitiesKHR`) |
| `port/android/host/host_vk.c` | `HALO_PS5`: the instance extensions required and enabled are `VK_KHR_surface` and `VK_KHR_display` instead of `VK_KHR_android_surface` |
| `port/android/host/host_vk_present.c` | `HALO_PS5`: `host_vk_surface_make` makes a display plane surface on the one display's one mode (below); `native_window` is NULL and the window checks are skipped; `surface_extent` has no window to ask; the screenshot hook called after the blit is recorded |
| `port/android/host/host_vk_shaders.c` | `HALO_PS5`: `glslang_load` binds `G`'s fields to the linked glslang's functions instead of `dlopen`/`dlsym`; the cache root is `/data/halo/vk_cache`; no `SDL_AddEventWatch` (there is no background on a console; the title's end saves the cache) |
| `port/android/host/host_common.c` | `host_exit` calls `host_vk_exit` under `defined(__ANDROID__) \|\| defined(HALO_PS5)`, so the pipeline cache is saved when the game ends |
| `tools/ps5_build.py` | the host's sources gain the nine shared renderer files (not `host_vk_driver.c`, not `host_vk_probe.c`) and the two new PS5 files; `-DHOST_VK_GLSLANG_TAG` as Android passes it; `-I port/android/guest` if the renderer's headers need it |
| `tools/ps5_deploy.py` | `fetch` also brings `/data/halo/screenshots/*.ppm`, converted to PNG beside the log with Pillow (the machine has it) and deleted from the console after |
| `port/ps5/README.md` | the picture: what is on the TV, the caches, the debugging switches, the screenshot |

### The renderer on the console

**Start-up** (`host_main.c`, before the game's thread, after
`host_memory_reserve_early`, as Android starts it after the image's range
is reserved): `ps5_klog_capture_stderr("[radv] ")` first, so Mesa's
messages reach the kernel log (the host probe did this); then
`host_renderer_vulkan = host_vk_startup("", 0, line, sizeof line)` and the
`renderer: ...` line logged. The console has no GL fallback: if it returns
0 the title logs why and ends. The device, the queue, the frames and the
swapchain are made at the first `host_vk_submit`, as on Android
(`host_vk_backend_ensure`), on the game's thread.

**The driver** (`host_vk_driver_ps5.c`): before returning the entry
point, `setenv("MESA_SHADER_CACHE_DIR", "/data/halo/radv-shader-cache", 1)`
and `mkdir` of that folder: PS5_Mesa's winsys sets the folder to
`/app0/radv-shader-cache` only if nothing has (`setenv(..., 0)`), and the
title's folder is wiped by every install. `MESA_DISK_CACHE_DATABASE` is
left to the winsys. Whatever `RADV_DEBUG`, `MESA_VK_*` or `RADV_PERFTEST`
the host's environment holds (from `environment.txt`) reaches the driver as
it is, and the open logs the ones set.

**The surface** (`host_vk_present.c`'s branch, the host probe's sequence):
`vkGetPhysicalDeviceDisplayPropertiesKHR` (one display, "PS5 VideoOut"),
`vkGetDisplayModePropertiesKHR` (one mode, 3840x2160 at 59,940 mHz),
`vkGetPhysicalDeviceDisplayPlanePropertiesKHR` (one plane); a
`VkDisplaySurfaceCreateInfoKHR` with that mode, plane 0, stack 0,
`VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR`, identity transform and the mode's
extent; `vkCreateDisplayPlaneSurfaceKHR`. The surface's capabilities say
the rest and the shared code reads them: 2 to 5 images (the shared code
asks for the minimum plus one), the current extent fixed at 3840x2160,
`B8G8R8A8_UNORM` in the sRGB-nonlinear space (the first the shared code
looks for), FIFO (the only mode), identity transform, opaque alpha. A
surface lost or a swapchain out of date is handled by the shared code as
it is (the plane surface is made again: there is no window to wait for);
`extent_check` finds nothing to change. The game's back buffer (the
window stand-in's 1920x1080) is blitted letterboxed into the 3840x2160
image by the shared `host_vk_present_record`, linear filter: that is the
scaling the "Display" row decided.

**glslang** is the linked library: in `glslang_load`'s PS5 branch, each
`G` field is set to the function of the same name (the C interface is in
the link, phase 1), `G.library` marked loaded, nothing opened. The
compile thread is a plain pthread (no guest calls; under the recipe's
`pthread_create` wrap, as the host probe's threads were). The SPIR-V and
the pipeline cache go under `/data/halo/vk_cache/` as the file lays them
out, the folder named by `HOST_VK_GLSLANG_TAG` and the generator's version;
the pipeline cache is saved on the file's own schedule and at the game's
end through `host_vk_exit`.

**What the host reads from its environment** (`environment.txt`, since
the desktop guest's settings table marks `debug.vk_self_test`,
`debug.vk_present_marker` and `debug.gpu_stats` as Android's and would not
keep them in `config.toml`; phase 5 decides whether the table gets a PS5
platform): `HALO_VK_SELF_TEST=1` sets `host_vk_self_test`,
`HALO_VK_PRESENT_MARKER=1` sets `host_vk_present_marker`,
`HALO_GPU_STATS=1` sets `host_gl_statistics` and whatever the renderer's
counters line keys on; `HALO_PS5_SCREENSHOT=<seconds>` the screenshot
(below). Each is logged when set.

**The screenshot** (`host_vk_screenshot_ps5.c`): every N seconds after the
first present, when asked, the swapchain image just blitted into (before
it is presented; the surface's usage has transfer-source) is copied with
`vkCmdCopyImageToBuffer` into a host-visible buffer of the frame's own,
behind the frame's fence; when the fence has passed, a thread of the
host's (not the game's) writes `/data/halo/screenshots/<frame number>.ppm`
(binary P6, 3840x2160, the alpha dropped, BGRA to RGB), `chmod 0666`, and
logs it. At most 20 files are kept (the oldest deleted). The hook is two
lines in the present file's PS5 branch, after the blit's barrier; the
copy's cost is logged once (it is the audit's, not the player's).

**Memory.** RADV's allocations and the renderer's upload rings and images
come from direct memory, in the one 12 GB budget with the guest's 855 MB
and the host's heap (phase 0 B). The `__wrap_mmap` counts tell whether
RADV ever maps below 4 GB (phase 2 saw none without it); the heap and
direct memory figures in the heartbeat now include the renderer. Record
both at the menu and in a map.

**Device loss.** A GPU fault under RADV ends in `VK_ERROR_DEVICE_LOST`;
the shared code reports it (`B.dead`, the device-lost report with the
startup line) and records nothing more. On the console that means a
black TV with the game running on; the host ends the title after the
report (the log is the evidence). `MESA_VK_ABORT_ON_DEVICE_LOSS=1` in the
environment makes RADV abort at the fault instead, with Mesa's own
message in the kernel log: use it when hunting one.

**What the game draws with lines**, for RADV's gap: `d3d8_vk.c` draws
`D3DPT_LINELIST`, `LINESTRIP` and `LINELOOP`; find what the game draws
with them (the debug geometry; whatever else the counters show) and
record whether PS5_Vulkan's "line lists do not draw correctly" is seen.
The lens flares and the sun's glow use the visibility tests; RADV reports
`occlusionQueryPrecise` false and the renderer already asks for imprecise
queries then (`host_vk_visibility.c`): record what the flares look like
against Android's.

### The order, each step on the console before the next

As the Vulkan plan's phases 2 to 6 were tried, with the user at the TV
from step 2 on (announce each run):

1. **Clears and the present** (`HALO_VK_SELF_TEST=1 HALO_VK_PRESENT_MARKER=1
   HALO_PS5_SCREENSHOT=5 HALO_EXIT_AFTER=40`): the `renderer:` line (RADV,
   its Mesa version, Vulkan 1.4); the device and the swapchain lines
   (3840x2160, the images, `B8G8R8A8_UNORM`, FIFO); `clears self-test: ok`
   and `data self-test: ... ok`; `host_vk_presenting` turning the stand-in's
   pacing off and the heartbeat still at 59.94 (the swapchain paces now);
   the marker's red and green squares top left and top right, seen on the
   TV and in the screenshots; the renderer's counters line every 60
   frames. The shader compile thread's first lines: the menus' shaders
   queued and compiled, their count and time.
2. **The menus**: the main menu on the TV as under Vulkan on Android (the
   title, the menu text in the high-res font, the background), draws
   skipped only while their shaders compile, then none skipped; a
   screenshot; the user's word and the Android port's picture side by
   side. The pad drives the menus (phase 3's run 3, below).
3. **A campaign level** from the menus: Campaign, the first level, Normal.
   The Pillar of Autumn's opening: the cryo bay, the lights, the characters
   (skinned models), the HUD, the decals. Screenshots every 10 s for the
   first two minutes; the counters (draws made, skipped by shader, by
   pipeline, by data; textures); the heartbeat; the heap and direct memory
   figures. Then The Silent Cartographer (from the campaign menu, if the
   save allows, else the level select): the beach, the sea (render targets,
   the water's ripples, the mip composite), the sky and the sun (the lens
   flare, the visibility tests), the Warthog (vehicle), the detail objects.
4. **RADV's gaps**, looked for in step 3's scenes and recorded with
   screenshots: the sun's flare and the lens flares (imprecise occlusion);
   anything drawn with lines; anything that differs from Android's
   picture of the same place.
5. **Phase 3's runs** (moved here by the user, 2026-10-18), with the TV's
   sound on:
   - run 3, the menus driven by the pad and heard: cross, circle, the
     d-pad, each heard and seen; the host's per-minute `pad:` line listing
     the presses;
   - run 5, the pad switched off and on at the menu: `SDL: gamepad removed`
     and `added` in the log, the game's `gamepads:` lines again, the menus
     answering after;
   - run 6, the PS button and the Quick Menu in the level: the `overlay:`
     lines (the first flag byte for the PS button's menu, the second for
     the Quick Menu), the focus lost and gained, what the game did seen on
     the TV (keeps playing, pauses), the pad's state while the menu is up;
   - runs 2, 4 and 7, as there is no network: the level played for five
     minutes (the movement, the turning, the shots, the jump, the weapon
     change, seen on the TV and in the `pad:` lines), the rumble felt
     (`pad: rumble asked ... with the motors on` above 0), and the audio
     lines over the five minutes (0 late callbacks, the hand-off's maximum
     under a period, no gaps heard). Run 2's own proof, the network test's
     per-second lines, waits for phase 6.
6. **Figures**: frames a second at the menu and in the level's opening
   (the heartbeat), the renderer's counters line, the compile thread's
   totals (shaders, pipelines, their times, the cache's files after the
   run), a second start's shader warm-up (nothing compiled, the pipelines
   from the cache), the heap's peak and direct memory's free block at the
   menu and in the level, and the screenshot copy's cost.

### Acceptance

The review checks each of these:

1. `ninja ps5` builds with `-Werror`; `ninja linux_x86_64` builds; the
   Android and Linux objects of every shared file touched (`host_vk.h`'s
   includers, `host_vk.c`, `host_vk_present.c`, `host_vk_shaders.c`,
   `host_common.c`) are unchanged with debug information stripped; the
   four test files pass; no weak host symbol stands; `host_vk_null.c` is
   gone.
2. Step 1's log: the renderer line, the swapchain line, both self-tests
   `ok`, the marker in a screenshot (the PNG under the run's folder).
3. Step 2: the main menu in a screenshot, as Android's; no draw skipped
   once the shaders are compiled (the counters line).
4. Step 3: The Pillar of Autumn's opening and The Silent Cartographer's
   beach in screenshots, the counters with the draws made, the frames a
   second in both, the figures under "Progress".
5. Step 4: what RADV's gaps change, seen and recorded with screenshots,
   or "not seen" with the scene that would show it.
6. Phase 3's items: the menus heard and driven (3), the gamepad removed and
   added (5), the focus events and what the game did (6), the five
   minutes' audio figures with no late callback and the hand-off under a
   period (7), the rumble felt (4), the level played (2 as far as it goes).
7. "Progress" has: the `HALO_PS5` branches argued with their line counts,
   the renderer line and the device's properties, the memory figures, the
   compile thread's totals and the second start's warm-up, the `__wrap_mmap`
   counts with RADV running, what the game draws with lines, the screenshot
   copy's cost, and anything unexpected.
8. Nothing changed outside `port/ps5/`, `tools/ps5_build.py`,
   `tools/ps5_deploy.py` and the shared files' `HALO_PS5` branches; nothing
   of PS5_Vulkan's or PS5_Mesa's copied in.

Don't commit; the review does that. Record anything unexpected under
"Progress", even if it was dealt with.

**Ends with:** the game on the TV: the menus and a campaign level as they
are under Vulkan on Android, driven by the pad and heard, with the frame
rates and what RADV's known gaps change under "Progress".

## Phase 5 — playing on the console

What makes it a port rather than a demo:

- **The first start on the TV**: the extraction of `maps/` from the disc
  image runs since phase 2; this adds what the player sees while it does
  (the desktop draws its progress through SDL's 2D renderer, a stand-in
  here: a notification per step, or the renderer's own clear-and-text), and
  what they see when `/data/halo` holds no disc image at all.
- **Saves and settings** in `/data/halo`; the console-specific settings
  (the display width, native 4K as an option) and their rows in
  `port_config.c`.
- **The PS button**: phase 0 B found that the title keeps running and
  presenting while the system's menu is up, the swapchain stays valid,
  `sceSystemServiceReceiveEvent` delivers nothing, and only two flags tell:
  the pad's `intercepted` bit and `isSystemUiOverlaid` from
  `sceSystemServiceGetStatus`. So the host pauses the game and stops
  reading the pad while either is set, and resumes when both clear. Rest
  mode and a closed title are tried here, with the log read afterwards.
- **Performance**: the game thread's milliseconds a frame in a busy scene,
  the present at 4K, the shader compile thread's effect on the first
  minutes; 60 frames a second held in a match, as the deko3d plan's phase
  7 held it.
- **Split screen**: a second controller and user (`scePadOpen` for each
  logged-in user), tried in a local match.
- **A long session**: a campaign level played start to end; a match of
  twenty minutes; the memory the host takes over it (the platform heap's
  peak and direct memory's free block, logged every minute: the heap never
  shrinks, so its peak is what the session costs).

**Ends with:** a campaign level played through and a split-screen match,
on the console, from a fresh install with only the `.iso` in `/data/halo`.

## Phase 6 — networking

FreeBSD sockets through `host_syscall.c` (`socket`, `bind`, `sendto`,
`recvfrom`, `poll`, the options the netcode sets, the address structures'
layouts) and whatever `libSceNet` needs first; the Switch's `host_net.c`
is the model, `port/linux/src/posix_net.c` compiled into the host as the
Linux host does. System link on the LAN first, then internet play (the
brokers, DTLS with mbedTLS in the host, UPnP, the server browser) and map
sharing. The network version is the same code and does not change.

**Ends with:** a system link game with the PC build and an internet game
through a broker, each played for ten minutes.

## Phase 7 — for players

`port/ps5/README.md` complete (requirements: the jailbreak and homebrew
environment the port was proved on; installing: the title folder over FTP,
`/data/halo` and the disc image; settings; what the port does not have;
how it operates), the release's title folder as a zip in the release, how
the build machine is set up (PS5_Vulkan, its SDK fork, the host tools;
whether CI can do it or a documented machine builds it), the licence
notice for the PS5 build, `AGENTS.md`'s layout table and `CHANGELOG.md`.

The notice (a file in the title folder and in the release's zip, generated
by `tools/ps5_build.py` from its pins) says that the PS5 build is a work
under the GPL-3.0-or-later as a whole, and names every part of its source
and where to get it: this repository at the release's commit; PS5_Vulkan
(`github.com/mpereiraesaa/PS5_Vulkan`), PS5_PayloadSDK
(`github.com/mihawk-99/PS5_PayloadSDK`) and PS5_Mesa, each at the commit
the build pinned, with its licence as its repository states it; and the
payload SDK release (`ps5-payload-dev/sdk` v0.42) the fork is built on. A
release that changes a pin changes the notice.

---

## How this plan is worked

As the deko3d and Vulkan plans were, on purpose:

- **Each phase is written out in detail before it is handed over**, with
  its files, its steps, how to test it on the console and what counts as
  done; the implementing agent reads the detailed phase and the documents
  it names, not this overview alone.
- **Each step is tried on the console before the next starts.** A step
  that is "done in the tree" and not seen on the console is not done.
- **Every result goes into "Progress"** with what shows it: the log lines,
  the report, what the TV showed. Newest first.
- **Work is audited against the source, not against its own description,**
  before it is committed. The implementer does not commit; the review does.
- **Diagnostics come before the feature**: the log file and the kernel log
  from phase 1, the renderer's counters and self-tests from the first draw.
- **Android and Linux stay working**: the shared files are theirs too, and
  their tests run with every phase.
- **Device rules**: the console is the user's; runs are started as
  "Decisions" says; nothing is uploaded but the title and what the phase
  names; `/data/halo` is the player's data and is not deleted without
  asking.

---

## Risks

| Risk | Settled in |
|---|---|
| glslang does not build or run on the console, so shaders cannot be compiled from the game's keys at run time | Phase 0 B; the fallback (a shipped SPIR-V corpus) is a worse game, said plainly |
| pacbrew's SDL2 does not fit a PS5_Vulkan-built title (SDK versions, video) | Phase 0 B; the shim on `scePad`/`sceAudioOut` is the fallback and the probe already drove both |
| The host's heap never shrinks: its peak over a session is permanent, inside the 12 GB the guest and the GPU share | Phase 0 B measured it (8 GB freed stayed mapped); phase 5 logs the peak over a long session |
| Page-protection faults are not delivered to a title, or not with a context | Phase 0 B; `host_watch_hash.c` is the fallback |
| RADV's gaps: imprecise occlusion queries (lens flares, the sun), line lists | Phase 4 sees them; the renderer copes as it does on drivers without `occlusionQueryPrecise` |
| The one display mode is 4K at 59.94 Hz: the present's cost, and no 60.00 Hz | Phase 0 B asks for 1080p; phase 5 measures the present |
| Firmware: proved on 13.59 here, 12.x by PS5_Vulkan; other versions unknown | Phase 7 says which were proved |
| The toolchain is another project's, GPL, and moves | Pinned by commit in `tools/ps5_build.py`; nothing copied in |
| Suspend and the PS button leave the title in a bad state | Phase 0 B: the title runs on under the system's menu, nothing breaks, two flags tell; phase 5 pauses on them and tries rest mode |
| A title's `exit()` ends in `SIGSYS` | Phase 2: it leaves nothing behind (the log is written a line at a time); PS5_Vulkan's titles get it too; explained in the README |
| The guest's 4 KB pages on 16 KB system pages: a 4 KB guard page that shares a system page with memory in use cannot be protected, so a guest stack overflow into it goes unnoticed | Phase 2 chose the union of the four pages' protections (nothing in use is ever made inaccessible); phase 5's long session watches for silent corruption, and the host's own guards are whole system pages |

---

## Progress

### Phase 3 — audit (2026-10-18)

Checked on the sources, the build, the tests and the two fetched logs:

1. **The build and scope.** `ninja ps5` is a null build; the four test files
   pass (15, the one old skip); no weak host symbol stands; only
   `port/ps5/host`, the README and the plan changed. SDL2's video is never
   asked for: the init mask is the four subsystems in the source, and the
   log's `SDL_Init` line says `video driver none`.
2. **The code.** The hand-off is the Switch's protocol (SDL's callback sets
   the request under the lock and waits on `done`; the thread with the guest
   stack runs the game's mixer outside the lock and signals; what the game
   puts into its stream meanwhile is appended under the lock and copied to
   SDL's buffer after); the stream's destruction closes the device before
   it tells the thread to quit, so no callback runs on a freed binding. The
   SDL3 values the SDL2 files rely on are literals asserted against SDL3's
   headers in one file and against SDL2's in the others, the way
   `freebsd_abi.h` is checked. The remote control is left out by type and
   name. The overlay poll reads either flag byte.
3. **The logs** (`20261018-050418`, `050508`) have every line the entry
   quotes: the init line, the two controllers, the obtained spec, the
   callback counts with no late one, the hand-off's maxima, the overlay's
   flags and the focus events.
4. **Three files, not one**, against the phase's text: accepted, for the
   reason given (SDL2's and SDL3's headers cannot be included together, as
   the Switch found). The Files table was updated by the implementer.
5. **Runs 2 to 7 were not made**, by the user's decision during the phase:
   they move to the end of phase 4, with the picture on the TV. Acceptance
   items 2 to 5 and 7's five-minute figures are therefore **open**, and
   phase 4's acceptance will carry them. Run 2 as written needs a hosted
   match, which the network stand-in cannot give before phase 6; phase 4's
   version plays a campaign level from the menus instead.
6. **Two corrections to the plan's record** found by the implementer and
   checked against phase 0 B's capture: SDL2's touch pad button is 20, as
   SDL3's (the phase text said 15; corrected in place), and the Quick Menu
   sets the status's second flag byte for as long as it is up (phase 0 B's
   entry misread it; corrected by this audit, above).

Carried forward: a title starts with the overlay flag set for about 6.6 s
after launch (the system's launch screen), so the game begins with the focus
lost; phase 5 must not pause on that. The game asks for rumble every frame
with the motors off; SDL2 forwards only changes, so it costs nothing. The
microphone is not SDL2's on this console (0 recording devices): voice chat
is phase 5's through the system's voice services, if at all.

### Phase 3 — SDL: pad, audio, events (2026-10-18)

Implemented on 2026-10-18; run 1 passed on the user's console (13.590.001).
**The result in one line:** the game starts SDL2 with its timer, events,
game controllers and audio and no video, lists the pad and leaves the
remote control out, opens the pad as a PS5 controller, plays its sound
through SDL2's device in exactly the format it asks for (48 kHz, float,
stereo, 1024 frames), with every callback handed to a thread with a guest
stack in at most 1.45 ms and none late, and turns the system's overlay into
focus events. **Runs 2 to 7 were not made:** the user moved them to the end
of phase 4, to be tried with the picture (phase 4, "Phase 3's runs"). So
acceptance items 2 to 5 are open, and run 2 cannot be made before phase 6
(below).

**Files.** `port/ps5/host/host_sdl_ps5_input.c` (new: `host_sdl_init`,
`set_hint`, `get_error`, the gamepad calls, `ps5_input_poll` with the
overlay check, the per-minute `pad:` line), `host_sdl_ps5_audio.c` (new:
the audio calls, the hand-off, the cadence and hand-off figures),
`host_sdl_ps5.c` (the moved parts gone; `host_sdl_poll_event` writes the
input file's events as SDL3's; the SDL3 values the other two use checked
with `_Static_assert`; `ps5_sdl_report` calls theirs), `ps5_host.h` (those
SDL3 values as literals, the event kinds, the three functions the files
share). `host_main.c` and `tools/ps5_build.py` unchanged: the host's
include path already has pacbrew's `include` folder, so `<SDL2/SDL.h>`
resolves, and the build globs `port/ps5/host/*.c`. The README.

**Why three files, not one.** The phase said to split only past about
1,500 lines (the three are 792, 573 and 463). The reason is the headers:
SDL2's and SDL3's declare the same names differently (`SDL_Init` returns
`int` in one and `bool` in the other, `SDL_Event` is two layouts), so no
file can include both, as on the Switch (`host_sdl2.c` beside
`host_sdl3_events.c`). The SDL3 values the SDL2 files need are literals in
`ps5_host.h`, each asserted against SDL3's headers in `host_sdl_ps5.c`; the
SDL2 files assert their side against SDL2's (`SDL_CONTROLLER_BUTTON_TOUCHPAD
== 20`, `SDL_CONTROLLER_AXIS_MAX == 6`, `AUDIO_F32SYS == 0x8120`), as
`freebsd_abi.h`'s literals are asserted against the SDK's.

**The SDL3-to-SDL2 values the files rely on:**

| What | SDL3 (the guest's) | SDL2 | Done |
|---|---|---|---|
| Init flags | any | `TIMER \| EVENTS \| GAMECONTROLLER \| AUDIO` | SDL2 is started for these four whatever the game asks (it asked `0x6030`: video, audio, gamepad, events), once; a later call adds nothing |
| Axes | `LEFTX` 0 to `RIGHT_TRIGGER` 5 | `LEFTX` 0 to `TRIGGERRIGHT` 5 | passed as they are; values as they are (-32768 to 32767) |
| Buttons | `SOUTH` 0 to `DPAD_RIGHT` 14, `MISC1` 15, paddles 16 to 19, `TOUCHPAD` 20, `MISC2` to `MISC6` 21 to 25 | `A` 0 to `DPAD_RIGHT` 14, `MISC1` 15, `PADDLE1` to `4` 16 to 19, `TOUCHPAD` 20 | 0 to 20 passed as they are (the paddles are in the same order, P1, P3, P2, P4); 21 on read 0 |
| Gamepad types | `UNKNOWN` 0, `STANDARD` 1, `XBOX360` 2, `XBOXONE` 3, `PS3` 4, `PS4` 5, `PS5` 6, `SWITCH_PRO` 7, Joy-Cons 8 to 10 | `UNKNOWN` 0, `XBOX360` 1, `XBOXONE` 2, `PS3` 3, `PS4` 4, `SWITCH_PRO` 5, `VIRTUAL` 6, `PS5` 7, Luna 8, Stadia 9, Shield 10, Joy-Cons 11 to 13 | mapped by name; one SDL3 lacks is unknown. The pad: SDL2's 7, the game's 6 |
| Audio format | `SDL_AUDIO_F32` 0x8120 | `AUDIO_F32SYS` 0x8120 | passed as it is |
| Default devices | playback `0xffffffff`, recording `0xfffffffe` | `NULL` name | playback opens SDL2's default; recording fails (below) |
| The period | `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` ("SDL_AUDIO_DEVICE_SAMPLE_FRAMES") | none: `SDL_AudioSpec.samples` | kept by the host, given as `samples` |
| Events | `GAMEPAD_ADDED`/`REMOVED` (by instance), `QUIT`, `WINDOW_FOCUS_LOST`/`GAINED` | `CONTROLLERDEVICEADDED` (by device index), `CONTROLLERDEVICEREMOVED` (by instance), `QUIT`; focus from the system's status | added: the index's instance id; button and axis events dropped and counted |
| Other hints | `SDL_APP_NAME` | `SDL_APP_NAME` | passed to `SDL_SetHint` (accepted) |

**Run 1** (`build/ps5/logs/20261018-050418`, `HALO_EXIT_AFTER=30`, nobody
at the console):

```
SDL: the hint SDL_APP_NAME=Halo
SDL: SDL_Init(timer, events, game controller, audio) for the game's 0x6030 in 1248 us: SDL 2.30.12 (SDL-2.30.12-ga47182a20), audio driver ps5, video driver none (SDL2's video is never started here)
SDL: audio driver 0: ps5
SDL: audio driver 1: disk
SDL: audio driver 2: dummy
SDL: 1 playback device(s)
SDL:   playback device 0: System audio output device
SDL: 0 recording device(s) (voice chat's microphone is phase 5's: no stream records in this phase)
overlay: the system's status flags 01 00 00 00 at 0.198 s
overlay: the system's menu is up: the window's focus is lost
SDL: gamepad added: "thelinkin3000", instance 1
gamepad 1: "thelinkin3000" opened, type 7 (PS5; SDL3's 6), 054c:0ce6, rumble yes
gamepads: SDL2 lists 2 joystick(s)
gamepads:   joystick 0: "thelinkin3000", instance 1, a game controller, type 7 (PS5), GUID 0300d0424c050000e60c000011810000
gamepads:   joystick 1: "PS5 Remote Control", instance 0, a game controller, type 0 (unknown), GUID 030000004c050000cec0000011810000: left out (the console's remote control)
game: halo-linux: gamepads: SDL lists 1
game: halo-linux: gamepads: port 0 reads SDL gamepad 1 (type 6)
audio: stream 4 on SDL2's device 2 (the default): asked 48000 Hz, float (0x8120), 2 channels, 1024 frames; obtained 48000 Hz, float (0x8120), 2 channels, 1024 frames (21.33 ms), 8192 bytes a callback, silence 0
audio: the first callback at 0.359 s, for 8192 bytes
overlay: "thelinkin3000" reads buttons 0x0, axes 0 0 0 0 0 0 while the menu is up
overlay: the system's status flags 00 00 00 00 at 6.772 s
overlay: the system's menu is gone: the window's focus is gained
...
pad: rumble asked 885 times (0 with the motors on), the longest call 0.02 ms
audio: 1400 callbacks of SDL2's for 10.9 MB of the game's sound, 0 late; gaps min 21.26 ms, max 21.44 ms; the hand-off's max 1.14 ms; 0.0 MB put into streams without a callback
```

- **SDL2 init and drivers:** 1.2 ms; audio driver `ps5` (of `ps5`, `disk`,
  `dummy`); video driver none, though the game's flags (`0x6030`) include
  SDL3's video bit.
- **The controllers:** the pad (its name the signed-in user's PSN name,
  type 7, `054c:0ce6`, rumble yes) and the remote control (type 0), as phase
  0 B listed them; the remote control is left out and logged. SDL2 sends
  `CONTROLLERDEVICEADDED` for the pad already there at the start, which the
  game opens; then the game lists one pad, type 6.
- **The audio device's obtained spec** is the asked one: 48000 Hz, float
  (0x8120), 2 channels, 1024 frames, 8192 bytes a callback.
- **The callback cadence:** 1400 callbacks in 30 s (46.9 a second: the
  1400 the phase expected), gaps 21.26 to 21.44 ms, 0 late. **The hand-off**
  (SDL2's callback to the game's mixer returning): at most 1.14 ms, 5% of a
  period. The per-minute lines (min, median, max of both) need a run of a
  minute or more; neither run had one. The five minutes are phase 4's.
- **Rumble:** the game asks every frame (885 times in 30 s), all with the
  motors at 0 at the menu; each call takes at most 0.02 ms (SDL2 sends the
  pad only a change).
- **Focus loss:** the game got `SDL_EVENT_WINDOW_FOCUS_LOST` at 0.2 s and
  `GAINED` at 6.8 s (below). It logs nothing on either; `sdl_platform.c`
  clears its held keys and mouse buttons and sets `input_state.focused`;
  the mixer's callbacks never stopped (nobody was listening). What
  it does on the PS button with a picture is phase 4's run 6.
- **The game's lines that show each input:** none yet; nobody pressed
  anything. The host's per-minute `pad:` line lists the presses (by the
  DualSense's names), each axis's range and the rumble requests for every
  minute the pad was used.
- **Streams without a callback:** none asked for; the game opened no
  microphone in either run (voice chat's stream opens only when voice is
  used). Pacbrew's SDL2 lists 0 recording devices: SDL2's recording is not
  the microphone's way here, as the phase guessed.

**The network test check** (`20261018-050508`, `HALO_EXIT_AFTER=45`,
`HALO_NETWORK_TEST=host:bloodgulch`, nobody at the console, made to learn
whether run 2 could work before calling the user): `bloodgulch.map` opened
at 2.7 s, `network test: hosting bloodgulch`, `starting the game`, then
heartbeats only, no per-second player lines. The game's `debug.txt`:

```
starting precaching of map 'bloodgulch'
winsock error #10050: WSAENETDOWN
failed to create the server connection
failed to initiate a multiplayer game server
```

A system link game needs its server's socket, and `host_posix_none.c`
answers as a machine whose network is down until phase 6. **Run 2 as
written cannot be made before phase 6**, and neither can run 4 (the game
rumbles only in play) inside it; phase 4's version plays a campaign level
from the menus instead, and the per-second lines wait for the network. The
run's audio: 2104 callbacks in 45 s, 0 late, gaps 21.27 to 21.43 ms, the
hand-off at most 1.45 ms.

**Unexpected, and what was done about it:**

- **The plan had SDL2's touch pad button wrong.** It said 15; pacbrew's
  `SDL_gamecontroller.h` has `MISC1` 15, `PADDLE1` to `4` 16 to 19 and
  `TOUCHPAD` 20, the same numbers as SDL3's, so no button is mapped. The
  phase text is corrected.
- **The Quick Menu sets the second flag, not the first.** Phase 0 B's
  run 3 capture, read again: the PS button's menu set the first byte after
  the count (`01 00 00 00`, 6.5 to 17.8 s); the Quick Menu set it for
  0.1 s, then the second (`00 01 00 00`) from 19.875 s until it closed at
  23.780 s, and the probe logs only changes. Phase 0 B's entry ("the second
  byte was set for 0.1 s") misread it. The host treats either byte as the
  overlay, and logs the four bytes on every change.
- **A title starts overlaid.** The first byte was set from the first check
  (0.198 s) until 6.772 s after the launch, with nobody touching the
  console: the system's own launch screen or notification, presumably. So
  the game starts with the focus lost and gains it at 6.8 s. Harmless at
  the menu; phase 5 should keep it in mind if it pauses on the overlay.
- **The pad while overlaid** (that start-up overlay): buttons 0, axes 0,
  as phase 0 B's scePad reading. The PS button's case is phase 4's run 6.
- **The console had been restarted** before run 1: ps5vkctl was not
  resident (loaded again through the payload manager), klogsrv refused
  connections, and `launch` failed with
  `lncUtilApp/foreground(user 372994645)=0x80b21401` (and the same for
  login). The user rebooted everything, after which both runs went through
  as before. ps5vkctl was then loaded a second time while one was already
  resident (its status had answered `ok idle`); the new one answered
  (`pid=101`) and nothing went wrong.
- **The FTP server answers a delete with 226**, which Python's `ftplib`
  takes for an error; the file was deleted (`environment.txt`, after the
  runs; the title's folder holds the title only).
- **The host's start-up line still says "phase 2"** (`host_main.c`, which
  this phase did not change).

**Tests:** `ninja ps5` builds with `-Werror`; `pytest -q
tools/test_ps5_host.py tools/test_ps5_build.py tools/test_host_units.py
tools/test_host_syscall.py`: 15 passed, 1 skipped (the skip predates
phase 2). `llvm-nm` on `eboot.elf`: 72 `host_sdl_*` defined, no weak host
symbol. No shared file changed, so Android's and Linux's builds are
untouched.

### Phase 2 — audit (2026-10-18)

Checked against the acceptance list, on the sources, the build, the fetched
logs and the tests:

1. **The builds and tests.** `ninja ps5` and `ninja linux_x86_64` are null
   builds here; the four test files pass (15, and the one skip that
   predates the phase). The `host_arch.c` change is two `HALO_PS5` branches
   and nothing else, so the other builds' code cannot change; the
   implementer's object comparison (identical with debug information
   stripped) agrees.
2. **The first start.** `build/ps5/logs/20261018-031835-2/halo.log` (the
   folder without the suffix holds the previous run's fetched log, which is
   why it shows the failed attempts): the defaults written to
   `config.toml`, the question answered, `maps.partial/a10.map` at 0.2 s to
   `wizard.map` at 31.2 s, `ui.map` opened, the heartbeat at 59.94. The
   second start (`032011`): `ui.map` at 0.2 s, the exit at 60.2 s.
3. **Five minutes** (`032750`): 60 heartbeats, 59 of them 300 frames in
   5.00 or 5.01 s at 59.94 a second with the longest frame 16.78 or 16.79
   ms; the heap at 160 MB throughout.
4. **The crash test**'s registers and frames are in "Progress" and its
   folder.
5. **No weak stub stands**: `llvm-nm` on `eboot.elf` lists no weak host
   symbol; 72 `host_sdl_*` functions are defined.
6. **"Progress"** has every figure the phase asked for.
7. `host_bootstrap.*` and the syscall-names rule are gone; `host_main.c`'s
   header says what it does.
8. **Scope and copying.** Nothing of PS5_Vulkan's or the fork's is in the
   new files (a line-by-line comparison with their sources finds only
   `vsnprintf` and `stmxcsr` idioms). FreeBSD's constants are literals in
   `freebsd_abi.h`, ten of them asserted against the SDK's headers in the
   PS5 build, and I checked eleven against the headers by hand: they agree.

The console's own state could not be listed this time: its FTP server
refuses connections (the console answers ping), so the folder contents
under "Progress" stand on the implementer's word and the fetched logs.

Deviations accepted:

- **Three files the phase did not name**: `host_debug_ps5.c` in place of
  the shared `host_debug.c` (the phase's own rule: more than a third of the
  file would be compiled out), `host_posix_none.c` for the 55 `posix.h`
  imports the phase did not count, and `host_local.map`, without which the
  title converter refused the wrapped `mmap`. All three are in the Files
  table now.
- **`tools/test_ps5_build.py`** changed by two lines, outside the allowed
  list, because it named the deleted bootstrap objects.
- **The disc image was copied by the implementer**, on the user's word in
  the session; the plan had asked the user to do it.
- **`HALO_DATA_ROOT` is not set**, against the phase's text: setting it is
  `paths.data`, which turns the first start's offer off. The game finds
  `/data/halo` through `/proc/self/exe` instead. Right, and recorded.
- **The first start cannot `exit_after`**: the game offers no extraction to
  a run it thinks nobody watches. The phase's step 1 was run without the
  setting and killed at the menu; the exit was shown on the second start.

Carried into later phases: phase 3's audio is a callback stream driven by a
device thread (the phase text now says so); the 4 KB guard-page weakness is
a new row in Risks for phase 5's long session to watch; `host_posix_none.c`
is what phase 6 replaces; the `__wrap_mmap` counts tell phase 4 whether
RADV maps below 4 GB.

### Phase 2 — the host's core, no graphics (2026-10-18)

Implemented on 2026-10-18 and run on the user's console (13.590.001). **The
result in one line:** from `/data/halo` holding only the disc image, the
title answers the game's question, extracts `maps/` (1774 MB in 31 s), loads
the main menu's map (`ui.map`, 35.1 s after the launch) and runs its loop at
59.94 frames a second with every draw dropped; the second start loads
`ui.map` 0.2 s after the launch; five minutes at the menu were steady; the
exit at 60 s reports the game's end; a crash is reported with registers and
frames.

**Files.** New under `port/ps5/host`: `host_memory.c`, `host_syscall.c`,
`host_syscall_tables.c` and `.h`, `freebsd_abi.h`, `host_futex.c`,
`host_sdl_ps5.c`, `host_vk_null.c`, `host_gl_none.c`, `host_update_none.c`,
and three the plan did not name: `host_debug_ps5.c` (in place of
`host_debug.c`, below), `host_posix_none.c` (the game's network and
desktop helpers, below) and `host_local.map` (the link's local names,
below). `host_main.c` is the real entry point, `ps5_host.h` what the PS5
files share; `host_bootstrap.c` and `.S` and the syscall-names rule are
gone. `port/ps5/tests/host_syscall_tables_test.c`, `tools/test_ps5_host.py`,
the README. Shared: `port/android/host/host_arch.c` (a `HALO_PS5` branch).
Outside the allowed list: `tools/test_ps5_build.py`, two lines (below).
`tools/ps5_title.sh` needed no change: it already hands what follows `--`
to the linker as it is, flags included.

**The runs** (`build/ps5/logs/<time>`; every one through
`tools/ps5_deploy.py --run`, stopped with its `kill` where the game does not
end itself):

1. 031543 (first start, before the fixes): memory, the loader, the 274
   imports (0 unavailable), musl's start-up, `config.toml` not writable,
   the question answered "Yes", the dialog answered with the image, then
   "extraction failed: Could not start the extraction." over and over (the
   game asks again on failure). Cause: the guest's 4 KB pages (below).
   Killed.
2. 031645: the same with the failure log added: every guest `mprotect` of
   a 4 KB page was `EINVAL`. Killed.
3. 031835 (**the first start**, from the folder holding the image and phase
   1's `halo.log`): `settings: wrote the defaults to /data/halo/config.toml`;
   the question and the dialog answered; `extracting maps/a10.map` at 0.2 s
   to `wizard.map` at 31.2 s, the 2D renderer's 1971 presents counted (its
   text logged every 10 s: `ui.map (1748 of 1774 MB)` at 30.2 s), `extracted
   the maps folder`; `data root: /data/halo`; the window and context
   stand-ins; `ui.map` opened at 35.1 s; heartbeats of 36.2 and 40.5 frames
   a second, then 59.94 (mean 16.68 ms, longest 16.79 ms). Stopped by the
   follow's timeout at 60 s and killed: see `exit_after` below.
4. 032011 (**the second start**, `HALO_EXIT_AFTER=60` through
   `/app0/environment.txt`): no question; the first heartbeat and `ui.map`
   at 0.2 s; `exiting after debug.exit_after`, `the game exited (0)`, the
   game ended at 60.2 s, the reports (below), `the host ends`; exit 0.
5. 032130: the host found no `/data/halo` and ended at once (below).
6. 032205: the same title again: `/data/halo` found. Killed after 20 s.
7. 032243 (**five minutes at the menu**, no `exit_after`): 331 s, then
   killed. `halo.log` (fetched after the kill, 032750) has 60 heartbeats:
   the first at 54.48 a second (the map's load), then 59 at 59.94 a second,
   mean 16.68 ms, longest 16.78 or 16.79 ms in every one; five memory
   lines, each "the host's heap 160 MB (peak 160 MB), direct memory's
   largest free block 11269 MB". No fault.
8. 032809, 032849 (**the crash test**, `HALO_PS5_CRASH_TEST=1`); the second,
   with eboot offsets in the report:

   ```
   HALO_PS5_CRASH_TEST: writing to the unmapped guest address 0x08000000 from the game's thread
   error: signal 11 (code 2) at address 8000000: pc 0000000000402d42 sp 0000000003883a30 fp 0000000003883f40
   error:   in the host: llvm-symbolizer --obj=build/ps5/host/eboot.elf 0x2d42
   error:   rax 000000000000005d rbx 0000000000000012 rcx f6dffad37c546278 rdx 000000000000005d
   error:   rsi 0000000000000000 rdi 0000000880f4d2c0 rbp 0000000003883f40 rsp 0000000003883a30
   error:   r8  0000002000000000 r9  0000002001000020 r10 000000000515ca11 r11 0000002001ffffa0
   error:   r12 000000080000e370 r13 0000000001203e94 r14 0000002003000020 r15 0000000001203e94
   error:   rip 0000000000402d42 efl 0000000000010206 err 0000000000000006 trp 000000000000000c
   error:   frame  0: return 000000000040cb4b (the host: eboot.elf 0xcb4b)
   error:   frame  1: return 0000000000ebe7c9 (the host: eboot.elf 0xabe7c9)
   error:   frame  2: return 000000080000763d
   error:   frame  3: return 0000000000000000
   ```

   then the system's own block (`signal: 11 (SIGSEGV)`, `page fault (user
   write data, page not present)`). The offsets are `game_main`
   (`host_main.c:533`, the write), `thread_main` (`host_thread.c`) and the
   platform layer's `plain_thread_start`. `si_code` is 2, not phase 0 B's 1
   for an unmapped read: 0x08000000 lies in pool 0 (0x02880000-0x12880000),
   whose pages not given out are kept `PROT_NONE`.
9. 032921, the closing run of the final build with `HALO_EXIT_AFTER=60`:
   the first frame and `ui.map` at 0.2 s, the exit at 60.2 s, the same
   reports; exit 0. (A comment changed after it; the code did not.)

`environment.txt` was removed from the title's folder after each run that
used it, and so was run 5's stray `/data/homebrew/PPSA99500/halo.log`. The
console holds the title, and in `/data/halo` the disc image, `maps/`,
`save/`, `custom_maps/`, `config.toml`, `debug.txt`, `gamestate.txt` and
`halo.log`.

**What the game's end reports** (runs 4 and 9, the same):

- **-ENOSYS**: `mremap (216) 28`, nothing else. musl's `realloc` asks for it
  and falls back to a new mapping and a copy. No `statfs`, `times`,
  `getrusage`, network call, timer, signal or process call was made in any
  run, and no `statx`. No file was mapped (`mmap` with a descriptor), so
  that path is untried.
- **GL names the Vulkan image called**: none, of the 112 the loader
  resolved.
- **`__wrap_mmap`**: 1 call answered in the guest's direct memory (the
  loader's `mmap(70000000, 46841856, RW, MAP_FIXED | MAP_ANON)`), 10 passed
  to the system, 0 of them below 4 GB. The ten are `host_thread.c`'s signal
  stacks (69632 bytes, one a thread, from 0x20001c000 up: flexible memory).
  RADV has not run yet; phase 4 sees its own.
- **The renderer stand-in**: about 3570 submissions in 60 s, 0 shaders sent
  to compile (the guest sends GLSL only for a shader it is told is unknown;
  the stand-in says "compiling"), every draw dropped.
- **The audio stand-in**: the game's mixer called 2808 times in 60 s, every
  21.3 ms (1024 frames at 48 kHz, the period the game asks SDL for), 21.9 MB
  of sound.
- **Memory**: one pool (159 of its 256 MB free), the host's heap 160 MB
  (peak 160 MB), direct memory's largest free block 11269 MB: the same in
  every run and over the five minutes.

**The thread checks** (every run): `__thread on the game's thread is its own
(it read 00000000 before it was set, the main thread holds 0badf00d), and it
reads back; thread ids: main 102239, the game's 100868 (distinct)`, and `a
thread made from the game's thread: made (0, -), its id 102252`. The id is
`pthread_getthreadid_np`, which libkernel exports to a title. The first
thread made has MXCSR 0x1fa0: no flush-to-zero, only the precision flag
set; each guest thread sets IEEE all the same (`host_debug_ps5.c`).

**The weak stubs still standing: none.** `llvm-nm` on `eboot.elf` lists no
weak symbol among the 162 functions of the host's import table. The one
that stood at the first link, `host_gl_debug_output` (debug.gl_debug's
logger, a ninth `host_gl_*` import the plan did not count), is answered in
`host_gl_none.c` as a no-op that says so once. No run's log has "is not
answered".

**The `HALO_PS5` branches, argued.**

- `host_arch.c`: one branch for FreeBSD's `mcontext` (`mc_rip`, `mc_rsp`,
  `mc_rbp`, the general registers, `mc_rflags`, `mc_err`, `mc_trapno`), and
  one for `host_arch_read_memory`, which on Linux copies through
  `process_vm_readv` and here asks `sceKernelQueryMemoryProtection` (one
  system call a region, no lock) whether each page is readable before the
  copy. 53 lines added, of 214: under a third.
- `host_debug.c`: **a PS5 file instead**, `port/ps5/host/host_debug_ps5.c`.
  Its sampler and profiler send signals with `tgkill` and
  `rt_tgsigqueueinfo` and read `/proc`, none of which a title has; compiled
  out they would be about 400 of its 481 lines, over the third. The PS5
  file keeps the thread bookkeeping, answers both "not on the PS5", and is
  where each guest thread sets its floating point mode
  (`host_debug_thread_started` is the first thing `host_thread.c`'s threads
  do).
- **Android's and Linux's objects**: `host_arch.c.o` and `host_debug.c.o` of
  `build/android` and `build/linux_x86_64` were saved before the change and
  rebuilt after. `host_debug.c.o` is identical (its source did not change).
  `host_arch.c.o` differs only in `.debug_line` (2 and 1 bytes longer), and
  is identical byte for byte with the debug information stripped
  (`llvm-objcopy --strip-debug`): its code and data are unchanged. Clang
  writes the source's MD5 into DWARF 5's line table, so no change to a
  source can leave a `-g` object identical. The Switch compiles neither
  file.

**Unexpected, and what was done about it:**

- **The guest's pages are 4 KB, the console's 16 KB.** The guest's musl is
  built for 4 KB pages whatever the boot block says; its malloc
  `mprotect`s single 4 KB pages, which the console refuses with `EINVAL`, so
  `calloc`, and with it `pthread_create`, failed (run 1's "Could not start
  the extraction"). `host_memory.c` keeps the guest's pages in 4 KB, as
  Android's does, with a protection byte each, and gives each 16 KB system
  page the union of its four: a guard page that shares a system page with
  memory in use stays accessible, and nothing in use is ever made
  inaccessible. `host_low_map` hands out whole system pages, so the host's
  own 16 KB guard pages (`host_thread.c`) never reach a neighbour. A run of
  equal protections is one `mprotect` call.
- **`debug.exit_after` turns the first start's extraction off.**
  `platform_offer_game_data` (`sdl_platform.c`) offers nothing to "runs
  nobody is watching" (`debug.hidden_window` or `debug.exit_after`). So the
  first start cannot both extract and exit at 60 s, as step 1 of "Testing
  on the console" expected: it ran without `exit_after` and was killed at
  the menu (run 3); the exit at 60 s is the second start's (runs 4 and 9).
- **`HALO_DATA_ROOT` is not set.** The plan's environment had it, but it is
  `paths.data`, which skips the search beside the executable and with it
  the offer to extract (`xbox_files.c`). The game finds `/data/halo` itself
  through `/proc/self/exe` (answered `/data/halo/halo`). `HOME`,
  `XDG_DATA_HOME`, `HALO_SAVE_ROOT` and `TZ` are set as planned.
- **55 imports the plan did not list**: `posix.h`'s helpers
  (`hostposix_*`), which the desktop guest imports and the Linux host
  compiles from `port/linux/src/posix_*.c`. The file helpers are
  `posix_files.c`, compiled for the PS5 as it is (it builds with
  `-Werror`); the network, UPnP, DTLS, Discord and desktop ones are
  `host_posix_none.c`, which answers as a machine whose network is down
  (phase 6, the plan's nearest case), and the rest as Android's
  `posix_net.c` does. The game said: `Internet play: cannot open its socket
  ... it is off`.
- **The audio stand-in drives the game's mixer.** The plan's stand-in took
  pushed audio; the game's stream is a callback stream (`dsound_sdl.c`), so
  a stream with a callback gets a thread with a guest stack that calls it a
  period at a time at the stream's rate, as a device would. Without it the
  game's voices would never drain. The plan's answers stay for a stream
  without a callback.
- **No window translation.** Android's `to_host` and `to_guest` move the
  game's window addresses to where the window really is. The x32 guest is
  built with the window movable and already passes real addresses (its first
  `mmap` is at 0x90000000); with the window at 0x90000000 the translation
  would have moved 0x90000000 to 0xa0000000. `host_memory.c` has none.
- **Libc calls a title cannot make.** `fcntl(F_SETFD)` answers `EINVAL`
  (now answered by the host: close-on-exec means nothing where no process
  is started); `chdir("/data/halo")` answers `EPERM` (the guest's relative
  paths are made absolute by `host_syscall.c`); `readlink` is not exported
  (`readlinkat` answers `/proc/self/exe`, else `EINVAL` or the file's
  error); `pipe2` and `dup3` are not exported (`pipe` and `dup2`, then the
  flags), and the kernel keeps no `umask` for a title (the guest's is
  remembered). Directories are read with `getdents`, which the platform
  layer itself uses. FreeBSD 11 has no `O_DSYNC` (both sync flags are
  `O_SYNC`). The plan's "the first 34 `errno` values agree" holds but for
  11 (FreeBSD's `EDEADLK`, Linux's `EAGAIN`).
- **The title saw no `/data/halo` once** (run 5): `stat` failed in a title
  started 19 s after the previous one ended, and the next launch saw it.
  The host now waits up to 2 s for the folder, and logs any wait.
- **The link exported `__wrap_mmap`**, which the title converter refuses
  ("native converter does not yet publish application exports"):
  libkernel's stub defines `mmap`, so the wrap was exported to override it.
  `visibility("hidden")` did not prevent it; a version script of our own,
  `port/ps5/host/host_local.map`, makes both wraps local, as PS5_Vulkan's
  recipe does for its bindings.
- **`host_fatal`'s message box** is SDL3's `SDL_ShowSimpleMessageBox` in
  `host_common.c`, which binds to SDL2's in this link, whose video is never
  started (phase 0 B). The link wraps it (`--wrap=SDL_ShowSimpleMessageBox`)
  into a system notification. `host_common.c` compiles against SDL3's
  headers, now on the host's include path, unchanged.
- **The game's end, seen from the PS5's files.** `host_exit` and
  `host_fatal` are `host_common.c`'s and end in `_exit`; the PS5's log sink
  recognises their last lines ("the game exited (", a fatal line) and writes
  the reports, the hold and "the host ends" before returning to them.
- **tomlc17 warns under the SDK's `math.h`** (`isnan`'s `_Generic` names
  volatile types: `-Wunreachable-code-generic-assoc`). The warning is the
  header's; `tomlc17.c` alone is compiled with it off.
- **`tools/test_ps5_build.py` changed**, outside the allowed list: it named
  `host_bootstrap.c.o` and `.S.o`, which this phase deletes, so its fake
  tree and its list name the phase 2 host's objects and the wrap instead
  (two lines). Without it the acceptance's tests could not pass.
- **The deploy's printout drops identical consecutive lines** (its
  de-duplication of klogsrv's doubled lines), so run 7's steady heartbeats
  showed 33 of 60 there; `halo.log` has them all.
- **The `SIGSYS` after the exit** is not in the captures: the follower stops
  at "the host ends", before the kernel reports it.
- **Descriptors 0 to 2 are open** when a title starts. The host would take
  any that were not, so no guest file is given 1 or 2, whose writes go to
  the log.
- **The extraction writes at about 57 MB/s** (1774 MB in 31 s), three times
  phase 0 B's 18 MB/s, which measured writes with an `fsync` each.
- **The disc image was copied by the implementer**, on the user's
  instruction (2026-10-18: "Put the image yourself"), from the PC's
  `E:\Halo - Combat Evolved (USA) (Rev 2).xiso.iso` to `/data/halo` over
  FTP: 3,625,713,664 bytes in 46 s, the size checked.
- **Tests**: `pytest -q tools/test_ps5_host.py tools/test_ps5_build.py
  tools/test_host_units.py tools/test_host_syscall.py`: 15 passed, 1
  skipped (`test_x32_boundary`: it wants an unversioned `ld.lld`,
  `llvm-objcopy` and `llvm-nm`, which this machine has only as `-22`; the
  skip is not new). `ninja linux_x86_64` builds.

### Phase 1 — audit (2026-10-18)

Checked against the acceptance list, on the sources, the build and the
console:

1. **The builds.** `ninja ps5` is a null build here (the build stamp alone
   reruns, as Android's); the three tests pass (14); `ninja linux_x86_64`
   has no work; `ninja` alone still defaults to `linux`. The Android and
   Switch objects of the seven changed shared files compile. The configure
   with a missing checkout gives the unavailable target, as the test shows.
2. **The link.** `llvm-nm` on `build/ps5/host/eboot.elf` has
   `vk_icdGetInstanceProcAddr`, `glslang_initialize_process` and
   `SDL_GetVersion` (local, as `--exclude-libs=ALL` makes them) and the
   host's `___mb_cur_max`; the fetched logs show the three proofs.
3. **The run.** The last fetched log (`build/ps5/logs/20261018-021445`) ends
   at `host_sdl_get_base_path` after the same 14 host calls as the guest
   probe's run 3, with the Vulkan image (274 imports). The console holds
   `/data/homebrew/PPSA99500` with the title's six files and
   `/data/halo/halo.log`; nothing else of ours is new there.
4. **The relaunch and the kill** are in the runs' record (runs 2 to 4).
5. **"Progress"** has the pins against the checkouts, the sizes, the times,
   the payload manager's requests and the compile survey.
6. **Scope.** Seven shared files changed, outside what the phase allowed,
   with the user's decision taken during the phase: the Vulkan renderer had
   never been compiled into a desktop guest. The change is nine `#if` gates
   widened by `|| defined(HALO_PS5)` and `xgpu_capabilities` in `d3d8_vk.c`
   put under `HALO_GLES`, where its type is declared; `HALO_PS5` is defined
   by no other build and Android defines `HALO_GLES`, so the other builds'
   objects cannot change. Accepted; it is the "Shared code" row's kind of
   change, argued under "Progress" as that row asks.
7. **The README** says how to build, install and run what exists.

Two things the audit changed:

- **`tools/ps5_deploy.py` was too close to PS5_Vulkan's `ps5_console.py`**
  (GPL) in two places: the one-command function and the kernel log's backlog
  loop were that file's lines with the names kept. Both are rewritten in a
  shape of their own (a line stream over the socket; a generator for the
  kernel log's lines), and tried read-only against the console (`status`,
  and a short `follow`). What remains in common with it is five idioms of
  one line each.
- The README called glslang GPL; it is not, and the notice says so. Fixed.

Noted for phase 2, not changed: the compile survey ran without
`-I port/third_party/tomlc17` and `-I port/linux/src`, so `host_common.c`
and `host_desktop.c` fail on include paths there, not on the system;
`host_main.c` ends with `_exit(0)` while the game's thread may still run.

### Phase 1 — the build and the deploy (2026-10-18)

Implemented on 2026-10-18 and run on the user's console (system software
13.590.001). The files: `tools/ps5_build.py`,
`tools/ps5_title.sh`, `tools/ps5_deploy.py`, `tools/test_ps5_build.py`,
`configure.py` (the six `--ps5` options and the one call), and under
`port/ps5`: `host/host_main.c`, `host/ps5_host.h`, `host/host_bootstrap.c`
and `.S` (temporary), `host/host_libc_extra.c`, `sce_sys/param.json`,
`sce_sys/icon0.png` and `README.md`. The probes are unchanged. And, with the
user's decision (below), seven shared files: `port/android/guest/d3d8_vk.c`
and `port/linux/src/hud_hires.c`, `.h`, `text_hires.c`, `.h`,
`menu_files.c`, `.h`.

**The result in one line:** `ninja ps5 && python3 tools/ps5_deploy.py --run`
builds the title from a clean `build/ps5`, installs and starts it, and its
log stops at `host_sdl_get_base_path` as the guest probe's run 3 did, with
the Vulkan image (runs 6 and 7).

**The Vulkan image did not compile in the desktop guest**, at first. Two of
its files failed, with every compiler (clang 18 and 22):

- `port/android/guest/d3d8_vk.c`: `struct xgpu_capabilities` is incomplete
  (6 errors). `xgpu.h` declares it only under `HALO_GLES`, and `d3d8_vk.c`
  sets its `shading_language` to `"310 es"` around two calls (lines 1992 to
  2008).
- `port/android/guest/xbox_textures_vk.c`: `hud_hires_png_pixels`,
  `text_hires_atlas_rows`, `menu_art_name`, `menu_art_serial` and
  `menu_art_png` are undeclared (9 errors). `hud_hires.h`, `text_hires.h` and
  `menu_files.h` declare them, and `hud_hires.c`, `text_hires.c` and
  `menu_files.c` define them, only under `defined(HALO_SWITCH) ||
  defined(HALO_ANDROID)`: the deko3d renderer's, which the Vulkan one shares.
- `nv2a_vsh_vk.c` and `nv2a_psh_vk.c` compile.

So no guest had ever had the Vulkan renderer without `HALO_ANDROID`. The
phase said nothing under `port/android` or `port/linux` changes; runs 1 to 5
were made with a stand-in (below), and then the user chose to make the
change in this phase. It is argued here, as "Shared code" asks:

- The gates of the helpers only the deko3d and Vulkan renderers call
  (`hud_hires_png_pixels`, `text_hires_atlas_rows`, `menu_art_name`,
  `menu_art_serial`, `menu_art_png`, and `menu_files.c`'s `art_serial`; nine
  `#if`s in six files) are now `defined(HALO_SWITCH) || defined(HALO_ANDROID)
  || defined(HALO_PS5)`, and the PS5's guest is compiled with `-DHALO_PS5=1`
  (`tools/ps5_build.py`). The headers' comments say "the deko3d and Vulkan
  renderers'".
- `d3d8_vk.c` defines `xgpu_capabilities` and sets its `shading_language`
  only under `HALO_GLES`, where `xgpu.h` declares it. On the desktop guest
  nothing reads it: the desktop GL generators write no `#version` (theirs is
  under `HALO_GLES` in `nv2a_vsh.c` and `nv2a_psh.c`), and the only use in
  `d3d8_vk.c` is its debug comparison of the Vulkan generators' text with
  the GL ones' (`gl_vertex_shader`, `gl_pixel_shader`), whose `.gl` files on
  the PS5 then have no `#version` line.
- The link then wanted the renderer's host calls (`host_vk_submit` and the
  rest): the PS5's guest takes `port/android/host_imports_vk.list` as
  Android's does, in the one import table of both images (274 imports, 265
  before).
- **Android, the Switch and Linux are unchanged, byte for byte**: the
  objects of the changed files in `build/android`, `build/switch` and
  `build/linux_x86_64` were rebuilt after the change and compared with the
  ones from before it, and all ten are identical (`HALO_PS5` is defined
  nowhere else, and Android defines `HALO_GLES`). So the phone was not
  needed.

**The stand-in, for runs 1 to 5.** Before the change, `build/ps5/halo_guest.elf`
(the GL image the generator links from the same objects, with the window
movable) was copied by hand to `build/ps5/title/PPSA99500/halo_guest.elf`,
with the user's leave: the bootstrap stops before any renderer runs. Runs 6
and 7 are the build's own title, the Vulkan image in it.

The pins, and what the checkouts are at (all at their pins; configure.py
warned of nothing):

| | pin (`tools/ps5_build.py`) | the checkout |
|---|---|---|
| PS5_Vulkan | 8aee3afb37bcfad06f0c7d263b901040f08ebbd0 | the same (`git rev-parse HEAD`) |
| the SDK fork | 95c08f27386fc698f6bbe21dde3030140a41d10b | the same (`.ps5-sdk-revision`) |
| PS5_Mesa | 01d70579b8d410d6a482e36c6f184798ae4c83ad | the same |
| pacbrew | v0.40.2, a85f65de…ec4d | the same (the setup script's `version=` and `hash=`) |
| glslang | 16.6.0 (`android_build.GLSLANG_TAG`) | the Android build's clone, e1b562a8bed2, reused |

The build, on this machine:

- `rm -rf build/ps5` (phase 0 B's folder was moved aside to
  `build/ps5-phase0b`, not deleted: it holds the probes' built titles and the
  raw kernel log capture), then `python3 configure.py --ps5`: 21.6 s (musl
  and SDL3 downloaded; glslang's clone reused; pacbrew already cached).
- `ninja ps5` from clean, after the change: 44.5 s, 1637 steps (glslang's
  build is one of them); `configure.py --ps5` before it, 19.6 s. (Before the
  change, `ninja -k 0 ps5` ran 1634 of them in 44.6 s and stopped at the two
  Vulkan objects.) A rebuild: the one build-stamp step, as Android's (it is
  written at every build and restat). But the first `ninja ps5` after a
  clean build redoes 293 steps, in 3.0 s, once: `-d explain` says the musl
  objects are older than `build/ps5/guest/libc_include/bits/alltypes.h`,
  which `.ninja_log` records as written once, 86 ms into the build, before
  them. That is the shared guest generator's graph, unchanged here, on this
  WSL filesystem; it is recorded, not chased.
- The eboot: `eboot.bin` 29,599,331 bytes; `eboot.title.elf` (ps5-native-tool's,
  unsigned) 30,134,764; `eboot.elf` (the linker's, with `-g`) 106,676,968.
- `llvm-nm` (the SDK's) on `build/ps5/host/eboot.elf`:
  `vk_icdGetInstanceProcAddr` at 0x6f20e0, `glslang_initialize_process` at
  0x7894d0, `SDL_GetVersion` at 0x9948f0 (local symbols, `t`: the link's
  `--exclude-libs=ALL`).
- No symbol clash between RADV's, glslang's and SDL2's recipes: the first
  link succeeded. The host compiles with `-Werror` and no warning.
- `ninja linux_x86_64` builds (with this tree's own options:
  `--linux-x86-64-cc clang-22 --linux-x86-64-movable-window`, which
  `configure.py` was put back to, with `--ps5`); `build.ninja` is unchanged
  outside the PS5 section. `python3 -m pytest -q tools/test_ps5_build.py
  tools/test_linux_guest_build.py tools/test_guest_layout.py`: 14 passed.
- `configure.py --ps5-vulkan /nonexistent`: exit 0, and `ninja ps5` says
  "PS5 build (ps5) unavailable: no PS5_Vulkan checkout at /nonexistent". A
  `configure.py` that is not asked for the build writes a quiet unavailable
  `ps5` target (the `linux_x86_64` pattern) and prints nothing: the test's
  first case. In this tree it is asked for once `build/ps5/third_party` is
  there, as the plan says.

`ps5vkctl` did not answer (connection refused: the console had been
restarted since phase 0 B). With the user's word that the payload manager was
up, `tools/ps5_deploy.py load-ps5vkctl` loaded it. The manager's requests,
read from ps5-payload-manager v0.5.2's `src/http_server.c` and
`frontend/src/App.jsx` (tag e4e72dc9d831):

```
POST /manage:upload?filename=ps5vkctl.elf   Content-Type: application/octet-stream, the ELF as the body (150936 bytes)
  -> 200 OK: OK      (it stores it as /data/pldmgr/payloads/ps5vkctl/ps5vkctl.elf, as phase 0 B's)
GET /loadpayload:ps5vkctl.elf               (the name resolved in its payload store; handed to the ELF loader on 9021)
  -> 200 OK: OK
ps5vkctl ping: ok ps5vkctl 1 pid=114
ps5vkctl status: ok idle
```

All of it took 0.6 s. `ps5vkctl.elf` is PS5_Vulkan's `build/ps5vkctl` of
phase 0 B, SHA-256 aee9e434…9390; it was not rebuilt.

**The runs** (each `--run` saved its capture and log under
`build/ps5/logs/<UTC time>/`):

1. `--run` (20261018-015922 and -015923): nothing to fetch before; upload,
   6 files, 47.5 MB in 0.7 s; `launch` answered `ok launched PPSA99500
   result=0x00002018 via systemServiceApp/login as user 372994645` (the
   first start of a new title; later ones went `via lncUtilApp/foreground`);
   14.1 s in all, 11.8 s from the launch to the end of the last fetch. The
   log:

   ```
   halo.log: writing to /data/halo/halo.log and the kernel log
   Halo: Combat Evolved for the PS5, build 93c2cc912+changes (phase 1: the bootstrap host)
   system software 13.590.001 (0x13590001)
   built with PS5_Vulkan 8aee3afb37bc, SDK fork 95c08f2, PS5_Mesa 01d70579b8d4, pacbrew v0.40.2, glslang 16.6.0
   SDL2: SDL_GetVersion: 2.30.12 (compiled against 2.30.12)
   RADV: vk_icdGetInstanceProcAddr(NULL, "vkCreateInstance"): af19c0 (non-null)
   glslang: glslang_initialize_process: 1
   glslang: glslang_finalize_process returned
   /data/halo: found; holds halo.log
   bootstrap: the guest's ranges, the image, its start-up (port/ps5/host/host_bootstrap.c)
   image: 0x70000000-0x74000000, 64 MB of direct memory, read, write and execute, zeroed, in 1 ms
   Xbox window: reserving 0x80000000-0xa0000000: placed elsewhere, 0xffffffff
   Xbox window: 0x90000000-0xb0000000, 512 MB of direct memory, zeroed, in 14 ms
   Custom Edition's tag cache: 0x40440000-0x41b40000, 23 MB of direct memory, zeroed, in 0 ms
   pool: 0x10000000-0x20000000, 256 MB of direct memory, zeroed, in 7 ms
   segment 0: 0x70000000-0x70000030, 48 bytes from the file, flags 4
   segment 1: 0x70000030-0x703e20fd, 4071629 bytes from the file, flags 5
   segment 2: 0x703e2100-0x70e93803, 11212547 bytes from the file, flags 4
   segment 3: 0x70ea0000-0x7117524c, 2970188 bytes from the file, flags 6
   segment 4: 0x71175280-0x72a10ff0, 0 bytes from the file, flags 6
   header: magic 0x4f4c4148 (right), ABI 4 (the host's 4), image end 0x72a11000 (loaded to 0x72a10ff0)
   entry points: start 0x703c95b0, thread start 0x703c9740, attach 0x703c9830, detach 0x703c98c0, constructors 0x71175244-0x7117524c
   265 imports
   the game's thread starts on 1fffff8c; MXCSR 0x00001f80
        1 host_set_tp(0x728c870c)
        2 host_syscall gettid(0x0, 0x0, 0x0, 0x0) = 1
        3 host_syscall mmap(0x90000000, 0x20000000, 0x0, 0x104022) = 2415919104
        5   the game writes to stderr: halo-linux:
        7   the game writes to stderr: Xbox contiguous memory window: 512 MB at 0x90000000
        8   the game writes to stderr:
        9 host_syscall brk(0x0, 0x0, 0x0, 0x0) = 0
       10 host_syscall brk(0x2000, 0x0, 0x0, 0x0) = 0
       11 host_syscall mmap(0x0, 0x2000, 0x0, 0x22) = 268500992
       12 host_syscall mprotect(0x10011000, 0x1000, 0x3, 0x0) = 0
       13 host_syscall mmap(0x0, 0x1000, 0x3, 0x22) = 268566528
   after 14 calls to the host in 0.040 s, 0 MB of the pool taken
   system calls: writev 3 gettid 1 brk 2 mmap 3 mprotect 1
   bootstrap: the first import not answered: host_sdl_get_base_path(0x728c87a0, 0x400, 0x1, 0x0)
   the host ends
   ```

   The same 14 calls as the guest probe's run 3, in 0.040 s instead of
   0.101 s (the probe logged to a file through stdio). Runs 1 to 4 had a
   guest built with clang 18 (see below), whose entry points differ from run
   3's; run 5's, with clang 22, are run 3's.
2. `--run` again (20261018-015950, -015951): run 1's `halo.log` was fetched
   first, then upload, launch and follow: 2.2 s in all, 0.9 s from the launch
   to the last fetch. The kernel log then reports the `SIGSYS` after
   `exit()`, as phase 0 B found.
3. The kill test: `--run --hold --timeout 30` (20261018-020004, -020004-2):
   `hold.txt` put; the follow ended at the host's `holding for 120 s before
   the end (/app0/hold.txt), so the title can be killed`. Then:

   ```
   ps5vkctl status: ok running app=24600 title=PPSA99500
   (tools/ps5_deploy.py launch: refused, "PPSA99500 is running already")
   ps5vkctl kill PPSA99500: ok killed PPSA99500: PPSA99500 is gone (app id 24600, 1 processes killed, result 0x00000000)
   ps5vkctl status: ok idle
   ```

4. `--run --hold` again (20261018-020020), then a plain `--run` while it was
   held (20261018-020022, -020024): `--run` fetched, found the title running
   and killed it (`ok killed ... app id 32792, 1 processes killed`), removed
   `hold.txt`, and ran it to `the host ends`: 6.3 s in all. `status` said
   `running` just after the last line and `idle` 3 s later.
5. `--run` with the guest rebuilt by clang 22 (20261018-020817, -020818):
   the same log to `host_sdl_get_base_path`, with run 3's entry points
   (`start 0x703d0470, thread start 0x702343b0, attach 0x702344a0, detach
   0x70234530, constructors 0x7117520c-0x71175214`); 2.2 s in all.
6. `--run` with the build's own title, the Vulkan image as `halo_guest.elf`
   (20261018-021324, -021325): segments to 0x72cabde0 (image end
   0x72cac000), `start 0x703cb060`, **274 imports**, the same 14 host calls,
   `bootstrap: the first import not answered:
   host_sdl_get_base_path(0x72742880, 0x400, 0x0, 0x0)`, `the host ends`;
   2.1 s in all, 0.8 s from the launch.
7. The closing check, from a clean `build/ps5` and in one shell:
   `ninja ps5 && python3 tools/ps5_deploy.py --run`: exit 0, the same log.

The console holds `/data/homebrew/PPSA99500` (the title, the Vulkan image
in it, no `hold.txt`) and `/data/halo/halo.log` of ours. The probes' titles and
`/data/halo` are otherwise as they were; the manager's copy of
`ps5vkctl.elf` was rewritten with the same file.

**The compile survey**, for phase 2: the host's flags of step 5
(`-std=c11 -O2 -g -Wall -Wextra -Werror ... -DHALO_PS5=1`, the includes,
clang 22 through `prospero-clang18`), `-fsyntax-only`, the first error:

| File | Compiles | The first error |
|---|---|---|
| `port/android/host/host_arch.c` | no (20 errors) | `host_arch.c:82: unknown type name 'greg_t'` (Linux's `ucontext_t` layout) |
| `port/android/host/host_common.c` | no | `host_common.c:15: 'tomlc17.h' file not found` (an include path: `port/third_party/tomlc17`) |
| `port/android/host/host_debug.c` | no (6 errors) | `host_debug.c:53: call to undeclared function 'gettid'` |
| `port/android/host/host_gl.c` | no | `host_gl.c:131: unused function 'gl_count_index'` (`-Werror`; the PS5 has no GL, the file goes) |
| `port/android/host/host_loader.c` | yes | |
| `port/android/host/host_memory.c` | no (3 errors) | `host_memory.c:219: use of undeclared identifier 'MAP_NORESERVE'` |
| `port/android/host/host_probe.c` | no (4 errors) | `host_probe.c:222: cast to 'const uint32_t *' from smaller integer type 'uint32_t'` (`-Wint-to-pointer-cast`) |
| `port/android/host/host_sdl.c` | no | `host_sdl.c:16: 'SDL3/SDL.h' file not found` (replaced by the PS5's SDL file) |
| `port/android/host/host_syscall.c` | no | `host_syscall.c:56: 'linux/futex.h' file not found` (replaced by the PS5's) |
| `port/android/host/host_thread.c` | yes | |
| `port/android/host/host_watch_hash.c` | yes | |
| `port/linux/host/host_desktop.c` | no | `host_desktop.c:16: 'update.h' file not found` (an include path: `port/linux/src`) |
| `port/linux/host/host_main.c` | no | `host_main.c:23: 'SDL3/SDL.h' file not found` (the PS5's `host_main.c` takes its place) |
| `port/linux/src/posix_files.c` | yes | |

**Unexpected, and what was done about it:**

- **The Vulkan image** (above): it did not compile in a desktop guest; runs 1
  to 5 used the GL image, then the shared files were changed with the user's
  decision.
- **The guest's default compiler.** The plan's default, `clang`, is clang 18
  here: the guest was built without profile-guided optimisation ("the
  profiles in pgo need clang 22"), and its archive step failed, as this
  machine has no unversioned `llvm-ar` or `llvm-objdump` on the path. So
  `--ps5-guest-cc` defaults to the host's clang 22 (`--ps5-llvm-config
  --bindir`'s, `/usr/lib/llvm-22/bin/clang`, the profiles' compiler), and
  `tools/ps5_build.py` takes `llvm-ar`, `ld.lld` and `llvm-objdump` from the
  folder of an unversioned compiler, as it takes `llvm-ar-22` and the rest
  for `clang-22`. A plain `configure.py --ps5` from a clean `build/ps5` then
  takes 13.8 s, and `ninja -k 0 ps5` 45.0 s (`-fprofile-use=pgo/halo_linux.profdata`),
  with the same two Vulkan objects failing. Runs 1 to 4 were made before
  this, with a clang 18 guest.
- **Clang 22 for the host needs `PS5_CLANG`.** PS5_Vulkan's
  `tooling/prospero-clang18` takes its compiler from `PS5_CLANG`, else the
  first `clang` on the path; `LLVM_CONFIG` reaches only the SDK's own
  wrappers. So the phase 0 B probes' own code was compiled with clang 18. The
  host's rule sets both (`PS5_CLANG` from `llvm-config-22 --bindir`), and
  `radv_link_recipe` then takes clang 22's runtime library too. glslang is
  built with `LLVM_CONFIG` unset, so with clang 18, as the glslang probe was.
- **`ps5-native-tool link` writes no symbol table**, so the plan's
  "`build/ps5/host/eboot.elf` defines the three symbols" holds only of
  prospero-lld's output. That is now `eboot.elf` (with `eboot.elf.map`: the
  file to look a crash up in), and ps5-native-tool's is `eboot.title.elf`,
  which is signed into `eboot.bin`.
- **NOTICE.txt is written by a build rule** (`python3 -m tools.ps5_build
  notice`), from the same constants, not at configure time: written at
  configure time it would be a file ninja could not make again once
  `build/ps5/title` is removed.
- **Paths.** The PS5's own rules name their files by absolute path (CMake
  and `tools/ps5_title.sh` run them elsewhere); the guest's rules are the
  shared generator's, which names them from the repository. The Switch's
  build is the same mixture: only its `elf2nro` needs absolute paths.
- **The hostdeps environment** is checked, as the plan says, but no rule of
  this phase needs meson or LLVM 18: it is sourced only if `libc.prx` has to
  be rebuilt (`tools/rebuild-libc.sh`). It is RADV's build's.
- **`radv_link_recipe` writes `build/radv-platform-local.map` into the
  PS5_Vulkan checkout** at every link, as it did for the probes.
- **klogsrv did not double its lines** in these runs (39 `halo:` lines in
  the capture for 39 in the file). The follower shows a line when it first
  comes and lets one copy of it pass after, so it handles both.
- **FTP's SIZE of a SELF is its decrypted size**: `eboot.bin` (29,599,331
  bytes) is reported as 30,134,764, `eboot.title.elf`'s size exactly;
  `libc.prx` (1,284,674) as 1,335,962. The upload compares the eboot with
  the unsigned ELF and says so for both.
- `ps5_build.py` also has the generator's `build/ps5/host/host_import_table.c`
  written, which phase 1 does not compile (phase 2's).
- `/data/halo: found; holds halo.log`: the log is opened before the folder
  is listed, so it lists itself.

### Phase 0, part B — audit (2026-10-18)

Checked against the acceptance list, on the sources and the console, not
on the write-up:

1. **The builds.** All five title probes build from the tree with
   `title/build.sh` (`glslang`, `sdl2`, `host`, and part A's `io` and
   `guest`), with `-Werror`, on this machine. Nothing of PS5_Vulkan's, the
   fork's, pacbrew's or glslang's is in the repository: the three
   `build-extra.sh` source `radv-link.sh` and call
   `setup-pacbrew-dependencies.sh` where they are, and compile PS5_Vulkan's
   AGC stub sources from its tree into the build folder. The host probe's
   Vulkan code shares three lines with `radv_smoke.c`, all boilerplate
   (`DECLARE`, a memory-property mask, a struct declaration); nothing of the
   smoke test's structure or strings. The pins are in "Progress".
2. **The reports** are in `results/`, each with the system software line
   first; the skipped sections name the kernel log's line. `spirv-val`
   passes the six SPIR-V files again here (`build/ps5/spirv`).
3. **"Progress"** has every item's answer with the lines that show it; the
   glslang probe's compiler input is the renderer's field for field
   (Vulkan 1.0, SPIR-V 1.0, version 450, no profile, the Vulkan and SPIR-V
   rules); the futex forms are the 32-bit private ones; the fault handler's
   `SIG_DFL` fallback now fires only when an unprotect fails, which was the
   run 1 bug.
4. **"Decisions"**: the SDL, shader compiler, texture write tracking and
   futexes rows are rewritten with their reasons; the user's two rows are
   rewritten with the user's answers.
5. **Nothing outside `port/ps5/probe` and `PS5.md` changed** (the other
   two modified files are the review's own audit of the findings and the
   handoff).
6. **The console** (listed over FTP, read-only) holds PPSA99103, 99104 and
   99105 beside part A's titles, each with its report; no skip file was
   left; `/data/halo` is empty; `/download0` holds nothing of ours. Two
   things are there that were not before, both disclosed: the `ps5vkctl`
   payload under `/data/pldmgr/payloads/` and `/data/ps5vkctl.log`. They
   are the user's to keep or remove.

Two deviations from the part as written, both accepted:

- **SDL2's link.** The part said a failed link means route (2). The
  implementer linked SDL2 with the RADV link recipe's libc bindings and a
  `___mb_cur_max` of its own instead, on the argument that the host links
  with that recipe anyway. Accepted: the host does, and the result is a
  working pad and audio with no shim to write. The two conditions are now
  in the SDL row and phase 3.
- **Section 6 with the user** ran in run 3, whose report file run 4
  overwrote; its lines were recovered from the kernel log, de-duplicated by
  hand, and are in `host-probe-run3-section6.txt`. Run 4's own section 6,
  with nobody at the console, agrees with it where they overlap. Accepted,
  with the lesson for phase 1's deploy: fetch the report before a title is
  started again.

What the audit changed in the plan, from the findings the implementer left
for the review:

- "Where the guest's memory goes" and the Risks: the host's `malloc` is
  the platform heap, in direct memory, and never shrinks; flexible memory
  is not counted on.
- Phase 2: `getcwd` and `realpath` are the host's; `exit()`'s `SIGSYS`;
  the extraction's write speed and a cold read are measured there.
- Phase 3: never `SDL_INIT_VIDEO`; the pad chosen by type and GUID.
- Phase 4 and the Files table: `vk_icdGetInstanceProcAddr`; RADV's shader
  cache moved to the data folder.
- Phase 5: the PS button as phase 0 B found it; the heap's peak watched.
- Phase 1: the host's link is the RADV recipe plus glslang's archives,
  SDL2's and `___mb_cur_max`; the deploy fetches the report before a
  relaunch and tries `kill` on a running title, which item 9 did not.

### Phase 0, part B

Run on 2026-10-17 on the user's console (system software 13.590.001);
audited 2026-10-18 (below). `/data/halo` was visible to the title (item 5).
The reports are in `port/ps5/probe/results/`:

- `host-probe.txt`: run 4, the complete one, launched by `ps5vkctl`. Its
  section 6 ran with nobody at the console.
- `host-probe-run1.txt` and `host-probe-run2.txt`: the runs that crashed.
- `host-probe-run3-section6.txt`: section 6 with the user's PS-button
  steps, recovered from the kernel log.
- `sdl2-probe.txt` (run 2) and `sdl2-probe-run1.txt`.
- `glslang-probe.txt`.
- `ps5vkctl.txt`.
- `klog-crashes.txt`: every fatal signal, with what it was.

The probes are in `port/ps5/probe/host`, `sdl2` and `glslang`. The pins are:

- PS5_Vulkan 8aee3afb37bc;
- the SDK fork 95c08f2 (`.ps5-sdk-revision`);
- PS5_Mesa 01d70579b8d4, for the RADV release archive and the Vulkan headers;
- pacbrew v0.40.2, SHA-256
  a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d;
- glslang 16.6.0 (e1b562a8bed2), the Android build's clone reused.

The answers, item by item:

**1. SDL2 (item 1): route (1), pacbrew's SDL2, for the pad, audio, events and
timers; never `SDL_INIT_VIDEO`.**

- It does not link with pacbrew's flags alone: its `libiconv.a` wants
  `nl_langinfo` and `___mb_cur_max`, which no system module exports
  (`SDL2_PLAIN_LINK=1` reproduces this). The plan says a failed link means
  route (2). It is linked here as the host will be linked anyway: with the
  RADV link recipe's libc bindings, which cover `nl_langinfo`, and a
  `___mb_cur_max` returning 1, the C locale's value (`sdl2/libc_extra.c`).
  That is the review's call to accept.
- With that link, every step ran except video:

  ```
  compiled against SDL 2.30.12, linked 2.30.12, revision SDL-2.30.12-ga47182a20
  SDL_InitSubSystem(SDL_INIT_TIMER): 0 in 203 us
  SDL_InitSubSystem(SDL_INIT_EVENTS): 0 in 23 us
  SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER): 0 in 995 us
  SDL_InitSubSystem(SDL_INIT_AUDIO): 0 in 1737 us
  joystick 0: "thelinkin3000", game controller yes, GUID 0300d0424c050000e60c000011810000, type 7
  joystick 1: "PS5 Remote Control", game controller yes, GUID 030000004c050000cec0000011810000, type 0
  3431 events: 122 button changes, 1557 axis motions
  SDL_GameControllerRumble(0x8000, 0x8000, 1000 ms): 0
  SDL_GetNumAudioDrivers: 3: ps5 disk dummy
  the spec SDL gave back: 48000 Hz, format 0x8120, 2 channels, 256 samples, buffer 2048 bytes, silence 0
  the callback: 374 calls in 2 s (375 expected at 256 samples), gaps between calls min 5279 us, median 5332 us, max 5399 us
  SDL_QueueAudio: 2.027 s of sound queued in 2 s; ... min 6144 bytes, max 14336 bytes ...; 0 failures
  over SDL_Delay(5000): the time stamp counter 5.001011 s, SDL_GetTicks64 5.000 s (-1.011 ms), SDL_GetPerformanceCounter 5.000072 s (-0.939 ms)
  ```

- The pad: every button (a, b, x, y, the d-pad, start, both shoulders,
  both stick clicks) and every axis (both sticks, both triggers) arrived.
  The run has no touch pad click, back or guide. The user felt the pad
  vibrate and heard both tones.
- The joystick's name is the signed-in user's PSN name. A second,
  "PS5 Remote Control", is listed too, so the host has to choose the pad
  rather than take index 0 blindly.
- `SDL_INIT_VIDEO` crashed the title. `PS5_CreateDevice` calls
  `PS5_Keyboard_Init`, which calls `sceKeyboardInit` through a NULL import,
  because `libSceKeyboard.sprx` is not loaded into a title:

  ```
  # signal: 11 (SIGSEGV)  # reason: page fault (user read instruction, page not present)
  # fault address: 0000000000000000  # rip: 0000000000000000
  backtrace 0x4f3e08 = PS5_CreateDevice (after the call to PS5_Keyboard_Init), 0x4aa2e6 = SDL_VideoInit_REAL
  ```

- Run 2 skipped video (the guard) and `SDL_Quit returned`.

**2. glslang (item 2): it builds with the title toolchain and runs on the
console.**

- The build: glslang 16.6.0, static, with the SDK fork's
  `toolchain/prospero.cmake`, whose `bin/prospero-clang` runs the clang
  `prospero-llvm-config` finds (18 here). Clang 22 was not needed.
- glslang 16 makes one `libglslang.a`, which holds MachineIndependent,
  GenericCodeGen, OSDependent and SPIRV (`libSPIRV.a` is empty), plus
  `libglslang-default-resource-limits.a`.
- The link: libc++ needs the platform layer's `newlocale`, `catopen` and
  the rest, so it uses the RADV link recipe without the archive, as the
  host will. That also sends glslang's allocations to the platform heap.

```
glslang_initialize_process: 1 in 4 us
the first compile of all (pass.vert, cold): 160911 us
pass.vert: 677 bytes of GLSL, 302 words of SPIR-V; first 160911 us, then min 1339 us, median 1361 us (of 9)
pass.frag: 328 bytes of GLSL, 158 words of SPIR-V; first 1368 us, then min 1278 us, median 1301 us (of 9)
vs_typical.vert: 6366 bytes of GLSL, 4415 words of SPIR-V; first 4240 us, then min 3971 us, median 4012 us (of 9)
ps_typical.frag: 4100 bytes of GLSL, 2339 words of SPIR-V; first 3374 us, then min 3005 us, median 3023 us (of 9)
vs_large.vert: 11374 bytes of GLSL, 8845 words of SPIR-V; first 7349 us, then min 7055 us, median 7140 us (of 9)
ps_large.frag: 8568 bytes of GLSL, 6046 words of SPIR-V; first 6093 us, then min 5726 us, median 5793 us (of 9)
after all compiles: flexible memory available 433.0 MB; ...; the platform heap ... holds 48.0 MB (16.0 MB before)
after glslang_finalize_process: ... the platform heap ... holds 48.0 MB
```

- `spirv-val --target-env vulkan1.0` (SPIRV-Tools v2025.1, hostdeps) passes
  all six files pulled from `/app0/spirv`.
- Flexible memory did not move (433 MB). The 32 MB glslang took came from
  the platform heap, in direct memory, and stayed there after
  `glslang_finalize_process`.

**3. Futexes (host section 1): `_umtx_op`'s 32-bit operations work, and are
the answer.**

- The link proves `libkernel` exports `_umtx_op`. The word is at
  0x10000000, in the pool; the waiters' stacks are in the pool.

```
-- UMTX_OP_WAIT_UINT, UMTX_OP_WAKE (wait 11, wake 3)
wait for 0 on a word holding 1: returned 0, errno 0 (-), in 3 us
one waiter (its stack at 0x1004ff7c): the wake returned 0; the waiter returned 0, errno 0
wake to the waiter's return, 10 runs: min 5 us, median 6 us, max 8 us
wait with a relative struct timespec of 100 ms: returned -1, errno 60 (Operation timed out), after 100128 us
wait with a relative struct _umtx_time of 100 ms (CLOCK_MONOTONIC): returned -1, errno 60 (Operation timed out), after 100130 us
wait with struct _umtx_time, UMTX_ABSTIME, now + 100 ms on CLOCK_MONOTONIC: returned -1, errno 60 (Operation timed out), after 100083 us
four waiters, wake INT_MAX: the wake returned 0 (errno 0); 4 of 4 had returned 50 ms later
-- UMTX_OP_WAIT_UINT_PRIVATE, UMTX_OP_WAKE_PRIVATE (wait 15, wake 16)
(the same: 2 us; wake to return min 5 us, median 6 us, max 7 us; ETIMEDOUT at 100 ms in all three forms; 4 of 4)
-- UMTX_OP_WAIT (a long, 64 bits) on a 32-bit word
wait for 0 on a word holding 0 whose next word holds 1, 100 ms timeout: returned 0, errno 0 (-), after 2 us
```

- A wait on a word that already differs returns 0, not `EAGAIN`.
- A wake returns 0, not the number it woke.
- `UMTX_OP_WAIT` returned at once although the 32-bit word matched: it
  compared 0x100000000. It must not be used on the guest's words.

The guest's futex calls (musl's), as the host will answer them, with the
Switch's `host_futex.c` as the model:

| The guest asks | The host does |
|---|---|
| `FUTEX_WAIT` (addr, val, relative timeout or none) | if `*addr != val`, answer `-EAGAIN` (`_umtx_op` would return 0, so the host compares first); else `_umtx_op(addr, UMTX_OP_WAIT_UINT_PRIVATE, val, ...)`, with no timeout as `NULL, NULL` and a timeout as a `struct _umtx_time` (flags 0, `CLOCK_MONOTONIC`) passed with its size in `uaddr`. 0 answers 0, whether woken or the word changed in between (musl re-tests); `ETIMEDOUT` (FreeBSD's 60) answers `-ETIMEDOUT` (Linux's 110); `EINTR` answers `-EINTR` |
| `FUTEX_WAIT_BITSET` (addr, val, absolute timeout, mask) | the same, with the bitset ignored as on the Switch, and the timeout as `struct _umtx_time` with `UMTX_ABSTIME`: `CLOCK_MONOTONIC`, or `CLOCK_REALTIME` when the guest set `FUTEX_CLOCK_REALTIME` (256) |
| `FUTEX_WAKE` (addr, n) | `_umtx_op(addr, UMTX_OP_WAKE_PRIVATE, n, NULL, NULL)`; it answers 0, since FreeBSD does not count the threads it woke and musl's callers only test for `-ENOSYS` |
| `FUTEX_WAKE_BITSET` | as `FUTEX_WAKE` |
| `FUTEX_PRIVATE_FLAG` (128) | ignored: the guest is one process, so the private forms are always used |
| `FUTEX_REQUEUE` | wake n on the first word and all on the second (the Switch wakes as many on the second as it woke on the first, a count FreeBSD does not give; waking all of them is a superset that musl's loops tolerate) |
| the priority-inheritance operations | `-ENOSYS`, as on the Switch |

**4. Faults (host section 2): a title gets the write fault, with its
context, on the alternate stack; the watch costs 63 µs a page.**

- `sysconf(_SC_PAGESIZE)` answers 16384.
- `sceKernelMprotect` and libc's `mprotect` behave the same on the
  window's direct memory, and `mprotect` the same again on flexible memory.
- The handler is installed with `sigaction` and `SA_SIGINFO | SA_ONSTACK`,
  on a 64 KB `sigaltstack` from `malloc`.

```
-- 2. faults: sceKernelMprotect on the window
  the faulting thread's stack is at 1074ff38
  sceKernelMprotect(0x90100000, 16384 bytes, read only): 0 (0x00000000)
  the write: 1 fault; then the value reads 0x22222222 (the write completed)
  signal 11 (SIGSEGV), si_code 2, si_addr 0x90100100
  mc_rip 0x404dd5 mc_rsp 0x1074ff20 mc_rbp 0x1074ff90 mc_rax 0x0 mc_rdi 0x90100000
  mc_err 0x7 mc_trapno 12 mc_addr 0x90100100 mc_flags 0x5
  the handler ran on 0x200100f644: inside the alternate stack (0x20010003e0-0x20010103e0); the interrupted stack is below 4 GB
  the handler's sceKernelMprotect(read, write): 0
-- 2. faults: mprotect on the window            (the same, si_code 2, si_addr 0x90200100)
-- 2. faults: mprotect on flexible memory       (0x20001c000: the same, si_code 2, si_addr 0x20001c100)
  1000 cycles of sceKernelMprotect read only, write, fault, the handler's unprotect: 63344 us, 63.34 us a cycle (the protect alone 28.68 us); 1000 faults, 0 failed protects
  1000 cycles of mprotect read only, write, fault, the handler's unprotect: 63239 us, 63.24 us a cycle (the protect alone 28.69 us); 1000 faults, 0 failed protects
-- 2. faults: a read of unmapped 0x08000000
  signal 11 (SIGSEGV), si_code 1, si_addr 0x8000000
  mc_err 0x4 mc_trapno 12 mc_addr 0x8000000 mc_flags 0x5
```

- A watched write is `si_code` 2 (`SEGV_ACCERR`) with `mc_err` 7. A plain
  crash on an unmapped page is `si_code` 1 (`SEGV_MAPERR`) with `mc_err` 4.
- The fields are where `ps5platform/context.h` says the console puts
  them: `mc_rip` is the faulting `mov` in the probe.
- The 1,000-cycle test crashed run 1. That was the probe's own fault: its
  handler fell back to `SIG_DFL` after the third fault. It was fixed
  before run 2.

**5. The data folder (host section 3): `/data/halo` is visible, readable and
writable by the title; the "Game data" decision stands.**

```
stat(/data/halo): mode 040777, uid 0, gid 0
wrote /data/halo/probe/16mb.bin: 16 MB in 1 MB fwrites, fflush, fsync (0): 0.885 s, 18.1 MB/s
wrote /data/halo/probe/100mb.bin: 100 MB in 1 MB fwrites, fflush, fsync (0): 5.476 s, 18.3 MB/s
read 100 MB in one read after open: 104857600 bytes in 1 call, 0.040 s, 2473.0 MB/s
read it in 64 KB reads: 104857600 bytes, 0.036 s, 2759.7 MB/s
read it through fread, a 64 KB buffer: 104857600 bytes, 0.040 s, 2530.7 MB/s
stat(/data/halo/probe/100mb.bin): st_size 104857600, st_mode 0100666, st_mtime 1792281372 (the clock: 1792281372, +0 s), st_blksize 65536
access(/data/halo, F_OK | R_OK | W_OK): 0
access(/data/halo/probe/missing.bin, F_OK | R_OK | W_OK): -1, No such file or directory
realpath(/data/halo/probe): Operation not permitted
rename(100mb.bin, renamed.bin): 0 / unlink of it: 0 / rmdir(/data/halo/probe): 0
/app0: the same, writes 18.8 and 18.5 MB/s, reads 4150, 4530 and 3892 MB/s
stat(/download0): mode 040777 / /download0: a file was made and written; unlink: 0
```

- Writes run at about 18 MB/s with `fsync`, in both places.
- The reads came straight after the writes, so they are the cache's
  speed, not the disc's. A cold read of a map was not measured.
- `access` is the platform layer's (`--defsym=access=ps5_access`, from the
  recipe), so its answers are that layer's, not the system's.
- `realpath` refuses.
- `getcwd` crashed the title in run 2, and again when it was asked last in
  run 3: the import jumps to an unmapped 0x100026c946 (`klog-crashes.txt`).
  The host answers the guest's `getcwd` itself and must not call libc's.
- `/data/halo` was left empty, as the user made it. Nothing was left in
  `/download0`.

**6. The host's memory (host section 4): RADV's allocations, and the host's
`malloc` under the RADV link recipe, are direct memory; flexible memory
does not move.**

The direct figure is the largest free block, which equals the free total
while nothing is fragmented.

```
memory before anything: flexible available 417.0 MB; direct memory's largest free block 12268.0 MB; the platform heap maps 16.0 MB
memory after the guest's ranges: flexible available 417.0 MB; direct memory's largest free block 11413.0 MB; the platform heap maps 16.0 MB
malloc of 16 MB blocks, every page written: 512 blocks, 8192 MB, in 0.023 s; stopped at 8 GB
memory with that taken: flexible available 417.0 MB; direct memory's largest free block 3093.0 MB; the platform heap maps 8336.0 MB
memory after it is freed: flexible available 417.0 MB; direct memory's largest free block 3093.0 MB; the platform heap maps 8336.0 MB
device "PlayStation 5 GPU (RADV NAVI21)", Vulkan 1.4.354, driver version 0x06802000, vendor 0x1002
memory heap 0: 4096.0 MB, flags 0x0 / memory heap 1: 8192.0 MB, flags 0x1 device-local
memory types: 0, 1, 7 device-local (heap 1); 2, 8 host-visible coherent (heap 0); 3, 4, 9 device-local host-visible coherent (heap 1); 5, 6, 10 host-visible coherent cached (heap 0)
memory with the instance and the device: flexible available 417.0 MB; direct memory's largest free block 3056.0 MB; the platform heap maps 8352.0 MB
vkAllocateMemory(256 MB, type 0, device-local): 0 in 23 us        -> direct 2800.0 MB, flexible 417.0 MB
vkAllocateMemory(64 MB, type 2, host-visible and host-coherent): 0 in 3011 us -> direct 2736.0 MB, flexible 417.0 MB
vkMapMemory of the 64 MB: 0, at 4010000000 / written and read back: 0 words wrong
memory after both are freed: direct memory's largest free block 3056.0 MB
```

- The guest's ranges take 855 MB of direct memory.
- The instance and the device take 37 MB of direct memory, 16 MB of it
  the heap's.
- Both `vkAllocateMemory` pools are direct memory, size for size, and
  freeing gives it back.
- The heaps RADV reports (4 + 8 GB) are views of the same 12 GB the guest
  and the host's `malloc` also take from, so they overstate what is free.
- The platform heap does not give freed memory back: its peak is
  permanent. With the guest's 855 MB, the 8 GB of `malloc` left 3 GB for
  everything else.

**7. The display (host section 5): one mode, 3840x2160 at 59.94 Hz; no other
can be made, so the renderer scales to 4K itself.**

```
display 0: "PS5 VideoOut", 0x0 mm, 3840x2160, transforms 0x1, plane reorder 0, persistent content 0
mode 0: 3840x2160 at 59940 mHz
plane 0: current display display 0, stack index 0; supported displays (0): 1 (display 0 first)
vkCreateDisplayModeKHR(1920x1080 at 59940 mHz): -3
vkCreateDisplayModeKHR(1920x1080 at 60000 mHz): -3
vkCreateDisplayModeKHR(3840x2160 at 60000 mHz): -3
vkCreateDisplayModeKHR(1280x720 at 59940 mHz): -3
plane 0 capabilities on 3840x2160 at 59940 mHz (the display's): 0; alpha 0x1, source position 0,0 to 0,0, source extent 1x1 to 3840x2160, destination position 0,0 to 0,0, destination extent 3840x2160 to 3840x2160
surface capabilities (0): images 2 to 5, current 3840x2160, min 1x1, max 3840x2160, layers 1, transforms 0x1 (current 0x1), composite alpha 0x1, usage 0x9f
surface formats (0): format 44 colour space 0; format 50 colour space 0;
present modes (0): 2 (FIFO)
vkCreateSwapchainKHR(FIFO, 3840x2160, format 44, 3 images asked): 0
the native mode: 60 frames, 60 presented; the last 50 took 0.834 s (50 refreshes at 59.94 Hz: 0.834 s); intervals min 16.65 ms, median 16.69 ms, max 16.73 ms
no 1920x1080 mode exists or was created: the renderer scales to the native mode itself
```

- -3 is `VK_ERROR_INITIALIZATION_FAILED`.
- The plane's source extent goes down to 1x1, so a smaller image on the
  plane might be scanned out scaled. The surface's current extent is fixed
  at 3840x2160, though, so a swapchain cannot ask for it. The 1080p
  swapchain the plan asked for could not be made.
- The formats: 44 is `B8G8R8A8_UNORM`, 50 is `B8G8R8A8_SRGB`. FIFO is the
  only present mode.

**8. Leaving and returning (host section 6, run 3): the title keeps running
and presenting while the system's menu is up; nothing breaks and nothing
tells it but two flags.**

```
  6.526 s: the pad: connected 1, intercepted 1 (buttons 0x80000000)
  6.526 s: sceSystemServiceGetStatus: 0x00000000; events waiting 0, then bytes 01 00 00 00 (isSystemUiOverlaid first)
 17.839 s: sceSystemServiceGetStatus: 0x00000000; events waiting 0, then bytes 00 00 00 00 (isSystemUiOverlaid first)
 17.873 s: the pad: connected 1, intercepted 0 (buttons 0x00000000)
 19.758 s: the pad: connected 1, intercepted 1 (buttons 0x80000000)
 19.775 s: sceSystemServiceGetStatus: ... bytes 01 00 00 00
 19.875 s: sceSystemServiceGetStatus: ... bytes 00 01 00 00
 23.780 s: the pad: connected 1, intercepted 0 (buttons 0x00000000)
 23.780 s: sceSystemServiceGetStatus: ... bytes 00 00 00 00
 (every second, 0 to 90 s: 60 frames in 1.001 s, intervals min 16.6 ms, median 16.69 ms, max 16.8 ms; the monotonic and real clocks one second apart each line)
90 s: 5396 frames presented in all; the swapchain never reported out of date
after the 90 s: 10 of 10 frames presented
```

- PS button from 6.5 to 17.8 s, the Quick Menu from 19.8 to 23.8 s.
- The pad's intercepted bit and the status's first byte (`isSystemUiOverlaid`)
  follow the PS button's menu. The Quick Menu sets the first byte for 0.1 s
  and then the **second** byte for as long as it is up (`00 01 00 00` from
  19.875 s to 23.780 s; the probe logs only changes). Corrected by phase 3's
  audit: this entry first read the second byte as set for 0.1 s only.
- Frames, the acquire and the present never stopped. The timestamps are
  continuous: the process is not suspended.
- `sceSystemServiceReceiveEvent` answered 0x80a10004 throughout and
  delivered no event.
- So the host has to pause the game itself, and stop reading the pad,
  while the intercepted bit or `isSystemUiOverlaid` is set. Phase 5 handles
  that.
- Run 4 repeated section 6 with nobody at the console: 5396 frames, no
  change of either flag.
- The two declarations are OpenOrbis's PS4 ones, each given a 16 KB buffer
  with only the first fields read. That is enough for what is reported
  here.

**9. `ps5vkctl` (item 9): run, with the user's leave for this session.**

- PS5_Vulkan 8aee3af's payload. The console's ELF loader takes
  connections from the console only, so it was loaded through the user's
  payload manager (itsPLK's 0.5.2, port 8084: `/manage:upload`, then
  `/loadpayload:`).

```
> ping  (5 ms)              ok ps5vkctl 1 pid=180
> status  (5 ms)            ok idle
> users  (5 ms)             ok users init=0x00000000 foreground=372994645 list=0x00000000 [372994645,-1,-1,-1]
> procs  (5 ms)             ok procs app=4294967295 title= count=0 pids=
> launch PPSA99103  (662 ms) ok launched PPSA99103 result=0x0000a018 via lncUtilApp/foreground as user 372994645 (the console reports the title running)
first report line in the kernel log 0.80 s after the launch command was sent
> status                    ok running app=40984 title=PPSA99103
> procs                     ok procs app=40984 title=PPSA99103 count=1 pids=181
> status (after the run)    ok idle
> kill PPSA99103  (9 ms)    ok idle PPSA99103: no application is running
```

- The probe ends itself, so `kill` met no title and refused: that is its
  safety check working. A `kill` of a running title was not tried.
- It left two files on the console:
  `/data/pldmgr/payloads/ps5vkctl/ps5vkctl.elf` (now in the manager's
  list) and `/data/ps5vkctl.log`. Whether they stay is the user's call.

**The user's answers** (2026-10-17): "Shared code" is used as it is, not
copied; `ps5vkctl` stays the way runs are started. Both rows of "Decisions"
are rewritten.

**Unexpected, and dealt with or left for the review:**

- **RADV's archive has no `vkGetInstanceProcAddr`.** Its entry point is
  `vk_icdGetInstanceProcAddr`, which the host probe calls; this part said
  `vkGetInstanceProcAddr`. Phase 4's `host_vk_driver_ps5.c` calls the ICD's.
- **The host's `malloc` is not in flexible memory.** The RADV link recipe
  wraps `malloc` and its family into the platform layer's heap, which is
  direct memory and never shrinks. That contradicts "Where the guest's
  memory goes": flexible memory went unused in every probe (417 to 433 MB
  free throughout). The row needs rewriting: the guest's ranges, RADV and
  the host's `malloc` all share the 12 GB, and the heap's peak is for
  good. Not changed here; it is not one of the four rows this part settles.
- **`exit()` from a title ends in `SIGSYS`.** The kernel log records "calls
  exit() exit_value=0", then a fatal signal 12. This happened with
  glslang's `return` from `main`, SDL2's, and the host probe's `_exit(0)`.
  The process ends either way, but the host's exit should be checked in
  phase 2 (PS5_Vulkan's titles never return from `main`).
- **Libc imports that crash or refuse in a title:**
  - `getcwd`'s import jumps to unmapped memory;
  - `realpath` answers `EPERM`;
  - `libSceKeyboard` is not loaded for a title, so a call into it jumps
    to 0.

  The host avoids all three.
- **The kernel log came through twice.** klogsrv's lines arrive doubled
  and interleaved with the system's, so the run 3 lines were
  de-duplicated by hand.
- **The runs did not go to plan:**
  - run 1 hit the probe's handler bug;
  - run 2 hit `getcwd`;
  - the user started run 3 by hand before `ps5vkctl` was loaded, and its
    report file was overwritten by run 4 (its section 6 is recovered from
    the kernel log);
  - run 4 found run 3's crash on file and skipped `getcwd`.

  Before run 2 the skip file was deleted by hand, because run 1's crash
  was the probe's own bug.
- **`title/build.sh`'s hook adds to the plan's list:** `extra_stubs` (the
  AGC stub libraries `ps5-native-tool link` must be given), the probe's
  `.cpp` sources, and `linker_script` as an array. Part A's probes build
  as before; `io` and `guest` were rebuilt to check.
- **RADV writes its shader cache** to `/app0/radv-shader-cache` in
  PPSA99103, as PS5_Vulkan documents.

### Phase 0, part A

Done 2026-10-16, before this plan. The probes and their results are in
`port/ps5/probe` and `port/ps5/probe/results/`; what they found is written
up in `PS5_FINDINGS.md`, with the console, the toolchain's traps, the
memory map of a title, the guest's three runs, the pad's sample layout and
button bits, audio's timings and RADV's smoke test. The two code changes
that came of it: upstream PR #359 merged into this branch (the x32 guest),
and `HALO_MOVABLE_WINDOW` (`12365b92a`), which lets the x32 guest take the
window where the host puts it.
