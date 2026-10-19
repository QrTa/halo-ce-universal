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
#include <stdlib.h>
#include <string.h>

#include "cseries.h"
#include "cseries/errors.h"
#include "effects/effect_definitions.h"
#include "effects/effects.h"
#include "effects/particles.h"
#include "cutscene/cinematics.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "render/render.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_engine.h"
#include "saved games/game_state.h"
#include "items/weapon_definitions.h"
#include "items/weapons.h"
#include "math/real_math.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
#include "sound/sound_manager.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "units/units.h"
#include "game/players.h"

#include "halo_port_limits.h"
#include "platform.h"
#include "custom_edition_cache.h"
#include "randomizer.h"

extern int config_boolean(char const *name);
extern char const *config_string(char const *name);
/* cseries_windows.h's */
unsigned long system_milliseconds(void);

/* the Master Chief Collection's skulls (Halopedia's MCC:Skulls), their help
on the skulls screen's help line, which wraps it (88 characters at most) */
struct skull_description const skull_descriptions[NUMBER_OF_SKULLS] =
{
	{ "anger", "ANGER", "Enemies fire faster." },
	{ "assassins", "ASSASSINS", "All enemies are permanently cloaked." },
	{ "bandanna", "BANDANNA", "Infinite ammo and grenades, never overheat." },
	{ "black_eye", "BLACK EYE", "Your shield only recharges when you punch an enemy." },
	{ "blind", "BLIND", "No HUD and no weapon on screen." },
	{ "boom", "BOOM", "Explosions reach twice as far." },
	{ "catch", "CATCH", "Enemies throw grenades more often. Also drop more." },
	{ "confetti_party", "CONFETTI PARTY", "Every enemy killed by a headshot goes out with a celebration." },
	{ "envy", "ENVY", "Your flashlight is active camo instead." },
	{ "eye_patch", "EYE PATCH", "No aim assist: no magnetism, no red reticle." },
	{ "famine", "FAMINE", "Weapons dropped by enemies have half\ntheir ammo." },
	{ "fog", "FOG", "No motion tracker." },
	{ "foreign", "FOREIGN", "You can't pick up Covenant weapons." },
	{ "ghost", "GHOST", "Enemies don't flinch when you hit them." },
	{ "grunt_birthday_party", "GRUNT BIRTHDAY", "A grunt killed by a headshot goes out with a celebration." },
	{ "grunt_funeral", "GRUNT FUNERAL", "Dying grunts blow up like plasma grenades." },
	{ "iron", "IRON", "Dying reverts to checkpoint. Single player restarts the level." },
	{ "malfunction", "MALFUNCTION", "Each time you spawn, one part of your HUD stops working." },
	{ "mythic", "MYTHIC", "Enemies have twice the health and shields." },
	{ "pinata", "PINATA", "Punching an enemy knocks a grenade loose." },
	{ "protected", "PROTECTED", "All enemies have shields." },
	{ "recession", "RECESSION", "Every shot uses twice the ammo." },
	{ "sputnik", "SPUTNIK", "Hits and blasts throw bodies, vehicles and weapons 3x as far." },
	{ "streaking", "STREAKING", "Your shields drain over time. Kill to recharge." },
	{ "swarm", "SWARM", "Hunters are much stronger." },
	{ "thats_just_wrong", "JUST... WRONG", "Enemies see and hear you from twice as far away." },
	{ "thunderstorm", "THUNDERSTORM", "Enemies are promoted." },
	{ "tough_luck", "TOUGH LUCK", "Enemies always go berserk, dive from grenades, and never flee." },
	{ "vampirism", "VAMPIRISM", "Every shot missed hurts you. Regenerate health by hitting shots." },
};

#define SKULLS_LOCAL_PLAYERS 4

static unsigned long level_mask;
static int remote_on;
static unsigned long remote_mask;

/* Malfunction's broken element, for each local player's body, and the
game time it was last looked at: a checkpoint's revert, or the level
started again, brings back the same body (the same unit index) at an
earlier time, which rolls again too */
static struct
{
	long unit_index;
	long game_time;
	int element;
} malfunction[SKULLS_LOCAL_PLAYERS] = { { NONE, NONE, NONE }, { NONE, NONE, NONE }, { NONE, NONE, NONE },
	{ NONE, NONE, NONE } };
static unsigned long malfunction_roll;

static void malfunction_reset(void)
{
	int index;

	/* (the element kept, so the next body's is another) */
	for (index = 0; index < SKULLS_LOCAL_PLAYERS; index++)
	{
		malfunction[index].unit_index = NONE;
		malfunction[index].game_time = NONE;
	}
}

/* an element, not the one broken before, nor the motion sensor while Fog
has it off already: from the clock, as the game's own local seed starts the
same each time a level loads */
static int malfunction_new_element(int previous)
{
	int choices[NUMBER_OF_SKULL_MALFUNCTIONS];
	int count = 0;
	int element;

	for (element = 0; element < NUMBER_OF_SKULL_MALFUNCTIONS; element++)
	{
		if (element == _skull_malfunction_motion_sensor && skull_active(_skull_fog))
			continue;
		if (element == previous)
			continue;
		choices[count++] = element;
	}
	malfunction_roll = malfunction_roll * 1664525UL + 1013904223UL + system_milliseconds();
	return choices[(malfunction_roll >> 16) % (unsigned long)count];
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

extern int config_write(char const *name, char const *value);

/* the question answered: unlocking by playing, ALL SKULLS on or off (the
answer "all" ALL SKULLS on, and so is a setting of "all" from before,
the first time it is read, so that ALL SKULLS can turn it off later) */
int skulls_unlock_mode(void)
{
	char const *mode = config_string("game.skulls_unlock_mode");

	if (mode && !_stricmp(mode, "all"))
	{
		if (config_write("game.skulls_unlock_mode", "play") && config_write("game.skulls_all_unlocked", "true"))
			error(_error_silent, "skulls: game.skulls_unlock_mode \"all\" is now \"play\", with ALL SKULLS on");
		return _skulls_unlock_play;
	}
	if (mode && !_stricmp(mode, "play"))
		return _skulls_unlock_play;
	return _skulls_unlock_unset;
}

/* the skulls of the mask kept for good (game.skulls_unlocked) */
static void skulls_keep(unsigned long mask)
{
	char text[400];

	skulls_text_from_mask(skulls_mask_from_text(config_string("game.skulls_unlocked")) | mask, text, sizeof(text));
	if (!config_write("game.skulls_unlocked", text))
		error(_error_silent, "skulls: could not write game.skulls_unlocked");
}

unsigned long skulls_unlocked_mask(void)
{
	unsigned long everything = (1UL << NUMBER_OF_SKULLS) - 1;

	/* (ALL SKULLS on: every one, those found kept for when it is off) */
	if (skulls_unlock_mode() != _skulls_unlock_play || config_boolean("game.skulls_all_unlocked"))
		return everything;
	/* (Grunt Birthday Party the one had from the start) */
	return (skulls_mask_from_text(config_string("game.skulls_unlocked")) | (1UL << _skull_grunt_birthday_party)) &
		everything;
}

int skulls_drop(void)
{
	return skulls_unlock_mode() == _skulls_unlock_play && !config_boolean("game.skulls_all_unlocked");
}

int skulls_carried_wanted(void)
{
	struct network_game *game = network_game_get_game();

	if (!config_boolean("game.skulls") || !skulls_drop())
		return 0;
	/* (solo and split screen: a network game's machines would each want
	to keep a skull another found) */
	if (game_connection() != _game_connection_local || network_game_distributed_client())
		return 0;
	return !game || game->variant.game_engine_index == game_engine_none;
}

/* the host's (or a game of one machine's) skulls, from its settings: those
turned on that it has */
static unsigned long config_mask(void)
{
	struct network_game *game = network_game_get_game();

	if (!config_boolean("game.skulls"))
		return 0;
	if (game && game->variant.game_engine_index != game_engine_none)
		return 0;

	return skulls_mask_from_text(config_string("game.skulls_active")) & skulls_unlocked_mask();
}

/* the game's own sounds (below) */
enum
{
	_own_sound_party = 0,
	_own_sound_skull_pickup,
	_own_sound_skull_carrier,
	_own_sound_skull_floor,
	NUMBER_OF_OWN_SOUNDS
};

static void own_sounds_add(unsigned long wanted);
static void own_sounds_remove(void);
static void finding_reset(void);
static unsigned long found_mask(void);

/* the level's carried skull's weapon tag (randomizer.c copies it in), or
NONE when its enemies carry none */
static long carried_definition = NONE;

static void running_skulls_reset(void);
static void vampirism_reset(void);

void skulls_level_loaded(long scenario_tag_index)
{
	malfunction_reset();
	running_skulls_reset();
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
	/* (the skull the enemies carry: a campaign level's, copied in by now) */
	carried_definition = NONE;
	if (scenario_tag_index != NONE && scenario_definition_get(scenario_tag_index)->type == _scenario_type_solo &&
		skulls_carried_wanted())
	{
		carried_definition = tag_loaded(WEAPON_DEFINITION_TAG, SKULLS_CARRIED_TAG);
		error(_error_silent, "skulls: the enemies %s", carried_definition != NONE ? "carry skulls" :
			"carry no skull (it could not be copied)");
	}
	/* (the sounds: Grunt Birthday Party's cheer, a campaign level's; those
	of the skulls to be found) */
	if (scenario_tag_index != NONE && scenario_definition_get(scenario_tag_index)->type == _scenario_type_solo)
	{
		own_sounds_add(((level_mask & ((1UL << _skull_grunt_birthday_party) | (1UL << _skull_confetti_party))) ?
			1UL << _own_sound_party : 0) |
			(carried_definition != NONE ? (1UL << _own_sound_skull_pickup) | (1UL << _own_sound_skull_carrier) |
				(1UL << _own_sound_skull_floor) : 0));
	}
}

void skulls_level_unloaded(void)
{
	carried_definition = NONE;
	own_sounds_remove();
	level_mask = 0;
	malfunction_reset();
}

int skull_active(int skull)
{
	/* (those turned on, and those found in the level, as the game state has
	them: a revert to before one was taken takes it back) */
	unsigned long mask = level_mask | found_mask();

	if (!mask || skull < 0 || skull >= NUMBER_OF_SKULLS || !(mask & (1UL << skull)))
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
	/* (not an object of no team, an empty vehicle's: game_team_is_enemy
	counts the default team the player's enemy) */
	return team_index != NONE && team_index != _game_team_default &&
		game_team_is_enemy(_game_team_player, team_index);
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
	/* (a new body, or the game gone back in time: another element breaks.
	Only what this machine shows) */
	if (malfunction[local_player_index].unit_index != unit_index ||
		game_time_get() < malfunction[local_player_index].game_time)
	{
		malfunction[local_player_index].unit_index = unit_index;
		malfunction[local_player_index].element = malfunction_new_element(malfunction[local_player_index].element);
	}
	malfunction[local_player_index].game_time = game_time_get();

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

/* an effect of the maps' (each campaign map has it), where the unit stands */
static void effect_at_unit(char const *name, long unit_index)
{
	long definition_index = tag_loaded(EFFECT_DEFINITION_TAG, name);
	char const *marker_names[1];
	real_point3d marker_points[1];
	real_vector3d marker_forwards[1];

	if (definition_index == NONE || !object_try_and_get_and_verify_type(unit_index, _object_mask_unit))
		return;
	marker_names[0] = "";
	object_get_origin(unit_index, &marker_points[0]);
	marker_forwards[0] = *global_up3d;
	effect_new_unattached_from_markers(definition_index, unit_index, NULL, 1, marker_names, marker_points,
		marker_forwards, 0.0f, 0.0f, NULL, NULL, TRUE);
}

/* ---------- the game's own sounds

WAVs of 16-bit PCM (port/assets/sounds, tools/embed_assets.py), made sound
tags of the level's while it wants them: a sound of the map's copied (a
grunt's dialog, else its first), with one permutation of these samples,
which the sound cache reads from memory (skulls_sound_read,
xbox_sound_cache.c). Not of the dialog's class: a unit speaks one sound at
a time, and the grunt's death cry, right after, would cut the cheer off; an
explosion's class, as loud and as far heard, plays beside it */
extern const unsigned int skulls_party_sound[];
extern const unsigned long skulls_party_sound_size;
extern const unsigned int skulls_pickup_sound[];
extern const unsigned long skulls_pickup_sound_size;
extern const unsigned int skulls_carrier_sound[];
extern const unsigned long skulls_carrier_sound_size;
extern const unsigned int skulls_floor_sound[];
extern const unsigned long skulls_floor_sound_size;
extern void *cache_files_tag_instances(long *count);
extern void cache_files_set_tag_instances(void *instances, long count);

#define SOUND_COMPRESSION_NONE 0
#define SOUND_COMPRESSION_XBOX_ADPCM 1
#define OWN_SOUND_RATE 22050
/* sound_classes.c's projectile_detonation */
#define OWN_SOUND_CLASS 1

/* cache_files.c's */
struct skulls_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

typedef char skulls_tag_instance_size[sizeof(struct skulls_tag_instance) == 0x20 ? 1 : -1];

/* the sounds the own sounds are made from, the first the map has (else
its first sound) */
static char const *const own_sound_templates[] =
{
	"sound\\dialog\\grunt\\conditional\\combat2\\postcombatactions\\celebration",
	"sound\\dialog\\grunt\\conditional\\combat2\\killingpeople\\killed_enemy",
	"sound\\dialog\\grunt\\conditional\\combat2\\friendsdying\\friend_killed_by_enemy_player",
};

static struct own_sound
{
	unsigned int const *file;
	unsigned long const *file_size;
	char *name;
	/* full loudness this near, in world units, heard this far */
	real minimum_distance;
	real maximum_distance;
	/* the samples, made Xbox ADPCM once (the game's allocator: kept) */
	boolean encoded;
	byte *samples;
	unsigned long sample_bytes;
	long milliseconds;
	/* the level's tag, while it has one */
	struct sound_definition definition;
	struct sound_pitch_range range;
	struct sound_permutation permutation;
	long tag_index;
} own_sounds[NUMBER_OF_OWN_SOUNDS] =
{
	{ skulls_party_sound, &skulls_party_sound_size, "sound\\port\\skulls\\grunt birthday party", 2.0f, 40.0f },
	{ skulls_pickup_sound, &skulls_pickup_sound_size, "sound\\port\\skulls\\skull picked up", 2.0f, 40.0f },
	{ skulls_carrier_sound, &skulls_carrier_sound_size, "sound\\port\\skulls\\skull dropped", 2.0f, 40.0f },
	/* (the skull lying about: heard near it only) */
	{ skulls_floor_sound, &skulls_floor_sound_size, "sound\\port\\skulls\\skull", 0.6f, 6.0f },
};

/* the level's tags with the own sounds added, and those before */
static struct skulls_tag_instance *own_grown_instances;
static struct skulls_tag_instance *own_saved_instances;
static long own_saved_count;

static unsigned long read_le32(byte const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

/* the WAV's samples, encoded; FALSE when it has none this build plays */
static boolean own_sound_encode(struct own_sound *sound)
{
	byte const *file = (byte const *)sound->file;
	unsigned long size = *sound->file_size;
	unsigned long offset = 12;
	byte const *data = NULL;
	unsigned long data_bytes = 0;
	long channels = 0;
	long rate = 0;
	long bits = 0;

	if (sound->encoded)
		return sound->samples != NULL;
	sound->encoded = TRUE;
	if (size < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4))
	{
		error(_error_silent, "skulls: %s is not a WAV", sound->name);
		return FALSE;
	}
	/* (its chunks: the format's and the samples') */
	while (offset + 8 <= size)
	{
		unsigned long chunk = read_le32(file + offset + 4);

		if (chunk > size - offset - 8)
			break;
		if (!memcmp(file + offset, "fmt ", 4) && chunk >= 16)
		{
			if (file[offset + 8] != 1 || file[offset + 9] != 0)
				break;
			channels = (long)(file[offset + 10] | (file[offset + 11] << 8));
			rate = (long)read_le32(file + offset + 12);
			bits = (long)(file[offset + 22] | (file[offset + 23] << 8));
		}
		else if (!memcmp(file + offset, "data", 4))
		{
			data = file + offset + 8;
			data_bytes = chunk;
		}
		offset += 8 + chunk + (chunk & 1);
	}
	if (!data || bits != 16 || (channels != 1 && channels != 2) || (rate != 22050 && rate != 44100) ||
		data_bytes < (unsigned long)(2 * channels))
	{
		error(_error_silent, "skulls: %s is not 16-bit PCM, mono or stereo, at 22 or 44 kHz", sound->name);
		return FALSE;
	}
	sound->samples = custom_edition_sounds_encode(data, (long)data_bytes, SOUND_COMPRESSION_NONE, FALSE,
		channels, rate, OWN_SOUND_RATE, &sound->sample_bytes);
	if (!sound->samples || sound->sample_bytes > MAXIMUM_SOUND_DATA_SIZE)
	{
		error(_error_silent, "skulls: %s could not be encoded", sound->name);
		sound->samples = NULL;
		return FALSE;
	}
	sound->milliseconds = (long)((data_bytes / (unsigned long)(2 * channels)) * 1000UL / (unsigned long)rate);
	return TRUE;
}

/* the sound's definition: the template's, its samples this one permutation */
static void own_sound_define(struct own_sound *sound, long template_index)
{
	struct sound_definition *definition = &sound->definition;

	*definition = *sound_definition_get(template_index);
	definition->sound_class = OWN_SOUND_CLASS;
	definition->minimum_distance = sound->minimum_distance;
	definition->maximum_distance = sound->maximum_distance;
	definition->gain_modifier = 1.0f;
	definition->zero_gain_modifier = 1.0f;
	definition->one_gain_modifier = 1.0f;
	definition->zero_pitch_modifier = 1.0f;
	definition->one_pitch_modifier = 1.0f;
	definition->zero_skip_fraction_modifier = 0.0f;
	definition->one_skip_fraction_modifier = 0.0f;
	/* (heard the same all round) */
	definition->inner_cone_angle = 2.0f * _pi;
	definition->outer_cone_angle = 2.0f * _pi;
	definition->outer_cone_gain = 1.0f;
	definition->skip_fraction = 0.0f;
	definition->random_pitch_bounds.lower = 1.0f;
	definition->random_pitch_bounds.upper = 1.0f;
	definition->encoding = 0;
	definition->compression = SOUND_COMPRESSION_XBOX_ADPCM;
	definition->sample_rate = OWN_SOUND_RATE == 44100 ? 1 : 0;
	definition->promotion_sound.index = NONE;
	definition->promotion_count = 0;
	definition->promotion_counter = 0;
	definition->promotion_time = 0;
	definition->scripting_time = 0;
	definition->scripting_sound_index = NONE;
	definition->longest_permutation_length = sound->milliseconds;
	definition->pitch_ranges.count = 1;
	definition->pitch_ranges.address = &sound->range;
	definition->pitch_ranges.definition = NULL;
	csmemset(&sound->range, 0, sizeof(sound->range));
	sound->range.natural_pitch = 1.0f;
	sound->range.bend_bounds.lower = 1.0f;
	sound->range.bend_bounds.upper = 1.0f;
	sound->range.actual_permutation_count = 1;
	sound->range.playback_rate = 1.0f;
	sound->range.played_permutation_mask = 0xFFFFFFFF;
	sound->range.previous_permutation_index = NONE;
	sound->range.forced_permutation_index = NONE;
	sound->range.permutations.count = 1;
	sound->range.permutations.address = &sound->permutation;
	sound->range.permutations.definition = NULL;
	csmemset(&sound->permutation, 0, sizeof(sound->permutation));
	sound->permutation.gain = 1.0f;
	sound->permutation.compression = SOUND_COMPRESSION_XBOX_ADPCM;
	sound->permutation.next_permutation_index = NONE;
	sound->permutation.samples.size = (long)sound->sample_bytes;
	sound->permutation.samples.address = sound->samples;
	/* (the sound cache's: block, address, tag: xbox_sound_cache.c) */
	sound->permutation.unknown0 = NONE;
	sound->permutation.unknown2 = (unsigned long)sound->tag_index;
	sound->permutation.unknown3 = (unsigned long)sound->tag_index;
}

/* the level's tags of the own sounds of the mask, appended to its tags
(after the randomizer's, which the level's unload restores after these) */
static void own_sounds_add(unsigned long wanted)
{
	long existing = 0;
	struct skulls_tag_instance *instances = (struct skulls_tag_instance *)cache_files_tag_instances(&existing);
	long template_index = NONE;
	long salt = 0;
	long added = 0;
	long index;
	int sound;

	for (sound = 0; sound < NUMBER_OF_OWN_SOUNDS; sound++)
		own_sounds[sound].tag_index = NONE;
	if (!wanted || !instances || existing <= 0 || existing + NUMBER_OF_OWN_SOUNDS >= UNSIGNED_SHORT_MAX)
		return;
	for (index = 0; index < (long)NUMBEROF(own_sound_templates) && template_index == NONE; index++)
		template_index = tag_loaded(SOUND_DEFINITION_TAG, own_sound_templates[index]);
	for (index = 0; index < existing && template_index == NONE; index++)
	{
		if (instances[index].group_tag == SOUND_DEFINITION_TAG)
			template_index = instances[index].tag_index;
	}
	if (template_index == NONE)
	{
		error(_error_silent, "skulls: the level has no sound to make the skulls' sounds of");
		return;
	}
	own_grown_instances = (struct skulls_tag_instance *)malloc((unsigned long)(existing + NUMBER_OF_OWN_SOUNDS) *
		sizeof(*own_grown_instances));
	if (!own_grown_instances)
		return;
	memcpy(own_grown_instances, instances, (unsigned long)existing * sizeof(*instances));
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (long)((unsigned long)instances[index].tag_index >> 16);

		if (instance_salt > salt)
			salt = instance_salt;
	}
	for (sound = 0; sound < NUMBER_OF_OWN_SOUNDS; sound++)
	{
		struct own_sound *own = own_sounds + sound;
		struct skulls_tag_instance *instance = own_grown_instances + existing + added;

		if (!(wanted & (1UL << sound)) || !own_sound_encode(own))
			continue;
		own->tag_index = ((salt + 1) << 16) | (existing + added);
		own_sound_define(own, template_index);
		*instance = instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(template_index)];
		instance->tag_index = own->tag_index;
		instance->name = own->name;
		instance->base_address = &own->definition;
		added++;
		error(_error_silent, "skulls: %s is tag %08lx (%lu bytes, %ld ms), made of %s", own->name,
			(unsigned long)own->tag_index, own->sample_bytes, own->milliseconds, tag_get_name(template_index));
	}
	if (!added)
	{
		free(own_grown_instances);
		own_grown_instances = NULL;
		return;
	}
	own_saved_instances = instances;
	own_saved_count = existing;
	cache_files_set_tag_instances(own_grown_instances, existing + added);
}

static void own_sounds_remove(void)
{
	int sound;

	if (own_saved_instances)
		cache_files_set_tag_instances(own_saved_instances, own_saved_count);
	if (own_grown_instances)
		free(own_grown_instances);
	own_grown_instances = NULL;
	own_saved_instances = NULL;
	own_saved_count = 0;
	for (sound = 0; sound < NUMBER_OF_OWN_SOUNDS; sound++)
		own_sounds[sound].tag_index = NONE;
}

/* the sound played from the object; the sound, or NONE */
static long own_sound_play(int sound, long object_index)
{
	if (own_sounds[sound].tag_index == NONE || object_index == NONE)
	{
		error(_error_silent, "skulls: %s not played (tag %08lx, object %08lx)", own_sounds[sound].name,
			(unsigned long)own_sounds[sound].tag_index, (unsigned long)object_index);
		return NONE;
	}
	return object_impulse_sound_new(object_index, own_sounds[sound].tag_index, NONE, global_origin3d,
		global_forward3d, 1.0f);
}

unsigned char skulls_sound_read(struct sound_permutation const *permutation, void *buffer)
{
	int sound;

	for (sound = 0; sound < NUMBER_OF_OWN_SOUNDS; sound++)
	{
		if (permutation == &own_sounds[sound].permutation && own_sounds[sound].samples)
		{
			memcpy(buffer, own_sounds[sound].samples, (size_t)own_sounds[sound].sample_bytes);
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- confetti

the shirt scraps of the maps' cloth debris, and the sparks of their impacts,
each a colour of a palette's, thrown from a unit's head (the local seed:
only what this machine shows) */
#define CONFETTI_SCRAPS 40
#define CONFETTI_SPARKS 16
#define CONFETTI_SCRAP_TAG "effects\\particles\\solid\\debris shirt"
#define CONFETTI_SPARK_TAG "effects\\particles\\flash\\sparks impact"

/* Grunt Birthday Party's */
static real_rgb_color const party_colors[] =
{
	{ 1.0f, 0.15f, 0.2f }, { 1.0f, 0.8f, 0.1f }, { 0.2f, 0.9f, 0.3f },
	{ 0.15f, 0.55f, 1.0f }, { 0.85f, 0.25f, 1.0f }, { 1.0f, 0.5f, 0.1f },
};

/* a skull dropped's: black and white, floating up */
static real_rgb_color const skull_colors[] =
{
	{ 0.04f, 0.04f, 0.04f }, { 1.0f, 1.0f, 1.0f },
};

#define SKULL_CONFETTI_RISE 0.6f

static void confetti_throw(long definition_index, short count, real_point3d const *center, real speed,
	real radius_lower, real radius_upper, real_rgb_color const *colors, short color_count)
{
	short piece;

	if (definition_index == NONE)
		return;
	for (piece = 0; piece < count; piece++)
	{
		struct new_particle_data data;
		real_vector3d direction;

		csmemset(&data, 0, sizeof(data));
		data.definition_index = definition_index;
		data.object_index = NONE;
		data.node_index = NONE;
		data.local_player_index = NONE;
		data.position = *center;
		direction.i = real_local_random_range(-0.6f, 0.6f);
		direction.j = real_local_random_range(-0.6f, 0.6f);
		direction.k = real_local_random_range(0.7f, 1.2f);
		normalize3d(&direction);
		data.direction = direction;
		scale_vector3d(&direction, speed * real_local_random_range(0.6f, 1.2f), &data.velocity);
		data.rotation = real_local_random_range(0.0f, 2.0f * _pi);
		data.angular_velocity = real_local_random_range(-8.0f, 8.0f);
		data.radius = real_local_random_range(radius_lower, radius_upper);
		data.color.alpha = 1.0f;
		data.color.rgb = colors[local_random() % color_count];
		particle_new(&data);
	}
}

/* (each a share of its particle's own size: a whole scrap of shirt is
bigger than the grunt) */
static void confetti(long unit_index, real_rgb_color const *colors, short color_count, real rise)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	real_point3d center;

	if (!unit)
		return;
	center = unit->object.bounding_sphere_center;
	center.z += unit->object.bounding_sphere_radius * 0.5f;
	particles_port_rise(rise);
	confetti_throw(tag_loaded(PARTICLE_TAG, CONFETTI_SCRAP_TAG), CONFETTI_SCRAPS, &center, 1.6f, 0.05f, 0.09f,
		colors, color_count);
	confetti_throw(tag_loaded(PARTICLE_TAG, CONFETTI_SPARK_TAG), CONFETTI_SPARKS, &center, 2.2f, 0.125f, 0.2f,
		colors, color_count);
	particles_port_rise(0.0f);
}

/* ---------- the skulls to be found

The enemies that hold things (randomizer.c's randomizer_unit_can_carry) drop
the oddball the level copied in: the first the players kill, then each
they kill one chance in as many as are left of those the level places
(randomizer_level_carriers, less those dead), until the difficulty's
number (one on easy to four on legendary). Each is a skull not had, at
random, named by the pick-up prompt; it hums on the floor. The action
button takes it (never walking over it): it chimes, is on for the level,
and is kept once the level is finished without dying */
#define SKULLS_DROPPED_MAX 8
#define SKULL_NAME_LENGTH 40

/* in the game state: a checkpoint's revert, and the level started again,
bring back the skulls dropped and found as they were then */
struct skulls_finding
{
	/* the level's carriers (NONE: not counted yet), those dead, and those
	of them the players killed */
	long total;
	long deaths;
	long kills;
	short dropped;
	/* the skulls taken this level: on, not yet kept; and those lost (as the
	level's lost_mask, below, which a saved game keeps through this) */
	unsigned long found_mask;
	unsigned long lost_mask;
	/* rolled when the level starts (and starts again): the order the skulls
	drop in, and the odds of each kill's, so a revert drops the same skull at
	the same kill; another start of the level, other skulls */
	unsigned long seed;
	signed char order[(NUMBER_OF_SKULLS + 3) & ~3];
	struct
	{
		long object_index;
		long next_hum_time;
		int skull;
		wchar_t name[SKULL_NAME_LENGTH];
	} skulls[SKULLS_DROPPED_MAX];
};

static struct skulls_finding *finding;
/* the skulls found in the level and lost by a death or a revert: not the
game state's, which a revert puts back, and not dropped again in the level */
static unsigned long lost_mask;
/* the hum each dropped skull plays (a sound of this machine's, not the
game state's: forgotten when a revert takes the game back), and when */
static long skull_hums[SKULLS_DROPPED_MAX] = { NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE };
static long skull_hums_time;

/* the skulls found in the level, not yet kept */
static unsigned long found_mask(void)
{
	return finding ? finding->found_mask : 0;
}

/* a number of the seed's, for the step (a kill's odds, a place in the
order): the same for the same seed and step */
static unsigned long finding_random(unsigned long step)
{
	unsigned long value = finding->seed ^ (step * 0x9E3779B9UL);

	value ^= value >> 16;
	value *= 0x7FEB352DUL;
	value ^= value >> 15;
	value *= 0x846CA68BUL;
	value ^= value >> 16;
	return value & 0xFFFFFFFFUL;
}

static void finding_reset(void)
{
	short index;

	finding->total = NONE;
	finding->deaths = 0;
	finding->kills = 0;
	finding->dropped = 0;
	finding->found_mask = 0;
	finding->lost_mask = 0;
	/* (the order, shuffled from the clock: each start of the level its own) */
	finding->seed = (unsigned long)system_milliseconds() * 2654435761UL ^ 0x5EED5EEDUL;
	for (index = 0; index < NUMBER_OF_SKULLS; index++)
		finding->order[index] = (signed char)index;
	for (index = NUMBER_OF_SKULLS - 1; index > 0; index--)
	{
		short other = (short)(finding_random(0x1000UL + (unsigned long)index) % (unsigned long)(index + 1));
		signed char swap = finding->order[index];

		finding->order[index] = finding->order[other];
		finding->order[other] = swap;
	}
	for (index = 0; index < SKULLS_DROPPED_MAX; index++)
	{
		finding->skulls[index].object_index = NONE;
		skull_hums[index] = NONE;
		finding->skulls[index].next_hum_time = 0;
		finding->skulls[index].skull = NONE;
		finding->skulls[index].name[0] = 0;
	}
}

static int skulls_unit_can_carry(long unit_index)
{
	struct unit_datum *unit = unit_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

	return unit && randomizer_unit_can_carry(tag_get_name(unit->definition_index));
}

/* the enemies alive that could carry one, the one dying among them */
static long carriers_left(long dying_index)
{
	struct object_iterator iterator;
	struct unit_datum *unit;
	long count = 1;

	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((unit = object_iterator_next(&iterator)) != NULL)
	{
		if (iterator.index == dying_index || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		if (skulls_unit_is_enemy(iterator.index) && skulls_unit_can_carry(iterator.index))
			count++;
	}
	return count;
}

/* the skulls that could be dropped: not had, not found this level, not
lying about */
static unsigned long skulls_to_find(void)
{
	unsigned long mask = ~skulls_unlocked_mask() & ~finding->found_mask & ~lost_mask & ((1UL << NUMBER_OF_SKULLS) - 1);
	short slot;

	for (slot = 0; slot < SKULLS_DROPPED_MAX; slot++)
	{
		if (finding->skulls[slot].object_index != NONE && finding->skulls[slot].skull != NONE)
			mask &= ~(1UL << finding->skulls[slot].skull);
	}
	return mask;
}

/* the mask's skull first in the level's order */
static int skull_next(unsigned long mask)
{
	short index;

	for (index = 0; index < NUMBER_OF_SKULLS; index++)
	{
		int skull = finding->order[index];

		if (skull >= 0 && skull < NUMBER_OF_SKULLS && (mask & (1UL << skull)))
			return skull;
	}
	return NONE;
}

/* the skulls still to drop in the level: its difficulty's number, less
those dropped, as many as are not had */
static short skulls_left_to_find(void)
{
	short left = (short)(game_difficulty_level_get() + 1 - finding->dropped);
	unsigned long mask = skulls_to_find();
	short count = 0;
	int skull;

	for (skull = 0; skull < NUMBER_OF_SKULLS; skull++)
		count += (short)((mask >> skull) & 1);
	if (left > count)
		left = count;
	return left > 0 ? left : 0;
}

static void skull_drop_maybe(long unit_index, long killer_player_index)
{
	short quota = (short)(game_difficulty_level_get() + 1);
	short slot;
	long left;
	long skull_index;
	boolean drop;
	int skull;
	char name[SKULL_NAME_LENGTH];
	short character;

	if (!skulls_unit_is_enemy(unit_index) || !skulls_unit_can_carry(unit_index))
		return;
	/* (the level's carriers, counted at the first death: those it places
	over the whole level, not only those about now) */
	if (finding->total == NONE)
	{
		finding->total = randomizer_level_carriers();
		error(_error_silent, "skulls: the level places %ld enemies that could carry a skull", finding->total);
	}
	/* (as many left as the level places less the dead, and no fewer than
	are alive now: a squad brought back more than its count) */
	left = finding->total - finding->deaths;
	if (left < carriers_left(unit_index))
		left = carriers_left(unit_index);
	finding->deaths++;
	if (killer_player_index == NONE)
		return;
	/* (none left to find: all of them had, or lying about) */
	if (finding->dropped >= quota || !skulls_to_find())
	{
		finding->kills++;
		return;
	}
	/* (the kill's odds, the level's own: the same kill drops after a revert) */
	drop = finding->kills == 0 || (long)(finding_random((unsigned long)finding->deaths) % (unsigned long)left) == 0;
	finding->kills++;
	if (!drop)
		return;
	for (slot = 0; slot < SKULLS_DROPPED_MAX && finding->skulls[slot].object_index != NONE; slot++)
		;
	if (slot == SKULLS_DROPPED_MAX)
		return;
	skull = skull_next(skulls_to_find());
	skull_index = skull == NONE ? NONE : unit_port_drop_object(unit_index, carried_definition);
	if (skull_index == NONE)
		return;
	finding->dropped++;
	finding->skulls[slot].object_index = skull_index;
	skull_hums[slot] = NONE;
	finding->skulls[slot].next_hum_time = game_time_get();
	finding->skulls[slot].skull = skull;
	snprintf(name, sizeof(name), "%s SKULL", skull_descriptions[skull].label);
	for (character = 0; name[character] && character < SKULL_NAME_LENGTH - 1; character++)
		finding->skulls[slot].name[character] = (wchar_t)(unsigned char)name[character];
	finding->skulls[slot].name[character] = 0;
	error(_error_silent, "skulls: kill %ld (death %ld) dropped %s, %d of %d (one chance in %ld)", finding->kills,
		finding->deaths, name, finding->dropped, quota, left);
	own_sound_play(_own_sound_skull_carrier, unit_index);
	confetti(unit_index, skull_colors, (short)NUMBEROF(skull_colors), SKULL_CONFETTI_RISE);
}

/* ---------- the skulls that change things as the game runs */

/* the players' camo powerup (players.c's) */
extern boolean player_handle_powerup(long player_index, short powerup_type, short duration);

/* Envy: the camo a press gives, and the wait after it before another */
#define ENVY_CAMO_TICKS (10 * TICKS_PER_SECOND)
#define ENVY_RECHARGE_TICKS (15 * TICKS_PER_SECOND)
#define ENVY_PLAYERS 16

static long envy_ready_time[ENVY_PLAYERS];

int skulls_envy(long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	long now = game_time_get();
	short slot;

	if (!unit || unit->unit.player_index == NONE || !skull_active(_skull_envy))
		return FALSE;
	slot = (short)(DATUM_INDEX_TO_ABSOLUTE_INDEX(unit->unit.player_index) % ENVY_PLAYERS);
	/* (the light stays off; a revert to before the last press, ready) */
	SET_FLAG(unit->unit.flags, _unit_integrated_light_on_bit, FALSE);
	if (now + ENVY_CAMO_TICKS + ENVY_RECHARGE_TICKS < envy_ready_time[slot])
		envy_ready_time[slot] = 0;
	if (now >= envy_ready_time[slot] && !TEST_FLAG(unit->unit.flags, _unit_active_camouflaged_bit) &&
		player_handle_powerup(unit->unit.player_index, _player_powerup_active_camouflage, ENVY_CAMO_TICKS))
	{
		envy_ready_time[slot] = now + ENVY_CAMO_TICKS + ENVY_RECHARGE_TICKS;
	}
	return TRUE;
}

/* Swarm: whether a definition is a hunter's, by its tag's name, remembered
for the level (asked of every object whose vitality is read) */
#define HUNTER_KINDS 32

static struct
{
	long definition_index;
	boolean hunter;
} hunter_kinds[HUNTER_KINDS];
static short hunter_kind_count;

int skulls_object_is_hunter(long definition_index)
{
	short index;
	boolean hunter;

	for (index = 0; index < hunter_kind_count; index++)
	{
		if (hunter_kinds[index].definition_index == definition_index)
			return hunter_kinds[index].hunter;
	}
	hunter = definition_index != NONE && randomizer_unit_is_hunter(tag_get_name(definition_index));
	if (hunter_kind_count < HUNTER_KINDS)
	{
		hunter_kinds[hunter_kind_count].definition_index = definition_index;
		hunter_kinds[hunter_kind_count].hunter = hunter;
		hunter_kind_count++;
	}
	return hunter;
}

/* a level's: no hunters known, Envy ready */
static void running_skulls_reset(void)
{
	short slot;

	hunter_kind_count = 0;
	for (slot = 0; slot < ENVY_PLAYERS; slot++)
		envy_ready_time[slot] = 0;
	vampirism_reset();
}

/* Streaking: the players' shields drain from full to none in this long,
and a kill gives back this share of them */
#define STREAKING_DRAIN_TICKS (40 * TICKS_PER_SECOND)
#define STREAKING_KILL_SHIELD 0.25f

/* Protected: an enemy without a shield (a grunt's, a jackal's) is given
one of this share of its health, which does not come back */
#define PROTECTED_SHIELD 0.75f

void skulls_protect(long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);

	if (!unit || !skull_active(_skull_protected) || unit->object.type != _object_type_biped ||
		unit->object.maximum_shield_vitality > 0.0f || unit->object.maximum_body_vitality <= 0.0f ||
		TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || !skulls_unit_is_enemy(unit_index))
	{
		return;
	}
	unit->object.maximum_shield_vitality = unit->object.maximum_body_vitality * PROTECTED_SHIELD;
	unit->object.shield_vitality = 1.0f;
}

/* the flare of a hit on Protected's shield: the covenant shield's sparks,
blue, at the body; and when it breaks, an elite's shield breaking */
#define GIVEN_SHIELD_SPARKS 6
#define GIVEN_SHIELD_SPARK_TAG "effects\\particles\\energy\\shield c generic"
#define GIVEN_SHIELD_BREAK_TAG "characters\\elite\\effects\\elite shield depletion"

static real_rgb_color const given_shield_colors[] = { { 0.35f, 0.65f, 1.0f }, { 0.6f, 0.85f, 1.0f } };

void skulls_given_shield_hit(long object_index, int broken)
{
	struct object_datum *object = object_try_and_get(object_index);
	real_point3d center;

	if (!object)
		return;
	center = object->object.bounding_sphere_center;
	confetti_throw(tag_loaded(PARTICLE_TAG, GIVEN_SHIELD_SPARK_TAG), GIVEN_SHIELD_SPARKS, &center, 0.4f, 0.1f, 0.16f,
		given_shield_colors, (short)NUMBEROF(given_shield_colors));
	if (broken)
	{
		effect_at_unit(GIVEN_SHIELD_BREAK_TAG, object_index);
		error(_error_silent, "skulls: protected's shield on %s broke", tag_get_name(object->definition_index));
	}
}

/* Vampirism: each player's shots not yet known to have hit (the game times
they were fired, oldest first): a hit on an enemy within this long takes
the oldest away and heals; one older is a miss, and hurts. Health comes
back by nothing else (health packs are left: players.c) */
#define VAMPIRISM_PLAYERS 4
#define VAMPIRISM_SHOTS 32
#define VAMPIRISM_HIT_TICKS (TICKS_PER_SECOND * 3 / 2)
#define VAMPIRISM_HIT_HEALTH 0.03f
#define VAMPIRISM_MISS_SHIELD 0.04f
#define VAMPIRISM_MISS_HEALTH 0.02f
/* (a miss never kills: it leaves this much health) */
#define VAMPIRISM_MISS_FLOOR 0.05f

static struct
{
	long unit_index;
	long fired[VAMPIRISM_SHOTS];
	short count;
} vampirism_shots[VAMPIRISM_PLAYERS];

static short vampirism_slot(long unit_index, boolean add)
{
	short slot;
	short free_slot = NONE;

	for (slot = 0; slot < VAMPIRISM_PLAYERS; slot++)
	{
		if (vampirism_shots[slot].unit_index == unit_index && vampirism_shots[slot].count >= 0)
			return slot;
		if (free_slot == NONE && (vampirism_shots[slot].count == 0 ||
			!object_try_and_get_and_verify_type(vampirism_shots[slot].unit_index, _object_mask_unit)))
		{
			free_slot = slot;
		}
	}
	if (!add || free_slot == NONE)
		return NONE;
	vampirism_shots[free_slot].unit_index = unit_index;
	vampirism_shots[free_slot].count = 0;
	return free_slot;
}

static void vampirism_reset(void)
{
	short slot;

	for (slot = 0; slot < VAMPIRISM_PLAYERS; slot++)
	{
		vampirism_shots[slot].unit_index = NONE;
		vampirism_shots[slot].count = 0;
	}
}

void skulls_shot_fired(long unit_index)
{
	short slot;

	if (!skull_active(_skull_vampirism) || network_game_distributed_client() || !skulls_unit_is_player(unit_index))
		return;
	slot = vampirism_slot(unit_index, TRUE);
	if (slot == NONE)
		return;
	/* (full: the oldest is let go, neither hit nor missed) */
	if (vampirism_shots[slot].count == VAMPIRISM_SHOTS)
	{
		memmove(vampirism_shots[slot].fired, vampirism_shots[slot].fired + 1,
			(VAMPIRISM_SHOTS - 1) * sizeof(vampirism_shots[slot].fired[0]));
		vampirism_shots[slot].count--;
	}
	vampirism_shots[slot].fired[vampirism_shots[slot].count++] = game_time_get();
}

void skulls_damage_dealt(long object_index, long owner_player_index)
{
	struct player_datum *player;
	struct unit_datum *unit;
	short slot;

	if (!skull_active(_skull_vampirism) || owner_player_index == NONE || !skulls_unit_is_enemy(object_index))
		return;
	player = player_get(owner_player_index);
	unit = player->unit_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return;
	/* (a hit: its shot no miss; and some health back) */
	slot = vampirism_slot(player->unit_index, FALSE);
	if (slot != NONE && vampirism_shots[slot].count > 0)
	{
		memmove(vampirism_shots[slot].fired, vampirism_shots[slot].fired + 1,
			(VAMPIRISM_SHOTS - 1) * sizeof(vampirism_shots[slot].fired[0]));
		vampirism_shots[slot].count--;
	}
	unit->object.body_vitality += VAMPIRISM_HIT_HEALTH;
	if (unit->object.body_vitality > 1.0f)
		unit->object.body_vitality = 1.0f;
}

/* the shots that hit nothing in time: each takes some shield, then health */
static void vampirism_tick(void)
{
	long now = game_time_get();
	short slot;

	if (!skull_active(_skull_vampirism) || network_game_distributed_client())
		return;
	for (slot = 0; slot < VAMPIRISM_PLAYERS; slot++)
	{
		struct unit_datum *unit;

		if (vampirism_shots[slot].count <= 0)
			continue;
		unit = (struct unit_datum *)object_try_and_get_and_verify_type(vampirism_shots[slot].unit_index,
			_object_mask_unit);
		/* (gone, dead, or a revert to before the shots) */
		if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
			now < vampirism_shots[slot].fired[0])
		{
			vampirism_shots[slot].count = 0;
			continue;
		}
		while (vampirism_shots[slot].count > 0 && now - vampirism_shots[slot].fired[0] > VAMPIRISM_HIT_TICKS)
		{
			memmove(vampirism_shots[slot].fired, vampirism_shots[slot].fired + 1,
				(VAMPIRISM_SHOTS - 1) * sizeof(vampirism_shots[slot].fired[0]));
			vampirism_shots[slot].count--;
			if (unit->object.shield_vitality > 0.0f)
			{
				unit->object.shield_vitality -= VAMPIRISM_MISS_SHIELD;
				if (unit->object.shield_vitality < 0.0f)
					unit->object.shield_vitality = 0.0f;
			}
			else if (unit->object.body_vitality > VAMPIRISM_MISS_FLOOR)
			{
				unit->object.body_vitality -= VAMPIRISM_MISS_HEALTH;
				if (unit->object.body_vitality < VAMPIRISM_MISS_FLOOR)
					unit->object.body_vitality = VAMPIRISM_MISS_FLOOR;
			}
		}
	}
}

/* the units' skulls, each tick: Assassins cloaks the enemies (those placed
before it was on too), Protected shields them, Streaking drains the
players' shields; where the shields are run (not a distributed client's:
the host's) */
static void skulls_units_tick(void)
{
	struct object_iterator iterator;
	struct unit_datum *unit;
	boolean assassins = skull_active(_skull_assassins) && game_time_get() % TICKS_PER_SECOND == 0;
	boolean protect = skull_active(_skull_protected) && !network_game_distributed_client() &&
		game_time_get() % TICKS_PER_SECOND == 0;
	/* (Streaking stops for a cutscene) */
	boolean streaking = skull_active(_skull_streaking) && !network_game_distributed_client() &&
		!cinematic_in_progress();

	vampirism_tick();
	if (!assassins && !streaking && !protect)
		return;
	object_iterator_new(&iterator, _object_mask_unit, 0);
	while ((unit = object_iterator_next(&iterator)) != NULL)
	{
		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		if (streaking && unit->unit.player_index != NONE && unit->object.shield_vitality > 0.0f)
		{
			unit->object.shield_vitality -= 1.0f / STREAKING_DRAIN_TICKS;
			if (unit->object.shield_vitality < 0.0f)
				unit->object.shield_vitality = 0.0f;
		}
		if (protect && unit->object.maximum_shield_vitality <= 0.0f)
			skulls_protect(iterator.index);
		/* (an enemy's body: not a vehicle, whose riders would vanish with it) */
		if (assassins && unit->object.type == _object_type_biped &&
			!TEST_FLAG(unit->unit.flags, _unit_active_camouflaged_bit) && skulls_unit_is_enemy(iterator.index))
		{
			SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit, TRUE);
			unit->unit.active_camouflage = 1.0f;
		}
	}
}

/* Streaking: a kill of the player's gives back some shield */
static void streaking_kill(long unit_index, long killer_player_index)
{
	struct player_datum *player;
	struct unit_datum *killer;

	if (!skull_active(_skull_streaking) || killer_player_index == NONE || network_game_distributed_client() ||
		!skulls_unit_is_enemy(unit_index))
	{
		return;
	}
	player = player_get(killer_player_index);
	killer = player->unit_index == NONE ? NULL :
		(struct unit_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);
	if (!killer)
		return;
	killer->object.shield_vitality += STREAKING_KILL_SHIELD;
	if (killer->object.shield_vitality > 1.0f)
		killer->object.shield_vitality = 1.0f;
}

void skulls_tick(void)
{
	long now;
	short slot;

	skulls_units_tick();
	if (carried_definition == NONE)
		return;
	now = game_time_get();
	if (now < skull_hums_time)
	{
		for (slot = 0; slot < SKULLS_DROPPED_MAX; slot++)
			skull_hums[slot] = NONE;
	}
	skull_hums_time = now;
	for (slot = 0; slot < SKULLS_DROPPED_MAX; slot++)
	{
		long skull_index = finding->skulls[slot].object_index;
		struct weapon_datum *skull = skull_index == NONE ? NULL :
			(struct weapon_datum *)object_try_and_get_and_verify_type(skull_index, _object_mask_weapon);

		if (skull_index == NONE)
			continue;
		/* (gone, or another object in its place after a revert) */
		if (!skull || skull->definition_index != carried_definition)
		{
			finding->skulls[slot].object_index = NONE;
			continue;
		}
		/* (in a unit's hands, which a revert may bring back: not heard) */
		if (skull->object.parent_object_index != NONE)
			continue;
		if (now >= finding->skulls[slot].next_hum_time)
		{
			skull_hums[slot] = own_sound_play(_own_sound_skull_floor, skull_index);
			/* (again as it ends: it loops) */
			finding->skulls[slot].next_hum_time = now +
				MAX(1, own_sounds[_own_sound_skull_floor].milliseconds * TICKS_PER_SECOND / 1000);
		}
	}
}

static short skull_slot(long object_index)
{
	short slot;

	if (object_index == NONE || carried_definition == NONE)
		return NONE;
	for (slot = 0; slot < SKULLS_DROPPED_MAX; slot++)
	{
		if (finding->skulls[slot].object_index == object_index && finding->skulls[slot].skull != NONE)
			return slot;
	}
	return NONE;
}

unsigned short const *skulls_item_name(long object_index)
{
	short slot = skull_slot(object_index);

	return slot == NONE ? NULL : (unsigned short const *)finding->skulls[slot].name;
}

int skulls_item_take(short local_player_index, long unit_index, long object_index)
{
	short slot = skull_slot(object_index);
	int skull;
	wchar_t line[64];
	char text[64];
	short character;
	boolean instant;

	if (slot == NONE)
		return FALSE;
	skull = finding->skulls[slot].skull;
	instant = config_boolean("game.skulls_instant_unlock");
	/* (on for the level from now on, kept once it is finished; or with
	INSTANT UNLOCK, kept now, and not on) */
	if (instant)
		skulls_keep(1UL << skull);
	else
		finding->found_mask |= 1UL << skull;
	if (skull_hums[slot] != NONE)
		sound_stop_impulse(skull_hums[slot]);
	finding->skulls[slot].object_index = NONE;
	finding->skulls[slot].skull = NONE;
	object_delete(object_index);
	own_sound_play(_own_sound_skull_pickup, unit_index);
	error(_error_silent, "skulls: %s taken%s", skull_descriptions[skull].key, instant ? ", and unlocked" : "");
	/* (the newest message is shown on top: printed last line first) */
	if (local_player_index != NONE)
	{
		short left = skulls_left_to_find();

		if (left == 0)
			snprintf(text, sizeof(text), "No more skulls to be found in here.");
		else
			snprintf(text, sizeof(text), "%d skull%s left to be found in here.", left, left == 1 ? "" : "s");
		for (character = 0; text[character] && character < (short)NUMBEROF(line) - 1; character++)
			line[character] = (wchar_t)(unsigned char)text[character];
		line[character] = 0;
		hud_print_message(local_player_index, line);
		if (!instant)
		{
			hud_print_message(local_player_index, L"Death will take your prize away.");
			hud_print_message(local_player_index, L"Finish the level without dying to unlock it.");
		}
		snprintf(text, sizeof(text), instant ? "%s SKULL unlocked." : "%s SKULL picked up.",
			skull_descriptions[skull].label);
		for (character = 0; text[character] && character < (short)NUMBEROF(line) - 1; character++)
			line[character] = (wchar_t)(unsigned char)text[character];
		line[character] = 0;
		hud_print_message(local_player_index, line);
	}
	return TRUE;
}

/* the skulls found and not kept, lost: a player died, or the game went
back to a checkpoint (or saved and quit, which goes back to one first). Said
on the screen; they are to be found again in another level */
static void skulls_lose(char const *why)
{
	unsigned long mask = found_mask();
	short local_player_index;
	int skull;

	if (!mask)
		return;
	finding->found_mask = 0;
	for (skull = 0; skull < NUMBER_OF_SKULLS; skull++)
	{
		char text[64];
		wchar_t line[64];
		short character;

		if (!(mask & (1UL << skull)))
			continue;
		error(_error_silent, "skulls: %s lost (%s)", skull_descriptions[skull].key, why);
		/* (said once, though a death and its revert both take it) */
		if (lost_mask & (1UL << skull))
			continue;
		lost_mask |= 1UL << skull;
		finding->lost_mask |= 1UL << skull;
		snprintf(text, sizeof(text), "%s SKULL lost.", skull_descriptions[skull].label);
		for (character = 0; text[character] && character < (short)NUMBEROF(line) - 1; character++)
			line[character] = (wchar_t)(unsigned char)text[character];
		line[character] = 0;
		for (local_player_index = 0; local_player_index < local_player_count(); local_player_index++)
			hud_print_message(local_player_index, line);
	}
}

/* the lost skulls, the level's and the game state's together: a revert
puts back the game state's of then, which a death since added to; a saved
game loaded has its own, which the level knew nothing of */
static void lost_merge(void)
{
	if (!finding)
		return;
	finding->lost_mask |= lost_mask;
	lost_mask |= finding->lost_mask;
}

void skulls_game_state_reverted(void)
{
	lost_merge();
	skulls_lose("the game went back to a checkpoint");
}

void skulls_game_state_loaded(void)
{
	lost_merge();
	skulls_lose("a saved game was loaded");
}

/* ---------- the skulls on, on the screen

each skull on in the level a small icon at the bottom right, in a row from
the right (on top of another past eleven): those turned on, and those found
and not yet kept, pulsing, and those lost, crossed out. The icons are Halopedia's of the Master Chief
Collection's (MCC:Skulls), the menus' bitmap skulls/icons, a frame each in
the skulls' order (port/assets/skulls, tools/ce_menus.py); none without the
menus' bitmaps (display.menus "xbox") */
#define SKULL_ICONS_BITMAP "pc\\skulls\\icons"
#define SKULL_ICON_SIZE 22
#define SKULL_ICON_SPACING 24
#define SKULL_ICON_MARGIN 12
#define SKULL_ICONS_PER_ROW 11

void skulls_hud_draw(short local_player_index)
{
	long bitmap_index;
	unsigned long found = found_mask();
	unsigned long mask = level_mask | found | lost_mask;
	real scale;
	short width, height;
	short shown = 0;
	int skull;

	(void)local_player_index;
	if (!mask || !global_scenario || global_scenario->type != _scenario_type_solo)
		return;
	bitmap_index = tag_loaded(BITMAP_GROUP_TAG, SKULL_ICONS_BITMAP);
	if (bitmap_index == NONE)
		return;
	width = (short)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	height = (short)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	scale = (real)height / 480.0f;
	for (skull = NUMBER_OF_SKULLS - 1; skull >= 0; skull--)
	{
		struct bitmap_data *bitmap;
		rectangle2d bounds;
		real alpha = 1.0f;
		short column, row;

		if (!(mask & (1UL << skull)))
			continue;
		bitmap = bitmap_group_try_and_get_bitmap(bitmap_index, (short)skull);
		if (!bitmap)
			continue;
		column = (short)(shown % SKULL_ICONS_PER_ROW);
		row = (short)(shown / SKULL_ICONS_PER_ROW);
		shown++;
		bounds.x1 = (short)(width - (SKULL_ICON_MARGIN + column * SKULL_ICON_SPACING) * scale);
		bounds.x0 = (short)(bounds.x1 - SKULL_ICON_SIZE * scale);
		bounds.y1 = (short)(height - (SKULL_ICON_MARGIN + row * SKULL_ICON_SPACING) * scale);
		bounds.y0 = (short)(bounds.y1 - SKULL_ICON_SIZE * scale);
		/* (found, not yet kept: pulsing, once a second; lost: faint, and
		crossed out in red, the frame after the skulls') */
		if ((found & (1UL << skull)) && !(level_mask & (1UL << skull)))
			alpha = 0.6f + 0.4f * cosine((real)(game_time_get() % TICKS_PER_SECOND) * (2.0f * _pi / TICKS_PER_SECOND));
		else if (!(level_mask & (1UL << skull)) && (lost_mask & (1UL << skull)))
			alpha = 0.45f;
		draw_bitmap_in_rect(bitmap, &bounds, NULL, NULL,
			((pixel32)(alpha * 255.0f + 0.5f) << 24) | 0x00FFFFFF, NULL, TRUE);
		if (!(level_mask & (1UL << skull)) && !(found & (1UL << skull)) && (lost_mask & (1UL << skull)))
		{
			struct bitmap_data *cross = bitmap_group_try_and_get_bitmap(bitmap_index, NUMBER_OF_SKULLS);

			if (cross)
				draw_bitmap_in_rect(cross, &bounds, NULL, NULL, 0xFFFFFFFF, NULL, TRUE);
		}
	}
}

/* ---------- a unit's death */

void skulls_unit_killed(long unit_index, int headshot, long killer_player_index)
{
	streaking_kill(unit_index, killer_player_index);
	/* (a player's death: the skulls found and not kept are lost) */
	if (skulls_unit_is_player(unit_index))
		skulls_lose("a player died");
	/* (the Master Chief Collection's confetti and cheer) */
	if (headshot && ((skull_active(_skull_grunt_birthday_party) && skulls_unit_is_grunt(unit_index)) ||
		(skull_active(_skull_confetti_party) && skulls_unit_is_enemy(unit_index))))
	{
		confetti(unit_index, party_colors, (short)NUMBEROF(party_colors), 0.0f);
		own_sound_play(_own_sound_party, unit_index);
	}
	if (carried_definition != NONE)
		skull_drop_maybe(unit_index, killer_player_index);
}

void skulls_unit_died(long unit_index)
{
	/* (a plasma grenade's explosion: its damage the host's alone, as an
	effect's damage is) */
	if (skull_active(_skull_grunt_funeral) && skulls_unit_is_grunt(unit_index))
		effect_at_unit("weapons\\plasma grenade\\effects\\explosion", unit_index);
}

void skulls_initialize(void)
{
	/* (a multiple of four bytes, as the game state allocates) */
	finding = (struct skulls_finding *)game_state_malloc("skulls finding", NULL,
		(long)((sizeof(*finding) + 3) & ~(unsigned long)3));
	finding_reset();
}

void skulls_initialize_for_new_map(void)
{
	finding_reset();
	lost_mask = 0;
}
