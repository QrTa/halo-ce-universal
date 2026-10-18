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
next, to be written out.
Nothing of the port itself exists yet; the probes are in `port/ps5/probe`.

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
| `port/ps5/host/host_mman.c` | `port/switch/host/host_mman.c` | the guest's ranges as direct memory; `mmap`, `munmap`, `mprotect`, `madvise` served inside them for `host_memory.c` |
| `port/ps5/host/host_syscall.c` | `port/switch/host/host_syscall.c` | the guest's Linux system calls on FreeBSD's libc: files (`stat` layouts, open flags), clocks, `writev`, directories, exit |
| `port/ps5/host/host_futex.c` | `port/switch/host/host_futex.c` | futexes over `_umtx_op`, or over a mutex and condition variable per address |
| `port/ps5/host/host_sdl_ps5.c` (and `host_sdl3_events.c` if SDL2 is under it) | `port/switch/host/host_sdl2.c` | SDL3's 69 calls answered: pad, audio, events, ticks; the window, context, displays, 2D renderer and dialogs as stand-ins |
| `port/ps5/host/host_vk_driver_ps5.c` | `port/android/host/host_vk_driver.c` | RADV linked in: its ICD entry point `vk_icdGetInstanceProcAddr` called directly (the archive has no `vkGetInstanceProcAddr`), the driver's name for the log |
| `port/ps5/host/host_vk_present_ps5.c` | `port/android/host/host_vk_present.c` | the surface through `VK_KHR_display` on VideoOut, the FIFO swapchain, the letterboxed blit, re-creation |
| `port/ps5/host/host_update_none.c` | — | `host_update_*` unavailable |
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

`ninja ps5` and one command to run it on the console. `tools/ps5_build.py`
after `tools/switch_build.py`: the x32 guest from
`tools/linux_guest_build.py`'s target with the movable window and
`vulkan=True`; the host compiled with the SDK fork's clang (clang 22 under
it, as `probe/vk/build.sh` does, so Mesa's entry points link), linked as
the phase 0 B probes were linked, together: PS5_Vulkan's RADV link recipe
(`radv-link.sh`, sourced where it is: its linker script, libc++, the
platform layer's libc bindings and allocator wraps, the AGC stubs), RADV's
archive whole, glslang's two archives whole, pacbrew's SDL2 with its
transitive archives and the `___mb_cur_max` the host provides, and
`libps5platform.a`; through `ps5-native-tool` into a fake-signed
`eboot.bin`; `param.json` with a title ID of our own, the icon, `libc.prx`;
the image beside the eboot. `configure.py` gets the options that name the
PS5_Vulkan checkout, PS5_Mesa and the LLVM, and `ninja ps5` says plainly
what is missing when it cannot build. `tools/ps5_deploy.py` uploads the
title (`eboot.bin` and `param.json` last), starts, stops and restarts it
through `ps5vkctl` (loaded once per boot through the user's payload
manager, as "Testing" says), reads the kernel log while it runs, and fetches
the report and log files **before** any relaunch (phase 0 B lost a run's
file that way). It tries `kill` on a running title, which phase 0 B did not.

The host in this phase is the smallest that proves the pipeline: the
guest probe's section 6 as a host (`host_main.c`, `host_mman.c`'s ranges,
the import thunks pointed at the real host's table with every unanswered
import reported), so the title built by `ninja ps5` reaches
`host_sdl_get_base_path` as the probe did.

**Ends with:** `ninja ps5 && python3 tools/ps5_deploy.py --run` builds,
installs and starts the title from one shell, and its log shows the game's
start-up to the first SDL call. `port/ps5/README.md` says how.

## Phase 2 — the host's core, no graphics

The system layer, after the Switch's, with the Android host's shared files
running as they are: `host_mman.c` (direct memory for the guest's ranges;
`mmap`, `munmap`, `mprotect` and `madvise` for `host_memory.c`, including
`MAP_FIXED_NOREPLACE` for the window's reservation), `host_syscall.c`
(files with the guest's `stat` and open flags translated, directories,
clocks, `writev` to the log, the ids, exit; `getcwd` and `realpath`
answered by the host itself, never libc's: a title's `getcwd` import jumps
to unmapped memory and `realpath` answers `EPERM`), `host_futex.c` (over
`_umtx_op`'s 32-bit private operations, the table under "Progress"),
`host_thread.c`'s stacks below 4 GB (proved), `host_debug.c`'s fault
reports with the PS5's `ucontext`, `host_common.c`'s boot block and
environment (the data folder as the game's home, `HALO_*` settings from
`config.toml`), the log file and the kernel log. The game's SDL is answered
by stand-ins (a window and a GL context that exist, no pad, no audio; the
first start's message box answered "Yes" and its file picker with the disc
image found in `/data/halo`, so the desktop's extraction of `maps/` runs
headless, its progress in the log), and the renderer's host half by a null
backend that retires every submission unseen, so the game runs headless.
`debug.exit_after` ends a run cleanly; a title's `exit()` and `_exit()` end
in `SIGSYS` in the kernel log (PS5_Vulkan's titles too), so the host's exit
is checked here for what it leaves behind (the log flushed, the files
closed) and the signal is explained once in the log, or avoided if a cleaner
end is found. Two numbers from phase 0 B to settle here: the extraction's
write speed (18 MB/s with `fsync` after each file in the probe; measure
without) and a cold read of a map (the probe's reads were the cache's).

**Ends with:** the game, started from `ninja ps5` against a `/data/halo`
holding only the disc image and a `config.toml`, extracts `maps/`, loads
the main menu's map and runs its loop, frames ticking in the log, for a
minute, and exits when told; the Switch's host unit tests
(`tools/test_host_syscall.py`, `tools/test_host_units.py`) pass for the
PS5's files where they apply.

## Phase 3 — SDL: pad, audio, events

The route phase 0 chose, pacbrew's SDL2 under the Switch's arrangement
(`host_sdl2.c`, `host_sdl3_events.c` as the model), in `host_sdl_ps5.c`:
SDL2 initialised for timers, events, game controllers and audio and **never
for video** (its video driver calls into `libSceKeyboard`, which a title
does not have, and crashes); the pad as an SDL game controller, chosen by
its type and GUID and not by index (the console lists "PS5 Remote Control"
beside it, and names the pad after the signed-in user), with the dead zone,
rumble through `SDL_GameControllerRumble`, and the pad's `intercepted` bit
(read through `scePad` beside SDL, or SDL's own events if it reports it) as
a pause; events for the game's loop (button and axis events, quit); audio
through an SDL2 audio device at 48 kHz float, 256 samples a callback
(5.3 ms apart, as probed), the guest's SDL3 audio streams queued onto it;
ticks from `SDL_GetTicks64` or the monotonic clock; the clipboard and text
input as a console has them; and
the desktop's calls (display modes: one, 1920x1080; fullscreen: always;
the 2D renderer and dialogs: accepted and invisible, logged once; message
boxes: logged). Every SDL call the guest makes crosses as integers,
strings and flat structures, as `host_sdl.c`'s do.

**Ends with:** the menus driven by the pad and heard through the TV, with
nothing drawn; a campaign level started blind from the menu by the pad
and left running with its sound for a minute; the log's event counts and
audio underruns (none) under "Progress".

## Phase 4 — the Vulkan renderer on RADV

The Android renderer's host half on the console, with two files replaced:
`host_vk_driver_ps5.c` (RADV is linked in; its ICD entry point
`vk_icdGetInstanceProcAddr` is called directly, as the host probe did; no
adrenotools, no `dlopen`)
and `host_vk_present_ps5.c` (`VK_KHR_display`: the display, its mode, a
plane surface, a FIFO swapchain of the surface's count plus one, the
letterboxed blit, the smoke test's sequence, which passed; re-creation when
the swapchain is out of date). glslang as phase 0 decided, for
`host_vk_shader_compile`; the SPIR-V and pipeline caches in the data
folder (`host_vk_shaders.c` already keeps them as files); RADV's own
shader cache pointed at the data folder too (it writes
`/app0/radv-shader-cache` by default, the title's folder, which a
reinstall wipes; PS5_Vulkan's `mesa-pin-disk-cache` change says how it is
placed). The guest's `d3d8_vk.c` runs as it is; its two
`HALO_ANDROID` branches (the screen mode) take the desktop path.

The order, each step on the console before the next, as the Vulkan plan's
phases 2 to 6 were: the game's clears on the TV (`debug.vk_self_test`), the
menus, a map (Blood Gulch; The Silent Cartographer), render targets and the
mip composite (water), visibility tests (lens flares, against RADV's
imprecise occlusion queries), the high-res HUD and text. The validation
layer is not available on the console; the renderer's own diagnostics
(draw counts, the two-second wait reports, the self-tests) are, and RADV's
debug output through its environment.

**Ends with:** the game on the TV: the menus and the two maps as they are
under Vulkan on Android, at 60 frames a second in the menus, with the
frame times of the maps under "Progress"; what RADV's known gaps (precise
occlusion queries, line lists) change, seen and recorded.

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
| A title's `exit()` ends in `SIGSYS` | Phase 2 checks what the host's exit leaves behind |

---

## Progress

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
  follow the system's menu. The second byte was set for 0.1 s while the
  Quick Menu opened.
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
