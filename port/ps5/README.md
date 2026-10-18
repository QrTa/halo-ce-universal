# PS5

`ninja ps5` builds the game for a jailbroken PlayStation 5, as a homebrew
title. This is the port's fifth phase (`PS5.md`, the plan): the game is on
the TV, drawn by the Android port's Vulkan renderer on RADV, with the pad,
its sound and the network. From a data folder holding only the game's disc
image it extracts its maps (with nothing on the screen yet: phase 6), then
plays the menus, the campaign and multiplayer at 60 frames a second, or 120
on a TV that takes it, over system link and internet play with the other
builds; the pad and the sound go through pacbrew's SDL2.

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

The run's files go to `build/ps5/logs/<time>`, a folder made at the
launch that holds that run alone: `halo.log`, `klog.txt` (the kernel log
of the run), the part of the game's `debug.txt` the run added, and its
screenshots. The follower stops at the host's last line, a crash, or as
soon as the title no longer runs (killed from the console or from another
shell). What an earlier run left on the console and no folder has goes to
a `<time>-leftover` folder first. `--minutes N` makes a long run: it
follows for N minutes, fetching the screenshots every 5 minutes, then
kills the title and fetches. `--host` names another console;
`tools/ps5_deploy.py --help` lists the actions one at a time (`upload`,
`launch`, `kill`, `status`, `follow`, `fetch`).

## Game data

The game's data folder is `/data/halo`; make it over FTP and put the game's
Xbox disc image (`.iso` or `.xiso`) in it. On the first start the game finds
no `maps` folder there and extracts it from the image (about 1.8 GB, half a
minute): the desktop game asks whether to and which image, and on the PS5
the host answers "Yes" and the first `.iso` or `.xiso` in `/data/halo`.
Nothing is drawn meanwhile; the console's notifications say what goes on:
one when the extraction starts (the image's name), one at every 10 %, and
one when the maps are in place, after which the menu comes up. With no
`maps` folder and no disc image, one notification says to put the image in
`/data/halo` over FTP and start again, and the title ends. After that the
folder holds:

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
longest time, how many took over 20 and over 33 ms (and over 10 and 17 ms
at 120 Hz), what the log's lines cost, and every minute the host's heap
and the console's free direct memory. A line costs its writer 2 to 13 ms
on the console (most of it the kernel log's), so a thread of the log's own
writes them; errors, the end and a crash report are written at once. When the game ends it lists the system calls it asked for
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
display, 3840x2160; with `display.ps5_resolution = "2160p"` in
`config.toml` it draws at 3840x2160 (read at the start; the PS5's own
setting, `port/linux/README.md`). Both hold 119.88 frames a second in a
match (the figures below). The display runs at 59.94 Hz, or at 119.88 Hz: the title declares
high-frame-rate output (`param.json`), and RADV takes the 120 Hz mode when
the console's output does (Settings, Screen and Video, Video Output, Enable
120 Hz Output, with a TV that takes it). RADV checks the switch once and
sometimes logs `59.94 Hz restored` while the display goes on at 120 Hz; the
heartbeat's frames a second say which it is. The system's menu may bring
the display back to 59.94 Hz for the rest of the run.

The renderer compiles the game's shaders on a thread of its own the first
time it meets them, below the game's priority (732 against the game's
700, in the console's range of 256, the highest, to 767), and keeps them,
with its pipelines, in `/data/halo/vk_cache`; RADV keeps its own in
`/data/halo/radv-shader-cache`. A pipeline not yet made is made by the
draw that needs it: on a first start its first sight costs a frame (a
match's first minute had about a dozen frames over 33 ms more than with
the caches). A second start compiles nothing it has met before. Removing
either folder only makes the next start compile again.

For testing, through `environment.txt` (`tools/ps5_deploy.py --env`):

- `HALO_VK_SELF_TEST=1`: the renderer's and the game's self-tests at start
  (`clears self-test: ok`, `data self-test: ... ok`);
- `HALO_VK_PRESENT_MARKER=1`: a red and a green square at the picture's top
  corners, to see which way up it is;
- `HALO_GPU_STATS=1`: the draws and what skipped them, every second;
- `HALO_PS5_SCREENSHOT=<seconds>`: the picture as it went to the TV, written
  that often to `/data/halo/screenshots` (the last 200 kept);
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
listed again (`SDL: gamepad removed`, `added`); when SDL2 says nothing of
it, the host compares the pads it lists once a second and tells the game
itself (`gamepads: ... (the poll)`). A second DualSense is a second
player: switched on, the console signs it in as a guest ("Invitado1" on a
Spanish console), no account needed, and the game opens it on the next
port, so two players play split screen from the menus. The pad rumbles through
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
In a campaign level (the loaded map is a solo one) the menu coming up also
opens the pause screen, as the Start button does: the host holds Start on
the first pad for 100 ms. In a match or the menus nothing pauses (a match
cannot), and the log says which (`overlay: the map is ratrace (type 1,
multiplayer ...): no pause`). The system sometimes sets the flag for about
6 s after the title is launched; that start-up overlay is told apart and
pauses nothing. The console's rest mode with the title running was not
tried: if it ends the title, what survives is the game's saves and the
renderer's caches as last written.

## The network

The console plays system link and internet play with the Linux, Windows,
Android, Switch and web builds of the same network version, as
`port/linux/README.md` sets them out ("System link", "Internet play"): the
system link list finds the console's games and the console finds theirs
by broadcast; it joins internet games by invite and from the server
browser, and hosts them; it downloads a Custom Edition map it lacks from
the game's players (map sharing). The settings are the desktop's
(`network.*` in `config.toml`; `network.address` and `network.broadcast`
work as there).

- **Sockets** are the console's own (FreeBSD's, in libkernel), through the
  Linux host's `posix_net.c`. A title is refused two things a socket
  normally takes: `fcntl`'s `O_NONBLOCK` and `ioctl`'s `FIONBIO` and
  `FIONREAD` answer `EACCES`, so a socket is made non-blocking with the
  console's own option (`SO_NBIO`). The game's addresses are Winsock's
  layout and the console's BSD's; the host turns each round.
- **Names** (the brokers', STUN's) are looked up with the console's resolver
  (libSceNet), since its C library refuses `getaddrinfo` to a title.
- **This machine's address** is the one its default route leaves from; the
  log's `network:` line gives it beside the console's own word on it
  (`sceNetCtlGetInfo`).
- **Internet play's brokers** are `brokers.txt`, which the title carries
  and writes to `/data/halo` (beside `config.toml`) at every start, so an
  update's list replaces it; name a list of your own in
  `network.brokers_file`.
- **UPnP** asks the router to forward internet play's port as on the
  desktop (miniupnpc). Behind a router that will not, or a second NAT, the
  console reaches hosts behind lenient NATs only: the game says so when it
  starts (`this network's NAT gives each destination its own port`).
- **Browsers** join the console's internet games through DTLS (Mbed TLS,
  with the console's random numbers); the certificate is made at the start,
  and the log gives its hash (`network: DTLS certificate made`).
- **Voice chat** plays the other players' voices; the console's own
  microphone is not used (SDL2 has no recording device here), so the
  console never talks.
- **Map sharing** downloads to `/data/halo/custom_maps` at the host's
  upload limit (`network.map_share_upload`, 1024 KB/s by default: a 500 MB
  map takes eight minutes).
- Not on a console: Discord, the `halo://` link handler and the command
  line, and a clipboard shared with other programs (the game's own holds
  only what it put there, its invite). Join internet games from the server
  browser.

`HALO_PS5_NET_TEST=1` in `environment.txt` runs the console's socket
checks before the game starts (`net test:` lines in the log): a socket,
the route and the console's address, a broadcast, the options UPnP's
search sets, the resolver, a connection to the first broker, a loopback
connection, the random numbers, the interfaces, and the ways of making a
socket non-blocking.

## What runs, and what does not yet

- Runs: the game's start-up, the first start's extraction, the menus,
  the campaign and multiplayer on the TV, paced to the display's 59.94 or
  119.88 Hz; settings, saves and the game's files in `/data/halo`; the pad,
  its rumble and the sound through SDL2; the system's menus as focus events
  (the level goes on under them); system link, internet play, the server
  browser and map sharing (above).
- Also: native 4K drawing (`display.ps5_resolution`), the first start's
  progress as notifications, split screen with a second pad, the pads'
  arrival and removal.
- Not yet: a picture during the first start's extraction (notifications
  only), the microphone (voice chat only listens), and the desktop's
  self-updater, which a console title does not have (it is updated by
  installing it again). The PS button's pause in a campaign level is in
  place but was not tried on the console.

## Figures

On the test console (13.59, 120 Hz output), in a Blood Gulch match the
console hosts with its bot (`HALO_NETWORK_TEST=host:bloodgulch`):

| | Frames a second | Frames over 17 / 20 / 33 ms in 5 s |
|---|---|---|
| 1080p, warm caches | 119.88 | 0 / 0 / 0 (longest 8.4 ms) |
| 2160p, warm caches | 119.88 | 0 / 0 / 0, one over 10 ms in 3 minutes |
| 1080p, split screen (two players) | 119.88 | 0 in 50 of 59 windows |
| 1080p, first minute with no caches | about 116 | a dozen frames over 33 ms in the minute |

The host's heap starts at 160 MB and stays there through a match (192 MB
at 2160p or in split screen). Pacbrew's SDL2 leaks a string in each call
of `SDL_GameControllerTypeForIndex`; the host asks it once per pad and
remembers the answer (it leaked 16 to 32 MB a minute before).
`port/ps5/host/host_alloc_track_ps5.c`, switched on at its top, logs every
minute which callers' heap grew.

For the deploy's tests, two files in the title's folder change a run:
`hold.txt` (`--hold`) keeps the title for 120 s after the game ends, and
`environment.txt`, `NAME=VALUE` lines (`--env NAME=VALUE`, again for each;
an upload without `--env` removes the file), gives the game settings
through its environment (`HALO_EXIT_AFTER=60` ends it after a minute; the
settings' variables are in `port/linux/README.md`).
