/*
NETWORK_REPLAY.H

The host's replay recording (port/linux/REPLAY.md, phase 0): what a client
that sees everything would be sent, written to a file instead.
*/

#ifndef __NETWORK_REPLAY_H
#define __NETWORK_REPLAY_H

struct observer_result;

/* a record's kind (REPLAY.md, "The file") */
enum
{
	/* the recorder's datagram: a batch of the distributed netcode's
	unreliable messages, as a client is sent one */
	_network_replay_datagram = 1,
	/* a distributed message the host sent every client reliably */
	_network_replay_reliable,
	/* ... or the recorder's machine alone, reliably */
	_network_replay_reliable_to_machine,
	/* a message of the game's own (network_server_message_handler.c) the
	host sent every machine in the game */
	_network_replay_game_message,
	/* (format 2, the first record) the game as the host had it at its first
	tick, struct network_game: its map, game type, players and random seed,
	which a playback loads the game from (network_replay_playback.c) */
	_network_replay_game,

	NUMBER_OF_NETWORK_REPLAY_RECORDS
};

/* (the host) whether a replay is being recorded */
boolean network_replay_recording(void);

/* (the host) once a tick, before anything is sent: starts the recording at
the game's first tick when network.record_replays is on */
void network_replay_host_tick(void);

/* (the host) a record of the kind, at this tick, when recording */
void network_replay_record(byte kind, void const *data, long size);

/* the recording's end: the map is unloaded (or a new game starts) */
void network_replay_stop(void);

/* the folder replays are written to (the data folder's replays/), into
path */
void network_replay_folder(char *path, long size);

/* the replays in that folder: how many, and their bytes (the Theater
screen, menu_functions.c) */
void network_replay_summary(long *count, unsigned long long *bytes);

/* the replays in the folder, newest first (their names begin with when
they were recorded), at most `maximum`, each name in names[index] (up to
NETWORK_REPLAY_NAME_SIZE bytes); how many */
#define NETWORK_REPLAY_NAME_SIZE 96
long network_replay_list(char (*names)[NETWORK_REPLAY_NAME_SIZE], long maximum);

/* what the replay of that name in the folder is, in lines of text: its
map, game type, when, its size, and whether this build can play it (its
return) */
boolean network_replay_describe(char const *name, char *text, long size);

/* the replay of that name in the folder deleted; FALSE if it could not be */
boolean network_replay_delete(char const *name);

/* ---------- the replay page (network_replay_page.c): the Switch's and the
PS5's, on the local network while the Theater is up */

/* the Theater's, each frame: the page while it is up */
void network_replay_page_wanted(void);
/* main_loop's, each frame */
void network_replay_page_update(void);
/* the page's address ("http://192.168.2.173:8090/"), "" while none is served */
char const *network_replay_page_address(void);

/* ---------- playback (network_replay_playback.c) */

/* plays the replay at path (its game is loaded at the next frame); FALSE,
with why in why, for one that cannot be played (not a replay, an older
format, another network version) */
boolean network_replay_watch(char const *path, char *why, long why_size);

/* whether a replay is being played (or its game loaded) */
boolean network_replay_playing(void);

/* the playback's view made for this frame (main.c, before the sound and
the windows: each asks network_replay_camera for it) */
void network_replay_playback_frame(void);

/* the view, for the frame's first window (main.c) and the sound's listener
(sound_manager.c); NULL when no replay plays, or before it has one */
struct observer_result const *network_replay_camera(void);

/* the player watched in a replay's playback (a datum index), NONE when no
replay plays: the announcer speaks to them (game_engine.c), and the view
through their eyes draws them as a player's own (render_objects.c) */
long network_replay_watched_player(void);

/* whether the view is through the watched player's eyes */
boolean network_replay_viewing_eyes(void);

/* the line at the bottom of the screen: who is watched, and the buttons
(interface.c) */
void network_replay_playback_draw(void);

#endif
