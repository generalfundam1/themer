# Group A: window managers (README material)

## Module table rows (the "Modules" table, `| **name** | what it does |`)

| **hyprland** | border colours (the active one keeps Hyprland's two-colour gradient under Themer Default), group borders, the groupbar, the background and the font. Written to `~/.config/hypr/themer.lua` (loaded from the end of `hyprland.lua` with `pcall(require, "themer")`) and/or `themer.conf` (sourced from the end of `hyprland.conf`), for whichever of the two you have. Themer Chicago also squares the corners and turns shadows off |
| **hyprlock** | with no `hyprlock.conf` of yours, a whole lock screen (hyprlock's own example, in the theme); with yours, `$themer_bg`, `$themer_accent`, `$themer_font` and the rest in `~/.config/hypr/themer-colors.conf`, sourced from the top of your file for your widgets to use |
| **niri** | focus ring, border, urgent and insert-hint colours and the workspace background, in `~/.config/niri/themer.kdl`, included from the end of `config.kdl` (niri 25.11 and later; older ones are left alone) |
| **river** | background and border colours as `riverctl` calls in `~/.config/river/themer`, run from the end of your `init` (a shell init; river-classic, the 0.3 line with `riverctl`) |
| **bspwm** | border and preselection colours as `bspc config` calls in `~/.config/bspwm/themer`, run from the end of your `bspwmrc` and at once while bspwm runs |
| **dwm** | a header with `fonts[]`, `dmenufont[]` and `colors[][3]`; dwm's `config.h` needs one edit, see `modules/dwm.c` (`$DWM_SRC` gets a copy) |
| **labwc** | title bars, borders, menus and on-screen displays at the end of `themerc-override` (over whatever theme you use), and the fonts in `rc.xml`'s `<theme>`, after yours; Themer Chicago adds square corners |
| **openbox** | a theme, `Themer`, in `~/.local/share/themes/Themer/openbox-3/`, selected and given the fonts by two marked blocks in `~/.config/openbox/rc.xml` (Clearlooks recoloured under Themer Default; flat, bevelled title bars under Themer Chicago) |

## Options table rows (`| Module | Option | Values | Default |`)

| hyprland, hyprlock, labwc, openbox | `font` | `ui` \| `terminal` | `ui` |
| dwm | `font` | `ui` \| `terminal` | `terminal` |

(niri, river and bspwm draw no text, so they have only `mode` and `template`.)

## Template directories (the Templates table, `| dir | files | how it is used |`)

| `hyprland/` | `themer.lua`, `themer.conf` | written to `~/.config/hypr/`; `themer.lua` loaded from `hyprland.lua`, `themer.conf` sourced from `hyprland.conf`. Variables: `{{font_family}}`, `{{font_size}}` |
| `hyprlock/` | `hyprlock.conf`, `colors.conf` | `hyprlock.conf` is the whole lock screen when you have none; `colors.conf` becomes `themer-colors.conf`, sourced from yours. Variables: `{{font_family}}`, `{{font_size}}` |
| `niri/` | `themer.kdl` | written to `~/.config/niri/`, included from `config.kdl` |
| `river/` | `themer` | `riverctl` calls, a shell file run from `init` |
| `bspwm/` | `themer` | `bspc config` calls, a shell file run from `bspwmrc` |
| `dwm/` | `colors.h` | the header (`~/.config/themer/dwm/colors.h`, and `$DWM_SRC/themer-colors.h`). Variables: `{{font_family}}`, `{{font_size}}` |
| `labwc/` | `themerc`, `theme.xml` | `themerc` goes at the end of `themerc-override`; `theme.xml` is the inside of `<theme>` in `rc.xml`. Variables: `{{font_family}}`, `{{font_size}}` |
| `openbox/` | `themerc`, `fonts.xml` | the theme's `openbox-3/themerc`; `fonts.xml` goes in `rc.xml`'s `<theme>`. Variables: `{{font_family}}`, `{{font_size}}`, `{{acc_hi}}` and `{{acc_lo}}` (the accent a step lighter and darker) |

## Window managers (a section, after the sway/i3 paragraphs)

Every window manager module does what the sway and i3 ones do: it writes the theme's
colours (and its font, where the window manager draws text) to a file of its own and
hooks that file into your config with a marked block, so unload puts your config back
byte for byte. Borders, gaps, layouts and bindings stay yours; a set changes the
look only where the program has one to change (Themer Chicago squares Hyprland's and
labwc's corners and turns Hyprland's shadows off).

**Hyprland.** Since 0.55 Hyprland reads `hyprland.lua` when it is there, and
`hyprland.conf` (hyprlang) otherwise, for a release or two more. The module writes
`themer.lua`, loaded from the end of `hyprland.lua` with `pcall(require, "themer")`, and
`themer.conf`, sourced from the end of `hyprland.conf`, for whichever of the two you
have. Each sets only colours and the font, through `hl.config` or the categories, so
it wins over earlier lines and leaves everything else alone. Without either file
nothing is written: Hyprland makes its example config on first start, and the next
apply hooks into it. Hyprland re-reads changed files itself; `hyprctl reload` is
also run inside a Hyprland session.

**hyprlock.** Every block in a hyprlock config is a widget of its own, so a block
added after yours would draw a second password field rather than recolour yours.
With no `hyprlock.conf`, the module writes one: hyprlock's example lock screen (the
clock, the date, the password field) in the theme. With yours, it gives your widgets
the theme to use: `themer-colors.conf` defines `$themer_bg`, `$themer_fg`,
`$themer_accent`, `$themer_urgent`, `$themer_field`, `$themer_font` and the rest, and a
`source =` line at the top of your file loads it. Write `outer_color = $themer_accent`
and the field follows every apply.

**niri.** `themer.kdl` is included from the end of `config.kdl`. Included files merge
into the config and win over what comes before them, so only the named colours
change; whether the focus ring or the border is on, and how wide, stays as your
config says. Includes came with niri 25.11: an older niri is left alone, with a
message. niri re-reads its config when a file changes.

**river and bspwm.** Both are configured by a script run at start (`init`,
`bspwmrc`). The module writes the `riverctl` or `bspc config` calls to a file of its
own and runs it from the end of your script; a running river or bspwm takes them at
once. A script that is not a shell script is left alone, with a message. The river
module is for river-classic, the 0.3 line that keeps `riverctl`; river 0.4 hands
window management to separate programs.

**dwm.** Like st, dwm is configured at compile time. The module writes
`~/.config/themer/dwm/colors.h` (and `$DWM_SRC/themer-colors.h`) with `fonts[]`,
`dmenufont[]` and `colors[][3]` in dwm's own layout. Delete those lines from your
`config.h`, put `#include "themer-colors.h"` in their place, and rebuild after an
apply.

**labwc.** labwc's documented way to change a theme is `themerc-override`; the
module's keys go at the end of it, over whatever theme `rc.xml` names. The fonts go
in `rc.xml`'s `<theme>`, after your own `<font>` lines so they win. Without an
`rc.xml`, one holding only that is made (labwc's defaults fill in the rest) and taken
away on unload. `labwc --reconfigure` runs inside a labwc session.

**Openbox.** The theme is a real Openbox theme, `Themer`, in
`~/.local/share/themes/Themer/openbox-3/themerc`. Openbox uses the first `<name>` in
`<theme>`, so the module puts `<name>Themer</name>` right after `<theme>` and your
own `<name>` line stays as it was; the fonts go after your last `</font>`, since
Openbox reads them in order. With no `~/.config/openbox/rc.xml` (Openbox then reads
the system's), only the theme is written: copy `/etc/xdg/openbox/rc.xml` there and
apply again.
