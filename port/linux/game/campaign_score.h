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

/* (damage.c's object_damage_aftermath) a unit's body was depleted: if it
was an enemy, `killing_player_index` (NONE for no player) is given the
points of its actor variant */
void campaign_score_record_kill(
	long unit_index,
	long killing_player_index);

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
void campaign_score_time_text(wchar_t *text, long count);

/* a player's name (by datum index): their own, else, for one of this
machine's campaign, their profile's (a single player's has none) */
void campaign_score_player_display_name(long player_index, wchar_t *name, long count);
/* the level's title (The Pillar of Autumn), else its map's name */
void campaign_score_level_title(wchar_t *text, long count);

/* (network_coop.c) network co-op: the host scores the kills and sends each
to the clients, and when the level is won */
void campaign_score_network_kill(short player_index, long variant_index, short points, boolean competitive);
void campaign_score_network_won(boolean competitive);

#endif
