/*
TAG_PREVIEW.H

The Tag Injector's preview (tag_preview.c): under the details of a tag
chosen in its menu, the tag's model, turning, drawn into a picture the
menu's tag_preview widget shows.
*/

#ifndef __TAG_PREVIEW_H
#define __TAG_PREVIEW_H
#pragma once

/* ---------- constants */

/* the tag_preview widget's size in the menus' 640x480
(tools/port_settings.py), which the picture is drawn in the shape of */
#define TAG_PREVIEW_WIDTH 200
#define TAG_PREVIEW_HEIGHT 88

/* ---------- prototypes/TAG_PREVIEW.C */

struct bitmap_data;

/* The tag the preview is of, each frame the menu is shown (menu_functions.c's
"port tags update"): the open map's tag of a category, a profile's tag, or
none. Its model is read once the same one has been chosen for a moment (not
each row scrolled past), and kept while it is. */
void tag_preview_map_tag(
	short category,
	long index);
void tag_preview_profile_tag(
	long profile,
	long tag);
void tag_preview_none(
	void);

/* The preview's picture, its model drawn as it turns now, for the
tag_preview widget (ui_widget.c), or NULL while there is none. */
struct bitmap_data *tag_preview_picture(
	void);

/* the model and the picture let go (the menu closed) */
void tag_preview_dispose(
	void);

#endif // __TAG_PREVIEW_H
