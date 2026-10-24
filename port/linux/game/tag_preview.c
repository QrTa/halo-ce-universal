/*
TAG_PREVIEW.C

The Tag Injector's preview (tag_preview.h): the model of the tag chosen in
its menu (tag_injector.c's tag_injector_*_preview: a weapon's, equipment's,
a vehicle's, a biped's or scenery's own, an actor variant's its unit's), in
its default pose, turning about its up axis and seen from a little above,
drawn by this file into a picture of its own: each part with its shader's
base map (a model's or an environment's shader's; grey without one), lit
from above and in front, its edges toward the eye in the menus' blue, on
nothing (clear), which the menu's tag_preview widget draws (ui_widget.c),
as a Custom Edition map's picture is drawn. Drawn by the processor, the same
on every build, a frame in TAG_PREVIEW_FRAME_MILLISECONDS at most: its
triangles, nearest first by a depth for each texel, each vertex's light and
texture coordinates carried across them (straight across: the view has no
perspective), each texel its texture's nearest.

The textures are decoded from Halo PC's pixels as they are read (DXT1, 3
and 5, and the uncompressed formats). A base map's alpha is not drawn (it is
a mask for its detail maps, not a part's transparency); a multipurpose map's
is its color change mask, where a model's shader's base map is tinted by the
object's change color its shader names (tag_injector_mesh_material: a grunt
major's red, a minor's orange, on the same model).

Its model is read once the same tag has been chosen for
TAG_PREVIEW_SETTLE_MILLISECONDS, so that the rows scrolled past are not, a
profile's tag from its bundle (its tags, and its vertices, strips and
pixels), the map open's from the map's tags already loaded; and kept while
it is chosen, with what it needs to work (tag_preview_needs). It is drawn to
fill the widget's shape whichever way it has turned.
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
eye; and how much brighter a texture is drawn than it is (a base map is
made to be lit by more than this light) */
#define TAG_PREVIEW_AMBIENT 0.3f
#define TAG_PREVIEW_DIFFUSE 0.7f
#define TAG_PREVIEW_RIM 0.6f
#define TAG_PREVIEW_LIGHT_RIGHT -0.45f
#define TAG_PREVIEW_LIGHT_UP 0.55f
#define TAG_PREVIEW_LIGHT_TOWARD 0.70f
#define TAG_PREVIEW_TEXTURE_BRIGHTNESS 1.5f
#define TAG_PREVIEW_KEY_LENGTH 384
#define TAG_PREVIEW_PI 3.14159265f

/* (bitmaps.c's formats: the picture's, and those of the textures decoded) */
enum
{
	_bitmap_format_a8 = 0,
	_bitmap_format_y8 = 1,
	_bitmap_format_ay8 = 2,
	_bitmap_format_a8y8 = 3,
	_bitmap_format_r5g6b5 = 6,
	_bitmap_format_a1r5g5b5 = 8,
	_bitmap_format_a4r4g4b4 = 9,
	_bitmap_format_x8r8g8b8 = 10,
	_bitmap_format_a8r8g8b8 = 11,
	_bitmap_format_dxt1 = 14,
	_bitmap_format_dxt3 = 15,
	_bitmap_format_dxt5 = 16,
};

/* its color without a texture, and its edges' (the menus' #2896FF) */
static float const tag_preview_color[3] = { 214.0f, 222.0f, 232.0f };
static float const tag_preview_rim_color[3] = { 40.0f, 150.0f, 255.0f };

/* what the preview is of */
enum
{
	_tag_preview_none,
	_tag_preview_map_tag,
	_tag_preview_profile_tag,
};

/* ---------- structures */

/* a texture of the mesh, decoded: its texels, 0xAARRGGBB, the top row
first (NULL: one that cannot be decoded, drawn grey) */
struct tag_preview_texture
{
	long width;
	long height;
	uint32_t *texels;
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
	chosen; and whether it has been read, and could be (its mesh empty when
	its model cannot be), and what it needs to work */
	char key[TAG_PREVIEW_KEY_LENGTH];
	short kind;
	long first;
	long second;
	unsigned long chosen_time;
	boolean read;
	boolean readable;
	struct tag_injector_mesh mesh;
	struct tag_injector_needs needs;
	struct tag_preview_texture textures[TAG_INJECTOR_MESH_MAXIMUM_TEXTURES];
	/* the mesh's middle, and the widget's units a world unit is drawn at */
	float center[3];
	float scale;
	/* each vertex as it is drawn: where in the picture (x, y) and its depth
	toward the eye; and its light (what its color or texture is drawn by)
	and the blue of its edge */
	float (*projected)[3];
	float (*shading)[2];
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
	short index;

	tag_injector_mesh_free(&tag_preview.mesh);
	/* (the game's free does not take NULL) */
	for (index = 0; index < TAG_INJECTOR_MESH_MAXIMUM_TEXTURES; index++)
	{
		if (tag_preview.textures[index].texels)
			free(tag_preview.textures[index].texels);
		tag_preview.textures[index].texels = NULL;
	}
	if (tag_preview.projected)
		free(tag_preview.projected);
	if (tag_preview.shading)
		free(tag_preview.shading);
	tag_preview.projected = NULL;
	tag_preview.shading = NULL;
	memset(&tag_preview.needs, 0, sizeof(tag_preview.needs));
	tag_preview.read = FALSE;
	tag_preview.readable = FALSE;
	tag_preview.drawn = FALSE;
}

/* a 16-bit color of 5, 6 and 5 bits as 0xFFRRGGBB */
static uint32_t tag_preview_r5g6b5(
	unsigned long color)
{
	unsigned long red = (color >> 11) & 0x1F;
	unsigned long green = (color >> 5) & 0x3F;
	unsigned long blue = color & 0x1F;

	return 0xFF000000u | (uint32_t)((red << 3 | red >> 2) << 16 | (green << 2 | green >> 4) << 8 | (blue << 3 | blue >> 2));
}

/* the colors of a DXT block's 4x4 texels (at `block`, its 8 bytes of
colors), into the texture at (x, y); four colors (DXT3's and DXT5's), else
three and black where c0 is not more than c1 (DXT1's) */
static void tag_preview_dxt_colors(
	unsigned char const *block,
	boolean four,
	struct tag_preview_texture *texture,
	long x,
	long y)
{
	unsigned long c0 = (unsigned long)block[0] | (unsigned long)block[1] << 8;
	unsigned long c1 = (unsigned long)block[2] | (unsigned long)block[3] << 8;
	uint32_t colors[4];
	short channel;
	long row;
	long column;

	colors[0] = tag_preview_r5g6b5(c0);
	colors[1] = tag_preview_r5g6b5(c1);
	colors[2] = 0xFF000000u;
	/* (DXT1's fourth in three colors: clear) */
	colors[3] = four || c0 > c1 ? 0xFF000000u : 0;
	for (channel = 0; channel < 3; channel++)
	{
		unsigned long a = (colors[0] >> (8 * channel)) & 0xFF;
		unsigned long b = (colors[1] >> (8 * channel)) & 0xFF;

		if (four || c0 > c1)
		{
			colors[2] |= (uint32_t)((2 * a + b) / 3) << (8 * channel);
			colors[3] |= (uint32_t)((a + 2 * b) / 3) << (8 * channel);
		}
		else
		{
			colors[2] |= (uint32_t)((a + b) / 2) << (8 * channel);
		}
	}
	for (row = 0; row < 4 && y + row < texture->height; row++)
	{
		unsigned long bits = block[4 + row];

		for (column = 0; column < 4 && x + column < texture->width; column++)
			texture->texels[(y + row) * texture->width + x + column] = colors[(bits >> (2 * column)) & 3];
	}
}

/* the alpha of a DXT3 or DXT5 block's 4x4 texels (at `block`, its 8 bytes
of alpha) into the texture at (x, y), over the colors there */
static void tag_preview_dxt_alpha(
	unsigned char const *block,
	boolean interpolated,
	struct tag_preview_texture *texture,
	long x,
	long y)
{
	unsigned long alphas[8];
	unsigned long long bits = 0;
	short index;
	long row;
	long column;

	if (interpolated)
	{
		/* (DXT5's: two alphas, six or four between them, and 3 bits a texel) */
		alphas[0] = block[0];
		alphas[1] = block[1];
		for (index = 2; index < 8; index++)
		{
			alphas[index] = alphas[0] > alphas[1] ?
				((8 - index) * alphas[0] + (index - 1) * alphas[1]) / 7 :
				index < 6 ? ((6 - index) * alphas[0] + (index - 1) * alphas[1]) / 5 : index == 6 ? 0 : 255;
		}
		for (index = 0; index < 6; index++)
			bits |= (unsigned long long)block[2 + index] << (8 * index);
	}
	for (row = 0; row < 4 && y + row < texture->height; row++)
	{
		for (column = 0; column < 4 && x + column < texture->width; column++)
		{
			long texel = row * 4 + column;
			uint32_t *out = &texture->texels[(y + row) * texture->width + x + column];
			/* (DXT3's: 4 bits a texel) */
			unsigned long alpha = interpolated ? alphas[(bits >> (3 * texel)) & 7] :
				((unsigned long)(block[texel / 2] >> (4 * (texel & 1))) & 0xF) * 0x11;

			*out = (*out & 0x00FFFFFFu) | (uint32_t)alpha << 24;
		}
	}
}

/* a texture of the mesh's decoded from its pixels; FALSE for a format it is
not, or pixels too few for its size */
static boolean tag_preview_texture_decode(
	struct tag_injector_mesh_texture const *in,
	struct tag_preview_texture *out)
{
	unsigned char const *pixels = in->pixels;
	long count = (long)in->width * in->height;
	long blocks_across = (in->width + 3) / 4;
	long blocks_down = (in->height + 3) / 4;
	long index;

	out->width = in->width;
	out->height = in->height;
	if (out->width <= 0 || out->height <= 0 || !pixels)
		return FALSE;
	switch (in->format)
	{
	case _bitmap_format_dxt1:
		if (in->size < (unsigned long)(blocks_across * blocks_down * 8))
			return FALSE;
		break;
	case _bitmap_format_dxt3:
	case _bitmap_format_dxt5:
		if (in->size < (unsigned long)(blocks_across * blocks_down * 16))
			return FALSE;
		break;
	case _bitmap_format_a8:
	case _bitmap_format_y8:
	case _bitmap_format_ay8:
		if (in->size < (unsigned long)count)
			return FALSE;
		break;
	case _bitmap_format_a8y8:
	case _bitmap_format_r5g6b5:
	case _bitmap_format_a1r5g5b5:
	case _bitmap_format_a4r4g4b4:
		if (in->size < (unsigned long)count * 2)
			return FALSE;
		break;
	case _bitmap_format_x8r8g8b8:
	case _bitmap_format_a8r8g8b8:
		if (in->size < (unsigned long)count * 4)
			return FALSE;
		break;
	default:
		return FALSE;
	}
	out->texels = malloc((unsigned long)count * sizeof(uint32_t));
	if (!out->texels)
		return FALSE;
	if (in->format >= _bitmap_format_dxt1)
	{
		long block_bytes = in->format == _bitmap_format_dxt1 ? 8 : 16;
		long block_y;
		long block_x;

		for (block_y = 0; block_y < blocks_down; block_y++)
		{
			for (block_x = 0; block_x < blocks_across; block_x++)
			{
				unsigned char const *block = pixels + (block_y * blocks_across + block_x) * block_bytes;

				/* (DXT3's and DXT5's colors after their 8 bytes of alpha) */
				tag_preview_dxt_colors(block + block_bytes - 8, in->format != _bitmap_format_dxt1, out, block_x * 4,
					block_y * 4);
				if (in->format != _bitmap_format_dxt1)
					tag_preview_dxt_alpha(block, in->format == _bitmap_format_dxt5, out, block_x * 4, block_y * 4);
			}
		}
		return TRUE;
	}
	for (index = 0; index < count; index++)
	{
		uint32_t color;

		switch (in->format)
		{
		case _bitmap_format_a8:
			color = (uint32_t)pixels[index] << 24 | 0xFFFFFFu;
			break;
		case _bitmap_format_y8:
			color = 0xFF000000u | (uint32_t)pixels[index] * 0x010101u;
			break;
		case _bitmap_format_ay8:
			color = (uint32_t)pixels[index] * 0x01010101u;
			break;
		case _bitmap_format_a8y8:
			/* (its low byte the brightness, its high byte alpha) */
			color = (uint32_t)pixels[index * 2 + 1] << 24 | (uint32_t)pixels[index * 2] * 0x010101u;
			break;
		case _bitmap_format_r5g6b5:
			color = tag_preview_r5g6b5((unsigned long)pixels[index * 2] | (unsigned long)pixels[index * 2 + 1] << 8);
			break;
		case _bitmap_format_a1r5g5b5:
		{
			unsigned long value = (unsigned long)pixels[index * 2] | (unsigned long)pixels[index * 2 + 1] << 8;
			unsigned long red = (value >> 10) & 0x1F;
			unsigned long green = (value >> 5) & 0x1F;
			unsigned long blue = value & 0x1F;

			color = (value & 0x8000 ? 0xFF000000u : 0) | (uint32_t)((red << 3 | red >> 2) << 16 |
				(green << 3 | green >> 2) << 8 | (blue << 3 | blue >> 2));
			break;
		}
		case _bitmap_format_a4r4g4b4:
		{
			unsigned long value = (unsigned long)pixels[index * 2] | (unsigned long)pixels[index * 2 + 1] << 8;

			color = (uint32_t)((value >> 12) & 0xF) * 0x11 << 24 | (uint32_t)(((value >> 8) & 0xF) * 0x11 << 16 |
				((value >> 4) & 0xF) * 0x11 << 8 | (value & 0xF) * 0x11);
			break;
		}
		default:
			/* (blue, green, red and alpha, in that order: x8r8g8b8's alpha
			opaque) */
			color = (in->format == _bitmap_format_a8r8g8b8 ? (uint32_t)pixels[index * 4 + 3] << 24 : 0xFF000000u) |
				(uint32_t)pixels[index * 4 + 2] << 16 | (uint32_t)pixels[index * 4 + 1] << 8 | (uint32_t)pixels[index * 4];
			break;
		}
		out->texels[index] = color;
	}

	return TRUE;
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

/* the tag chosen read: what it needs, and its model and textures */
static void tag_preview_read(
	void)
{
	short index;

	tag_preview.read = TRUE;
	if (tag_preview.kind == _tag_preview_map_tag)
	{
		tag_preview.readable = tag_injector_map_tag_preview((short)tag_preview.first, tag_preview.second,
			&tag_preview.mesh, &tag_preview.needs);
	}
	else if (tag_preview.kind == _tag_preview_profile_tag)
	{
		tag_preview.readable = tag_injector_profile_tag_preview(tag_preview.first, tag_preview.second,
			&tag_preview.mesh, &tag_preview.needs);
	}
	if (tag_preview.mesh.triangle_count <= 0)
		return;
	for (index = 0; index < tag_preview.mesh.texture_count; index++)
	{
		if (!tag_preview_texture_decode(&tag_preview.mesh.textures[index], &tag_preview.textures[index]) &&
			tag_preview.textures[index].texels)
		{
			free(tag_preview.textures[index].texels);
			tag_preview.textures[index].texels = NULL;
		}
	}
	tag_preview.projected = malloc((unsigned long)tag_preview.mesh.vertex_count * sizeof(*tag_preview.projected));
	tag_preview.shading = malloc((unsigned long)tag_preview.mesh.vertex_count * sizeof(*tag_preview.shading));
	if (!tag_preview.projected || !tag_preview.shading)
	{
		struct tag_injector_needs needs = tag_preview.needs;
		boolean readable = tag_preview.readable;

		/* (what it needs kept: only its picture is gone) */
		tag_preview_forget();
		tag_preview.needs = needs;
		tag_preview.readable = readable;
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
light */
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
		tag_preview.shading[index][0] = lit;
		tag_preview.shading[index][1] = rim;
	}
}

/* a texture of the mesh's decoded, or NULL (none, or one that could not be) */
static struct tag_preview_texture const *tag_preview_texture(
	short index)
{
	return index >= 0 && index < tag_preview.mesh.texture_count && tag_preview.textures[index].texels ?
		&tag_preview.textures[index] : NULL;
}

/* a texture's nearest texel to (u, v), the texture repeating */
static uint32_t tag_preview_sample(
	struct tag_preview_texture const *texture,
	float u,
	float v)
{
	long x = (long)floorf((u - floorf(u)) * (float)texture->width);
	long y = (long)floorf((v - floorf(v)) * (float)texture->height);

	x = PIN(x, 0, texture->width - 1);
	y = PIN(y, 0, texture->height - 1);
	return texture->texels[y * texture->width + x];
}

/* a triangle into the picture: each texel whose middle it covers and is
nearer than what is there, its material's base map (tinted by its change
color where its mask says) or its color, by its light, and its edge's blue */
static void tag_preview_triangle(
	uint32_t *texels,
	long triangle_index)
{
	unsigned short const *triangle = tag_preview.mesh.triangles[triangle_index];
	short material_index = tag_preview.mesh.triangle_materials[triangle_index];
	struct tag_injector_mesh_material const *material =
		material_index >= 0 && material_index < tag_preview.mesh.material_count ?
		&tag_preview.mesh.materials[material_index] : NULL;
	struct tag_preview_texture const *texture = material ? tag_preview_texture(material->base_texture) : NULL;
	struct tag_preview_texture const *mask = material && material->tinted ?
		tag_preview_texture(material->mask_texture) : NULL;
	float const *a = tag_preview.projected[triangle[0]];
	float const *b = tag_preview.projected[triangle[1]];
	float const *c = tag_preview.projected[triangle[2]];
	float const *a_shading = tag_preview.shading[triangle[0]];
	float const *b_shading = tag_preview.shading[triangle[1]];
	float const *c_shading = tag_preview.shading[triangle[2]];
	float const *a_texcoord = tag_preview.mesh.texcoords[triangle[0]];
	float const *b_texcoord = tag_preview.mesh.texcoords[triangle[1]];
	float const *c_texcoord = tag_preview.mesh.texcoords[triangle[2]];
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
			float lit;
			float rim;
			float base[3];
			uint32_t color = 0xFF000000u;
			short channel;

			if (a_share < 0.0f || b_share < 0.0f || c_share < 0.0f)
				continue;
			depth = a_share * a[2] + b_share * b[2] + c_share * c[2];
			if (depth <= tag_preview.depths[texel])
				continue;
			tag_preview.depths[texel] = depth;
			lit = a_share * a_shading[0] + b_share * b_shading[0] + c_share * c_shading[0];
			rim = a_share * a_shading[1] + b_share * b_shading[1] + c_share * c_shading[1];
			if (texture || mask)
			{
				float u = a_share * a_texcoord[0] + b_share * b_texcoord[0] + c_share * c_texcoord[0];
				float v = a_share * a_texcoord[1] + b_share * b_texcoord[1] + c_share * c_texcoord[1];
				uint32_t sample = texture ? tag_preview_sample(texture, u, v) : 0xFFFFFFFFu;

				lit *= TAG_PREVIEW_TEXTURE_BRIGHTNESS;
				base[0] = (float)((sample >> 16) & 0xFF);
				base[1] = (float)((sample >> 8) & 0xFF);
				base[2] = (float)(sample & 0xFF);
				/* (the base map times the change color, where the mask is) */
				if (mask)
				{
					float amount = (float)(tag_preview_sample(mask, u, v) >> 24) * (1.0f / 255.0f);

					for (channel = 0; channel < 3; channel++)
						base[channel] *= 1.0f - amount + amount * material->tint[channel];
				}
			}
			else
			{
				base[0] = tag_preview_color[0];
				base[1] = tag_preview_color[1];
				base[2] = tag_preview_color[2];
			}
			for (channel = 0; channel < 3; channel++)
			{
				float value = base[channel] * lit + tag_preview_rim_color[channel] * rim;

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
		tag_preview_triangle(texels, index);
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

	if (!tag_preview.read || tag_preview.mesh.triangle_count <= 0 || !tag_preview.projected ||
		!tag_preview_picture_new())
	{
		return NULL;
	}
	if (!tag_preview.drawn || now - tag_preview.drawn_time >= TAG_PREVIEW_FRAME_MILLISECONDS)
		tag_preview_draw(now);

	return tag_preview.bitmap;
}

short tag_preview_needs(
	struct tag_injector_needs *needs)
{
	if (!tag_preview.read || tag_preview.kind == _tag_preview_none)
		return _tag_preview_needs_waiting;
	if (!tag_preview.readable)
		return _tag_preview_needs_unreadable;
	*needs = tag_preview.needs;
	return _tag_preview_needs_read;
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
