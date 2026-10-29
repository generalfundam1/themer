/* thunderbird.c: the Thunderbird interface, as marked blocks at the end of the
 * profile's chrome/userChrome.css (the window: tabs, toolbars, the spaces bar,
 * menus, compose) and chrome/userContent.css (the panes: folders, message list,
 * message header, and the settings and address book pages, which Thunderbird draws
 * as content documents since 115). Your own rules stay as they are; unloading cuts
 * the blocks out again.
 *
 * Thunderbird draws from design tokens (--layout-background-*, --selected-item-*,
 * --tree-*, --button-*, ...); templates/<set>/thunderbird/tokens.css sets them to the
 * theme's roles and goes into both files, followed by chrome.css or content.css.
 * Thunderbird reads the files at startup, with
 * toolkit.legacyUserProfileCustomizations.stylesheets set to true in its config
 * editor; the module does not write user.js. */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "themer_module.h"
#include "mozprofile.h"

static const TmOption opts[] = {
	{ "font", "font for the interface (tabs, toolbars, folders, message list): own keeps Thunderbird's (the system font), ui or terminal sets the theme's", "own|ui|terminal", "own", 0, 0 },
	TM_OPT_TEMPLATE,
};

/* where Thunderbird keeps its profiles: the XDG location (current builds), the
 * classic one, the snap, the Flatpak */
static const char *const bases[] = {
	".config/thunderbird",
	".thunderbird",
	"snap/thunderbird/common/.thunderbird",
	".var/app/org.mozilla.Thunderbird/.thunderbird",
};

/* the module's own variables: the font family and size, and font_rule, a complete
 * font-family declaration or nothing with font=own (the family is then the UI font,
 * the system font Thunderbird uses itself, for sets that size from it) */
static int
template_vars(const TmTheme *t, TmVar *v, int max)
{
	static char fam[128], size[16], rule[192];
	const char *font = tm_opt(t, "thunderbird.font");
	int font_size;

	if (max < 3)
		return 0;
	tm_font_for(t, "thunderbird.font", fam, sizeof fam, &font_size);
	snprintf(size, sizeof size, "%d", font_size);
	rule[0] = '\0';
	if (font && strcmp(font, "own"))
		snprintf(rule, sizeof rule, "font-family: \"%s\" !important;", fam);
	v[0] = (TmVar){ "font_family", fam };
	v[1] = (TmVar){ "font_size", size };
	v[2] = (TmVar){ "font_rule", rule };
	return 3;
}

TM_VARS(template_vars)

static int
present(const TmTheme *t)
{
	return moz_present(t, bases, TM_COUNT(bases));
}

static int
load(const TmTheme *t)
{
	static char tokens[128 * 1024], part[128 * 1024], chrome[256 * 1024], content[256 * 1024];
	char dir[PATH_MAX];
	const char *set = tm_opt(t, "thunderbird.template");
	TmVar v[3];
	int nv = template_vars(t, v, TM_COUNT(v));

	if (!moz_profile(t, "thunderbird", bases, TM_COUNT(bases), dir, sizeof dir))
		return 0;
	if (tm_render(tokens, sizeof tokens, "thunderbird", set, "tokens.css", t, v, nv) < 0 ||
	    tm_render(part, sizeof part, "thunderbird", set, "chrome.css", t, v, nv) < 0 ||
	    snprintf(chrome, sizeof chrome, "%s\n%s", tokens, part) >= (int)sizeof chrome)
		return -1;
	if (tm_render(part, sizeof part, "thunderbird", set, "content.css", t, v, nv) < 0 ||
	    snprintf(content, sizeof content, "%s\n%s", tokens, part) >= (int)sizeof content)
		return -1;
	{
		char setdir[PATH_MAX];
		const char *sd = tm_template_dir("thunderbird", set, setdir, sizeof setdir) == 0 ? setdir : NULL;

		return moz_write(dir, "thunderbird", chrome, content, sd);
	}
}

static const TmModule mod = {
	.name = "thunderbird",
	.desc = "Thunderbird: the window, the panes and the settings pages",
	.templates = "thunderbird",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
