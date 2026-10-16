/*
RANDOMIZER.C

A campaign level can swap its actors and the weapons they carry, and the
weapons lying in it, without changing the map file. The actors are taken
from a pool (any of them, one faction, or one type). A tag the level does
not have is copied from another campaign map, dependencies included, and
appended to the tag table in the same order on every machine. A bitmap's
pixels and a sound's samples stay in that map's file and are read from it.

The choice is a hash of the map, the setup and the placement, so a
checkpoint reloads the same level. A starting location with a command list,
and a squad named by a conversation, keep the actor the level gave them. A
weapon a script names is left where it was.

Off unless game.randomizer is on, and only for the ten campaign levels. A
multiplayer game that is not co-op is not randomized. A client uses the
setup the host advertised, not its own.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cseries.h"
#include "ai/actor_definitions.h"
#include "ai/ai_scenario_definitions.h"
#include "cache/cache_files.h"
#include "items/weapon_definitions.h"
#include "main/main.h"
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
#include "tag_schema.h"

/* the addresses a map file gives its tag pointers, wherever this process
keeps the tag cache (halo_port_window.h) */
#define DONOR_WINDOW 0x803A6000UL

#define DONOR_COUNT 10
#define WANTED_MAX 8192
#define IMPORT_MAX 2048
#define POOL_MAX 256
#define WEAPON_MAX 128
#define PROTECTED_MAX 128
#define COPY_BUDGET (32UL * 1024UL * 1024UL)

enum
{
	POOL_ANY = 0,
	POOL_COVENANT,
	POOL_FLOOD,
	POOL_HUMAN,
	POOL_ONE,
	TYPE_GRUNT = 0,
	TYPE_ELITE,
	TYPE_JACKAL,
	TYPE_HUNTER,
	TYPE_FLOOD_COMBAT,
	TYPE_FLOOD_CARRIER,
	TYPE_MARINE,
	TYPE_FLOOD_INFECTION,
	TYPE_ENGINEER,
	TYPE_NONE = -1
};

/* a conversation's participant, as tag_schema_scenario.c has it: the name
at 0x24 is the squad the conversation keeps */
struct randomizer_participant
{
	word pad00;
	word flags;
	short selection_type;
	short actor_type;
	short preexisting_object_name_index;
	short new_attach_object_name_index;
	byte unknown0C[0x0C];
	short dialogue_variants[6];
	char ai_index_name[32];
	long runtime_ai_index;
	byte unknown48[0x0C];
};

typedef char randomizer_participant_size[sizeof(struct randomizer_participant) == 0x54 ? 1 : -1];
typedef char randomizer_pointer_size[sizeof(void *) == 4 ? 1 : -1];

struct randomizer_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

struct randomizer_tag_header
{
	struct randomizer_tag_instance *tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	void *vertex_buffers;
	long index_buffer_count;
	void *index_buffers;
	unsigned long signature;
};

typedef char randomizer_tag_instance_size[sizeof(struct randomizer_tag_instance) == 0x20 ? 1 : -1];
typedef char randomizer_tag_count_offset[offsetof(struct randomizer_tag_header, tag_count) == 0xC ? 1 : -1];

struct donor
{
	FILE *file;
	unsigned char *bytes;
	unsigned long size;
	long file_length;
	long count;
	struct randomizer_tag_instance *instances;
	unsigned char *seen;
	long *datum_of;
	int keep;
	char leaf[16];
};

struct wanted
{
	short donor;
	long absolute;
};

struct copied_tag
{
	short donor;
	void *root;
	struct tag_schema_definition const *schema;
};

struct held_block
{
	struct held_block *next;
};

struct setup
{
	int enemies;
	int pickups;
	int weapons;
	int pool;
	int type;
};

extern boolean tag_index_is_group(long tag_index, long group_tag);
extern void *cache_files_tag_instances(long *count);
extern void cache_files_set_tag_instances(void *instances, long count);
extern int config_boolean(char const *name);
extern char const *config_string(char const *name);

/* the loaded map's tag, as cache_files.c has it. The semantics header only
names it (menu_tags.c defines it the same way) */
struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

typedef char randomizer_tag_table_size[
	sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

static struct donor donors[DONOR_COUNT];
static int donor_count;
static struct wanted *wanted;
static long wanted_count;
static struct copied_tag copied[IMPORT_MAX];
static long copied_count;
static struct held_block *held;
static unsigned long held_bytes;
static int import_stopped;
static struct cache_file_tag_instance *saved_instances;
static long saved_count;
static struct cache_file_tag_instance *grown_instances;
static long grown_count;
static long import_absolute[IMPORT_MAX];
static short import_donor[IMPORT_MAX];
static long import_count;
static long variant_pool[POOL_MAX];
static long variant_count;
static long weapon_pool[WEAPON_MAX];
static long weapon_count;
static char protected_names[PROTECTED_MAX][32];
static int protected_count;
static struct setup current;
static int loaded;
static char map_name_leaf[32];
static int remote_on;
static unsigned char remote_setup[3];
static void *map_widget;
static wchar_t prefixed_title[96];

static unsigned long mix(unsigned long value)
{
	value ^= value >> 16;
	value *= 0x7FEB352DUL;
	value ^= value >> 15;
	value *= 0x846CA68BUL;
	value ^= value >> 16;

	return value;
}

static unsigned long mix_text(unsigned long seed, char const *text)
{
	for (; text && *text; text++)
		seed = mix(seed ^ (unsigned char)*text);

	return seed;
}

static unsigned long mix_bits(unsigned long seed, float value)
{
	unsigned long bits = 0;

	memcpy(&bits, &value, sizeof(value) < sizeof(bits) ? sizeof(value) : sizeof(bits));

	return mix(seed ^ bits);
}

static int campaign_leaf(char const *name, char *leaf, unsigned int leaf_size)
{
	char const *stripped;
	int index;

	if (!name || !name[0])
		return 0;
	stripped = tag_name_strip_path(name);
	for (index = 0; index < 10; index++)
	{
		char const *path = main_get_solo_level_name(index);

		if (!_stricmp(name, path) || !_stricmp(stripped, tag_name_strip_path(path)))
		{
			snprintf(leaf, leaf_size, "%s", tag_name_strip_path(path));
			return 1;
		}
	}

	return 0;
}

static int pool_id(char const *text)
{
	static char const *const names[] = { "any", "covenant", "flood", "human", "one" };
	int index;

	if (!text)
		return POOL_ANY;
	for (index = 0; index < (int)NUMBEROF(names); index++)
	{
		if (!_stricmp(text, names[index]))
			return index;
	}

	return POOL_ANY;
}

static int type_id(char const *text)
{
	static char const *const names[] = {
		"grunt", "elite", "jackal", "hunter", "flood_combat", "flood_carrier", "marine"
	};
	int index;

	if (!text)
		return TYPE_GRUNT;
	for (index = 0; index < (int)NUMBEROF(names); index++)
	{
		if (!_stricmp(text, names[index]))
			return index;
	}

	return TYPE_GRUNT;
}

static void setup_from_config(struct setup *setup)
{
	setup->enemies = config_boolean("game.randomizer_enemies");
	setup->pickups = config_boolean("game.randomizer_pickups");
	setup->weapons = config_boolean("game.randomizer_enemy_weapons");
	setup->pool = pool_id(config_string("game.randomizer_pool"));
	setup->type = type_id(config_string("game.randomizer_type"));
}

static void setup_from_remote(struct setup *setup)
{
	setup->enemies = (remote_setup[0] & 1) != 0;
	setup->pickups = (remote_setup[0] & 2) != 0;
	setup->weapons = (remote_setup[0] & 4) != 0;
	setup->pool = remote_setup[1] <= POOL_ONE ? remote_setup[1] : POOL_ANY;
	setup->type = remote_setup[2] <= TYPE_MARINE ? remote_setup[2] : TYPE_GRUNT;
}

static int setup_swaps(struct setup const *setup)
{
	return setup->enemies || setup->pickups || setup->weapons;
}

static int host_randomized(void)
{
	struct network_game *game = network_game_get_game();
	struct setup setup;
	char leaf[32];

	if (network_game_distributed_client())
		return remote_on;
	if (!config_boolean("game.randomizer"))
		return 0;
	if (game && game->variant.game_engine_index != game_engine_none)
		return 0;
	if (!game || !campaign_leaf(game->map.name, leaf, sizeof(leaf)))
		return 0;
	setup_from_config(&setup);

	return setup_swaps(&setup);
}

static int classify(char const *name)
{
	char lower[256];
	unsigned int index;

	if (!name || !name[0])
		return TYPE_NONE;
	for (index = 0; name[index] && index + 1 < sizeof(lower); index++)
	{
		char character = name[index];

		if (character >= 'A' && character <= 'Z')
			character = (char)(character - 'A' + 'a');
		lower[index] = character;
	}
	lower[index] = 0;
	if (strstr(lower, "floodcombat") || strstr(lower, "flood_combat"))
		return TYPE_FLOOD_COMBAT;
	if (strstr(lower, "floodcarrier") || strstr(lower, "flood_carrier"))
		return TYPE_FLOOD_CARRIER;
	if (strstr(lower, "flood"))
		return TYPE_FLOOD_INFECTION;
	if (strstr(lower, "grunt"))
		return TYPE_GRUNT;
	if (strstr(lower, "jackal"))
		return TYPE_JACKAL;
	if (strstr(lower, "hunter"))
		return TYPE_HUNTER;
	if (strstr(lower, "elite"))
		return TYPE_ELITE;
	if (strstr(lower, "marine") || strstr(lower, "crew"))
		return TYPE_MARINE;
	if (strstr(lower, "bugger") || strstr(lower, "engineer"))
		return TYPE_ENGINEER;

	return TYPE_NONE;
}

static int pool_allows(int kind)
{
	if (kind == TYPE_NONE)
		return 0;
	if (current.pool == POOL_ANY)
		return 1;
	if (current.pool == POOL_COVENANT)
		return kind == TYPE_GRUNT || kind == TYPE_ELITE || kind == TYPE_JACKAL || kind == TYPE_HUNTER ||
			kind == TYPE_ENGINEER;
	if (current.pool == POOL_FLOOD)
		return kind == TYPE_FLOOD_COMBAT || kind == TYPE_FLOOD_CARRIER || kind == TYPE_FLOOD_INFECTION;
	if (current.pool == POOL_HUMAN)
		return kind == TYPE_MARINE;
	if (current.pool == POOL_ONE)
		return kind == current.type;

	return 0;
}

static void *donor_address(struct donor *donor, void *pointer)
{
	unsigned long address = (unsigned long)pointer;

	if (!pointer || address < DONOR_WINDOW || address - DONOR_WINDOW >= donor->size)
		return NULL;

	return donor->bytes + (address - DONOR_WINDOW);
}

static int range_in(struct donor *donor, void const *pointer, unsigned long bytes)
{
	unsigned char const *start = pointer;

	if (!pointer)
		return bytes == 0;
	if (start < donor->bytes || start > donor->bytes + donor->size)
		return 0;
	if ((unsigned long)(donor->bytes + donor->size - start) < bytes)
		return 0;

	return 1;
}

static char const *instance_name(struct donor *donor, struct randomizer_tag_instance *instance)
{
	char const *name = donor_address(donor, instance->name);
	unsigned long room;
	void const *end;

	if (!name || !range_in(donor, name, 1))
		return NULL;
	room = (unsigned long)(donor->bytes + donor->size - (unsigned char const *)name);
	end = memchr(name, 0, room > 256 ? 256 : room);
	if (!end)
		return NULL;

	return name;
}

static struct tag_schema_definition const *schema_of(unsigned long group)
{
	long list;

	for (list = 0; tag_schema_group_lists[list]; list++)
	{
		struct tag_schema_group const *groups = tag_schema_group_lists[list];
		long index;

		for (index = 0; groups[index].group_tag; index++)
		{
			if (groups[index].group_tag == group)
				return groups[index].definition;
		}
	}

	return NULL;
}

static void *grab(unsigned long size)
{
	struct held_block *block;

	if (!size || import_stopped || held_bytes > COPY_BUDGET || size > 0x7fffffffUL - sizeof(*block))
	{
		import_stopped = 1;
		return NULL;
	}
	block = malloc(sizeof(*block) + size);
	if (!block)
	{
		import_stopped = 1;
		return NULL;
	}
	block->next = held;
	held = block;
	held_bytes += size;

	return block + 1;
}

static void held_free(void)
{
	while (held)
	{
		struct held_block *next = held->next;

		free(held);
		held = next;
	}
	held_bytes = 0;
}

static int leave_group(long group)
{
	return group == 'vehi' || group == 'scnr' || group == 'sbsp' || group == (long)'matg';
}

static void want(struct donor *donor, long datum);

static void walk_want(struct donor *donor, void *element, struct tag_schema_definition const *schema)
{
	struct tag_schema_field const *field;

	if (!element || !schema || !range_in(donor, element, (unsigned long)schema->size))
		return;
	for (field = schema->fields; field->type != _tag_schema_terminator; field++)
	{
		long item;

		for (item = 0; item < field->count; item++)
		{
			void *place = (char *)element + field->offset + item * field->size;

			if (field->type == _tag_schema_reference)
				want(donor, ((struct tag_reference *)place)->index);
			else if (field->type == _tag_schema_tag_index)
				want(donor, *(long *)place);
			else if (field->type == _tag_schema_struct)
				walk_want(donor, place, field->definition);
			else if (field->type == _tag_schema_block)
			{
				struct tag_block *block = place;
				struct tag_schema_definition const *child = field->definition;
				void *elements = donor_address(donor, block->address);
				long index;
				long count = block->count;

				if (!child || count <= 0 || !elements || child->size <= 0 || count > 100000 ||
					(unsigned long)count > 0x1000000UL / (unsigned long)child->size ||
					!range_in(donor, elements, (unsigned long)count * (unsigned long)child->size))
					continue;
				for (index = 0; index < count; index++)
					walk_want(donor, (char *)elements + index * (unsigned long)child->size, child);
			}
		}
	}
}

static void want(struct donor *donor, long datum)
{
	long absolute = datum & 0xFFFF;
	struct randomizer_tag_instance *instance;
	char const *name;
	long existing;

	if (datum == NONE || absolute < 0 || absolute >= donor->count || donor->seen[absolute])
		return;
	donor->seen[absolute] = 1;
	instance = donor->instances + absolute;
	name = instance_name(donor, instance);
	if (leave_group(instance->group_tag))
	{
		donor->datum_of[absolute] = name ? tag_loaded(instance->group_tag, name) : NONE;
		return;
	}
	existing = name ? tag_loaded(instance->group_tag, name) : NONE;
	if (existing != NONE)
	{
		donor->datum_of[absolute] = existing;
		return;
	}
	if (!name || wanted_count >= WANTED_MAX)
	{
		import_stopped = 1;
		return;
	}
	wanted[wanted_count].donor = (short)(donor - donors);
	wanted[wanted_count].absolute = absolute;
	wanted_count++;
}

static int wanted_less(void const *left, void const *right)
{
	struct wanted const *a = left;
	struct wanted const *b = right;
	struct donor *da = donors + a->donor;
	struct donor *db = donors + b->donor;
	char const *an = instance_name(da, da->instances + a->absolute);
	char const *bn = instance_name(db, db->instances + b->absolute);
	int order = _stricmp(an ? an : "", bn ? bn : "");

	if (order)
		return order;
	if (a->donor != b->donor)
		return a->donor - b->donor;

	return a->absolute < b->absolute ? -1 : a->absolute > b->absolute;
}

static void copy_fields(struct donor *donor, void *element, struct tag_schema_definition const *schema);

static void copy_fields(struct donor *donor, void *element, struct tag_schema_definition const *schema)
{
	struct tag_schema_field const *field;

	if (!element || !schema)
		return;
	for (field = schema->fields; field->type != _tag_schema_terminator; field++)
	{
		long item;

		for (item = 0; item < field->count; item++)
		{
			void *place = (char *)element + field->offset + item * field->size;

			if (field->type == _tag_schema_reference)
			{
				struct tag_reference *reference = place;
				char const *source = donor_address(donor, reference->name);
				unsigned long room;
				void const *end;
				char *copy;

				reference->name = "";
				reference->name_length = 0;
				if (!source || !range_in(donor, source, 1))
				{
					reference->index = NONE;
					continue;
				}
				room = (unsigned long)(donor->bytes + donor->size - (unsigned char const *)source);
				end = memchr(source, 0, room > 256 ? 256 : room);
				if (!end)
					continue;
				copy = grab((unsigned long)strlen(source) + 1);
				if (!copy)
					continue;
				strcpy(copy, source);
				reference->name = copy;
				reference->name_length = (long)strlen(copy);
			}
			else if (field->type == _tag_schema_data || field->type == _tag_schema_file_data)
			{
				struct tag_data *data = place;

				data->definition = NULL;
				if (field->type == _tag_schema_file_data || data->size <= 0)
				{
					data->address = NULL;
					continue;
				}
				{
					void *source = donor_address(donor, data->address);
					void *bytes;

					if (!source || data->size > 0x1000000 ||
						!range_in(donor, source, (unsigned long)data->size))
					{
						data->size = 0;
						data->address = NULL;
						continue;
					}
					bytes = grab((unsigned long)data->size);
					if (!bytes)
					{
						data->size = 0;
						data->address = NULL;
						continue;
					}
					memcpy(bytes, source, (unsigned long)data->size);
					data->address = bytes;
				}
			}
			else if (field->type == _tag_schema_reset)
			{
				short size = field->size;
				long value = field->target_offset;

				if (size > 4)
					memset(place, 0, (unsigned long)size);
				else if (size > 0)
				{
					memset(place, 0, (unsigned long)size);
					memcpy(place, &value, (unsigned long)size);
				}
			}
			else if (field->type == _tag_schema_struct)
				copy_fields(donor, place, field->definition);
			else if (field->type == _tag_schema_block)
			{
				struct tag_block *block = place;
				struct tag_schema_definition const *child = field->definition;
				void *source = donor_address(donor, block->address);
				void *elements;
				long index;
				long count = block->count;

				block->definition = NULL;
				block->address = NULL;
				if (!child || count <= 0 || !source || child->size <= 0 || count > 100000 ||
					(unsigned long)count > 0x1000000UL / (unsigned long)child->size ||
					!range_in(donor, source, (unsigned long)count * (unsigned long)child->size))
				{
					block->count = 0;
					continue;
				}
				elements = grab((unsigned long)count * (unsigned long)child->size);
				if (!elements)
				{
					block->count = 0;
					continue;
				}
				memcpy(elements, source, (unsigned long)count * (unsigned long)child->size);
				block->address = elements;
				for (index = 0; index < count; index++)
					copy_fields(donor, (char *)elements + index * (unsigned long)child->size, child);
			}
		}
	}
}

static long retarget_datum(struct donor *donor, long datum)
{
	long absolute = datum & 0xFFFF;

	if (datum == NONE || absolute < 0 || absolute >= donor->count)
		return NONE;

	return donor->datum_of[absolute];
}

static void retarget_fields(struct donor *donor, void *element, struct tag_schema_definition const *schema)
{
	struct tag_schema_field const *field;

	if (!element || !schema)
		return;
	for (field = schema->fields; field->type != _tag_schema_terminator; field++)
	{
		long item;

		for (item = 0; item < field->count; item++)
		{
			void *place = (char *)element + field->offset + item * field->size;

			if (field->type == _tag_schema_reference)
			{
				struct tag_reference *reference = place;
				long old = reference->index;
				long index = reference->name && reference->name[0] ?
					tag_loaded(reference->group_tag, reference->name) : NONE;

				if (index == NONE)
					index = retarget_datum(donor, old);
				reference->index = index;
				if (index != NONE)
				{
					long count = 0;
					struct cache_file_tag_instance *instances = cache_files_tag_instances(&count);
					long absolute = index & 0xFFFF;

					reference->name = tag_get_name(index);
					reference->name_length = reference->name ? (long)strlen(reference->name) : 0;
					if (instances && absolute >= 0 && absolute < count &&
						tag_loaded(reference->group_tag, reference->name) != index)
						reference->group_tag = (unsigned long)instances[absolute].group_tag;
				}
			}
			else if (field->type == _tag_schema_tag_index)
				*(long *)place = retarget_datum(donor, *(long *)place);
			else if (field->type == _tag_schema_struct)
				retarget_fields(donor, place, field->definition);
			else if (field->type == _tag_schema_block)
			{
				struct tag_block *block = place;
				struct tag_schema_definition const *child = field->definition;
				long index;

				if (!child || !block->address || block->count <= 0)
					continue;
				for (index = 0; index < block->count; index++)
					retarget_fields(donor, (char *)block->address + index * (unsigned long)child->size, child);
			}
		}
	}
}

static int open_donor(struct donor *donor, char const *leaf)
{
	char path[512];
	unsigned char header[0x800];
	long version;
	long file_length;
	long tag_offset;
	long tag_size;
	unsigned long signature;
	struct randomizer_tag_header *tags;
	size_t read;

	memset(donor, 0, sizeof(*donor));
	snprintf(donor->leaf, sizeof(donor->leaf), "%s", leaf);
	snprintf(path, sizeof(path), "%s%s.map", cache_files_map_directory(), leaf);
	donor->file = fopen(path, "rb");
	if (!donor->file)
		return 0;
	read = fread(header, 1, sizeof(header), donor->file);
	if (read != sizeof(header))
		return 0;
	memcpy(&signature, header, 4);
	memcpy(&version, header + 4, 4);
	memcpy(&file_length, header + 8, 4);
	memcpy(&tag_offset, header + 0x10, 4);
	memcpy(&tag_size, header + 0x14, 4);
	if (signature != 'head' || version != 5 || file_length < (long)sizeof(header) || tag_offset < 0 ||
		tag_size < (long)sizeof(struct randomizer_tag_header) || tag_offset > file_length - tag_size)
		return 0;
	donor->file_length = file_length;
	donor->bytes = malloc((unsigned long)tag_size);
	donor->size = (unsigned long)tag_size;
	if (!donor->bytes)
		return 0;
	if (fseek(donor->file, tag_offset, SEEK_SET) != 0 ||
		fread(donor->bytes, 1, (unsigned long)tag_size, donor->file) != (unsigned long)tag_size)
		return 0;
	tags = (struct randomizer_tag_header *)donor->bytes;
	if (tags->signature != 'tags' || tags->tag_count <= 0 || tags->tag_count > 100000)
		return 0;
	donor->count = tags->tag_count;
	donor->instances = donor_address(donor, tags->tag_instances);
	if (!donor->instances ||
		!range_in(donor, donor->instances, (unsigned long)donor->count * sizeof(*donor->instances)))
		return 0;
	donor->seen = calloc((unsigned long)donor->count, 1);
	donor->datum_of = malloc((unsigned long)donor->count * sizeof(*donor->datum_of));
	if (!donor->seen || !donor->datum_of)
		return 0;
	{
		long index;

		for (index = 0; index < donor->count; index++)
			donor->datum_of[index] = NONE;
	}

	return 1;
}

static void close_donor_bytes(struct donor *donor)
{
	if (donor->bytes)
		free(donor->bytes);
	donor->bytes = NULL;
	if (donor->seen)
		free(donor->seen);
	donor->seen = NULL;
	if (donor->datum_of)
		free(donor->datum_of);
	donor->datum_of = NULL;
	donor->instances = NULL;
	if (!donor->keep && donor->file)
	{
		fclose(donor->file);
		donor->file = NULL;
	}
}

static void donors_close(void)
{
	int index;

	for (index = 0; index < donor_count; index++)
	{
		close_donor_bytes(donors + index);
		if (donors[index].file)
		{
			fclose(donors[index].file);
			donors[index].file = NULL;
		}
	}
	donor_count = 0;
}

static void remember_import(long absolute, short donor_index)
{
	if (import_count >= IMPORT_MAX)
		return;
	import_absolute[import_count] = absolute;
	import_donor[import_count] = donor_index;
	import_count++;
}

static int variant_in_pool(struct donor *donor, long datum)
{
	long absolute = datum & 0xFFFF;
	struct randomizer_tag_instance *instance;
	struct actor_variant_definition *variant;
	char const *unit_name;

	if (datum == NONE || absolute < 0 || absolute >= donor->count)
		return 0;
	instance = donor->instances + absolute;
	if (instance->group_tag != ACTOR_VARIANT_DEFINITION_TAG)
		return 0;
	variant = donor_address(donor, instance->base_address);
	if (!variant || !range_in(donor, variant, sizeof(*variant)))
		return 0;
	unit_name = donor_address(donor, variant->unit_reference.name);
	if (!unit_name || !range_in(donor, unit_name, 1) || !memchr(unit_name, 0, 256))
		return 0;

	return pool_allows(classify(unit_name));
}

static void seed_donor(struct donor *donor)
{
	long scenario_absolute;
	struct randomizer_tag_header *tags;
	struct scenario *scenario;
	struct tag_reference *palette;
	long index;
	long queued;

	tags = (struct randomizer_tag_header *)donor->bytes;
	scenario_absolute = tags->scenario_tag_index & 0xFFFF;
	if (scenario_absolute < 0 || scenario_absolute >= donor->count)
		return;
	scenario = donor_address(donor, donor->instances[scenario_absolute].base_address);
	if (!scenario || !range_in(donor, scenario, sizeof(*scenario)))
		return;
	palette = donor_address(donor, scenario->ai_actor_palette.address);
	if (!palette || scenario->ai_actor_palette.count <= 0 || scenario->ai_actor_palette.count > 256 ||
		!range_in(donor, palette, (unsigned long)scenario->ai_actor_palette.count * sizeof(*palette)))
		return;
	queued = wanted_count;
	for (index = 0; index < scenario->ai_actor_palette.count; index++)
	{
		if (variant_in_pool(donor, palette[index].index))
			want(donor, palette[index].index);
	}
	while (queued < wanted_count)
	{
		struct wanted *entry = wanted + queued;
		struct donor *owner = donors + entry->donor;
		struct randomizer_tag_instance *instance = owner->instances + entry->absolute;
		struct tag_schema_definition const *schema = schema_of((unsigned long)instance->group_tag);

		queued++;
		if (schema)
			walk_want(owner, donor_address(owner, instance->base_address), schema);
	}
}

static void import_wanted(void)
{
	long existing = 0;
	struct cache_file_tag_instance *instances = cache_files_tag_instances(&existing);
	long salt = 0;
	long index;

	if (!instances || wanted_count <= 0)
		return;
	if (wanted_count > IMPORT_MAX)
		wanted_count = IMPORT_MAX;
	qsort(wanted, (unsigned long)wanted_count, sizeof(*wanted), wanted_less);
	grown_instances = malloc((unsigned long)(existing + wanted_count) * sizeof(*grown_instances));
	if (!grown_instances)
	{
		platform_log("randomizer: no room for the imported tags");
		return;
	}
	memcpy(grown_instances, instances, (unsigned long)existing * sizeof(*grown_instances));
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (unsigned long)instances[index].tag_index >> 16;

		if (instance_salt > salt)
			salt = instance_salt;
	}
	saved_instances = instances;
	saved_count = existing;
	grown_count = existing;
	cache_files_set_tag_instances(grown_instances, grown_count);
	salt++;
	for (index = 0; index < wanted_count && !import_stopped; index++)
	{
		struct wanted *entry = wanted + index;
		struct donor *donor = donors + entry->donor;
		struct randomizer_tag_instance *source = donor->instances + entry->absolute;
		struct tag_schema_definition const *schema = schema_of((unsigned long)source->group_tag);
		char const *name = instance_name(donor, source);
		void *root_source;
		void *root;
		char *name_copy;
		long datum;
		struct cache_file_tag_instance *dest;

		if (!schema || !name)
		{
			if (!schema)
				platform_log("randomizer: left out %s, which has no schema", name ? name : donor->leaf);
			continue;
		}
		if (tag_loaded(source->group_tag, name) != NONE)
		{
			donor->datum_of[entry->absolute] = tag_loaded(source->group_tag, name);
			continue;
		}
		if (grown_count > 0xFFFE || copied_count >= IMPORT_MAX)
		{
			import_stopped = 1;
			break;
		}
		root_source = donor_address(donor, source->base_address);
		if (!root_source || schema->size <= 0 || !range_in(donor, root_source, (unsigned long)schema->size))
			continue;
		root = grab((unsigned long)schema->size);
		name_copy = grab((unsigned long)strlen(name) + 1);
		if (!root || !name_copy)
			break;
		memcpy(root, root_source, (unsigned long)schema->size);
		strcpy(name_copy, name);
		copy_fields(donor, root, schema);
		datum = (salt << 16) | grown_count;
		dest = grown_instances + grown_count;
		memset(dest, 0, sizeof(*dest));
		dest->group_tag = source->group_tag;
		dest->parent_group_tags[0] = source->parent_group_tags[0];
		dest->parent_group_tags[1] = source->parent_group_tags[1];
		dest->tag_index = datum;
		dest->name = name_copy;
		dest->base_address = root;
		donor->datum_of[entry->absolute] = datum;
		donor->keep = 1;
		remember_import(grown_count, entry->donor);
		copied[copied_count].donor = entry->donor;
		copied[copied_count].root = root;
		copied[copied_count].schema = schema;
		copied_count++;
		grown_count++;
		cache_files_set_tag_instances(grown_instances, grown_count);
	}
	for (index = 0; index < copied_count; index++)
		retarget_fields(donors + copied[index].donor, copied[index].root, copied[index].schema);
	if (import_stopped)
		platform_log("randomizer: stopped importing; the level keeps what was copied");
	else if (copied_count)
		platform_log("randomizer: imported %ld tags", copied_count);
}

static void add_variant(long datum)
{
	struct actor_variant_definition *variant;
	char const *unit_name;

	if (variant_count >= POOL_MAX || datum == NONE || !tag_index_is_group(datum, ACTOR_VARIANT_DEFINITION_TAG))
		return;
	variant = actor_variant_definition_get(datum);
	if (!variant)
		return;
	unit_name = variant->unit_reference.index == NONE ? NULL : tag_get_name(variant->unit_reference.index);
	if (!pool_allows(classify(unit_name)))
		return;
	variant_pool[variant_count++] = datum;
}

static void add_weapon(long datum)
{
	long index;

	if (weapon_count >= WEAPON_MAX || datum == NONE || !tag_index_is_group(datum, WEAPON_DEFINITION_TAG))
		return;
	for (index = 0; index < weapon_count; index++)
	{
		if (weapon_pool[index] == datum)
			return;
	}
	weapon_pool[weapon_count++] = datum;
}

static void build_pools(void)
{
	struct scenario *scenario = global_scenario_get();
	long count = 0;
	struct cache_file_tag_instance *instances = cache_files_tag_instances(&count);
	long index;

	variant_count = 0;
	weapon_count = 0;
	if (!current.enemies && !current.pickups && !current.weapons)
		return;
	for (index = 0; instances && index < count; index++)
	{
		if (instances[index].group_tag == ACTOR_VARIANT_DEFINITION_TAG)
			add_variant(instances[index].tag_index);
	}
	if (scenario)
	{
		for (index = 0; index < scenario->weapon_palette.count; index++)
		{
			struct scenario_object_palette_entry *entry = tag_block_get_element_with_size(
				&scenario->weapon_palette, index, sizeof(*entry));

			if (entry)
				add_weapon(entry->reference.index);
		}
	}
	for (index = 0; index < variant_count; index++)
	{
		struct actor_variant_definition *variant = actor_variant_definition_get(variant_pool[index]);

		if (variant)
			add_weapon(variant->ranged_combat.reference.index);
	}
}

static void remember_protected(char const *name)
{
	if (!name || !name[0] || protected_count >= PROTECTED_MAX)
		return;
	snprintf(protected_names[protected_count], sizeof(protected_names[0]), "%s", name);
	protected_count++;
}

static void build_protected(void)
{
	struct scenario *scenario = global_scenario_get();
	long conversation_index;

	protected_count = 0;
	if (!scenario)
		return;
	for (conversation_index = 0; conversation_index < scenario->ai_conversations.count; conversation_index++)
	{
		struct ai_conversation *conversation = tag_block_get_element_with_size(
			&scenario->ai_conversations, conversation_index, sizeof(*conversation));
		long participant_index;

		if (!conversation)
			continue;
		for (participant_index = 0; participant_index < conversation->participants.count; participant_index++)
		{
			struct randomizer_participant *participant = tag_block_get_element_with_size(
				&conversation->participants, participant_index, sizeof(*participant));

			if (participant)
				remember_protected(participant->ai_index_name);
		}
	}
}

static int squad_is_protected(char const *name)
{
	int index;

	if (!name || !name[0])
		return 0;
	for (index = 0; index < protected_count; index++)
	{
		if (!_stricmp(name, protected_names[index]))
			return 1;
	}

	return 0;
}

static unsigned long placement_seed(long encounter_index, long squad_index, long location_index, float const *position)
{
	unsigned long seed = mix_text(0x524E444DUL, map_name_leaf);

	seed = mix(seed ^ (unsigned long)current.enemies);
	seed = mix(seed ^ ((unsigned long)current.pickups << 1));
	seed = mix(seed ^ ((unsigned long)current.weapons << 2));
	seed = mix(seed ^ (unsigned long)current.pool);
	seed = mix(seed ^ (unsigned long)current.type);
	seed = mix(seed ^ (unsigned long)encounter_index);
	seed = mix(seed ^ (unsigned long)squad_index);
	seed = mix(seed ^ (unsigned long)location_index);
	if (position)
	{
		seed = mix_bits(seed, position[0]);
		seed = mix_bits(seed, position[1]);
		seed = mix_bits(seed, position[2]);
	}

	return seed;
}

static long pool_pick(long const *pool, long count, unsigned long seed)
{
	if (count <= 0)
		return NONE;

	return pool[seed % (unsigned long)count];
}

static void import_campaign(char const *leaf)
{
	int level;

	if (!current.enemies)
		return;
	wanted = calloc(WANTED_MAX, sizeof(*wanted));
	if (!wanted)
	{
		platform_log("randomizer: no room to gather tags");
		return;
	}
	for (level = 0; level < 10 && donor_count < DONOR_COUNT; level++)
	{
		char const *path = main_get_solo_level_name(level);
		char const *other = tag_name_strip_path(path);
		struct donor *donor = donors + donor_count;

		if (!_stricmp(other, leaf))
			continue;
		if (!open_donor(donor, other))
		{
			platform_log("randomizer: could not read %s.map", other);
			close_donor_bytes(donor);
			continue;
		}
		donor_count++;
		seed_donor(donor);
	}
	import_wanted();
}

void randomizer_tags_loaded(char const *scenario_name)
{
	struct network_game *game = network_game_get_game();
	char leaf[32];

	randomizer_tags_unloaded();
	if (!campaign_leaf(scenario_name, leaf, sizeof(leaf)))
		return;
	if (network_game_distributed_client())
	{
		if (!remote_on)
			return;
		setup_from_remote(&current);
	}
	else
	{
		if (!config_boolean("game.randomizer"))
			return;
		setup_from_config(&current);
	}
	if (game && game->variant.game_engine_index != game_engine_none)
		return;
	if (!setup_swaps(&current))
		return;
	snprintf(map_name_leaf, sizeof(map_name_leaf), "%s", leaf);
	import_campaign(leaf);
	build_pools();
	build_protected();
	loaded = 1;
	{
		int index;

		for (index = 0; index < donor_count; index++)
			close_donor_bytes(donors + index);
	}
	if (wanted)
		free(wanted);
	wanted = NULL;
	platform_log("randomizer: %s, %ld actors, %ld weapons", leaf, variant_count, weapon_count);
}

void randomizer_tags_unloaded(void)
{
	donors_close();
	held_free();
	if (saved_instances)
		cache_files_set_tag_instances(saved_instances, saved_count);
	if (grown_instances)
		free(grown_instances);
	grown_instances = NULL;
	saved_instances = NULL;
	saved_count = 0;
	grown_count = 0;
	if (wanted)
		free(wanted);
	wanted = NULL;
	wanted_count = 0;
	copied_count = 0;
	import_count = 0;
	import_stopped = 0;
	variant_count = 0;
	weapon_count = 0;
	protected_count = 0;
	loaded = 0;
	map_widget = NULL;
}

int randomizer_donor_read(long tag_index, long offset, long size, void *buffer)
{
	long absolute;
	long index;
	struct donor *donor;
	size_t read;

	if (!loaded || tag_index == NONE || !buffer || offset < 0 || size < 0)
		return 0;
	absolute = tag_index & 0xFFFF;
	for (index = 0; index < import_count; index++)
	{
		if (import_absolute[index] == absolute)
			break;
	}
	if (index >= import_count)
		return 0;
	donor = donors + import_donor[index];
	memset(buffer, 0, (unsigned long)size);
	if (!donor->file || offset >= donor->file_length)
		return 1;
	if (size > donor->file_length - offset)
		size = donor->file_length - offset;
	if (fseek(donor->file, offset, SEEK_SET) != 0)
		return 1;
	read = fread(buffer, 1, (unsigned long)size, donor->file);
	if (read < (unsigned long)size)
		memset((char *)buffer + read, 0, (unsigned long)size - read);

	return 1;
}

long randomizer_actor_variant(long original, long encounter_index, long squad_index, void *starting_location)
{
	struct actor_starting_location *location = starting_location;
	struct scenario *scenario;
	struct encounter_definition *encounter;
	struct squad_definition *squad;
	long encounter_block;
	long location_index = 0;
	unsigned long seed;

	if (!loaded || !current.enemies || original == NONE || variant_count <= 0 || !location)
		return original;
	if (encounter_index == NONE || squad_index == NONE)
		return original;
	if (location->command_list_index != NONE)
		return original;
	scenario = global_scenario_get();
	encounter_block = DATUM_INDEX_TO_ABSOLUTE_INDEX(encounter_index);
	encounter = scenario ? tag_block_get_element_with_size(&scenario->ai_encounters, encounter_block, sizeof(*encounter)) :
		NULL;
	squad = encounter ? tag_block_get_element_with_size(&encounter->squads, squad_index, sizeof(*squad)) : NULL;
	if (!squad)
		return original;
	if (squad_is_protected(squad->name))
		return original;
	{
		struct actor_starting_location *first = tag_block_get_element_with_size(
			&squad->starting_locations, 0, sizeof(*first));

		if (first && location >= first && location < first + squad->starting_locations.count)
			location_index = (long)(location - first);
	}
	seed = placement_seed(encounter_block, squad_index, location_index, &location->position.x);
	{
		long picked = pool_pick(variant_pool, variant_count, seed);

		return picked == NONE ? original : picked;
	}
}

long randomizer_weapon_definition(long definition_index, void *scenario_object)
{
	struct scenario_object_datum *object = scenario_object;
	unsigned long seed;
	long picked;

	if (!loaded || !current.pickups || definition_index == NONE || weapon_count <= 0 || !object)
		return definition_index;
	if (object->name_index != NONE || !tag_index_is_group(definition_index, WEAPON_DEFINITION_TAG))
		return definition_index;
	seed = placement_seed(object->palette_entry_index, object->name_index, 0, &object->position.x);
	picked = pool_pick(weapon_pool, weapon_count, seed);

	return picked == NONE ? definition_index : picked;
}

long randomizer_enemy_weapon(long weapon_definition, long unit_index, long attempt)
{
	struct unit_datum *unit;
	unsigned long seed;
	long picked;

	(void)unit_index;
	if (!loaded || !current.weapons || weapon_count <= 0)
		return attempt == 0 ? weapon_definition : NONE;
	if (attempt < 0 || attempt >= 8)
		return NONE;
	unit = unit_try_and_get(unit_index);
	/* the place it spawned, not the unit's index: a checkpoint's unit is a
	new one, and the weapon has to be the one it had */
	seed = placement_seed(weapon_definition, 0, attempt, unit ? &unit->object.position.x : NULL);
	picked = pool_pick(weapon_pool, weapon_count, seed);

	return picked == NONE ? weapon_definition : picked;
}

void randomizer_advertisement(unsigned char *reserved)
{
	struct network_game *game = network_game_get_game();
	struct setup setup;
	char leaf[32];

	if (!reserved || !game || game->variant.game_engine_index != game_engine_none)
		return;
	if (!config_boolean("game.randomizer") || !campaign_leaf(game->map.name, leaf, sizeof(leaf)))
		return;
	setup_from_config(&setup);
	if (!setup_swaps(&setup))
		return;
	reserved[HALO_PORT_ADVERTISED_FLAGS_OFFSET] =
		(unsigned char)(reserved[HALO_PORT_ADVERTISED_FLAGS_OFFSET] | HALO_PORT_ADVERTISED_RANDOMIZER_FLAG);
	reserved[HALO_PORT_ADVERTISED_RANDOMIZER_SETUP_OFFSET] =
		(unsigned char)((setup.enemies ? 1 : 0) | (setup.pickups ? 2 : 0) | (setup.weapons ? 4 : 0));
	reserved[HALO_PORT_ADVERTISED_RANDOMIZER_SETUP_OFFSET + 1] = (unsigned char)setup.pool;
	reserved[HALO_PORT_ADVERTISED_RANDOMIZER_SETUP_OFFSET + 2] = (unsigned char)setup.type;
}

void randomizer_set_remote(int on, unsigned char const *setup)
{
	remote_on = on && setup;
	if (remote_on)
		memcpy(remote_setup, setup, sizeof(remote_setup));
}

int randomizer_remote_on(void)
{
	return remote_on;
}

int randomizer_show_prefix(void)
{
	return host_randomized();
}

void randomizer_note_map_widget(void *widget)
{
	map_widget = widget;
}

wchar_t const *randomizer_level_title_text(void *widget, wchar_t const *string)
{
	static wchar_t const prefix[] = L"[RNDM MOD] ";
	int index;
	int out;

	if (widget != map_widget || !string || !randomizer_show_prefix())
		return NULL;
	for (out = 0; prefix[out] && out < (int)NUMBEROF(prefixed_title) - 1; out++)
		prefixed_title[out] = prefix[out];
	for (index = 0; string[index] && out < (int)NUMBEROF(prefixed_title) - 1; index++, out++)
		prefixed_title[out] = string[index];
	prefixed_title[out] = 0;

	return prefixed_title;
}
