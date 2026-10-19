/*
HALO_SKULLS.H

The campaign's skulls, as the Master Chief Collection's Halo: Combat Evolved
has them (port/linux/game/skulls.c). The game's sources ask skull_active;
the menus write game.skulls_active, and a co-op client takes the host's.
*/

#ifndef __HALO_SKULLS_H
#define __HALO_SKULLS_H

/* in the order of the skulls screen (tools/port_settings.py's SKULLS) */
enum skull
{
	_skull_anger = 0,
	_skull_assassins,
	_skull_bandanna,
	_skull_black_eye,
	_skull_blind,
	_skull_boom,
	_skull_catch,
	_skull_confetti_party,
	_skull_envy,
	_skull_eye_patch,
	_skull_famine,
	_skull_fog,
	_skull_foreign,
	_skull_ghost,
	_skull_grunt_birthday_party,
	_skull_grunt_funeral,
	_skull_iron,
	_skull_malfunction,
	_skull_mythic,
	_skull_pinata,
	_skull_protected,
	_skull_recession,
	_skull_sputnik,
	_skull_streaking,
	_skull_swarm,
	_skull_thats_just_wrong,
	_skull_thunderstorm,
	_skull_tough_luck,
	_skull_vampirism,
	NUMBER_OF_SKULLS
};

struct skull_description
{
	char const *key;
	char const *label;
	char const *description;
};

extern struct skull_description const skull_descriptions[NUMBER_OF_SKULLS];

/* whether the skull is on in the campaign game being played */
int skull_active(int skull);

/* how the skulls are had (game.skulls_unlock_mode): every one, or those
found playing, or not asked yet; and the mask of those had */
enum
{
	_skulls_unlock_unset = 0,
	_skulls_unlock_all,
	_skulls_unlock_play
};
int skulls_unlock_mode(void);
unsigned long skulls_unlocked_mask(void);
/* whether the locked skulls are carried by enemies to be found: unlocking
by playing, without Settings > Gameplay's ALL SKULLS */
int skulls_drop(void);

/* game.skulls_active's words, to a mask of the skulls and back */
unsigned long skulls_mask_from_text(char const *text);
void skulls_text_from_mask(unsigned long mask, char *text, unsigned int size);

/* the skulls' part of the game state (the skulls dropped and found in the
level, which a checkpoint keeps), made with the game's; and made new with
each map, and each start again of the level (game.c) */
void skulls_initialize(void);
void skulls_initialize_for_new_map(void);
/* the game state gone back to a checkpoint (or to save and quit): the
skulls found and not kept are lost (game_state.c) */
void skulls_game_state_reverted(void);
/* a saved game loaded (Continue): its lost skulls the level's (game_state.c) */
void skulls_game_state_loaded(void);
/* the skulls on in the level, their icons at the bottom right of the local
player's screen (hud.c) */
void skulls_hud_draw(short local_player_index);

/* a level loaded: the skulls it is played with, taken now (the host's for
a client), and Grunt Birthday Party's sound added to its tags; the level
going (before the randomizer's tags go, which were added before it) */
void skulls_level_loaded(long scenario_tag_index);
void skulls_level_unloaded(void);

/* the sound cache's samples of the birthday party's sound, from memory
(xbox_sound_cache.c); FALSE for any other sound */
struct sound_permutation;
unsigned char skulls_sound_read(struct sound_permutation const *permutation, void *buffer);

/* the advertisement's reserved bytes: the skulls flag and mask, when the
host plays a campaign game with skulls; the client's copy of them */
void skulls_advertisement(unsigned char *reserved);
void skulls_set_remote(int on, unsigned char const *mask);

/* whether the unit (or an object's team) is the players' enemy */
int skulls_unit_is_enemy(long unit_index);
int skulls_team_is_enemy(short team_index);
/* whether the unit is a grunt, by its tag's name (randomizer.c's classes) */
int skulls_unit_is_grunt(long unit_index);
/* whether the weapon tag is a Covenant weapon (randomizer.c's) */
int skulls_weapon_is_covenant(long weapon_definition_index);

/* whether the object is a player's unit */
int skulls_unit_is_player(long object_index);

/* a melee hit on the victim, which the host (or a game of one machine)
deals: Pinata's grenades and Black Eye's shield */
void skulls_melee_damage(long victim_index, long attacker_object_index, long attacker_player_index);

/* Boom: what an explosion's radii are multiplied by */
float skulls_boom_scale(void);

/* a unit's killing blow, by the player (NONE: another's), and its death
(Grunt Funeral's); on every machine, the effects' damage the host's. The
blow: Grunt Birthday Party's celebration of a headshot, and a skull to be
found dropped by an enemy */
void skulls_unit_killed(long unit_index, int headshot, long killer_player_index);
/* a blow to a unit, before its aftermath (and its killing blow's
skulls_unit_killed): the kind it was, and whether it killed. A kill's
odds of dropping a skull: a headshot, a punch from behind, an instant kill
(none before it), a group's (one blow, several), a vehicle's. (The
direction is a real_vector3d: this header comes before real_math.h) */
void skulls_unit_blow(long unit_index, long definition_index, long owner_object_index,
	void const *direction, int melee, int vehicle, int lethal);
void skulls_unit_died(long unit_index);

/* the skull the enemies carry, to be found (unlocking by playing): a
multiplayer map's oddball, copied into the level as it loads
(port/linux/game/randomizer.c, from skulls.pack). Whether a level loading
wants it: the skulls on, unlocking by playing, a game of this machine's
alone (solo or split screen) */
#define SKULLS_CARRIED_TAG "weapons\\ball\\ball"
int skulls_carried_wanted(void);
/* each tick of the game: the skulls lying about, heard */
void skulls_tick(void);
/* a skull to be found lying about: its name ("IRON SKULL"), else NULL; and
it taken by a player's action button: on for the level, to be kept once
the level is finished without dying. FALSE for another item. (The name is
a wchar_t string, of the 16-bit wchar_t every build has: this header comes
before <stddef.h>) */
unsigned short const *skulls_item_name(long object_index);
int skulls_item_take(short local_player_index, long unit_index, long object_index);

/* Envy: a player's unit's flashlight button pressed: TRUE when it is the
skull's active camo instead (taken, or still recharging); Swarm: whether an
object's definition is a hunter's */
int skulls_envy(long unit_index);
int skulls_object_is_hunter(long definition_index);

/* Vampirism: a player's unit fired a shot (a miss, unless an enemy is hit
soon); damage of a player's dealt to an object (a hit, if an enemy) */
void skulls_shot_fired(long unit_index);
void skulls_damage_dealt(long object_index, long owner_player_index);
/* Protected: an enemy's body without a shield given one; a hit on such a
shield (broken by it, or not), shown as the model cannot show it */
void skulls_protect(long unit_index);
void skulls_given_shield_hit(long object_index, int broken);

/* Famine: the weapon an actor drops, with half its ammunition */
void skulls_famine_weapon(long weapon_index);

/* Malfunction: whether the local player's HUD element is broken. The
elements are rolled again each time the player has a new body */
enum
{
	_skull_malfunction_shield = 0,
	_skull_malfunction_health,
	_skull_malfunction_motion_sensor,
	_skull_malfunction_crosshair,
	_skull_malfunction_weapon,
	_skull_malfunction_grenades,
	NUMBER_OF_SKULL_MALFUNCTIONS
};
int skulls_hud_broken(short local_player_index, long unit_index, int element);

#endif /* __HALO_SKULLS_H */
