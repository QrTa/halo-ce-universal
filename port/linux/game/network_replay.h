/*
NETWORK_REPLAY.H

The host's replay recording (port/linux/REPLAY.md, phase 0): what a client
that sees everything would be sent, written to a file instead.
*/

#ifndef __NETWORK_REPLAY_H
#define __NETWORK_REPLAY_H

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

#endif
