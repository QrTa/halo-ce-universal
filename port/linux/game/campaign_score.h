/*
CAMPAIGN_SCORE.H

The campaign's score (game.campaign_score: campaign_score.c): each enemy a
player kills is worth points by its actor variant.
*/

#ifndef __CAMPAIGN_SCORE_H
#define __CAMPAIGN_SCORE_H

/* game.campaign_score, and a campaign level being played */
boolean campaign_score_enabled(void);

/* (interface.c's hud_initialize_for_new_map) a map loads: a new level's
score begins; a level just won has its summary shown over this one's start */
void campaign_score_new_map(void);

/* (damage.c's object_damage_aftermath) a unit was hurt (`lethal`: its body
was depleted) by a blow of `killing_player_index`'s (NONE for no player's),
to the body part (units.c's), by melee or not, going `direction` (a
real_vector3d): an enemy's kill scores, by its style; an ally's, a player's
death and another player's are penalties; the hurt is remembered for one
shot, one kill. `no_statistics`: a kill not counted */
void campaign_score_unit_damaged(
	long unit_index,
	long killing_player_index,
	short body_part,
	boolean melee,
	void const *direction,
	boolean lethal,
	boolean no_statistics);

/* (ui_widget_event_handler_functions.c's pause_game_restart_at_checkpoint)
Revert to Checkpoint was chosen: its penalty, for this machine's players */
void campaign_score_checkpoint_reverted(void);

/* (main.c's main_won_map) the level is won: its summary is written to the
log and kept for the next level's start */
void campaign_score_level_won(void);

/* (main.c's main_won_map_private) a level was won: TRUE while the level's
summary is shown over it, until A or B is pressed; the level is left only
when FALSE */
boolean campaign_score_postgame_wait(void);

/* (interface.c's interface_draw_fullscreen_overlays) the last level's summary */
void campaign_score_draw(void);

/* (hud.c's hud_draw_screen) the score and the level's time, a panel at the
bottom right of the view being drawn, before the skulls' icons */
void campaign_score_draw_view(long player_index);

/* (skulls.c's skulls_hud_draw) how far, in the HUD's 480 lines, the panel
the view drawn last took above the corner's margin: 0 for none */
short campaign_score_hud_height(void);

/* (game_engine.c's scoreboard) a player's points and kills (by datum
index), the team's points, whether co-op scores each player on their own,
and the level's time (in ticks, and H:MM:SS) */
long campaign_score_player_points(long player_index);
long campaign_score_player_kills(long player_index);
long campaign_score_team_points(void);
boolean campaign_score_is_competitive(void);
long campaign_score_level_ticks(void);
/* (game.c's game_tick) the level's time counted on, each tick: a checkpoint
reverted to takes nothing from it */
void campaign_score_tick(void);
/* the level's time's multiplier now (by its par time: x1 for a level with
none), and its time over its par time (less than 0: none) */
real campaign_score_time_multiplier(void);
real campaign_score_par_fraction(void);
/* the score's multipliers now: the difficulty's, the skulls' (those on),
and the difficulty's, the skulls' and the time's together; a multiplier as
the HUD has it (x3, x0.5, x1.32) */
real campaign_score_difficulty_multiplier(void);
real campaign_score_skulls_multiplier(void);
real campaign_score_total_multiplier(void);
void campaign_score_multiplier_text(real multiplier, wchar_t *text, long count);
void campaign_score_time_text(wchar_t *text, long count);

/* a player's name (by datum index): their own, else, for one of this
machine's campaign, their profile's (a single player's has none) */
void campaign_score_player_display_name(long player_index, wchar_t *name, long count);
/* the level's title (The Pillar of Autumn), else its map's name */
void campaign_score_level_title(wchar_t *text, long count);

/* (network_coop.c) network co-op: the host scores the kills and penalties,
and sends each to the clients (its kind and style too), and when the level
is won */
void campaign_score_network_kill(short player_index, long variant_index, long points, short kind, byte style,
	boolean competitive);
void campaign_score_network_won(boolean competitive);

#endif
