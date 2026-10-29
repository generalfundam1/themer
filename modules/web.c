/* web.c: themer-web's own page, as a module like the programs it themes.
 *
 * The page's layout is themer-web's (gui/web/index.html); its look, the colors,
 * edges and corners of every control, is the template set's web/page.css: Themer
 * Default's after GNOME's Adwaita, Themer Chicago's square and bevelled. themer-web
 * renders it as /page.css from the settings being browsed, so it changes with the
 * set and the theme before they are applied, as the rest of the preview does; this
 * module is what gives it the options every module has (mode, to pin the page to
 * one side of the theme, and template, to draw it from another set) and a switch:
 * off, the page keeps the browser's own controls. Nothing is written to disk. */
#include "themer_module.h"

static const TmOption opts[] = {
	TM_OPT_TEMPLATE,
};

static int
load(const TmTheme *t)
{
	(void)t;
	return 0;
}

static const TmModule mod = {
	.name = "web",
	.desc = "themer-web: the look of its own page, from the template set",
	.templates = "web",
	.default_on = 1,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
