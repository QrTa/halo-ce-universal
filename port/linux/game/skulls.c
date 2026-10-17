/*
SKULLS.C

The campaign's skulls, as the Master Chief Collection's Halo: Combat Evolved
has them: each makes the campaign harder or stranger, and the game's sources
ask skull_active where it changes what they do (each marked "port: skull").

The skulls are taken when a level loads: game.skulls_active's, while
game.skulls is on; a co-op client's are those the host advertised, not its
own. Only a campaign game has them (a solo scenario, no multiplayer game
engine). The menus' screen (menu_functions.c) writes game.skulls_active
after a level's difficulty.

Only the host runs the AI and deals damage (port/linux/NETCODE.md), so the
skulls that change those take effect there; those of the HUD, the camera
and the pickups take effect on each machine, which the advertised mask
makes the same.
*/

#include <stdio.h>
#include <string.h>

#include "cseries.h"
#include "cseries/errors.h"
#include "effects/effect_definitions.h"
#include "effects/effects.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "math/real_math.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"

#include "halo_port_limits.h"
#include "platform.h"
#include "randomizer.h"

extern int config_boolean(char const *name);
extern char const *config_string(char const *name);

/* the words are Halopedia's of the Master Chief Collection's (MCC:Skulls) */
struct skull_description const skull_descriptions[NUMBER_OF_SKULLS] =
{
	{ "acrophobia", "ACROPHOBIA", "Rifles AND wings: jump twice to fly, crouch to\ncome down. May break a level's scenes.", "0.00" },
	{ "anger", "ANGER", "Enemies fire their weapons faster and more\nfrequently.", "1.10" },
	{ "bandanna", "BANDANNA", "Infinite ammo.", "0.00" },
	{ "black_eye", "BLACK EYE", "Your shields only recharge when you melee enemies.\nBash your way to better health!", "1.10" },
	{ "blind", "BLIND", "HUD and weapon do not display onscreen.", "1.10" },
	{ "boom", "BOOM", "Explosions have twice the damage radius.\nLight fuse, run away.", "1.00" },
	{ "catch", "CATCH", "Enemies throw and drop more grenades.", "1.10" },
	{ "eye_patch", "EYE PATCH", "Auto aim features disabled for all weapons.\nYou'll miss it when it's gone.", "1.10" },
	{ "famine", "FAMINE", "Weapons dropped by AI have half the ammo they\nnormally would.", "1.10" },
	{ "fog", "FOG", "The motion tracker is disabled. You'll miss\nthose eyes in the back of your head.", "1.10" },
	{ "foreign", "FOREIGN", "Covenant weapons: how do they work? Players\ncannot pick up or use Covenant weapons.", "1.10" },
	{ "ghost", "GHOST", "AI no longer flinch from attacks.", "1.00" },
	{ "grunt_birthday_party", "BIRTHDAY PARTY", "Grunt headshots lead to glorious celebrations.", "1.00" },
	{ "grunt_funeral", "GRUNT FUNERAL", "Grunts die in a blaze of glory. And plasma.", "1.00" },
	{ "iron", "IRON", "Death carries a heavy price. Co-op: back to the\nlast checkpoint. Solo: the level restarts.", "1.30" },
	{ "malfunction", "MALFUNCTION", "Every time you respawn, a random element of your\nHUD is disabled.", "1.00" },
	{ "mythic", "MYTHIC", "Enemies have increased health.", "1.20" },
	{ "pinata", "PINATA", "Punching enemies makes them drop grenades.\nBeat them 'til candy comes out!", "1.00" },
	{ "recession", "RECESSION", "Every shot is worth twice the ammo.\nSave those bullets, player.", "1.20" },
	{ "sputnik", "SPUTNIK", "Mass of objects is decreased, making them more\neasily displaced.", "1.00" },
	{ "thats_just_wrong", "JUST... WRONG", "Increases enemy awareness of player.", "1.10" },
	{ "thunderstorm", "THUNDERSTORM", "Upgrades the rank of most enemies.", "1.20" },
	{ "tough_luck", "TOUGH LUCK", "Enemies always go berserk, always dive out of the\nway, never flee.", "1.10" },
};

#define SKULLS_LOCAL_PLAYERS 4

static unsigned long level_mask;
static int remote_on;
static unsigned long remote_mask;

/* Malfunction's broken element, for each local player's body */
static struct
{
	long unit_index;
	int element;
} malfunction[SKULLS_LOCAL_PLAYERS];

static void malfunction_reset(void)
{
	int index;

	for (index = 0; index < SKULLS_LOCAL_PLAYERS; index++)
	{
		malfunction[index].unit_index = NONE;
		malfunction[index].element = NONE;
	}
}

unsigned long skulls_mask_from_text(char const *text)
{
	unsigned long mask = 0;
	char word[32];
	unsigned int length = 0;

	for (;; text++)
	{
		char character = text ? *text : 0;

		if (character == ',' || character == ' ' || character == 0)
		{
			if (length)
			{
				int skull;

				word[length] = 0;
				for (skull = 0; skull < NUMBER_OF_SKULLS; skull++)
				{
					if (!_stricmp(word, skull_descriptions[skull].key))
						mask |= 1UL << skull;
				}
			}
			length = 0;
			if (!character)
				break;
			continue;
		}
		if (length + 1 < sizeof(word))
			word[length++] = character;
	}

	return mask;
}

void skulls_text_from_mask(unsigned long mask, char *text, unsigned int size)
{
	unsigned int length = 0;
	int skull;

	if (!size)
		return;
	text[0] = 0;
	for (skull = 0; skull < NUMBER_OF_SKULLS; skull++)
	{
		if (!(mask & (1UL << skull)))
			continue;
		length += (unsigned int)snprintf(text + length, size - length, "%s%s", length ? "," : "",
			skull_descriptions[skull].key);
		if (length >= size)
		{
			text[size - 1] = 0;
			return;
		}
	}
}

/* the host's (or a game of one machine's) skulls, from its settings */
static unsigned long config_mask(void)
{
	struct network_game *game = network_game_get_game();

	if (!config_boolean("game.skulls"))
		return 0;
	if (game && game->variant.game_engine_index != game_engine_none)
		return 0;

	return skulls_mask_from_text(config_string("game.skulls_active"));
}

void skulls_level_loaded(void)
{
	malfunction_reset();
	if (network_game_distributed_client())
		level_mask = remote_on ? remote_mask : 0;
	else
		level_mask = config_mask();
	if (level_mask)
	{
		char text[400];

		skulls_text_from_mask(level_mask, text, sizeof(text));
		error(_error_silent, "skulls: %s", text);
	}
}

void skulls_level_unloaded(void)
{
	level_mask = 0;
	malfunction_reset();
}

int skull_active(int skull)
{
	if (!level_mask || skull < 0 || skull >= NUMBER_OF_SKULLS || !(level_mask & (1UL << skull)))
		return 0;
	/* (a campaign's scenario: not the main menu's, nor a multiplayer map's) */
	return global_scenario && global_scenario->type == _scenario_type_solo;
}

void skulls_advertisement(unsigned char *reserved)
{
	unsigned long mask;

	if (!reserved)
		return;
	mask = config_mask();
	if (!mask)
		return;
	reserved[HALO_PORT_ADVERTISED_FLAGS_OFFSET] =
		(unsigned char)(reserved[HALO_PORT_ADVERTISED_FLAGS_OFFSET] | HALO_PORT_ADVERTISED_SKULLS_FLAG);
	reserved[HALO_PORT_ADVERTISED_SKULLS_OFFSET] = (unsigned char)mask;
	reserved[HALO_PORT_ADVERTISED_SKULLS_OFFSET + 1] = (unsigned char)(mask >> 8);
	reserved[HALO_PORT_ADVERTISED_SKULLS_OFFSET + 2] = (unsigned char)(mask >> 16);
	reserved[HALO_PORT_ADVERTISED_SKULLS_OFFSET + 3] = (unsigned char)(mask >> 24);
}

void skulls_set_remote(int on, unsigned char const *mask)
{
	remote_on = on && mask;
	remote_mask = remote_on ?
		((unsigned long)mask[0] | ((unsigned long)mask[1] << 8) | ((unsigned long)mask[2] << 16) |
			((unsigned long)mask[3] << 24)) & ((1UL << NUMBER_OF_SKULLS) - 1) :
		0;
}

int skulls_team_is_enemy(short team_index)
{
	return team_index != NONE && game_team_is_enemy(_game_team_player, team_index);
}

int skulls_unit_is_enemy(long unit_index)
{
	struct unit_datum *unit = unit_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

	return unit && unit->unit.player_index == NONE && skulls_team_is_enemy(unit->object.owner_team_index);
}

int skulls_unit_is_grunt(long unit_index)
{
	struct unit_datum *unit = unit_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

	return unit && randomizer_unit_is_grunt(tag_get_name(unit->definition_index));
}

int skulls_weapon_is_covenant(long weapon_definition_index)
{
	return weapon_definition_index != NONE && randomizer_weapon_is_covenant(tag_get_name(weapon_definition_index));
}

int skulls_hud_broken(short local_player_index, long unit_index, int element)
{
	if (local_player_index < 0 || local_player_index >= SKULLS_LOCAL_PLAYERS || unit_index == NONE ||
		!skull_active(_skull_malfunction))
		return 0;
	/* (a new body: another element breaks. The local seed, as this is
	only what this machine shows) */
	if (malfunction[local_player_index].unit_index != unit_index)
	{
		malfunction[local_player_index].unit_index = unit_index;
		malfunction[local_player_index].element = (int)(local_random() % NUMBER_OF_SKULL_MALFUNCTIONS);
	}

	return malfunction[local_player_index].element == element;
}

void skulls_famine_weapon(long weapon_index)
{
	struct weapon_datum *weapon = weapon_index == NONE ? NULL :
		(struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon);
	struct weapon_definition *definition;
	short magazine;

	if (!weapon || !skull_active(_skull_famine))
		return;
	definition = weapon_definition_get(weapon->definition_index);
	/* (a plasma weapon's charge: half of what it has left) */
	weapon->weapon.age = 1.0f - (1.0f - weapon->weapon.age) * 0.5f;
	for (magazine = 0; magazine < (short)NUMBEROF(weapon->weapon.magazines) &&
		magazine < definition->weapon.magazines.count; magazine++)
	{
		weapon->weapon.magazines[magazine].rounds_total = (short)(weapon->weapon.magazines[magazine].rounds_total / 2);
		weapon->weapon.magazines[magazine].rounds_loaded = (short)((weapon->weapon.magazines[magazine].rounds_loaded + 1) / 2);
	}
}

int skulls_unit_is_player(long object_index)
{
	struct unit_datum *unit = object_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(object_index, _object_mask_unit);

	return unit && unit->unit.player_index != NONE;
}

/* Black Eye's share of a player's shield that a punch gives back */
#define BLACK_EYE_SHIELD 0.5f

void skulls_melee_damage(long victim_index, long attacker_object_index, long attacker_player_index)
{
	struct unit_datum *attacker;

	if (attacker_player_index == NONE || !skulls_unit_is_enemy(victim_index))
		return;
	if (skull_active(_skull_pinata))
	{
		/* (a grenade of the two kinds in turn: the game's own seed, as the
		host alone runs this) */
		unit_port_drop_grenade(victim_index, (short)(random_range(0, 2)));
	}
	attacker = attacker_object_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(attacker_object_index, _object_mask_unit);
	if (attacker && skull_active(_skull_black_eye) && attacker->unit.player_index != NONE &&
		attacker->object.shield_vitality < 1.0f)
	{
		attacker->object.shield_vitality += BLACK_EYE_SHIELD;
		if (attacker->object.shield_vitality > 1.0f)
			attacker->object.shield_vitality = 1.0f;
		attacker->object.shield_stun_ticks = 0;
	}
}

float skulls_boom_scale(void)
{
	return skull_active(_skull_boom) ? 2.0f : 1.0f;
}

/* an effect of the maps' (each campaign map has these two), where the unit
stands; NONE when the map has neither */
static void effect_at_unit(char const *name, char const *otherwise, long unit_index)
{
	long definition_index = tag_loaded(EFFECT_DEFINITION_TAG, name);
	char const *marker_names[1];
	real_point3d marker_points[1];
	real_vector3d marker_forwards[1];

	if (definition_index == NONE && otherwise)
		definition_index = tag_loaded(EFFECT_DEFINITION_TAG, otherwise);
	if (definition_index == NONE || !object_try_and_get_and_verify_type(unit_index, _object_mask_unit))
		return;
	marker_names[0] = "";
	object_get_origin(unit_index, &marker_points[0]);
	marker_forwards[0] = *global_up3d;
	effect_new_unattached_from_markers(definition_index, unit_index, NULL, 1, marker_names, marker_points,
		marker_forwards, 0.0f, 0.0f, NULL, NULL, TRUE);
}

void skulls_unit_killed(long unit_index, int headshot)
{
	if (!skulls_unit_is_grunt(unit_index))
		return;
	/* (the Master Chief Collection's confetti: the maps have none, so the
	co-op teleport's sparkle, which does no harm) */
	if (headshot && skull_active(_skull_grunt_birthday_party))
		effect_at_unit("effects\\coop teleport", "weapons\\plasma grenade\\effects\\detonation start", unit_index);
}

void skulls_unit_died(long unit_index)
{
	/* (a plasma grenade's explosion: its damage the host's alone, as an
	effect's damage is) */
	if (skull_active(_skull_grunt_funeral) && skulls_unit_is_grunt(unit_index))
		effect_at_unit("weapons\\plasma grenade\\effects\\explosion", NULL, unit_index);
}
