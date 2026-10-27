/* icons.h: toolbar icons from the installed icon theme, for modules/firefox.c.
 *
 * Each glyph has a list of names (freedesktop names first, then the older GTK ones),
 * and the theme is searched along its Inherits chain from index.theme, ending at
 * hicolor and then Adwaita. Themes are looked for in the user's icon directories
 * (~/.icons, ~/.local/share/icons) and the system's (/usr/share/icons,
 * /usr/local/share/icons), in any of the usual layouts: <size>/<context>/,
 * <context>/<size>/, <context>/symbolic/, <symbolic>/<context>/, and
 * scalable/<context>/. Ranking: symbolic SVG, then other SVG, then PNG (the size
 * nearest 22px). A glyph takes the first theme in the chain that has it at all.
 *
 * icon_css embeds one icon as a CSS url() with a base64 data URI, so the browser
 * needs no file access. An SVG can be made one color (the bar's ink), and every
 * SVG's root element is rewritten to one form: a viewBox and a 16px size. */
#ifndef ICONS_H
#define ICONS_H

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer_module.h"

#ifndef SYS_ICONS_DIR
#define SYS_ICONS_DIR "/usr/local/share/themer/icons"
#endif

/* the glyphs the chrome needs, each with the icon names to try, in order */
static const struct { const char *key; const char *names[6]; } glyphs[] = {
	{ "back",       { "go-previous", "back", "go-back", "gtk-go-back", "arrow-left", NULL } },
	{ "forward",    { "go-next", "forward", "go-forward", "gtk-go-forward", "arrow-right", NULL } },
	{ "reload",     { "view-refresh", "reload", "gtk-refresh", "view-reload", NULL } },
	{ "stop",       { "process-stop", "gtk-stop", "stop", "media-playback-stop", NULL } },
	{ "home",       { "go-home", "gtk-home", "user-home", "home", NULL } },
	{ "newtab",     { "tab-new", "document-new", "list-add", "gtk-add", "add", NULL } },
	{ "menu",       { "open-menu", "application-menu", "view-more", "gtk-index", "menu", NULL } },
	{ "star",       { "starred", "emblem-favorite", "bookmark-new", "gtk-about", NULL } },
	{ "star_off",   { "non-starred", "bookmark-new", "bookmark", "emblem-favorite", NULL } },
	{ "downloads",  { "folder-download", "emblem-downloads", "go-down", "document-save", "download", NULL } },
	{ "extensions", { "application-x-addon", "system-run", "preferences-plugin", "applications-other", NULL } },
	{ "reader",     { "view-paged", "view-list-text", "text-x-generic", "document-properties", NULL } },
	{ "identity",   { "channel-secure", "security-high", "dialog-password", "system-lock-screen", NULL } },
	{ "close",      { "window-close", "gtk-close", "dialog-close", "edit-delete", NULL } },
	/* the rest of the chrome: arrows, the sidebar, the tab strip, the account and
	 * screenshot buttons, the bookmarks toolbar's folders, and the app menu's items */
	{ "prev",       { "pan-start", "go-previous", "arrow-left", NULL } },
	{ "next",       { "pan-end", "go-next", "arrow-right", NULL } },
	{ "down",       { "pan-down", "go-down", "arrow-down", NULL } },
	{ "sidebar",    { "sidebar-show", "view-sidebar-start", "view-dual", "view-list", NULL } },
	{ "history",    { "document-open-recent", "appointment-soon", "view-history", "history", NULL } },
	{ "library",    { "folder-documents", "accessories-dictionary", "folder", NULL } },
	{ "bookmarks",  { "user-bookmarks", "bookmark-new", "starred", NULL } },
	{ "folder",     { "folder", "inode-directory", "folder-documents", NULL } },
	{ "search",     { "edit-find", "system-search", "search", NULL } },
	{ "more",       { "view-more-horizontal", "view-more", "open-menu", NULL } },
	{ "screenshot", { "camera-photo", "applets-screenshooter", "camera-web", NULL } },
	{ "account",    { "avatar-default", "user-info", "system-users", "user-available", NULL } },
	{ "sound",      { "audio-volume-high", "audio-speakers", "audio-volume-medium", NULL } },
	{ "muted",      { "audio-volume-muted", "audio-volume-mute", "audio-volume-low", NULL } },
	{ "translate",  { "preferences-desktop-locale", "config-language", "accessories-dictionary", NULL } },
	{ "window",     { "window-new", "window", "document-new", NULL } },
	{ "private",    { "user-not-tracked", "security-high", "view-private", "dialog-password", NULL } },
	{ "password",   { "dialog-password", "security-high", "channel-secure", NULL } },
	{ "print",      { "document-print", "printer", "gtk-print", NULL } },
	{ "save",       { "document-save", "gtk-save", "drive-harddisk", NULL } },
	{ "zoom_in",    { "zoom-in", "list-add", "gtk-zoom-in", NULL } },
	{ "zoom_out",   { "zoom-out", "list-remove", "gtk-zoom-out", NULL } },
	{ "fullscreen", { "view-fullscreen", "gtk-fullscreen", "zoom-fit-best", NULL } },
	{ "settings",   { "preferences-system", "emblem-system", "gtk-preferences", "applications-system", NULL } },
	{ "tools",      { "applications-utilities", "application-x-executable", "preferences-other", NULL } },
	{ "help",       { "help-browser", "help-about", "dialog-question", "gtk-help", NULL } },
	{ "quit",       { "application-exit", "system-log-out", "gtk-quit", "window-close", NULL } },
	{ "shield",     { "security-medium", "security-high", "channel-secure", "dialog-password", NULL } },
	{ "tag",        { "tag", "edit-tag", "mail-mark-important", "emblem-important", NULL } },
	{ "appearance", { "preferences-desktop-theme", "preferences-color", "color-select", "applications-graphics", NULL } },
	{ "access",     { "preferences-desktop-accessibility", "accessibility", "help-about", NULL } },
	{ "info",       { "dialog-information", "help-about", "info", NULL } },
};
#define NGLYPH   TM_COUNT(glyphs)
#define ICON_CAP 40960              /* one embedded icon, as CSS */
#define SVG_CAP  65536              /* one icon file read */
#define MAX_DEPTH 3                 /* directory levels searched under a theme */

/* the best file found for one glyph so far; rank 0 is a symbolic SVG */
typedef struct {
	char path[PATH_MAX];
	int rank, alias;
	long size;
	int found;
} Hit;

/* the rank of an icon file: symbolic svg 0, svg 1, png 2 */
static int
rank_of(const char *ext, int symbolic)
{
	return !strcmp(ext, "svg") ? (symbolic ? 0 : 1) : 2;
}

/* how well a bitmap's pixel size suits the toolbar (about 22px, so the 16, 22, and 24px
 * sizes are picked and a 64px icon is not squeezed into a small box and blurred) */
static long
suit(long size)
{
	return size <= 0 ? -1000 : -labs(size - 22);
}

/* true if this is a better candidate than the one held */
static int
better(const Hit *h, int rank, int alias, long size)
{
	if (!h->found)
		return 1;
	if (rank != h->rank)
		return rank < h->rank;
	if (alias != h->alias)
		return alias < h->alias;
	return suit(size) > suit(h->size);
}

/* a directory name's pixel size: "48", "48x48", "16x16@2"; "scalable" is large; else 0 */
static long
size_of(const char *n)
{
	if (!strcmp(n, "scalable"))
		return 256;
	if (!isdigit((unsigned char)n[0]))
		return 0;
	return strtol(n, NULL, 10);
}

/* Checks one file against every glyph's names; keeps the best for each. */
static void
check_file(const char *name, const char *path, int symbolic, long size, Hit *hits)
{
	char base[256], ext[8];
	const char *dot = strrchr(name, '.');
	size_t blen;

	if (!dot || (strcmp(dot, ".svg") && strcmp(dot, ".png")))
		return;
	snprintf(ext, sizeof ext, "%s", dot + 1);
	blen = (size_t)(dot - name);
	if (blen >= sizeof base)
		return;
	memcpy(base, name, blen);
	base[blen] = '\0';
	/* "name-symbolic.svg" is a symbolic icon named "name" */
	if (!strcmp(ext, "svg") && blen > 9 && !strcmp(base + blen - 9, "-symbolic")) {
		base[blen - 9] = '\0';
		symbolic = 1;
	}
	for (size_t g = 0; g < NGLYPH; g++)
		for (int a = 0; glyphs[g].names[a]; a++) {
			int rank;

			if (strcmp(base, glyphs[g].names[a]))
				continue;
			rank = rank_of(ext, symbolic);
			if (better(&hits[g], rank, a, size)) {
				Hit *h = &hits[g];

				snprintf(h->path, sizeof h->path, "%s", path);
				h->rank = rank;
				h->alias = a;
				h->size = size;
				h->found = 1;
			}
		}
}

/* Walks one directory of a theme, up to MAX_DEPTH levels deep. */
static void
scan(const char *dir, int depth, int symbolic, long size, Hit *hits)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[PATH_MAX];

		if (e->d_name[0] == '.' || snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
			continue;
		if (e->d_type == DT_DIR) {
			long sz = size_of(e->d_name);

			if (depth < MAX_DEPTH)
				scan(path, depth + 1, symbolic || !strcmp(e->d_name, "symbolic"),
				     sz ? sz : size, hits);
		} else {
			check_file(e->d_name, path, symbolic, size, hits);
		}
	}
	closedir(d);
}

/* adds root/theme to dirs when it fits; returns the new count */
static int
add_dir(char dirs[4][PATH_MAX], int n, const char *root, const char *theme)
{
	return n < 4 && snprintf(dirs[n], PATH_MAX, "%s/%s", root, theme) < PATH_MAX ? n + 1 : n;
}

/* the icon theme's directories, from the user's icon dirs and the system's */
static int
theme_dirs(const TmTheme *t, const char *theme, char dirs[4][PATH_MAX])
{
	char user[PATH_MAX];
	int n = 0;

	if (snprintf(user, sizeof user, "%s/.icons", t->home) < (int)sizeof user)
		n = add_dir(dirs, n, user, theme);
	if (snprintf(user, sizeof user, "%s/.local/share/icons", t->home) < (int)sizeof user)
		n = add_dir(dirs, n, user, theme);
	n = add_dir(dirs, n, "/usr/share/icons", theme);
	n = add_dir(dirs, n, "/usr/local/share/icons", theme);
	return n;
}

/* the Inherits= list of a theme's index.theme, one name per call; 0 when there is none */
static int
inherits(const char *dir, char out[][512], int max)
{
	char path[PATH_MAX], line[512];
	FILE *f;
	int n = 0;

	if (snprintf(path, sizeof path, "%s/index.theme", dir) >= (int)sizeof path ||
	    !(f = fopen(path, "r")))
		return 0;
	while (fgets(line, sizeof line, f) && n == 0) {
		char *p = strstr(line, "Inherits=");
		char *tok;

		if (!p || p != line)
			continue;
		for (tok = strtok(p + 9, ",\r\n"); tok && n < max; tok = strtok(NULL, ",\r\n")) {
			if (snprintf(out[n], 512, "%s", tok) < 512)
				n++;
		}
	}
	fclose(f);
	return n;
}

/* the bundled fallback icons: $THEMER_ICONS, or the installed copy */
static const char *
fallback_dir(void)
{
	const char *env = getenv("THEMER_ICONS");

	return env && *env ? env : SYS_ICONS_DIR;
}

/* Finds the best icon for each glyph. Themes are searched in order: the user's theme,
 * then the themes it inherits from (each followed by its own), then hicolor and
 * Adwaita. A glyph takes the first theme that has it at all. Whatever is still
 * missing comes from the bundled Adwaita icons (icons/ in the source tree). */
static void
resolve(const TmTheme *t, Hit *hits)
{
	char queue[16][512], seen[16][512];
	int nq = 0, ns = 0;

	snprintf(queue[nq++], 512, "%s", t->icons);
	snprintf(queue[nq++], 512, "hicolor");
	snprintf(queue[nq++], 512, "Adwaita");
	for (int qi = 0; qi < nq; qi++) {
		char dirs[4][PATH_MAX], names[8][512];
		Hit cur[NGLYPH];
		int nd, ninh = 0, all = 1, at = qi + 1;

		for (int k = 0; k < ns; k++)
			if (!strcmp(seen[k], queue[qi]))
				goto next;
		if (snprintf(seen[ns], 512, "%s", queue[qi]) < 512)
			ns++;
		memset(cur, 0, sizeof cur);
		nd = theme_dirs(t, queue[qi], dirs);
		for (int k = 0; k < nd; k++) {
			scan(dirs[k], 0, 0, 0, cur);
			if (!ninh)
				ninh = inherits(dirs[k], names, 8);
		}
		for (size_t g = 0; g < NGLYPH; g++) {
			if (!hits[g].found && cur[g].found)
				hits[g] = cur[g];
			if (!hits[g].found)
				all = 0;
		}
		if (all)
			return;
		/* the parents go right after this theme, ahead of hicolor and Adwaita */
		for (int k = 0; k < ninh && nq < 16; k++) {
			memmove(queue[at + 1], queue[at], (size_t)(nq - at) * sizeof queue[0]);
			if (snprintf(queue[at], 512, "%s", names[k]) < 512) {
				at++;
				nq++;
			} else {
				memmove(queue[at], queue[at + 1], (size_t)(nq - at) * sizeof queue[0]);
			}
		}
	next:;
	}
	{
		Hit cur[NGLYPH];

		memset(cur, 0, sizeof cur);
		scan(fallback_dir(), 0, 1, 0, cur);
		for (size_t g = 0; g < NGLYPH; g++)
			if (!hits[g].found && cur[g].found)
				hits[g] = cur[g];
	}
}

/* Makes an SVG one color: every fill, stroke, and stop-color, as an attribute or inside
 * style, becomes ink. none and transparent stay, so cut-outs keep working; gradient and
 * url() paints become ink too. Returns the new length, or 0 if the result does not fit. */
static size_t
monochrome(const char *in, size_t len, const char *ink, char *out, size_t cap)
{
	static const char *const keys[] = { "fill", "stroke", "stop-color" };
	size_t i = 0, o = 0;

	while (i < len) {
		int hit = 0;

		for (size_t k = 0; k < sizeof keys / sizeof keys[0] && !hit; k++) {
			size_t kl = strlen(keys[k]), sep, j, end, vl;
			const char *v;

			if (strncmp(in + i, keys[k], kl))
				continue;
			if (in[i + kl] == ':')
				sep = 1;
			else if (in[i + kl] == '=' && i + kl + 1 < len && in[i + kl + 1] == '"')
				sep = 2;
			else
				continue;
			j = i + kl + sep;
			for (end = j; end < len && in[end] != ';' && in[end] != '"' && in[end] != '\''; end++)
				;
			v = in + j;
			vl = end - j;
			if (o + kl + sep + 8 > cap)
				return 0;
			memcpy(out + o, in + i, kl + sep);
			o += kl + sep;
			if ((vl == 4 && !strncmp(v, "none", 4)) || (vl == 11 && !strncmp(v, "transparent", 11))) {
				memcpy(out + o, v, vl);
				o += vl;
			} else {
				out[o++] = '#';
				memcpy(out + o, ink, 6);
				o += 6;
			}
			i = end;
			hit = 1;
		}
		if (!hit) {
			if (o + 1 >= cap)
				return 0;
			out[o++] = in[i++];
		}
	}
	out[o] = '\0';
	return o;
}

/* The root svg element of an icon is rewritten to one canonical form, the one Adwaita's
 * icons already use: a viewBox (taken from the theme's, or from its width and height) and
 * an intrinsic size of 16px. Other attributes are kept. Returns the new length, or 0 to
 * leave the icon as it is. */
static size_t
normalize_svg(const char *in, char *out, size_t cap)
{
	const char *svg = strstr(in, "<svg"), *end, *p;
	char vb[128] = "", w[32] = "", h[32] = "";
	size_t o, tail;

	if (!svg || !(end = strchr(svg, '>')))
		return 0;
	/* the root attributes we need: viewBox, or width and height to derive one */
	for (p = svg + 4; p < end; p++) {
		const char *name, *eq;
		size_t nl;
		char *dst = NULL;

		while (p < end && isspace((unsigned char)*p))
			p++;
		name = p;
		eq = strchr(p, '=');
		if (!eq || eq > end || eq[1] == '\0')
			break;
		nl = (size_t)(eq - name);
		if (eq + 1 >= end || (eq[1] != '"' && eq[1] != '\''))
			break;
		if (nl == 7 && !strncmp(name, "viewBox", 7))
			dst = vb;
		else if (nl == 5 && !strncmp(name, "width", 5))
			dst = w;
		else if (nl == 6 && !strncmp(name, "height", 6))
			dst = h;
		{
			const char *q = eq + 2, *close = strchr(q, eq[1]);

			if (!close || close > end)
				break;
			if (dst)
				snprintf(dst, dst == vb ? sizeof vb : sizeof w, "%.*s", (int)(close - q), q);
			p = close;
		}
	}
	if (!vb[0]) {
		long wv = strtol(w, NULL, 10), hv = strtol(h, NULL, 10);

		if (wv <= 0 || hv <= 0)
			return 0;
		snprintf(vb, sizeof vb, "0 0 %ld %ld", wv, hv);
	}
	/* rebuild the root tag: keep its other attributes, set viewBox and the 16px size */
	o = (size_t)(svg - in);
	if (o + 4 + 96 + strlen(vb) >= cap)
		return 0;
	memcpy(out, in, o);
	o += snprintf(out + o, cap - o, "<svg");
	for (p = svg + 4; p < end; ) {
		const char *eq, *close;
		size_t nl;

		while (p < end && isspace((unsigned char)*p))
			p++;
		if (p >= end)
			break;
		eq = strchr(p, '=');
		if (!eq || eq > end || eq[1] == '\0')
			break;
		nl = (size_t)(eq - p);
		close = strchr(eq + 2, eq[1]);
		if (!close || close > end)
			break;
		if (!((nl == 7 && !strncmp(p, "viewBox", 7)) || (nl == 5 && !strncmp(p, "width", 5)) ||
		      (nl == 6 && !strncmp(p, "height", 6)))) {
			size_t l = (size_t)(close + 1 - p);

			if (o + 1 + l >= cap)
				return 0;
			out[o++] = ' ';
			memcpy(out + o, p, l);
			o += l;
		}
		p = close + 1;
	}
	o += snprintf(out + o, cap - o, " viewBox=\"%s\" width=\"16px\" height=\"16px\"", vb);
	tail = strlen(end);
	if (o + tail + 1 >= cap)
		return 0;
	memcpy(out + o, end, tail + 1);
	return o + tail;
}

/* the value for an icon that is not there: no image */
static int
icon_none(char *out, size_t n)
{
	snprintf(out, n, "none");
	return 0;
}

/* Writes one icon as a CSS url() value: a base64 data URI. With an ink, a symbolic SVG
 * is made that ink (any SVG when recolor_all is set); PNG and other SVG keep their own
 * colors. Every SVG has its root normalized (see normalize_svg). A missing or too large
 * icon becomes "none". */
static int
icon_css(const Hit *h, const char *ink, int recolor_all, char *out, size_t n)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	static char file[SVG_CAP], mono[SVG_CAP], scaled[SVG_CAP];
	const char *src = file;
	char head[80];
	size_t len, o = 0, i;
	FILE *f;

	if (!h->found || !(f = fopen(h->path, "rb")))
		return icon_none(out, n);
	len = fread(file, 1, sizeof file - 1, f);
	fclose(f);
	if (len == sizeof file - 1)
		return icon_none(out, n);
	file[len] = '\0';
	if (ink && (h->rank == 0 || (h->rank == 1 && recolor_all))) {
		len = monochrome(file, len, ink, mono, sizeof mono);
		if (!len)
			return icon_none(out, n);
		src = mono;
	}
	if (h->rank != 2) {
		size_t with = normalize_svg(src, scaled, sizeof scaled);

		if (with) {
			len = with;
			src = scaled;
		}
	}
	snprintf(head, sizeof head, "url(\"data:image/%s;base64,", h->rank == 2 ? "png" : "svg+xml");
	if (strlen(head) + 4 * ((len + 2) / 3) + 3 > n) {
		fprintf(stderr, "themer: firefox: icon %s is too large to embed; left out\n", h->path);
		return icon_none(out, n);
	}
	memcpy(out, head, strlen(head));
	o = strlen(head);
	for (i = 0; i < len; i += 3) {
		unsigned v = (unsigned char)src[i] << 16;

		if (i + 1 < len) v |= (unsigned char)src[i + 1] << 8;
		if (i + 2 < len) v |= (unsigned char)src[i + 2];
		out[o++] = b64[(v >> 18) & 63];
		out[o++] = b64[(v >> 12) & 63];
		out[o++] = i + 1 < len ? b64[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < len ? b64[v & 63] : '=';
	}
	memcpy(out + o, "\")", 3);
	return 0;
}

#endif
