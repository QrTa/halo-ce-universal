/*
PORT_TAG_MP_PROFILES.C

The Tag Injector's multiplayer profiles (port_tag_mp_profiles.h): how a
multiplayer game on one of the game's own multiplayer maps plays the weapons
of a profile (port_tag_profiles.c), each a file of the data root's
tag_injector/.multiplayer folder (a folder no profile scan lists):

	tag_injector/
	  .multiplayer/
	    Shotgun Madness.toml

	name = "Shotgun Madness"
	profile = "Brutes"
	rotation = "add"
	replace = "manual"
	loadout = "set"
	primary = "cmt\\weapons\\covenant\\carbine\\carbine"
	secondary = "stock:pistol"

	[manual]
	shotgun = ["original", "cmt\\weapons\\covenant\\carbine\\carbine"]

Its settings (port/linux/game/tag_multiplayer.c says what each does) are
strings, any of them left out ""; [manual] has, for each of the map's weapons
by its key (the stock HUDs' names: "shotgun"), the weapons that take its
place, by their paths in the profile ("original": the map's own). A
setting's value is kept as it was given, but for its control characters.
*/

#include "platform.h"
#include "port_config.h"
#include "port_tag_mp_profiles.h"
#include "posix.h"
#include "tomlc17.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- constants */

#define MAXIMUM_MP_PROFILES 64
#define MAXIMUM_MANUAL_ENTRIES 256
#define MAXIMUM_FILE_LENGTH 64
#define MAXIMUM_NAME_LENGTH 64
#define MAXIMUM_KEY_LENGTH 32
#define MAXIMUM_VALUE_LENGTH 256
#define MAXIMUM_PATH_LENGTH 1024
#define TOML_CHARACTER_LENGTH 6
#define MP_FOLDER ".multiplayer"
#define MP_EXTENSION ".toml"

/* ---------- structures */

static const char *const option_keys[] = { "profile", "rotation", "replace", "loadout", "primary", "secondary" };
#define OPTION_COUNT (sizeof(option_keys) / sizeof(option_keys[0]))

struct manual_entry
{
	char key[MAXIMUM_KEY_LENGTH];
	char path[MAXIMUM_VALUE_LENGTH];
};

struct mp_profile
{
	/* its file's name, without .toml */
	char file[MAXIMUM_FILE_LENGTH];
	char name[MAXIMUM_NAME_LENGTH];
	char options[OPTION_COUNT][MAXIMUM_VALUE_LENGTH];
	struct manual_entry *manual;
	long manual_count;
};

/* ---------- globals */

static struct
{
	struct mp_profile profiles[MAXIMUM_MP_PROFILES];
	long count;
	unsigned long scans;
} port_tag_mp_profiles;

/* ---------- private code */

static char lower(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static int names_compare(const char *first, const char *second)
{
	for (; *first && lower(*first) == lower(*second); first++, second++)
	{
	}
	return (unsigned char)lower(*first) - (unsigned char)lower(*second);
}

static void folder_path(char *path, size_t size)
{
	snprintf(path, size, "%s/tag_injector/%s", platform_data_root(), MP_FOLDER);
}

static void profile_path(const struct mp_profile *profile, char *path, size_t size)
{
	char folder[MAXIMUM_PATH_LENGTH];

	folder_path(folder, sizeof(folder));
	snprintf(path, size, "%s/%s%s", folder, profile->file, MP_EXTENSION);
}

static void text_copy(char *destination, size_t size, const char *source)
{
	snprintf(destination, size, "%s", source ? source : "");
}

/* a value as it is kept: without its control characters */
static void value_copy(char *destination, size_t size, const char *source)
{
	size_t length = 0;

	for (; source && *source && length + 1 < size; source++)
	{
		if ((unsigned char)*source >= 0x20 && *source != 0x7F)
			destination[length++] = *source;
	}
	destination[length] = 0;
}

/* a name made a file's: its letters, digits, spaces, - and _, without
spaces at either end; 0 if none is left */
static int file_name_clean(char *name)
{
	char *read = name;
	char *write = name;

	while (*read == ' ')
		read++;
	for (; *read; read++)
	{
		char character = *read;

		if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
			(character >= '0' && character <= '9') || character == ' ' || character == '_' || character == '-')
			*write++ = character;
	}
	while (write > name && write[-1] == ' ')
		write--;
	*write = 0;
	return write != name;
}

static void toml_string_append(char *text, size_t size, const char *value)
{
	size_t length = strlen(text);

	for (; *value && length + TOML_CHARACTER_LENGTH < size; value++)
	{
		unsigned char character = (unsigned char)*value;

		if (character < 0x20 || character == 0x7F)
			continue;
		if (character == '"' || character == '\\')
			text[length++] = '\\';
		text[length++] = (char)character;
	}
	text[length] = 0;
}

static int option_index(const char *key)
{
	size_t index;

	for (index = 0; key && index < OPTION_COUNT; index++)
	{
		if (!strcmp(option_keys[index], key))
			return (int)index;
	}
	return -1;
}

/* whether a manual key is one: lowercase letters, digits and _ */
static int key_valid(const char *key)
{
	size_t length = key ? strlen(key) : 0;
	size_t index;

	if (!length || length >= MAXIMUM_KEY_LENGTH)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!((key[index] >= 'a' && key[index] <= 'z') || (key[index] >= '0' && key[index] <= '9') ||
			key[index] == '_'))
			return 0;
	}
	return 1;
}

static void profile_dispose(struct mp_profile *profile)
{
	free(profile->manual);
	memset(profile, 0, sizeof(*profile));
}

static int manual_add(struct mp_profile *profile, const char *key, const char *path)
{
	struct manual_entry *grown;

	if (profile->manual_count >= MAXIMUM_MANUAL_ENTRIES || !key_valid(key) || !path || !path[0] ||
		strlen(path) >= MAXIMUM_VALUE_LENGTH)
		return 0;
	grown = realloc(profile->manual, (size_t)(profile->manual_count + 1) * sizeof(*grown));
	if (!grown)
		return 0;
	profile->manual = grown;
	text_copy(grown[profile->manual_count].key, sizeof(grown[0].key), key);
	value_copy(grown[profile->manual_count].path, sizeof(grown[0].path), path);
	profile->manual_count++;
	return 1;
}

/* reads a profile's file (its name without .toml); 0 when it is not one */
static int profile_read(struct mp_profile *profile, const char *folder, const char *file)
{
	char path[MAXIMUM_PATH_LENGTH];
	size_t size = 0;
	char *text;
	toml_result_t result;
	toml_datum_t value;
	toml_datum_t manual;
	size_t option;

	memset(profile, 0, sizeof(*profile));
	text_copy(profile->file, sizeof(profile->file), file);
	text_copy(profile->name, sizeof(profile->name), file);
	snprintf(path, sizeof(path), "%s/%s%s", folder, file, MP_EXTENSION);
	text = config_file_read(path, &size);
	if (!text)
		return 0;
	result = toml_parse(text, (int)size);
	free(text);
	if (!result.ok)
	{
		platform_log("tag injector: %s: %s; it is not a multiplayer profile", path, result.errmsg);
		toml_free(result);
		return 0;
	}
	value = toml_get(result.toptab, "name");
	if (value.type == TOML_STRING && value.u.s[0])
		value_copy(profile->name, sizeof(profile->name), value.u.s);
	for (option = 0; option < OPTION_COUNT; option++)
	{
		value = toml_get(result.toptab, option_keys[option]);
		if (value.type == TOML_STRING)
			value_copy(profile->options[option], sizeof(profile->options[option]), value.u.s);
	}
	manual = toml_get(result.toptab, "manual");
	if (manual.type == TOML_TABLE)
	{
		int key;

		for (key = 0; key < manual.u.tab.size; key++)
		{
			toml_datum_t entries = manual.u.tab.value[key];
			int entry;

			if (entries.type != TOML_ARRAY)
				continue;
			for (entry = 0; entry < entries.u.arr.size; entry++)
			{
				if (entries.u.arr.elem[entry].type == TOML_STRING &&
					!manual_add(profile, manual.u.tab.key[key], entries.u.arr.elem[entry].u.s))
					platform_log("tag injector: %s: a weapon of [manual] is left out", path);
			}
		}
	}
	toml_free(result);
	return 1;
}

static int profile_write(const struct mp_profile *profile)
{
	size_t capacity = 1024 + (OPTION_COUNT + (size_t)profile->manual_count) *
		(MAXIMUM_KEY_LENGTH + MAXIMUM_VALUE_LENGTH * TOML_CHARACTER_LENGTH + 16);
	char folder[MAXIMUM_PATH_LENGTH];
	char path[MAXIMUM_PATH_LENGTH];
	char *text = malloc(capacity);
	size_t option;
	long index;
	int written;
	FILE *stream;

	if (!text)
		return 0;
	snprintf(text, capacity, "# a Tag Injector multiplayer profile (Settings > Gameplay > Tag Injector)\nname = \"");
	toml_string_append(text, capacity, profile->name);
	snprintf(text + strlen(text), capacity - strlen(text), "\"\n");
	for (option = 0; option < OPTION_COUNT; option++)
	{
		snprintf(text + strlen(text), capacity - strlen(text), "%s = \"", option_keys[option]);
		toml_string_append(text, capacity, profile->options[option]);
		snprintf(text + strlen(text), capacity - strlen(text), "\"\n");
	}
	if (profile->manual_count)
		snprintf(text + strlen(text), capacity - strlen(text), "\n[manual]\n");
	/* (each key once, its weapons in the order they were added) */
	for (index = 0; index < profile->manual_count; index++)
	{
		long other;

		for (other = 0; other < index && strcmp(profile->manual[other].key, profile->manual[index].key); other++)
		{
		}
		if (other < index)
			continue;
		snprintf(text + strlen(text), capacity - strlen(text), "%s = [", profile->manual[index].key);
		for (other = index; other < profile->manual_count; other++)
		{
			if (strcmp(profile->manual[other].key, profile->manual[index].key))
				continue;
			snprintf(text + strlen(text), capacity - strlen(text), "%s\"", other > index ? ", " : "");
			toml_string_append(text, capacity, profile->manual[other].path);
			snprintf(text + strlen(text), capacity - strlen(text), "\"");
		}
		snprintf(text + strlen(text), capacity - strlen(text), "]\n");
	}
	/* (the tag_injector folder first, which a profile may not have made yet) */
	snprintf(folder, sizeof(folder), "%s/tag_injector", platform_data_root());
	posix_make_directory(folder);
	folder_path(folder, sizeof(folder));
	posix_make_directory(folder);
	profile_path(profile, path, sizeof(path));
	{
		char temporary[MAXIMUM_PATH_LENGTH + 8];

		snprintf(temporary, sizeof(temporary), "%s.new", path);
		stream = fopen(temporary, "wb");
		written = stream && fwrite(text, 1, strlen(text), stream) == strlen(text);
		if (stream)
			written = fclose(stream) == 0 && written;
		written = written && rename(temporary, path) == 0;
		if (!written)
			remove(temporary);
	}
	if (!written)
		platform_log("tag injector: %s cannot be written", path);
	free(text);
	return written;
}

static struct mp_profile *profile_get(long profile)
{
	return profile >= 0 && profile < port_tag_mp_profiles.count ? &port_tag_mp_profiles.profiles[profile] : NULL;
}

/* ---------- public code */

long port_tag_mp_profiles_scan(void)
{
	char folder[MAXIMUM_PATH_LENGTH];
	char name[256];
	void *directory;
	long index;

	for (index = 0; index < port_tag_mp_profiles.count; index++)
		profile_dispose(&port_tag_mp_profiles.profiles[index]);
	port_tag_mp_profiles.count = 0;
	port_tag_mp_profiles.scans++;
	folder_path(folder, sizeof(folder));
	directory = posix_directory_open(folder);
	if (!directory)
		return 0;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		size_t length = strlen(name);
		size_t extension = strlen(MP_EXTENSION);
		struct mp_profile profile;

		if (name[0] == '.' || length <= extension || strcmp(name + length - extension, MP_EXTENSION) ||
			length - extension >= MAXIMUM_FILE_LENGTH)
			continue;
		name[length - extension] = 0;
		if (port_tag_mp_profiles.count == MAXIMUM_MP_PROFILES)
		{
			platform_log("tag injector: more than %d multiplayer profiles in %s: %s and those after it are left out",
				MAXIMUM_MP_PROFILES, folder, name);
			break;
		}
		if (!profile_read(&profile, folder, name))
			continue;
		for (index = port_tag_mp_profiles.count;
			index > 0 && names_compare(port_tag_mp_profiles.profiles[index - 1].name, profile.name) > 0; index--)
			port_tag_mp_profiles.profiles[index] = port_tag_mp_profiles.profiles[index - 1];
		port_tag_mp_profiles.profiles[index] = profile;
		port_tag_mp_profiles.count++;
	}
	posix_directory_close(directory);
	return port_tag_mp_profiles.count;
}

long port_tag_mp_profiles_count(void)
{
	return port_tag_mp_profiles.count;
}

unsigned long port_tag_mp_profiles_scans(void)
{
	return port_tag_mp_profiles.scans;
}

const char *port_tag_mp_profiles_name(long profile)
{
	return profile_get(profile) ? profile_get(profile)->name : "";
}

const char *port_tag_mp_profiles_file(long profile)
{
	return profile_get(profile) ? profile_get(profile)->file : "";
}

long port_tag_mp_profiles_find(const char *file)
{
	long index;

	for (index = 0; file && file[0] && index < port_tag_mp_profiles.count; index++)
	{
		if (!names_compare(port_tag_mp_profiles.profiles[index].file, file))
			return index;
	}
	return -1;
}

long port_tag_mp_profiles_create(const char *name, const char *profile)
{
	char folder[MAXIMUM_PATH_LENGTH];
	char base[MAXIMUM_FILE_LENGTH - 8];
	struct mp_profile made;
	int suffix;

	snprintf(base, sizeof(base), "%s", name ? name : "");
	if (!file_name_clean(base))
	{
		platform_log("tag injector: '%s' has no letter or digit to name a multiplayer profile by", name ? name : "");
		return -1;
	}
	memset(&made, 0, sizeof(made));
	value_copy(made.name, sizeof(made.name), name);
	value_copy(made.options[option_index("profile")], MAXIMUM_VALUE_LENGTH, profile);
	text_copy(made.options[option_index("rotation")], MAXIMUM_VALUE_LENGTH, "add");
	text_copy(made.options[option_index("replace")], MAXIMUM_VALUE_LENGTH, "random");
	text_copy(made.options[option_index("loadout")], MAXIMUM_VALUE_LENGTH, "map");
	folder_path(folder, sizeof(folder));
	for (suffix = 1; suffix < 1000; suffix++)
	{
		char path[MAXIMUM_PATH_LENGTH];
		struct posix_file_information information;

		if (suffix == 1)
			snprintf(made.file, sizeof(made.file), "%s", base);
		else
			snprintf(made.file, sizeof(made.file), "%s %d", base, suffix);
		profile_path(&made, path, sizeof(path));
		if (posix_stat(path, &information) != 0)
			break;
	}
	if (suffix == 1000 || !profile_write(&made))
		return -1;
	port_tag_mp_profiles_scan();
	platform_log("tag injector: made the multiplayer profile %s (%s), of %s", made.name, made.file,
		profile ? profile : "");
	return port_tag_mp_profiles_find(made.file);
}

int port_tag_mp_profiles_delete(long profile)
{
	struct mp_profile *found = profile_get(profile);
	char path[MAXIMUM_PATH_LENGTH];

	if (!found)
		return 0;
	profile_path(found, path, sizeof(path));
	if (remove(path) != 0)
	{
		platform_log("tag injector: %s cannot be deleted", path);
		return 0;
	}
	platform_log("tag injector: deleted the multiplayer profile %s", found->name);
	port_tag_mp_profiles_scan();
	return 1;
}

int port_tag_mp_profiles_rename(long profile, const char *name)
{
	struct mp_profile *found = profile_get(profile);
	char clean[MAXIMUM_NAME_LENGTH];

	if (!found || !name)
		return 0;
	text_copy(clean, sizeof(clean), name);
	if (!file_name_clean(clean))
		return 0;
	value_copy(found->name, sizeof(found->name), name);
	port_tag_mp_profiles.scans++;
	return profile_write(found);
}

const char *port_tag_mp_profiles_option(long profile, const char *key)
{
	int option = option_index(key);

	return profile_get(profile) && option >= 0 ? profile_get(profile)->options[option] : "";
}

int port_tag_mp_profiles_set_option(long profile, const char *key, const char *value)
{
	struct mp_profile *found = profile_get(profile);
	int option = option_index(key);
	char kept[MAXIMUM_VALUE_LENGTH];

	if (!found || option < 0 || (value && strlen(value) >= MAXIMUM_VALUE_LENGTH))
		return 0;
	value_copy(kept, sizeof(kept), value);
	if (!strcmp(found->options[option], kept))
		return 1;
	text_copy(found->options[option], sizeof(found->options[option]), kept);
	port_tag_mp_profiles.scans++;
	platform_log("tag injector: the multiplayer profile %s: %s = \"%s\"", found->name, key, kept);
	return profile_write(found);
}

long port_tag_mp_profiles_manual_count(long profile, const char *key)
{
	struct mp_profile *found = profile_get(profile);
	long count = 0;
	long index;

	for (index = 0; found && key && index < found->manual_count; index++)
		count += !strcmp(found->manual[index].key, key);
	return count;
}

const char *port_tag_mp_profiles_manual(long profile, const char *key, long entry)
{
	struct mp_profile *found = profile_get(profile);
	long index;

	for (index = 0; found && key && index < found->manual_count; index++)
	{
		if (strcmp(found->manual[index].key, key))
			continue;
		if (!entry--)
			return found->manual[index].path;
	}
	return "";
}

int port_tag_mp_profiles_manual_has(long profile, const char *key, const char *path)
{
	struct mp_profile *found = profile_get(profile);
	long index;

	for (index = 0; found && key && path && index < found->manual_count; index++)
	{
		if (!strcmp(found->manual[index].key, key) && !names_compare(found->manual[index].path, path))
			return 1;
	}
	return 0;
}

int port_tag_mp_profiles_manual_set(long profile, const char *key, const char *path, int on)
{
	struct mp_profile *found = profile_get(profile);
	long index;

	if (!found || !key_valid(key) || !path || !path[0])
		return 0;
	if ((port_tag_mp_profiles_manual_has(profile, key, path) != 0) == (on != 0))
		return 1;
	if (on)
	{
		if (!manual_add(found, key, path))
			return 0;
	}
	else
	{
		for (index = 0; index < found->manual_count; index++)
		{
			if (!strcmp(found->manual[index].key, key) && !names_compare(found->manual[index].path, path))
				break;
		}
		memmove(&found->manual[index], &found->manual[index + 1],
			(size_t)(found->manual_count - index - 1) * sizeof(found->manual[0]));
		found->manual_count--;
	}
	port_tag_mp_profiles.scans++;
	return profile_write(found);
}

void port_tag_mp_profiles_path(long profile, char *path, unsigned long size)
{
	if (profile_get(profile))
		profile_path(profile_get(profile), path, (size_t)size);
	else if (size)
		path[0] = 0;
}

long port_tag_mp_profiles_import(const char *path, const char *suffix, const char *profile)
{
	char folder[MAXIMUM_PATH_LENGTH];
	char file[MAXIMUM_FILE_LENGTH];
	char base[MAXIMUM_FILE_LENGTH - 8];
	const char *slash = path ? strrchr(path, '/') : NULL;
	size_t length;
	struct mp_profile read;
	char name[MAXIMUM_NAME_LENGTH];
	int suffix_number;

	/* (read as a profile of its folder is: its name without .toml) */
	if (!slash || (size_t)(slash - path) >= sizeof(folder))
		return -1;
	snprintf(folder, sizeof(folder), "%.*s", (int)(slash - path), path);
	length = strlen(slash + 1);
	if (length <= strlen(MP_EXTENSION) || length - strlen(MP_EXTENSION) >= sizeof(file) ||
		strcmp(slash + 1 + length - strlen(MP_EXTENSION), MP_EXTENSION))
		return -1;
	snprintf(file, sizeof(file), "%.*s", (int)(length - strlen(MP_EXTENSION)), slash + 1);
	if (!profile_read(&read, folder, file))
		return -1;
	/* (its name the host's and the suffix, its file a new one, playing the
	profile given) */
	snprintf(name, sizeof(name), "%.*s%s", (int)(sizeof(name) - strlen(suffix ? suffix : "") - 1), read.name,
		suffix ? suffix : "");
	value_copy(read.name, sizeof(read.name), name);
	value_copy(read.options[option_index("profile")], MAXIMUM_VALUE_LENGTH, profile);
	snprintf(base, sizeof(base), "%s", name);
	if (!file_name_clean(base))
		snprintf(base, sizeof(base), "online");
	folder_path(folder, sizeof(folder));
	for (suffix_number = 1; suffix_number < 1000; suffix_number++)
	{
		char made[MAXIMUM_PATH_LENGTH];
		struct posix_file_information information;

		if (suffix_number == 1)
			snprintf(read.file, sizeof(read.file), "%s", base);
		else
			snprintf(read.file, sizeof(read.file), "%s %d", base, suffix_number);
		profile_path(&read, made, sizeof(made));
		if (posix_stat(made, &information) != 0)
			break;
	}
	if (suffix_number == 1000 || !profile_write(&read))
	{
		profile_dispose(&read);
		return -1;
	}
	snprintf(file, sizeof(file), "%s", read.file);
	profile_dispose(&read);
	port_tag_mp_profiles_scan();
	platform_log("tag injector: copied the multiplayer profile %s, playing %s", name, profile);
	return port_tag_mp_profiles_find(file);
}
