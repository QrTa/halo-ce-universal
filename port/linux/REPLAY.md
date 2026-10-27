# Replays

Recording a network game so that it can be watched again later, from any
player's view or a free camera. This file holds the plan and, phase by
phase, what was built. It answers "Proposal: Lightweight Network State
Replay System", which suggested recording the netcode's state, compressing
it, adding keyframes for seeking, and putting the version in a header. That
direction is right. What follows fits it to this netcode
([NETCODE.md](NETCODE.md)).

## What a replay is here

The Xbox's films replayed each tick's input, which works only because the
Xbox played in lockstep: every machine simulated the whole game from the
same inputs. The native builds no longer do (NETCODE.md, "Distributed
netcode"), and `source/saved films/saved_films.c` is empty. So a replay
records the netcode's messages instead.

A client does not just draw what the host sends. It simulates the game
itself. It drives remote players with the input the host relays, and their
shots, projectiles and effects come from that. The host's state then pulls
each object back toward the host's copy. So **playing a replay is running
the game as a client with no player of its own, its messages read from a
file instead of a socket**. Replays reuse the decoder the game already has:
`network_objects.c`, `network_actors.c`, the damage, statistics and co-op
handlers.

What the replay records is what the host would send such a client: a
**recorder**, a client machine that has no players. The host already sends
a machine whose players have never been alive every player's unit and
input every tick (NETCODE.md, "The players each client needs"), and the
objects and actors as often as they are near anyone's players. That is
what a free camera needs. It is about one client's traffic at its fullest,
not the host's traffic to all of its clients.

Where the proposal differs from this netcode:

- **Size.** The proposal's ~50 MB for a host's recording assumes the
  host keeps every client's stream. One recorder's stream is what is
  recorded, so a host's file is about one client's size.
- **Culling.** No player is ever left out of a client's updates here; far
  and unseen players are sent less often. A recording made on a client
  would be complete for its own view, only coarser for far players.
- **Quantization and deltas.** The wire format does most of it already:
  16 bits a vector part, parts at their default left out, buttons sent
  only where they changed, what a unit carries only when it changes. The
  file records the encoded messages, and compression comes later, when
  phase 0 has measured what takes the room.
- **Out-of-order packets.** The host records what it sends, before the
  transport, so nothing is lost or reordered.
- **Versions.** The messages change with nearly every network version (39
  now). A replay plays only on a build of its network version. The header
  says which, with the map and the game variant, and anything else is
  refused with a message. A Custom Edition map's replay needs that map
  (map sharing's hashes can check it).

## The plan

| Phase | What | Done when |
| --- | --- | --- |
| 0 | The host writes the recorder's messages to a file. `tools/replay_stats.py` says how large it is, raw and through zlib, and which messages take the room. | A 16-player game of bots is measured. |
| 1 | Playback, start to end. A client with no player reads a file instead of the host. A spectator camera follows any player, or flies free. | A recorded game plays to its end and looks like the game did. |
| 2 | Seeking. A keyframe every few seconds: what a machine joining the game in progress is given (objects, statistics, the game type's state). A seek backwards loads the map again, applies the keyframe before the target and runs ahead to it as fast as it can. | Seeking anywhere in a 10-minute game takes a few seconds on a PC, and is measured on the Switch. |
| 3 | The files and the menu. Compression (zlib in the game, maybe the messages split by type if phase 0 shows it pays). A Replays menu lists the files, with each one's map, game type, players and length, and plays one. Old network versions are refused with a message. | A replay is recorded, found and watched from the menus on every platform. |
| 4 | Further, if wanted: a client's recording of its own view, sharing replays, co-op levels' scripts and cutscenes in playback, voice. | |

## Phase 0: recording

`network.record_replays = true` (`HALO_NET_RECORD_REPLAYS=1`) makes a
host record each network game it hosts, a player's or a dedicated
server's, to `replays/` in the data folder. It is off by default: nothing
plays the files yet. The overlay's System folder has it (Record Replays).
It is read at a game's first tick, so a game is recorded whole or not at
all: turned on during a game, it records from the next.

### What is recorded

`port/linux/game/network_replay.c` opens the file at the host's first tick
of a game (`network_distributed_tick`), and closes it when the map is
unloaded (`game_dispose_from_old_map`) or a new game starts.

The recorder is a machine slot that no client has, joined or still
joining, and no player is at, a bot's either (the bots take slots from 65
up, `bots.h`). It is the highest such slot, found again each tick
(`distributed_recorder_find` in `network_distributed.c`), so a client or a
bot taking it moves it. It has no players, so:

- `distributed_host_plan_players` sends it every player's unit and input,
  every tick;
- the moving objects (`network_objects.c`), what units carry, and the
  actors (`network_actors.c`) go to it as often as they are near any
  player, as to a client whose players are dead;
- every damage event goes to it (`distributed_event_goes_to`).

The loops that decide what each client is sent take it through
`distributed_viewing_machines`, which is `distributed_client_machines`
with the recorder added. Everything that counts machines as players
(votes to kick or to skip a cutscene, pings, chat, voice, map sharing)
still uses `distributed_client_machines`, and never sees it.

What is written:

| Record | What |
| --- | --- |
| datagram | The recorder's batch of the tick's unreliable messages (`distributed_batch_flush`), as a client would receive it |
| reliable | A distributed message sent to every client reliably (`_distributed_to_clients_reliably`): objects created and deleted, kills, the game type's state... |
| reliable to the recorder | One sent to the recorder alone, reliably (none yet: it never loads late) |
| game message | A message of the game's own sent to every machine in the game (`network_game_server_send_message_to_all_machines`): a player joining or leaving, the game's end |

The clients are sent what they were before; the recorder adds a datagram a
tick to the host's work and nothing to the network.

### The file

Little-endian. A header of 216 bytes:

| Offset | Size | What |
| --- | --- | --- |
| 0 | 8 | `OCREPLAY` |
| 8 | 4 | The format, 0 |
| 12 | 4 | The network version (`HALO_PORT_NETWORK_VERSION`) |
| 16 | 4 | The build's number |
| 20 | 4 | Ticks a second (30) |
| 24 | 4 | The host's tick at the start |
| 28 | 4 | The header's size (216): records start there |
| 32 | 8 | When it started, seconds since 1970 |
| 40 | 128 | The map's path, as the game names it (`levels\test\bloodgulch\bloodgulch`) |
| 168 | 16 | The game variant's name |
| 184 | 32 | The build's version text |

Then records to the end of the file: a byte for the kind (1 to 4, the
table above), a byte 0, the size (16 bits), the host's tick (32 bits), then
the bytes as they would have been sent, before the transport swaps their
header. A distributed message is its 8-byte header (`struct
distributed_message_header`: the message header, the type, the count, the
tick) and its entries. A datagram is a batch (`_distributed_message_batch`),
whose messages each follow as their length (16 bits), their header past
the message header, and their entries. A game message is bit-packed by
`network_messages.c`.

The file is written through a 64 KB buffer, and flushed every five seconds:
a crash loses no more, and `tools/replay_stats.py` reads a file cut short.

### Measuring

```
python3 tools/replay_stats.py replays/20261027-201500_bloodgulch.ocreplay
```

prints the game's length; the file's size raw and through zlib at levels
1, 6 and 9, each per minute and for ten minutes; and the bytes by record
kind and by message type. `tools/test_replay_stats.py` tests it.

To record a game of bots without playing, set in `config.toml` (or the
`HALO_` variables on a desktop):

```
[network]
record_replays = true
[bots]
count = 15
[debug]
network_test = "local:bloodgulch:slayer"
```

`network_test`'s local game is hosted as a network game, so the recorder
runs; with 15 bots it is a game of 16 players. Copy the file from
`replays/` and run `tools/replay_stats.py` on it.

### Results

Not measured yet. Phase 0 builds for Android, the Switch and the PS5, and
`tools/replay_stats.py` is tested on files made the way the game writes
them, but no game has been recorded: the desktop builds, where it would be
quickest, do not build at the moment (the 64-bit Linux build's guest link
lacks the dedicated server's `posix_server_*` functions, and the Windows
build's game sources cannot find `pthread.h`), so the first recording
comes from a phone or a console.
