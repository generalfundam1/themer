/* stylus.c: userstyles for web sites, for the Stylus browser extension. The firefox
 * module themes the browser; this one themes the pages, through Stylus, which runs
 * in Firefox and the Chromium family alike.
 *
 * Every <site>.user.css in templates/stylus/<set>/ is rendered into
 * ~/.config/themer/stylus/<site>.user.css, a UserCSS file with its metadata block
 * (@name, @-moz-document ...). Stylus cannot be written to from outside the
 * browser, so the file is installed once by hand: open
 *     file:///home/<you>/.config/themer/stylus/youtube.user.css
 * in the browser, Stylus offers to install it; tick "Live reload" in that dialog
 * and every apply afterwards restyles the site at once, since Stylus re-reads the
 * file when it changes. Chromium needs "Allow access to file URLs" on the Stylus
 * extension for that. The template set ships youtube.user.css; a set adds a site
 * by adding a file, nothing in the module changes.
 *
 * Options:
 *   font      ui or terminal: the site's text in the theme's font; site keeps the
 *             site's own
 *   template  the set under templates/stylus/ */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer_module.h"

#define DIR_NAME "stylus"
#define MAX_SITES 32

static const TmOption opts[] = {
	{ "font", "the site's text: ui or terminal font from the theme, or the site's own", "ui|terminal|site", "ui", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* the module's own variables: font_family and font_size for the chosen font, and
 * font_rule, a complete declaration or nothing when the site keeps its own */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size_s[16], rule[256];
	const char *font = tm_opt(t, "stylus.font");
	int size;

	if (max < 3)
		return 0;
	tm_font_for(t, "stylus.font", fam, sizeof fam, &size);
	snprintf(size_s, sizeof size_s, "%d", size);
	rule[0] = '\0';
	if (!font || strcmp(font, "site"))
		snprintf(rule, sizeof rule, "font-family: \"%s\", sans-serif !important;", fam);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size_s };
	v[2] = (TmVar){ "font_rule", rule };
	return 3;
}

TM_VARS(template_vars)

/* the sites of a set: every *.user.css under templates/stylus/<set>/, plus those of
 * the built-in set, so a set that adds one site still gets the rest */
static int
sites(const TmTheme *t, char names[][64], int max)
{
	const char *sets[2] = { tm_opt(t, "stylus.template"), TM_DEFAULT_SET };
	int n = 0;

	for (int s = 0; s < 2 && n < max; s++) {
		char dir[PATH_MAX];
		DIR *d;
		struct dirent *e;

		if (!sets[s] || !*sets[s] || tm_template_dir(DIR_NAME, sets[s], dir, sizeof dir) < 0)
			continue;
		if (!(d = opendir(dir)))
			continue;
		while ((e = readdir(d)) && n < max) {
			size_t len = strlen(e->d_name);
			int dup = 0;

			if (len < 10 || len >= 64 || strcmp(e->d_name + len - 9, ".user.css"))
				continue;
			for (int i = 0; i < n; i++)
				if (!strcmp(names[i], e->d_name))
					dup = 1;
			if (!dup)
				memcpy(names[n++], e->d_name, len + 1);
		}
		closedir(d);
	}
	return n;
}

static int
present(const TmTheme *t)
{
	static const char *const browsers[] = {
		"firefox", "librewolf", "zen", "floorp", "chromium", "chromium-browser",
		"google-chrome", "google-chrome-stable", "brave", "brave-browser", "vivaldi"
	};
	char p[PATH_MAX];

	for (size_t i = 0; i < TM_COUNT(browsers); i++)
		if (tm_have(browsers[i]))
			return 1;
	tm_path(p, sizeof p, t, ".mozilla");
	return !access(p, F_OK);
}

static int
load(const TmTheme *t)
{
	static char body[65536];
	char names[MAX_SITES][64], p[PATH_MAX], rel[PATH_MAX];
	TmVar v[3];
	int nv = template_vars(t, v, TM_COUNT(v));
	int n = sites(t, names, MAX_SITES);

	for (int i = 0; i < n; i++) {
		if (tm_render(body, sizeof body, DIR_NAME, tm_opt(t, "stylus.template"), names[i],
		              t, v, nv) < 0)
			return -1;
		snprintf(rel, sizeof rel, ".config/themer/stylus/%s", names[i]);
		tm_path(p, sizeof p, t, rel);
		if (tm_writef(p, "%s", body) < 0)
			return -1;
	}
	if (n)
		fprintf(stderr, "themer: stylus: %d userstyle%s in ~/.config/themer/stylus/ "
		                "(install once from file://, with Live reload on)\n", n, n == 1 ? "" : "s");
	return 0;
}

static const TmModule mod = {
	.name = "stylus",
	.desc = "userstyles for web sites (YouTube) through the Stylus extension: ~/.config/themer/stylus/",
	.templates = DIR_NAME,
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
