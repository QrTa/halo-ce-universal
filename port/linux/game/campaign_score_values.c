/*
CAMPAIGN_SCORE_VALUES.C

Written by tools/campaign_score_values.py from campaign_score_values.json:
edit that, not this (configure.py, and so ninja, writes this again).
*/

#include <stddef.h>

#include "campaign_score_values.h"

static struct campaign_score_time_bracket const campaign_score_time_brackets[] =
{
	{ 0.3f, 3.0f },
	{ 0.7f, 2.0f },
	{ 1.0f, 1.0f },
	{ 1.5f, 0.5f },
	{ -1.0f, 0.0f },
};
static struct campaign_score_par_time const campaign_score_par_times[] =
{
	{ "a10", 900 },
	{ "a30", 1200 },
	{ "a50", 1800 },
	{ "b30", 900 },
	{ "b40", 1500 },
	{ "c10", 720 },
	{ "c20", 1800 },
	{ "c40", 1500 },
	{ "d20", 900 },
	{ "d40", 1500 },
};
static char const *const campaign_score_words_1[] = { "specops", NULL };
static char const *const campaign_score_words_2[] = { "major", NULL };
static char const *const campaign_score_words_3[] = { "minor", NULL };
static struct campaign_score_match_value const campaign_score_matches_4[] =
{
	{ campaign_score_words_1, 10 },
	{ campaign_score_words_2, 10 },
	{ campaign_score_words_3, 10 },
};
static char const *const campaign_score_words_5[] = { "major", NULL };
static char const *const campaign_score_words_6[] = { "minor", NULL };
static struct campaign_score_match_value const campaign_score_matches_7[] =
{
	{ campaign_score_words_5, 20 },
	{ campaign_score_words_6, 15 },
};
static char const *const campaign_score_words_8[] = { "commander", NULL };
static char const *const campaign_score_words_9[] = { "stealth", "specops", NULL };
static char const *const campaign_score_words_10[] = { "major", NULL };
static char const *const campaign_score_words_11[] = { "minor", NULL };
static struct campaign_score_match_value const campaign_score_matches_12[] =
{
	{ campaign_score_words_8, 75 },
	{ campaign_score_words_9, 50 },
	{ campaign_score_words_10, 40 },
	{ campaign_score_words_11, 30 },
};
static char const *const campaign_score_words_13[] = { "major", NULL };
static struct campaign_score_match_value const campaign_score_matches_14[] =
{
	{ campaign_score_words_13, 175 },
};
static char const *const campaign_score_words_15[] = { "floodcombat elite", NULL };
static struct campaign_score_match_value const campaign_score_matches_16[] =
{
	{ campaign_score_words_15, 15 },
};
static char const *const campaign_score_words_17[] = { "shielded major", NULL };
static char const *const campaign_score_words_18[] = { "shielded", "major", NULL };
static struct campaign_score_match_value const campaign_score_matches_19[] =
{
	{ campaign_score_words_17, 15 },
	{ campaign_score_words_18, 10 },
};
static struct campaign_score_actor_value const campaign_score_actors_20[] =
{
	{ 2, 10, 3, campaign_score_matches_4 }, /* grunt */
	{ 1, 15, 2, campaign_score_matches_7 }, /* jackal */
	{ 0, 30, 4, campaign_score_matches_12 }, /* elite */
	{ 3, 150, 1, campaign_score_matches_14 }, /* hunter */
	{ 4, 10, 0, NULL }, /* engineer */
	{ 5, 30, 0, NULL }, /* assassin */
	{ 9, 10, 1, campaign_score_matches_16 }, /* combat_form */
	{ 11, 10, 0, NULL }, /* carrier_form */
	{ 10, 1, 0, NULL }, /* infection_form */
	{ 13, 5, 2, campaign_score_matches_19 }, /* sentinel */
};
static char const *const campaign_score_words_21[] = { "wraith", NULL };
static char const *const campaign_score_words_22[] = { "banshee", NULL };
static char const *const campaign_score_words_23[] = { "ghost", NULL };
static char const *const campaign_score_words_24[] = { "c gun turret", NULL };
static struct campaign_score_match_value const campaign_score_matches_25[] =
{
	{ campaign_score_words_21, 200 },
	{ campaign_score_words_22, 100 },
	{ campaign_score_words_23, 100 },
	{ campaign_score_words_24, 50 },
};
static char const *const campaign_score_words_26[] = { "captain", NULL };
static char const *const campaign_score_words_27[] = { "armored", NULL };
static char const *const campaign_score_words_28[] = { "major", NULL };
static struct campaign_score_match_value const campaign_score_matches_29[] =
{
	{ campaign_score_words_26, -100 },
	{ campaign_score_words_27, -100 },
	{ campaign_score_words_28, -100 },
};
static struct campaign_score_actor_value const campaign_score_actors_30[] =
{
	{ 7, -50, 3, campaign_score_matches_29 }, /* marine */
	{ 8, -50, 0, NULL }, /* crew */
};
static struct campaign_score_skull_value const campaign_score_skull_values[] =
{
	{ "anger", 1.1f },
	{ "armistice", 1.1f },
	{ "assassins", 1.2f },
	{ "bandanna", 0.0f },
	{ "black_eye", 1.1f },
	{ "blind", 1.1f },
	{ "boom", 1.0f },
	{ "catch", 1.1f },
	{ "confetti_party", 1.0f },
	{ "efficient", 0.9f },
	{ "envy", 0.0f },
	{ "eye_patch", 1.1f },
	{ "famine", 1.2f },
	{ "floor_is_lava", 1.3f },
	{ "fog", 1.1f },
	{ "foreign", 1.1f },
	{ "ghost", 1.0f },
	{ "give_and_take", 1.2f },
	{ "grunt_birthday_party", 1.0f },
	{ "grunt_funeral", 1.0f },
	{ "iron", 1.3f },
	{ "johnny_ammo", 1.1f },
	{ "leadhead", 1.2f },
	{ "m_rating", 1.0f },
	{ "malfunction", 1.0f },
	{ "mythic", 1.2f },
	{ "need_glasses", 1.2f },
	{ "outbreak", 1.1f },
	{ "pinata", 1.0f },
	{ "protected", 1.2f },
	{ "recession", 1.2f },
	{ "rip_and_tear", 1.1f },
	{ "scarab", 0.0f },
	{ "sputnik", 1.0f },
	{ "streaking", 1.2f },
	{ "swarm", 1.0f },
	{ "thats_just_wrong", 1.1f },
	{ "thunderstorm", 1.2f },
	{ "tough_luck", 1.1f },
	{ "woe_is_me", 1.2f },
};

struct campaign_score_values const campaign_score_values =
{
	{ 0.5f, 1.0f, 2.0f, 4.0f },
	5, campaign_score_time_brackets,
	10, campaign_score_par_times,
	0,
	1.0f,
	1.25f,
	1.25f,
	1.25f,
	1.5f,
	10, campaign_score_actors_20,
	4, campaign_score_matches_25,
	-25,
	-50,
	-25,
	2, campaign_score_actors_30,
	0, NULL,
	0,
	1.0f,
	40, campaign_score_skull_values,
};
