/*
TAG_PREVIEW.C

The Tag Injector's preview (tag_preview.h): the model of the tag chosen in
its menu (tag_injector.c's tag_injector_*_mesh: a weapon's, equipment's, a
vehicle's, a biped's or scenery's own, an actor variant's its unit's), in
its default pose, turning about its up axis and seen from a little above,
drawn by this file into a picture of its own: lit from above and in front,
its edges toward the eye in the menus' blue, on nothing (clear), which the
menu's tag_preview widget draws (ui_widget.c), as a Custom Edition map's
picture is drawn. Drawn by the processor, the same on every build, a
frame in TAG_PREVIEW_FRAME_MILLISECONDS at most: its triangles, nearest
first by a depth for each texel, each vertex's light carried across them.

Its model is read once the same tag has been chosen for
TAG_PREVIEW_SETTLE_MILLISECONDS, so that the rows scrolled past are not, a
profile's tag from its bundle (its tags, and its vertices and strips), the
map open's from the map's tags already loaded; and kept while it is chosen.
It is drawn to fill the widget's shape whichever way it has turned.
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries/cseries_windows.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "custom_edition_cache.h"
#include "tag_injector.h"
#include "tag_preview.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* the picture: of the widget's shape, more texels than the widget has
units, for screens several times the menus' 640x480 */
#define TAG_PREVIEW_TEXTURE_WIDTH 512
#define TAG_PREVIEW_TEXTURE_HEIGHT 256
/* how long a tag is chosen before its model is read, and how often its
picture is drawn again */
#define TAG_PREVIEW_SETTLE_MILLISECONDS 150
#define TAG_PREVIEW_FRAME_MILLISECONDS 33
/* a turn's time, the view's pitch down onto it (radians), and how much of
the widget it fills at most */
#define TAG_PREVIEW_TURN_MILLISECONDS 8000
#define TAG_PREVIEW_PITCH 0.35f
#define TAG_PREVIEW_FILL 0.9f
/* its light: what every side has, what faces the light has more, and the
blue its edges have; the light's direction, to the right, up and toward the
eye */
#define TAG_PREVIEW_AMBIENT 0.3f
#define TAG_PREVIEW_DIFFUSE 0.7f
#define TAG_PREVIEW_RIM 0.6f
#define TAG_PREVIEW_LIGHT_RIGHT -0.45f
#define TAG_PREVIEW_LIGHT_UP 0.55f
#define TAG_PREVIEW_LIGHT_TOWARD 0.70f
#define TAG_PREVIEW_KEY_LENGTH 384
#define TAG_PREVIEW_PI 3.14159265f

/* (bitmaps.c's format of 32-bit color with alpha) */
enum
{
	_bitmap_format_a8r8g8b8 = 11,
};

/* its color, and its edges' (the menus' #2896FF) */
static float const tag_preview_color[3] = { 214.0f, 222.0f, 232.0f };
static float const tag_preview_rim_color[3] = { 40.0f, 150.0f, 255.0f };

/* what the preview is of */
enum
{
	_tag_preview_none,
	_tag_preview_map_tag,
	_tag_preview_profile_tag,
};

/* ---------- globals */

/* (port_tag_profiles.c's: port_tag_profiles.h) */
long port_tag_profiles_count(void);
long port_tag_profiles_tag_count(long profile);
char const *port_tag_profiles_folder(long profile);
char const *port_tag_profiles_tag_name(long profile, long tag);
unsigned long port_tag_profiles_tag_group(long profile, long tag);

static struct
{
	/* the tag chosen: what it is, by its key ("" for none), and when it was
	chosen; and whether its model has been read (its mesh empty when it has
	none) */
	char key[TAG_PREVIEW_KEY_LENGTH];
	short kind;
	long first;
	long second;
	unsigned long chosen_time;
	boolean read;
	struct tag_injector_mesh mesh;
	/* the mesh's middle, and the widget's units a world unit is drawn at */
	float center[3];
	float scale;
	/* each vertex as it is drawn: where in the picture (x, y) and its depth
	toward the eye; and its color */
	float (*projected)[3];
	float (*colors)[3];
	/* the picture and its texels' depths (failed: they could not be made,
	and are not tried again until the menu closes), and when it was drawn
	(drawn: since its model was read) */
	struct bitmap_data *bitmap;
	float *depths;
	boolean failed;
	unsigned long drawn_time;
	boolean drawn;
} tag_preview;

/* ---------- private code */

static void tag_preview_forget(
	void)
{
	tag_injector_mesh_free(&tag_preview.mesh);
	/* (the game's free does not take NULL) */
	if (tag_preview.projected)
		free(tag_preview.projected);
	if (tag_preview.colors)
		free(tag_preview.colors);
	tag_preview.projected = NULL;
	tag_preview.colors = NULL;
	tag_preview.read = FALSE;
	tag_preview.drawn = FALSE;
}

/* the middle of the mesh, and the scale that fits it in the widget however
it turns: across, as far as its farthest vertex from its up axis; down,
as high as it is, tipped by the pitch */
static void tag_preview_fit(
	void)
{
	struct tag_injector_mesh const *mesh = &tag_preview.mesh;
	float minimum[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
	float maximum[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	float across = 0.0f;
	float high = 0.0f;
	float down;
	long index;
	short axis;

	for (index = 0; index < mesh->vertex_count; index++)
	{
		for (axis = 0; axis < 3; axis++)
		{
			minimum[axis] = MIN(minimum[axis], mesh->positions[index][axis]);
			maximum[axis] = MAX(maximum[axis], mesh->positions[index][axis]);
		}
	}
	for (axis = 0; axis < 3; axis++)
		tag_preview.center[axis] = (minimum[axis] + maximum[axis]) * 0.5f;
	for (index = 0; index < mesh->vertex_count; index++)
	{
		float x = mesh->positions[index][0] - tag_preview.center[0];
		float y = mesh->positions[index][1] - tag_preview.center[1];

		across = MAX(across, x * x + y * y);
	}
	across = MAX(sqrtf(across), 0.001f);
	high = (maximum[2] - minimum[2]) * 0.5f;
	down = high * cosf(TAG_PREVIEW_PITCH) + across * sinf(TAG_PREVIEW_PITCH);
	tag_preview.scale = TAG_PREVIEW_FILL *
		MIN(TAG_PREVIEW_WIDTH * 0.5f / across, TAG_PREVIEW_HEIGHT * 0.5f / MAX(down, 0.001f));
}

/* the tag chosen's model read */
static void tag_preview_read(
	void)
{
	boolean read = FALSE;

	tag_preview.read = TRUE;
	if (tag_preview.kind == _tag_preview_map_tag)
		read = tag_injector_map_tag_mesh((short)tag_preview.first, tag_preview.second, &tag_preview.mesh);
	else if (tag_preview.kind == _tag_preview_profile_tag)
		read = tag_injector_profile_tag_mesh(tag_preview.first, tag_preview.second, &tag_preview.mesh);
	if (!read)
		return;
	tag_preview.projected = malloc((unsigned long)tag_preview.mesh.vertex_count * sizeof(*tag_preview.projected));
	tag_preview.colors = malloc((unsigned long)tag_preview.mesh.vertex_count * sizeof(*tag_preview.colors));
	if (!tag_preview.projected || !tag_preview.colors)
	{
		tag_preview_forget();
		tag_preview.read = TRUE;
		return;
	}
	tag_preview_fit();
}

/* the tag chosen now: its key, and what it is */
static void tag_preview_choose(
	char const *key,
	short kind,
	long first,
	long second)
{
	unsigned long now = system_milliseconds();

	if (strcmp(key, tag_preview.key))
	{
		tag_preview_forget();
		snprintf(tag_preview.key, sizeof(tag_preview.key), "%s", key);
		tag_preview.kind = kind;
		tag_preview.first = first;
		tag_preview.second = second;
		tag_preview.chosen_time = now;
	}
	if (!tag_preview.read && tag_preview.kind != _tag_preview_none &&
		now - tag_preview.chosen_time >= TAG_PREVIEW_SETTLE_MILLISECONDS)
	{
		tag_preview_read();
	}
}

/* the picture made, the first time it is wanted; FALSE when it cannot be */
static boolean tag_preview_picture_new(
	void)
{
	if (tag_preview.bitmap)
		return TRUE;
	if (tag_preview.failed)
		return FALSE;
	tag_preview.failed = TRUE;
	tag_preview.depths = malloc(TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * sizeof(float));
	if (!tag_preview.depths)
		return FALSE;
	tag_preview.bitmap = bitmap_2d_new(TAG_PREVIEW_TEXTURE_WIDTH, TAG_PREVIEW_TEXTURE_HEIGHT, 0,
		_bitmap_format_a8r8g8b8);
	if (tag_preview.bitmap && tag_preview.bitmap->base_address)
	{
		memset(XBOX_POINTER(uint32_t, tag_preview.bitmap->base_address), 0,
			TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * sizeof(uint32_t));
		bitmap_rebuild(tag_preview.bitmap);
	}
	if (tag_preview.bitmap && (!tag_preview.bitmap->base_address || !tag_preview.bitmap->hardware_format))
	{
		bitmap_delete(tag_preview.bitmap);
		tag_preview.bitmap = NULL;
	}
	if (!tag_preview.bitmap)
	{
		free(tag_preview.depths);
		tag_preview.depths = NULL;
		return FALSE;
	}
	tag_preview.failed = FALSE;
	return TRUE;
}

/* each vertex where it is drawn as the model has turned by `turn`, and its
color */
static void tag_preview_project(
	float turn)
{
	struct tag_injector_mesh const *mesh = &tag_preview.mesh;
	float turn_cosine = cosf(turn);
	float turn_sine = sinf(turn);
	float pitch_cosine = cosf(TAG_PREVIEW_PITCH);
	float pitch_sine = sinf(TAG_PREVIEW_PITCH);
	float across = tag_preview.scale * TAG_PREVIEW_TEXTURE_WIDTH / TAG_PREVIEW_WIDTH;
	float down = tag_preview.scale * TAG_PREVIEW_TEXTURE_HEIGHT / TAG_PREVIEW_HEIGHT;
	float light[3] = { TAG_PREVIEW_LIGHT_RIGHT, TAG_PREVIEW_LIGHT_UP, TAG_PREVIEW_LIGHT_TOWARD };
	float length = sqrtf(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
	long index;
	short channel;

	for (channel = 0; channel < 3; channel++)
		light[channel] /= length;
	for (index = 0; index < mesh->vertex_count; index++)
	{
		float const *position = mesh->positions[index];
		float const *normal = mesh->normals[index];
		float x = position[0] - tag_preview.center[0];
		float y = position[1] - tag_preview.center[1];
		float z = position[2] - tag_preview.center[2];
		/* (the eye on the model's forward axis once turned, looking back
		along it: its right the model's left, +y) */
		float toward = x * turn_cosine - y * turn_sine;
		float right = x * turn_sine + y * turn_cosine;
		float up = z * pitch_cosine - toward * pitch_sine;
		float normal_toward = normal[0] * turn_cosine - normal[1] * turn_sine;
		float normal_right = normal[0] * turn_sine + normal[1] * turn_cosine;
		float normal_up = normal[2] * pitch_cosine - normal_toward * pitch_sine;
		float facing;
		float lit;
		float rim;

		normal_toward = normal_toward * pitch_cosine + normal[2] * pitch_sine;
		tag_preview.projected[index][0] = TAG_PREVIEW_TEXTURE_WIDTH * 0.5f + right * across;
		tag_preview.projected[index][1] = TAG_PREVIEW_TEXTURE_HEIGHT * 0.5f - up * down;
		tag_preview.projected[index][2] = toward * pitch_cosine + z * pitch_sine;
		length = sqrtf(normal_right * normal_right + normal_up * normal_up + normal_toward * normal_toward);
		if (length > 0.001f)
		{
			/* (both sides of a triangle drawn: the side seen is the one lit) */
			if (normal_toward < 0.0f)
				length = -length;
			normal_right /= length;
			normal_up /= length;
			normal_toward /= length;
			facing = normal_right * light[0] + normal_up * light[1] + normal_toward * light[2];
			lit = TAG_PREVIEW_AMBIENT + TAG_PREVIEW_DIFFUSE * MAX(facing, 0.0f);
			rim = 1.0f - normal_toward;
			rim = rim * rim * rim * TAG_PREVIEW_RIM;
		}
		else
		{
			lit = TAG_PREVIEW_AMBIENT + TAG_PREVIEW_DIFFUSE * 0.5f;
			rim = 0.0f;
		}
		for (channel = 0; channel < 3; channel++)
		{
			tag_preview.colors[index][channel] =
				MIN(tag_preview_color[channel] * lit + tag_preview_rim_color[channel] * rim, 255.0f);
		}
	}
}

/* a triangle into the picture: each texel whose middle it covers and is
nearer than what is there */
static void tag_preview_triangle(
	uint32_t *texels,
	unsigned short const *triangle)
{
	float const *a = tag_preview.projected[triangle[0]];
	float const *b = tag_preview.projected[triangle[1]];
	float const *c = tag_preview.projected[triangle[2]];
	float const *a_color = tag_preview.colors[triangle[0]];
	float const *b_color = tag_preview.colors[triangle[1]];
	float const *c_color = tag_preview.colors[triangle[2]];
	float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
	long left;
	long right;
	long top;
	long bottom;
	long x;
	long y;

	if (fabsf(area) < 0.0001f)
		return;
	left = (long)MAX(floorf(MIN(a[0], MIN(b[0], c[0]))), 0.0f);
	right = (long)MIN(ceilf(MAX(a[0], MAX(b[0], c[0]))), (float)(TAG_PREVIEW_TEXTURE_WIDTH - 1));
	top = (long)MAX(floorf(MIN(a[1], MIN(b[1], c[1]))), 0.0f);
	bottom = (long)MIN(ceilf(MAX(a[1], MAX(b[1], c[1]))), (float)(TAG_PREVIEW_TEXTURE_HEIGHT - 1));
	area = 1.0f / area;
	for (y = top; y <= bottom; y++)
	{
		float middle_y = (float)y + 0.5f;

		for (x = left; x <= right; x++)
		{
			float middle_x = (float)x + 0.5f;
			/* (each corner's share: the area across from it, of the whole) */
			float a_share = ((b[0] - middle_x) * (c[1] - middle_y) - (b[1] - middle_y) * (c[0] - middle_x)) * area;
			float b_share = ((c[0] - middle_x) * (a[1] - middle_y) - (c[1] - middle_y) * (a[0] - middle_x)) * area;
			float c_share = 1.0f - a_share - b_share;
			long texel = y * TAG_PREVIEW_TEXTURE_WIDTH + x;
			float depth;
			uint32_t color = 0xFF000000u;
			short channel;

			if (a_share < 0.0f || b_share < 0.0f || c_share < 0.0f)
				continue;
			depth = a_share * a[2] + b_share * b[2] + c_share * c[2];
			if (depth <= tag_preview.depths[texel])
				continue;
			tag_preview.depths[texel] = depth;
			for (channel = 0; channel < 3; channel++)
			{
				float value = a_share * a_color[channel] + b_share * b_color[channel] + c_share * c_color[channel];

				color |= (uint32_t)PIN(value, 0.0f, 255.0f) << (16 - 8 * channel);
			}
			texels[texel] = color;
		}
	}
}

/* the picture drawn again, the model turned as it is now */
static void tag_preview_draw(
	unsigned long now)
{
	uint32_t *texels = XBOX_POINTER(uint32_t, tag_preview.bitmap->base_address);
	/* (from its side: a weapon's length across) */
	float turn = (float)(now % TAG_PREVIEW_TURN_MILLISECONDS) / TAG_PREVIEW_TURN_MILLISECONDS * 2.0f * TAG_PREVIEW_PI +
		TAG_PREVIEW_PI * 0.5f;
	long index;

	tag_preview_project(turn);
	memset(texels, 0, TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT * sizeof(uint32_t));
	for (index = 0; index < TAG_PREVIEW_TEXTURE_WIDTH * TAG_PREVIEW_TEXTURE_HEIGHT; index++)
		tag_preview.depths[index] = -FLT_MAX;
	for (index = 0; index < tag_preview.mesh.triangle_count; index++)
		tag_preview_triangle(texels, tag_preview.mesh.triangles[index]);
	rasterizer_bitmap_changed(tag_preview.bitmap);
	tag_preview.drawn_time = now;
	tag_preview.drawn = TRUE;
}

/* ---------- public code */

void tag_preview_map_tag(
	short category,
	long index)
{
	char key[TAG_PREVIEW_KEY_LENGTH];

	snprintf(key, sizeof(key), "map %d %lu %s", tag_injector_map_opened(), tag_injector_category_group(category),
		tag_injector_category_tag(category, index));
	tag_preview_choose(key, _tag_preview_map_tag, category, index);
}

void tag_preview_profile_tag(
	long profile,
	long tag)
{
	char key[TAG_PREVIEW_KEY_LENGTH];

	if (profile < 0 || profile >= port_tag_profiles_count() || tag < 0 || tag >= port_tag_profiles_tag_count(profile))
	{
		tag_preview_none();
		return;
	}
	snprintf(key, sizeof(key), "profile %s %lu %s", port_tag_profiles_folder(profile),
		port_tag_profiles_tag_group(profile, tag), port_tag_profiles_tag_name(profile, tag));
	tag_preview_choose(key, _tag_preview_profile_tag, profile, tag);
}

void tag_preview_none(
	void)
{
	tag_preview_choose("", _tag_preview_none, NONE, NONE);
}

struct bitmap_data *tag_preview_picture(
	void)
{
	unsigned long now = system_milliseconds();

	if (!tag_preview.read || tag_preview.mesh.triangle_count <= 0 || !tag_preview_picture_new())
		return NULL;
	if (!tag_preview.drawn || now - tag_preview.drawn_time >= TAG_PREVIEW_FRAME_MILLISECONDS)
		tag_preview_draw(now);

	return tag_preview.bitmap;
}

void tag_preview_dispose(
	void)
{
	tag_preview_forget();
	tag_preview.key[0] = 0;
	tag_preview.kind = _tag_preview_none;
	if (tag_preview.bitmap)
		bitmap_delete(tag_preview.bitmap);
	tag_preview.bitmap = NULL;
	if (tag_preview.depths)
		free(tag_preview.depths);
	tag_preview.depths = NULL;
	tag_preview.failed = FALSE;
}
