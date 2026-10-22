/* themer-gtk.c: a GTK 3 front end for themer, in the spirit of nwg-look.
 *
 * The window is a sidebar of pages (Theme, Fonts, Icons, Cursor, Modules) on the
 * left, the preview on the right, and an action bar with Apply at the bottom. The
 * preview is the real thing: the window restyles itself from the gtk.css the gtk
 * module would write for the theme being browsed, so the widgets on the Widgets
 * and List tabs look the way every GTK app will after Apply. The Terminal tab draws
 * a transcript in the terminal font and colors, the Icons and Cursor pages show the
 * selected icon theme and cursor theme themselves, and the swatches show the roles
 * and the sixteen terminal colors. Nothing is written until Apply, which runs the
 * same apply as "themer apply": every module, the environment, and the TTY, then
 * saves the state.
 *
 * Build: the Makefile builds it when pkg-config finds gtk+-3.0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "common.h"

#define MAX_LIST 256
#define ICON_PX  32
#define CURSOR_PX 32
#define MAX_CURSOR_PIX (512 * 512)

static Config cfg, applied;
static int dirty;

static GtkWidget *window, *theme_box, *mode_dark, *mode_light, *set_combo, *ui_font, *mono_font;
static GtkWidget *icon_combo, *cursor_combo, *cursor_size, *hint_combo, *aa_check, *rgba_combo;
static GtkWidget *modules_box, *status, *swatches, *sample, *apply_button, *icon_grid, *cursor_area;
static GtkWidget *title_label, *module_switch[MAX_MODULES];
static GtkCssProvider *provider;
static char theme_names[MAX_LIST][NAME_LEN];
static Theme themes[MAX_LIST];
static int ntheme_names, updating;
static int module_order[MAX_MODULES], nmodule_order;

/* the cursor images, decoded once per cursor theme */
static struct {
	unsigned *pix;
	int w, h, ok;
} cursors[16];
static int ncursors;
static char cursors_for[NAME_LEN];

/* ---- the live preview: the window takes the rendered GTK stylesheet ---- */

static void
restyle(void)
{
	static char css[65536];
	GtkSettings *settings = gtk_settings_get_default();
	char font[160];

	if (front_render(&cfg, "gtk", "gtk", "gtk.css", css, sizeof css) == 0)
		gtk_css_provider_load_from_data(provider, css, -1, NULL);
	g_object_set(settings, "gtk-application-prefer-dark-theme", cfg.mode == MODE_DARK, NULL);
	snprintf(font, sizeof font, "%s %d", cfg.ui_font, cfg.ui_size);
	g_object_set(settings, "gtk-font-name", font, NULL);
	/* the font rendering, as the GTK settings would carry it after Apply */
	g_object_set(settings, "gtk-xft-antialias", cfg.antialias ? 1 : 0,
	             "gtk-xft-hinting", strcmp(cfg.hinting, "none") ? 1 : 0,
	             "gtk-xft-rgba", cfg.rgba, NULL);
	{
		char hs[32];

		snprintf(hs, sizeof hs, "hint%s", cfg.hinting);
		g_object_set(settings, "gtk-xft-hintstyle", hs, NULL);
	}
	snprintf(font, sizeof font, "%s  \xc2\xb7  %s  \xc2\xb7  %s", cfg.th.name,
	         cfg.mode == MODE_DARK ? "dark" : "light", cfg.set);
	gtk_label_set_text(GTK_LABEL(title_label), font);
	gtk_widget_queue_draw(swatches);
	gtk_widget_queue_draw(sample);
	gtk_widget_queue_draw(theme_box);
}

static void
set_status(const char *text)
{
	gtk_label_set_text(GTK_LABEL(status), text);
}

static void
changed(void)
{
	if (updating)
		return;
	dirty = 1;
	set_status("unapplied changes");
	gtk_widget_set_sensitive(apply_button, TRUE);
	restyle();
}

/* ---- drawing helpers ---- */

static void
set_hex(cairo_t *cr, const char *hex)
{
	unsigned v = (unsigned)strtoul(hex, NULL, 16);

	cairo_set_source_rgb(cr, ((v >> 16) & 255) / 255.0, ((v >> 8) & 255) / 255.0, (v & 255) / 255.0);
}

/* a theme row's swatch: bg, surf, acc of the theme in the current mode */
static gboolean
draw_theme_swatch(GtkWidget *w, cairo_t *cr, gpointer data)
{
	const Theme *t = &themes[GPOINTER_TO_INT(data)];
	int m = cfg.mode == MODE_DARK ? 0 : 1;
	int h = gtk_widget_get_allocated_height(w);

	set_hex(cr, t->hex[m][C_BG]);
	cairo_rectangle(cr, 0, 0, 16, h);
	cairo_fill(cr);
	set_hex(cr, t->hex[m][C_SURF]);
	cairo_rectangle(cr, 16, 0, 12, h);
	cairo_fill(cr);
	set_hex(cr, t->hex[m][C_ACC]);
	cairo_rectangle(cr, 28, 0, 12, h);
	cairo_fill(cr);
	set_hex(cr, t->hex[m][C_FG]);
	cairo_rectangle(cr, 4, h / 2 - 1, 8, 2);
	cairo_fill(cr);
	return FALSE;
}

/* ---- the swatches: the roles, then the sixteen terminal colors ---- */

static gboolean
draw_swatches(GtkWidget *w, cairo_t *cr, gpointer data)
{
	static const char *role_names[C_N] = { "bg", "fg", "surf", "acc", "accfg", "urg", "warn" };
	Scheme s;
	int width = gtk_widget_get_allocated_width(w), cell, i;

	(void)data;
	gui_scheme(&cfg, &s);
	set_hex(cr, s.hex[C_BG]);
	cairo_paint(cr);
	cell = width / 16;
	if (cell < 8)
		cell = 8;
	cairo_select_font_face(cr, cfg.mono_font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 10);
	for (i = 0; i < C_N; i++) {
		set_hex(cr, s.hex[i]);
		cairo_rectangle(cr, i * cell * 2, 0, cell * 2 - 2, 28);
		cairo_fill(cr);
		/* the label in whichever of fg and bg reads better on the swatch */
		set_hex(cr, tm_contrast(s.hex[i], s.hex[C_FG]) >= tm_contrast(s.hex[i], s.hex[C_BG])
		            ? s.hex[C_FG] : s.hex[C_BG]);
		cairo_move_to(cr, i * cell * 2 + 4, 18);
		cairo_show_text(cr, role_names[i]);
	}
	for (i = 0; i < 16; i++) {
		set_hex(cr, s.ansi[i]);
		cairo_rectangle(cr, (i % 8) * cell * 2, 34 + (i / 8) * 22, cell * 2 - 2, 20);
		cairo_fill(cr);
	}
	return FALSE;
}

/* the transcript, in the terminal font and the terminal colors */
static gboolean
draw_sample(GtkWidget *w, cairo_t *cr, gpointer data)
{
	Scheme s;
	PangoLayout *layout;
	PangoFontDescription *desc;
	char spec[160];
	int width = gtk_widget_get_allocated_width(w), y = 8, i;
	const char *colors[] = { NULL, NULL, NULL, NULL, NULL, NULL };

	(void)data;
	gui_scheme(&cfg, &s);
	colors[0] = s.ansi[2]; colors[1] = s.ansi[4]; colors[2] = s.ansi[1];
	colors[3] = s.ansi[3]; colors[4] = s.hex[C_FG]; colors[5] = s.ansi[5];
	set_hex(cr, s.hex[C_BG]);
	cairo_paint(cr);
	set_hex(cr, s.line);
	cairo_rectangle(cr, 0.5, 0.5, width - 1, gtk_widget_get_allocated_height(w) - 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	snprintf(spec, sizeof spec, "%s %d", cfg.mono_font, cfg.mono_size);
	desc = pango_font_description_from_string(spec);
	layout = pango_cairo_create_layout(cr);
	pango_layout_set_font_description(layout, desc);
	for (i = 0; i < gui_nsample_lines; i++) {
		int h;

		set_hex(cr, colors[i % 6]);
		pango_layout_set_text(layout, gui_sample_lines[i], -1);
		cairo_move_to(cr, 8, y);
		pango_cairo_show_layout(cr, layout);
		pango_layout_get_pixel_size(layout, NULL, &h);
		y += h + 2;
	}
	/* a selection, in the accent */
	set_hex(cr, s.hex[C_ACC]);
	cairo_rectangle(cr, 8, y, width - 16, 20);
	cairo_fill(cr);
	set_hex(cr, s.hex[C_ACCFG]);
	pango_layout_set_text(layout, " selected text, in the accent ", -1);
	cairo_move_to(cr, 8, y + 2);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
	pango_font_description_free(desc);
	gtk_widget_set_size_request(w, -1, y + 32);
	return FALSE;
}

/* ---- the icon preview: the selected icon theme's own icons ---- */

static void
fill_icon_grid(void)
{
	GtkIconTheme *theme = gtk_icon_theme_new();
	GList *kids = gtk_container_get_children(GTK_CONTAINER(icon_grid)), *k;
	GdkRGBA fg;
	Scheme s;
	char hexcol[TM_COLOR_LEN + 1];
	int i;

	for (k = kids; k; k = k->next)
		gtk_widget_destroy(GTK_WIDGET(k->data));
	g_list_free(kids);
	gtk_icon_theme_set_custom_theme(theme, cfg.icons);
	gui_scheme(&cfg, &s);
	snprintf(hexcol, sizeof hexcol, "#%s", s.hex[C_FG]);
	gdk_rgba_parse(&fg, hexcol);
	for (i = 0; gui_preview_icons[i][0]; i++) {
		GtkIconInfo *info = gtk_icon_theme_lookup_icon(theme, gui_preview_icons[i][0], ICON_PX,
		                                               GTK_ICON_LOOKUP_FORCE_SIZE);
		GdkPixbuf *pb = NULL;
		GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2), *img, *l;

		/* symbolic icons are single-color: painted in the text color, as apps see them */
		if (info) {
			pb = gtk_icon_info_is_symbolic(info)
			     ? gtk_icon_info_load_symbolic(info, &fg, NULL, NULL, NULL, NULL, NULL)
			     : gtk_icon_info_load_icon(info, NULL);
			g_object_unref(info);
		}

		img = pb ? gtk_image_new_from_pixbuf(pb) : gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_DND);
		if (pb)
			g_object_unref(pb);
		gtk_widget_set_size_request(img, ICON_PX + 8, ICON_PX + 8);
		l = gtk_label_new(gui_preview_icons[i][1]);
		gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
		gtk_widget_set_size_request(l, 64, -1);
		gtk_box_pack_start(GTK_BOX(cell), img, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(cell), l, FALSE, FALSE, 0);
		gtk_grid_attach(GTK_GRID(icon_grid), cell, i % 5, i / 5, 1, 1);
	}
	g_object_unref(theme);
	gtk_widget_show_all(icon_grid);
}

/* ---- the cursor preview: the theme's cursor files, drawn at the chosen size ---- */

static void
load_cursors(void)
{
	int i;

	if (!strcmp(cursors_for, cfg.cursor) && ncursors)
		return;
	snprintf(cursors_for, sizeof cursors_for, "%s", cfg.cursor);
	for (i = 0; gui_preview_cursors[i][0] && i < 16; i++) {
		if (!cursors[i].pix)
			cursors[i].pix = malloc(MAX_CURSOR_PIX * sizeof(unsigned));
		cursors[i].ok = gui_cursor_image(cfg.cursor, gui_preview_cursors[i][0], CURSOR_PX,
		                                 cursors[i].pix, MAX_CURSOR_PIX, &cursors[i].w, &cursors[i].h) == 0;
	}
	ncursors = i;
}

static gboolean
draw_cursors(GtkWidget *w, cairo_t *cr, gpointer data)
{
	Scheme s;
	PangoLayout *layout = pango_cairo_create_layout(cr);
	int x = 12, y = 12, width = gtk_widget_get_allocated_width(w), found = 0, i;
	/* drawn at the chosen size, whatever size the file's image is */
	double scale = cfg.cursor_size / (double)CURSOR_PX;

	(void)data;
	gui_scheme(&cfg, &s);
	load_cursors();
	set_hex(cr, s.hex[C_BG]);
	cairo_paint(cr);
	set_hex(cr, s.line);
	cairo_rectangle(cr, 0.5, 0.5, width - 1, gtk_widget_get_allocated_height(w) - 1);
	cairo_set_line_width(cr, 1);
	cairo_stroke(cr);
	for (i = 0; i < ncursors; i++) {
		int px = (int)(CURSOR_PX * scale), cellw = px + 16 > 72 ? px + 16 : 72, cw;

		if (x + cellw > width - 12 && x > 12) {
			x = 12;
			y += px + 36;
		}
		if (cursors[i].ok) {
			cairo_surface_t *surf = cairo_image_surface_create_for_data(
				(unsigned char *)cursors[i].pix, CAIRO_FORMAT_ARGB32, cursors[i].w, cursors[i].h,
				cursors[i].w * 4);
			double sc = (double)px / (cursors[i].w > cursors[i].h ? cursors[i].w : cursors[i].h);

			cairo_save(cr);
			cairo_translate(cr, x + (cellw - px) / 2, y);
			cairo_scale(cr, sc, sc);
			cairo_set_source_surface(cr, surf, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
			cairo_surface_destroy(surf);
			found++;
		}
		set_hex(cr, s.hex[C_FG]);
		pango_layout_set_text(layout, gui_preview_cursors[i][1], -1);
		pango_layout_get_pixel_size(layout, &cw, NULL);
		cairo_move_to(cr, x + (cellw - cw) / 2, y + px + 6);
		pango_cairo_show_layout(cr, layout);
		x += cellw + 8;
	}
	if (!found) {
		pango_layout_set_text(layout, "no cursor theme of that name is installed", -1);
		set_hex(cr, s.hex[C_FG]);
		cairo_move_to(cr, 12, 12);
		pango_cairo_show_layout(cr, layout);
	}
	g_object_unref(layout);
	gtk_widget_set_size_request(w, -1, y + (int)(CURSOR_PX * scale) + 40);
	(void)found;
	return FALSE;
}

/* ---- the settings ---- */

static void
on_theme_selected(GtkListBox *box, GtkListBoxRow *row, gpointer data)
{
	int i;

	(void)box;
	(void)data;
	if (!row || updating)
		return;
	i = gtk_list_box_row_get_index(row);
	if (i < 0 || i >= ntheme_names || !strcmp(theme_names[i], cfg.th.name))
		return;
	if (front_set_theme(&cfg, theme_names[i]) < 0) {
		set_status("cannot read that theme");
		return;
	}
	changed();
}

static void
on_mode(GtkToggleButton *b, gpointer data)
{
	if (updating || !gtk_toggle_button_get_active(b))
		return;
	cfg.mode = GPOINTER_TO_INT(data);
	changed();
}

/* a combo whose text goes into a fixed buffer */
static void
on_combo_text(GtkComboBox *combo, gpointer data)
{
	const char *text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
	char *dst = data;
	int r;

	if (!text || updating || !text[0])
		return;
	r = dst == cfg.icons ? front_set_icons(&cfg, text)
	  : dst == cfg.cursor ? front_set_cursor(&cfg, text, 0) : front_set_set(&cfg, text);
	if (r < 0)
		return;
	if (dst == cfg.icons)
		fill_icon_grid();
	if (dst == cfg.cursor)
		gtk_widget_queue_draw(cursor_area);
	changed();
}

static void
on_cursor_size(GtkSpinButton *b, gpointer data)
{
	(void)data;
	if (updating)
		return;
	if (front_set_cursor(&cfg, NULL, gtk_spin_button_get_value_as_int(b)) < 0)
		return;
	gtk_widget_queue_draw(cursor_area);
	changed();
}

static void
on_hinting(GtkComboBox *combo, gpointer data)
{
	int i = gtk_combo_box_get_active(combo);

	(void)data;
	if (updating || i < 0)
		return;
	if (front_set_hinting(&cfg, hinting_values[i]) == 0)
		changed();
}

static void
on_rgba(GtkComboBox *combo, gpointer data)
{
	int i = gtk_combo_box_get_active(combo);

	(void)data;
	if (updating || i < 0)
		return;
	if (front_set_rgba(&cfg, rgba_values[i]) == 0)
		changed();
}

static void
on_antialias(GtkToggleButton *b, gpointer data)
{
	(void)data;
	if (updating)
		return;
	front_set_antialias(&cfg, gtk_toggle_button_get_active(b));
	changed();
}

static gboolean
monospace_only(const PangoFontFamily *family, const PangoFontFace *face, gpointer data)
{
	(void)face;
	(void)data;
	return pango_font_family_is_monospace((PangoFontFamily *)family);
}

/* The font buttons are plain buttons that open a font chooser, so they can show
 * the family by name even when it is not installed here (a GtkFontButton would say
 * "None"). data is 1 for the terminal font. */
static void
font_button_label(GtkWidget *b, const char *fam, int size)
{
	char text[160];

	snprintf(text, sizeof text, "%s  %d", fam, size);
	gtk_button_set_label(GTK_BUTTON(b), text);
}

static void
on_font_button(GtkButton *b, gpointer data)
{
	int mono = GPOINTER_TO_INT(data);
	char *fam = mono ? cfg.mono_font : cfg.ui_font;
	int *size = mono ? &cfg.mono_size : &cfg.ui_size;
	GtkWidget *d = gtk_font_chooser_dialog_new(mono ? "Terminal font" : "Interface font", GTK_WINDOW(window));
	char spec[160];

	snprintf(spec, sizeof spec, "%s %d", fam, *size);
	gtk_font_chooser_set_font(GTK_FONT_CHOOSER(d), spec);
	if (mono)
		gtk_font_chooser_set_filter_func(GTK_FONT_CHOOSER(d), monospace_only, NULL, NULL);
	if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
		PangoFontDescription *desc = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(d));

		if (desc) {
			int sz = pango_font_description_get_size(desc) > 0
			         ? pango_font_description_get_size(desc) / PANGO_SCALE : 0;

			if (front_set_font(&cfg, mono, pango_font_description_get_family(desc), sz) == 0) {
				font_button_label(GTK_WIDGET(b), fam, *size);
				changed();
			}
			pango_font_description_free(desc);
		}
	}
	gtk_widget_destroy(d);
}

static void
on_module(GtkSwitch *sw, GParamSpec *ps, gpointer data)
{
	int i = GPOINTER_TO_INT(data);

	(void)ps;
	if (updating)
		return;
	config_set_module(&cfg, themer_module_name(i), gtk_switch_get_active(sw));
	dirty = 1;
	set_status("unapplied changes");
	gtk_widget_set_sensitive(apply_button, TRUE);
}

static void
on_apply(GtkButton *b, gpointer data)
{
	int fails;

	(void)b;
	(void)data;
	set_status("applying...");
	while (gtk_events_pending())
		gtk_main_iteration();
	fails = front_apply(&cfg);
	applied = cfg;
	dirty = 0;
	gtk_widget_set_sensitive(apply_button, FALSE);
	if (fails)
		set_status("applied with failures; see the terminal");
	else
		set_status("applied. GTK apps keep their theme until restarted; restart Firefox and Thunderbird");
}

/* back to what was last applied */
static void
set_controls_from_cfg(void);

static void
on_revert(GtkWidget *w, gpointer data)
{
	(void)w;
	(void)data;
	cfg = applied;
	updating = 1;
	set_controls_from_cfg();
	updating = 0;
	dirty = 0;
	gtk_widget_set_sensitive(apply_button, FALSE);
	set_status("back to the applied settings");
	fill_icon_grid();
	gtk_widget_queue_draw(cursor_area);
	restyle();
}

static void
on_about(GtkWidget *w, gpointer data)
{
	(void)w;
	(void)data;
	gtk_show_about_dialog(GTK_WINDOW(window), "program-name", "themer",
	                      "comments", "One theme for the whole desktop: GTK, Qt, Firefox, "
	                                  "Thunderbird, the window manager, the terminals, the TTY.\n"
	                                  "The preview is the real thing: this window is styled "
	                                  "by the stylesheet Apply writes.",
	                      "logo-icon-name", "preferences-desktop-theme", NULL);
}

static gboolean
on_delete(GtkWidget *w, GdkEvent *e, gpointer data)
{
	GtkWidget *d;
	int r;

	(void)w;
	(void)e;
	(void)data;
	if (!dirty)
		return FALSE;
	d = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
	                           GTK_BUTTONS_NONE, "There are unapplied changes.");
	gtk_dialog_add_buttons(GTK_DIALOG(d), "Discard", GTK_RESPONSE_CLOSE, "Apply", GTK_RESPONSE_APPLY,
	                       "Keep editing", GTK_RESPONSE_CANCEL, NULL);
	r = gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
	if (r == GTK_RESPONSE_APPLY)
		on_apply(NULL, NULL);
	return r == GTK_RESPONSE_CANCEL;
}

/* ---- building the window ---- */

static GtkWidget *
heading(const char *text)
{
	GtkWidget *l = gtk_label_new(NULL);
	char *m = g_markup_printf_escaped("<b>%s</b>", text);

	gtk_label_set_markup(GTK_LABEL(l), m);
	g_free(m);
	gtk_widget_set_halign(l, GTK_ALIGN_START);
	gtk_widget_set_margin_top(l, 6);
	gtk_widget_set_margin_bottom(l, 2);
	return l;
}

static GtkWidget *
caption(const char *text)
{
	GtkWidget *l = gtk_label_new(text);

	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_widget_set_halign(l, GTK_ALIGN_START);
	gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
	return l;
}

/* label and control in one row of a grid */
static void
grid_row(GtkWidget *grid, int row, const char *label, GtkWidget *control)
{
	GtkWidget *l = gtk_label_new(label);

	gtk_widget_set_halign(l, GTK_ALIGN_END);
	gtk_widget_set_hexpand(control, TRUE);
	gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), control, 1, row, 1, 1);
}

static GtkWidget *
page_box(void)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);

	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	return box;
}

static GtkWidget *
grid_new(void)
{
	GtkWidget *g = gtk_grid_new();

	gtk_grid_set_row_spacing(GTK_GRID(g), 6);
	gtk_grid_set_column_spacing(GTK_GRID(g), 10);
	return g;
}

/* a combo of names with an entry, the current one selected or typed */
static GtkWidget *
name_combo(char names[][NAME_LEN], int n, const char *current)
{
	GtkWidget *combo = gtk_combo_box_text_new_with_entry();
	int i;

	for (i = 0; i < n; i++) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), names[i]);
		if (!strcmp(names[i], current))
			gtk_combo_box_set_active(GTK_COMBO_BOX(combo), i);
	}
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo)) < 0)
		gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(combo))), current);
	return combo;
}

/* ---- template sets: new, import, check ---- */

static void
pick_into(GtkEntry *entry, GtkFileChooserAction action, const char *title)
{
	GtkWidget *d = gtk_file_chooser_dialog_new(title, GTK_WINDOW(window), action, "_Cancel",
	                                           GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);

	if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT) {
		char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));

		if (f) {
			gtk_entry_set_text(entry, f);
			g_free(f);
		}
	}
	gtk_widget_destroy(d);
}

static void on_pick_path(GtkButton *b, gpointer e)   { (void)b; pick_into(e, GTK_FILE_CHOOSER_ACTION_OPEN, "The theme file"); }
static void on_pick_folder(GtkButton *b, gpointer e) { (void)b; pick_into(e, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "The theme directory"); }

/* refills the set combo after a set was added, selecting cfg.set */
static void
refill_sets(void)
{
	char names[MAX_LIST][NAME_LEN];
	int n = template_all_sets(names, MAX_LIST);

	updating++;
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(set_combo));
	for (int i = 0; i < n; i++) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(set_combo), names[i]);
		if (!strcmp(names[i], cfg.set))
			gtk_combo_box_set_active(GTK_COMBO_BOX(set_combo), i);
	}
	updating--;
}

/* a report (the output of set check, import, or copy) in a dialog */
static void
show_report(const char *title, const char *text)
{
	GtkWidget *d = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window), GTK_DIALOG_MODAL,
	                                           "_Close", GTK_RESPONSE_CLOSE, NULL);
	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL), *view = gtk_text_view_new();

	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text, -1);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
	gtk_container_add(GTK_CONTAINER(scroll), view);
	gtk_widget_set_size_request(scroll, 640, 360);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))), scroll, TRUE, TRUE, 0);
	gtk_widget_show_all(d);
	gtk_dialog_run(GTK_DIALOG(d));
	gtk_widget_destroy(d);
}

/* runs fn with its report in memory and shows it; returns fn's result */
static int
with_report(const char *title, int (*fn)(FILE *, void *), void *arg)
{
	char *buf = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&buf, &len);
	int r;

	if (!f)
		return -1;
	r = fn(f, arg);
	fclose(f);
	show_report(title, buf && len ? buf : "(nothing to report)");
	free(buf);
	return r;
}

static int do_check(FILE *f, void *arg) { return set_check(&cfg, arg, f); }
static int do_copy(FILE *f, void *arg) { return front_set_copy(((char **)arg)[0], ((char **)arg)[1], f) < 0 ? -1 : 0; }
static int do_import(FILE *f, void *arg) { char **a = arg; return set_import(&cfg, a[0], a[1], a[2], f); }

/* asks for a set name; returns the entry text in name, 1 if accepted */
static int
ask_name(const char *title, const char *prompt, char *name, size_t n)
{
	GtkWidget *d = gtk_dialog_new_with_buttons(title, GTK_WINDOW(window), GTK_DIALOG_MODAL,
	                                           "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, NULL);
	GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(d)), *entry = gtk_entry_new();
	int ok;

	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_box_set_spacing(GTK_BOX(box), 6);
	gtk_box_pack_start(GTK_BOX(box), caption(prompt), FALSE, FALSE, 0);
	gtk_entry_set_text(GTK_ENTRY(entry), name);
	gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	gtk_widget_show_all(d);
	ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
	if (ok)
		snprintf(name, n, "%s", gtk_entry_get_text(GTK_ENTRY(entry)));
	gtk_widget_destroy(d);
	return ok && name[0];
}

static void
on_set_new(GtkButton *b, gpointer data)
{
	char name[NAME_LEN] = "";
	const char *args[2];

	(void)b;
	(void)data;
	if (!ask_name("New template set", "A copy of the current set under this name, in ~/.config/themer/templates/, "
	              "to edit. Letters, digits, - and _.", name, sizeof name))
		return;
	args[0] = cfg.set;
	args[1] = name;
	if (with_report("New set", do_copy, args) < 0) {
		set_status("could not copy the set; the name may be taken or not valid");
		return;
	}
	if (front_set_set(&cfg, name) == 0) {
		refill_sets();
		changed();
	}
}

static void
on_set_import(GtkButton *b, gpointer data)
{
	GtkWidget *d, *box, *grid, *name_entry, *module_combo, *path_entry, *hb, *fb, *db;
	char name[NAME_LEN], module[NAME_LEN], path[PATH_MAX];
	const char *args[3];
	int r;

	(void)b;
	(void)data;
	d = gtk_dialog_new_with_buttons("Import a theme into a set", GTK_WINDOW(window), GTK_DIALOG_MODAL,
	                                "_Cancel", GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_OK, NULL);
	box = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	gtk_box_set_spacing(GTK_BOX(box), 8);
	gtk_box_pack_start(GTK_BOX(box), caption("The theme's files go under the module's names in the set, "
	                   "with every color that matches the current theme turned into a variable, so "
	                   "it follows the color theme. Import with the theme and mode the look was made for."),
	                   FALSE, FALSE, 0);
	grid = grid_new();
	name_entry = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(name_entry), strcmp(cfg.set, TM_DEFAULT_SET) ? cfg.set : "");
	gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "new or existing set name");
	grid_row(grid, 0, "Set", name_entry);
	module_combo = gtk_combo_box_text_new();
	for (int i = 0; i < themer_module_count(); i++)
		if (themer_module_templates(i))
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(module_combo), themer_module_name(i));
	gtk_combo_box_set_active(GTK_COMBO_BOX(module_combo), 0);
	grid_row(grid, 1, "Module", module_combo);
	hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	path_entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(path_entry), "a theme directory or file");
	fb = gtk_button_new_with_label("File\u2026");
	db = gtk_button_new_with_label("Folder\u2026");
	gtk_box_pack_start(GTK_BOX(hb), path_entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hb), fb, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hb), db, FALSE, FALSE, 0);
	g_signal_connect(fb, "clicked", G_CALLBACK(on_pick_path), path_entry);
	g_signal_connect(db, "clicked", G_CALLBACK(on_pick_folder), path_entry);
	grid_row(grid, 2, "Theme", hb);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	gtk_widget_show_all(d);
	r = gtk_dialog_run(GTK_DIALOG(d));
	snprintf(name, sizeof name, "%s", gtk_entry_get_text(GTK_ENTRY(name_entry)));
	snprintf(module, sizeof module, "%s", gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(module_combo))
	                                      ? gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(module_combo)) : "");
	snprintf(path, sizeof path, "%s", gtk_entry_get_text(GTK_ENTRY(path_entry)));
	gtk_widget_destroy(d);
	if (r != GTK_RESPONSE_OK || !name[0] || !path[0])
		return;
	args[0] = name;
	args[1] = module;
	args[2] = path;
	if (with_report("Import", do_import, args) < 0) {
		set_status("import failed; see the report");
		return;
	}
	if (front_set_set(&cfg, name) == 0) {
		refill_sets();
		changed();
	}
}

static void
on_set_check(GtkButton *b, gpointer data)
{
	(void)b;
	(void)data;
	with_report("Set check", do_check, cfg.set);
}

static GtkWidget *
page_theme(void)
{
	GtkWidget *box = page_box(), *scroll, *hbox, *grid;
	char names[MAX_LIST][NAME_LEN];
	int n, i;

	gtk_box_pack_start(GTK_BOX(box), heading("Color theme"), FALSE, FALSE, 0);
	theme_box = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(theme_box), GTK_SELECTION_BROWSE);
	ntheme_names = theme_list(theme_names, MAX_LIST);
	for (i = 0; i < ntheme_names; i++) {
		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8), *l, *sw;

		theme_read(theme_names[i], &themes[i]);
		sw = gtk_drawing_area_new();
		gtk_widget_set_size_request(sw, 40, 18);
		gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
		g_signal_connect(sw, "draw", G_CALLBACK(draw_theme_swatch), GINT_TO_POINTER(i));
		l = gtk_label_new(theme_names[i]);
		gtk_widget_set_halign(l, GTK_ALIGN_START);
		gtk_box_pack_start(GTK_BOX(row), sw, FALSE, FALSE, 4);
		gtk_box_pack_start(GTK_BOX(row), l, TRUE, TRUE, 0);
		gtk_container_set_border_width(GTK_CONTAINER(row), 3);
		gtk_list_box_insert(GTK_LIST_BOX(theme_box), row, -1);
		if (!strcmp(theme_names[i], cfg.th.name))
			gtk_list_box_select_row(GTK_LIST_BOX(theme_box),
			                        gtk_list_box_get_row_at_index(GTK_LIST_BOX(theme_box), i));
	}
	g_signal_connect(theme_box, "row-selected", G_CALLBACK(on_theme_selected), NULL);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_widget_set_size_request(scroll, -1, 200);
	gtk_container_add(GTK_CONTAINER(scroll), theme_box);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

	grid = grid_new();
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	mode_dark = gtk_radio_button_new_with_label(NULL, "Dark");
	mode_light = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(mode_dark), "Light");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cfg.mode == MODE_DARK ? mode_dark : mode_light), TRUE);
	g_signal_connect(mode_dark, "toggled", G_CALLBACK(on_mode), GINT_TO_POINTER(MODE_DARK));
	g_signal_connect(mode_light, "toggled", G_CALLBACK(on_mode), GINT_TO_POINTER(MODE_LIGHT));
	gtk_box_pack_start(GTK_BOX(hbox), mode_dark, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), mode_light, FALSE, FALSE, 0);
	grid_row(grid, 0, "Mode", hbox);

	set_combo = gtk_combo_box_text_new();
	n = template_all_sets(names, MAX_LIST);
	for (i = 0; i < n; i++) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(set_combo), names[i]);
		if (!strcmp(names[i], cfg.set))
			gtk_combo_box_set_active(GTK_COMBO_BOX(set_combo), i);
	}
	g_signal_connect(set_combo, "changed", G_CALLBACK(on_combo_text), cfg.set);
	grid_row(grid, 1, "Template set", set_combo);
	{
		/* new (a copy to edit), import (an existing theme), check (render it all) */
		GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *b;

		b = gtk_button_new_with_mnemonic("_New set\u2026");
		gtk_widget_set_tooltip_text(b, "a copy of the selected set under a new name, in ~/.config/themer/templates, to edit");
		g_signal_connect(b, "clicked", G_CALLBACK(on_set_new), NULL);
		gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
		b = gtk_button_new_with_mnemonic("_Import theme\u2026");
		gtk_widget_set_tooltip_text(b, "turn a theme you have (a GTK theme directory, a .qss, a chrome/ directory...) into part of a set");
		g_signal_connect(b, "clicked", G_CALLBACK(on_set_import), NULL);
		gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
		b = gtk_button_new_with_mnemonic("Chec_k");
		gtk_widget_set_tooltip_text(b, "render every file of the selected set for every theme, both modes");
		g_signal_connect(b, "clicked", G_CALLBACK(on_set_check), NULL);
		gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
		gtk_grid_attach(GTK_GRID(grid), bar, 1, 2, 1, 1);
	}
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), caption("A template set is the look: the widgets, bars, and "
	                                        "chrome every module draws. What a set lacks comes "
	                                        "from themer_chicago."), FALSE, FALSE, 0);
	return box;
}

static GtkWidget *
page_fonts(void)
{
	GtkWidget *box = page_box(), *grid;
	int i;

	gtk_box_pack_start(GTK_BOX(box), heading("Fonts"), FALSE, FALSE, 0);
	grid = grid_new();
	ui_font = gtk_button_new();
	font_button_label(ui_font, cfg.ui_font, cfg.ui_size);
	g_signal_connect(ui_font, "clicked", G_CALLBACK(on_font_button), GINT_TO_POINTER(0));
	grid_row(grid, 0, "Interface", ui_font);
	mono_font = gtk_button_new();
	font_button_label(mono_font, cfg.mono_font, cfg.mono_size);
	g_signal_connect(mono_font, "clicked", G_CALLBACK(on_font_button), GINT_TO_POINTER(1));
	grid_row(grid, 1, "Terminal", mono_font);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), caption("The interface font goes to GTK, Qt, the window "
	                                        "manager, and the browsers; the terminal font to "
	                                        "the terminals, editors, and Qt's fixed font."), FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(box), heading("Rendering"), FALSE, FALSE, 0);
	grid = grid_new();
	hint_combo = gtk_combo_box_text_new();
	for (i = 0; i < 4; i++) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(hint_combo), hinting_values[i]);
		if (!strcmp(hinting_values[i], cfg.hinting))
			gtk_combo_box_set_active(GTK_COMBO_BOX(hint_combo), i);
	}
	g_signal_connect(hint_combo, "changed", G_CALLBACK(on_hinting), NULL);
	grid_row(grid, 0, "Hinting", hint_combo);
	aa_check = gtk_check_button_new_with_label("Antialiasing");
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(aa_check), cfg.antialias);
	g_signal_connect(aa_check, "toggled", G_CALLBACK(on_antialias), NULL);
	grid_row(grid, 1, "", aa_check);
	rgba_combo = gtk_combo_box_text_new();
	for (i = 0; i < 5; i++) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(rgba_combo), rgba_values[i]);
		if (!strcmp(rgba_values[i], cfg.rgba))
			gtk_combo_box_set_active(GTK_COMBO_BOX(rgba_combo), i);
	}
	g_signal_connect(rgba_combo, "changed", G_CALLBACK(on_rgba), NULL);
	grid_row(grid, 2, "Subpixel order", rgba_combo);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), caption("Written to the GTK settings, xsettingsd, gsettings, "
	                                        "and fontconfig, so every toolkit draws text the same way. "
	                                        "This window follows as you change them."), FALSE, FALSE, 0);
	return box;
}

static GtkWidget *
page_icons(void)
{
	GtkWidget *box = page_box(), *frame;
	char names[MAX_LIST][NAME_LEN];
	int n = icon_themes_scan(names, MAX_LIST, 0);

	gtk_box_pack_start(GTK_BOX(box), heading("Icon theme"), FALSE, FALSE, 0);
	icon_combo = name_combo(names, n, cfg.icons);
	g_signal_connect(icon_combo, "changed", G_CALLBACK(on_combo_text), cfg.icons);
	gtk_box_pack_start(GTK_BOX(box), icon_combo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), caption("Set for GTK, Qt (qt5ct and qt6ct), xsettingsd, and "
	                                        "gsettings. Firefox's toolbar follows it too when the "
	                                        "firefox module's icons option is on."), FALSE, FALSE, 0);
	frame = gtk_frame_new(NULL);
	icon_grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(icon_grid), 10);
	gtk_grid_set_column_spacing(GTK_GRID(icon_grid), 6);
	gtk_grid_set_column_homogeneous(GTK_GRID(icon_grid), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(icon_grid), 10);
	gtk_container_add(GTK_CONTAINER(frame), icon_grid);
	gtk_widget_set_margin_top(frame, 6);
	gtk_box_pack_start(GTK_BOX(box), frame, FALSE, FALSE, 0);
	fill_icon_grid();
	return box;
}

static GtkWidget *
page_cursor(void)
{
	GtkWidget *box = page_box(), *grid;
	char names[MAX_LIST][NAME_LEN];
	int n = icon_themes_scan(names, MAX_LIST, 1);

	gtk_box_pack_start(GTK_BOX(box), heading("Mouse cursor"), FALSE, FALSE, 0);
	grid = grid_new();
	cursor_combo = name_combo(names, n, cfg.cursor);
	g_signal_connect(cursor_combo, "changed", G_CALLBACK(on_combo_text), cfg.cursor);
	grid_row(grid, 0, "Theme", cursor_combo);
	cursor_size = gtk_spin_button_new_with_range(8, 128, 2);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(cursor_size), cfg.cursor_size);
	g_signal_connect(cursor_size, "value-changed", G_CALLBACK(on_cursor_size), NULL);
	grid_row(grid, 1, "Size", cursor_size);
	gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), caption("Applied through the cursor module (the Xcursor "
	                                        "default theme, XCURSOR_THEME and XCURSOR_SIZE, "
	                                        "Xresources), the GTK settings, gsettings, and sway's "
	                                        "seat. Drawn below at the chosen size."), FALSE, FALSE, 0);
	cursor_area = gtk_drawing_area_new();
	gtk_widget_set_size_request(cursor_area, -1, 120);
	gtk_widget_set_margin_top(cursor_area, 6);
	g_signal_connect(cursor_area, "draw", G_CALLBACK(draw_cursors), NULL);
	gtk_box_pack_start(GTK_BOX(box), cursor_area, FALSE, FALSE, 0);
	return box;
}

/* ---- module options ---- */

typedef struct { int module, option; GtkWidget *control; } OptRef;

/* the value a control holds, as text */
static const char *
option_control_value(const OptRef *o, char *buf, size_t n)
{
	if (GTK_IS_COMBO_BOX_TEXT(o->control))
		snprintf(buf, n, "%s", gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(o->control))
		                       ? gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(o->control)) : "");
	else if (GTK_IS_SPIN_BUTTON(o->control))
		snprintf(buf, n, "%d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(o->control)));
	else
		snprintf(buf, n, "%s", gtk_entry_get_text(GTK_ENTRY(o->control)));
	return buf;
}

static void on_option_changed(GtkWidget *w, gpointer data);

static gboolean
on_option_focus_out(GtkWidget *w, GdkEvent *e, gpointer data)
{
	(void)e;
	on_option_changed(w, data);
	return FALSE;
}

static void
on_option_changed(GtkWidget *w, gpointer data)
{
	OptRef *o = data;
	char value[NAME_LEN], msg[256];

	(void)w;
	if (updating)
		return;
	option_control_value(o, value, sizeof value);
	if (!strcmp(value, themer_option_value(&cfg, o->module, o->option)))
		return;
	if (front_option_set(&cfg, o->module, o->option, value, msg, sizeof msg) < 0)
		set_status(msg);
	else
		set_status(msg);
	restyle();
}

/* a control for one option: a combo for choices, a spin button for a range, an entry
 * otherwise; the template option lists the sets */
static GtkWidget *
option_control(int module, int option)
{
	const TmOption *o = themer_option(module, option);
	const char *cur = themer_option_value(&cfg, module, option);
	GtkWidget *w;
	OptRef *ref = g_new0(OptRef, 1);

	ref->module = module;
	ref->option = option;
	if (o->choices) {
		const char *p = o->choices;
		int i = 0;

		w = gtk_combo_box_text_new();
		while (*p) {
			const char *bar = strchr(p, '|');
			size_t len = bar ? (size_t)(bar - p) : strlen(p);
			char *item = g_strndup(p, len);

			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), item);
			if (!strcmp(item, cur))
				gtk_combo_box_set_active(GTK_COMBO_BOX(w), i);
			g_free(item);
			i++;
			if (!bar)
				break;
			p = bar + 1;
		}
		g_signal_connect(w, "changed", G_CALLBACK(on_option_changed), ref);
	} else if (o->max > o->min) {
		w = gtk_spin_button_new_with_range(o->min, o->max, 1);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), atoi(cur));
		g_signal_connect(w, "value-changed", G_CALLBACK(on_option_changed), ref);
	} else if (!strcmp(o->name, "template")) {
		char names[MAX_LIST][NAME_LEN];
		int n = template_all_sets(names, MAX_LIST);

		w = gtk_combo_box_text_new();
		for (int i = 0; i < n; i++) {
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), names[i]);
			if (!strcmp(names[i], cur))
				gtk_combo_box_set_active(GTK_COMBO_BOX(w), i);
		}
		g_signal_connect(w, "changed", G_CALLBACK(on_option_changed), ref);
	} else {
		w = gtk_entry_new();
		gtk_entry_set_text(GTK_ENTRY(w), cur);
		g_signal_connect(w, "activate", G_CALLBACK(on_option_changed), ref);
		g_signal_connect(w, "focus-out-event", G_CALLBACK(on_option_focus_out), ref);
	}
	ref->control = w;
	gtk_widget_set_tooltip_text(w, o->desc);
	return w;
}

/* the options of one module as a grid: name, control, description */
static GtkWidget *
options_grid(int module)
{
	GtkWidget *grid = grid_new();
	int n = themer_option_count(module);

	gtk_widget_set_margin_start(grid, 12);
	gtk_widget_set_margin_top(grid, 4);
	gtk_widget_set_margin_bottom(grid, 6);
	for (int j = 0; j < n; j++) {
		GtkWidget *l = gtk_label_new(themer_option(module, j)->name), *d = caption(themer_option(module, j)->desc);

		gtk_widget_set_halign(l, GTK_ALIGN_END);
		gtk_grid_attach(GTK_GRID(grid), l, 0, j, 1, 1);
		gtk_grid_attach(GTK_GRID(grid), option_control(module, j), 1, j, 1, 1);
		gtk_widget_set_margin_start(d, 6);
		gtk_grid_attach(GTK_GRID(grid), d, 2, j, 1, 1);
	}
	return grid;
}

static GtkWidget *
page_modules(void)
{
	GtkWidget *box = page_box(), *scroll, *list;
	int i;

	gtk_box_pack_start(GTK_BOX(box), heading("Modules"), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), caption("Each module writes one program's settings on Apply "
	                                        "and removes them when turned off. Open a module for "
	                                        "its options, which take effect at once; \"template\" "
	                                        "pins it to a set of its own (the global set otherwise)."),
	                   FALSE, FALSE, 0);
	list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
	modules_box = list;
	nmodule_order = front_module_order(module_order, MAX_MODULES);
	for (int k = 0; k < nmodule_order; k++) {
		GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10), *text, *name, *desc, *sw;
		GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
		int no;

		i = module_order[k];
		text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		name = gtk_label_new(themer_module_name(i));
		gtk_widget_set_halign(name, GTK_ALIGN_START);
		desc = caption(themer_module_desc(i));
		if (!themer_module_present(&cfg, i)) {
			gtk_label_set_text(GTK_LABEL(name), g_strdup_printf("%s  (not installed)", themer_module_name(i)));
			gtk_widget_set_sensitive(desc, FALSE);
		}
		gtk_box_pack_start(GTK_BOX(text), name, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(text), desc, FALSE, FALSE, 0);
		sw = gtk_switch_new();
		gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
		gtk_switch_set_active(GTK_SWITCH(sw), themer_module_on(&cfg, i));
		g_signal_connect(sw, "notify::active", G_CALLBACK(on_module), GINT_TO_POINTER(i));
		module_switch[i] = sw;
		gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);
		gtk_box_pack_end(GTK_BOX(row), sw, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(outer), row, FALSE, FALSE, 0);
		/* the options, under an expander */
		no = themer_option_count(i);
		if (no) {
			GtkWidget *exp = gtk_expander_new("options");

			gtk_container_add(GTK_CONTAINER(exp), options_grid(i));
			gtk_box_pack_start(GTK_BOX(outer), exp, FALSE, FALSE, 0);
		}
		gtk_container_set_border_width(GTK_CONTAINER(outer), 6);
		gtk_list_box_insert(GTK_LIST_BOX(list), outer, -1);
	}
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scroll), list);
	gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
	return box;
}

/* pushes cfg into every control (after a revert) */
static void
set_controls_from_cfg(void)
{
	int i;

	for (i = 0; i < ntheme_names; i++)
		if (!strcmp(theme_names[i], cfg.th.name))
			gtk_list_box_select_row(GTK_LIST_BOX(theme_box),
			                        gtk_list_box_get_row_at_index(GTK_LIST_BOX(theme_box), i));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cfg.mode == MODE_DARK ? mode_dark : mode_light), TRUE);
	font_button_label(ui_font, cfg.ui_font, cfg.ui_size);
	font_button_label(mono_font, cfg.mono_font, cfg.mono_size);
	gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(icon_combo))), cfg.icons);
	gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(cursor_combo))), cfg.cursor);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(cursor_size), cfg.cursor_size);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(aa_check), cfg.antialias);
	for (i = 0; i < 4; i++)
		if (!strcmp(hinting_values[i], cfg.hinting))
			gtk_combo_box_set_active(GTK_COMBO_BOX(hint_combo), i);
	for (i = 0; i < 5; i++)
		if (!strcmp(rgba_values[i], cfg.rgba))
			gtk_combo_box_set_active(GTK_COMBO_BOX(rgba_combo), i);
	for (i = 0; i < themer_module_count(); i++)
		if (module_switch[i])
			gtk_switch_set_active(GTK_SWITCH(module_switch[i]), themer_module_on(&cfg, i));
}

/* ordinary widgets, so the GTK theme shows on the real thing */
static GtkWidget *
build_preview(void)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8), *notebook, *page, *hbox, *w;
	GtkWidget *tree, *scroll;
	GtkListStore *store;
	GtkTreeIter it;
	const char *rows[][2] = { { "Inbox", "12" }, { "Drafts", "1" }, { "Sent", "" }, { "Archive", "" } };
	int i;

	gtk_container_set_border_width(GTK_CONTAINER(box), 12);
	title_label = gtk_label_new("");
	gtk_widget_set_halign(title_label, GTK_ALIGN_START);
	gtk_style_context_add_class(gtk_widget_get_style_context(title_label), "dim-label");
	gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);

	notebook = gtk_notebook_new();
	for (i = 0; i < 3; i++) {
		static const char *tabs[] = { "Widgets", "List", "Terminal" };

		page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
		gtk_container_set_border_width(GTK_CONTAINER(page), 10);
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, gtk_label_new(tabs[i]));
		if (i == 0) {
			GtkWidget *menubar, *menu, *item;

			/* a menu bar, with a menu to open */
			menubar = gtk_menu_bar_new();
			{
				static const char *menus[] = { "File", "Edit", "View", "Help" };
				static const char *items[] = { "New", "Open...", "Save", NULL, "Quit" };
				size_t m, k;

				for (m = 0; m < sizeof menus / sizeof menus[0]; m++) {
					item = gtk_menu_item_new_with_label(menus[m]);
					menu = gtk_menu_new();
					for (k = 0; k < sizeof items / sizeof items[0]; k++)
						gtk_menu_shell_append(GTK_MENU_SHELL(menu), items[k]
							? gtk_menu_item_new_with_label(items[k]) : gtk_separator_menu_item_new());
					gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
					gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item);
				}
			}
			gtk_box_pack_start(GTK_BOX(page), menubar, FALSE, FALSE, 0);
			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
			gtk_box_pack_start(GTK_BOX(hbox), gtk_button_new_with_label("Button"), FALSE, FALSE, 0);
			w = gtk_toggle_button_new_with_label("Toggled");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
			gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
			w = gtk_button_new_with_label("Disabled");
			gtk_widget_set_sensitive(w, FALSE);
			gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
			w = gtk_button_new_from_icon_name("document-open", GTK_ICON_SIZE_BUTTON);
			gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
			w = gtk_switch_new();
			gtk_switch_set_active(GTK_SWITCH(w), TRUE);
			gtk_box_pack_end(GTK_BOX(hbox), w, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, FALSE, 0);
			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
			w = gtk_entry_new();
			gtk_entry_set_text(GTK_ENTRY(w), "a text field");
			gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
			w = gtk_spin_button_new_with_range(0, 100, 1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), 42);
			gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, FALSE, 0);
			hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
			w = gtk_check_button_new_with_label("checked");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
			gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(hbox), gtk_check_button_new_with_label("unchecked"), FALSE, FALSE, 0);
			w = gtk_radio_button_new_with_label(NULL, "radio");
			gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(hbox), gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w), "other"), FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(page), hbox, FALSE, FALSE, 0);
			w = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
			gtk_range_set_value(GTK_RANGE(w), 40);
			gtk_box_pack_start(GTK_BOX(page), w, FALSE, FALSE, 0);
			w = gtk_progress_bar_new();
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w), 0.6);
			gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(w), TRUE);
			gtk_box_pack_start(GTK_BOX(page), w, FALSE, FALSE, 0);
			w = gtk_combo_box_text_new();
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), "a combo box");
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w), "another choice");
			gtk_combo_box_set_active(GTK_COMBO_BOX(w), 0);
			gtk_box_pack_start(GTK_BOX(page), w, FALSE, FALSE, 0);
			w = gtk_text_view_new();
			gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)),
			                         "A text view. The quick brown fox jumps over the lazy dog.\n"
			                         "0123456789 \xe2\x80\x94 how the interface font renders.", -1);
			gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w), GTK_WRAP_WORD);
			scroll = gtk_scrolled_window_new(NULL, NULL);
			gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
			gtk_container_add(GTK_CONTAINER(scroll), w);
			gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
		} else if (i == 1) {
			store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
			for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
				gtk_list_store_append(store, &it);
				gtk_list_store_set(store, &it, 0, rows[r][0], 1, rows[r][1], -1);
			}
			tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
			gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
				gtk_tree_view_column_new_with_attributes("Folder", gtk_cell_renderer_text_new(), "text", 0, NULL));
			gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
				gtk_tree_view_column_new_with_attributes("Unread", gtk_cell_renderer_text_new(), "text", 1, NULL));
			gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)), gtk_tree_path_new_first());
			scroll = gtk_scrolled_window_new(NULL, NULL);
			gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
			gtk_container_add(GTK_CONTAINER(scroll), tree);
			gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
		} else {
			sample = gtk_drawing_area_new();
			g_signal_connect(sample, "draw", G_CALLBACK(draw_sample), NULL);
			gtk_box_pack_start(GTK_BOX(page), sample, TRUE, TRUE, 0);
		}
	}
	gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

	swatches = gtk_drawing_area_new();
	gtk_widget_set_size_request(swatches, -1, 80);
	g_signal_connect(swatches, "draw", G_CALLBACK(draw_swatches), NULL);
	gtk_box_pack_start(GTK_BOX(box), swatches, FALSE, FALSE, 0);
	return box;
}

/* File: Apply, Revert, Quit. Help: About. */
static GtkWidget *
build_menubar(void)
{
	GtkWidget *bar = gtk_menu_bar_new(), *menu, *item;
	GtkAccelGroup *accel = gtk_accel_group_new();

	gtk_window_add_accel_group(GTK_WINDOW(window), accel);
	item = gtk_menu_item_new_with_mnemonic("_File");
	menu = gtk_menu_new();
	gtk_menu_set_accel_group(GTK_MENU(menu), accel);
	{
		GtkWidget *a = gtk_menu_item_new_with_mnemonic("_Apply");
		GtkWidget *r = gtk_menu_item_new_with_mnemonic("_Revert to applied");
		GtkWidget *q = gtk_menu_item_new_with_mnemonic("_Quit");

		gtk_widget_add_accelerator(a, "activate", accel, GDK_KEY_s, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		gtk_widget_add_accelerator(q, "activate", accel, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
		g_signal_connect(a, "activate", G_CALLBACK(on_apply), NULL);
		g_signal_connect(r, "activate", G_CALLBACK(on_revert), NULL);
		g_signal_connect_swapped(q, "activate", G_CALLBACK(gtk_window_close), window);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), a);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), r);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), q);
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);

	item = gtk_menu_item_new_with_mnemonic("_Help");
	menu = gtk_menu_new();
	{
		GtkWidget *about = gtk_menu_item_new_with_mnemonic("_About themer");

		g_signal_connect(about, "activate", G_CALLBACK(on_about), NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), about);
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
	return bar;
}

int
main(int argc, char *argv[])
{
	GtkWidget *vbox, *paned, *stack, *sidebar, *side, *actions;

	gtk_init(&argc, &argv);
	if (themer_startup(&cfg) < 0)
		return 1;
	applied = cfg;

	provider = gtk_css_provider_new();
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
	                                          GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "themer");
	gtk_window_set_default_size(GTK_WINDOW(window), 980, 640);
	gtk_window_set_icon_name(GTK_WINDOW(window), "preferences-desktop-theme");
	g_signal_connect(window, "delete-event", G_CALLBACK(on_delete), NULL);
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(vbox), build_menubar(), FALSE, FALSE, 0);

	/* the settings: a sidebar of pages */
	updating = 1;
	stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
	gtk_stack_add_titled(GTK_STACK(stack), page_theme(), "theme", "Theme");
	gtk_stack_add_titled(GTK_STACK(stack), page_fonts(), "fonts", "Fonts");
	gtk_stack_add_titled(GTK_STACK(stack), page_icons(), "icons", "Icons");
	gtk_stack_add_titled(GTK_STACK(stack), page_cursor(), "cursor", "Cursor");
	gtk_stack_add_titled(GTK_STACK(stack), page_modules(), "modules", "Modules");
	sidebar = gtk_stack_sidebar_new();
	gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(stack));
	gtk_widget_set_size_request(sidebar, 130, -1);
	side = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(side), sidebar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(side), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
	{
		GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);

		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
		gtk_widget_set_size_request(scroll, 360, -1);
		gtk_container_add(GTK_CONTAINER(scroll), stack);
		gtk_box_pack_start(GTK_BOX(side), scroll, TRUE, TRUE, 0);
	}

	paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED(paned), side, FALSE, FALSE);
	gtk_paned_pack2(GTK_PANED(paned), build_preview(), TRUE, FALSE);
	gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

	/* the action bar: status on the left, Apply on the right */
	actions = gtk_action_bar_new();
	status = gtk_label_new("up to date");
	gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_END);
	gtk_action_bar_pack_start(GTK_ACTION_BAR(actions), status);
	apply_button = gtk_button_new_with_mnemonic("_Apply");
	gtk_style_context_add_class(gtk_widget_get_style_context(apply_button), "suggested-action");
	gtk_widget_set_sensitive(apply_button, FALSE);
	g_signal_connect(apply_button, "clicked", G_CALLBACK(on_apply), NULL);
	gtk_action_bar_pack_end(GTK_ACTION_BAR(actions), apply_button);
	gtk_box_pack_start(GTK_BOX(vbox), actions, FALSE, FALSE, 0);
	updating = 0;

	gtk_container_add(GTK_CONTAINER(window), vbox);
	restyle();
	gtk_widget_show_all(window);
	/* themer-gtk [theme|fonts|icons|cursor|modules] opens on that page */
	if (argc > 1 && gtk_stack_get_child_by_name(GTK_STACK(stack), argv[1]))
		gtk_stack_set_visible_child_name(GTK_STACK(stack), argv[1]);
	gtk_main();
	return 0;
}
