/* dmenu.c: the dmenu launcher, with the theme's colors and font. The script and
 * its template (templates/launcher/) are shared with wmenu through launcher.h. */
#include "launcher.h"

/* dmenu's flags: normal and selected colors, and the font. dmenu takes X colors, so
 * each gets its '#' (quoted: in the script a word starting with '#' is a comment), and
 * an Xft font name, "family:size=N", where '-', ':' and ',' in the family are escaped
 * (the pango-style "family N" names a family that does not exist, and fontconfig
 * falls back to its default font and size). */
static const char *
args(const TmTheme *t)
{
	static char buf[512];
	char fam[128], esc[256], *o = esc;
	int size;

	tm_font_for(t, "dmenu.font", fam, sizeof fam, &size);
	for (const char *s = fam; *s && o < esc + sizeof esc - 3; s++) {
		if (*s == '"' || *s == '$' || *s == '`' || *s == '\\')
			continue;           /* nothing a font name needs; they would break the quoting */
		if (*s == '-' || *s == ':' || *s == ',')
			*o++ = '\\';
		*o++ = *s;
	}
	*o = '\0';
	snprintf(buf, sizeof buf, "-nb \"#%s\" -nf \"#%s\" -sb \"#%s\" -sf \"#%s\" -fn \"%s:size=%d\"",
	         t->bg, t->fg, t->acc, t->accfg, esc, size);
	return buf;
}

static int
vars(const TmTheme *t, TmVar *v, int max)
{
	return launcher_vars("dmenu", args(t), v, max);
}

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("dmenu");
}

static int
load(const TmTheme *t)
{
	return launcher_load(t, "dmenu", args(t));
}

TM_VARS(vars)

static const TmModule mod = {
	.name = "dmenu",
	.desc = "dmenu: a launcher in the theme's colors, used as $menu by sway and i3",
	.templates = LAUNCHER_TEMPLATES,
	.default_on = 1,
	.present = present,
	.load = load,
	.options = launcher_options,
	.noptions = TM_COUNT(launcher_options),
};
TM_EXPORT(mod)
