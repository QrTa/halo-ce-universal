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
#include "interface/hud_messaging.h"
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
	{ "bandanna", "BANDANNA", "Infinite ammo and grenades, never overheat." },
	{ "black_eye", "BLACK EYE", "Your shield only recharges when you punch an enemy." },
	{ "blind", "BLIND", "No HUD and no weapon on screen." },
	{ "boom", "BOOM", "Explosions reach twice as far." },
	{ "catch", "CATCH", "Enemies throw grenades more often. Also drop more." },
	{ "eye_patch", "EYE PATCH", "No aim assist: no magnetism, no red reticle." },
	{ "famine", "FAMINE", "Weapons dropped by enemies have half\ntheir ammo." },
	{ "fog", "FOG", "No motion tracker." },
	{ "foreign", "FOREIGN", "You can't pick up Covenant weapons." },
	{ "ghost", "GHOST", "Enemies don't flinch when you hit them." },
	{ "grunt_birthday_party", "BIRTHDAY PARTY", "A grunt killed by a headshot goes out with a celebration." },
	{ "grunt_funeral", "GRUNT FUNERAL", "Dying grunts blow up like plasma grenades." },
	{ "iron", "IRON", "Dying reverts to checkpoint. Single player restarts the level." },
	{ "malfunction", "MALFUNCTION", "Each time you spawn, one part of your HUD stops working." },
	{ "mythic", "MYTHIC", "Enemies have twice the health and shields." },
	{ "pinata", "PINATA", "Punching an enemy knocks a grenade loose." },
	{ "recession", "RECESSION", "Every shot uses twice the ammo." },
	{ "sputnik", "SPUTNIK", "Hits and blasts throw bodies, vehicles and weapons 3x as far." },
	{ "thats_just_wrong", "JUST... WRONG", "Enemies see and hear you from twice as far away." },
	{ "thunderstorm", "THUNDERSTORM", "Enemies are promoted." },
	{ "tough_luck", "TOUGH LUCK", "Enemies always go berserk, dive from grenades, and never flee." },
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

int skulls_unlock_mode(void)
{
	char const *mode = config_string("game.skulls_unlock_mode");

	if (mode && !_stricmp(mode, "all"))
		return _skulls_unlock_all;
	if (mode && !_stricmp(mode, "play"))
		return _skulls_unlock_play;
	return _skulls_unlock_unset;
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

void skulls_level_loaded(long scenario_tag_index)
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
		own_sounds_add(((level_mask & (1UL << _skull_grunt_birthday_party)) ? 1UL << _own_sound_party : 0) |
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
	/* the skulls taken this level: on, not yet kept */
	unsigned long found_mask;
	struct
	{
		long object_index;
		long next_hum_time;
		int skull;
		wchar_t name[SKULL_NAME_LENGTH];
	} skulls[SKULLS_DROPPED_MAX];
};

static struct skulls_finding *finding;
/* the hum each dropped skull plays (a sound of this machine's, not the
game state's: forgotten when a revert takes the game back), and when */
static long skull_hums[SKULLS_DROPPED_MAX] = { NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE };
static long skull_hums_time;

/* the skulls found in the level, not yet kept */
static unsigned long found_mask(void)
{
	return finding ? finding->found_mask : 0;
}

static void finding_reset(void)
{
	short index;

	finding->total = NONE;
	finding->deaths = 0;
	finding->kills = 0;
	finding->dropped = 0;
	finding->found_mask = 0;
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
	unsigned long mask = ~skulls_unlocked_mask() & ~finding->found_mask & ((1UL << NUMBER_OF_SKULLS) - 1);
	short slot;

	for (slot = 0; slot < SKULLS_DROPPED_MAX; slot++)
	{
		if (finding->skulls[slot].object_index != NONE && finding->skulls[slot].skull != NONE)
			mask &= ~(1UL << finding->skulls[slot].skull);
	}
	return mask;
}

/* one of the mask's skulls, at random */
static int skull_pick(unsigned long mask)
{
	int count = 0;
	int skull;
	int chosen;

	for (skull = 0; skull < NUMBER_OF_SKULLS; skull++)
		count += (mask >> skull) & 1;
	if (!count)
		return NONE;
	chosen = (int)(local_random() % (unsigned long)count);
	for (skull = 0; skull < NUMBER_OF_SKULLS; skull++)
	{
		if (((mask >> skull) & 1) && chosen-- == 0)
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
	drop = finding->kills == 0 || (long)(local_random() % (unsigned long)left) == 0;
	finding->kills++;
	if (!drop)
		return;
	for (slot = 0; slot < SKULLS_DROPPED_MAX && finding->skulls[slot].object_index != NONE; slot++)
		;
	if (slot == SKULLS_DROPPED_MAX)
		return;
	skull = skull_pick(skulls_to_find());
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

void skulls_tick(void)
{
	long now;
	short slot;

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

	if (slot == NONE)
		return FALSE;
	skull = finding->skulls[slot].skull;
	/* (on for the level from now on; kept once it is finished) */
	finding->found_mask |= 1UL << skull;
	if (skull_hums[slot] != NONE)
		sound_stop_impulse(skull_hums[slot]);
	finding->skulls[slot].object_index = NONE;
	finding->skulls[slot].skull = NONE;
	object_delete(object_index);
	own_sound_play(_own_sound_skull_pickup, unit_index);
	error(_error_silent, "skulls: %s taken", skull_descriptions[skull].key);
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
		hud_print_message(local_player_index, L"Death will take your prize away.");
		hud_print_message(local_player_index, L"Finish the level without dying to unlock it.");
		snprintf(text, sizeof(text), "%s SKULL picked up.", skull_descriptions[skull].label);
		for (character = 0; text[character] && character < (short)NUMBEROF(line) - 1; character++)
			line[character] = (wchar_t)(unsigned char)text[character];
		line[character] = 0;
		hud_print_message(local_player_index, line);
	}
	return TRUE;
}

/* ---------- a unit's death */

void skulls_unit_killed(long unit_index, int headshot, long killer_player_index)
{
	/* (the Master Chief Collection's confetti and cheer) */
	if (headshot && skull_active(_skull_grunt_birthday_party) && skulls_unit_is_grunt(unit_index))
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
}
