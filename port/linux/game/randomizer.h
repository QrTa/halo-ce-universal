/*
RANDOMIZER.H

The campaign randomizer (randomizer.c). A level swaps actors and weapons
in memory; tags it does not have are read from the other campaign maps.
*/

#ifndef __RANDOMIZER_H
#define __RANDOMIZER_H

void randomizer_tags_loaded(char const *scenario_name);
void randomizer_tags_unloaded(void);

/* a bitmap's or sound's bytes of an imported tag, from the map they were
copied from. 0: this tag is the open map's, and the caller reads that */
int randomizer_donor_read(long tag_index, long offset, long size, void *buffer);

/* the actor, weapon or carried weapon a placement uses. each returns the
index it was given when the randomizer is not swapping that */
long randomizer_actor_variant(long original, long encounter_index, long squad_index, void *starting_location);
long randomizer_weapon_definition(long definition_index, void *scenario_object);
long randomizer_enemy_weapon(long weapon_definition, long unit_index, long attempt);

/* the advertisement's reserved bytes: the randomizer flag and the setup,
when this game is a randomized campaign */
void randomizer_advertisement(unsigned char *reserved);

/* the setup of the game this machine is joining, or none */
void randomizer_set_remote(int on, unsigned char const *setup);
int randomizer_remote_on(void);

/* the lobby's map line, prefixed while the game is randomized */
void randomizer_note_map_widget(void *widget);
wchar_t const *randomizer_level_title_text(void *widget, wchar_t const *string);

/* whether the game this machine is in, or is hosting, is randomized */
int randomizer_show_prefix(void);

/* maps/randomizer.pack, written from the campaign maps (randomizer_pack.c),
behind the loading screen's bar. 1 when it was */
int randomizer_pack_build(void);

/* the pack is there and matches the maps */
int randomizer_pack_ready(void);

/* the pack is wanted (game.randomizer turned on): a notice, then, at the
main menu, the pack written. Nothing when it is ready */
void randomizer_pack_request(void);

/* a campaign map's header, as the pack records it. 0 when the map is not
there or is not an Xbox map */
int randomizer_pack_map_header(char const *leaf, long *file_length, long *tag_offset, long *tag_size,
	int *compressed);

#endif /* __RANDOMIZER_H */
