/* swaync.c: SwayNotificationCenter, its notifications and control center in the
 * theme's colors and font.
 *
 * Puts a marked block at the end of the user's ~/.config/swaync/style.css, from
 * templates/<set>/swaync/style.css: swaync's own color names (@define-color, read by
 * swaync before 0.12) and custom properties (:root, read from 0.12 on) set to the
 * theme, and the font. Being last, it wins over the rules above it; the rest of the
 * user's stylesheet stays as it is. Without a style.css of the user's own, swaync
 * would use the system's: the file themer then creates imports the system's first,
 * so its look is kept, and it is removed on unload. A running swaync re-reads its
 * style (swaync-client -rs).
 *
 * Options (after the host's mode):
 *   font      ui (the default) or terminal
 *   template  the set it is drawn from (templates/<set>/swaync/) */
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the notifications and the control center", "ui|terminal", "ui", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* "r, g, b" of the shared color name, for swaync's --noti-bg (see template_vars) */
static void
rgb_triple(const TmTheme *t, const char *name, char *out, size_t n)
{
	char hex[TM_COLOR_LEN + 1] = "000000";
	unsigned r = 0, g = 0, b = 0;

	tm_shared(t, name, hex, sizeof hex);
	if (sscanf(hex, "%2x%2x%2x", &r, &g, &b) != 3)
		r = g = b = 0;
	snprintf(out, n, "%u, %u, %u", r, g, b);
}

/* the module's own variables: the font family and size, and the two background
 * tones as "r, g, b" triples. swaync 0.12 declares --noti-bg as a bare triple
 * (--noti-bg: 48, 48, 48) and draws with rgba(var(--noti-bg), var(--noti-bg-alpha)),
 * so a #rrggbb there makes that rgba() invalid and the notifications lose their
 * background. */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16], raised[16], face[16];
	int font_size;

	if (max < 4)
		return 0;
	tm_font_for(t, "swaync.font", fam, sizeof fam, &font_size);
	snprintf(size, sizeof size, "%d", font_size);
	rgb_triple(t, "raised", raised, sizeof raised);
	rgb_triple(t, "face", face, sizeof face);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	v[2] = (TmVar){ "raised_rgb", raised };
	v[3] = (TmVar){ "face_rgb", face };
	return 4;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("swaync");
}

/* the stylesheet swaync uses when the user has none */
static const char *
system_style(void)
{
	static const char *const paths[] = {
		"/etc/xdg/swaync/style.css", "/usr/local/etc/xdg/swaync/style.css",
		"/usr/share/swaync/style.css",
	};

	for (size_t i = 0; i < TM_COUNT(paths); i++)
		if (!access(paths[i], R_OK))
			return paths[i];
	return NULL;
}

static int
load(const TmTheme *t)
{
	char p[PATH_MAX], text[8192];
	TmVar v[4];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (tm_render(text, sizeof text, "swaync", tm_opt(t, "swaync.template"), "style.css", t, v, nv) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/swaync/style.css");
	if (access(p, F_OK) && system_style() &&
	    tm_blockf(p, "swaync-base", "/*", " */", 1,
	              "/* no style.css of your own: swaync's own first, then the theme */\n"
	              "@import url(\"file://%s\");", system_style()) < 0)
		return -1;
	return tm_blockf(p, "swaync", "/*", " */", 0, "%s", text);
}

/* a running swaync re-reads its style; signal 0 only asks whether one runs */
static void
reload(void)
{
	static const char *const argv[] = { "swaync-client", "--reload-css", "--skip-wait", NULL };

	if (tm_signal("swaync", 0) > 0 && tm_have("swaync-client"))
		tm_run(argv);
}

static const TmModule mod = {
	.name = "swaync",
	.desc = "swaync: notifications and the control center in the theme's colors and font",
	.templates = "swaync",
	.default_on = 1,
	.present = present,
	.load = load,
	.reload = reload,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
