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
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
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
in two lines of the skulls screen's (42 characters at most) */
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

static void party_sound_add(void);
static void party_sound_remove(void);

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
	/* (Grunt Birthday Party's cheer, a tag of a campaign level's) */
	if ((level_mask & (1UL << _skull_grunt_birthday_party)) && scenario_tag_index != NONE &&
		scenario_definition_get(scenario_tag_index)->type == _scenario_type_solo)
	{
		party_sound_add();
	}
}

void skulls_level_unloaded(void)
{
	party_sound_remove();
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

/* ---------- Grunt Birthday Party */

/* its cheer: a WAV of 16-bit PCM (port/assets/sounds, tools/embed_assets.py),
made a sound tag of the level's while it plays with the skull: a grunt
dialog sound of the map's copied, with one permutation of these samples,
which the sound cache reads from memory (skulls_sound_read,
xbox_sound_cache.c). Not of the dialog's class: a unit speaks one sound at
a time, and the grunt's death cry, right after, would cut the cheer off;
an explosion's class, as loud and as far heard, plays beside it */
extern const unsigned int skulls_party_sound[];
extern const unsigned long skulls_party_sound_size;
extern void *cache_files_tag_instances(long *count);
extern void cache_files_set_tag_instances(void *instances, long count);

#define SOUND_COMPRESSION_NONE 0
#define SOUND_COMPRESSION_XBOX_ADPCM 1
#define PARTY_SOUND_RATE 22050
#define PARTY_SOUND_NAME "sound\\port\\skulls\\grunt birthday party"
/* sound_classes.c's projectile_detonation */
#define PARTY_SOUND_CLASS 1
/* full loudness this near, in world units, heard this far */
#define PARTY_SOUND_MINIMUM_DISTANCE 2.0f
#define PARTY_SOUND_MAXIMUM_DISTANCE 40.0f

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

/* the dialog sounds the cheer is made from, the first the map has */
static char const *const party_sound_templates[] =
{
	"sound\\dialog\\grunt\\conditional\\combat2\\postcombatactions\\celebration",
	"sound\\dialog\\grunt\\conditional\\combat2\\killingpeople\\killed_enemy",
	"sound\\dialog\\grunt\\conditional\\combat2\\friendsdying\\friend_killed_by_enemy_player",
};

static struct
{
	/* the samples, made Xbox ADPCM once (the game's allocator: kept) */
	boolean encoded;
	byte *samples;
	unsigned long sample_bytes;
	long milliseconds;
	/* the level's tag, while it has one */
	struct sound_definition definition;
	struct sound_pitch_range range;
	struct sound_permutation permutation;
	struct skulls_tag_instance *grown_instances;
	struct skulls_tag_instance *saved_instances;
	long saved_count;
	long tag_index;
} party_sound = { FALSE, NULL, 0, 0 };

static unsigned long read_le32(byte const *bytes)
{
	return (unsigned long)bytes[0] | ((unsigned long)bytes[1] << 8) | ((unsigned long)bytes[2] << 16) |
		((unsigned long)bytes[3] << 24);
}

/* the WAV's samples, encoded; FALSE when it has none this build plays */
static boolean party_sound_encode(void)
{
	byte const *file = (byte const *)skulls_party_sound;
	unsigned long size = skulls_party_sound_size;
	unsigned long offset = 12;
	byte const *data = NULL;
	unsigned long data_bytes = 0;
	long channels = 0;
	long rate = 0;
	long bits = 0;

	if (party_sound.encoded)
		return party_sound.samples != NULL;
	party_sound.encoded = TRUE;
	if (size < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4))
	{
		error(_error_silent, "skulls: the birthday party's sound is not a WAV");
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
		error(_error_silent, "skulls: the birthday party's sound is not 16-bit PCM, mono or stereo, at 22 or 44 kHz");
		return FALSE;
	}
	party_sound.samples = custom_edition_sounds_encode(data, (long)data_bytes, SOUND_COMPRESSION_NONE, FALSE,
		channels, rate, PARTY_SOUND_RATE, &party_sound.sample_bytes);
	if (!party_sound.samples || party_sound.sample_bytes > MAXIMUM_SOUND_DATA_SIZE)
	{
		error(_error_silent, "skulls: the birthday party's sound could not be encoded");
		party_sound.samples = NULL;
		return FALSE;
	}
	party_sound.milliseconds = (long)((data_bytes / (unsigned long)(2 * channels)) * 1000UL / (unsigned long)rate);
	return TRUE;
}

/* the level's tag of the cheer, appended to its tags (after the randomizer's,
which the level's unload restores after this) */
static void party_sound_add(void)
{
	long existing = 0;
	struct skulls_tag_instance *instances = (struct skulls_tag_instance *)cache_files_tag_instances(&existing);
	struct skulls_tag_instance *template_instance;
	long template_index = NONE;
	long salt = 0;
	long index;

	party_sound.tag_index = NONE;
	if (!instances || existing <= 0 || existing >= UNSIGNED_SHORT_MAX)
		return;
	for (index = 0; index < (long)NUMBEROF(party_sound_templates) && template_index == NONE; index++)
		template_index = tag_loaded(SOUND_DEFINITION_TAG, party_sound_templates[index]);
	if (template_index == NONE)
	{
		error(_error_silent, "skulls: no grunt dialog to make the birthday party's sound of");
		return;
	}
	if (!party_sound_encode())
		return;
	party_sound.grown_instances = (struct skulls_tag_instance *)malloc((unsigned long)(existing + 1) *
		sizeof(*party_sound.grown_instances));
	if (!party_sound.grown_instances)
		return;
	memcpy(party_sound.grown_instances, instances, (unsigned long)existing * sizeof(*instances));
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (long)((unsigned long)instances[index].tag_index >> 16);

		if (instance_salt > salt)
			salt = instance_salt;
	}
	party_sound.tag_index = ((salt + 1) << 16) | existing;

	/* the template's definition, its samples this one permutation */
	party_sound.definition = *sound_definition_get(template_index);
	party_sound.definition.sound_class = PARTY_SOUND_CLASS;
	party_sound.definition.minimum_distance = PARTY_SOUND_MINIMUM_DISTANCE;
	party_sound.definition.maximum_distance = PARTY_SOUND_MAXIMUM_DISTANCE;
	party_sound.definition.gain_modifier = 1.0f;
	party_sound.definition.zero_gain_modifier = 1.0f;
	party_sound.definition.one_gain_modifier = 1.0f;
	party_sound.definition.zero_pitch_modifier = 1.0f;
	party_sound.definition.one_pitch_modifier = 1.0f;
	party_sound.definition.zero_skip_fraction_modifier = 0.0f;
	party_sound.definition.one_skip_fraction_modifier = 0.0f;
	/* (heard the same all round) */
	party_sound.definition.inner_cone_angle = 2.0f * _pi;
	party_sound.definition.outer_cone_angle = 2.0f * _pi;
	party_sound.definition.outer_cone_gain = 1.0f;
	party_sound.definition.skip_fraction = 0.0f;
	party_sound.definition.random_pitch_bounds.lower = 1.0f;
	party_sound.definition.random_pitch_bounds.upper = 1.0f;
	party_sound.definition.encoding = 0;
	party_sound.definition.compression = SOUND_COMPRESSION_XBOX_ADPCM;
	party_sound.definition.sample_rate = PARTY_SOUND_RATE == 44100 ? 1 : 0;
	party_sound.definition.promotion_sound.index = NONE;
	party_sound.definition.promotion_count = 0;
	party_sound.definition.promotion_counter = 0;
	party_sound.definition.promotion_time = 0;
	party_sound.definition.scripting_time = 0;
	party_sound.definition.scripting_sound_index = NONE;
	party_sound.definition.longest_permutation_length = party_sound.milliseconds;
	party_sound.definition.pitch_ranges.count = 1;
	party_sound.definition.pitch_ranges.address = &party_sound.range;
	party_sound.definition.pitch_ranges.definition = NULL;
	csmemset(&party_sound.range, 0, sizeof(party_sound.range));
	party_sound.range.natural_pitch = 1.0f;
	party_sound.range.bend_bounds.lower = 1.0f;
	party_sound.range.bend_bounds.upper = 1.0f;
	party_sound.range.actual_permutation_count = 1;
	party_sound.range.playback_rate = 1.0f;
	party_sound.range.played_permutation_mask = 0xFFFFFFFF;
	party_sound.range.previous_permutation_index = NONE;
	party_sound.range.forced_permutation_index = NONE;
	party_sound.range.permutations.count = 1;
	party_sound.range.permutations.address = &party_sound.permutation;
	party_sound.range.permutations.definition = NULL;
	csmemset(&party_sound.permutation, 0, sizeof(party_sound.permutation));
	party_sound.permutation.gain = 1.0f;
	party_sound.permutation.compression = SOUND_COMPRESSION_XBOX_ADPCM;
	party_sound.permutation.next_permutation_index = NONE;
	party_sound.permutation.samples.size = (long)party_sound.sample_bytes;
	party_sound.permutation.samples.address = party_sound.samples;
	/* (the sound cache's: block, address, tag: xbox_sound_cache.c) */
	party_sound.permutation.unknown0 = NONE;
	party_sound.permutation.unknown2 = (unsigned long)party_sound.tag_index;
	party_sound.permutation.unknown3 = (unsigned long)party_sound.tag_index;

	template_instance = &instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(template_index)];
	party_sound.grown_instances[existing] = *template_instance;
	party_sound.grown_instances[existing].tag_index = party_sound.tag_index;
	party_sound.grown_instances[existing].name = PARTY_SOUND_NAME;
	party_sound.grown_instances[existing].base_address = &party_sound.definition;
	party_sound.saved_instances = instances;
	party_sound.saved_count = existing;
	cache_files_set_tag_instances(party_sound.grown_instances, existing + 1);
	error(_error_silent, "skulls: the birthday party's sound is tag %08lx (%lu bytes, %ld ms), made of %s",
		(unsigned long)party_sound.tag_index, party_sound.sample_bytes, party_sound.milliseconds,
		tag_get_name(template_index));
}

static void party_sound_remove(void)
{
	if (party_sound.saved_instances)
		cache_files_set_tag_instances(party_sound.saved_instances, party_sound.saved_count);
	if (party_sound.grown_instances)
		free(party_sound.grown_instances);
	party_sound.grown_instances = NULL;
	party_sound.saved_instances = NULL;
	party_sound.saved_count = 0;
	party_sound.tag_index = NONE;
}

unsigned char skulls_sound_read(struct sound_permutation const *permutation, void *buffer)
{
	if (permutation != &party_sound.permutation || !party_sound.samples)
		return FALSE;
	memcpy(buffer, party_sound.samples, (size_t)party_sound.sample_bytes);
	return TRUE;
}

/* the confetti: the shirt scraps of the maps' cloth debris, and the sparks of
their impacts, each a colour of the party's, thrown up from the grunt's head
(the local seed: only what this machine shows) */
#define CONFETTI_SCRAPS 40
#define CONFETTI_SPARKS 16

static real_rgb_color const confetti_colors[] =
{
	{ 1.0f, 0.15f, 0.2f }, { 1.0f, 0.8f, 0.1f }, { 0.2f, 0.9f, 0.3f },
	{ 0.15f, 0.55f, 1.0f }, { 0.85f, 0.25f, 1.0f }, { 1.0f, 0.5f, 0.1f },
};

static void confetti_throw(long definition_index, short count, real_point3d const *center, real speed,
	real radius_lower, real radius_upper)
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
		data.color.rgb = confetti_colors[local_random() % NUMBEROF(confetti_colors)];
		particle_new(&data);
	}
}

static void party_confetti(long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	real_point3d center;

	if (!unit)
		return;
	center = unit->object.bounding_sphere_center;
	center.z += unit->object.bounding_sphere_radius * 0.5f;
	/* (each a share of its particle's own size: a whole scrap of shirt is
	bigger than the grunt) */
	confetti_throw(tag_loaded(PARTICLE_TAG, "effects\\particles\\solid\\debris shirt"), CONFETTI_SCRAPS, &center,
		1.6f, 0.05f, 0.09f);
	confetti_throw(tag_loaded(PARTICLE_TAG, "effects\\particles\\flash\\sparks impact"), CONFETTI_SPARKS, &center,
		2.2f, 0.125f, 0.2f);
}

void skulls_unit_killed(long unit_index, int headshot)
{
	/* (the Master Chief Collection's confetti and cheer) */
	if (!headshot || !skull_active(_skull_grunt_birthday_party) || !skulls_unit_is_grunt(unit_index))
		return;
	party_confetti(unit_index);
	if (party_sound.tag_index != NONE)
		object_impulse_sound_new(unit_index, party_sound.tag_index, NONE, global_origin3d, global_forward3d, 1.0f);
}

void skulls_unit_died(long unit_index)
{
	/* (a plasma grenade's explosion: its damage the host's alone, as an
	effect's damage is) */
	if (skull_active(_skull_grunt_funeral) && skulls_unit_is_grunt(unit_index))
		effect_at_unit("weapons\\plasma grenade\\effects\\explosion", unit_index);
}
