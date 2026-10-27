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
all: turned on during a game, it records from the next. The main menu's
Theater has it too (RECORD REPLAYS), says how many replays there are and
where, and on Android copies them out of the app's folder (EXPORT
REPLAYS: the system's folder picker, `HaloActivity.exportReplays`).

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
- every damage event goes to it (`distributed_event_goes_to`);
- an object at rest goes to it only when its state differs from the last
  it was sent. A client is sent each object at rest again and again (as it
  comes to rest, and four a tick round them all), in case a datagram was
  lost; the recorder loses none. `network_objects.c` keeps a checksum of
  each object's last state sent to the recorder (4 bytes an object), not
  the state itself (44 bytes, for up to 8192 objects).

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

Little-endian. A header of 280 bytes (formats 1 and 2; format 0's was
216, its version text 32 bytes, too few for `OpenCE Android | local build
(<commit>)`):

| Offset | Size | What |
| --- | --- | --- |
| 0 | 8 | `OCREPLAY` |
| 8 | 4 | The format, 2 (1 had no game record; 0 a 216-byte header) |
| 12 | 4 | The network version (`HALO_PORT_NETWORK_VERSION`) |
| 16 | 4 | The build's number |
| 20 | 4 | Ticks a second (30) |
| 24 | 4 | The host's tick at the start |
| 28 | 4 | The header's size (280): records start there |
| 32 | 8 | When it started, seconds since 1970 |
| 40 | 128 | The map's path, as the game names it (`levels\test\bloodgulch\bloodgulch`) |
| 168 | 16 | The game variant's name |
| 184 | 96 | The build's version text (to the header's end) |

Then records to the end of the file. From format 2 the first is the
game (kind 5): `struct network_game` as the host had it at its first
tick, its map, game type, players, random seed and PC options, which a
playback loads the game from. Each record is a byte for the kind (1 to 5,
the table above and the game), a byte 0, the size (16 bits), the host's tick (32 bits), then
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

The first recording, from Android (format 0, before objects at rest were
left out): a Slayer game on Beaver Creek, three players (a player and two
bots added at ticks 145 and 171), 50 seconds. Every one of its 1,512 ticks
has its datagram, and the file was closed when the map was unloaded.

| | 50 seconds | A minute | 10 minutes |
| --- | --- | --- | --- |
| Raw | 594 KB | 707 KB | 6.9 MB |
| zlib, level 1 | 135 KB | 161 KB | 1.6 MB |
| zlib, level 9 | 121 KB | 144 KB | 1.4 MB |

zlib takes it to a fifth, and its fastest level is nearly as good as its
best: compressing in the game (phase 3) can be cheap.

| Message | Share | |
| --- | --- | --- |
| object_states | 50% | about four a tick, 44 bytes each: most were objects at rest, sent again in their turn (now left out) |
| unit_states | 26% | the players: with their input, about 58 bytes a player a tick |
| relayed_actions | 12% | the players' input |
| the rest | 12% | the batches' and records' headers, inventories, objects made and deleted, statistics, the game type's state, damage |

The second, from Android (format 1, objects at rest left out unless they
change): Slayer on Rat Race, three players again, 50 seconds, every one of
its 1,502 ticks there.

| | Beaver Creek (format 0) | Rat Race (format 1) |
| --- | --- | --- |
| Raw | 594 KB (707 KB a minute) | 341 KB (409 KB a minute) |
| zlib, level 1 | 135 KB | 122 KB |
| zlib, level 9 | 121 KB | 110 KB |
| object_states | 295 KB, 50% | 46 KB, 14% |
| Ticks with no object state | none | 3 in 4 |

Leaving the objects at rest out took the raw file down by 43%, but the
zlibbed one by 9% only: zlib had found most of those repeats itself. The
players are now most of the file (unit_states 43%, relayed_actions 20%),
the same 58 bytes a player a tick as before.

What grows with the players is about 58 bytes a player a tick: 16 players
would be about 17 MB raw in ten minutes for them alone, about 3.5 MB
zlibbed, beside what the objects take. Getting a 16-player game well under
that needs what is particular to these messages (phase 3): states as their
difference from the last one of the same object or player, and the
messages split by type before they are compressed.

## Phase 1: playback

The Theater's WATCH plays the replay chosen with left and right (the
newest first; the panel says what it is, or why it cannot be played: a
replay from before format 2, or of another network version).
`port/linux/game/network_replay_playback.c`:

- **As a client with no player and no host.** The file is read whole.
  The global client is made one joined to the game of its first record
  (`network_game_client_port_replay_begin`, `network_client_manager.c`),
  with no machine of its own, and its next idle loads that game as a
  client loads the game it joins (`network_game_client_game_has_started`).
  Its clock starts at the replay's first tick, released at once: no host
  sends the first game update it waits for.
- **The records, at their ticks.** Each frame, before its ticks, the
  records of the ticks the clock has reached go where a client's reads
  of its host go: the datagrams and the reliable messages to
  `network_distributed_handle_message`, marked as having come unreliably
  or not (`network_connection_port_set_last_read_unreliable`), and the
  game's own to `network_game_client_handle_message`. Once the first
  tick's are handed over (the host's objects, all of them) the client is
  synchronized, as a client the host has told them all is. The client
  sends nothing: `network_game_client_write` drops it, and the in-game
  idle asks for no connection.
- **The view.** The game has no local player, so the frame's first
  window takes the playback's camera (`main.c`): behind the player
  watched (or the vehicle they ride), where the frame draws them (render
  interpolation), turning round behind them, kept this side of walls. A
  (or right) watches the next player alive, X (or left) the one before;
  one who dies is watched again when they spawn. A line at the bottom
  says who (`interface_draw_fullscreen_overlays`).
- **The end.** B (or Back) leaves; the replay's end, or the game's,
  returns to the main menu four seconds later (`network_game_abort`).

Not yet: sound follows no listener (none is local), there is no free
camera or seeking (phase 2), and co-op levels' scripts have not been
tried.
