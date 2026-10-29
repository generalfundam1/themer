<!-- Group C: launchers, notifications, the lock screen. Rows for README.md. -->

## Module table rows (| Module | Themes |)

| **rofi** | the rofi launcher (X11, and Wayland from rofi 2.0): a theme, `~/.local/share/rofi/themes/themer.rasi`, selected by a marked `@theme` line at the end of `~/.config/rofi/config.rasi` |
| **fuzzel** | the fuzzel launcher (Wayland): its font, `[colors]` and `[border]` keys in `~/.config/fuzzel/fuzzel.ini`, set in place |
| **mako** | mako notifications (Wayland): font, colors, border and progress bar in `~/.config/mako/config`, set in place, plus the border of low and high urgency |
| **dunst** | dunst notifications: a drop-in, `~/.config/dunst/dunstrc.d/90-themer.conf`; your `dunstrc` is not edited |
| **swaync** | SwayNotificationCenter: its color names and custom properties, and the font, in a marked block at the end of `~/.config/swaync/style.css` |
| **swaylock** | the swaylock lock screen (and swaylock-effects): the ring, its inside, line and text in every state, and the font, in `~/.config/swaylock/config`, set in place |

## Options table rows (| Module | Option | Values | Default |)

| rofi, fuzzel, mako, dunst, swaync, swaylock | `font` | `ui` \| `terminal` | `ui` |

(Each also has the host's `mode` and, last, `template`.)

## Launchers, notifications and the lock screen

**rofi** writes a whole rofi theme, `~/.local/share/rofi/themes/themer.rasi`, from
`templates/<set>/rofi/themer.rasi`, and selects it with `@theme "<that path>"` in a
marked block at the end of `~/.config/rofi/config.rasi`. `@theme` starts the theme
afresh, so a theme you chose earlier in the file gives way to it while the module is on
and comes back on unload, when the block goes (and the file too, if themer created it).
Your `configuration { }` block, modes and keys are left as they are. Themer Default is
rofi's own default theme (square, dashed separators, the scrollbar at the side) in the
theme's colors; Themer Chicago draws it square and flat with 1px solid separators and the
input as a sunken field, as the dmenu and wmenu launchers look. As sway's or i3's
launcher: `rofi -show drun`.

**fuzzel** sets its keys in `~/.config/fuzzel/fuzzel.ini` one by one (`[main] font`, the
`[colors]`, `[border] width` and `radius`), so your other keys stay and unload puts your
values back. The keys come from `templates/<set>/fuzzel/fuzzel.ini`, laid out as
fuzzel.ini is, so a set changes them by listing them. Colors are `RRGGBBAA`. Themer
Default keeps fuzzel's rounded popover (radius 12, as Adwaita rounds its popovers);
Chicago is square. As sway's or i3's launcher: `fuzzel`.

**mako** sets its keys in `~/.config/mako/config` the same way: the global options
(font, colors, border, progress bar) before your first `[criteria]` section, and the
border color of `[urgency=low]` and `[urgency=high]`. A running mako re-reads the file
(`makoctl reload`).

**dunst** writes a drop-in, `~/.config/dunst/dunstrc.d/90-themer.conf`, which dunst reads
after your `dunstrc`: the font, the frame, the progress bar (`highlight`) and the colors
of each urgency. Your `dunstrc` is never edited. Dunst only reads the drop-ins next to
the dunstrc it uses, so without one of your own (dunst then uses the system's) themer
creates a `~/.config/dunst/dunstrc` holding nothing but a marked comment, and removes it
on unload. A running dunst re-reads both (`dunstctl reload`).

**swaync** puts a marked block at the end of `~/.config/swaync/style.css`: swaync's own
color names, as `@define-color` (read before swaync 0.12) and as custom properties on
`:root` (read from 0.12, GTK 4), and the font. Being last, it wins over your rules
above it, and the rest of your stylesheet stays. Without a `style.css` of your own the
file themer creates first imports the system's (`/etc/xdg/swaync/style.css`), so
swaync's look is kept; it is removed on unload. Chicago also squares every corner and
outlines the control center and the notifications in 1px. A running swaync re-reads
its style (`swaync-client --reload-css`).

**swaylock** sets its keys in `~/.config/swaylock/config` (one option per line, as on its
command line without the dashes): the background, the ring, its inside, line and text
while typing, clearing, verifying, after a wrong password and with Caps Lock on, the key
and backspace highlights, the keyboard-layout box, and the font. Flags without a value
(`ignore-empty-password`, `daemonize`) are yours and left alone. swaylock-effects reads
the same file.
