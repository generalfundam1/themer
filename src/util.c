/* util.c: paths, plain file writes, and small helpers shared by the host. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "themer.h"

int
valid_name(const char *s)
{
	if (!*s || *s == '.')
		return 0;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
			return 0;
	return 1;
}

/* A name typed by a person (a font family, an icon or cursor theme): not empty,
 * shorter than max, and without control characters, so it cannot carry a newline
 * into the config files it is written to. */
int
valid_text(const char *s, size_t max)
{
	size_t n = 0;

	if (!s || !*s)
		return 0;
	for (; *s; s++, n++)
		if ((unsigned char)*s < 0x20 || *s == 0x7f)
			return 0;
	return n < max;
}

int
path_under_home(char *out, size_t n, const char *rel)
{
	const char *home = getenv("HOME");

	if (!home)
		return -1;
	return snprintf(out, n, "%s/%s", home, rel) < (int)n ? 0 : -1;
}

/* creates a directory and any missing parents */
int
mkdirs(const char *path)
{
	char buf[PATH_MAX];

	if (strlen(path) >= sizeof buf)
		return -1;
	strcpy(buf, path);
	for (char *p = buf + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(buf, 0755) < 0 && errno != EEXIST)
			return -1;
		*p = '/';
	}
	if (mkdir(buf, 0755) < 0 && errno != EEXIST)
		return -1;
	return 0;
}

static int
ensure_parent(const char *path)
{
	char dir[PATH_MAX];
	const char *slash = strrchr(path, '/');

	if (!slash || (size_t)(slash - path) >= sizeof dir)
		return -1;
	memcpy(dir, path, slash - path);
	dir[slash - path] = '\0';
	return mkdirs(dir);
}

int
write_gen(const char *path, EmitFn fn, const Config *c, const Scheme *s)
{
	AFile a;
	FILE *f;

	if (!(f = afile_open(&a, path, 0)))
		return -1;
	fn(f, c, s);
	a.sync = 1;              /* the state file and the few others written this way */
	return afile_commit(&a);
}

int
write_bytes(const char *path, const void *buf, size_t len)
{
	AFile a;
	FILE *f;

	if (!(f = afile_open(&a, path, 1)))
		return -1;
	if (fwrite(buf, 1, len, f) != len) {
		afile_abort(&a);
		return -1;
	}
	return afile_commit(&a);
}

/* Set key=val in a shell-style file, replacing the first matching line or appending. */
int
kv_set_file(const char *path, const char *key, const char *val)
{
	char tmp[PATH_MAX], *line = NULL;
	size_t cap = 0, klen = strlen(key);
	int done = 0;
	FILE *in, *out;

	if (snprintf(tmp, sizeof tmp, "%s.themer", path) >= (int)sizeof tmp)
		return -1;
	in = fopen(path, "r");
	if (!(out = fopen(tmp, "w"))) {
		if (in)
			fclose(in);
		return -1;
	}
	while (in && getline(&line, &cap, in) > 0) {
		if (!done && !strncmp(line, key, klen) && line[klen] == '=') {
			fprintf(out, "%s=%s\n", key, val);
			done = 1;
		} else {
			fputs(line, out);
		}
	}
	if (!done)
		fprintf(out, "%s=%s\n", key, val);
	free(line);
	if (in)
		fclose(in);
	if (fclose(out) != 0)
		return -1;
	return rename(tmp, path);
}

int
run(const char *cmd)
{
	return system(cmd) == 0 ? 0 : -1;
}

int
is_root(void)
{
	return getuid() == 0;
}

/* ---- atomic replacement ---- */

FILE *
afile_open(AFile *a, const char *path, int binary)
{
	char real[PATH_MAX];
	struct stat st;
	int fd;

	a->f = NULL;
	a->sync = 0;
	/* write through a symlink to its target, so a linked dotfile stays linked */
	if (!lstat(path, &st) && S_ISLNK(st.st_mode) && realpath(path, real))
		path = real;
	if (snprintf(a->path, sizeof a->path, "%s", path) >= (int)sizeof a->path ||
	    snprintf(a->tmp, sizeof a->tmp, "%s.themer-XXXXXX", path) >= (int)sizeof a->tmp)
		return NULL;
	if (ensure_parent(a->path) < 0 || (fd = mkstemp(a->tmp)) < 0)
		return NULL;
	/* the original's permissions, or the usual ones for a new file */
	fchmod(fd, !stat(a->path, &st) ? (st.st_mode & 07777) : 0644);
	if (!(a->f = fdopen(fd, binary ? "wb" : "w"))) {
		close(fd);
		unlink(a->tmp);
	}
	return a->f;
}

/* whether two files hold the same bytes */
static int
same_file(const char *a, const char *b)
{
	char x[8192], y[8192];
	struct stat sa, sb;
	FILE *fa, *fb;
	size_t n;
	int same = 0;

	if (stat(a, &sa) || stat(b, &sb) || !S_ISREG(sb.st_mode) || sa.st_size != sb.st_size)
		return 0;
	if (!(fa = fopen(a, "rb")))
		return 0;
	if ((fb = fopen(b, "rb"))) {
		do {
			n = fread(x, 1, sizeof x, fa);
			same = fread(y, 1, sizeof y, fb) == n && !memcmp(x, y, n);
		} while (same && n == sizeof x);
		fclose(fb);
	}
	fclose(fa);
	return same;
}

int
afile_commit(AFile *a)
{
	int bad = fflush(a->f) != 0 || ferror(a->f) || (a->sync && fsync(fileno(a->f)) != 0);

	if (fclose(a->f) != 0)
		bad = 1;
	a->f = NULL;
	/* the same bytes as already there: the file is left alone, its time too, so
	 * nothing that watches it (waybar, fcitx, Stylus) reloads for nothing */
	if (!bad && same_file(a->tmp, a->path)) {
		unlink(a->tmp);
		return 0;
	}
	if (bad || rename(a->tmp, a->path) < 0) {
		unlink(a->tmp);
		return -1;
	}
	return 0;
}

void
afile_abort(AFile *a)
{
	if (a->f)
		fclose(a->f);
	a->f = NULL;
	unlink(a->tmp);
}
