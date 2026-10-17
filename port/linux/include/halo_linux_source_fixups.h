/*
HALO_LINUX_SOURCE_FIXUPS.H

The port's functions that the game's sources call, declared for them.
*/

#ifndef __HALO_LINUX_SOURCE_FIXUPS_H
#define __HALO_LINUX_SOURCE_FIXUPS_H

/* frames between the 30 Hz ticks (port/linux/game/render_interpolation.c);
the platform layer reads the display.interpolation setting */
struct observer_result;
struct render_camera;
struct real_matrix4x3;
int halo_interpolation_enabled(void);
float game_time_get_tick_fraction(void);
void render_interpolation_tick(void);
void render_interpolation_reset(void);
void render_interpolation_frame_begin(void);
void render_interpolation_frame_end(void);
float render_interpolation_fraction(void);
struct real_matrix4x3 *render_interpolation_object_node_matrices(long object_index);
struct observer_result const *render_interpolation_camera(short local_player_index,
	struct observer_result const *observer);
void render_interpolation_first_person(short local_player_index, struct real_matrix4x3 *node_matrices,
	short node_count, struct render_camera const *camera);
float render_interpolation_game_time_sec(long ticks);

/* the port's menus' screen for one of ui.map's, by name, else the name
(port/linux/game/menu_tags.c) */
char const *pc_menus_screen(char const *name);
/* the Custom Edition tag cache window, or NULL unless the
game.custom_edition setting reserved it (port/linux/src/xbox_memory.c) */
void *halo_custom_edition_tag_cache(void);
/* where Halo PC keeps the channels of the pixels a Custom Edition bitmap
just arrived at (an enum custom_edition_channel_order,
port/linux/game/cache_file_formats.h), which the renderer then samples in
this build's order; forgotten together when the map goes
(port/linux/src/xbox_textures.c) */
void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order);
void halo_custom_edition_texels_forget(void);
/* whether a Halo Custom Edition map's multiplayer vehicles are chosen by
their placements' spawn flags, as in retail Halo, and whether a vehicle
placement is placed in the running game
(port/linux/game/custom_edition_objects.c) */
struct scenario_object_datum;
unsigned char custom_edition_vehicles_by_placement(void);
unsigned char custom_edition_vehicle_placement_allowed(struct scenario_object_datum const *placement);

/* the width of the screen the game draws, 480 lines tall: the device's or
the display's shape, or 640 (port/linux/src/d3d8_gl.c) */
long halo_screen_width(void);
/* the screen's pixels to the Xbox's one (port/linux/src/d3d8_gl.c) */
float halo_screen_scale(void);
/* takes up a new width between frames (F11); returns the width */
long halo_screen_commit(void);
/* the shadow maps' pixels for each of their 128 texels each way, a power of
two (display.shadow_resolution) */
long halo_shadow_map_scale(void);
/* while TRUE, drawing shifts right to center 640-column layouts */
void halo_screen_ui_offset(unsigned char centered);
/* names a model lighting vertex program, whose draws can be lit for each
pixel (port/linux/src/d3d8_gl.c, display.per_pixel_lighting) */
void halo_vertex_shader_lighting(unsigned long handle);
/* display.anti_aliasing's pass over a window's 3D view, before the HUD and
menus (source/render/render.c): the window's bounds on the screen */
void halo_screen_anti_alias(short x0, short y0, short x1, short y1);
/* the part of a frame the game is about to draw (source/render/render.c):
the Switch's renderer keeps it in its record of the commands it ran, for
the log after a GPU fault (port/switch/host/host_dk.c); nothing elsewhere */
void halo_render_marker(char const *name);
/* the draws that follow drawn only where texture coordinate 0's u and v are
in [low, high], or (clip FALSE) everywhere: a shadow cast on the level
(source/rasterizer/xbox/rasterizer_xbox_shadows.c), which changes nothing
outside its map; the Switch's renderer clips there (port/switch/guest/
d3d8_dk.c), the others draw it all */
void halo_texture_clip(unsigned char clip, float low, float high);
/* whether the zoom's blur is drawn on a small copy of the screen and blended
over it once (source/rasterizer/xbox/rasterizer_xbox_screen_effect.c), and
that blend's shader on or off for the draws that follow: the Switch's
renderer only (port/switch/guest/d3d8_dk.c; debug.original_zoom) */
unsigned char halo_screen_fast_warp(void);
void halo_screen_warp_blend(unsigned char on);
/* the mouse in the menus (source/interface/ui_widget.c) */
#include "halo_ui_pointer.h"
/* TRUE when B steps out of a custom-map folder or the Map screen's
difficulties and the screen stays (port/linux/game/menu_functions.c) */
unsigned char menu_port_back(void *widget);
/* the campaign's skulls (port/linux/game/skulls.c) */
#include "halo_skulls.h"
/* the Switch GPU, waited on before a map's memory goes
(port/switch/guest/d3d8_dk.c) */
#ifdef HALO_SWITCH
void halo_switch_renderer_idle(void);
#endif

#endif
