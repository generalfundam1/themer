/* setimport.c: turning an existing theme into a template set, and checking a set.
 *
 * themer set import NAME MODULE PATH: PATH is what the user has (a GTK theme
 * directory, a .qss, a Firefox chrome/ directory, a waybar style.css...). Its
 * files land in ~/.config/themer/templates/NAME/<module dir>/ under the names the
 * module renders (set_import_name), and every text file is "templated": a color in
 * it that equals one of the current theme's colors becomes {{that name}}, so the
 * file follows the color theme from then on; other colors stay as they are and are
 * counted in the report. Files that are not text (images, fonts) are copied as
 * assets, which the module places beside its output.
 *
 * themer set check NAME: renders every file of the set for every theme in both
 * modes, so an unknown {{name}} or a missing file shows up before apply. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "themer.h"

#define SRC_MAX (2 * 1024 * 1024)

/* ---- names ---- */

/* The file name a module renders for an input, from the input's own name and
 * extension. NULL means the file is an asset, copied as it is. */
static const char *
output_name(const char *dir, const char *base)
{
	const char *ext = strrchr(base, '.');
	static const struct { const char *dir, *ext, *name; } primary[] = {
		{ "gtk", ".css", "gtk.css" }, { "qt", ".qss", "qt.qss" }, { "qt", ".conf", "colors.conf" },
		{ "waybar", ".css", "style.css" }, { "firefox", ".css", "userChrome.css" },
		{ "thunderbird", ".css", "chrome.css" }, { "wm", ".conf", "themer.conf" },
		{ "launcher", "", "launcher" }, { "nvim", ".lua", "themer.lua" }, { "nvim", ".vim", "select.vim" },
		{ "fcitx", ".conf", "theme.conf" }, { "newt", ".conf", "newt.conf" }, { "st", ".h", "colors.h" },
		{ "example", "", "colors" },
	};
	char files[MAX_NAMES][NAME_LEN];
	int n = template_files(dir, TM_DEFAULT_SET, files, MAX_NAMES);

	/* a name the built-in set has is kept */
	for (int i = 0; i < n; i++)
		if (!strcmp(files[i], base))
			return files[i][0] ? base : NULL;
	/* Thunderbird's own files are named like Firefox's */
	if (!strcmp(dir, "thunderbird")) {
		if (!strcmp(base, "userChrome.css"))
			return "chrome.css";
		if (!strcmp(base, "userContent.css"))
			return "content.css";
	}
	if (!strcmp(dir, "firefox") && !strcmp(base, "userContent.css"))
		return "userContent.css";
	if (!ext)
		ext = "";
	for (size_t i = 0; i < sizeof primary / sizeof primary[0]; i++)
		if (!strcmp(primary[i].dir, dir) && !strcmp(primary[i].ext, ext))
			return primary[i].name;
	return NULL;
}

/* ---- templating ---- */

typedef struct {
	const char *name;
	char hex[TM_COLOR_LEN];        /* lower case, six digits */
} Color;

/* every shared color name, roles first so a color that is both a role and a tone
 * (bg and field, say) gets the role's name */
static int
scheme_colors(const Config *c, Color *out, int max)
{
	static const char *const roles[] = { "bg", "fg", "surf", "acc", "accfg", "urg", "warn", "line", "on_urg", "on_warn" };
	Scheme s;
	TmTheme t;
	SharedStore st;
	TmVar v[MAX_SHARED];
	int nv, n = 0;

	theme_scheme(c, &s);
	themer_view(c, &s, &t);
	nv = shared_vars(&t, &st, v, MAX_SHARED);
	for (size_t r = 0; r < sizeof roles / sizeof roles[0] && n < max; r++)
		for (int i = 0; i < nv; i++)
			if (!strcmp(v[i].name, roles[r]) && strlen(v[i].value) == 6) {
				out[n].name = v[i].name;
				for (int k = 0; k < 6; k++)
					out[n].hex[k] = (char)tolower((unsigned char)v[i].value[k]);
				out[n].hex[6] = '\0';
				n++;
			}
	for (int i = 0; i < nv && n < max; i++) {
		int dup = 0;

		if (strlen(v[i].value) != 6 || strspn(v[i].value, "0123456789abcdefABCDEF") != 6)
			continue;
		for (int k = 0; k < n; k++)
			if (!strcmp(out[k].name, v[i].name))
				dup = 1;
		if (dup)
			continue;
		out[n].name = v[i].name;
		for (int k = 0; k < 6; k++)
			out[n].hex[k] = (char)tolower((unsigned char)v[i].value[k]);
		out[n].hex[6] = '\0';
		n++;
	}
	/* the variables point into st and v, which die here: the names are the static
	 * strings palette.c uses, so they survive; the values were copied */
	return n;
}

static const char *
color_name(const Color *cs, int n, const char *hex6)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(cs[i].hex, hex6))
			return cs[i].name;
	return NULL;
}

/* #rrggbb and #rgb in src become #{{name}} where the color is the theme's. in
 * and out are the counts of colors replaced and left. Returns the output length,
 * or -1 if it does not fit. */
static long
templatize(const char *src, size_t len, char *out, size_t n, const Color *cs, int ncs,
           int *replaced, int *left, char leftover[][TM_COLOR_LEN], int *nleft)
{
	size_t o = 0;

	for (size_t i = 0; i < len;) {
		if (src[i] == '#' && i + 1 < len && isxdigit((unsigned char)src[i + 1])) {
			size_t k = i + 1;
			char hex[7];
			const char *name = NULL;

			while (k < len && isxdigit((unsigned char)src[k]))
				k++;
			if (k - i - 1 == 6) {
				for (int j = 0; j < 6; j++)
					hex[j] = (char)tolower((unsigned char)src[i + 1 + j]);
				hex[6] = '\0';
				name = color_name(cs, ncs, hex);
			} else if (k - i - 1 == 3) {
				for (int j = 0; j < 3; j++)
					hex[2 * j] = hex[2 * j + 1] = (char)tolower((unsigned char)src[i + 1 + j]);
				hex[6] = '\0';
				name = color_name(cs, ncs, hex);
			}
			if (name) {
				int w = snprintf(out + o, n - o, "#{{%s}}", name);

				if (w < 0 || o + (size_t)w >= n)
					return -1;
				o += (size_t)w;
				(*replaced)++;
				i = k;
				continue;
			}
			if (k - i - 1 == 6 || k - i - 1 == 3) {
				int dup = 0;

				for (int j = 0; j < *nleft; j++)
					if (!strcmp(leftover[j], hex))
						dup = 1;
				if (!dup && *nleft < 64)
					snprintf(leftover[(*nleft)++], TM_COLOR_LEN, "%s", hex);
				(*left)++;
			}
		}
		if (o + 1 >= n)
			return -1;
		out[o++] = src[i++];
	}
	out[o] = '\0';
	return (long)o;
}

/* The marker line every rendered file must start with (tm_writef refuses to touch a
 * file without it, and unload removes only files that have it), in the comment
 * syntax of the file type. NULL for a type with no comment syntax known here. */
static const char *
marker_for(const char *name)
{
	const char *ext = strrchr(name, '.');

	if (!ext)
		return "# generated by themer ({{theme}}, {{mode}})\n";
	if (!strcmp(ext, ".css") || !strcmp(ext, ".qss") || !strcmp(ext, ".h"))
		return "/* generated by themer ({{theme}}, {{mode}}) */\n";
	if (!strcmp(ext, ".lua"))
		return "-- generated by themer ({{theme}}, {{mode}})\n";
	if (!strcmp(ext, ".vim"))
		return "\" generated by themer ({{theme}}, {{mode}})\n";
	if (!strcmp(ext, ".conf") || !strcmp(ext, ".ini") || !strcmp(ext, ".rasi") || !strcmp(ext, ".toml"))
		return "# generated by themer ({{theme}}, {{mode}})\n";
	return NULL;
}

/* text: no NUL in the first 4 KiB */
static int
is_text(const char *buf, size_t len)
{
	size_t n = len < 4096 ? len : 4096;

	return memchr(buf, '\0', n) == NULL;
}

static int
read_all(const char *path, char *buf, size_t cap, size_t *len)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return -1;
	*len = fread(buf, 1, cap, f);
	fclose(f);
	return *len == cap ? -1 : 0;
}

/* ---- the import ---- */

typedef struct {
	const Config *c;
	const char *set, *dir;
	char dst[PATH_MAX];            /* ~/.config/themer/templates/<set>/<dir> */
	Color colors[64];
	int ncolors;
	int files, assets, replaced, left;
	char leftover[64][TM_COLOR_LEN];
	int nleft;
	FILE *report;
} Import;

/* one input file: templated under name, or copied as an asset under rel */
static int
import_file(Import *im, const char *src, const char *rel, const char *name)
{
	static char buf[SRC_MAX], out[SRC_MAX + 65536];
	char dst[PATH_MAX];
	size_t len;
	long olen;

	if (read_all(src, buf, sizeof buf, &len) < 0) {
		fprintf(stderr, "themer: cannot read %s\n", src);
		return -1;
	}
	if (name && is_text(buf, len)) {
		if (snprintf(dst, sizeof dst, "%s/%s", im->dst, name) >= (int)sizeof dst)
			return -1;
		if (memmem(buf, len, "{{", 2)) {
			fprintf(stderr, "themer: %s has \"{{\" in it, which a template cannot carry; "
			        "copied as an asset\n", src);
			name = NULL;
		} else {
			const char *marker = memmem(buf, len < 512 ? len : 512, "generated by themer", 19)
			                     ? NULL : marker_for(name);
			size_t off = 0;

			if (marker) {
				/* after a #! line, if the file starts with one */
				if (len > 2 && buf[0] == '#' && buf[1] == '!') {
					const char *nl = memchr(buf, '\n', len);

					off = nl ? (size_t)(nl - buf) + 1 : len;
				}
				memcpy(out, buf, off);
				memcpy(out + off, marker, strlen(marker));
			}
			olen = templatize(buf + off, len - off, out + off + (marker ? strlen(marker) : 0),
			                  sizeof out - off - (marker ? strlen(marker) : 0), im->colors, im->ncolors,
			                  &im->replaced, &im->left, im->leftover, &im->nleft);
			if (olen < 0)
				return -1;
			olen += (long)(off + (marker ? strlen(marker) : 0));
			if (write_bytes(dst, out, (size_t)olen) < 0)
				return -1;
			fprintf(im->report, "  %-28s -> %s/%s\n", rel, im->dir, name);
			im->files++;
			return 0;
		}
	}
	if (snprintf(dst, sizeof dst, "%s/%s", im->dst, rel) >= (int)sizeof dst ||
	    write_bytes(dst, buf, len) < 0)
		return -1;
	fprintf(im->report, "  %-28s -> %s/%s  (asset, as it is)\n", rel, im->dir, rel);
	im->assets++;
	return 0;
}

/* a directory, recursively: top-level files get output names, deeper ones are assets */
static int
import_dir(Import *im, const char *root, const char *sub)
{
	char path[PATH_MAX];
	DIR *d;
	struct dirent *e;

	if (snprintf(path, sizeof path, "%s%s%s", root, sub[0] ? "/" : "", sub) >= (int)sizeof path ||
	    !(d = opendir(path)))
		return -1;
	while ((e = readdir(d))) {
		char src[PATH_MAX], rel[PATH_MAX];
		struct stat st;

		if (e->d_name[0] == '.')
			continue;
		if (snprintf(src, sizeof src, "%s/%s", path, e->d_name) >= (int)sizeof src ||
		    snprintf(rel, sizeof rel, "%s%s%s", sub, sub[0] ? "/" : "", e->d_name) >= (int)sizeof rel ||
		    stat(src, &st))
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (import_dir(im, root, rel) < 0) {
				closedir(d);
				return -1;
			}
		} else if (S_ISREG(st.st_mode)) {
			const char *name = sub[0] ? NULL : output_name(im->dir, e->d_name);

			if (import_file(im, src, rel, name) < 0) {
				closedir(d);
				return -1;
			}
		}
	}
	closedir(d);
	return 0;
}

/* the layouts themes come in: a GTK theme has gtk-3.0/ and gtk-4.0/, a Firefox
 * profile has chrome/; the files are taken from there */
static int
import_layout(Import *im, const char *path)
{
	char sub[PATH_MAX];
	struct stat st;

	if (!strcmp(im->dir, "gtk")) {
		int any = 0;

		if (snprintf(sub, sizeof sub, "%s/gtk-3.0", path) < (int)sizeof sub && !stat(sub, &st)) {
			if (import_dir(im, sub, "") < 0)
				return -1;
			any = 1;
		}
		if (snprintf(sub, sizeof sub, "%s/gtk-4.0/gtk.css", path) < (int)sizeof sub && !stat(sub, &st)) {
			/* GTK 4's own stylesheet, as gtk4.css; its assets go with the rest */
			if (import_file(im, sub, "gtk-4.0/gtk.css", "gtk4.css") < 0)
				return -1;
			snprintf(sub, sizeof sub, "%s/gtk-4.0", path);
			{
				DIR *d = opendir(sub);
				struct dirent *e;

				while (d && (e = readdir(d))) {
					char p[PATH_MAX], rel[PATH_MAX];

					if (e->d_name[0] == '.' || !strcmp(e->d_name, "gtk.css"))
						continue;
					if (snprintf(p, sizeof p, "%s/%s", sub, e->d_name) >= (int)sizeof p)
						continue;
					if (!stat(p, &st) && S_ISDIR(st.st_mode)) {
						snprintf(rel, sizeof rel, "%s", e->d_name);
						import_dir(im, sub, rel);
					} else if (!stat(p, &st) && S_ISREG(st.st_mode)) {
						import_file(im, p, e->d_name, NULL);
					}
				}
				if (d)
					closedir(d);
			}
			any = 1;
		}
		if (any)
			return 0;
	}
	if (!strcmp(im->dir, "firefox") || !strcmp(im->dir, "thunderbird")) {
		if (snprintf(sub, sizeof sub, "%s/chrome", path) < (int)sizeof sub && !stat(sub, &st))
			return import_dir(im, sub, "");
	}
	return import_dir(im, path, "");
}

int
set_import(const Config *c, const char *set, const char *module, const char *path, FILE *report)
{
	Import im;
	struct stat st;
	char user[PATH_MAX], conf[PATH_MAX];
	int i = themer_module_find(module);

	memset(&im, 0, sizeof im);
	if (i < 0 || !themer_module_templates(i)) {
		fprintf(stderr, "themer: %s is not a module that renders templates\n", module);
		return -1;
	}
	if (!valid_name(set) || !strcmp(set, TM_DEFAULT_SET)) {
		fprintf(stderr, "themer: the set needs a name of its own (letters, digits, - and _)\n");
		return -1;
	}
	if (stat(path, &st)) {
		fprintf(stderr, "themer: no such file or directory: %s\n", path);
		return -1;
	}
	if (path_under_home(user, sizeof user, ".config/themer/templates") < 0)
		return -1;
	im.c = c;
	im.set = set;
	im.dir = themer_module_templates(i);
	im.report = report;
	if (snprintf(im.dst, sizeof im.dst, "%s/%s/%s", user, set, im.dir) >= (int)sizeof im.dst)
		return -1;
	im.ncolors = scheme_colors(c, im.colors, 64);

	fprintf(report, "%s (%s, %s) into %s:\n", path, c->th.name,
	        c->mode == MODE_DARK ? "dark" : "light", im.dst);
	if (S_ISDIR(st.st_mode)) {
		if (import_layout(&im, path) < 0)
			return -1;
	} else {
		const char *base = strrchr(path, '/');
		const char *name;

		base = base ? base + 1 : path;
		name = output_name(im.dir, base);
		if (!name) {
			fprintf(stderr, "themer: %s: not a file %s renders (see themer template list %s); "
			        "name it like one of those\n", base, module, module);
			return -1;
		}
		if (import_file(&im, path, base, name) < 0)
			return -1;
	}
	/* thunderbird's tokens.css would otherwise come from the built-in set, on top of
	 * the imported chrome: an empty one keeps the import as it is */
	if (!strcmp(im.dir, "thunderbird")) {
		char tok[PATH_MAX];

		if (snprintf(tok, sizeof tok, "%s/tokens.css", im.dst) < (int)sizeof tok && access(tok, F_OK))
			write_bytes(tok, "/* no tokens: this set brings its own chrome */\n", 47);
	}
	/* the set's manifest, if it has none yet */
	if (snprintf(conf, sizeof conf, "%s/%s/set.conf", user, set) < (int)sizeof conf && access(conf, F_OK)) {
		char body[512];
		int n = snprintf(body, sizeof body, "name=%s\ndescription=imported from %s\nauthor=\nurl=\n", set, path);

		if (n > 0 && n < (int)sizeof body)
			write_bytes(conf, body, (size_t)n);
	}

	fprintf(report, "%d file(s) templated, %d asset(s) copied; %d color(s) became theme variables",
	        im.files, im.assets, im.replaced);
	if (im.left) {
		fprintf(report, ", %d left as they are (%d distinct:", im.left, im.nleft);
		for (int k = 0; k < im.nleft && k < 12; k++)
			fprintf(report, " #%s", im.leftover[k]);
		fprintf(report, "%s). Those will not follow the color theme; replace them with "
		        "{{bg}}, {{fg}}, {{acc}}... by hand where they should.", im.nleft > 12 ? " ..." : "");
	}
	fprintf(report, "\nthemer set check %s  renders it for every theme;  themer set %s  switches to it.\n", set, set);
	return 0;
}

/* ---- the check ---- */

/* the first module that renders dir, for its variables */
static int
module_for(const char *dir)
{
	for (int i = 0; i < themer_module_count(); i++)
		if (themer_module_templates(i) && !strcmp(themer_module_templates(i), dir))
			return i;
	return -1;
}

int
set_check(const Config *c, const char *set, FILE *report)
{
	static char out[2 * 1024 * 1024];
	char names[MAX_NAMES][NAME_LEN], dirs[MAX_NAMES][NAME_LEN], files[MAX_NAMES][NAME_LEN];
	char conf[PATH_MAX], user[PATH_MAX];
	int nt = theme_list(names, MAX_NAMES), nd, bad = 0, rendered = 0, covered = 0;
	Config cc = *c;

	snprintf(cc.set, sizeof cc.set, "%s", set);
	nd = template_dirs(dirs, MAX_NAMES);
	for (int d = 0; d < nd; d++) {
		int nf = template_files(dirs[d], set, files, MAX_NAMES), mi = module_for(dirs[d]);

		if (!nf)
			continue;
		covered++;
		fprintf(report, "%-12s", dirs[d]);
		for (int f = 0; f < nf; f++)
			fprintf(report, " %s", files[f]);
		fprintf(report, "\n");
		for (int f = 0; f < nf; f++) {
			if (!strcmp(files[f], "set.conf"))
				continue;
			for (int t = 0; t < nt; t++)
				for (int m = 0; m < 2; m++) {
					cc.mode = m;
					if (theme_load(names[t], &cc.th) < 0)
						continue;
					if (front_render(&cc, mi >= 0 ? themer_module_name(mi) : NULL, dirs[d], files[f],
					                 out, sizeof out) < 0) {
						fprintf(report, "  FAIL %s/%s with %s %s\n", dirs[d], files[f], names[t],
						        m ? "light" : "dark");
						bad++;
					} else {
						rendered++;
					}
				}
		}
	}
	if (!covered) {
		fprintf(report, "no set '%s'\n", set);
		return -1;
	}
	if (path_under_home(user, sizeof user, ".config/themer/templates") == 0 &&
	    snprintf(conf, sizeof conf, "%s/%s/set.conf", user, set) < (int)sizeof conf && access(conf, F_OK))
		fprintf(report, "(no set.conf: add one with name=, description=, author= for themer set list)\n");
	fprintf(report, "%d render(s) over %d theme(s) in both modes, %d failed; %d of %d module directories covered, "
	        "the rest from %s\n", rendered + bad, nt, bad, covered, nd, TM_DEFAULT_SET);
	return bad ? -1 : 0;
}

/* ---- set.conf ---- */

int
set_info(const char *set, const char *key, char *out, size_t n)
{
	char p[PATH_MAX], line[512];
	FILE *f = NULL;
	const char *roots[3];
	char user[PATH_MAX];
	const char *env = getenv("THEMER_TEMPLATES");
	int nr = 0;

	if (path_under_home(user, sizeof user, ".config/themer/templates") == 0)
		roots[nr++] = user;
	if (env)
		roots[nr++] = env;
	roots[nr++] = SYS_TEMPLATES_DIR;
	for (int i = 0; i < nr && !f; i++)
		if (snprintf(p, sizeof p, "%s/%s/set.conf", roots[i], set) < (int)sizeof p)
			f = fopen(p, "r");
	if (!f)
		return -1;
	out[0] = '\0';
	while (fgets(line, sizeof line, f)) {
		size_t kl = strlen(key);

		if (!strncmp(line, key, kl) && line[kl] == '=') {
			snprintf(out, n, "%s", line + kl + 1);
			out[strcspn(out, "\r\n")] = '\0';
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	return -1;
}
