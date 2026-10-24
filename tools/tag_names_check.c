/*
TAG_NAMES_CHECK.C

Runs the Tag Injector's names for a protected map's tags
(port/linux/game/tag_names.c) on a map's tag table made here, as a Custom
Edition map's is once its loader, conversion and validator are done with
it: tags of the game's schemas, their references set, the names a
protector leaves ("<protected>", nothing, one path many tags share). Prints
each tag's index, group, whether its name was made, and its name; then
whether the same table, its names as the map had them, is given the same
names again. tools/test_tag_names.py checks what it prints.

Built as the game's units are (its structures and schemas the game's), by
ninja as build/web/tools/tag_names_check.js, and run with node.
*/

#include "cseries.h"
#include "interface/hud_definitions.h"
#include "items/item_definitions.h"
#include "text/text_group.h"
#include "tag_files/tag_groups.h"
#include "tag_schema.h"
#include "tag_names.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the C library's own, which the shims below call: cseries.h and the
port's stdio.h name the game's) */
#undef memset
#undef memcpy
#undef memcmp
#undef strlen
#undef strcat
#undef strcmp
#undef snprintf
#undef vsnprintf
#undef printf
#undef fprintf
#undef malloc
#undef free
#undef realloc

/* ---------- constants */

enum
{
	TAG_COUNT = 16,
	SALT = 0xE174,
};

/* ---------- structures */

/* a tag of a Custom Edition map's table (tag_names.c's) */
struct check_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long in_resource_map;
	unsigned long unused;
};

struct check_header
{
	struct check_instance *instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
};

/* ---------- the game's, which tag_names.c and the schemas (the validator's
checks among them) call by these names */

void tag_validate_report(char const *message)
{
	(void)message;
}

int halo_linux_vsnprintf(char *buffer, size_t count, char const *format, va_list arguments)
{
	return vsnprintf(buffer, count, format, arguments);
}

int halo_linux_snprintf(char *buffer, size_t count, char const *format, ...)
{
	va_list arguments;
	int result;

	va_start(arguments, format);
	result = vsnprintf(buffer, count, format, arguments);
	va_end(arguments);
	return result;
}

void *csmemset(void *buffer, long c, unsigned long size)
{
	return memset(buffer, (int)c, size);
}

void *csmemcpy(void *destination, void const *source, unsigned long size)
{
	return memcpy(destination, source, size);
}

unsigned long csstrlen(char const *s)
{
	return strlen(s);
}

long csstrcmp(char const *s1, char const *s2)
{
	return strcmp(s1, s2);
}

void *debug_malloc(unsigned int size, unsigned char clear, char const *file, long line)
{
	(void)file;
	(void)line;
	return clear ? calloc(size, 1) : malloc(size);
}

void debug_free(void *pointer, char const *file, long line)
{
	(void)file;
	(void)line;
	free(pointer);
}

void *debug_realloc(void *pointer, unsigned int size, char const *file, long line)
{
	(void)file;
	(void)line;
	return realloc(pointer, size);
}

/* ---------- the tags */

static struct check_instance instances[TAG_COUNT];
static char const *original_names[TAG_COUNT];
static struct check_header header = { instances, NONE, 0, TAG_COUNT };

static struct tag_schema_group const *check_group(unsigned long group_tag)
{
	struct tag_schema_group const *const *list;
	struct tag_schema_group const *group;

	for (group = tag_schema_custom_edition_groups; group->group_tag; group++)
	{
		if (group->group_tag == group_tag)
			return group;
	}
	for (list = tag_schema_group_lists; *list; list++)
	{
		for (group = *list; group->group_tag; group++)
		{
			if (group->group_tag == group_tag)
				return group;
		}
	}
	fprintf(stderr, "no schema of %08lx\n", group_tag);
	exit(2);
}

/* a tag of the table: its root as large as its group's, all zero */
static void tag_make(long index, unsigned long group_tag, char const *name, boolean in_resource_map)
{
	struct tag_schema_group const *group = check_group(group_tag);

	instances[index].group_tag = group_tag;
	instances[index].parent_group_tags[0] = group->parent_group_tags[0];
	instances[index].parent_group_tags[1] = group->parent_group_tags[1];
	instances[index].tag_index = (long)((unsigned long)SALT << 16 | (unsigned long)index);
	instances[index].name = (char *)name;
	instances[index].base_address = calloc((size_t)group->definition->size, 1);
	instances[index].in_resource_map = in_resource_map;
	original_names[index] = name;
}

/* the offset of a reference field `name` of a definition, in it or in the
structures it has (not its blocks), or NONE */
static long field_find(struct tag_schema_definition const *definition, char const *name)
{
	struct tag_schema_field const *field;

	for (field = definition->fields; field->type != _tag_schema_terminator; field++)
	{
		if (field->type == _tag_schema_reference && !strcmp(field->name, name))
			return field->offset;
		if (field->type == _tag_schema_struct)
		{
			long inner = field_find(field->definition, name);

			if (inner != NONE)
				return field->offset + inner;
		}
	}
	return NONE;
}

/* a tag's reference of the field `name` naming another */
static void reference_set(long from, char const *name, long to)
{
	struct tag_schema_group const *group = check_group(instances[from].group_tag);
	long offset = field_find(group->definition, name);
	struct tag_reference *reference;

	if (offset == NONE)
	{
		fprintf(stderr, "no reference %s in %08lx\n", name, instances[from].group_tag);
		exit(2);
	}
	reference = (struct tag_reference *)((byte *)instances[from].base_address + offset);
	reference->group_tag = instances[to].group_tag;
	reference->index = instances[to].tag_index;
}

/* the HUD's item texts: a string list of UTF-16 strings */
static void strings_set(long index, char const *const *texts, long count)
{
	struct string_list *list = instances[index].base_address;
	struct string_list_entry *entries = calloc((size_t)count, sizeof(*entries));
	long text;

	list->strings.count = count;
	list->strings.address = entries;
	for (text = 0; text < count; text++)
	{
		long length = (long)strlen(texts[text]);
		unsigned short *wide = calloc((size_t)length + 1, sizeof(unsigned short));
		long character;

		for (character = 0; character < length; character++)
			wide[character] = (unsigned char)texts[text][character];
		entries[text].string.address = wide;
		entries[text].string.size = (length + 1) * (long)sizeof(unsigned short);
	}
}

static void names_print(char const *label, unsigned char const *made)
{
	long index;

	printf("%s\n", label);
	for (index = 0; index < TAG_COUNT; index++)
	{
		char group[5];
		short character;

		for (character = 0; character < 4; character++)
			group[character] = (char)(instances[index].group_tag >> (24 - 8 * character));
		group[4] = 0;
		printf("%ld\t%s\t%d\t%s\n", index, group, made && made[index], instances[index].name ? instances[index].name :
			"");
	}
}

int main(void)
{
	static char const *const texts[] = { "Battle Rifle", "%d battle rifle rounds" };
	char *first[TAG_COUNT];
	unsigned char *made = NULL;
	long count;
	long index;
	boolean same = TRUE;

	tag_make(0, 'scnr', "<protected>", FALSE);
	tag_make(1, 'matg', "<protected>", FALSE);
	tag_make(2, 'hudg', "<protected>", FALSE);
	tag_make(3, 'ustr', "ui\\hud\\hud item messages", TRUE);
	tag_make(4, 'weap', "<protected>", FALSE);
	tag_make(5, 'weap', "<protected>", FALSE);
	tag_make(6, 'mod2', "<protected>", FALSE);
	tag_make(7, 'bipd', "<protected>", FALSE);
	tag_make(8, 'mod2', "<protected>", FALSE);
	tag_make(9, 'coll', "characters\\elite\\elite", FALSE);
	tag_make(10, 'actv', "<protected>", FALSE);
	tag_make(11, 'actv', "", FALSE);
	tag_make(12, 'bitm', "levels\\shared\\decal", FALSE);
	tag_make(13, 'bitm', "levels\\shared\\decal", FALSE);
	tag_make(14, 'bitm', "levels\\shared\\stock", TRUE);
	tag_make(15, 'bitm', "levels\\shared\\stock", FALSE);
	header.scenario_tag_index = instances[0].tag_index;

	reference_set(2, "messaging.hud_item_messages", 3);
	strings_set(3, texts, 2);
	/* (both weapons' pickup text the first: "battle rifle") */
	((struct item_definition *)instances[4].base_address)->item.hud_message_index = 0;
	((struct item_definition *)instances[5].base_address)->item.hud_message_index = 0;
	reference_set(4, "model", 6);
	reference_set(7, "model", 8);
	reference_set(7, "collision_model", 9);
	reference_set(10, "unit_reference", 7);
	reference_set(10, "ranged_combat.reference", 4);
	reference_set(11, "unit_reference", 7);
	reference_set(11, "ranged_combat.reference", 4);

	count = tag_names_restore(&header, "Descent-Quarantine", &made);
	printf("made\t%ld\n", count);
	names_print("names", made);
	for (index = 0; index < TAG_COUNT; index++)
	{
		char const *name = instances[index].name ? instances[index].name : "";

		first[index] = malloc(strlen(name) + 1);
		memcpy(first[index], name, strlen(name) + 1);
	}
	free(made);

	/* (loaded again: the map's names, given the same) */
	for (index = 0; index < TAG_COUNT; index++)
		instances[index].name = (char *)original_names[index];
	tag_names_restore(&header, "Descent-Quarantine", NULL);
	for (index = 0; index < TAG_COUNT; index++)
		same = same && !strcmp(first[index], instances[index].name ? instances[index].name : "");
	printf("same\t%d\n", same);

	/* (names of their own all: none made) */
	printf("again\t%ld\n", tag_names_restore(&header, "Descent-Quarantine", NULL));
	tag_names_dispose();

	return 0;
}
