/*
PORT_TAG_PROFILES.H

The tag injector's profiles in the data root's tag_injector folder
(port_tag_profiles.c): each a name and the tags taken out of Custom Edition
maps into it, as tag bundles, which the randomizer's Go Crazy adds to a
level (port/linux/game/tag_injector.c, which declares these for itself:
keep the two the same).
*/

#ifndef PORT_TAG_PROFILES_H
#define PORT_TAG_PROFILES_H

/* finds the profiles again: how many there are. Every profile and tag index
from before is gone */
long port_tag_profiles_scan(void);
/* how many profiles the last scan found, and how many times they have been
found (a profile made, deleted or changed is a scan) */
long port_tag_profiles_count(void);
unsigned long port_tag_profiles_scans(void);
/* a profile's name, as it was typed, and its folder's (the setting
game.tag_injection_profile names it by); "" past them */
const char *port_tag_profiles_name(long profile);
const char *port_tag_profiles_folder(long profile);
/* the profile of the folder `folder` (any case), or -1 */
long port_tag_profiles_find(const char *folder);
/* makes a profile named `name` (its folder's name made of its letters,
digits, spaces, - and _): its index, or -1 */
long port_tag_profiles_create(const char *name);
/* deletes a profile, its folder and its bundles; 0 if it cannot */
int port_tag_profiles_delete(long profile);

/* a profile's tags, each the map it was taken out of (its file's name), its
group and its path */
long port_tag_profiles_tag_count(long profile);
const char *port_tag_profiles_tag_map(long profile, long tag);
unsigned long port_tag_profiles_tag_group(long profile, long tag);
const char *port_tag_profiles_tag_name(long profile, long tag);
/* a tag's bundle's bytes, as the last scan found it (0: missing), and a
profile's, all of its tags' */
unsigned long port_tag_profiles_tag_size(long profile, long tag);
unsigned long port_tag_profiles_size(long profile);
/* whether a tag is played (on), or kept in the profile and left out (off);
set_ writes the profile: 0 if it cannot */
int port_tag_profiles_tag_enabled(long profile, long tag);
int port_tag_profiles_tag_set_enabled(long profile, long tag, int enabled);
/* a tag's setting, by its key: "faction", "type" (an actor variant's) and
"class" (a weapon's), the randomizer's names for them, and "custom_type", the
name of its own type it is chosen by; "" when it is not set (the randomizer
finds it by the tag's name, its custom type its name's last part). set_
keeps the value's letters, digits, spaces, - and _ (lower case but for
custom_type's), and writes the profile: 0 if it cannot */
const char *port_tag_profiles_tag_option(long profile, long tag, const char *key);
int port_tag_profiles_tag_set_option(long profile, long tag, const char *key, const char *value);
/* the tag of a group and path (any case) of a profile, or -1 */
long port_tag_profiles_tag_find(long profile, unsigned long group, const char *name);
/* adds a tag to a profile, with its bundle (`size` bytes), or replaces the
one of its group and path; 0 if it cannot be written */
int port_tag_profiles_tag_add(long profile, const char *map, unsigned long group, const char *name,
	const void *bundle, unsigned long size);
/* takes a tag out of a profile, and deletes its bundle; 0 if it cannot */
int port_tag_profiles_tag_remove(long profile, long tag);
/* opens a tag's bundle for a level to read from, whatever profiles are
found meanwhile: a handle, and its size; -1 if it cannot be */
long port_tag_profiles_bundle_open(long profile, long tag, unsigned long *size);
/* reads `size` bytes at `offset` of an open bundle into buffer (the Xbox's
memory too); 0 unless all of them were read */
int port_tag_profiles_bundle_read_at(long handle, unsigned long offset, void *buffer, unsigned long size);
void port_tag_profiles_bundle_close(long handle);

#endif
