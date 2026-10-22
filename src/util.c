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
	FILE *f;
	int r;

	if (ensure_parent(path) < 0 || !(f = fopen(path, "w")))
		return -1;
	fn(f, c, s);
	r = fclose(f);
	return r == 0 ? 0 : -1;
}

int
write_bytes(const char *path, const void *buf, size_t len)
{
	FILE *f;
	int ok;

	if (ensure_parent(path) < 0 || !(f = fopen(path, "wb")))
		return -1;
	ok = fwrite(buf, 1, len, f) == len;
	return (fclose(f) == 0 && ok) ? 0 : -1;
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
