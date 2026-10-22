/* icons.c: the installed icon and cursor themes. A theme is a directory with an
 * index.theme under one of the icon directories; a cursor theme is one that also
 * holds a cursors/ directory. The same lists serve the TUI, the CLI, and the
 * graphical front ends. */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "themer.h"

const char *const hinting_values[4] = { "none", "slight", "medium", "full" };
const char *const rgba_values[5] = { "none", "rgb", "bgr", "vrgb", "vbgr" };

/* the icon directories, most specific first; n is how many were filled */
static int
icon_dirs(char dirs[][PATH_MAX], int max)
{
	int n = 0;

	if (n < max && path_under_home(dirs[n], PATH_MAX, ".icons") == 0)
		n++;
	if (n < max && path_under_home(dirs[n], PATH_MAX, ".local/share/icons") == 0)
		n++;
	if (n < max)
		snprintf(dirs[n++], PATH_MAX, "/usr/share/icons");
	if (n < max)
		snprintf(dirs[n++], PATH_MAX, "/usr/local/share/icons");
	return n;
}

static int
cmp_str(const void *a, const void *b)
{
	return strcmp(a, b);
}

static int
add_dir(const char *dir, char names[][NAME_LEN], int n, int max, int cursors)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return n;
	while ((e = readdir(d)) && n < max) {
		char p[PATH_MAX];
		int dup = 0;

		if (e->d_name[0] == '.' || strlen(e->d_name) >= NAME_LEN ||
		    !strcmp(e->d_name, "default") ||
		    snprintf(p, sizeof p, "%s/%s/%s", dir, e->d_name,
		             cursors ? "cursors" : "index.theme") >= (int)sizeof p ||
		    access(p, R_OK))
			continue;
		/* icon themes only: hicolor and locolor are fallbacks, not choices */
		if (!cursors && (!strcmp(e->d_name, "hicolor") || !strcmp(e->d_name, "locolor")))
			continue;
		for (int i = 0; i < n; i++)
			if (!strcmp(names[i], e->d_name))
				dup = 1;
		if (!dup)
			snprintf(names[n++], NAME_LEN, "%.*s", NAME_LEN - 1, e->d_name);
	}
	closedir(d);
	return n;
}

int
icon_themes_scan(char names[][NAME_LEN], int max, int cursors)
{
	char dirs[4][PATH_MAX];
	int nd = icon_dirs(dirs, 4), n = 0;

	for (int i = 0; i < nd; i++)
		n = add_dir(dirs[i], names, n, max, cursors);
	qsort(names, n, NAME_LEN, cmp_str);
	return n;
}

int
icon_theme_dir(const char *name, char *out, size_t n)
{
	char dirs[4][PATH_MAX];
	int nd = icon_dirs(dirs, 4);

	for (int i = 0; i < nd; i++) {
		char p[PATH_MAX];

		if (snprintf(p, sizeof p, "%s/%s", dirs[i], name) >= (int)sizeof p)
			continue;
		if (!access(p, R_OK)) {
			snprintf(out, n, "%s", p);
			return 0;
		}
	}
	return -1;
}
