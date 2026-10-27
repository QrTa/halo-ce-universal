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

The records are the bytes as they would go out, after an uncompressed
header through one zlib stream (format 3, zlib's fastest level: about a
fifth of their size), synced every few seconds so that a file a crash cut
short plays to its last sync. tools/replay_stats.py says how large they
are and which messages take the room; network_replay_playback.c plays them.
The folder keeps the newest network.replays_kept (the oldest are deleted
as a recording starts).
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
#include <xtl.h>

#include "zlib.h" /* source/memory/zlib */

int config_boolean(char const *name);
long config_integer(char const *name);
const char *platform_data_root(void);
FILE *platform_fopen_host(char const *path, char const *mode);
void platform_log(char const *format, ...);
void main_build_identity(long *number, char *flavor, size_t flavor_size, char *repository, size_t repository_size,
	char *version, size_t version_size);

/* the file's header (REPLAY.md, "The file"): its first bytes, and its format */
#define REPLAY_MAGIC "OCREPLAY"
#define REPLAY_FORMAT 3
#define REPLAY_HEADER_SIZE 280
/* the build's version text, at the header's end (format 0's 32 bytes cut
"OpenCE Android | local build (<commit>)" short) */
#define REPLAY_VERSION_SIZE 96
/* a record's header: its kind, a byte left 0, its size and the tick */
#define REPLAY_RECORD_HEADER_SIZE 8
/* the file written out this often (ticks): what a crash loses */
#define REPLAY_FLUSH_TICKS (5 * TICKS_PER_SECOND)
/* the file's buffer: on the Switch each write waits for the card, so
fewer, larger ones */
#define REPLAY_BUFFER_SIZE 0x10000
#define REPLAY_PATH_SIZE 512
/* a replay's file name ends so */
#define REPLAY_EXTENSION ".ocreplay"

static struct
{
	FILE *file;
	/* the game's first tick has been and network.record_replays was read
	then: a game is recorded whole or not at all, so the setting turned on
	in a game applies from the next */
	boolean considered;
	long start_time;
	long last_flush_time;
	char path[REPLAY_PATH_SIZE];
	/* what was written, for the log at its end */
	unsigned long records[NUMBER_OF_NETWORK_REPLAY_RECORDS];
	unsigned long bytes[NUMBER_OF_NETWORK_REPLAY_RECORDS];
	unsigned long total_bytes;
	char buffer[REPLAY_BUFFER_SIZE];
	/* the records' stream (format 3), and what it has made not yet written */
	z_stream stream;
	byte compressed[REPLAY_BUFFER_SIZE];
} replay;

/* through the stream into the file: flush Z_NO_FLUSH, Z_SYNC_FLUSH (all of
it written out, readable) or Z_FINISH; FALSE if the file could not take it */
static boolean replay_deflate(
	void const *data,
	unsigned long size,
	int flush)
{
	replay.stream.next_in = (Bytef *)data;
	replay.stream.avail_in = (uInt)size;
	for (;;)
	{
		int result;
		unsigned long made;

		replay.stream.next_out = replay.compressed;
		replay.stream.avail_out = sizeof(replay.compressed);
		result = deflate(&replay.stream, flush);
		made = sizeof(replay.compressed) - replay.stream.avail_out;
		if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
			return FALSE;
		if (made && fwrite(replay.compressed, made, 1, replay.file) != 1)
			return FALSE;
		/* (done: everything taken, and for a flush all of it made) */
		if (result == Z_STREAM_END || (!replay.stream.avail_in && replay.stream.avail_out))
			return TRUE;
	}
}

/* the replays past the newest `kept` deleted (0 keeps them all) */
static void replay_prune(
	long kept)
{
	static char names[256][NETWORK_REPLAY_NAME_SIZE];
	long count;
	long index;

	if (kept <= 0)
		return;
	count = network_replay_list(names, NUMBEROF(names));
	for (index = kept; index < count; index++)
	{
		if (network_replay_delete(names[index]))
			error(_error_log, "replay: %s deleted (network.replays_kept is %ld)", names[index], kept);
	}
}

boolean network_replay_delete(
	char const *name)
{
	char path[REPLAY_PATH_SIZE + NETWORK_REPLAY_NAME_SIZE];

	/* (a name of the folder's, no further) */
	if (!name || !name[0] || strchr(name, '/') || strchr(name, '\\'))
		return FALSE;
	network_replay_folder(path, REPLAY_PATH_SIZE);
	snprintf(path + strlen(path), sizeof(path) - strlen(path), "/%s", name);
	return DeleteFileA(path) != 0;
}

static unsigned long replay_get_long(
	byte const *source)
{
	return (unsigned long)source[0] | (unsigned long)source[1] << 8 | (unsigned long)source[2] << 16 |
		(unsigned long)source[3] << 24;
}

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
	char flavor[32], repository[64], version[REPLAY_VERSION_SIZE];
	char when[32] = "unknown";
	char const *base_name;
	long number = 0;
	time_t now = time(NULL);
	struct tm *local = localtime(&now);
	short index;

	if (!game)
		return FALSE;
	/* (room for this one: as many as network.replays_kept, with it) */
	replay_prune(config_integer("network.replays_kept") - 1);
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
	network_replay_folder(replay.path, sizeof(replay.path));
	posix_make_directory(replay.path);
	snprintf(replay.path + strlen(replay.path), sizeof(replay.path) - strlen(replay.path), "/%s_%s%s", when,
		map_for_name[0] ? map_for_name : "map", REPLAY_EXTENSION);
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
	replay_printable((char *)header + 184, REPLAY_VERSION_SIZE, version, FALSE);
	csmemset(&replay.stream, 0, sizeof(replay.stream));
	if (fwrite(header, sizeof(header), 1, replay.file) != 1 || deflateInit(&replay.stream, 1) != Z_OK)
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
	/* (format 2: the game itself first, which a playback loads) */
	network_replay_record(_network_replay_game, game, sizeof(*game));
	if (!replay.file)
		return FALSE;
	error(_error_log, "replay: recording %s (%s) to %s", map, variant, replay.path);
	return TRUE;
}

void network_replay_folder(
	char *path,
	long size)
{
	snprintf(path, (size_t)size, "%s/replays", platform_data_root());
}

void network_replay_summary(
	long *count,
	unsigned long long *bytes)
{
	char folder[REPLAY_PATH_SIZE];
	char name[256];
	void *directory;

	*count = 0;
	*bytes = 0;
	network_replay_folder(folder, sizeof(folder));
	directory = posix_directory_open(folder);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		size_t length = strlen(name);
		char path[REPLAY_PATH_SIZE + 256];
		struct posix_file_information information;

		if (length <= strlen(REPLAY_EXTENSION) ||
			strcmp(name + length - strlen(REPLAY_EXTENSION), REPLAY_EXTENSION))
		{
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (posix_stat(path, &information) != 0 || (information.flags & _posix_file_is_directory))
			continue;
		(*count)++;
		*bytes += ((unsigned long long)information.size_high << 32) | information.size_low;
	}
	if (directory)
		posix_directory_close(directory);
}

long network_replay_list(
	char (*names)[NETWORK_REPLAY_NAME_SIZE],
	long maximum)
{
	char folder[REPLAY_PATH_SIZE];
	char name[256];
	void *directory;
	long count = 0;

	network_replay_folder(folder, sizeof(folder));
	directory = posix_directory_open(folder);
	while (directory && posix_directory_next(directory, name, sizeof(name)))
	{
		size_t length = strlen(name);
		long place;

		if (length >= NETWORK_REPLAY_NAME_SIZE || length <= strlen(REPLAY_EXTENSION) ||
			strcmp(name + length - strlen(REPLAY_EXTENSION), REPLAY_EXTENSION))
		{
			continue;
		}
		/* (in order, newest first: an insertion, the folder's few) */
		for (place = count; place > 0 && strcmp(names[place - 1], name) < 0; place--)
		{
			if (place < maximum)
				strcpy(names[place], names[place - 1]);
		}
		if (place < maximum)
		{
			strcpy(names[place], name);
			if (count < maximum)
				count++;
		}
	}
	if (directory)
		posix_directory_close(directory);
	return count;
}

boolean network_replay_describe(
	char const *name,
	char *text,
	long size)
{
	char path[REPLAY_PATH_SIZE + NETWORK_REPLAY_NAME_SIZE];
	byte header[REPLAY_HEADER_SIZE];
	struct posix_file_information information;
	unsigned long long bytes = 0;
	unsigned long format, network_version, header_size;
	char map[0x80], variant[16];
	char const *base_name;
	FILE *file;
	size_t read;

	network_replay_folder(path, sizeof(path));
	snprintf(path + strlen(path), sizeof(path) - strlen(path), "/%s", name);
	if (posix_stat(path, &information) == 0)
		bytes = ((unsigned long long)information.size_high << 32) | information.size_low;
	file = platform_fopen_host(path, "rb");
	read = file ? fread(header, 1, sizeof(header), file) : 0;
	if (file)
		fclose(file);
	if (read < 216 || memcmp(header, REPLAY_MAGIC, 8))
	{
		snprintf(text, (size_t)size, "%s\nNot a replay.", name);
		return FALSE;
	}
	format = replay_get_long(header + 8);
	network_version = replay_get_long(header + 12);
	header_size = replay_get_long(header + 28);
	csmemcpy(map, header + 40, sizeof(map));
	map[sizeof(map) - 1] = 0;
	csmemcpy(variant, header + 168, sizeof(variant));
	variant[sizeof(variant) - 1] = 0;
	base_name = strrchr(map, '\\');
	base_name = base_name ? base_name + 1 : map;
	/* (when: the name's first fifteen characters, YYYYMMDD-HHMMSS) */
	snprintf(text, (size_t)size, "%s, %s\n%.4s-%.2s-%.2s %.2s:%.2s\n%llu KB", base_name, variant[0] ? variant : "?",
		name, name + 4, name + 6, name + 9, name + 11, bytes / 1024);
	if (format < 2 || header_size < 216)
	{
		snprintf(text + strlen(text), (size_t)size - strlen(text),
			"\nRecorded before replays could be played back: it cannot be watched.");
		return FALSE;
	}
	if (network_version != HALO_PORT_NETWORK_VERSION)
	{
		snprintf(text + strlen(text), (size_t)size - strlen(text),
			"\nRecorded by a build of network version %lu: this one plays %d.", network_version,
			HALO_PORT_NETWORK_VERSION);
		return FALSE;
	}
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
			if (!replay_deflate(NULL, 0, Z_SYNC_FLUSH))
			{
				error(_error_log, "replay: could not write to %s; the recording stops", replay.path);
				deflateEnd(&replay.stream);
				fclose(replay.file);
				replay.file = NULL;
				return;
			}
			fflush(replay.file);
		}
		return;
	}
	if (replay.considered)
		return;
	replay.considered = TRUE;
	if (config_boolean("network.record_replays"))
		replay_start();
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
	if (!replay_deflate(header, sizeof(header), Z_NO_FLUSH) || (size && !replay_deflate(data, (unsigned long)size, Z_NO_FLUSH)))
	{
		error(_error_log, "replay: could not write to %s; the recording stops", replay.path);
		deflateEnd(&replay.stream);
		fclose(replay.file);
		replay.file = NULL;
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
		boolean written = replay_deflate(NULL, 0, Z_FINISH);
		long file_size;

		deflateEnd(&replay.stream);
		file_size = ftell(replay.file);
		written = fclose(replay.file) == 0 && written;
		replay.file = NULL;
		error(_error_log,
			"replay: %s %s: %ld seconds, %ld KB (%lu KB before compression: %lu datagrams %lu KB, %lu reliable %lu KB, "
			"%lu to the recorder %lu KB, %lu game messages %lu KB)",
			written ? "recorded" : "could not finish", replay.path, ticks / TICKS_PER_SECOND,
			file_size / 1024, replay.total_bytes / 1024,
			replay.records[_network_replay_datagram], replay.bytes[_network_replay_datagram] / 1024,
			replay.records[_network_replay_reliable], replay.bytes[_network_replay_reliable] / 1024,
			replay.records[_network_replay_reliable_to_machine], replay.bytes[_network_replay_reliable_to_machine] / 1024,
			replay.records[_network_replay_game_message], replay.bytes[_network_replay_game_message] / 1024);
	}
	replay.considered = FALSE;
}
