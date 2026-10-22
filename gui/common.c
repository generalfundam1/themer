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

		if (type != 0xfffd0002 || pos + 36 > (unsigned)len)
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
		    best + 36 + 4 * (long)n > (unsigned)len) {
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
