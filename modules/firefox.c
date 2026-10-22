/* firefox.c: browser chrome in the profile in use, as marked blocks at the end of
 * chrome/userChrome.css (the chrome: tabs, bars, menus, the Library) and
 * chrome/userContent.css (Firefox's own pages: settings, add-ons, the new tab).
 * Your own rules stay as they are; unloading cuts the blocks out again.
 *
 * The rules come from templates/firefox/<set>/userChrome.css and userContent.css.
 * This module finds the profile, supplies the tab font and sizes, and embeds the
 * toolbar icons (icons.h), as images recolored to the bar ink, or, with the style
 * option set to mask, as masks over the ink: every icon's shape in the bar's color,
 * whatever its own artwork. The style contract in themer_module.h applies: tabs are sway/i3 tabs, the
 * selected tab takes the accent, every edge is a 1px outline.
 *
 * Firefox reads userChrome.css only when toolkit.legacyUserProfileCustomizations.stylesheets
 * is true; set that once in about:config. The module does not touch user.js. */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"
#include "icons.h"
#include "mozprofile.h"

static const TmOption opts[] = {
	{ "font", "font for the Firefox interface: tabs, URL bar, bookmarks, menus", "ui|terminal", "ui", 0, 0 },
	{ "icons", "icon glyph size, in points above the text size", NULL, "2", 0, 8 },
	{ "recolor", "icons in the bar's ink: all (every SVG; bitmaps keep their colors) or symbolic only", "all|symbolic", "all", 0, 0 },
	{ "raster", "bitmap icons (PNG): color keeps their own colors; ink draws their shape in the bar ink", "color|ink", "color", 0, 0 },
	{ "style", "image: the theme's icons, recolored; mask: every icon's shape as a mask over the bar ink", "image|mask", "image", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* the icon glyph size option, clamped to 0..8 points above the text size */
static int
icons_of(const TmTheme *t)
{
	const char *v = tm_opt(t, "firefox.icons");
	char *end;
	long k = v && *v ? strtol(v, &end, 10) : 2;

	if (!(v && *v && !*end))
		return 2;
	if (k < 0)
		return 0;
	if (k > 8)
		return 8;
	return (int)k;
}

#define NVARS (3 + 5 * NGLYPH)   /* tab_font, tab_size, glyph_size, then five per glyph */

/* the module's own template variables: the tab and URL font, its size, the glyph size
 * (text size plus the icon option), and per glyph:
 *   icon_X, icon_X_hover   an SVG icon in the bar's text and hover colors, as a background
 *                          ("none" for a bitmap, which cannot be recolored)
 *   raster_X               the icon as a mask, so it takes a color ("none" when the
 *                          image is used)
 *   tint_X, tint_X_hover   the color the mask is drawn in ("transparent" with an image)
 * An icon's alpha channel is its shape, so the mask draws it as a silhouette in the
 * bar's ink. Bitmaps go that way with raster=ink; every icon does with style=mask. */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], tab_size[16], glyph[16];
	static char shapes[3][NGLYPH][ICON_CAP];   /* icon, hover, mask */
	static char tints[NGLYPH][2][16];          /* normal and hover tint */
	static char names[5 * NGLYPH][40];
	static Hit hits[NGLYPH];
	char bar_text[16], glyph_hover[16];
	const char *recolor = tm_opt(t, "firefox.recolor");
	const int ink_rasters = strcmp(tm_opt(t, "firefox.raster") ? tm_opt(t, "firefox.raster") : "color", "ink") == 0;
	const int masks = strcmp(tm_opt(t, "firefox.style") ? tm_opt(t, "firefox.style") : "image", "mask") == 0;
	int size, k = 3, recolor_all, nn = 0;

	if (max < (int)NVARS)
		return 0;
	tm_font_for(t, "firefox.font", fam, sizeof fam, &size);
	snprintf(tab_size, sizeof tab_size, "%d", size);
	snprintf(glyph, sizeof glyph, "%d", size + icons_of(t));
	v[0] = (TmVar){ "tab_font", fam };
	v[1] = (TmVar){ "tab_size", tab_size };
	v[2] = (TmVar){ "glyph_size", glyph };
	if (tm_shared(t, "bar_text", bar_text, sizeof bar_text) < 0 ||
	    tm_shared(t, "glyph_hover", glyph_hover, sizeof glyph_hover) < 0)
		return -1;
	recolor_all = strcmp(recolor ? recolor : "all", "symbolic") != 0;
	memset(hits, 0, sizeof hits);
	resolve(t, hits);
	for (size_t g = 0; g < NGLYPH; g++) {
		const char *key = glyphs[g].key;
		const int bitmap = hits[g].found && hits[g].rank == 2;

		if (masks || (bitmap && ink_rasters)) {
			snprintf(shapes[0][g], ICON_CAP, "none");
			snprintf(shapes[1][g], ICON_CAP, "none");
			if (icon_css(&hits[g], NULL, 0, shapes[2][g], ICON_CAP) < 0)
				return -1;
			snprintf(tints[g][0], sizeof tints[g][0], "#%.6s", bar_text);
			snprintf(tints[g][1], sizeof tints[g][1], "#%.6s", glyph_hover);
		} else if (bitmap) {
			/* a bitmap in its own colors: the image itself, the same on hover */
			if (icon_css(&hits[g], NULL, 0, shapes[0][g], ICON_CAP) < 0)
				return -1;
			memcpy(shapes[1][g], shapes[0][g], ICON_CAP);
			snprintf(shapes[2][g], ICON_CAP, "none");
			snprintf(tints[g][0], sizeof tints[g][0], "transparent");
			snprintf(tints[g][1], sizeof tints[g][1], "transparent");
		} else {
			if (icon_css(&hits[g], bar_text, recolor_all, shapes[0][g], ICON_CAP) < 0 ||
			    icon_css(&hits[g], glyph_hover, recolor_all, shapes[1][g], ICON_CAP) < 0)
				return -1;
			snprintf(shapes[2][g], ICON_CAP, "none");
			snprintf(tints[g][0], sizeof tints[g][0], "transparent");
			snprintf(tints[g][1], sizeof tints[g][1], "transparent");
		}

		snprintf(names[nn], sizeof names[nn], "icon_%s", key);
		v[k++] = (TmVar){ names[nn++], shapes[0][g] };
		snprintf(names[nn], sizeof names[nn], "icon_%s_hover", key);
		v[k++] = (TmVar){ names[nn++], shapes[1][g] };
		snprintf(names[nn], sizeof names[nn], "raster_%s", key);
		v[k++] = (TmVar){ names[nn++], shapes[2][g] };
		snprintf(names[nn], sizeof names[nn], "tint_%s", key);
		v[k++] = (TmVar){ names[nn++], tints[g][0] };
		snprintf(names[nn], sizeof names[nn], "tint_%s_hover", key);
		v[k++] = (TmVar){ names[nn++], tints[g][1] };
	}
	return k;
}

TM_VARS(template_vars)

/* Where Firefox keeps its profiles: the XDG and classic locations, the snap, and the
 * Flatpak. The one profile in use is chosen from them. */
static const char *const firefox_bases[] = {
	".config/mozilla/firefox",          /* XDG layout, used by current Firefox builds */
	".mozilla/firefox",
	"snap/firefox/common/.mozilla/firefox",
	".var/app/org.mozilla.firefox/.mozilla/firefox",
};

static int
present(const TmTheme *t)
{
	return moz_present(t, firefox_bases, TM_COUNT(firefox_bases));
}

static int
load(const TmTheme *t)
{
	static char chrome[2 * 1024 * 1024], content[256 * 1024];
	char dir[PATH_MAX];
	TmVar v[NVARS];
	const char *set = tm_opt(t, "firefox.template");
	int nv;

	if (!moz_profile(t, "firefox", firefox_bases, TM_COUNT(firefox_bases), dir, sizeof dir))
		return 0;
	nv = template_vars(t, v, TM_COUNT(v));
	if (nv < 0 ||
	    tm_render(chrome, sizeof chrome, "firefox", set, "userChrome.css", t, v, nv) < 0 ||
	    tm_render(content, sizeof content, "firefox", set, "userContent.css", t, v, nv) < 0)
		return -1;
	{
		char setdir[PATH_MAX];
		const char *sd = tm_template_dir("firefox", set, setdir, sizeof setdir) == 0 ? setdir : NULL;

		return moz_write(dir, "firefox", chrome, content, sd);
	}
}

static const TmModule mod = {
	.name = "firefox",
	.desc = "firefox chrome: one flat top bar, tabs above the URL bar (userChrome.css)",
	.templates = "firefox",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
