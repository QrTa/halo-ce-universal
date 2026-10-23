/*
TAG_INJECTOR.C

The tag injector (tag_injector.h): Settings > Gameplay > Tag Injector lists
the Custom Edition maps, a map's weapons, AI, equipment, vehicles, bipeds
and scenery, and takes a weapon or an actor variant out of it, with every
tag it names, as a tag bundle (tag_bundle.h) into a profile
(port/linux/src/port_tag_profiles.c), from which the randomizer's Go Crazy
adds it to a campaign level and its pools (randomizer.c).

A map's tags are taken out as the game would play them: loaded, converted
and checked by its loader (custom_edition_cache_bundle_open), its sounds
those of the tag decoded to Xbox ADPCM, which this build plays; its models
are kept as Halo PC's, and its bitmaps' pixels as Halo PC lays them out. A
tag is listed by its group and path, and the menu loads its map again to
take one out, with the sounds of the tags it names decoded.

A profile's tags are added to a level after its own (and those the
randomizer copied from the other campaign maps) as the randomizer adds its
own: each tag of a bundle the level has not got, by its group and path, is
appended to the tag table, the next salt after the table's, in the
profile's order; one it has is the level's, and so is a tag another bundle
added before. The bundle's bytes stay as they were loaded, and are the
tags': each reference and tag index is given the tag of the level it names
now, and a sound's permutations the tag their samples are read for. A
gbxmodel is given this build's layout and buffers as a Custom Edition map's
are (custom_edition_injected_model_convert), and its group the models'. A
bitmap's pixels and a sound's samples are read from the bundle's file data
(tag_injector_read, from cache_file_read), a bitmap's laid out for the
texture cache as a Custom Edition map's are, with its channels where Halo PC
keeps them (custom_edition_loaded_reordered_bitmaps_find finds them in the
level's tags, the bundles' with them). A bundle whose models cannot be made,
or whose bitmaps cannot be drawn, is left out, and the tags of none are
added when there is no room for them all.

Only a machine playing alone adds them (randomizer.c): the tags are this
machine's, and a network game's machines must have the same tag table.
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "bitmaps/bitmap_group.h"
#include "models/model_definitions.h"
#include "rasterizer/rasterizer_swizzle.h"
#include "sound/sound_definitions.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "cache_file_formats.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "tag_schema.h"
#include "tag_bundle.h"
#include "tag_injector.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	GBXMODEL_GROUP_TAG = 'mod2',
	MODEL_GROUP_TAG = 'mode',

	/* the most maps listed, and tags of a category */
	MAXIMUM_TAG_INJECTOR_MAPS = 512,
	/* the most bytes of bundles' tags a level holds (their pixels, samples
	and vertices are read from their files), and bundles */
	MAXIMUM_INJECTED_BYTES = 0x4000000,
	MAXIMUM_INJECTED_BUNDLES = 64,
	/* the tags of a level, as datum indices count them */
	MAXIMUM_TAG_COUNT = 0xFFFF,
	MAXIMUM_WALK_DEPTH = 32,
	MAP_PATH_SIZE = 256,
	/* a tag's size not looked for yet (tag_injector_tag_size) */
	TAG_SIZE_UNKNOWN = -2,
};

/* ---------- structures */

/* a tag of the tag table (cache_files.c's, as randomizer.c has it) */
struct injector_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

/* a Custom Edition map's tags' header, as tag_bundle.c has it: the table */
struct injector_map_header
{
	struct injector_tag_instance *instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
};

typedef char verify_injector_tag_instance_size[sizeof(struct injector_tag_instance) == 0x20 ? 1 : -1];

/* a map of the menu's list */
struct injector_map
{
	short display_index;
	boolean campaign;
};

/* a bundle of a profile added to the level: its file, open while the level
is (port_tag_profiles_bundle_open), its tags, and the tag of the level each
of them is (NONE: none). Its file data stays in its file */
struct injected_bundle
{
	long handle;
	byte *bytes;
	unsigned long size;
	struct tag_bundle_loaded loaded;
	long *datums;
	/* left out (its models could not be made, or its bitmaps drawn) */
	boolean refused;
};

/* ---------- globals */

/* (cache_files.c's) */
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);
/* (port_config.c's) */
int config_boolean(char const *name);
char const *config_string(char const *name);
/* (port/linux/src/xbox_textures.c's) */
void halo_custom_edition_texels_channels(void const *texels, unsigned char channel_order);

/* (port/linux/src/port_tag_profiles.c's: port_tag_profiles.h) */
long port_tag_profiles_scan(void);
long port_tag_profiles_count(void);
char const *port_tag_profiles_name(long profile);
long port_tag_profiles_find(char const *folder);
long port_tag_profiles_tag_count(long profile);
char const *port_tag_profiles_tag_map(long profile, long tag);
unsigned long port_tag_profiles_tag_group(long profile, long tag);
char const *port_tag_profiles_tag_name(long profile, long tag);
long port_tag_profiles_tag_find(long profile, unsigned long group, char const *name);
int port_tag_profiles_tag_add(long profile, char const *map, unsigned long group, char const *name,
	void const *bundle, unsigned long size);
long port_tag_profiles_bundle_open(long profile, long tag, unsigned long *size);
int port_tag_profiles_bundle_read_at(long handle, unsigned long offset, void *buffer, unsigned long size);
void port_tag_profiles_bundle_close(long handle);

static struct
{
	char const *name;
	unsigned long group_tag;
	boolean addable;
} const tag_injector_categories[NUMBER_OF_TAG_INJECTOR_CATEGORIES] =
{
	{ "WEAPONS", 'weap', TRUE },
	{ "AI", 'actv', TRUE },
	{ "EQUIPMENT", 'eqip', FALSE },
	{ "VEHICLES", 'vehi', FALSE },
	{ "BIPEDS", 'bipd', FALSE },
	{ "SCENERY", 'scen', FALSE },
};

/* the menu's: the maps, the map open, and its tags of each category (their
absolute indices), each one's size (TAG_SIZE_UNKNOWN until asked) and the
tag that keeps it from being taken out (NONE) */
static struct
{
	struct injector_map maps[MAXIMUM_TAG_INJECTOR_MAPS];
	short map_count;
	short opened;
	char path[MAP_PATH_SIZE];
	char map_name[64];
	struct injector_map_header *header;
	long *tags[NUMBER_OF_TAG_INJECTOR_CATEGORIES];
	long *sizes[NUMBER_OF_TAG_INJECTOR_CATEGORIES];
	long *problems[NUMBER_OF_TAG_INJECTOR_CATEGORIES];
	long tag_counts[NUMBER_OF_TAG_INJECTOR_CATEGORIES];
} tag_injector_menu = { { { 0 } }, 0, NONE };

/* the level's: the bundles added, the table as it was and as it is now, and
of the tags added (from first_injected on), the bundle each is of */
static struct
{
	struct injected_bundle bundles[MAXIMUM_INJECTED_BUNDLES];
	long bundle_count;
	struct injector_tag_instance *saved_instances;
	long saved_count;
	struct injector_tag_instance *grown_instances;
	long first_injected;
	long injected_count;
	short *injected_bundle;
	boolean bitmaps_added;
} tag_injector_level;

/* ---------- private code */

static unsigned long read_unsigned_long(
	byte const *address)
{
	unsigned long value;

	memcpy(&value, address, sizeof(value));
	return value;
}

static struct tag_schema_definition const *schema_of(
	unsigned long group_tag)
{
	struct tag_schema_group const *const *list;
	struct tag_schema_group const *group;

	for (list = tag_schema_group_lists; *list; list++)
	{
		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group->definition;
		}
	}

	return NULL;
}

/* the group the level knows a tag of a bundle by: a gbxmodel is a model */
static unsigned long played_group(
	unsigned long group_tag)
{
	return group_tag == GBXMODEL_GROUP_TAG ? MODEL_GROUP_TAG : group_tag;
}

static void reason_set(
	char *reason,
	long reason_size,
	char const *text)
{
	if (reason && reason_size > 0)
		snprintf(reason, (size_t)reason_size, "%s", text);
}

/* ---------- the menu's */

static void menu_tags_free(
	void)
{
	short category;

	for (category = 0; category < NUMBER_OF_TAG_INJECTOR_CATEGORIES; category++)
	{
		/* (the game's free does not take NULL) */
		if (tag_injector_menu.tags[category])
			free(tag_injector_menu.tags[category]);
		if (tag_injector_menu.sizes[category])
			free(tag_injector_menu.sizes[category]);
		if (tag_injector_menu.problems[category])
			free(tag_injector_menu.problems[category]);
		tag_injector_menu.tags[category] = NULL;
		tag_injector_menu.sizes[category] = NULL;
		tag_injector_menu.problems[category] = NULL;
		tag_injector_menu.tag_counts[category] = 0;
	}
}

static char const *menu_tag_name(
	long absolute)
{
	struct injector_map_header const *header = tag_injector_menu.header;

	if (!header || absolute < 0 || absolute >= header->tag_count || !header->instances[absolute].name)
		return "";
	return header->instances[absolute].name;
}

static int menu_tag_less(
	void const *left,
	void const *right)
{
	return _stricmp(menu_tag_name(*(long const *)left), menu_tag_name(*(long const *)right));
}

/* the absolute index of a tag of a category of the map open, or NONE */
static long menu_tag(
	short category,
	long index)
{
	if (tag_injector_menu.opened == NONE || category < 0 || category >= NUMBER_OF_TAG_INJECTOR_CATEGORIES ||
		index < 0 || index >= tag_injector_menu.tag_counts[category])
		return NONE;
	return tag_injector_menu.tags[category][index];
}

/* the map's file in the Custom Edition maps folders, into path */
static boolean menu_map_path(
	short map,
	char *path)
{
	char const *level_name;

	if (map < 0 || map >= tag_injector_menu.map_count)
		return FALSE;
	level_name = custom_edition_maps_level_name(tag_injector_menu.maps[map].display_index);
	return level_name && custom_edition_map_file_path(level_name, path);
}

/* writes a bundle into a growing buffer (tag_bundle_write's) */
struct bundle_buffer
{
	byte *bytes;
	unsigned long size;
	unsigned long capacity;
};

static boolean bundle_buffer_write(
	void *context,
	void const *bytes,
	unsigned long size)
{
	struct bundle_buffer *buffer = context;

	if (size > 0x7FFFFFFFUL - buffer->size)
		return FALSE;
	if (buffer->size + size > buffer->capacity)
	{
		unsigned long capacity = buffer->capacity ? buffer->capacity : 0x100000;
		byte *grown;

		while (capacity < buffer->size + size)
		{
			if (capacity > 0x3FFFFFFFUL)
				return FALSE;
			capacity *= 2;
		}
		grown = buffer->bytes ? realloc(buffer->bytes, capacity) : malloc(capacity);
		if (!grown)
			return FALSE;
		buffer->bytes = grown;
		buffer->capacity = capacity;
	}
	memcpy(buffer->bytes + buffer->size, bytes, size);
	buffer->size += size;

	return TRUE;
}

/* ---------- the level's */

/* the bundle's tag of `bundle_index`: its group and path */
static unsigned long bundle_tag_group(
	struct tag_bundle_loaded const *loaded,
	long bundle_index)
{
	return read_unsigned_long(loaded->tags + bundle_index * TAG_BUNDLE_TAG_BYTES + TAG_BUNDLE_TAG_GROUP_OFFSET);
}

static char const *bundle_tag_name(
	struct tag_bundle_loaded const *loaded,
	long bundle_index)
{
	return loaded->names +
		read_unsigned_long(loaded->tags + bundle_index * TAG_BUNDLE_TAG_BYTES + TAG_BUNDLE_TAG_NAME_OFFSET);
}

static byte *bundle_tag_root(
	struct tag_bundle_loaded const *loaded,
	long bundle_index)
{
	return loaded->data +
		read_unsigned_long(loaded->tags + bundle_index * TAG_BUNDLE_TAG_BYTES + TAG_BUNDLE_TAG_ROOT_OFFSET);
}

/* the level's tag of a group and path, the bundles added before this
one's too, or NONE */
static long level_tag(
	unsigned long group_tag,
	char const *name)
{
	return name && name[0] ? tag_loaded((long)played_group(group_tag), name) : NONE;
}

/* the tags the level does not keep from another map: a scenario, a
structure, the globals (randomizer.c's) */
static boolean group_left_out(
	unsigned long group_tag)
{
	return group_tag == 'scnr' || group_tag == 'sbsp' || group_tag == 'matg';
}

/* whether every bitmap of a bundle's bitmap tags can be drawn from Halo
PC's pixels */
static boolean bundle_bitmaps_drawable(
	struct injected_bundle *bundle)
{
	long bundle_index;

	for (bundle_index = 0; bundle_index < bundle->loaded.tag_count; bundle_index++)
	{
		struct bitmap_group *group;
		long bitmap_index;

		if (bundle_tag_group(&bundle->loaded, bundle_index) != BITMAP_GROUP_TAG ||
			level_tag(BITMAP_GROUP_TAG, bundle_tag_name(&bundle->loaded, bundle_index)) != NONE)
			continue;
		group = (struct bitmap_group *)bundle_tag_root(&bundle->loaded, bundle_index);
		for (bitmap_index = 0; bitmap_index < group->bitmaps.count; bitmap_index++)
		{
			struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, bitmap_index, struct bitmap_data);

			if (!custom_edition_bitmap_drawable(bitmap))
			{
				error(_error_silent, "tag injector: bitmap %ld of '%s' cannot be drawn", bitmap_index,
					bundle_tag_name(&bundle->loaded, bundle_index));
				return FALSE;
			}
		}
	}

	return TRUE;
}

/* reads `size` bytes at `offset` in a bundle's file data, from its file */
static boolean bundle_file_read(
	void *context,
	unsigned long offset,
	unsigned long size,
	void *buffer)
{
	struct injected_bundle const *bundle = context;

	return offset <= bundle->loaded.file_data_size && size <= bundle->loaded.file_data_size - offset &&
		port_tag_profiles_bundle_read_at(bundle->handle, bundle->loaded.file_data_offset + offset, buffer, size);
}

/* a bundle's gbxmodels the level has not got made this build's models;
FALSE when one cannot be */
static boolean bundle_models_convert(
	struct injected_bundle *bundle)
{
	long bundle_index;

	for (bundle_index = 0; bundle_index < bundle->loaded.tag_count; bundle_index++)
	{
		char const *name = bundle_tag_name(&bundle->loaded, bundle_index);
		byte *entry = (byte *)bundle->loaded.tags + bundle_index * TAG_BUNDLE_TAG_BYTES;

		if (bundle_tag_group(&bundle->loaded, bundle_index) != GBXMODEL_GROUP_TAG || level_tag(MODEL_GROUP_TAG, name) != NONE)
			continue;
		if (!custom_edition_injected_model_convert((struct model *)bundle_tag_root(&bundle->loaded, bundle_index), name,
			bundle->loaded.file_data_size, bundle_file_read, bundle))
		{
			return FALSE;
		}
		/* (a model now, as the level's are: its schema the Xbox's) */
		memcpy(entry + TAG_BUNDLE_TAG_GROUP_OFFSET, &(unsigned long){ MODEL_GROUP_TAG }, sizeof(unsigned long));
	}

	return TRUE;
}

/* the level's tag for a bundle's tag index, or NONE */
static long bundle_datum(
	struct injected_bundle const *bundle,
	long bundle_index)
{
	return bundle_index >= 0 && bundle_index < bundle->loaded.tag_count ? bundle->datums[bundle_index] : NONE;
}

/* gives each reference and tag index of an element of a tag added the
level's tag it names */
static void retarget_fields(
	struct injected_bundle const *bundle,
	byte *element,
	struct tag_schema_definition const *definition,
	short depth)
{
	struct tag_schema_field const *field;

	if (depth >= MAXIMUM_WALK_DEPTH || !element || !definition)
		return;
	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		short item;

		for (item = 0; item < field->count; item++)
		{
			byte *place = element + field->offset + item * field->size;

			switch (field->type)
			{
			case _tag_schema_reference:
			{
				struct tag_reference *reference = (struct tag_reference *)place;
				long datum = bundle_datum(bundle, reference->index);

				reference->index = datum;
				reference->group_tag = played_group(reference->group_tag);
				if (datum == NONE)
				{
					/* (the bundle's names end with an empty one) */
					reference->name = (char *)bundle->loaded.names + bundle->loaded.names_size - 1;
					reference->name_length = 0;
				}
				else
				{
					reference->name = tag_get_name(datum);
					reference->name_length = reference->name ? (long)strlen(reference->name) : 0;
				}
				break;
			}

			case _tag_schema_tag_index:
				*(long *)place = bundle_datum(bundle, *(long *)place);
				break;

			case _tag_schema_struct:
				retarget_fields(bundle, place, field->definition, (short)(depth + 1));
				break;

			case _tag_schema_block:
			{
				struct tag_block *block = (struct tag_block *)place;
				struct tag_schema_definition const *element = field->definition;
				long index;

				for (index = 0; element && block->address && index < block->count; index++)
				{
					retarget_fields(bundle, (byte *)block->address + index * element->size, element,
						(short)(depth + 1));
				}
				break;
			}

			default:
				break;
			}
		}
	}
}

/* a sound added: the tag its permutations' samples are read for (the sound
cache's cache_tag_index), which the bundle still has as its map's */
static void sound_permutations_retarget(
	struct sound_definition *sound,
	long datum)
{
	long range_index;

	for (range_index = 0; range_index < sound->pitch_ranges.count; range_index++)
	{
		struct sound_pitch_range *range = TAG_BLOCK_GET_ELEMENT(&sound->pitch_ranges, range_index,
			struct sound_pitch_range);
		long permutation_index;

		for (permutation_index = 0; permutation_index < range->permutations.count; permutation_index++)
		{
			struct sound_permutation *permutation = TAG_BLOCK_GET_ELEMENT(&range->permutations, permutation_index,
				struct sound_permutation);

			permutation->unknown2 = (unsigned long)datum;
			permutation->unknown3 = (unsigned long)datum;
		}
	}
}

static void level_bundles_free(
	void)
{
	long index;

	for (index = 0; index < tag_injector_level.bundle_count; index++)
	{
		struct injected_bundle *bundle = &tag_injector_level.bundles[index];

		if (bundle->bytes)
			free(bundle->bytes);
		if (bundle->datums)
			free(bundle->datums);
		port_tag_profiles_bundle_close(bundle->handle);
	}
	memset(tag_injector_level.bundles, 0, sizeof(tag_injector_level.bundles));
	tag_injector_level.bundle_count = 0;
}

/* opens a profile's bundle and reads and checks its tags; FALSE after
logging why (what it holds is the caller's to let go: level_bundles_free) */
static boolean level_bundle_read(
	long profile,
	long tag,
	struct injected_bundle *bundle,
	unsigned long *total)
{
	enum tag_bundle_status status;
	char const *name = port_tag_profiles_tag_name(profile, tag);
	byte header[TAG_BUNDLE_HEADER_BYTES];
	unsigned long tags_size;

	memset(bundle, 0, sizeof(*bundle));
	bundle->handle = port_tag_profiles_bundle_open(profile, tag, &bundle->size);
	if (bundle->handle == NONE)
	{
		error(_error_silent, "tag injector: the bundle of %s is missing", name);
		return FALSE;
	}
	/* (its tags alone: all but its file data) */
	if (bundle->size < sizeof(header) || !port_tag_profiles_bundle_read_at(bundle->handle, 0, header, sizeof(header)))
	{
		error(_error_silent, "tag injector: the bundle of %s cannot be read", name);
		return FALSE;
	}
	tags_size = tag_bundle_file_data_offset(header);
	if (tags_size < sizeof(header) || tags_size > bundle->size || tags_size > MAXIMUM_INJECTED_BYTES - *total)
	{
		error(_error_silent, "tag injector: %s is not a bundle, or its tags and those before it are past the %d MB "
			"a level adds", name, MAXIMUM_INJECTED_BYTES >> 20);
		return FALSE;
	}
	bundle->bytes = malloc(tags_size);
	if (!bundle->bytes || !port_tag_profiles_bundle_read_at(bundle->handle, 0, bundle->bytes, tags_size))
	{
		error(_error_silent, "tag injector: the bundle of %s cannot be read", name);
		return FALSE;
	}
	status = tag_bundle_load_tags(bundle->bytes, tags_size, bundle->size, &bundle->loaded);
	if (status != _tag_bundle_status_ok)
	{
		error(_error_silent, "tag injector: the bundle of %s is not one this build can play: %s (its tag %ld)", name,
			tag_bundle_status_describe(status), bundle->loaded.problem_tag_index);
		return FALSE;
	}
	bundle->datums = malloc((unsigned long)bundle->loaded.tag_count * sizeof(long));
	if (!bundle->datums)
		return FALSE;
	*total += tags_size;

	return TRUE;
}

/* ---------- public code */

boolean tag_injector_maps_available(
	char *reason,
	long reason_size)
{
	if (!halo_custom_edition_tag_cache())
	{
		reason_set(reason, reason_size, "Custom Edition maps are off (game.custom_edition in config.toml), or cannot run here.");
		return FALSE;
	}
	if (custom_edition_cache_tags_loaded())
	{
		reason_set(reason, reason_size, "A Custom Edition map is being played.");
		return FALSE;
	}
	reason_set(reason, reason_size, "");

	return TRUE;
}

short tag_injector_maps_find(
	void)
{
	short pass;

	custom_edition_maps_look_again();
	tag_injector_menu.map_count = 0;
	for (pass = 0; pass < 2; pass++)
	{
		boolean campaign = pass == 0;
		short count = custom_edition_maps_count(campaign);
		short index;

		for (index = 0; index < count && tag_injector_menu.map_count < MAXIMUM_TAG_INJECTOR_MAPS; index++)
		{
			struct injector_map *map = &tag_injector_menu.maps[tag_injector_menu.map_count];

			map->display_index = custom_edition_maps_display_index_of(campaign, index);
			map->campaign = campaign;
			/* (Halo PC's own multiplayer maps are Custom Edition maps too) */
			if (map->display_index != NONE && custom_edition_maps_level_name(map->display_index))
				tag_injector_menu.map_count++;
		}
	}

	return tag_injector_menu.map_count;
}

short tag_injector_map_count(
	void)
{
	return tag_injector_menu.map_count;
}

wchar_t const *tag_injector_map_name(
	short map)
{
	wchar_t const *name = map >= 0 && map < tag_injector_menu.map_count ?
		custom_edition_maps_name(tag_injector_menu.maps[map].display_index) : NULL;

	return name ? name : L"";
}

char const *tag_injector_map_folder(
	short map)
{
	struct injector_map const *found;
	short count;
	short index;

	if (map < 0 || map >= tag_injector_menu.map_count)
		return "";
	found = &tag_injector_menu.maps[map];
	count = custom_edition_maps_count(found->campaign);
	for (index = 0; index < count; index++)
	{
		if (custom_edition_maps_display_index_of(found->campaign, index) == found->display_index)
		{
			char const *category = custom_edition_maps_category(found->campaign, index);

			return category ? category : "";
		}
	}

	return "";
}

boolean tag_injector_map_campaign(
	short map)
{
	return map >= 0 && map < tag_injector_menu.map_count && tag_injector_menu.maps[map].campaign;
}

boolean tag_injector_map_open(
	short map)
{
	struct injector_map_header *header;
	char const *level_name;
	short category;
	long absolute;

	tag_injector_map_close();
	if (!menu_map_path(map, tag_injector_menu.path))
	{
		error(_error_silent, "tag injector: map %d has no file", map);
		return FALSE;
	}
	header = custom_edition_cache_bundle_open(tag_injector_menu.path, NULL, 0);
	if (!header)
		return FALSE;
	tag_injector_menu.header = header;
	tag_injector_menu.opened = map;
	level_name = custom_edition_maps_level_name(tag_injector_menu.maps[map].display_index);
	snprintf(tag_injector_menu.map_name, sizeof(tag_injector_menu.map_name), "%s", tag_name_strip_path(level_name));
	for (category = 0; category < NUMBER_OF_TAG_INJECTOR_CATEGORIES; category++)
	{
		long count = 0;

		for (absolute = 0; absolute < header->tag_count; absolute++)
			count += (unsigned long)header->instances[absolute].group_tag == tag_injector_categories[category].group_tag;
		tag_injector_menu.tags[category] = malloc((unsigned long)(count + 1) * sizeof(long));
		tag_injector_menu.sizes[category] = malloc((unsigned long)(count + 1) * sizeof(long));
		tag_injector_menu.problems[category] = malloc((unsigned long)(count + 1) * sizeof(long));
		if (!tag_injector_menu.tags[category] || !tag_injector_menu.sizes[category] ||
			!tag_injector_menu.problems[category])
		{
			tag_injector_map_close();
			return FALSE;
		}
		for (absolute = 0; absolute < header->tag_count; absolute++)
		{
			if ((unsigned long)header->instances[absolute].group_tag == tag_injector_categories[category].group_tag)
			{
				long index = tag_injector_menu.tag_counts[category]++;

				tag_injector_menu.tags[category][index] = absolute;
				tag_injector_menu.sizes[category][index] = TAG_SIZE_UNKNOWN;
				tag_injector_menu.problems[category][index] = NONE;
			}
		}
		qsort(tag_injector_menu.tags[category], (size_t)tag_injector_menu.tag_counts[category], sizeof(long),
			menu_tag_less);
	}
	error(_error_silent, "tag injector: %s open: %ld weapons, %ld actor variants", tag_injector_menu.path,
		tag_injector_menu.tag_counts[_tag_injector_weapons], tag_injector_menu.tag_counts[_tag_injector_ai]);

	return TRUE;
}

void tag_injector_map_close(
	void)
{
	menu_tags_free();
	if (tag_injector_menu.opened != NONE)
		custom_edition_cache_bundle_close();
	tag_injector_menu.opened = NONE;
	tag_injector_menu.header = NULL;
	tag_injector_menu.path[0] = 0;
	tag_injector_menu.map_name[0] = 0;
}

short tag_injector_map_opened(
	void)
{
	return tag_injector_menu.opened;
}

char const *tag_injector_category_name(
	short category)
{
	return category >= 0 && category < NUMBER_OF_TAG_INJECTOR_CATEGORIES ? tag_injector_categories[category].name : "";
}

boolean tag_injector_category_addable(
	short category)
{
	return category >= 0 && category < NUMBER_OF_TAG_INJECTOR_CATEGORIES && tag_injector_categories[category].addable;
}

long tag_injector_category_count(
	short category)
{
	return tag_injector_menu.opened != NONE && category >= 0 && category < NUMBER_OF_TAG_INJECTOR_CATEGORIES ?
		tag_injector_menu.tag_counts[category] : 0;
}

char const *tag_injector_category_tag(
	short category,
	long index)
{
	long absolute = menu_tag(category, index);

	return absolute == NONE ? "" : menu_tag_name(absolute);
}

long tag_injector_tag_size(
	short category,
	long index,
	char *reason,
	long reason_size)
{
	long absolute = menu_tag(category, index);
	long *size;

	reason_set(reason, reason_size, "");
	if (absolute == NONE)
		return NONE;
	size = &tag_injector_menu.sizes[category][index];
	if (*size == TAG_SIZE_UNKNOWN)
	{
		struct tag_bundle_source source;
		long *indices = NULL;
		long count = 0;
		long problem = NONE;
		enum tag_bundle_status status;

		custom_edition_cache_bundle_source(&source);
		status = tag_bundle_tags(&source, absolute, &indices, &count, &problem);
		/* (the bundle's tags were taken from the game's malloc) */
		if (indices)
			free(indices);
		*size = status == _tag_bundle_status_ok ? count : NONE;
		tag_injector_menu.problems[category][index] = problem;
		if (status != _tag_bundle_status_ok)
			error(_error_silent, "tag injector: %s cannot be taken out: %s (%s)", menu_tag_name(absolute),
				tag_bundle_status_describe(status), menu_tag_name(problem));
	}
	if (*size == NONE)
	{
		char text[320];

		snprintf(text, sizeof(text), "It names a tag this build cannot take out: %s",
			menu_tag_name(tag_injector_menu.problems[category][index]));
		reason_set(reason, reason_size, text);
	}

	return *size;
}

long tag_injector_tag_in_profile(
	long profile,
	short category,
	long index)
{
	long absolute = menu_tag(category, index);

	return absolute == NONE ? NONE :
		port_tag_profiles_tag_find(profile, tag_injector_categories[category].group_tag, menu_tag_name(absolute));
}

boolean tag_injector_tag_add(
	long profile,
	short category,
	long index,
	char *reason,
	long reason_size)
{
	long absolute = menu_tag(category, index);
	struct injector_map_header *header;
	struct tag_bundle_source source;
	struct tag_bundle_report report;
	struct bundle_buffer buffer = { NULL, 0, 0 };
	char name[256];
	byte *sounds;
	long *indices = NULL;
	long count = 0;
	long problem = NONE;
	long tag_count;
	long tag;
	boolean added;

	reason_set(reason, reason_size, "");
	if (absolute == NONE || !tag_injector_category_addable(category))
		return FALSE;
	if (profile < 0 || profile >= port_tag_profiles_count())
	{
		reason_set(reason, reason_size, "Make a profile first.");
		return FALSE;
	}
	if (tag_injector_tag_size(category, index, reason, reason_size) == NONE)
		return FALSE;
	snprintf(name, sizeof(name), "%s", menu_tag_name(absolute));
	tag_count = tag_injector_menu.header->tag_count;

	/* the map again, with the sounds of the tags the bundle holds decoded
	(the tags are the same, at the same indices) */
	custom_edition_cache_bundle_source(&source);
	tag_bundle_tags(&source, absolute, &indices, &count, &problem);
	sounds = malloc((unsigned long)tag_count);
	if (!sounds || !indices)
	{
		if (sounds)
			free(sounds);
		if (indices)
			free(indices);
		reason_set(reason, reason_size, "Out of memory.");
		return FALSE;
	}
	memset(sounds, 0, (unsigned long)tag_count);
	for (tag = 0; tag < count; tag++)
	{
		if ((unsigned long)tag_injector_menu.header->instances[indices[tag]].group_tag == SOUND_DEFINITION_TAG)
			sounds[indices[tag]] = TRUE;
	}
	free(indices);
	header = custom_edition_cache_bundle_open(tag_injector_menu.path, sounds, tag_count);
	free(sounds);
	tag_injector_menu.header = header;
	if (!header || header->tag_count != tag_count ||
		(unsigned long)header->instances[absolute].group_tag != tag_injector_categories[category].group_tag ||
		_stricmp(menu_tag_name(absolute), name))
	{
		/* (the file changed under the menu: what it lists is gone) */
		reason_set(reason, reason_size, "The map cannot be read again.");
		tag_injector_map_close();
		return FALSE;
	}

	custom_edition_cache_bundle_source(&source);
	tag_bundle_write(&source, absolute, bundle_buffer_write, &buffer, &report);
	added = report.status == _tag_bundle_status_ok &&
		port_tag_profiles_tag_add(profile, tag_injector_menu.map_name, tag_injector_categories[category].group_tag,
			name, buffer.bytes, buffer.size);
	if (report.status != _tag_bundle_status_ok)
	{
		error(_error_silent, "tag injector: %s cannot be taken out: %s", name, tag_bundle_status_describe(report.status));
		reason_set(reason, reason_size, tag_bundle_status_describe(report.status));
	}
	else if (!added)
	{
		reason_set(reason, reason_size, "The profile cannot be written (the log says why).");
	}
	else
	{
		error(_error_silent, "tag injector: took %s out of %s: %ld tags, %lu bytes", name, tag_injector_menu.map_name,
			report.tag_count, report.size);
	}
	if (buffer.bytes)
		free(buffer.bytes);

	return added;
}

long tag_injector_profile_playable(
	void)
{
	long profile;

	if (!config_boolean("game.tag_injection"))
		return NONE;
	port_tag_profiles_scan();
	profile = port_tag_profiles_find(config_string("game.tag_injection_profile"));

	return profile >= 0 && port_tag_profiles_tag_count(profile) > 0 ? profile : NONE;
}

long tag_injector_tags_load(
	long profile,
	long *roots,
	long root_capacity)
{
	struct injector_tag_instance *instances;
	long existing = 0;
	unsigned long total = 0;
	long added = 0;
	long root_count = 0;
	long salt = 0;
	long tag_count = port_tag_profiles_tag_count(profile);
	long bundle_index;
	long index;

	tag_injector_tags_unloaded();
	instances = cache_files_tag_instances(&existing);
	if (!instances || tag_count <= 0)
		return 0;

	/* each bundle read, checked, and its models made (none of the level's
	tags is touched until all are) */
	for (index = 0; index < tag_count && tag_injector_level.bundle_count < MAXIMUM_INJECTED_BUNDLES; index++)
	{
		struct injected_bundle *bundle = &tag_injector_level.bundles[tag_injector_level.bundle_count];

		/* (counted, so that what it holds is let go with the rest) */
		tag_injector_level.bundle_count++;
		if (!level_bundle_read(profile, index, bundle, &total))
		{
			bundle->refused = TRUE;
			continue;
		}
		if (!bundle_bitmaps_drawable(bundle) || !bundle_models_convert(bundle))
		{
			error(_error_silent, "tag injector: %s is left out", port_tag_profiles_tag_name(profile, index));
			bundle->refused = TRUE;
		}
	}

	/* the tag of the level each bundle's tag is: the level's own, one added
	before, or a new one */
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (long)((unsigned long)instances[index].tag_index >> 16);

		if (instance_salt > salt)
			salt = instance_salt;
	}
	salt++;
	tag_injector_level.grown_instances = malloc((unsigned long)existing * sizeof(*instances));
	if (!tag_injector_level.grown_instances)
	{
		level_bundles_free();
		custom_edition_injected_models_dispose();
		return 0;
	}
	memcpy(tag_injector_level.grown_instances, instances, (unsigned long)existing * sizeof(*instances));
	tag_injector_level.saved_instances = instances;
	tag_injector_level.saved_count = existing;
	tag_injector_level.first_injected = existing;
	for (bundle_index = 0; bundle_index < tag_injector_level.bundle_count; bundle_index++)
	{
		struct injected_bundle *bundle = &tag_injector_level.bundles[bundle_index];
		long tag;

		for (tag = 0; bundle->datums && tag < bundle->loaded.tag_count; tag++)
			bundle->datums[tag] = NONE;
		if (bundle->refused)
			continue;
		for (tag = 0; tag < bundle->loaded.tag_count; tag++)
		{
			unsigned long group_tag = bundle_tag_group(&bundle->loaded, tag);
			char const *name = bundle_tag_name(&bundle->loaded, tag);
			struct injector_tag_instance *grown;
			struct injector_tag_instance *instance;
			byte const *entry = bundle->loaded.tags + tag * TAG_BUNDLE_TAG_BYTES;
			long datum = level_tag(group_tag, name);
			long count = existing + added;

			if (datum != NONE || group_left_out(group_tag))
			{
				bundle->datums[tag] = datum;
				continue;
			}
			if (count >= MAXIMUM_TAG_COUNT || !schema_of(group_tag))
			{
				error(_error_silent, "tag injector: no room for %s, or no schema of its group: no tags are added", name);
				tag_injector_tags_unloaded();
				return 0;
			}
			grown = realloc(tag_injector_level.grown_instances, (unsigned long)(count + 1) * sizeof(*grown));
			if (!grown)
			{
				tag_injector_tags_unloaded();
				return 0;
			}
			tag_injector_level.grown_instances = grown;
			instance = &grown[count];
			memset(instance, 0, sizeof(*instance));
			instance->group_tag = (long)group_tag;
			instance->parent_group_tags[0] = (long)read_unsigned_long(entry + TAG_BUNDLE_TAG_PARENT_GROUPS_OFFSET);
			instance->parent_group_tags[1] = (long)read_unsigned_long(entry + TAG_BUNDLE_TAG_PARENT_GROUPS_OFFSET + 4);
			instance->tag_index = (salt << 16) | count;
			instance->name = (char *)name;
			instance->base_address = bundle_tag_root(&bundle->loaded, tag);
			bundle->datums[tag] = instance->tag_index;
			added++;
			/* (tag_loaded finds it now, for the bundles after this one) */
			cache_files_set_tag_instances(tag_injector_level.grown_instances, existing + added);
		}
	}
	tag_injector_level.injected_count = added;
	tag_injector_level.injected_bundle = malloc((unsigned long)(added + 1) * sizeof(short));
	if (!tag_injector_level.injected_bundle)
	{
		tag_injector_tags_unloaded();
		return 0;
	}

	/* the tags added: each named tag the level's, and each sound's samples
	read for it */
	for (bundle_index = 0; bundle_index < tag_injector_level.bundle_count; bundle_index++)
	{
		struct injected_bundle *bundle = &tag_injector_level.bundles[bundle_index];
		long tag;

		for (tag = 0; !bundle->refused && tag < bundle->loaded.tag_count; tag++)
		{
			long datum = bundle->datums[tag];
			long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(datum);
			unsigned long group_tag = bundle_tag_group(&bundle->loaded, tag);
			byte *root = bundle_tag_root(&bundle->loaded, tag);

			if (datum == NONE || absolute < existing || tag_injector_level.grown_instances[absolute].base_address != root)
				continue;
			tag_injector_level.injected_bundle[absolute - existing] = (short)bundle_index;
			retarget_fields(bundle, root, schema_of(group_tag), 0);
			if (group_tag == SOUND_DEFINITION_TAG)
				sound_permutations_retarget((struct sound_definition *)root, datum);
			if (group_tag == BITMAP_GROUP_TAG)
				tag_injector_level.bitmaps_added = TRUE;
		}
		/* (the tag it was taken out for: the pools' weapon or actor) */
		if (!bundle->refused && bundle->loaded.tag_count > 0 && bundle->datums[0] != NONE && root_count < root_capacity)
			roots[root_count++] = bundle->datums[0];
	}
	/* (the channels of every bitmap Halo PC keeps elsewhere, the bundles'
	among the level's, as a mod's bitmaps are found: mod_bitmaps.c) */
	if (tag_injector_level.bitmaps_added && !custom_edition_cache_tags_loaded())
		custom_edition_loaded_reordered_bitmaps_find();
	error(_error_silent, "tag injector: %s: %ld tags added from %ld bundles (%lu bytes of tags), %ld weapons and actors",
		port_tag_profiles_name(profile), added, tag_injector_level.bundle_count, total, root_count);

	return root_count;
}

void tag_injector_tags_unloaded(
	void)
{
	if (tag_injector_level.saved_instances)
		cache_files_set_tag_instances(tag_injector_level.saved_instances, tag_injector_level.saved_count);
	if (tag_injector_level.grown_instances)
		free(tag_injector_level.grown_instances);
	if (tag_injector_level.injected_bundle)
		free(tag_injector_level.injected_bundle);
	/* (a Custom Edition map's channels are its own, which it forgets as it
	goes; a mod's go with the mods') */
	if (tag_injector_level.bitmaps_added && !custom_edition_cache_tags_loaded())
		custom_edition_bitmaps_dispose();
	custom_edition_injected_models_dispose();
	level_bundles_free();
	tag_injector_level.saved_instances = NULL;
	tag_injector_level.saved_count = 0;
	tag_injector_level.grown_instances = NULL;
	tag_injector_level.injected_bundle = NULL;
	tag_injector_level.first_injected = 0;
	tag_injector_level.injected_count = 0;
	tag_injector_level.bitmaps_added = FALSE;
}

boolean tag_injector_tag_injected(
	long tag_index)
{
	long absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index);

	return tag_index != NONE && tag_injector_level.injected_count > 0 &&
		absolute >= tag_injector_level.first_injected &&
		absolute < tag_injector_level.first_injected + tag_injector_level.injected_count &&
		tag_injector_level.grown_instances[absolute].tag_index == tag_index;
}

boolean tag_injector_tags_loaded(
	void)
{
	return tag_injector_level.injected_count > 0;
}

int tag_injector_read(
	long tag_index,
	long offset,
	long size,
	void *buffer)
{
	struct injected_bundle const *bundle;
	struct injector_tag_instance const *instance;
	unsigned long available;

	if (!tag_injector_level.injected_count || !buffer || size < 0)
		return 0;
	if (!tag_injector_tag_injected(tag_index))
	{
		/* (the renderer keeps each address's channel order until it is told
		another: the level's bitmap where a bundle's was has the Xbox's) */
		if (tag_injector_level.bitmaps_added)
			halo_custom_edition_texels_channels(buffer, _custom_edition_channels_xbox);
		return 0;
	}
	instance = &tag_injector_level.grown_instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index)];
	bundle = &tag_injector_level.bundles[tag_injector_level.injected_bundle[DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index) -
		tag_injector_level.first_injected]];
	/* (what is past its file data, or cannot be read, is none) */
	available = offset >= 0 && (unsigned long)offset < bundle->loaded.file_data_size ?
		MIN((unsigned long)size, bundle->loaded.file_data_size - (unsigned long)offset) : 0;
	if (!available || !bundle_file_read((void *)bundle, (unsigned long)offset, available, buffer))
	{
		error(_error_silent, "tag injector: cannot read 0x%lX bytes at 0x%08lX of '%s'", (unsigned long)size,
			(unsigned long)offset, instance->name);
		available = 0;
	}
	memset((byte *)buffer + available, 0, (unsigned long)size - available);
	if ((unsigned long)instance->group_tag == BITMAP_GROUP_TAG)
	{
		struct bitmap_group *group = instance->base_address;
		long bitmap_index;

		/* the texture cache sets the address its pixels go to before it
		reads them (as custom_edition_bitmap_pixels_arrived finds them) */
		for (bitmap_index = 0; bitmap_index < group->bitmaps.count; bitmap_index++)
		{
			struct bitmap_data *bitmap = TAG_BLOCK_GET_ELEMENT(&group->bitmaps, bitmap_index, struct bitmap_data);

			if (bitmap->base_address == buffer && bitmap->pixels_offset == offset)
			{
				halo_custom_edition_texels_channels(buffer,
					(unsigned char)custom_edition_bitmap_channel_order(tag_index, bitmap_index));
				/* (on failure it logs, and the texture shows the pixels as read) */
				rasterizer_xbox_bitmap_rebuild_hardware_format(bitmap);
				break;
			}
		}
	}

	return 1;
}
