/*
TAG_PREVIEW.H

The Tag Injector's preview (tag_preview.c): under the details of a tag
chosen in its menu, the tag's model, textured and turning, drawn into a
picture the menu's tag_preview widget shows; and what the tag needs to work,
which the menu says under it.
*/

#ifndef __TAG_PREVIEW_H
#define __TAG_PREVIEW_H
#pragma once

/* ---------- constants */

/* the tag_preview widget's size in the menus' 640x480
(tools/port_settings.py), which the picture is drawn in the shape of */
#define TAG_PREVIEW_WIDTH 200
#define TAG_PREVIEW_HEIGHT 80

/* what is known of the needs of the tag the preview is of
(tag_preview_needs) */
enum tag_preview_needs_state
{
	/* none chosen, or not read yet */
	_tag_preview_needs_waiting,
	/* its tag (a profile's bundle) cannot be read */
	_tag_preview_needs_unreadable,
	_tag_preview_needs_read,
};

/* ---------- prototypes/TAG_PREVIEW.C */

struct bitmap_data;
struct tag_injector_needs;

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

/* What the tag the preview is of needs to work (tag_injector.h's), into
`needs` once it is read: what is known of them. */
short tag_preview_needs(
	struct tag_injector_needs *needs);

/* the model and the picture let go (the menu closed) */
void tag_preview_dispose(
	void);

#endif // __TAG_PREVIEW_H
