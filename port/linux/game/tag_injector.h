/*
TAG_INJECTOR.H

The tag injector (tag_injector.c): tags taken out of Custom Edition maps,
as tag bundles, into profiles (port/linux/src/port_tag_profiles.c), from
Settings > Gameplay > Tag Injector, and the tags of a profile added to a
campaign level the randomizer plays, the profile chosen before its screen.
*/

#ifndef __TAG_INJECTOR_H
#define __TAG_INJECTOR_H
#pragma once

/* ---------- constants */

/* the kinds a map's tags are listed by */
enum tag_injector_category
{
	_tag_injector_weapons,
	_tag_injector_ai,
	_tag_injector_equipment,
	_tag_injector_vehicles,
	_tag_injector_bipeds,
	_tag_injector_scenery,

	NUMBER_OF_TAG_INJECTOR_CATEGORIES
};

/* the longest custom type's name a tag is chosen by (its terminator too) */
#define TAG_INJECTOR_CUSTOM_TYPE_LENGTH 32

/* ---------- prototypes/TAG_INJECTOR.C */

/* ---------- the menu's (menu_functions.c) */

/* Whether tags can be taken out of maps: Custom Edition maps can run
(game.custom_edition) and none is being played. When not, `reason` says
why, for the menu. */
boolean tag_injector_maps_available(
	char *reason,
	long reason_size);

/* The Custom Edition maps, as the map lists have them (the campaign maps,
then the multiplayer maps), found again: how many there are. */
short tag_injector_maps_find(
	void);
short tag_injector_map_count(
	void);
/* a map's name in the menus, and its subfolder of custom_maps ("" for
none) */
wchar_t const *tag_injector_map_name(
	short map);
char const *tag_injector_map_folder(
	short map);
/* the map of the list that is Custom Edition's campaign map (campaign TRUE)
or multiplayer map at `index` of custom_edition_maps_count's, or NONE */
short tag_injector_map_of(
	boolean campaign,
	short index);
/* whether a map is a campaign map */
boolean tag_injector_map_campaign(
	short map);

/* Loads a map's tags, as the game would play them, to list them and take
them out; FALSE after logging why. Opening another, or
tag_injector_map_close, lets it go. The map open, or NONE. */
boolean tag_injector_map_open(
	short map);
void tag_injector_map_close(
	void);
short tag_injector_map_opened(
	void);

/* the name of a category ("WEAPONS"), and whether its tags can be added to
a profile (those the randomizer plays: weapons and AI) */
char const *tag_injector_category_name(
	short category);
boolean tag_injector_category_addable(
	short category);
/* the group of a category's tags ('weap'), or 0 */
unsigned long tag_injector_category_group(
	short category);
/* the open map's tags of a category: how many, and the path of each (by
path, alphabetically) */
long tag_injector_category_count(
	short category);
char const *tag_injector_category_tag(
	short category,
	long index);
/* whether a tag's path is one made for it, its map having none for it (a
protected map's: tag_names.c) */
boolean tag_injector_tag_name_made(
	short category,
	long index);
/* How many tags the bundle of a tag would hold (itself and every tag it
names), and the bytes of its tags; NONE when it cannot be taken out,
`reason` saying why. */
long tag_injector_tag_size(
	short category,
	long index,
	char *reason,
	long reason_size);
/* whether a profile's weapon, actor variant or equipment is of a path a
campaign map has (REPLACE ORIGINAL can replace that one). The campaign maps
are read the first time it is asked */
boolean tag_injector_tag_stock(
	long profile,
	long tag);
/* the tag of a profile that is this one of the open map (by its group and
path), or NONE */
long tag_injector_tag_in_profile(
	long profile,
	short category,
	long index);
/* Takes the open map's tag out, with every tag it names, into the profile;
FALSE after logging why, and `reason` saying it for the menu. An actor
variant's weapons and equipment are added too, each a tag of its own (unless
the profile has one of its path): how many, in `carried`. */
boolean tag_injector_tag_add(
	long profile,
	short category,
	long index,
	long *carried,
	char *reason,
	long reason_size);

/* How many of the profiles' tags are bundles as they were taken out (an
earlier build kept them so), which the menu stores as port_tag_store.c keeps
tags (a tag file each, and what they share once in the profile's pack); and
stores the next of them: FALSE when none is left, else TRUE, with the
bytes of its profile before and after (both 0 when it could not be stored:
it is kept as it was, and not tried again until the game starts again). */
long tag_injector_profiles_unstored(
	void);
boolean tag_injector_profile_store_next(
	unsigned long *before,
	unsigned long *after);

/* ---------- the randomizer's (randomizer.c) */

/* The profile the randomizer plays: game.tag_injection's on, and the one
profile there is, or of more the one game.randomizer_tag_profile names (the
randomizer's profile screen chooses it), has a tag on. Its index, else
NONE. */
long tag_injector_profile_playable(
	void);
/* how many tags of a profile are on: of a group, or of all (0) */
long tag_injector_profile_enabled_count(
	long profile,
	unsigned long group_tag);
/* a tag's setting the randomizer reads ("faction", "type", "class", "hud",
"replace", "name": port_tag_profiles.h's), "" when it is not set */
char const *tag_injector_tag_setting(
	long profile,
	long tag,
	char const *key);
/* the name a profile's tag is shown by: the one it was given (its "name"
setting), else the last part of its path */
void tag_injector_tag_display_name(
	long profile,
	long tag,
	char *name,
	long name_size);
/* the name of a tag's own type, which the randomizer's one type chooses it
by: its custom_type, else the name it is shown by; in capitals */
void tag_injector_tag_custom_type(
	long profile,
	long tag,
	char *name,
	long name_size);
/* the custom types of a profile's tags on of a group, each once, by name:
how many (`capacity` at most) */
long tag_injector_custom_types(
	long profile,
	unsigned long group_tag,
	char (*names)[TAG_INJECTOR_CUSTOM_TYPE_LENGTH],
	long capacity);

/* The campaign's weapon HUDs a weapon's "hud" setting can draw it with: how many, and of each, its setting's value ("sniper_rifle"), its
weapon HUD interface's path, and the campaign map that has it (the first,
of the levels' order), which the randomizer copies it from into a level
without it. */
short tag_injector_stock_hud_count(
	void);
char const *tag_injector_stock_hud_value(
	short hud);
char const *tag_injector_stock_hud_tag(
	short hud);
char const *tag_injector_stock_hud_map(
	short hud);

/* Reads the bundles of a profile's tags on for the level loading, before
the randomizer copies the tags it wants (tag_injector_tags_load adds the
bundles' after those, without reading them again): the stock HUDs their
weapons are drawn with that the level has not got go into `huds`
(tag_injector_stock_hud_* indices, `capacity` at most), each once: how
many. */
long tag_injector_tags_read(
	long profile,
	short *huds,
	long capacity);
/* Adds the tags on of a profile to the level loaded, after its own and
those the randomizer copied: each tag a bundle holds that the level has not
got (a tag of its group and path), the bundle's models made this build's and
its bitmaps' channels found, and the weapons a stock HUD is chosen for
given it, when the level has it (else they keep their own). A bundle that
cannot be played is left out. The
weapons and actor variants of the bundles (the tags they were taken out
for), the level's own when it has them, go into `roots`, and the profile's
tag each is into `root_tags` (`root_capacity` at most): how many. */
long tag_injector_tags_load(
	long profile,
	long *roots,
	long *root_tags,
	long root_capacity);
/* the tags added let go, and the level's table as it was */
void tag_injector_tags_unloaded(
	void);
/* whether a tag is one a profile added, and whether the level has any:
Halo PC's tags, which the engine takes as a Custom Edition map's
(tag_groups.c, hud_draw.c and the texture cache) */
boolean tag_injector_tag_injected(
	long tag_index);
boolean tag_injector_tags_loaded(
	void);

/* A cache file read of a tag a profile added: its bitmap pixels or sound
samples, read from its bundle (a bitmap's laid out as the texture cache
wants them). 0 for any other tag, which is read as it would be. */
int tag_injector_read(
	long tag_index,
	long offset,
	long size,
	void *buffer);

#endif
