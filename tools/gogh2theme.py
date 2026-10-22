#!/usr/bin/env python3
"""gogh2theme.py: themer theme files from Gogh-style terminal schemes.

Input is a list of lines "name= #ansi0 #ansi1 ... #ansi15" (the 16 terminal
colors, background first and foreground among the whites, the way Gogh's
collection is usually tabulated). Output is a themer theme file per scheme,
with [dark] and [light] sections.

    gogh2theme.py LIST OUTDIR NAME=dark[/light][:displayname] ...

Each argument names a theme to write: NAME is the file (themes/NAME.theme),
dark and light the schemes in LIST that fill the two sections. A missing side
is derived from the other: the background and text keep their hue and move to
the other end of the lightness scale, the terminal colors are darkened or
lightened until they read on it. themer's readability rules adjust what is
still short when the theme is loaded, so the derived side is usable as it
is, and a starting point to edit.

Roles are picked from the 16 colors: bg = ansi0; fg = the white that reads
best on it; surf = bg moved a step toward fg; urg = red, warn = yellow (the
normal or the bright one, whichever reads better); acc = the most saturated
color that reads 3:1 on bg, reds left out so the accent is not the urgent
color (monochrome schemes take their one color). ACC=n after a scheme name
forces ansi n as the accent: NAME=dark@4/light@12.
"""
import colorsys
import re
import sys

def hex2rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))

def rgb2hex(c):
    return ''.join('%02x' % max(0, min(255, round(v * 255))) for v in c)

def lum(c):
    def f(v):
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (f(v) for v in c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

def contrast(a, b):
    la, lb = lum(hex2rgb(a)), lum(hex2rgb(b))
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)

def mix(a, b, t):
    ca, cb = hex2rgb(a), hex2rgb(b)
    return rgb2hex(tuple(x + (y - x) * t for x, y in zip(ca, cb)))

def hls(h):
    return colorsys.rgb_to_hls(*hex2rgb(h))

def from_hls(h, l, s):
    return rgb2hex(colorsys.hls_to_rgb(h, l, s))

def is_dark(bg):
    return contrast(bg, 'ffffff') > contrast(bg, '000000')

def with_lightness(c, l):
    h, _, s = hls(c)
    return from_hls(h, l, s)

def readable(c, bg, ratio, toward):
    """c moved toward 'toward' (a lightness) until it reads at ratio on bg"""
    if contrast(c, bg) >= ratio:
        return c
    h, l, s = hls(c)
    for i in range(1, 41):
        l2 = l + (toward - l) * i / 40
        c2 = from_hls(h, l2, s)
        if contrast(c2, bg) >= ratio:
            return c2
    return from_hls(h, toward, s)

def roles(ansi, acc_idx=None):
    bg = ansi[0]
    dark = is_dark(bg)
    fg = max((ansi[7], ansi[15]), key=lambda c: contrast(c, bg))
    if contrast(fg, bg) < 4.5:
        fg = max(ansi, key=lambda c: contrast(c, bg))
    surf = mix(bg, fg, 0.12 if dark else 0.10)
    def better(i, j):
        return ansi[i] if contrast(ansi[i], bg) >= contrast(ansi[j], bg) else ansi[j]
    urg = better(1, 9)
    warn = better(3, 11)
    if contrast(urg, bg) < 3:
        urg = readable(urg, bg, 3.0, 0.7 if dark else 0.35)
    if contrast(warn, bg) < 3:
        warn = readable(warn, bg, 3.0, 0.7 if dark else 0.35)
    if acc_idx is not None:
        acc = ansi[acc_idx]
    else:
        cands = [ansi[i] for i in (2, 3, 4, 5, 6, 10, 11, 12, 13, 14)]
        def score(c):
            h, l, s = hls(c)
            cr = contrast(c, bg)
            if cr < 2.0 or c in (urg, warn):
                return -1
            # yellows and oranges are the warning color's territory: let the
            # blues, greens, cyans and magentas win unless they are much weaker
            hue_w = 0.5 if 0.06 < h < 0.2 else 1.0
            return s * hue_w * min(cr, 4.5) / 4.5
        distinct = {c for c in ansi[1:7]}
        if len(distinct) <= 2:          # monochrome: the one color it has
            acc = ansi[1]
        else:
            acc = max(cands, key=score)
    acc = readable(acc, bg, 3.0, 0.75 if dark else 0.3)
    accfg = '000000' if contrast(acc, '000000') >= contrast(acc, 'ffffff') else 'ffffff'
    return dict(bg=bg, fg=fg, surf=surf, acc=acc, accfg=accfg, urg=urg, warn=warn), dark

def derive(ansi, to_dark):
    """the other mode of a scheme: same hues, lightness flipped"""
    bg, fg = ansi[0], max((ansi[7], ansi[15]), key=lambda c: contrast(c, ansi[0]))
    h, l, s = hls(bg)
    nbg = from_hls(h, 0.13 if to_dark else 0.94, min(s, 0.25))
    h, l, s = hls(fg)
    nfg = from_hls(h, 0.85 if to_dark else 0.18, min(s, 0.3))
    out = []
    for i, c in enumerate(ansi):
        if i == 0:
            out.append(nbg)
        elif i == 15:
            out.append(nfg)
        elif i == 7:
            out.append(mix(nfg, nbg, 0.25))
        elif i == 8:
            out.append(mix(nfg, nbg, 0.55))
        else:
            out.append(readable(c, nbg, 4.5, 0.7 if to_dark else 0.35))
    return out

def load(path):
    schemes = {}
    for line in open(path):
        m = re.match(r"\s*(?:COLORS=')?([A-Za-z0-9_]+)=\s*((?:#[0-9a-fA-F]{6}\s*){16})", line)
        if m:
            schemes[m.group(1)] = [c.lstrip('#').lower() for c in m.group(2).split()]
    return schemes

def section(name, ansi, acc_idx, note):
    r, _ = roles(ansi, acc_idx)
    lines = ['[%s]' % name]
    if note:
        lines.insert(0, '# ' + note)
    for k in ('bg', 'fg', 'surf', 'acc', 'accfg', 'urg', 'warn'):
        lines.append('%s=%s' % (k, r[k]))
    for i, c in enumerate(ansi):
        lines.append('ansi%d=%s' % (i, c))
    return '\n'.join(lines) + '\n'

def parse_side(s):
    """'scheme' or 'scheme@n' -> (scheme, n or None)"""
    if not s:
        return None, None
    if '@' in s:
        a, n = s.split('@', 1)
        return a, int(n)
    return s, None

def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    schemes = load(sys.argv[1])
    outdir = sys.argv[2]
    for spec in sys.argv[3:]:
        name, _, rest = spec.partition('=')
        rest, _, title = rest.partition(':')
        d, _, l = rest.partition('/')
        dark, dacc = parse_side(d)
        light, lacc = parse_side(l)
        for s in (dark, light):
            if s and s not in schemes:
                sys.exit('%s: no scheme %s' % (name, s))
        if dark and not is_dark(schemes[dark][0]):
            sys.exit('%s: %s is not dark' % (name, dark))
        if light and is_dark(schemes[light][0]):
            sys.exit('%s: %s is not light' % (name, light))
        head = '# %s: ' % (title or name)
        if dark and light:
            head += 'dark from Gogh "%s", light from Gogh "%s".' % (dark, light)
            da, la = schemes[dark], schemes[light]
            dn = ln = None
        elif dark:
            head += 'dark from Gogh "%s"; the light section is derived (no light variant).' % dark
            da, la = schemes[dark], derive(schemes[dark], False)
            dn, ln = None, 'derived from the dark section: same hues, lightness flipped'
        else:
            head += 'light from Gogh "%s"; the dark section is derived (no dark variant).' % light
            da, la = derive(schemes[light], True), schemes[light]
            dn, ln = 'derived from the light section: same hues, lightness flipped', None
        head += '\n# Terminal colors as published; roles chosen by tools/gogh2theme.py (see it).\n'
        text = head + section('dark', da, dacc, dn) + section('light', la, lacc, ln)
        open('%s/%s.theme' % (outdir, name), 'w').write(text)
        rd, _ = roles(da, dacc)
        rl, _ = roles(la, lacc)
        print('%-20s dark bg=%s acc=%s   light bg=%s acc=%s' % (name, rd['bg'], rd['acc'], rl['bg'], rl['acc']))

if __name__ == '__main__':
    main()
