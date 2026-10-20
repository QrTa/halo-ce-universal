/*
CAMPAIGN_SCORE.C

The campaign's score (game.campaign_score, Settings > Gameplay's CAMPAIGN
SCORING, off by default: the stock game has none). Each enemy a player kills
is worth points by its actor variant: a base by the kind of actor (a grunt, a
jackal, an elite, a hunter, the Flood's forms, a sentinel) times a rank by the
variant's name (minor 1, major 1.5, ultra 2, zealot 3, and so on) times the
difficulty (easy 0.5, normal 1, heroic 2, legendary 5), rounded up to a
multiple of 5. A ghost, a banshee and a wraith destroyed are worth 50, 75 and
100 points, scaled by the difficulty the same way. Marines, crew and the like
are worth nothing, so friendly fire gains no points.

Only the machine that runs the AI knows the kills: the host of a co-op
game, or a single player. The host sends each kill, and the level's win, to
the clients of a network co-op game (network_coop.c), which keep the same
log. A co-op game's score is the players' added together (network.coop_score_type
"cooperative") or each player's own ("competitive"); the host's choice counts.

The score is a log of kills, each with the game time it happened at and its
player's absolute index (the same on every machine of a co-op game). Going
back to a checkpoint puts the game time back, and the kills after it are
forgotten with it; the totals are summed from the log.

On the HUD, the score and the level's time are a panel at the bottom right,
as the Master Chief Collection's (campaign_score_draw_view); the skulls' icons
are drawn above it (skulls.c, campaign_score_hud_height). The scoreboard
(hold BACK, or tab: game_engine.c) lists every player's score.

When a level is won (main_won_map) its totals, each player's score and kills
and its best variants are written to the log, and shown over the level until
A or B is pressed, before the main menu (or the co-op lobby) loads.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "ai/actors.h"
#include "ai/actor_definitions.h"
#include "ai/actor_types.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "interface/interface.h"
#include "interface/hud_messaging.h"
#include "main/console.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"

#include "campaign_score.h"
#include "custom_edition_maps.h"

#include <stdio.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the kills one level logs (a level places a few hundred enemies) */
	CAMPAIGN_SCORE_MAXIMUM_KILLS = 8192,
	/* the players a score is kept for (by absolute index) */
	CAMPAIGN_SCORE_MAXIMUM_PLAYERS = 16,
	/* the kinds of variant the summary tells apart */
	CAMPAIGN_SCORE_MAXIMUM_VARIANTS = 64,
	/* the lines of the summary, and their length */
	CAMPAIGN_SCORE_SUMMARY_LINES = 14,
	CAMPAIGN_SCORE_SUMMARY_LENGTH = 56,
	/* draw_string.c's justifications (its enum is its own) */
	_campaign_score_text_left = 0,
	_campaign_score_text_right = 1,
	_campaign_score_text_center = 2,
};

/* the HUD's panel, in the HUD's 480 lines (scaled with the view as the
skulls' icons are): its corner's margins (the skulls'), its width, its
strip of segments on top, and each row's padding */
enum
{
	CAMPAIGN_SCORE_PANEL_MARGIN = 12,
	CAMPAIGN_SCORE_PANEL_WIDTH = 216,
	CAMPAIGN_SCORE_STRIP_HEIGHT = 7,
	CAMPAIGN_SCORE_STRIP_SEGMENTS = 24,
	CAMPAIGN_SCORE_STRIP_GAP = 2,
	CAMPAIGN_SCORE_ROW_GAP = 2,
	CAMPAIGN_SCORE_ROW_PADDING = 3,
	/* (the shortest a row is) */
	CAMPAIGN_SCORE_ROW_MINIMUM = 22,
	/* (the skulls' icons this far above the panel) */
	CAMPAIGN_SCORE_SKULLS_GAP = 4,
};

/* the panel's colours, the Master Chief Collection's blue (ARGB) */
#define CAMPAIGN_SCORE_PANEL_FILL 0xA00A2246
#define CAMPAIGN_SCORE_PANEL_EDGE 0xE04F9BE0
#define CAMPAIGN_SCORE_STRIP_FILL 0xC06EB4F0
/* the subline's text, smaller than the row's */
#define CAMPAIGN_SCORE_SUBLINE_SIZE 0.72f

/* the summary's colour: a player's name in the chat (network_chat.c's
chat_color) */
#define CAMPAIGN_SCORE_BLUE_RED 0.34f
#define CAMPAIGN_SCORE_BLUE_GREEN 0.51f
#define CAMPAIGN_SCORE_BLUE_BLUE 0.86f

/* ---------- structures */

struct campaign_score_kill
{
	long game_time;
	long variant_index;
	short player_index;
	short points;
};

struct campaign_score_variant
{
	long variant_index;
	long kills;
	long points;
};

/* ---------- prototypes */

/* port_config.c's, cinematics.c's / cseries_windows.c's, tag_files.c's */
int config_boolean(const char *name);
unsigned long config_changes(void);
/* input_abstraction.c: how many ticks the keyboard's jump key (A) has been held */
byte input_abstraction_port_accept(short controller_index);
char *tag_get_name(long tag_index);
/* sdl_platform.c's: the log (debug.txt) only, not the console on screen */
void platform_log(char const *format, ...);
/* network_coop.c's: co-op is a network game on a campaign level; the host's
kills and its win are sent to the clients (campaign_score_network_kill, ...) */
boolean network_coop_active(void);
void network_coop_note_score_kill(short player_index, long variant_index, short points, boolean competitive);
void network_coop_note_score_won(boolean competitive);
/* port_config.c's */
char const *config_string(char const *name);
/* cinematics.c's */
void draw_quad(rectangle2d *rectangle, pixel32 color);

/* ---------- globals */

static struct campaign_score_kill campaign_score_kills[CAMPAIGN_SCORE_MAXIMUM_KILLS];
static long campaign_score_kill_count = 0;
static boolean campaign_score_full_logged = FALSE;
/* (the last game time seen: a smaller one is a checkpoint reverted to) */
static long campaign_score_last_time = 0;
/* a level just won, for the next one, and the one shown now */
static char campaign_score_pending[CAMPAIGN_SCORE_SUMMARY_LINES][CAMPAIGN_SCORE_SUMMARY_LENGTH];
static short campaign_score_pending_lines = 0;
static char campaign_score_summary[CAMPAIGN_SCORE_SUMMARY_LINES][CAMPAIGN_SCORE_SUMMARY_LENGTH];
static short campaign_score_summary_lines = 0;
/* (a button held when the summary came up does not dismiss it: only a new press) */
static boolean campaign_score_button_primed = FALSE;
static boolean campaign_score_button_held = FALSE;
/* the summary is shown over the level just won, which waits for it */
static boolean campaign_score_postgame = FALSE;
/* whether each player keeps a score of their own in co-op (network.coop_score_type
"competitive"): the host's choice, which a client is told with the kills */
static boolean campaign_score_competitive = FALSE;
/* the height the panel took in the view drawn last, for the skulls' icons
above it (campaign_score_hud_height), in the HUD's 480 lines */
static short campaign_score_drawn_height = 0;

/* the difficulty's share of a point, in tenths: easy, normal, heroic, legendary */
static short const campaign_score_difficulty_tenths[NUMBER_OF_GAME_DIFFICULTY_LEVELS] = { 5, 10, 20, 50 };
static char const *const campaign_score_difficulty_names[NUMBER_OF_GAME_DIFFICULTY_LEVELS] =
	{ "EASY", "NORMAL", "HEROIC", "LEGENDARY" };

/* the rank of a variant by its name's words, tenths of a point per point
(the first that is in the name) */
static struct
{
	char const *word;
	short tenths;
} const campaign_score_ranks[] =
{
	{ "honor", 35 },
	{ "zealot", 30 },
	{ "commander", 30 },
	{ "captain", 30 },
	{ "gold", 30 },
	{ "white", 30 },
	{ "ultra", 20 },
	{ "spec", 20 },
	{ "stealth", 20 },
	{ "red", 20 },
	{ "major", 15 },
	{ "ranger", 15 },
	{ "heavy", 15 },
	{ "leader", 15 },
	{ "minor", 10 },
	{ NULL, 0 },
};

/* ---------- private code */

static boolean campaign_score_level(
	void)
{
	struct scenario *scenario;

	if (!game_in_progress() || game_engine_running())
		return FALSE;
	scenario = global_scenario_try_and_get();
	return scenario != NULL && scenario->type == _scenario_type_solo;
}

/* the difficulty, as an index of the tables above */
static short campaign_score_difficulty(
	void)
{
	short difficulty = game_difficulty_level_get();

	return PIN(difficulty, 0, NUMBER_OF_GAME_DIFFICULTY_LEVELS - 1);
}

/* the host's (or a single machine's) co-op scoring type, from its config */
static boolean campaign_score_type_competitive(
	void)
{
	static unsigned long changes = (unsigned long)-1;
	static boolean competitive = FALSE;

	if (changes != config_changes())
	{
		char const *type = config_string("network.coop_score_type");

		changes = config_changes();
		competitive = type != NULL && !strcmp(type, "competitive");
	}
	return competitive;
}

/* the points a variant is worth (0 for one that is no enemy) */
static short campaign_score_variant_points(
	long variant_definition_index)
{
	struct actor_variant_definition *variant;
	struct actor_definition *actor;
	char const *name;
	char lower[128];
	long base;
	long tenths = 10;
	long points;
	long index;

	if (variant_definition_index == NONE)
		return 0;
	variant = actor_variant_definition_get(variant_definition_index);
	if (variant->actor_reference.index == NONE)
		return 0;
	actor = actor_definition_get(variant->actor_reference.index);
	switch (actor->type)
	{
	case _actor_elite: base = 30; break;
	case _actor_jackal: base = 15; break;
	case _actor_grunt: base = 10; break;
	case _actor_hunter: base = 100; break;
	case _actor_engineer: base = 5; break;
	case _actor_assassin: base = 30; break;
	case _actor_combat_form: base = 10; break;
	case _actor_infection_form: base = 5; break;
	case _actor_carrier_form: base = 10; break;
	case _actor_sentinel: base = 15; break;
	default: base = 0; break;
	}
	if (base == 0)
		return 0;
	name = tag_get_name(variant_definition_index);
	if (name != NULL)
	{
		for (index = 0; index < (long)sizeof(lower) - 1 && name[index]; index++)
			lower[index] = (char)((name[index] >= 'A' && name[index] <= 'Z') ? name[index] + 32 : name[index]);
		lower[index] = '\0';
		for (index = 0; campaign_score_ranks[index].word; index++)
		{
			if (strstr(lower, campaign_score_ranks[index].word))
			{
				tenths = campaign_score_ranks[index].tenths;
				break;
			}
		}
	}
	points = base * tenths * campaign_score_difficulty_tenths[campaign_score_difficulty()];
	/* (up to a multiple of 5: an enemy worth any is worth at least 5) */
	points = (points + 499) / 500 * 5;
	return (short)points;
}

/* a vehicle's worth (the ghost, banshee and wraith), scaled by difficulty
like an enemy; 0 for any other vehicle */
static short campaign_score_vehicle_points(
	long definition_index)
{
	char const *name = tag_get_name(definition_index);
	char lower[128];
	long base = 0;
	long index;
	long points;

	if (name == NULL)
		return 0;
	for (index = 0; index < (long)sizeof(lower) - 1 && name[index]; index++)
		lower[index] = (char)((name[index] >= 'A' && name[index] <= 'Z') ? name[index] + 32 : name[index]);
	lower[index] = '\0';
	if (strstr(lower, "ghost"))
		base = 50;
	else if (strstr(lower, "banshee"))
		base = 75;
	else if (strstr(lower, "wraith"))
		base = 100;
	if (base == 0)
		return 0;
	points = base * 10 * campaign_score_difficulty_tenths[campaign_score_difficulty()];
	points = (points + 499) / 500 * 5;
	return (short)points;
}

/* whether A or B is down on a controller (or the keyboard's jump key, A) */
static boolean campaign_score_button_down(
	void)
{
	short controller_index;

	for (controller_index = 0; controller_index < MAXIMUM_GAMEPADS; controller_index++)
	{
		struct gamepad_state const *gamepad = input_get_gamepad_state(controller_index);

		if (gamepad &&
			(gamepad->buttons[FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_a] > 0 ||
				gamepad->buttons[FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_b] > 0))
			return TRUE;
		if (input_abstraction_port_accept(controller_index) > 0)
			return TRUE;
	}
	return FALSE;
}

/* the kills after the game time are gone: a checkpoint was reverted to */
static void campaign_score_update(
	void)
{
	long time = game_time_get();

	if (time < campaign_score_last_time)
	{
		while (campaign_score_kill_count > 0 &&
			campaign_score_kills[campaign_score_kill_count - 1].game_time > time)
			campaign_score_kill_count--;
	}
	campaign_score_last_time = time;

	return;
}

static long campaign_score_total(
	short player_index)
{
	long total = 0;
	long index;

	for (index = 0; index < campaign_score_kill_count; index++)
	{
		if (campaign_score_kills[index].player_index == player_index)
			total += campaign_score_kills[index].points;
	}
	return total;
}

/* a player's name, in plain letters (PLAYER 1 for one that has left) */
static void campaign_score_player_name(
	short player_index,
	char *name,
	long size)
{
	struct data_iterator iterator;
	struct player_datum *player;
	wchar_t wide[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1];
	long index = 0;

	wide[0] = 0;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index) == player_index)
		{
			campaign_score_player_display_name(iterator.datum_index, wide, NUMBEROF(wide));
			break;
		}
	}
	for (; index < size - 1 && wide[index]; index++)
		name[index] = (char)(wide[index] >= 32 && wide[index] < 127 ? wide[index] : '?');
	name[index] = '\0';
	if (index == 0)
		snprintf(name, size, "PLAYER %d", (int)player_index + 1);

	return;
}

/* how many kills a player has */
static long campaign_score_kills_of(
	short player_index)
{
	long total = 0;
	long index;

	for (index = 0; index < campaign_score_kill_count; index++)
	{
		if (campaign_score_kills[index].player_index == player_index)
			total++;
	}
	return total;
}

/* the points of all the players' kills together (a co-op team's score) */
static long campaign_score_team_total(
	void)
{
	long total = 0;
	long index;

	for (index = 0; index < campaign_score_kill_count; index++)
		total += campaign_score_kills[index].points;
	return total;
}

/* a variant's name as the summary has it: the last of its path, in capitals,
with spaces for underscores */
static void campaign_score_variant_name(
	long variant_index,
	char *name,
	long size)
{
	char const *path = tag_get_name(variant_index);
	char const *last;
	long index;

	if (path == NULL)
		path = "enemy";
	last = path;
	for (index = 0; path[index]; index++)
	{
		if (path[index] == '\\' || path[index] == '/')
			last = path + index + 1;
	}
	for (index = 0; index < size - 1 && last[index]; index++)
	{
		char c = last[index];

		if (c == '_')
			c = ' ';
		else if (c >= 'a' && c <= 'z')
			c = (char)(c - 32);
		name[index] = c;
	}
	name[index] = '\0';

	return;
}

/* the level's time, H:MM:SS, as the Master Chief Collection's */
static void campaign_score_time_string(
	char *text,
	long size)
{
	long seconds = MAX(campaign_score_level_ticks(), 0) / TICKS_PER_SECOND;

	snprintf(text, size, "%ld:%02ld:%02ld", seconds / 3600, seconds / 60 % 60, seconds % 60);

	return;
}

/* the level's totals and best variants, as lines */
static short campaign_score_compose(
	char lines[CAMPAIGN_SCORE_SUMMARY_LINES][CAMPAIGN_SCORE_SUMMARY_LENGTH])
{
	struct campaign_score_variant variants[CAMPAIGN_SCORE_MAXIMUM_VARIANTS];
	long variant_count = 0;
	short count = 0;
	short player_index;
	long index;
	long kills = 0;
	long points = 0;
	long players = 0;
	char time[16];

	for (index = 0; index < campaign_score_kill_count; index++)
	{
		long found;

		kills++;
		points += campaign_score_kills[index].points;
		for (found = 0; found < variant_count; found++)
		{
			if (variants[found].variant_index == campaign_score_kills[index].variant_index)
				break;
		}
		if (found == variant_count && variant_count < CAMPAIGN_SCORE_MAXIMUM_VARIANTS)
		{
			variants[found].variant_index = campaign_score_kills[index].variant_index;
			variants[found].kills = 0;
			variants[found].points = 0;
			variant_count++;
		}
		if (found < variant_count)
		{
			variants[found].kills++;
			variants[found].points += campaign_score_kills[index].points;
		}
	}
	if (kills == 0)
		return 0;

	campaign_score_time_string(time, sizeof(time));
	snprintf(lines[count++], CAMPAIGN_SCORE_SUMMARY_LENGTH, "LEVEL COMPLETE  %s x%.1f  %s",
		campaign_score_difficulty_names[campaign_score_difficulty()],
		campaign_score_difficulty_tenths[campaign_score_difficulty()] / 10.0, time);
	for (player_index = 0; player_index < CAMPAIGN_SCORE_MAXIMUM_PLAYERS; player_index++)
	{
		if (campaign_score_total(player_index) > 0)
			players++;
	}
	if (players > 1)
	{
		for (player_index = 0; player_index < CAMPAIGN_SCORE_MAXIMUM_PLAYERS &&
			count < CAMPAIGN_SCORE_SUMMARY_LINES - 6; player_index++)
		{
			if (campaign_score_total(player_index) > 0)
			{
				char name[24];

				campaign_score_player_name(player_index, name, sizeof(name));
				snprintf(lines[count++], CAMPAIGN_SCORE_SUMMARY_LENGTH, "%s  %ld  KILLS %ld",
					name, campaign_score_total(player_index), campaign_score_kills_of(player_index));
			}
		}
	}
	if (campaign_score_competitive && players > 1)
		snprintf(lines[count++], CAMPAIGN_SCORE_SUMMARY_LENGTH, "KILLS %ld", kills);
	else
		snprintf(lines[count++], CAMPAIGN_SCORE_SUMMARY_LENGTH, "SCORE %ld  KILLS %ld", points, kills);
	/* (the best variants, the most points first) */
	while (count < CAMPAIGN_SCORE_SUMMARY_LINES && variant_count > 0)
	{
		long best = 0;
		char name[40];

		for (index = 1; index < variant_count; index++)
		{
			if (variants[index].points > variants[best].points)
				best = index;
		}
		campaign_score_variant_name(variants[best].variant_index, name, sizeof(name));
		snprintf(lines[count++], CAMPAIGN_SCORE_SUMMARY_LENGTH, "%s x%ld  %ld",
			name, variants[best].kills, variants[best].points);
		variants[best] = variants[variant_count - 1];
		variant_count--;
	}

	return count;
}

/* ---------- public code */

boolean campaign_score_enabled(
	void)
{
	static unsigned long changes = (unsigned long)-1;
	static boolean enabled = FALSE;

	if (changes != config_changes())
	{
		changes = config_changes();
		enabled = config_boolean("game.campaign_score") != 0;
	}
	return enabled && campaign_score_level();
}

void campaign_score_new_map(
	void)
{
	short line;

	campaign_score_kill_count = 0;
	campaign_score_postgame = FALSE;
	campaign_score_full_logged = FALSE;
	campaign_score_last_time = 0;
	campaign_score_drawn_height = 0;
	/* (a level just won, shown now; else nothing) */
	campaign_score_summary_lines = campaign_score_pending_lines;
	for (line = 0; line < campaign_score_pending_lines; line++)
		memcpy(campaign_score_summary[line], campaign_score_pending[line], CAMPAIGN_SCORE_SUMMARY_LENGTH);
	campaign_score_pending_lines = 0;
	campaign_score_button_primed = FALSE;

	return;
}

/* a kill is logged (the host's own, or one it sent): FALSE if the log is full */
static boolean campaign_score_append(
	long variant_index,
	short player_index,
	short points)
{
	campaign_score_update();
	if (campaign_score_kill_count >= CAMPAIGN_SCORE_MAXIMUM_KILLS)
	{
		if (!campaign_score_full_logged)
		{
			campaign_score_full_logged = TRUE;
			console_warning("campaign score: the level's %d kills are logged; no more are scored",
				(int)CAMPAIGN_SCORE_MAXIMUM_KILLS);
		}
		return FALSE;
	}
	campaign_score_kills[campaign_score_kill_count].game_time = game_time_get();
	campaign_score_kills[campaign_score_kill_count].variant_index = variant_index;
	campaign_score_kills[campaign_score_kill_count].player_index = player_index;
	campaign_score_kills[campaign_score_kill_count].points = points;
	campaign_score_kill_count++;
	if (campaign_score_enabled())
		platform_log("campaign score: player %d killed %s for %d",
			(int)player_index, tag_get_name(variant_index), (int)points);

	return TRUE;
}

void campaign_score_record_kill(
	long unit_index,
	long killing_player_index)
{
	struct unit_datum *unit;
	long actor_index;
	long variant_index;
	short player_index;
	short points;

	/* (a client's kills are the host's, which sends them: the AI is the host's) */
	if (killing_player_index == NONE || !campaign_score_level() || game_connection() == _game_connection_network_client)
		return;
	/* (the killer's datum index, salted; the log keeps its absolute index,
	which every machine of a co-op game gives the same player) */
	if (!player_try_and_get(killing_player_index))
		return;
	player_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(killing_player_index);
	if (player_index >= CAMPAIGN_SCORE_MAXIMUM_PLAYERS)
		return;
	unit = unit_get(unit_index);
	/* (a destroyed ghost, banshee or wraith is worth its own points) */
	variant_index = NONE;
	points = 0;
	if (unit->object.type == _object_type_vehicle)
	{
		variant_index = unit->definition_index;
		points = campaign_score_vehicle_points(variant_index);
	}
	if (points <= 0)
	{
		actor_index = unit->unit.swarm_actor_index != NONE ? unit->unit.swarm_actor_index : unit->unit.actor_index;
		if (actor_index == NONE)
			return;
		variant_index = actor_get(actor_index)->meta.variant_definition_index;
		points = campaign_score_variant_points(variant_index);
	}
	if (points <= 0)
		return;
	campaign_score_competitive = campaign_score_type_competitive();
	if (campaign_score_append(variant_index, player_index, points) && network_coop_active())
		network_coop_note_score_kill(player_index, variant_index, points, campaign_score_competitive);

	return;
}

/* (network_coop.c) a kill the host scored, on a client */
void campaign_score_network_kill(
	short player_index,
	long variant_index,
	short points,
	boolean competitive)
{
	if (!campaign_score_level() || player_index < 0 || player_index >= CAMPAIGN_SCORE_MAXIMUM_PLAYERS || points <= 0)
		return;
	campaign_score_competitive = competitive;
	campaign_score_append(variant_index, player_index, points);

	return;
}

static void campaign_score_win_compose(
	void)
{
	short line;

	if (!campaign_score_enabled())
		return;
	campaign_score_update();
	campaign_score_pending_lines = campaign_score_compose(campaign_score_pending);
	for (line = 0; line < campaign_score_pending_lines; line++)
		platform_log("campaign score: %s", campaign_score_pending[line]);

	return;
}

/* the pending summary is shown over the level (in the level itself, which is
still running): FALSE if there is none to show */
static boolean campaign_score_postgame_begin(
	void)
{
	short line;

	if (campaign_score_pending_lines <= 0 || !campaign_score_level() || cinematic_in_progress())
		return FALSE;
	campaign_score_summary_lines = campaign_score_pending_lines;
	for (line = 0; line < campaign_score_pending_lines; line++)
		memcpy(campaign_score_summary[line], campaign_score_pending[line], CAMPAIGN_SCORE_SUMMARY_LENGTH);
	campaign_score_pending_lines = 0;
	campaign_score_button_primed = FALSE;

	return TRUE;
}

boolean campaign_score_postgame_wait(
	void)
{
	if (!campaign_score_postgame)
	{
		if (!campaign_score_postgame_begin())
			return FALSE;
		campaign_score_postgame = TRUE;
		return TRUE;
	}
	/* (until A or B is pressed: campaign_score_draw) */
	if (campaign_score_summary_lines > 0 && campaign_score_level())
		return TRUE;
	campaign_score_postgame = FALSE;
	campaign_score_summary_lines = 0;

	return FALSE;
}

void campaign_score_level_won(
	void)
{
	if (!campaign_score_level())
		return;
	if (game_connection() != _game_connection_network_client)
		campaign_score_competitive = campaign_score_type_competitive();
	/* (the clients write their own summaries from the kills they were sent) */
	if (game_connection() == _game_connection_network_server && network_coop_active())
		network_coop_note_score_won(campaign_score_competitive);
	campaign_score_win_compose();

	return;
}

/* (network_coop.c) the host won the level, on a client */
void campaign_score_network_won(
	boolean competitive)
{
	if (!campaign_score_level())
		return;
	campaign_score_competitive = competitive;
	campaign_score_win_compose();
	/* (shown at once: the host holds the level for its own, then the lobby
	takes everyone) */
	campaign_score_postgame_begin();

	return;
}

boolean campaign_score_is_competitive(
	void)
{
	if (game_connection() != _game_connection_network_client)
		campaign_score_competitive = campaign_score_type_competitive();
	return campaign_score_competitive;
}

long campaign_score_player_points(
	long player_index)
{
	campaign_score_update();
	return campaign_score_total((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index));
}

long campaign_score_player_kills(
	long player_index)
{
	campaign_score_update();
	return campaign_score_kills_of((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index));
}

long campaign_score_team_points(
	void)
{
	campaign_score_update();
	return campaign_score_team_total();
}

long campaign_score_level_ticks(
	void)
{
	return game_time_get();
}

void campaign_score_time_text(
	wchar_t *text,
	long count)
{
	char time[16];
	long index;

	campaign_score_time_string(time, sizeof(time));
	for (index = 0; index < count - 1 && time[index]; index++)
		text[index] = (wchar_t)time[index];
	text[index] = 0;

	return;
}

void campaign_score_player_display_name(
	long player_index,
	wchar_t *name,
	long count)
{
	struct player_datum const *player = player_try_and_get(player_index);
	wchar_t const *source = player ? player->name : L"";
	long length = player ? (long)NUMBEROF(player->name) : 0;
	struct player_profile profile;
	long index;

	/* (a player of this machine's campaign has no name of their own: a
	network game's player is given it; theirs is their profile's) */
	if (player && !player->name[0] && player->local_player_index != NONE &&
		player->local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS &&
		player_ui_get_active_player_profile_index(player->local_player_index) != NONE)
	{
		player_ui_get_active_player_profile(player->local_player_index, &profile);
		source = profile.player_name;
		length = NUMBEROF(profile.player_name);
	}
	for (index = 0; index < count - 1 && index < length && source[index]; index++)
		name[index] = source[index];
	name[index] = 0;
	if (index == 0 && player)
	{
		char text[16];

		snprintf(text, sizeof(text), "Player %d", (int)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) + 1);
		for (index = 0; index < count - 1 && text[index]; index++)
			name[index] = (wchar_t)text[index];
		name[index] = 0;
	}

	return;
}

void campaign_score_level_title(
	wchar_t *text,
	long count)
{
	char const *level_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	wchar_t const *title = custom_edition_maps_level_title(level_name);
	char const *last = level_name ? level_name : "";
	long index;

	if (title)
	{
		for (index = 0; index < count - 1 && title[index]; index++)
			text[index] = title[index];
		text[index] = 0;
		return;
	}
	/* (another level: the last of its name's path) */
	for (index = 0; level_name && level_name[index]; index++)
	{
		if (level_name[index] == '\\' || level_name[index] == '/')
			last = level_name + index + 1;
	}
	for (index = 0; index < count - 1 && last[index]; index++)
		text[index] = (wchar_t)last[index];
	text[index] = 0;

	return;
}

short campaign_score_hud_height(
	void)
{
	return campaign_score_drawn_height;
}

void campaign_score_draw(
	void)
{
	long font_index;
	struct font_header *font;
	short line_height;
	real_argb_color color;
	rectangle2d bounds;
	rectangle2d window;
	short line;

	if (cinematic_in_progress() || !game_in_progress())
		return;
	/* (the match clock's font: the HUD's) */
	font_index = hud_get_font_index();
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	window = render.camera.window_bounds;
	offset_rectangle2d(&window, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);

	/* the last level's summary, over this one's start */
	if (campaign_score_summary_lines > 0 && campaign_score_level())
	{
		boolean down = campaign_score_button_down();
		short top = (short)(window.y0 + (window.y1 - window.y0) / 4);

		/* (until A or B is pressed anew) */
		if (!campaign_score_button_primed)
			campaign_score_button_primed = TRUE;
		else if (down && !campaign_score_button_held)
			campaign_score_summary_lines = 0;
		campaign_score_button_held = down;

		color.alpha = 1.0f;
		for (line = 0; line < campaign_score_summary_lines; line++)
		{
			bounds.x0 = window.x0;
			bounds.x1 = window.x1;
			bounds.y0 = (short)(top + line * line_height);
			bounds.y1 = (short)(bounds.y0 + line_height);
			/* (the blue of a player's name in the chat, the header paler) */
			color.red = line == 0 ? 0.60f : CAMPAIGN_SCORE_BLUE_RED;
			color.green = line == 0 ? 0.74f : CAMPAIGN_SCORE_BLUE_GREEN;
			color.blue = line == 0 ? 1.0f : CAMPAIGN_SCORE_BLUE_BLUE;
			draw_string_set_draw_mode(font_index, NONE, _campaign_score_text_center, 0, &color);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, campaign_score_summary[line]);
		}
		if (campaign_score_summary_lines > 0)
		{
			bounds.x0 = window.x0;
			bounds.x1 = window.x1;
			bounds.y0 = (short)(top + (campaign_score_summary_lines + 1) * line_height);
			bounds.y1 = (short)(bounds.y0 + line_height);
			color.red = color.green = color.blue = 0.7f;
			draw_string_set_draw_mode(font_index, NONE, _campaign_score_text_center, 0, &color);
			rasterizer_draw_string(&bounds, NULL, NULL, 0, "PRESS A OR B TO CONTINUE");
		}
	}

	return;
}

/* ---------- the HUD's panel

the panel is laid out in the HUD's 480 lines from the view's bottom right
corner, and drawn scaled with the view (its height over 480), as the skulls'
icons are */

struct campaign_score_panel
{
	real scale;
	/* (the panel's top left, in the view's pixels) */
	real left;
	real top;
	long font_index;
	short line_height;
};

/* a rectangle of the panel, in its own units from its top left */
static void campaign_score_panel_quad(
	struct campaign_score_panel const *panel,
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 color)
{
	rectangle2d rectangle;

	rectangle.x0 = (short)(panel->left + x0 * panel->scale);
	rectangle.y0 = (short)(panel->top + y0 * panel->scale);
	rectangle.x1 = (short)MAX(panel->left + x1 * panel->scale, rectangle.x0 + 1);
	rectangle.y1 = (short)MAX(panel->top + y1 * panel->scale, rectangle.y0 + 1);
	draw_quad(&rectangle, color);

	return;
}

/* a rectangle's edge, a unit wide */
static void campaign_score_panel_frame(
	struct campaign_score_panel const *panel,
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 color)
{
	campaign_score_panel_quad(panel, x0, y0, x1, y0 + 1.0f, color);
	campaign_score_panel_quad(panel, x0, y1 - 1.0f, x1, y1, color);
	campaign_score_panel_quad(panel, x0, y0 + 1.0f, x0 + 1.0f, y1 - 1.0f, color);
	campaign_score_panel_quad(panel, x1 - 1.0f, y0 + 1.0f, x1, y1 - 1.0f, color);

	return;
}

/* a line of text from x0 to x1 of the panel, its top at y, `size` times the
HUD's text (scaled about its own top left: rasterizer_text_set_scale) */
static void campaign_score_panel_text(
	struct campaign_score_panel const *panel,
	char const *text,
	wchar_t const *unicode_text,
	real x0,
	real x1,
	real y,
	real size,
	short justification,
	real_argb_color const *color)
{
	real scale = panel->scale * size;
	real origin_x = panel->left + x0 * panel->scale;
	real origin_y = panel->top + y * panel->scale;
	wchar_t wide[64];
	rectangle2d bounds;
	rectangle2d ink;
	rectangle2d cursor;
	short shift;
	long index;

	if (!unicode_text)
	{
		for (index = 0; index < (long)NUMBEROF(wide) - 1 && text && text[index]; index++)
			wide[index] = (wchar_t)(unsigned char)text[index];
		wide[index] = 0;
		unicode_text = wide;
	}
	bounds.x0 = (short)origin_x;
	bounds.y0 = (short)origin_y;
	bounds.x1 = (short)(origin_x + (x1 - x0) / size);
	bounds.y1 = (short)(origin_y + panel->line_height);
	draw_string_set_draw_mode(panel->font_index, NONE, justification, 0, color);
	/* (the ink, not the characters' cells, at x0 or x1: each character's
	side bearing scales with the text, so that lines of two sizes would
	otherwise start, or end, apart. Moved in the text's own units, about
	the scale's origin) */
	draw_unicode_string_compute_bounds(&bounds, unicode_text, &ink, &cursor);
	shift = 0;
	if (ink.x0 <= ink.x1)
	{
		if (justification == _campaign_score_text_left)
			shift = (short)(ink.x0 - bounds.x0);
		else if (justification == _campaign_score_text_right)
			shift = (short)(ink.x1 - bounds.x1);
	}
	rasterizer_text_set_scale(scale, origin_x, origin_y);
	bounds.x0 = (short)(bounds.x0 - shift);
	bounds.x1 = (short)(bounds.x1 - shift);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, unicode_text);
	rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);

	return;
}

/* a row: the name and its value on top, the subline and its
value beneath; its top at y, `row_height` high */
static void campaign_score_panel_row(
	struct campaign_score_panel const *panel,
	real y,
	real row_height,
	char const *name,
	wchar_t const *unicode_name,
	char const *value,
	char const *subline,
	char const *subline_value)
{
	real_argb_color text_color;
	real_argb_color subline_color;
	real text_left = CAMPAIGN_SCORE_ROW_PADDING * 2;
	real text_right = CAMPAIGN_SCORE_PANEL_WIDTH - CAMPAIGN_SCORE_ROW_PADDING * 2;
	real subline_top = y + CAMPAIGN_SCORE_ROW_PADDING + panel->line_height * 0.92f;

	/* (the row's panel and edge) */
	campaign_score_panel_quad(panel, 0.0f, y, CAMPAIGN_SCORE_PANEL_WIDTH, y + row_height, CAMPAIGN_SCORE_PANEL_FILL);
	campaign_score_panel_frame(panel, 0.0f, y, CAMPAIGN_SCORE_PANEL_WIDTH, y + row_height, CAMPAIGN_SCORE_PANEL_EDGE);

	text_color.alpha = 1.0f;
	text_color.red = text_color.green = text_color.blue = 0.95f;
	subline_color.alpha = 1.0f;
	subline_color.red = 0.62f;
	subline_color.green = 0.80f;
	subline_color.blue = 1.0f;
	campaign_score_panel_text(panel, name, unicode_name, text_left, text_right, y + CAMPAIGN_SCORE_ROW_PADDING, 1.0f,
		_campaign_score_text_left, &text_color);
	campaign_score_panel_text(panel, value, NULL, text_left, text_right, y + CAMPAIGN_SCORE_ROW_PADDING, 1.0f,
		_campaign_score_text_right, &text_color);
	if (subline)
	{
		campaign_score_panel_text(panel, subline, NULL, text_left, text_right, subline_top,
			CAMPAIGN_SCORE_SUBLINE_SIZE, _campaign_score_text_left, &subline_color);
	}
	if (subline_value)
	{
		campaign_score_panel_text(panel, subline_value, NULL, text_left, text_right, subline_top,
			CAMPAIGN_SCORE_SUBLINE_SIZE, _campaign_score_text_right, &subline_color);
	}

	return;
}

/* the score and the level's time, as the Master Chief Collection's panel at
the bottom right of one player's view (hud.c's hud_draw_screen, before the
skulls' icons, which it lifts above itself): a strip of segments, then the
player's row (the team's in a cooperative co-op game), and in a competitive
one the best of the other players, their rival */
void campaign_score_draw_view(
	long player_index)
{
	struct campaign_score_panel panel;
	struct font_header *font;
	struct data_iterator iterator;
	struct player_datum *other;
	long rival_index = NONE;
	long rival_points = 0;
	boolean competitive;
	boolean coop;
	real row_height;
	real height;
	short rows;
	short width;
	short view_height;
	short segment;
	char score[24];
	char time[16];
	wchar_t own_name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1];

	campaign_score_drawn_height = 0;
	if (!campaign_score_enabled() || player_index == NONE)
		return;
	panel.font_index = hud_get_font_index();
	if (panel.font_index == NONE)
		return;
	font = font_definition_get(panel.font_index);
	panel.line_height = (short)(font->ascending_height + font->descending_height);
	if (panel.line_height <= 0)
		return;
	width = (short)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	view_height = (short)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	panel.scale = (real)view_height / 480.0f;
	if (panel.scale <= 0.0f)
		return;
	campaign_score_update();
	competitive = campaign_score_is_competitive();
	coop = network_coop_active() || local_player_count() > 1;

	/* (the rival: the other player with the most points) */
	if (competitive && coop)
	{
		data_iterator_new(&iterator, player_data);
		while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long points;

			if (iterator.datum_index == player_index || other->quit_out_of_game)
				continue;
			points = campaign_score_player_points(iterator.datum_index);
			if (rival_index == NONE || points > rival_points)
			{
				rival_index = iterator.datum_index;
				rival_points = points;
			}
		}
	}
	rows = rival_index != NONE ? 2 : 1;
	/* (each row a line of the HUD's text and a smaller one beneath) */
	row_height = panel.line_height * (1.0f + CAMPAIGN_SCORE_SUBLINE_SIZE) + CAMPAIGN_SCORE_ROW_PADDING * 2;
	row_height = MAX(row_height, CAMPAIGN_SCORE_ROW_MINIMUM);
	height = CAMPAIGN_SCORE_STRIP_HEIGHT + CAMPAIGN_SCORE_STRIP_GAP + rows * row_height + (rows - 1) * CAMPAIGN_SCORE_ROW_GAP;
	panel.left = width - (CAMPAIGN_SCORE_PANEL_MARGIN + CAMPAIGN_SCORE_PANEL_WIDTH) * panel.scale;
	panel.top = view_height - (CAMPAIGN_SCORE_PANEL_MARGIN + height) * panel.scale;

	/* the strip of segments over the rows */
	campaign_score_panel_quad(&panel, 0.0f, 0.0f, CAMPAIGN_SCORE_PANEL_WIDTH, CAMPAIGN_SCORE_STRIP_HEIGHT,
		CAMPAIGN_SCORE_PANEL_FILL);
	campaign_score_panel_frame(&panel, 0.0f, 0.0f, CAMPAIGN_SCORE_PANEL_WIDTH, CAMPAIGN_SCORE_STRIP_HEIGHT,
		CAMPAIGN_SCORE_PANEL_EDGE);
	for (segment = 0; segment < CAMPAIGN_SCORE_STRIP_SEGMENTS; segment++)
	{
		real span = (CAMPAIGN_SCORE_PANEL_WIDTH - 4.0f) / CAMPAIGN_SCORE_STRIP_SEGMENTS;
		real x0 = 2.0f + segment * span;

		campaign_score_panel_quad(&panel, x0 + 0.5f, 2.0f, x0 + span - 0.5f, CAMPAIGN_SCORE_STRIP_HEIGHT - 2.0f,
			CAMPAIGN_SCORE_STRIP_FILL);
	}

	campaign_score_time_string(time, sizeof(time));
	snprintf(score, sizeof(score), "%ld",
		competitive || !coop ? campaign_score_player_points(player_index) : campaign_score_team_total());
	/* (the player's own name, their profile's; a cooperative co-op
	game's row is the team's) */
	campaign_score_player_display_name(player_index, own_name, NUMBEROF(own_name));
	campaign_score_panel_row(&panel, CAMPAIGN_SCORE_STRIP_HEIGHT + CAMPAIGN_SCORE_STRIP_GAP, row_height,
		coop && !competitive ? "TEAM" : NULL, coop && !competitive ? NULL : own_name, score,
		campaign_score_difficulty_names[campaign_score_difficulty()], time);
	if (rival_index != NONE)
	{
		wchar_t rival_name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1];
		char rival_kills[24];

		campaign_score_player_display_name(rival_index, rival_name, NUMBEROF(rival_name));
		snprintf(score, sizeof(score), "%ld", rival_points);
		snprintf(rival_kills, sizeof(rival_kills), "KILLS %ld", campaign_score_player_kills(rival_index));
		campaign_score_panel_row(&panel,
			CAMPAIGN_SCORE_STRIP_HEIGHT + CAMPAIGN_SCORE_STRIP_GAP + row_height + CAMPAIGN_SCORE_ROW_GAP, row_height,
			NULL, rival_name, score, "RIVAL", rival_kills);
	}

	campaign_score_drawn_height = (short)(height + CAMPAIGN_SCORE_SKULLS_GAP + 0.5f);

	return;
}
