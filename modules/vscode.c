/* vscode.c: VS Code, VSCodium and Code - OSS in the theme's colors.
 *
 * Each installed one (its own build, or its Flatpak) gets, in its User/settings.json,
 * four top-level settings set with tm_json_set (the rest of the file is left as it is,
 * and unload takes them out again or puts your values back):
 *   "workbench.colorTheme"            VS Code's own Dark Modern or Light Modern, by
 *                                     the mode, under the id the installed build
 *                                     knows (see modern_ids)
 *   "workbench.colorCustomizations"   that theme recolored: templates/<set>/vscode/
 *                                     colors.json, the workbench in the theme's roles
 *                                     and Adwaita tones, the terminal in its colors
 *   "editor.tokenColorCustomizations" the syntax in the theme's terminal colors
 *                                     (templates/<set>/vscode/tokens.json)
 *   "editor.fontFamily" and the terminal's, and the size, with font=terminal
 * VS Code watches its settings, so a running one changes at once. No extension is
 * installed: the theme is VS Code's default, recolored.
 *
 * Where the settings are:
 *   code         ~/.config/Code/User (and ~/.config/Code - OSS/User, where Arch's
 *                code keeps them, when that directory is there)
 *   code-oss     ~/.config/Code - OSS/User
 *   codium       ~/.config/VSCodium/User
 *   Flatpaks     ~/.var/app/com.visualstudio.code/config/Code/User,
 *                ~/.var/app/com.vscodium.codium/config/VSCodium/User
 * A settings.json that is not there yet is made, holding only these.
 *
 * Options (after the host's mode):
 *   font      own (the default) keeps the editor's font; terminal sets the theme's
 *             terminal font for the editor and the integrated terminal
 *   template  the set it is drawn from (templates/<set>/vscode/) */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "themer_module.h"

static const TmOption opts[] = {
	{ "font", "font for the editor and its terminal: own keeps VS Code's, terminal sets the theme's", "own|terminal", "own", 0, 0 },
	TM_OPT_TEMPLATE,
};

typedef struct {
	const char *bins[2];     /* its commands, either of which means it is installed */
	const char *flatpak;     /* or its Flatpak, with the settings under ~/.var/app */
	const char *dirs[2];     /* ~/.config/<dir>: where its settings go */
	const char *app;         /* where a Flatpak keeps resources/app, under files/ */
} Variant;

static const Variant variants[] = {
	{ { "code", NULL },          NULL, { "Code", "Code - OSS" }, NULL },
	{ { "code-oss", NULL },      NULL, { "Code - OSS", NULL }, NULL },
	{ { "codium", "vscodium" },  NULL, { "VSCodium", NULL }, NULL },
	{ { NULL, NULL }, "com.visualstudio.code", { ".var/app/com.visualstudio.code/config/Code", NULL }, "extra/vscode/resources/app" },
	{ { NULL, NULL }, "com.vscodium.codium", { ".var/app/com.vscodium.codium/config/VSCodium", NULL }, "share/codium/resources/app" },
};

static int
installed(const Variant *v)
{
	if (v->flatpak)
		return tm_have_flatpak(v->flatpak);
	return (v->bins[0] && tm_have(v->bins[0])) || (v->bins[1] && tm_have(v->bins[1]));
}

static int
present(const TmTheme *t)
{
	(void)t;
	for (size_t i = 0; i < TM_COUNT(variants); i++)
		if (installed(&variants[i]))
			return 1;
	return 0;
}

/* 1 if app (a resources/app directory, or a directory holding extensions/) is a
 * build whose default themes have the ids "Dark Modern" and "Light Modern" */
static int
modern_in(const char *app)
{
	char p[PATH_MAX], buf[65536];
	size_t n;
	FILE *f;

	if (snprintf(p, sizeof p, "%s/extensions/theme-defaults/package.json", app) >= (int)sizeof p ||
	    !(f = fopen(p, "r")))
		return -1;
	n = fread(buf, 1, sizeof buf - 1, f);
	fclose(f);
	buf[n] = '\0';
	return strstr(buf, "\"Dark Modern\"") != NULL;
}

/* VS Code 1.113 renamed its themes "Default Dark Modern" and "Default Light Modern"
 * to "Dark Modern" and "Light Modern". A build from before knows only the old ids;
 * one from since takes the old ids too, but writes the new one back over them in
 * settings.json, so every apply would leave a file VS Code then changes. The ids are
 * read from the installed build's theme-defaults extension, found from the command
 * on PATH (its bin/ beside resources/app, or the usual install directories) or the
 * Flatpak; 1 when it has the new ids, 0 (the old ids, which every build takes) when
 * it has not or cannot be found. */
static int
modern_ids(const TmTheme *t, const Variant *v)
{
	char p[PATH_MAX], real[PATH_MAX], app[PATH_MAX + 64];
	const char *path = getenv("PATH");

	if (v->flatpak) {
		/* the system installation, then the user's */
		for (int i = 0; i < 2; i++) {
			if (i == 0)
				snprintf(p, sizeof p, "/var/lib/flatpak/app");
			else
				tm_path(p, sizeof p, t, ".local/share/flatpak/app");
			snprintf(app, sizeof app, "%.*s/%s/current/active/files/%s", (int)sizeof p - 1, p,
			         v->flatpak, v->app);
			if (modern_in(app) >= 0)
				return modern_in(app);
		}
		return 0;
	}
	for (int b = 0; b < 2 && v->bins[b]; b++) {
		const char *s = path ? path : "";

		while (*s) {
			size_t len = strcspn(s, ":");
			char *slash;

			if (len && snprintf(p, sizeof p, "%.*s/%s", (int)len, s, v->bins[b]) < (int)sizeof p &&
			    !access(p, X_OK) && realpath(p, real)) {
				static const char *const rel[] = { "../resources/app", "resources/app" };

				if ((slash = strrchr(real, '/')))
					*slash = '\0';
				for (size_t k = 0; k < TM_COUNT(rel); k++) {
					snprintf(app, sizeof app, "%s/%s", real, rel[k]);
					if (modern_in(app) >= 0)
						return modern_in(app);
				}
				/* distribution packages that start it from a script */
				{
					static const char *const pre[] = { "/usr/lib/", "/usr/lib/", "/usr/share/", "/opt/" };
					static const char *const post[] = { "", "/resources/app", "/resources/app", "/resources/app" };

					for (size_t k = 0; k < TM_COUNT(pre); k++) {
						snprintf(app, sizeof app, "%s%s%s", pre[k], v->bins[b], post[k]);
						if (modern_in(app) >= 0)
							return modern_in(app);
					}
				}
				return 0;
			}
			s += len;
			if (*s == ':')
				s++;
		}
	}
	return 0;
}

/* the editor and the chrome around it, as VS Code's Modern themes have them: on the
 * dark side the chrome a step darker than the editor, on the light side the editor
 * white and the chrome a step darker */
static int
vars(const TmTheme *t, TmVar *v, int max)
{
	static char view[TM_COLOR_LEN], bg[TM_COLOR_LEN];

	if (max < 2 || tm_shared(t, "view", view, sizeof view) < 0)
		return 0;
	snprintf(bg, sizeof bg, "%s", t->bg);
	v[0] = (TmVar){ "vscode_editor", t->dark ? bg : view };
	v[1] = (TmVar){ "vscode_chrome", t->dark ? view : bg };
	return 2;
}
TM_VARS(vars)

/* "\"text\"", with quotes and backslashes escaped, for tm_json_set */
static void
json_string(char *out, size_t n, const char *s)
{
	size_t o = 0;

	if (n < 3)
		return;
	out[o++] = '"';
	for (; *s && o + 3 < n; s++) {
		if (*s == '"' || *s == '\\')
			out[o++] = '\\';
		out[o++] = *s;
	}
	out[o++] = '"';
	out[o] = '\0';
}

/* the four settings in one settings.json */
static int
settings(const TmTheme *t, const char *path, const char *colors, const char *tokens, int modern)
{
	char family[200], size[16];
	const char *font = tm_opt(t, "vscode.font");

	if (access(path, F_OK) && tm_writef(path, "// generated by themer: VS Code settings, made for its colors\n{\n}\n") < 0)
		return -1;
	if (tm_json_set(path, "workbench.colorTheme",
	                modern ? (t->dark ? "\"Dark Modern\"" : "\"Light Modern\"")
	                       : (t->dark ? "\"Default Dark Modern\"" : "\"Default Light Modern\"")) < 0 ||
	    tm_json_set(path, "workbench.colorCustomizations", colors) < 0 ||
	    tm_json_set(path, "editor.tokenColorCustomizations", tokens) < 0)
		return -1;
	if (font && !strcmp(font, "terminal")) {
		json_string(family, sizeof family, t->mono_font);
		snprintf(size, sizeof size, "%d", t->mono_size);
		if (tm_json_set(path, "editor.fontFamily", family) < 0 ||
		    tm_json_set(path, "editor.fontSize", size) < 0 ||
		    tm_json_set(path, "terminal.integrated.fontFamily", family) < 0)
			return -1;
	}
	return 0;
}

/* 1 the first time a path is seen this load */
static int
once(char (*seen)[PATH_MAX], int *n, const char *path)
{
	for (int i = 0; i < *n; i++)
		if (!strcmp(seen[i], path))
			return 0;
	if (*n < 8)
		snprintf(seen[(*n)++], PATH_MAX, "%s", path);
	return 1;
}

static int
isdir(const char *p)
{
	struct stat st;

	return !stat(p, &st) && S_ISDIR(st.st_mode);
}

static int
load(const TmTheme *t)
{
	static char colors[32768], tokens[4096];
	const char *set = tm_opt(t, "vscode.template");
	static char seen[8][PATH_MAX];   /* a file two variants share is set once */
	TmVar v[2];
	int nv = vars(t, v, 2), done = 0, nseen = 0;

	if (tm_render(colors, sizeof colors, "vscode", set, "colors.json", t, v, nv) < 0 ||
	    tm_render(tokens, sizeof tokens, "vscode", set, "tokens.json", t, v, nv) < 0)
		return -1;
	for (size_t i = 0; i < TM_COUNT(variants); i++) {
		const Variant *va = &variants[i];
		char base[PATH_MAX], file[PATH_MAX];
		int any = 0;

		if (!installed(va))
			continue;
		/* every settings directory of it that is there; the first when none is */
		for (int k = 0; k < 2 && va->dirs[k]; k++) {
			snprintf(file, sizeof file, "%s%s", va->flatpak ? "" : ".config/", va->dirs[k]);
			tm_path(base, sizeof base, t, file);
			if (!isdir(base))
				continue;
			if (snprintf(file, sizeof file, "%s/User/settings.json", base) >= (int)sizeof file)
				continue;
			any = done = 1;
			if (once(seen, &nseen, file) && settings(t, file, colors, tokens, modern_ids(t, va)) < 0)
				return -1;
		}
		if (!any) {
			snprintf(file, sizeof file, "%s%s/User/settings.json", va->flatpak ? "" : ".config/", va->dirs[0]);
			tm_path(base, sizeof base, t, file);
			done = 1;
			if (once(seen, &nseen, base) && settings(t, base, colors, tokens, modern_ids(t, va)) < 0)
				return -1;
		}
	}
	return done ? 0 : -1;
}

static const TmModule mod = {
	.name = "vscode",
	.desc = "VS Code, VSCodium and Code - OSS: the default theme in the theme's colors (settings.json)",
	.templates = "vscode",
	.default_on = 1,
	.present = present,
	.load = load,
	.options = opts,
	.noptions = TM_COUNT(opts),
};
TM_EXPORT(mod)
