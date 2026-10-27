/*
NETWORK_REPLAY_PLAYBACK.C

A replay's playback, phase 1 of port/linux/REPLAY.md: the game a host
recorded (network_replay.c), watched from behind any of its players.

It plays as a client of the distributed netcode does, with no player of
its own and no host: the global client is made one joined to the game the
replay's first record holds (network_game_client_port_replay_begin), which
loads it as a client loads the game it joins; its clock starts at the
replay's first tick, and each frame the records of the ticks it has
reached are handed to what a client hands the host's messages it reads to:
the distributed netcode's (network_distributed_handle_message, the
datagrams marked unreliable and the rest reliable, as they came) and the
game's own (network_game_client_handle_message). So the replay is the game
as a client simulates it from the host's messages: the players driven by
their relayed input, corrected toward the host's states, the actors as the
host ran them, the damage, scores and the game's end as the host decided
them. The client sends nothing (network_game_client_write).

The view is a camera behind the player watched (network_replay_camera,
main.c's window), drawn where the frame draws the unit (render
interpolation), kept on this side of walls. A (or the d-pad's right)
watches the next player, X (or left) the one before, B (or Back) leaves; a
line at the bottom says who is watched. The game's end, or the replay's,
returns to the main menu a few seconds later.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "networking/network_client_message_handler.h"
#include "bungie_net/network/transport.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"
#include "camera/observer.h"
#include "network_distributed.h"
#include "network_replay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *platform_fopen_host(char const *path, char const *mode);
void network_distributed_handle_message(long machine_index, word const *message, word size);
void network_connection_port_set_last_read_unreliable(boolean unreliable);
boolean network_game_client_port_replay_begin(struct network_game const *game);
boolean network_game_client_port_in_postgame(struct network_game_client *client);
void network_game_abort(void);

/* the file (network_replay.c, REPLAY.md's "The file") */
#define REPLAY_MAGIC "OCREPLAY"
#define REPLAY_RECORD_HEADER_SIZE 8
/* the oldest format a playback reads: the first with the game's record */
#define REPLAY_PLAYABLE_FORMAT 2
/* the largest replay read (all of it, at once) */
#define REPLAY_MAXIMUM_FILE_SIZE (256 * 1024 * 1024)
/* how long the main menu waits after the replay's end, or the game's */
#define REPLAY_END_MILLISECONDS 4000

/* the camera: how far behind and above what it watches (a unit on foot, or
the vehicle it rides), its pitch, and how much of its turn toward the
watched's facing it makes a frame */
#define CAMERA_DISTANCE 3.5f
#define CAMERA_VEHICLE_DISTANCE 7.0f
#define CAMERA_HEIGHT 0.6f
#define CAMERA_VEHICLE_HEIGHT 1.5f
#define CAMERA_PITCH -0.3f
#define CAMERA_TURN 0.12f
#define CAMERA_FIELD_OF_VIEW DEGREES_TO_RADIANS(70.0f)
/* (how far from a wall the camera stays) */
#define CAMERA_WALL_MARGIN 0.2f

static struct
{
	/* the file, read whole, and where its next record is */
	byte *data;
	long size;
	long next;
	long start_tick;
	/* the replay is being set up (the old client's disposal does not stop
	it: network_replay_playback_stop) */
	boolean setting_up;
	/* the game is to start at the client's next idle; it has */
	boolean starting;
	boolean playing;
	/* when the replay (or the game) ended, for the return to the menu; 0
	before then; and whether the return has been asked for */
	unsigned long ended;
	boolean leaving;
	/* the first tick's records handed over (the host's objects all told) */
	boolean synchronized;
	/* the player watched (a datum index), NONE before one is */
	long watched;
	/* the buttons held at the last idle (each press counted once) */
	boolean held[NUMBER_OF_GAMEPAD_BUTTONS];
	/* the camera */
	boolean camera_valid;
	real yaw;
	struct observer_result camera;
	/* a record's bytes, aligned as the handlers read them */
	word message[0x10000 / sizeof(word)];
} playback = { NULL, 0, 0, 0, FALSE, FALSE, FALSE, 0, FALSE, FALSE, NONE };

static unsigned long replay_get_long(
	byte const *source)
{
	return (unsigned long)source[0] | (unsigned long)source[1] << 8 | (unsigned long)source[2] << 16 |
		(unsigned long)source[3] << 24;
}

static void playback_free(
	void)
{
	free(playback.data);
	playback.data = NULL;
	playback.size = 0;
	playback.next = 0;
	playback.starting = FALSE;
	playback.playing = FALSE;
	playback.ended = 0;
	playback.leaving = FALSE;
	playback.synchronized = FALSE;
	playback.watched = NONE;
	playback.camera_valid = FALSE;
}

boolean network_replay_watch(
	char const *path,
	char *why,
	long why_size)
{
	FILE *file;
	long size;
	unsigned long format, network_version, header_size;
	struct network_game game;

	why[0] = 0;
	playback_free();
	file = platform_fopen_host(path, "rb");
	if (!file)
	{
		snprintf(why, (size_t)why_size, "The replay cannot be opened.");
		return FALSE;
	}
	if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) <= 0 || size > REPLAY_MAXIMUM_FILE_SIZE ||
		fseek(file, 0, SEEK_SET) != 0 || !(playback.data = (byte *)malloc((size_t)size)) ||
		fread(playback.data, (size_t)size, 1, file) != 1)
	{
		fclose(file);
		playback_free();
		snprintf(why, (size_t)why_size, "The replay cannot be read.");
		return FALSE;
	}
	fclose(file);
	playback.size = size;
	format = size >= 32 ? replay_get_long(playback.data + 8) : 0;
	network_version = size >= 32 ? replay_get_long(playback.data + 12) : 0;
	header_size = size >= 32 ? replay_get_long(playback.data + 28) : 0;
	if (size < 32 || memcmp(playback.data, REPLAY_MAGIC, 8) || header_size < 32 ||
		header_size + REPLAY_RECORD_HEADER_SIZE > (unsigned long)size)
	{
		snprintf(why, (size_t)why_size, "This is not a replay.");
		playback_free();
		return FALSE;
	}
	if (format < REPLAY_PLAYABLE_FORMAT)
	{
		snprintf(why, (size_t)why_size, "This replay was recorded by a build that could not play it back.");
		playback_free();
		return FALSE;
	}
	if (network_version != HALO_PORT_NETWORK_VERSION)
	{
		snprintf(why, (size_t)why_size, "This replay is of network version %lu; this build plays %d.",
			network_version, HALO_PORT_NETWORK_VERSION);
		playback_free();
		return FALSE;
	}
	/* (its first record is the game) */
	if (playback.data[header_size] != _network_replay_game ||
		(playback.data[header_size + 2] | playback.data[header_size + 3] << 8) != sizeof(game) ||
		header_size + REPLAY_RECORD_HEADER_SIZE + sizeof(game) > (unsigned long)size)
	{
		snprintf(why, (size_t)why_size, "This replay has no game to play.");
		playback_free();
		return FALSE;
	}
	csmemcpy(&game, playback.data + header_size + REPLAY_RECORD_HEADER_SIZE, sizeof(game));
	playback.start_tick = (long)replay_get_long(playback.data + 24);
	playback.next = (long)(header_size + REPLAY_RECORD_HEADER_SIZE + sizeof(game));
	/* (as a client joins a game: none of this machine's own network game is
	left, benchmark.c's way) */
	playback.setting_up = TRUE;
	dispose_global_network_game_server();
	dispose_global_network_game_client();
	if (!create_global_network_game_client() || !network_game_client_port_replay_begin(&game))
	{
		snprintf(why, (size_t)why_size, "The replay's game cannot be set up.");
		dispose_global_network_game_client();
		playback.setting_up = FALSE;
		playback_free();
		return FALSE;
	}
	playback.setting_up = FALSE;
	playback.starting = TRUE;
	error(_error_log, "replay: playing %s (%s), from tick %ld", path, game.map.name, playback.start_tick);
	return TRUE;
}

boolean network_replay_playing(
	void)
{
	return playback.data != NULL && (playback.starting || playback.playing);
}

boolean network_replay_playback_starting(
	void)
{
	return playback.data != NULL && playback.starting;
}

void network_replay_playback_started(
	void)
{
	playback.starting = FALSE;
	playback.playing = TRUE;
	game_time_set_distributed(playback.start_tick);
	error(_error_log, "replay: the game is loaded; playing from tick %ld", playback.start_tick);
}

void network_replay_playback_stop(
	void)
{
	if (playback.setting_up)
		return;
	if (playback.data)
		error(_error_log, "replay: stopped");
	playback_free();
}

/* whether the button was pressed on any controller since the last idle */
static boolean playback_pressed(
	short button)
{
	boolean down = FALSE;
	boolean pressed;
	short controller;

	for (controller = 0; controller < MAXIMUM_GAMEPADS; controller++)
	{
		struct gamepad_state const *gamepad = input_get_gamepad_state(controller);

		down |= gamepad && gamepad->buttons[button] > 0;
	}
	pressed = down && !playback.held[button];
	playback.held[button] = down;
	return pressed;
}

/* the player after (or before) `after` that has a unit, wrapping round;
`after` itself if it is the only one, NONE if nobody has one */
static long playback_next_player(
	long after,
	boolean backwards)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long previous = NONE;
	long first = NONE;
	long last = NONE;
	boolean passed = after == NONE;

	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		if (iterator.datum_index == after)
		{
			if (backwards && previous != NONE)
				return previous;
			passed = TRUE;
			continue;
		}
		if (player->unit_index == NONE)
			continue;
		if (passed && !backwards)
			return iterator.datum_index;
		if (first == NONE)
			first = iterator.datum_index;
		previous = iterator.datum_index;
		last = iterator.datum_index;
	}
	if (backwards)
		return last != NONE ? last : after;
	return first != NONE ? first : after;
}

/* the records of the ticks the clock has reached, to the client's
handlers as it reads them from its host */
static void playback_feed(
	struct network_game_client *client)
{
	struct transport_address host;

	csmemset(&host, 0, sizeof(host));
	while (playback.next + REPLAY_RECORD_HEADER_SIZE <= playback.size)
	{
		byte const *record = playback.data + playback.next;
		byte kind = record[0];
		word size = (word)(record[2] | record[3] << 8);
		long tick = (long)replay_get_long(record + 4);

		if (playback.next + REPLAY_RECORD_HEADER_SIZE + size > playback.size)
		{
			/* (a file cut short: its end) */
			playback.next = playback.size;
			break;
		}
		if (tick > game_time_get())
			break;
		playback.next += REPLAY_RECORD_HEADER_SIZE + size;
		if (size < sizeof(message_header))
			continue;
		csmemcpy(playback.message, record + REPLAY_RECORD_HEADER_SIZE, size);
		switch (kind)
		{
		case _network_replay_datagram:
			network_connection_port_set_last_read_unreliable(TRUE);
			network_distributed_handle_message(NONE, playback.message, size);
			break;
		case _network_replay_reliable:
		case _network_replay_reliable_to_machine:
			network_connection_port_set_last_read_unreliable(FALSE);
			network_distributed_handle_message(NONE, playback.message, size);
			break;
		case _network_replay_game_message:
			network_connection_port_set_last_read_unreliable(FALSE);
			if (size == GET_MESSAGE_SIZE(playback.message[0]))
				network_game_client_handle_message(client, playback.message, (short)size, &host);
			break;
		}
	}
	network_connection_port_set_last_read_unreliable(FALSE);
}

boolean network_replay_playback_idle(
	struct network_game_client *client)
{
	unsigned long now = system_milliseconds();

	if (!playback.playing)
		return TRUE;
	playback_feed(client);
	/* (the host's objects, all of them, came at the game's first tick: as a
	client is told once the host has sent it them all, its own objects the
	host has not are let go, network_objects.c) */
	if (!playback.synchronized)
	{
		playback.synchronized = TRUE;
		network_objects_handle_synchronized();
	}
	if (playback_pressed(FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_a) |
		playback_pressed(_gamepad_binary_button_dpad_right))
	{
		playback.watched = playback_next_player(playback.watched, FALSE);
	}
	if (playback_pressed(FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_x) |
		playback_pressed(_gamepad_binary_button_dpad_left))
	{
		playback.watched = playback_next_player(playback.watched, TRUE);
	}
	if (playback.leaving)
		return TRUE;
	if (playback_pressed(FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_b) |
		playback_pressed(_gamepad_binary_button_back))
	{
		error(_error_log, "replay: left at tick %ld", game_time_get());
		playback.leaving = TRUE;
		network_game_abort();
		return TRUE;
	}
	/* (its end: the file's, or the game's) */
	if (!playback.ended && (playback.next >= playback.size ||
		network_game_client_port_in_postgame(client)))
	{
		error(_error_log, "replay: its end, at tick %ld", game_time_get());
		playback.ended = now ? now : 1;
	}
	if (playback.ended && now - playback.ended >= REPLAY_END_MILLISECONDS)
	{
		playback.leaving = TRUE;
		network_game_abort();
	}
	return TRUE;
}

/* ---------- the view */

struct observer_result const *network_replay_camera(
	void)
{
	struct player_datum *player;
	long unit_index;
	long followed;
	struct object_datum *object;
	real_point3d focus;
	real_vector3d back;
	real_vector3d forward;
	real yaw, turn, distance, height;
	struct collision_result collision;

	if (!playback.playing)
		return NULL;
	player = playback.watched != NONE ? player_try_and_get(playback.watched) : NULL;
	/* (the first player alive, before anyone is watched, or once the one
	watched has left; one who died is watched again when they spawn, the
	camera where it was until then) */
	if (!player || (player->unit_index == NONE && !playback.camera_valid))
	{
		playback.watched = playback_next_player(NONE, FALSE);
		player = playback.watched != NONE ? player_try_and_get(playback.watched) : NULL;
	}
	unit_index = player ? player->unit_index : NONE;
	if (unit_index == NONE)
		return playback.camera_valid ? &playback.camera : NULL;
	/* (a rider is watched from behind its vehicle) */
	object = object_get(unit_index);
	followed = object->object.parent_object_index != NONE ? object->object.parent_object_index : unit_index;
	object = object_get(followed);
	distance = followed == unit_index ? CAMERA_DISTANCE : CAMERA_VEHICLE_DISTANCE;
	height = followed == unit_index ? CAMERA_HEIGHT : CAMERA_VEHICLE_HEIGHT;
	object_get_origin(followed, &focus);
	render_interpolation_object_point(followed, &focus);
	focus.z += height;
	/* (eased round behind it) */
	yaw = (real)atan2(object->object.forward.j, object->object.forward.i);
	if (!playback.camera_valid)
		playback.yaw = yaw;
	turn = yaw - playback.yaw;
	while (turn > _pi)
		turn -= 2.0f * _pi;
	while (turn < -_pi)
		turn += 2.0f * _pi;
	playback.yaw += turn * CAMERA_TURN;
	forward.i = (real)(cos(playback.yaw) * cos(CAMERA_PITCH));
	forward.j = (real)(sin(playback.yaw) * cos(CAMERA_PITCH));
	forward.k = (real)sin(CAMERA_PITCH);
	back.i = -forward.i * distance;
	back.j = -forward.j * distance;
	back.k = -forward.k * distance;
	/* (no further than the first wall behind it) */
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&focus, &back, NONE, &collision))
	{
		real kept = collision.t * distance > CAMERA_WALL_MARGIN ?
			(collision.t * distance - CAMERA_WALL_MARGIN) / distance : 0.0f;

		back.i *= kept;
		back.j *= kept;
		back.k *= kept;
	}
	csmemset(&playback.camera, 0, sizeof(playback.camera));
	playback.camera.position.x = focus.x + back.i;
	playback.camera.position.y = focus.y + back.j;
	playback.camera.position.z = focus.z + back.k;
	scenario_location_from_point(&playback.camera.location, &playback.camera.position);
	playback.camera.forward = forward;
	playback.camera.up.i = (real)(-cos(playback.yaw) * sin(CAMERA_PITCH));
	playback.camera.up.j = (real)(-sin(playback.yaw) * sin(CAMERA_PITCH));
	playback.camera.up.k = (real)cos(CAMERA_PITCH);
	playback.camera.field_of_view = CAMERA_FIELD_OF_VIEW;
	playback.camera_valid = TRUE;
	return &playback.camera;
}

/* the line at the bottom: who is watched, and the buttons */
void network_replay_playback_draw(
	void)
{
	long font_index;
	struct font_header *font;
	real_argb_color color;
	rectangle2d bounds;
	short height;
	wchar_t text[128];
	struct player_datum *player;

	if (!playback.playing)
		return;
	font_index = hud_get_font_index();
	if (font_index == NONE)
		return;
	player = playback.watched != NONE ? player_try_and_get(playback.watched) : NULL;
	if (playback.ended)
		usnprintf(text, NUMBEROF(text) - 1, L"REPLAY OVER");
	else if (player)
		usnprintf(text, NUMBEROF(text) - 1, L"REPLAY: %.12s\r\nA: NEXT   X: PREVIOUS   B: LEAVE", player->name);
	else
		usnprintf(text, NUMBEROF(text) - 1, L"REPLAY\r\nB: LEAVE");
	text[NUMBEROF(text) - 1] = 0;
	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	color = *global_real_argb_white;
	bounds = render.camera.window_bounds;
	bounds.y0 = (short)(bounds.y1 - 3 * height - 24);
	bounds.y1 = (short)(bounds.y1 - 24);
	/* (draw mode 2: centred) */
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}
