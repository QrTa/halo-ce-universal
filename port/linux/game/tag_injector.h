/*
TAG_INJECTOR.H

The tag injector (tag_injector.c): tags taken out of Custom Edition maps,
as tag bundles, into profiles (port/linux/src/port_tag_profiles.c), from
Settings > Gameplay > Tag Injector, and the tags of a profile added to a
campaign level the randomizer's Go Crazy plays.
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
/* the open map's tags of a category: how many, and the path of each (by
path, alphabetically) */
long tag_injector_category_count(
	short category);
char const *tag_injector_category_tag(
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
/* the tag of a profile that is this one of the open map (by its group and
path), or NONE */
long tag_injector_tag_in_profile(
	long profile,
	short category,
	long index);
/* Takes the open map's tag out, with every tag it names, into the profile;
FALSE after logging why, and `reason` saying it for the menu. */
boolean tag_injector_tag_add(
	long profile,
	short category,
	long index,
	char *reason,
	long reason_size);

/* ---------- the randomizer's (randomizer.c) */

/* The profile the randomizer's Go Crazy plays: game.tag_injection's on, and
game.tag_injection_profile names a profile of a tag or more. Its index, else
NONE. */
long tag_injector_profile_playable(
	void);

/* Adds the tags of a profile to the level loaded, after its own and those
the randomizer copied: each tag a bundle holds that the level has not got (a
tag of its group and path), the bundle's models made this build's and its
bitmaps' channels found. A bundle that cannot be played is left out. The
weapons and actor variants of the bundles (the tags they were taken out
for), the level's own when it has them, go into `roots` (`root_capacity` at
most): how many. */
long tag_injector_tags_load(
	long profile,
	long *roots,
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
