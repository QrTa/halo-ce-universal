/*
NETWORK_REPLAY.C

The host's replay recording, phase 0 of port/linux/REPLAY.md: with
network.record_replays on, the host of a network game (a player's or a
dedicated server) writes what a client that sees everything would be sent
to a file in the data folder's replays/, from the game's first tick until
its map is unloaded.

That client is the recorder: a machine slot no client and no bot has
(network_distributed.c's distributed_recorder_machine), with no players, so
the distributed netcode sends it every player every tick and the objects
and actors as often as they are near anyone's players, as it does a client
whose players have never been alive. Its datagrams are written here instead
of sent; so are the distributed messages sent every client reliably, and
the game's own messages to every machine (a player joining or leaving, the
game's end). Nothing else changes: the clients are sent what they were.

Phase 0 is for measuring: the records are the bytes as they would go out,
uncompressed, and tools/replay_stats.py says how large they are, zlibbed and
not, and which messages take the room. Nothing plays them yet (phase 1).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
#include "game/players.h"
#include "network_distributed.h"
#include "network_replay.h"
#include "../src/posix.h" /* port: port/linux/src/posix.c */

#include <stdio.h>
#include <string.h>
#include <time.h>

int config_boolean(char const *name);
const char *platform_data_root(void);
FILE *platform_fopen_host(char const *path, char const *mode);
void platform_log(char const *format, ...);
void main_build_identity(long *number, char *flavor, size_t flavor_size, char *repository, size_t repository_size,
	char *version, size_t version_size);

/* the file's header (REPLAY.md, "The file"): its first bytes, and its format */
#define REPLAY_MAGIC "OCREPLAY"
#define REPLAY_FORMAT 0
#define REPLAY_HEADER_SIZE 216
/* a record's header: its kind, a byte left 0, its size and the tick */
#define REPLAY_RECORD_HEADER_SIZE 8
/* the file written out this often (ticks): what a crash loses */
#define REPLAY_FLUSH_TICKS (5 * TICKS_PER_SECOND)
/* the file's buffer: on the Switch each write waits for the card, so
fewer, larger ones */
#define REPLAY_BUFFER_SIZE 0x10000
#define REPLAY_PATH_SIZE 512

static struct
{
	FILE *file;
	/* a recording was asked for this game and could not be made: not tried
	again until the next */
	boolean failed;
	long start_time;
	long last_flush_time;
	char path[REPLAY_PATH_SIZE];
	/* what was written, for the log at its end */
	unsigned long records[NUMBER_OF_NETWORK_REPLAY_RECORDS];
	unsigned long bytes[NUMBER_OF_NETWORK_REPLAY_RECORDS];
	unsigned long total_bytes;
	char buffer[REPLAY_BUFFER_SIZE];
} replay;

static void replay_put_long(
	byte *destination,
	unsigned long value)
{
	destination[0] = (byte)value;
	destination[1] = (byte)(value >> 8);
	destination[2] = (byte)(value >> 16);
	destination[3] = (byte)(value >> 24);
}

/* the text, in printable ASCII, a file name's letters only (others '_') when
for_name */
static void replay_printable(
	char *destination,
	long size,
	char const *source,
	boolean for_name)
{
	long index;

	for (index = 0; index < size - 1 && source[index]; index++)
	{
		char character = source[index];
		boolean kept = for_name ?
			(character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				(character >= '0' && character <= '9') || character == '-' :
			character >= ' ' && character < 127;

		destination[index] = kept ? character : '_';
	}
	destination[index] = 0;
}

static boolean replay_start(
	void)
{
	struct network_game_server *server = global_network_game_server_get();
	struct network_game *game = server ? network_game_server_get_game(server) : NULL;
	byte header[REPLAY_HEADER_SIZE];
	char map[0x80] = "";
	char map_for_name[48];
	char variant[16] = "";
	char flavor[32], repository[64], version[32];
	char when[32] = "unknown";
	char const *base_name;
	long number = 0;
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	short index;

	if (!game)
		return FALSE;
	csmemset(header, 0, sizeof(header));
	replay_printable(map, sizeof(map), game->map.name, FALSE);
	for (index = 0; index < 11 && game->variant.human_readable_game_description[index]; index++)
		variant[index] = player_name_character_ascii(game->variant.human_readable_game_description[index]);
	variant[index] = 0;
	main_build_identity(&number, flavor, sizeof(flavor), repository, sizeof(repository), version, sizeof(version));
	/* (the map's name past its folders, as levels\test\bloodgulch\bloodgulch) */
	base_name = strrchr(map, '\\');
	base_name = base_name ? base_name + 1 : map;
	replay_printable(map_for_name, sizeof(map_for_name), base_name, TRUE);
	if (local)
		strftime(when, sizeof(when), "%Y%m%d-%H%M%S", local);
	snprintf(replay.path, sizeof(replay.path), "%s/replays", platform_data_root());
	posix_make_directory(replay.path);
	snprintf(replay.path, sizeof(replay.path), "%s/replays/%s_%s.ocreplay", platform_data_root(), when,
		map_for_name[0] ? map_for_name : "map");
	replay.file = platform_fopen_host(replay.path, "wb");
	if (!replay.file)
	{
		error(_error_log, "replay: could not open %s to record the game", replay.path);
		return FALSE;
	}
	setvbuf(replay.file, replay.buffer, _IOFBF, sizeof(replay.buffer));
	csmemcpy(header, REPLAY_MAGIC, 8);
	replay_put_long(header + 8, REPLAY_FORMAT);
	replay_put_long(header + 12, HALO_PORT_NETWORK_VERSION);
	replay_put_long(header + 16, (unsigned long)number);
	replay_put_long(header + 20, TICKS_PER_SECOND);
	replay_put_long(header + 24, (unsigned long)game_time_get());
	replay_put_long(header + 28, REPLAY_HEADER_SIZE);
	replay_put_long(header + 32, (unsigned long)now);
	replay_put_long(header + 36, (unsigned long)((unsigned long long)now >> 32));
	csmemcpy(header + 40, map, MIN(sizeof(map), 128));
	csmemcpy(header + 168, variant, sizeof(variant));
	replay_printable((char *)header + 184, 32, version, FALSE);
	if (fwrite(header, sizeof(header), 1, replay.file) != 1)
	{
		error(_error_log, "replay: could not write to %s", replay.path);
		fclose(replay.file);
		replay.file = NULL;
		return FALSE;
	}
	replay.start_time = game_time_get();
	replay.last_flush_time = replay.start_time;
	csmemset(replay.records, 0, sizeof(replay.records));
	csmemset(replay.bytes, 0, sizeof(replay.bytes));
	replay.total_bytes = REPLAY_HEADER_SIZE;
	error(_error_log, "replay: recording %s (%s) to %s", map, variant, replay.path);
	return TRUE;
}

boolean network_replay_recording(
	void)
{
	return replay.file != NULL;
}

void network_replay_host_tick(
	void)
{
	if (replay.file)
	{
		if (game_time_get() - replay.last_flush_time >= REPLAY_FLUSH_TICKS)
		{
			replay.last_flush_time = game_time_get();
			fflush(replay.file);
		}
		return;
	}
	if (replay.failed || !config_boolean("network.record_replays"))
		return;
	replay.failed = !replay_start();
}

void network_replay_record(
	byte kind,
	void const *data,
	long size)
{
	byte header[REPLAY_RECORD_HEADER_SIZE];

	if (!replay.file || size < 0 || size > 0xFFFF || kind <= 0 || kind >= NUMBER_OF_NETWORK_REPLAY_RECORDS)
		return;
	header[0] = kind;
	header[1] = 0;
	header[2] = (byte)size;
	header[3] = (byte)(size >> 8);
	replay_put_long(header + 4, (unsigned long)game_time_get());
	if (fwrite(header, sizeof(header), 1, replay.file) != 1 || (size && fwrite(data, (size_t)size, 1, replay.file) != 1))
	{
		error(_error_log, "replay: could not write to %s; the recording stops", replay.path);
		fclose(replay.file);
		replay.file = NULL;
		replay.failed = TRUE;
		return;
	}
	replay.records[kind]++;
	replay.bytes[kind] += (unsigned long)size;
	replay.total_bytes += REPLAY_RECORD_HEADER_SIZE + (unsigned long)size;
}

void network_replay_stop(
	void)
{
	if (replay.file)
	{
		long ticks = game_time_get() - replay.start_time;
		boolean written = fclose(replay.file) == 0;

		replay.file = NULL;
		error(_error_log,
			"replay: %s %s: %ld seconds, %lu KB (%lu datagrams %lu KB, %lu reliable %lu KB, %lu to the recorder %lu KB, "
			"%lu game messages %lu KB)",
			written ? "recorded" : "could not finish", replay.path, ticks / TICKS_PER_SECOND,
			replay.total_bytes / 1024,
			replay.records[_network_replay_datagram], replay.bytes[_network_replay_datagram] / 1024,
			replay.records[_network_replay_reliable], replay.bytes[_network_replay_reliable] / 1024,
			replay.records[_network_replay_reliable_to_machine], replay.bytes[_network_replay_reliable_to_machine] / 1024,
			replay.records[_network_replay_game_message], replay.bytes[_network_replay_game_message] / 1024);
	}
	replay.failed = FALSE;
}
