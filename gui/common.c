/* common.c: see common.h */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "common.h"

/* ---- Xcursor files ---- */


/* little-endian 32-bit word at p */
static unsigned
le32(const unsigned char *p)
{
	return p[0] | (p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* the value of Inherits= in an index.theme, first theme only */
static int
theme_inherits(const char *dir, char *out, size_t n)
{
	char p[PATH_MAX], line[512];
	FILE *f;
	int found = 0;

	if (snprintf(p, sizeof p, "%s/index.theme", dir) >= (int)sizeof p || !(f = fopen(p, "r")))
		return -1;
	while (!found && fgets(line, sizeof line, f))
		if (!strncmp(line, "Inherits=", 9)) {
			size_t len = strcspn(line + 9, ",\r\n");

			snprintf(out, n, "%.*s", (int)len, line + 9);
			found = out[0] != '\0';
		}
	fclose(f);
	return found ? 0 : -1;
}

/* An Xcursor file: "Xcur", header size, version, ntoc, then the toc (type, subtype
 * = nominal size, position) and the image chunks (header size, type, subtype,
 * version, width, height, xhot, yhot, delay, pixels). The first image of the
 * best-matching size is taken. */
static int
xcursor_read(const char *path, int size, unsigned *pix, int maxpix, int *w, int *h)
{
	unsigned char *buf;
	long len;
	unsigned ntoc, best = 0, bestsize = 0, i;
	FILE *f = fopen(path, "rb");

	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len < 16 || len > 16 * 1024 * 1024 || !(buf = malloc(len))) {
		fclose(f);
		return -1;
	}
	if (fread(buf, 1, len, f) != (size_t)len || memcmp(buf, "Xcur", 4)) {
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);
	ntoc = le32(buf + 12);
	for (i = 0; i < ntoc && 16 + 12 * (long)i + 12 <= len; i++) {
		const unsigned char *e = buf + 16 + 12 * i;
		unsigned type = le32(e), sub = le32(e + 4), pos = le32(e + 8);

		/* compared without adding to pos, which comes from the file and could wrap */
		if (type != 0xfffd0002 || len < 36 || pos > (unsigned)len - 36)
			continue;
		if (!bestsize || abs((int)sub - size) < abs((int)bestsize - size)) {
			bestsize = sub;
			best = pos;
		}
	}
	if (!bestsize) {
		free(buf);
		return -1;
	}
	{
		const unsigned char *c = buf + best;
		unsigned cw = le32(c + 16), ch = le32(c + 20), n = cw * ch;

		if (!cw || !ch || cw > 512 || ch > 512 || (int)n > maxpix ||
		    (long)best + 36 + 4 * (long)n > (long)len) {
			free(buf);
			return -1;
		}
		for (i = 0; i < n; i++)
			pix[i] = le32(c + 36 + 4 * i);
		*w = (int)cw;
		*h = (int)ch;
	}
	free(buf);
	return 0;
}

int
gui_cursor_image(const char *theme, const char *name, int size, unsigned *pix,
                 int maxpix, int *w, int *h)
{
	char cur[NAME_LEN], dir[PATH_MAX], p[PATH_MAX];

	snprintf(cur, sizeof cur, "%s", theme);
	for (int depth = 0; depth < 8; depth++) {
		const char *alt = name;

		if (icon_theme_dir(cur, dir, sizeof dir) < 0)
			return -1;
		/* name may list alternatives, "a|b": themes differ in what they call things */
		while (alt) {
			const char *bar = strchr(alt, '|');
			int len = bar ? (int)(bar - alt) : (int)strlen(alt);

			if (snprintf(p, sizeof p, "%s/cursors/%.*s", dir, len, alt) < (int)sizeof p &&
			    xcursor_read(p, size, pix, maxpix, w, h) == 0)
				return 0;
			alt = bar ? bar + 1 : NULL;
		}
		if (theme_inherits(dir, cur, sizeof cur) < 0)
			return -1;
	}
	return -1;
}

const char *const gui_preview_icons[][2] = {
	{ "folder", "folder" }, { "text-x-generic", "text" }, { "image-x-generic", "image" },
	{ "audio-x-generic", "audio" }, { "video-x-generic", "video" },
	{ "application-x-executable", "program" }, { "package-x-generic", "package" },
	{ "user-home", "home" }, { "user-trash", "trash" }, { "drive-harddisk", "disk" },
	{ "system-file-manager", "files" }, { "utilities-terminal", "terminal" },
	{ "web-browser", "browser" }, { "preferences-system", "settings" },
	{ "dialog-information", "info" }, { "dialog-warning", "warning" },
	{ "dialog-error", "error" }, { "edit-copy", "copy" }, { "document-save", "save" },
	{ "view-refresh", "refresh" },
	{ NULL, NULL },
};

const char *const gui_preview_cursors[][2] = {
	{ "left_ptr|default", "arrow" }, { "hand2|pointer|hand1", "link" }, { "xterm|text", "text" },
	{ "watch|wait", "busy" }, { "left_ptr_watch|progress", "working" }, { "crosshair|cross", "crosshair" },
	{ "fleur|move|all-scroll", "move" }, { "sb_h_double_arrow|ew-resize|col-resize", "resize" },
	{ "sb_v_double_arrow|ns-resize|row-resize", "resize" },
	{ "question_arrow|help|whats_this", "help" }, { "not-allowed|crossed_circle|X_cursor", "not allowed" },
	{ NULL, NULL },
};


void
gui_scheme(const Config *c, Scheme *s)
{
	theme_scheme(c, s);
}

const char *const gui_sample_lines[] = {
	"user@host:~/src/themer $ make",
	"cc -std=c99 -Wall -Wextra -Os -c main.c",
	"main.c:12:3: error: expected ';' before 'return'",
	"warning: unused variable 'ok' [-Wunused-variable]",
	"int main(void) { for (int i = 0; i < 16; i++) ... }",
	"\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90 \xe2\x96\x88\xe2\x96\x93\xe2\x96\x92\xe2\x96\x91 0O 1lI |!  {}[]()<>",
};
const int gui_nsample_lines = (int)(sizeof gui_sample_lines / sizeof gui_sample_lines[0]);

/* ---- an icon of the icon theme, found the way the toolkits find it ----
 * The freedesktop lookup: in the theme, an icon in a directory made for the
 * size; failing that, the one whose size is closest; then the same in each theme
 * it inherits, and hicolor last. Each theme's directory list (index.theme) is
 * read once and kept. */

#define ICON_THEMES 12
#define ICON_DIRS   768

typedef struct { char dir[96]; int size, min, max, threshold, scalable; } IconDir;
typedef struct {
	char name[NAME_LEN], base[PATH_MAX], inherits[512];
	IconDir *d;
	int nd, ok;
} IconTheme;

static IconTheme icon_cache[ICON_THEMES];
static int icon_ncache;

static IconTheme *
icon_theme_load(const char *name)
{
	char idx[PATH_MAX + 16], *line = NULL, sect[96] = "";
	size_t cap = 0;
	IconTheme *t;
	FILE *f;

	for (int i = 0; i < icon_ncache; i++)
		if (!strcmp(icon_cache[i].name, name))
			return icon_cache[i].ok ? &icon_cache[i] : NULL;
	if (icon_ncache == ICON_THEMES)
		return NULL;
	t = &icon_cache[icon_ncache++];
	memset(t, 0, sizeof *t);
	snprintf(t->name, sizeof t->name, "%s", name);
	if (icon_theme_dir(name, t->base, sizeof t->base) < 0)
		return NULL;
	snprintf(idx, sizeof idx, "%s/index.theme", t->base);
	if (!(f = fopen(idx, "r")) || !(t->d = calloc(ICON_DIRS, sizeof *t->d))) {
		if (f)
			fclose(f);
		return NULL;
	}
	while (getline(&line, &cap, f) > 0) {
		char *v;
		IconDir *d = NULL;

		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') {
			snprintf(sect, sizeof sect, "%.*s", (int)strcspn(line + 1, "]"), line + 1);
			continue;
		}
		if (!(v = strchr(line, '=')))
			continue;
		*v++ = '\0';
		if (!strcmp(sect, "Icon Theme")) {
			if (!strcmp(line, "Inherits"))
				snprintf(t->inherits, sizeof t->inherits, "%s", v);
			else if (!strcmp(line, "Directories") || !strcmp(line, "ScaledDirectories"))
				for (char *tok = strtok(v, ","); tok && t->nd < ICON_DIRS; tok = strtok(NULL, ",")) {
					snprintf(t->d[t->nd].dir, sizeof t->d[t->nd].dir, "%s", tok);
					t->d[t->nd].threshold = 2;
					t->nd++;
				}
			continue;
		}
		for (int i = 0; i < t->nd && !d; i++)
			if (!strcmp(t->d[i].dir, sect))
				d = &t->d[i];
		if (!d)
			continue;
		if (!strcmp(line, "Size"))
			d->size = atoi(v);
		else if (!strcmp(line, "MinSize"))
			d->min = atoi(v);
		else if (!strcmp(line, "MaxSize"))
			d->max = atoi(v);
		else if (!strcmp(line, "Threshold"))
			d->threshold = atoi(v);
		else if (!strcmp(line, "Type"))
			d->scalable = !strcmp(v, "Scalable");
		else if (!strcmp(line, "Scale") && atoi(v) > 1)
			d->size = 0;            /* a HiDPI copy: the 1x one serves the preview */
	}
	free(line);
	fclose(f);
	t->ok = 1;
	return t;
}

/* how far a directory is from the size (0: made for it) */
static int
icon_distance(const IconDir *d, int size)
{
	int lo = d->scalable ? (d->min ? d->min : d->size) : d->size - d->threshold;
	int hi = d->scalable ? (d->max ? d->max : d->size) : d->size + d->threshold;

	if (!d->size)
		return 1 << 20;
	if (size >= lo && size <= hi)
		return 0;
	return size < lo ? lo - size : size - hi;
}

/* the icon in one theme: exact first, else the closest */
static int
icon_in_theme(const IconTheme *t, const char *name, int size, char *out, size_t n)
{
	static const char *const ext[] = { "svg", "png", "xpm" };
	int best = -1;
	char path[PATH_MAX];

	for (int i = 0; i < t->nd; i++) {
		int dist = icon_distance(&t->d[i], size);

		if (best >= 0 && dist >= best)
			continue;
		for (size_t e = 0; e < sizeof ext / sizeof ext[0]; e++) {
			if (snprintf(path, sizeof path, "%s/%s/%s.%s", t->base, t->d[i].dir, name, ext[e]) >= (int)sizeof path ||
			    access(path, R_OK))
				continue;
			snprintf(out, n, "%s", path);
			best = dist;
			break;
		}
		if (best == 0)
			break;
	}
	return best >= 0 ? 0 : -1;
}

int
gui_icon_path(const char *theme, const char *name, int size, char *out, size_t n)
{
	char queue[16][NAME_LEN];
	int nq = 0;

	snprintf(queue[nq++], NAME_LEN, "%s", theme);
	for (int q = 0; q < nq; q++) {
		IconTheme *t = icon_theme_load(queue[q]);
		char inh[512];

		if (!t)
			continue;
		if (icon_in_theme(t, name, size, out, n) == 0)
			return 0;
		snprintf(inh, sizeof inh, "%s", t->inherits);
		for (char *tok = strtok(inh, ","); tok && nq < 15; tok = strtok(NULL, ",")) {
			int dup = 0;

			while (*tok == ' ')
				tok++;
			for (int k = 0; k < nq; k++)
				dup |= !strcmp(queue[k], tok);
			if (!dup && strcmp(tok, "hicolor"))
				snprintf(queue[nq++], NAME_LEN, "%s", tok);
		}
	}
	/* hicolor is every theme's last parent */
	{
		IconTheme *h = icon_theme_load("hicolor");

		return h && icon_in_theme(h, name, size, out, n) == 0 ? 0 : -1;
	}
}

/* the bytes of file path, at most max of them, NUL-terminated; NULL on failure */
static char *
slurp(const char *path, size_t max, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *s = NULL;
	size_t n = 0, cap = 0, r;

	if (!f)
		return NULL;
	do {
		char *t;

		if (n + 8192 + 1 > cap) {
			if (!(t = realloc(s, cap = cap ? cap * 2 : 16384))) {
				free(s);
				fclose(f);
				return NULL;
			}
			s = t;
		}
		r = fread(s + n, 1, cap - n - 1, f);
		n += r;
	} while (r > 0 && n < max);
	fclose(f);
	if (!s)
		return NULL;
	s[n] = '\0';
	*len = n;
	return s;
}

/* svg with every "#rrggbb" color value (after a quote, colon or space) made
 * #fg: a symbolic icon drawn in the text color, as GTK draws it */
static char *
recolor(char *svg, size_t *len, const char *fg)
{
	char *o = malloc(*len * 2 + 1), *w = o;

	if (!o)
		return svg;
	for (const char *p = svg; *p; ) {
		if (*p == '#' && p > svg && (p[-1] == '"' || p[-1] == ':' || p[-1] == ' ') &&
		    strspn(p + 1, "0123456789abcdefABCDEF") >= 6) {
			w += sprintf(w, "#%.6s", fg);
			p += 7;
		} else {
			*w++ = *p++;
		}
	}
	*w = '\0';
	*len = (size_t)(w - o);
	free(svg);
	return o;
}

char *
gui_icon_load(const char *theme, const char *name, int size, const char *fg, size_t *len, const char **mime)
{
	char path[PATH_MAX], sym[128], *data;
	const char *ext;
	int symbolic = 0;

	if (gui_icon_path(theme, name, size, path, sizeof path) < 0) {
		snprintf(sym, sizeof sym, "%s-symbolic", name);
		if (gui_icon_path(theme, sym, size, path, sizeof path) < 0)
			return NULL;
		symbolic = 1;
	}
	if (!(data = slurp(path, 4 * 1024 * 1024, len)))
		return NULL;
	ext = strrchr(path, '.');
	*mime = ext && !strcmp(ext, ".svg") ? "image/svg+xml" : ext && !strcmp(ext, ".png") ? "image/png" : "image/x-xpixmap";
	symbolic |= strstr(path, "-symbolic.") != NULL;
	if (symbolic && fg && !strcmp(*mime, "image/svg+xml"))
		data = recolor(data, len, fg);
	return data;
}
