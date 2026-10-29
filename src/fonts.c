/* fonts.c: installed font families.
 * Uses fc-list when it is available, and also reads the family name from every
 * .ttf, .otf, or .ttc under the standard font directories. The list is complete
 * without fontconfig's tools. */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "themer.h"

#define MAXF     8192
#define MAXDEPTH 8

static char found[MAXF][NAME_LEN];
static int nfound;

static unsigned
be16(const unsigned char *p)
{
	return ((unsigned)p[0] << 8) | p[1];
}

static unsigned long
be32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
	       ((unsigned long)p[2] << 8) | p[3];
}

static int
read_at(FILE *f, long off, void *buf, size_t n)
{
	return fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}

/* a name is usable if it is printable ASCII; anything else is skipped */
static int
ascii16(const unsigned char *s, size_t len, char *out, size_t n)
{
	size_t k = 0;

	for (size_t i = 0; i + 1 < len; i += 2) {
		unsigned c = be16(s + i);

		if (c < 32 || c > 126)
			return 0;
		if (k + 1 < n)
			out[k++] = (char)c;
	}
	out[k] = '\0';
	return k > 0;
}

static int
ascii8(const unsigned char *s, size_t len, char *out, size_t n)
{
	size_t k = 0;

	for (size_t i = 0; i < len; i++) {
		if (s[i] < 32 || s[i] > 126)
			return 0;
		if (k + 1 < n)
			out[k++] = (char)s[i];
	}
	out[k] = '\0';
	return k > 0;
}

/* reads nameID 1 (family) from the sfnt "name" table; first font of a collection */
static int
sfnt_family(FILE *f, char *out, size_t n)
{
	unsigned char b[16], r[16];
	unsigned long noff = 0, nlen = 0;
	unsigned count, stroff, i, pass;
	unsigned char *name;
	long base = 0;

	if (!read_at(f, 0, b, 4))
		return -1;
	if (!memcmp(b, "ttcf", 4)) {
		if (!read_at(f, 12, b, 4))
			return -1;
		base = (long)be32(b);
	}
	if (!read_at(f, base, b, 12))
		return -1;
	for (i = 0; i < be16(b + 4) && i < 512; i++) {
		if (!read_at(f, base + 12 + 16L * i, r, 16))
			return -1;
		if (!memcmp(r, "name", 4)) {
			noff = be32(r + 8);
			nlen = be32(r + 12);
			break;
		}
	}
	if (!noff || nlen < 6 || nlen > (1UL << 20))
		return -1;
	if (!(name = malloc(nlen)) || !read_at(f, (long)noff, name, nlen)) {
		free(name);
		return -1;
	}

	count = be16(name + 2);
	stroff = be16(name + 4);
	/* Windows platform names first, then Mac */
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < count && 6 + 12 * (size_t)i + 12 <= nlen; i++) {
			unsigned char *rec = name + 6 + 12 * i;
			unsigned plat = be16(rec), id = be16(rec + 6);
			unsigned len = be16(rec + 8), off = be16(rec + 10);
			const unsigned char *s;
			int ok;

			if (id != 1 || (plat != 3 && plat != 1) || (pass == 0) != (plat == 3))
				continue;
			if (stroff + off + len > nlen)
				continue;
			s = name + stroff + off;
			ok = plat == 3 ? ascii16(s, len, out, n) : ascii8(s, len, out, n);
			if (ok) {
				free(name);
				return 0;
			}
		}
	free(name);
	return -1;
}

static int
is_font_file(const char *n)
{
	size_t l = strlen(n);

	return l > 4 && (!strcasecmp(n + l - 4, ".ttf") || !strcasecmp(n + l - 4, ".otf") ||
	                 !strcasecmp(n + l - 4, ".ttc"));
}

static void
add_found(const char *s)
{
	if (nfound < MAXF && s[0])
		snprintf(found[nfound++], NAME_LEN, "%s", s);
}

static void
scan_dir(const char *dir, int depth)
{
	struct dirent *e;
	DIR *d;

	if (depth > MAXDEPTH || !(d = opendir(dir)))
		return;
	while ((e = readdir(d))) {
		char p[PATH_MAX], fam[NAME_LEN];
		FILE *f;

		if (e->d_name[0] == '.')
			continue;
		snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
		if (e->d_type == DT_DIR) {
			scan_dir(p, depth + 1);
		} else if (is_font_file(e->d_name) && (f = fopen(p, "rb"))) {
			if (sfnt_family(f, fam, sizeof fam) == 0)
				add_found(fam);
			fclose(f);
		}
	}
	closedir(d);
}

static int
cmp_name(const void *a, const void *b)
{
	return strcmp(a, b);
}

/* fills names with sorted, unique family names. returns the count */
static int scan_all(char names[][NAME_LEN], int max);
static int scan_mono(char names[][NAME_LEN], int max);

/* The lists, kept for a while: a front end that stays open asks again on every
 * refresh (themer-web on every request for the state), and each ask runs fc-list.
 * A font installed meanwhile shows up within FONT_TTL seconds. */
#define FONT_TTL 30
#define FONT_MAX 8192
static int
cached(char names[][NAME_LEN], int max, int mono)
{
	static char list[2][FONT_MAX][NAME_LEN];
	static int count[2] = { -1, -1 };
	static struct timespec when[2];
	struct timespec now;
	int n;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (count[mono] < 0 || now.tv_sec - when[mono].tv_sec >= FONT_TTL) {
		count[mono] = mono ? scan_mono(list[mono], FONT_MAX) : scan_all(list[mono], FONT_MAX);
		when[mono] = now;
	}
	n = count[mono] < max ? count[mono] : max;
	memcpy(names, list[mono], (size_t)n * NAME_LEN);
	return n;
}

int
fonts_scan(char names[][NAME_LEN], int max)
{
	return cached(names, max, 0);
}

int
fonts_mono(char names[][NAME_LEN], int max)
{
	return cached(names, max, 1);
}

static int
scan_all(char names[][NAME_LEN], int max)
{
	static const char *dirs[] = { "/usr/share/fonts", "/usr/local/share/fonts" };
	char line[512], p[PATH_MAX];
	FILE *fc;
	int i, n = 0;

	nfound = 0;
	if ((fc = popen("fc-list : family 2>/dev/null", "r"))) {
		while (fgets(line, sizeof line, fc)) {
			char *c;

			/* a line can list several family names, separated by commas */
			line[strcspn(line, "\r\n")] = '\0';
			for (c = strtok(line, ","); c; c = strtok(NULL, ","))
				add_found(c);
		}
		pclose(fc);
	}
	for (size_t k = 0; k < TM_COUNT(dirs); k++)
		scan_dir(dirs[k], 0);
	if (path_under_home(p, sizeof p, ".local/share/fonts") == 0)
		scan_dir(p, 0);
	if (path_under_home(p, sizeof p, ".fonts") == 0)
		scan_dir(p, 0);

	qsort(found, nfound, NAME_LEN, cmp_name);
	for (i = 0; i < nfound && n < max; i++)
		if (!n || strcmp(names[n - 1], found[i]))
			snprintf(names[n++], NAME_LEN, "%.*s", NAME_LEN - 1, found[i]);
	return n;
}

/* collects families from one fc-list command, one family set per line */
static int
collect_fc(const char *cmd)
{
	char line[512];
	FILE *fc = popen(cmd, "r");

	if (!fc)
		return 0;
	while (fgets(line, sizeof line, fc)) {
		char *tok;

		line[strcspn(line, "\r\n")] = '\0';
		for (tok = strtok(line, ","); tok; tok = strtok(NULL, ","))
			add_found(tok);
	}
	pclose(fc);
	return 1;
}

/* monospace families, from fontconfig's spacing property. returns 0 when
 * fontconfig is missing or reports none, so callers can fall back. */
static int
scan_mono(char names[][NAME_LEN], int max)
{
	int i, n = 0;

	nfound = 0;
	collect_fc("fc-list :spacing=mono : family 2>/dev/null");
	qsort(found, nfound, NAME_LEN, cmp_name);
	for (i = 0; i < nfound && n < max; i++)
		if (!n || strcmp(names[n - 1], found[i]))
			snprintf(names[n++], NAME_LEN, "%.*s", NAME_LEN - 1, found[i]);
	return n;
}
