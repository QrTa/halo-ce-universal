/*
CE_TAGS.C

Finds and takes tags out of Halo Custom Edition maps, as tag bundles
(port/linux/game/tag_bundle.h; docs/custom_edition_caches.md, "Tag
bundles"):

	ce_tags check FOLDER
		FOLDER as the game takes a custom_maps folder: Custom Edition's
		bitmaps.map, sounds.map and loc.map in it, each the resource map it
		is named for, and the maps in it and in one level of its subfolders,
		with the resource maps each needs.
	ce_tags find MAP [TEXT] [--group GROUP]
		MAP's tags whose path has TEXT in it (any case), of GROUP (as
		'weap', or weap), each with its index, group, path and where the map
		read it from.
	ce_tags extract MAP TAG [--group GROUP] [--out FILE]
		Writes the bundle of MAP's tag TAG (its path, any case, or its index)
		and every tag it names to FILE (by default the last part of its path
		and .tagbundle), and lists them.
	ce_tags info FILE
		What a bundle holds.

A map is read as the game reads one (port/linux/game/custom_edition_cache.c):
its loader reads it with its resource maps into the Custom Edition tag
cache, its bytes are converted for this build, and the tag validator checks
its tags (port/linux/game/tag_validate.c), whose corrections are made and
counted (--verbose prints them). The resource maps are those of --maps
FOLDER, else those beside the map, else those of the folder above it (the
game keeps them in custom_maps itself, and a map may be in a subfolder).
OpenSauce maps are refused, as the game refuses them.

Built with the validator's objects of the game: by ninja web as
build/web/tools/ce_tags.js, run with node (its tag cache in WebAssembly's
memory, at the address Custom Edition maps are linked to:
tools/web_build.py), and wherever a 32-bit build can map that address.
*/

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef __EMSCRIPTEN__
#include <unistd.h>
#else
#include <sys/mman.h>
#endif

#include "../port/linux/game/cache_file_formats.h"

/* (cseries.h's, for tag_bundle.h) */
typedef unsigned char boolean;
typedef unsigned char byte;
#include "../port/linux/game/tag_bundle.h"

/* ---------- constants */

enum
{
	/* where a Custom Edition map's bitmaps.map and sounds.map count in the
	offsets of its tags, after the map itself (tools/map_validate.c's) */
	CUSTOM_EDITION_BITMAPS_OFFSET = 0x40000000,
	CUSTOM_EDITION_SOUNDS_OFFSET = 0x60000000,

	PATH_BYTES = 1024,
	NONE = -1,
};

/* ---------- structures */

struct tag_validate_file_range
{
	unsigned long offset;
	unsigned long size;
};

/* a tag in the tags' table (tag_validate.c's struct tag_validate_instance) */
struct tag_instance
{
	unsigned long group_tag;
	unsigned long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long in_resource_map;
	unsigned long unused;
};

struct tag_header
{
	struct tag_instance *instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
};

/* a file read as it is asked for */
struct stdio_file
{
	FILE *stream;
	unsigned long size;
	struct cache_file_source source;
};

/* a Custom Edition map, its resource maps, and its tags in the tag cache */
struct loaded_map
{
	struct stdio_file map;
	struct stdio_file resource_files[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map resource_storage[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct resource_map *resource_maps[NUMBER_OF_RESOURCE_MAP_TYPES];
	struct custom_edition_load_report report;
	struct tag_header *tags;
	unsigned long loaded_bytes;
	long corrections;
};

/* ---------- the validator's (port/linux/game/tag_validate.c) */

unsigned char tag_validate_custom_edition_tags(void *tag_header, long loaded_size, unsigned long tag_cache_size,
	struct tag_validate_file_range const *file_ranges, short file_range_count, char const *map_name);
long tag_validate_corrections(void);

static int verbose;

void tag_validate_report(char const *message)
{
	if (verbose)
		printf("  %s\n", message);
}

/* (the game's printf family, which the validator calls by these names:
port/linux/include/stdio.h) */
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

/* (the game's string functions, which its units call by these names:
cseries.h) */
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

char *csstrcat(char *s1, char const *s2)
{
	return strcat(s1, s2);
}

long csstrcmp(char const *s1, char const *s2)
{
	return strcmp(s1, s2);
}

/* (the game's allocator, which its units call by these names: cseries.h) */
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

/* ---------- files */

static int stdio_file_read(void *context, uint32_t offset, uint32_t size, void *buffer)
{
	struct stdio_file *file = context;

	if (offset > file->size || size > file->size - offset)
		return 0;
	return fseek(file->stream, (long)offset, SEEK_SET) == 0 && fread(buffer, 1, size, file->stream) == size;
}

static int stdio_file_open(struct stdio_file *file, char const *path)
{
	long size;

	memset(file, 0, sizeof(*file));
	file->stream = fopen(path, "rb");
	if (!file->stream)
		return 0;
	if (fseek(file->stream, 0, SEEK_END) || (size = ftell(file->stream)) < 0)
	{
		fclose(file->stream);
		file->stream = NULL;
		return 0;
	}
	file->size = (unsigned long)size;
	file->source.context = file;
	file->source.read = stdio_file_read;
	file->source.size = (uint32_t)size;
	return 1;
}

static void stdio_file_close(struct stdio_file *file)
{
	if (file->stream)
		fclose(file->stream);
	memset(file, 0, sizeof(*file));
}

static int is_file(char const *path)
{
	struct stat status;

	return stat(path, &status) == 0 && S_ISREG(status.st_mode);
}

static int is_folder(char const *path)
{
	struct stat status;

	return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

/* the folder part of path, without its separator ("." for none) */
static void folder_of(char const *path, char *folder)
{
	char const *slash = strrchr(path, '/');

	if (!slash)
		snprintf(folder, PATH_BYTES, ".");
	else
		snprintf(folder, PATH_BYTES, "%.*s", (int)(slash - path), path);
}

/* where a map's resource maps are: --maps, beside it, or the folder above */
static void resource_folder_of(char const *map_path, char const *maps_option, char *folder)
{
	char beside[PATH_BYTES];
	char path[PATH_BYTES];

	if (maps_option)
	{
		snprintf(folder, PATH_BYTES, "%s", maps_option);
		return;
	}
	folder_of(map_path, beside);
	snprintf(path, sizeof(path), "%s/bitmaps.map", beside);
	if (is_file(path))
	{
		snprintf(folder, PATH_BYTES, "%s", beside);
		return;
	}
	snprintf(path, sizeof(path), "%s/..", beside);
	snprintf(folder, PATH_BYTES, "%s", path);
}

static void group_text(unsigned long group_tag, char text[5])
{
	short index;

	for (index = 0; index < 4; index++)
	{
		char character = (char)(group_tag >> (24 - 8 * index));

		text[index] = character >= 0x20 && character < 0x7F ? character : '?';
	}
	text[4] = 0;
}

/* a group as given ('weap', weap, or shorter: padded with spaces) */
static int group_parse(char const *text, unsigned long *group_tag)
{
	size_t length;
	size_t index;

	if (text[0] == '\'')
		text++;
	length = strcspn(text, "'");
	if (length == 0 || length > 4)
		return 0;
	*group_tag = 0;
	for (index = 0; index < 4; index++)
		*group_tag = *group_tag << 8 | (unsigned char)(index < length ? text[index] : ' ');
	return 1;
}

/* ---------- maps */

static void loaded_map_close(struct loaded_map *loaded)
{
	short type;

	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		if (loaded->resource_maps[type])
			resource_map_close(loaded->resource_maps[type]);
		stdio_file_close(&loaded->resource_files[type]);
	}
	stdio_file_close(&loaded->map);
}

/* reads in the combined space of a map's files (tag_bundle_source) */
static boolean loaded_map_read(void *context, unsigned long offset, unsigned long size, void *buffer)
{
	struct loaded_map *loaded = context;
	struct stdio_file *file = &loaded->map;
	unsigned long base = 0;

	if (offset >= CUSTOM_EDITION_SOUNDS_OFFSET)
	{
		file = &loaded->resource_files[_resource_map_sounds];
		base = CUSTOM_EDITION_SOUNDS_OFFSET;
	}
	else if (offset >= CUSTOM_EDITION_BITMAPS_OFFSET)
	{
		file = &loaded->resource_files[_resource_map_bitmaps];
		base = CUSTOM_EDITION_BITMAPS_OFFSET;
	}
	return file->stream && stdio_file_read(file, (uint32_t)(offset - base), (uint32_t)size, buffer);
}

/* a Custom Edition map loaded, converted and checked as the game does it,
into tag_cache (at its address): 1 if its tags can be used, otherwise 0
(said why) */
static int loaded_map_open(struct loaded_map *loaded, char const *path, char const *maps_option,
	unsigned char *tag_cache)
{
	struct cache_file_identity identity;
	struct custom_edition_conversion_report conversion;
	struct tag_validate_file_range ranges[3];
	short range_count = 0;
	char folder[PATH_BYTES];
	enum cache_file_status status;
	uint32_t needed = 0;
	short type;

	memset(loaded, 0, sizeof(*loaded));
	if (!stdio_file_open(&loaded->map, path))
	{
		printf("%s: cannot be read (%s)\n", path, strerror(errno));
		return 0;
	}
	status = cache_file_identify(&loaded->map.source, &identity);
	if (status != _cache_file_status_ok || identity.format != _cache_file_format_custom_edition_cache)
	{
		printf("%s: not a Custom Edition map (%s, %s)\n", path, cache_file_format_describe(identity.format),
			cache_file_status_describe(status));
		return 0;
	}
	cache_file_resource_maps_needed(&loaded->map.source, &needed);
	resource_folder_of(path, maps_option, folder);
	for (type = _resource_map_bitmaps; type < NUMBER_OF_RESOURCE_MAP_TYPES; type++)
	{
		char resource_path[PATH_BYTES];

		snprintf(resource_path, sizeof(resource_path), "%s/%s.map", folder,
			resource_map_type_describe((enum resource_map_type)type));
		if (!stdio_file_open(&loaded->resource_files[type], resource_path))
		{
			if (needed & (1u << type))
				printf("%s: needs %s, which is not there\n", path, resource_path);
			continue;
		}
		status = resource_map_open(&loaded->resource_files[type].source, (enum resource_map_type)type,
			&loaded->resource_storage[type]);
		if (status != _cache_file_status_ok)
		{
			printf("%s: %s\n", resource_path, cache_file_status_describe(status));
			continue;
		}
		loaded->resource_maps[type] = &loaded->resource_storage[type];
	}

	memset(tag_cache, 0, CUSTOM_EDITION_TAG_CACHE_BYTES);
	status = custom_edition_cache_load(&loaded->map.source, loaded->resource_maps, tag_cache,
		CUSTOM_EDITION_TAG_CACHE_BYTES, &loaded->report);
	if (status != _cache_file_status_ok)
	{
		printf("%s: its loader refuses it: %s (tag %ld, %08lx)\n", path, cache_file_status_describe(status),
			(long)loaded->report.problem_tag_index, (unsigned long)loaded->report.problem_location);
		return 0;
	}
	loaded->loaded_bytes = loaded->report.tag_data_bytes + loaded->report.resource_tag_bytes;
	custom_edition_cache_combine_resource_offsets(tag_cache, loaded->loaded_bytes, CUSTOM_EDITION_BITMAPS_OFFSET,
		CUSTOM_EDITION_SOUNDS_OFFSET);
	status = custom_edition_cache_convert(tag_cache, loaded->loaded_bytes, loaded->report.identity.name, &conversion);
	if (status != _cache_file_status_ok)
	{
		printf("%s: its conversion refuses it: %s\n", path, cache_file_status_describe(status));
		return 0;
	}
	ranges[range_count].offset = 0;
	ranges[range_count++].size = loaded->report.identity.file_length;
	if (loaded->resource_maps[_resource_map_bitmaps])
	{
		ranges[range_count].offset = CUSTOM_EDITION_BITMAPS_OFFSET;
		ranges[range_count++].size = loaded->resource_files[_resource_map_bitmaps].size;
	}
	if (loaded->resource_maps[_resource_map_sounds])
	{
		ranges[range_count].offset = CUSTOM_EDITION_SOUNDS_OFFSET;
		ranges[range_count++].size = loaded->resource_files[_resource_map_sounds].size;
	}
	if (!tag_validate_custom_edition_tags(tag_cache, (long)loaded->loaded_bytes, loaded->report.tag_cache_bytes,
		ranges, range_count, loaded->report.identity.name))
	{
		printf("%s: the tag validator refuses it (--verbose says why)\n", path);
		return 0;
	}
	loaded->corrections = tag_validate_corrections();
	loaded->tags = (struct tag_header *)tag_cache;

	return 1;
}

static char const *tag_origin(struct tag_instance const *instance)
{
	return instance->in_resource_map ? "resource map" : "map";
}

static char const *tag_name(struct tag_instance const *instance)
{
	return instance->name ? instance->name : "";
}

static void tag_print(struct loaded_map const *loaded, long absolute_index)
{
	struct tag_instance const *instance = &loaded->tags->instances[absolute_index];
	char group[5];

	group_text(instance->group_tag, group);
	printf("%6ld  %s  %s  (%s)\n", absolute_index, group, tag_name(instance), tag_origin(instance));
}

/* ---------- commands */

static int command_info(char const *path, int list);

/* one file of a custom_maps folder, if it is a map */
static void check_map(char const *folder, char const *path, char const *name, int *maps, int *problems)
{
	size_t length = strlen(name);
	struct stdio_file file;
	struct cache_file_identity identity;
	enum cache_file_status status;
	uint32_t needed = 0;

	if (length < 4 || strcasecmp(name + length - 4, ".map") || !is_file(path) || !stdio_file_open(&file, path))
		return;
	(*maps)++;
	status = cache_file_identify(&file.source, &identity);
	if (status != _cache_file_status_ok || identity.format != _cache_file_format_custom_edition_cache)
	{
		printf("map: %s: not a Custom Edition map (%s)\n", path + strlen(folder) + 1,
			cache_file_format_describe(identity.format));
		(*problems)++;
	}
	else
	{
		cache_file_resource_maps_needed(&file.source, &needed);
		printf("map: %s: %s, needs%s%s%s%s\n", path + strlen(folder) + 1, identity.name,
			needed & (1u << _resource_map_bitmaps) ? " bitmaps.map" : "",
			needed & (1u << _resource_map_sounds) ? " sounds.map" : "",
			needed & (1u << _resource_map_locale) ? " loc.map" : "",
			needed ? "" : " no resource maps");
	}
	stdio_file_close(&file);
}

static int command_check(char const *folder)
{
	static char const *const resource_names[] = { "bitmaps.map", "sounds.map", "loc.map" };
	int problems = 0;
	int maps = 0;
	short index;
	DIR *directory;
	struct dirent *entry;

	if (!is_folder(folder))
	{
		printf("%s: no such folder\n", folder);
		return 1;
	}
	printf("folder: %s\n", folder);
	for (index = 0; index < 3; index++)
	{
		char path[PATH_BYTES];
		struct stdio_file file;
		struct resource_map map;
		enum cache_file_status status;

		snprintf(path, sizeof(path), "%s/%s", folder, resource_names[index]);
		if (!stdio_file_open(&file, path))
		{
			printf("%s: missing (Halo Custom Edition's maps folder has it)\n", resource_names[index]);
			problems++;
			continue;
		}
		status = resource_map_open(&file.source, (enum resource_map_type)(_resource_map_bitmaps + index), &map);
		if (status == _cache_file_status_ok)
		{
			printf("%s: ok, %ld items, %lu bytes\n", resource_names[index], (long)map.item_count, file.size);
			resource_map_close(&map);
		}
		else
		{
			printf("%s: %s\n", resource_names[index], cache_file_status_describe(status));
			problems++;
		}
		stdio_file_close(&file);
	}

	/* the maps, as the game lists them: the folder, then one level of its
	subfolders */
	directory = opendir(folder);
	while (directory && (entry = readdir(directory)))
	{
		char path[PATH_BYTES];

		if (entry->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, entry->d_name);
		if (is_folder(path))
		{
			DIR *subfolder = opendir(path);
			struct dirent *subentry;

			while (subfolder && (subentry = readdir(subfolder)))
			{
				char map_path[PATH_BYTES];

				if (subentry->d_name[0] == '.')
					continue;
				snprintf(map_path, sizeof(map_path), "%s/%s", path, subentry->d_name);
				check_map(folder, map_path, subentry->d_name, &maps, &problems);
			}
			if (subfolder)
				closedir(subfolder);
		}
		else if (strcasecmp(entry->d_name, "bitmaps.map") && strcasecmp(entry->d_name, "sounds.map") &&
			strcasecmp(entry->d_name, "loc.map"))
		{
			check_map(folder, path, entry->d_name, &maps, &problems);
		}
	}
	if (directory)
		closedir(directory);
	printf("%d maps; %s\n", maps, problems ? "problems above" : "ready");

	return problems ? 1 : 0;
}

/* the tag TAG names: its index, or its path (and group); NONE (said why)
if none or several */
static long tag_find(struct loaded_map const *loaded, char const *tag, int has_group, unsigned long group_tag)
{
	char *end;
	long number = strtol(tag, &end, 10);
	long found = NONE;
	int matches = 0;
	long index;

	if (*tag && !*end)
	{
		if (number < 0 || number >= loaded->tags->tag_count)
		{
			printf("the map has no tag %ld\n", number);
			return NONE;
		}
		return number;
	}
	for (index = 0; index < loaded->tags->tag_count; index++)
	{
		struct tag_instance const *instance = &loaded->tags->instances[index];

		if (strcasecmp(tag_name(instance), tag) || (has_group && instance->group_tag != group_tag))
			continue;
		if (matches++)
		{
			if (matches == 2)
			{
				printf("several tags are %s: name one with --group, or by its index\n", tag);
				tag_print(loaded, found);
			}
			tag_print(loaded, index);
		}
		found = index;
	}
	if (!matches)
		printf("the map has no tag %s%s (ce_tags find lists them)\n", tag, has_group ? " of that group" : "");

	return matches == 1 ? found : NONE;
}

static int command_find(struct loaded_map const *loaded, char const *text, int has_group, unsigned long group_tag)
{
	long index;
	long found = 0;

	for (index = 0; index < loaded->tags->tag_count; index++)
	{
		struct tag_instance const *instance = &loaded->tags->instances[index];
		char const *name = tag_name(instance);
		size_t text_length = text ? strlen(text) : 0;
		size_t at;
		int matched = !text_length;

		if (has_group && instance->group_tag != group_tag)
			continue;
		for (at = 0; !matched && name[at]; at++)
			matched = !strncasecmp(name + at, text, text_length);
		if (!matched)
			continue;
		tag_print(loaded, index);
		found++;
	}
	printf("%ld tags\n", found);

	return 0;
}

static boolean file_write(void *context, void const *bytes, unsigned long size)
{
	return fwrite(bytes, 1, size, context) == size;
}

static int command_extract(struct loaded_map *loaded, char const *tag, int has_group, unsigned long group_tag,
	char const *out_option)
{
	struct tag_bundle_source source;
	struct tag_bundle_report report;
	char out[PATH_BYTES];
	long tag_index = tag_find(loaded, tag, has_group, group_tag);
	long *indices = NULL;
	long count = 0;
	long problem = NONE;
	long index;
	enum tag_bundle_status status;
	FILE *stream;
	int closed;

	if (tag_index == NONE)
		return 1;
	memset(&source, 0, sizeof(source));
	source.tag_header = loaded->tags;
	source.loaded_size = (long)loaded->loaded_bytes;
	source.read = loaded_map_read;
	source.context = loaded;
	source.map_name = loaded->report.identity.name;
	source.map_build = loaded->report.identity.build;
	if (out_option)
	{
		snprintf(out, sizeof(out), "%s", out_option);
	}
	else
	{
		char const *name = tag_name(&loaded->tags->instances[tag_index]);
		char const *last = strrchr(name, '\\');

		snprintf(out, sizeof(out), "%s.tagbundle", last ? last + 1 : name);
	}

	/* its tags first, so that a bundle that cannot be written says what led
	to the tag that stopped it */
	status = tag_bundle_tags(&source, tag_index, &indices, &count, &problem);
	if (status != _tag_bundle_status_ok)
	{
		printf("it cannot be bundled: %s:\n", tag_bundle_status_describe(status));
		if (problem != NONE)
			tag_print(loaded, problem);
		free(indices);
		return 1;
	}
	free(indices);
	stream = fopen(out, "wb");
	if (!stream)
	{
		printf("%s: cannot be written (%s)\n", out, strerror(errno));
		return 1;
	}
	tag_bundle_write(&source, tag_index, file_write, stream, &report);
	closed = fclose(stream) == 0;
	if (report.status != _tag_bundle_status_ok || !closed)
	{
		printf("%s: %s\n", out, closed ? tag_bundle_status_describe(report.status) : "cannot be written");
		if (report.problem_tag_index != NONE)
			tag_print(loaded, report.problem_tag_index);
		remove(out);
		return 1;
	}
	tag_bundle_tags(&source, tag_index, &indices, &count, &problem);
	for (index = 0; index < count; index++)
		tag_print(loaded, indices[index]);
	free(indices);
	printf("%s: %ld tags, %lu bytes of tags, %lu bytes of pixels, samples and vertices, %lu bytes\n", out,
		report.tag_count, report.data_size, report.file_data_size, report.size);

	/* (read back as the game would read it) */
	return command_info(out, 0);
}

static unsigned long bytes_u32(unsigned char const *bytes)
{
	return bytes[0] | bytes[1] << 8 | (unsigned long)bytes[2] << 16 | (unsigned long)bytes[3] << 24;
}

/* what a bundle holds, checked and loaded as the game would (tag_bundle_load) */
static int command_info(char const *path, int list)
{
	struct stdio_file file;
	struct tag_bundle_loaded loaded;
	enum tag_bundle_status status;
	byte *bundle = NULL;
	byte header[TAG_BUNDLE_HEADER_BYTES];
	long index;
	int result = 1;

	if (!stdio_file_open(&file, path) || !(bundle = malloc(file.size ? file.size : 1)) ||
		!stdio_file_read(&file, 0, (uint32_t)file.size, bundle))
	{
		printf("%s: cannot be read\n", path);
		goto done;
	}
	/* (the header as it is in the file: the load makes the data's pointers
	this process's) */
	memcpy(header, bundle, file.size < sizeof(header) ? file.size : sizeof(header));
	status = tag_bundle_load(bundle, file.size, &loaded);
	if (status != _tag_bundle_status_ok)
	{
		printf("%s: %s", path, tag_bundle_status_describe(status));
		if (loaded.problem_tag_index != NONE)
			printf(" (its tag %ld)", loaded.problem_tag_index);
		printf("\n");
		goto done;
	}
	printf("%s: a tag bundle of %s (%s): %ld tags, %lu bytes of tags, %lu bytes of file data; checked\n", path,
		loaded.names + bytes_u32(header + TAG_BUNDLE_HEADER_MAP_NAME_OFFSET),
		loaded.names + bytes_u32(header + TAG_BUNDLE_HEADER_MAP_BUILD_OFFSET), loaded.tag_count, loaded.data_size,
		loaded.file_data_size);
	for (index = 0; list && index < loaded.tag_count; index++)
	{
		byte const *entry = loaded.tags + index * TAG_BUNDLE_TAG_BYTES;
		char group[5];

		group_text(bytes_u32(entry + TAG_BUNDLE_TAG_GROUP_OFFSET), group);
		printf("%6ld  %s  %s  (%s)\n", index, group, loaded.names + bytes_u32(entry + TAG_BUNDLE_TAG_NAME_OFFSET),
			bytes_u32(entry + TAG_BUNDLE_TAG_FLAGS_OFFSET) & (1u << _tag_bundle_tag_from_resource_map_bit) ?
				"resource map" : "map");
	}
	result = 0;

done:
	free(bundle);
	stdio_file_close(&file);
	return result;
}

/* ---------- the tag cache */

/* the Custom Edition tag cache, at the address its tags are linked to */
static unsigned char *tag_cache_reserve(void)
{
#ifdef __EMSCRIPTEN__
	/* WebAssembly's memory is one block, which grows: the C heap's end is
	moved past the tag cache, so that no allocation is in it */
	uintptr_t end = (uintptr_t)sbrk(0);
	uintptr_t top = CUSTOM_EDITION_TAG_CACHE_ADDRESS + CUSTOM_EDITION_TAG_CACHE_BYTES;

	if (end > CUSTOM_EDITION_TAG_CACHE_ADDRESS || sbrk((intptr_t)(top - end)) == (void *)-1)
		return NULL;
	return (unsigned char *)CUSTOM_EDITION_TAG_CACHE_ADDRESS;
#else
	void *wanted = (void *)CUSTOM_EDITION_TAG_CACHE_ADDRESS;
	void *result = mmap(wanted, CUSTOM_EDITION_TAG_CACHE_BYTES, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);

	return result == wanted ? (unsigned char *)result : NULL;
#endif
}

static int usage(void)
{
	fprintf(stderr,
		"usage: ce_tags check FOLDER\n"
		"       ce_tags find MAP [TEXT] [--group GROUP] [--maps FOLDER]\n"
		"       ce_tags extract MAP TAG [--group GROUP] [--out FILE] [--maps FOLDER]\n"
		"       ce_tags info FILE\n"
		"       (--verbose prints the validator's corrections)\n");
	return 2;
}

int main(int argc, char **argv)
{
	char const *positional[3] = { NULL, NULL, NULL };
	char const *maps_option = NULL;
	char const *out_option = NULL;
	unsigned long group_tag = 0;
	int has_group = 0;
	int positional_count = 0;
	struct loaded_map loaded;
	unsigned char *tag_cache;
	char const *command;
	int result;
	int argument;

	if (argc < 2)
		return usage();
	command = argv[1];
	for (argument = 2; argument < argc; argument++)
	{
		if (!strcmp(argv[argument], "--verbose"))
			verbose = 1;
		else if (!strcmp(argv[argument], "--maps") && argument + 1 < argc)
			maps_option = argv[++argument];
		else if (!strcmp(argv[argument], "--out") && argument + 1 < argc)
			out_option = argv[++argument];
		else if (!strcmp(argv[argument], "--group") && argument + 1 < argc)
		{
			if (!group_parse(argv[++argument], &group_tag))
			{
				fprintf(stderr, "ce_tags: %s is not a group ('weap', weap)\n", argv[argument]);
				return 2;
			}
			has_group = 1;
		}
		else if (positional_count < 3)
			positional[positional_count++] = argv[argument];
		else
			return usage();
	}

	if (!strcmp(command, "check") && positional_count == 1)
		return command_check(positional[0]);
	if (!strcmp(command, "info") && positional_count == 1)
		return command_info(positional[0], 1);
	if (!(!strcmp(command, "find") && positional_count >= 1 && positional_count <= 2) &&
		!(!strcmp(command, "extract") && positional_count == 2))
	{
		return usage();
	}

	tag_cache = tag_cache_reserve();
	if (!tag_cache)
	{
		fprintf(stderr, "ce_tags: the Custom Edition tag cache's address (%08lx) is taken\n",
			(unsigned long)CUSTOM_EDITION_TAG_CACHE_ADDRESS);
		return 2;
	}
	if (!loaded_map_open(&loaded, positional[0], maps_option, tag_cache))
	{
		loaded_map_close(&loaded);
		return 1;
	}
	printf("%s: %s (%s), %ld tags, %ld corrected by the validator\n", positional[0], loaded.report.identity.name,
		loaded.report.identity.build, loaded.tags->tag_count, loaded.corrections);
	if (!strcmp(command, "find"))
		result = command_find(&loaded, positional[1], has_group, group_tag);
	else
		result = command_extract(&loaded, positional[1], has_group, group_tag, out_option);
	loaded_map_close(&loaded);

	return result;
}
