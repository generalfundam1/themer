/* common.h: what the graphical front ends (themer-gtk, themer-qt) share beyond
 * src/front.c, which every front end uses to change settings and apply: the
 * previews. They are previewers and switches in the spirit of nwg-look: the window
 * restyles itself from the theme being browsed, and Apply runs the normal apply, so
 * every module (terminals, GTK, Qt, the window manager, the browsers) follows. */
#ifndef GUI_COMMON_H
#define GUI_COMMON_H

#include "themer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The effective colors as hex without '#': the roles (C_* order), then the 16
 * terminal colors, then line, on_urg, on_warn. */
void gui_scheme(const Config *c, Scheme *s);

/* One image of a cursor from an Xcursor theme: the cursor named name (such as
 * "left_ptr", or "left_ptr|default" to try several) in theme, at the nominal size closest to size, following
 * the theme's Inherits. pix receives up to maxpix premultiplied ARGB pixels in
 * native byte order, row by row, w and h the size. Returns 0, or -1 if the theme
 * or the cursor is not installed. */
int gui_cursor_image(const char *theme, const char *name, int size, unsigned *pix,
                     int maxpix, int *w, int *h);

/* The file of an icon in an icon theme, at the size wanted (pixels), found as GTK
 * and Qt find it: the theme, the themes it inherits, then hicolor. Returns 0 with
 * the path in out, or -1 when no theme in the chain has it. */
int gui_icon_path(const char *theme, const char *name, int size, char *out, size_t n);

/* That icon's file, read: falls back to NAME-symbolic as GTK and Qt do (current
 * Adwaita has many icons only in that form), and paints a symbolic SVG in the
 * color fg (hex without #; NULL leaves it). Returns the bytes (NUL-terminated,
 * free them) with len and the MIME type, or NULL when the theme lacks it. */
char *gui_icon_load(const char *theme, const char *name, int size, const char *fg,
                    size_t *len, const char **mime);

/* The icons the icon preview shows, as (icon name, caption) pairs, and the cursors
 * the cursor preview shows, as Xcursor names with "|" between alternatives. Both
 * NULL-terminated. */
extern const char *const gui_preview_icons[][2];
extern const char *const gui_preview_cursors[][2];

/* The sample text the previews draw: a terminal transcript in the terminal font. */
extern const char *const gui_sample_lines[];
extern const int gui_nsample_lines;

#ifdef __cplusplus
}
#endif

#endif
