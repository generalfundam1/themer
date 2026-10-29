/* color.c: color math shared by the host and by modules.
 * Colors are six hex digits, no '#'. Contrast is WCAG 2 contrast ratio. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "themer.h"

#define WCAG_TEXT 4.5          /* normal text */

/* Theme lists draw a swatch per theme, and each theme's colors are checked for
 * contrast as it loads: this runs thousands of times a refresh, so it parses the
 * digits itself and looks the curve up rather than calling strtol and pow. */
static int
digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0;
}

static int
channel(const char *hex, int i)
{
	return digit(hex[2 * i]) * 16 + digit(hex[2 * i + 1]);
}

static double
linear(int v)
{
	static double table[256];
	static int ready;

	if (!ready) {
		for (int k = 0; k < 256; k++) {
			double x = k / 255.0;

			table[k] = x <= 0.03928 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
		}
		ready = 1;
	}
	return table[v & 255];
}

static double
luminance(const char *hex)
{
	return 0.2126 * linear(channel(hex, 0)) + 0.7152 * linear(channel(hex, 1)) +
	       0.0722 * linear(channel(hex, 2));
}

double
tm_contrast(const char *a, const char *b)
{
	double la = luminance(a), lb = luminance(b);

	if (la < lb) {
		double t = la;

		la = lb;
		lb = t;
	}
	return (la + 0.05) / (lb + 0.05);
}

/* a moved toward b by t10 tenths, per channel */
void
tm_mix(char *out, size_t n, const char *a, const char *b, int t10)
{
	static const char hexd[] = "0123456789abcdef";
	char buf[7];

	if (t10 < 0)
		t10 = 0;
	if (t10 > 10)
		t10 = 10;
	for (int i = 0; i < 3; i++) {
		int x = channel(a, i), y = channel(b, i);
		/* a weighted average, rounded: a step of a few units must still move the color */
		int v = (x * (10 - t10) + y * t10 + 5) / 10;

		buf[2 * i] = hexd[v >> 4];
		buf[2 * i + 1] = hexd[v & 15];
	}
	buf[6] = '\0';
	snprintf(out, n, "%s", buf);
}

/* c as is when it reads at min against bg; otherwise moved toward target
 * until it does. Returns 1 if c was changed. */
int
tm_ensure_contrast(char *out, size_t n, const char *c, const char *bg,
                   const char *target, double min)
{
	int step;

	snprintf(out, n, "%s", c);
	if (tm_contrast(c, bg) >= min)
		return 0;
	for (step = 1; step <= 10; step++) {
		tm_mix(out, n, c, target, step);
		if (tm_contrast(out, bg) >= min)
			break;
	}
	return 1;
}

/* preferred when it reads at WCAG text level on bg; otherwise the better of
 * preferred, black, and white */
void
tm_text_on(char *out, size_t n, const char *bg, const char *preferred)
{
	const char *cands[3] = { preferred, "000000", "ffffff" };
	double best = -1;
	int i;

	for (i = 0; i < 3; i++) {
		double c = tm_contrast(cands[i], bg);

		if (c >= WCAG_TEXT) {
			snprintf(out, n, "%s", cands[i]);
			return;
		}
		if (c > best) {
			best = c;
			snprintf(out, n, "%s", cands[i]);
		}
	}
}
