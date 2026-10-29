# themer

**One theme, one dark/light switch, applied to the whole desktop — and undone the same way.**

themer is a suckless-style theme manager written in C99. It takes a colour theme and a
mode (dark or light) and writes the configuration of everything on the desktop from
them: GTK 2/3/4, Qt 5/6, Firefox, Thunderbird, sway or i3, waybar, foot, nvim, fcitx5,
launchers, newt dialogs, the TTY — and YouTube and Wikipedia, through Stylus. Every write is recorded, so unloading a module puts
the user's files back byte for byte.

The look itself comes from a template set. Themer Default, the default, is GNOME's
Adwaita in the theme's colours, with Firefox and the web sites left as they come and
recoloured; Themer Chicago is flat, square and outlined everywhere. A set of your own
is a directory of ordinary theme files with `{{variables}}` in them.

It replaces nwg-look, qt5ct and qt6ct: icons, cursor, fonts and font rendering are
applied from the same state, and Qt programs are themed through themer's own platform
theme plugin.

```
themer                 # the TUI
themer-gtk             # the same, as a GTK window with a live preview
themer-qt              # ... or a Qt window
themer-web             # ... or a page in the browser, on localhost
themer toggle          # flip dark/light, apply everywhere
themer theme gruvbox   # switch theme, apply everywhere
```

<p align="center">
  <img src="screenshots/themer-gtk-dark.png" width="49%" alt="themer-gtk, gruvbox dark">
  <img src="screenshots/themer-gtk-light.png" width="49%" alt="themer-gtk, gruvbox light">
</p>
<p align="center"><sub>themer-gtk with Themer Default: the same theme in dark and light. The window and the preview on the right are drawn with the GTK theme the gtk module writes: GTK's Adwaita in the theme's colours.</sub></p>

<p align="center">
  <img src="screenshots/firefox-gruvbox-dark.png" width="100%" alt="Firefox chrome, gruvbox dark">
  <img src="screenshots/firefox-nord-light.png" width="100%" alt="Firefox chrome, nord light">
</p>
<p align="center"><sub>Firefox 140 with the firefox module and Themer Chicago: three flat rows, tabs like sway's, the URL bar in the terminal font.</sub></p>

## Contents

- [Build and install](#build-and-install)
- [Usage](#usage)
- [Setting up Firefox, Thunderbird and Stylus](#setting-up-firefox-thunderbird-and-stylus)
- [Front ends](#front-ends)
- [What apply covers](#what-apply-covers)
- [Qt](#qt)
- [Environment](#environment)
- [Modules](#modules)
- [Templates](#templates)
- [Writing a template set](#writing-a-template-set)
- [Design language](#design-language)
- [Readability](#readability)
- [Firefox](#firefox)
- [Stylus (web sites)](#stylus-web-sites)
- [Thunderbird](#thunderbird)
- [Colour schemes](#colour-schemes)
- [Fonts](#fonts)
- [Writing a module](#writing-a-module)
- [Upgrading](#upgrading)
- [Status and license](#status-and-license)

## Build and install

```sh
make                  # host, modules, and whichever front ends the system can build
make check            # render every template, then an apply/unload round trip in a throwaway HOME
sudo make install     # to PREFIX (/usr/local); an earlier install is removed first
sudo make uninstall   # removes all of that; ~/.config/themer is yours and stays
```

`make` builds `themer-gtk` when `pkg-config` finds `gtk+-3.0`, `themer-qt` when it
finds `Qt6Widgets` or `Qt5Widgets` and a C++ compiler, and the Qt platform theme
plugin for each Qt major it finds. It prints what it found; `GTK=0` or `QT=0` skips
one on purpose. The only hard dependencies are a C compiler and libc.

| Make target | What it does |
|---|---|
| `make` | host binary, modules, front ends, Qt plugin |
| `make check` | templates rendered for every theme and mode; apply/unload round trip (`test/run.sh`) |
| `FIREFOX=/path/to/firefox sh test/firefox.sh [theme] [dark\|light]` | renders the chrome in a real Firefox under Xvfb; screenshots to `test/firefox/out/` |
| `sudo make install` | binaries to `PREFIX/bin`, themes/templates/icons to `PREFIX/share/themer`, modules to `PREFIX/lib/themer/modules`, the Qt plugin to `PREFIX/lib/themer/qt`, the module header to `PREFIX/include/themer` |
| `make dist` | `themer-VERSION.tar.gz` from the git tree |

**Arch:** `dist/PKGBUILD` and `dist/themer.install`. Run `make dist`, then `makepkg -si`
next to the tarball. The Qt plugin is built against the installed Qt and must be
rebuilt after a Qt minor upgrade; a package makes that one command.

`themer -v` prints the version; `VERSION` is set at the top of the Makefile.

### Layout

| Directory | Contents |
|---|---|
| `src/` | the host: one binary. `main.c` the command line; `state.c` the state file; `registry.c` finds modules and runs apply; `manifest.c` the `tm_*` helpers modules write with, and the undo log; `template.c` and `palette.c` the templates and shared variables; `theme.c` the theme files and readability rules; `targets.c` the env and tty targets; `front.c` the layer every front end calls; `tui.c`, `fonts.c`, `preview.c` the interactive parts |
| `gui/` | `themer-gtk` (GTK 3, C), `themer-qt` (Qt 5 or 6, C++) and `themer-web` (C, with its page in `web/index.html`); `common.c` is what they share; `qpa/` the Qt platform theme plugin |
| `include/` | `themer_module.h`, the API modules are built against |
| `modules/` | one `.c` per module; `wm.h`, `launcher.h`, `icons.h` are shared code |
| `templates/` | `templates/<set>/<dir>/<file>`; a set is one directory (`themer_default` and `themer_chicago` are built in), `<dir>` is named by the module |
| `themes/` | `<name>.theme`, a `[dark]` and a `[light]` section each |
| `icons/` | Adwaita's symbolic icons, the fallback for the Firefox toolbar |
| `tools/` | `adwaita/build.sh`, which makes the GTK files of `themer_default` from GTK's own Adwaita (see [Templates](#templates)) |
| `test/` | `make check`, a shell script (its themer-web checks use `curl`, and are skipped without it); `firefox.sh` screenshots a real Firefox, by hand; `gogh-schemes.txt`, the Gogh collection the shipped schemes were imported from |

## Usage

```
themer                              TUI
themer-gtk | themer-qt [PAGE]       the same settings in a window, with a live preview;
                                    PAGE: theme, fonts, icons, modules or system
                                    (modules NAME opens one module)
themer-web [-p PORT] [-d | -q]      the same in the browser at http://localhost:7331/
                                    (-d detach, -q stop the one running); runs only
                                    while a browser module (firefox, stylus) is on;
                                    started while one runs, it opens that one's page
themer apply                        re-apply everything and save state (run at login)
themer remove                       Remove all styles: every module unloaded, your configs
                                    as they were; the settings stay, and the login's
                                    apply leaves it so until restore (or Apply)
themer restore                      the styles back, after remove
themer toggle                       flip dark/light and apply
themer dark | light                 set mode and apply
themer theme NAME                   switch theme and apply
themer list                         list themes
themer check                        list every color the readability rules adjust
themer env [NAME=VALUE]             list the session's variables, or set one (NAME=
                                    removes it) and apply
themer color [ROLE HEX|reset]       list the colors (yours marked), or put one of your
                                    own over the scheme's (bg fg surf acc accfg urg
                                    warn), or reset it, and apply
themer icons [NAME]                 show (current first) or set the icon theme and apply
themer cursor [NAME [SIZE]]         show or set the cursor theme and size and apply
themer font ui|mono FAMILY [SIZE]   set the interface or terminal font and apply
themer font tty [NAME]              list the console fonts, or set one and apply
themer render [hinting none|slight|medium|full] [antialias on|off]
              [rgba none|rgb|bgr|vrgb|vbgr]
                                    show or set font rendering and apply
themer set                          show the template set
themer set NAME                     switch every module to template set NAME and apply
themer set list                     list the sets and the modules each covers
themer set copy FROM TO             copy a set under a new name, to edit
themer set import NAME MODULE PATH  turn an existing theme into (part of) a set
themer set check NAME               render every file of NAME for every theme
themer module list                  list modules, on/off, description
themer module load|unload NAME      load (write its files) or unload (remove them) now
themer module options NAME          list a module's options by section; * marks a set value
themer module option NAME KEY VALUE set one; a loaded module reloads at once
themer module reset NAME KEY        back to the default (template: the global set)
themer server start|stop|status     themer-web: start it, stop it, or say whether it runs
themer server port N                its port (a running server moves to it)
themer server install [SITE]        open the userstyles (or one site's) in the browser,
                                    for Stylus to install; starts the server if need be
themer scheme import FILE [NAME]    add a colour scheme (see Colour schemes)
themer scheme remove NAME           delete one of yours, or hide a built-in one
themer scheme restore [NAME]        bring hidden built-in schemes back
themer template list DIR [SET]      list template sets, or the files in SET
themer template render MODULE SET FILE [OUT]
                                    render one template with the current theme
themer system install|remove|sync|watch|status [USER]
                                    as root: the console and ly in the theme (see
                                    ly and the console)
themer -v                           version
```

**Lookup, most specific first:**

| | |
|---|---|
| themes | `$THEMER_THEMES`, `~/.config/themer/themes`, `PREFIX/share/themer/themes` |
| modules | `$THEMER_MODULES`, `~/.config/themer/modules`, `PREFIX/lib/themer/modules` |
| templates | `~/.config/themer/templates`, `$THEMER_TEMPLATES`, `PREFIX/share/themer/templates` (built-in sets are mirrored into the first; see [Templates](#templates)) |
| icons | `$THEMER_ICONS`, `PREFIX/share/themer/icons` (the Firefox fallback only) |

Running from the source tree:

```sh
THEMER_THEMES=$PWD/themes THEMER_MODULES=$PWD/modules \
THEMER_TEMPLATES=$PWD/templates THEMER_ICONS=$PWD/icons ./themer
```

**State** lives in `~/.config/themer/state`: theme, mode, template set, fonts, icon
theme, cursor theme and size, font rendering, environment, which modules are on, and
option values. Compile-time defaults are in `config.def.h`.

## Setting up Firefox, Thunderbird and Stylus

Most modules work as soon as they are on. These three need a step or two in the
program itself the first time, because themer does not change a program's own
settings for it.

### Firefox

1. **Let Firefox read user stylesheets.** Open `about:config`, accept the warning,
   search for `toolkit.legacyUserProfileCustomizations.stylesheets` and set it to
   `true` (double-click it). Without it Firefox ignores `userChrome.css`, which is
   where the theme goes. themer does not write `user.js` for you.
2. **Apply.** `themer apply`, or Apply in any front end, with the firefox module on
   (it is by default). The output names the profile it wrote to:
   `themer: firefox profile: ~/.mozilla/firefox/xxxx.default-release`.
3. **Restart Firefox.** It reads its stylesheets at startup only, so every later
   apply (another theme, dark/light) shows after a restart too; the output says when
   a running Firefox holds the profile.

The profile written to is the one a running Firefox holds, otherwise the one used most
recently (profiles in `~/.mozilla/firefox`, `~/.config/mozilla/firefox`, the snap and
the Flatpak). With several profiles, start Firefox with the one you want, then apply.

### Thunderbird

1. **Let Thunderbird read user stylesheets.** Settings, General, at the very bottom
   **Config Editor…**; search for `toolkit.legacyUserProfileCustomizations.stylesheets`
   and set it to `true`.
2. **Apply** with the thunderbird module on; the output names the profile.
3. **Restart Thunderbird**, now and after every later apply.

### Stylus (web sites)

1. **Install the Stylus extension** in your browser: from addons.mozilla.org for
   Firefox, from the Chrome Web Store for the Chromium family.
2. **Apply** with the stylus module on. It renders a style per site into
   `~/.config/themer/stylus/` (YouTube and Wikipedia ship).
3. **Install each style once:** **Install** beside the site in the stylus module's
   options (Modules, stylus, What it themes) in any front end, or
   `themer server install` for every site (`themer server install youtube` for one).
   That starts the server if it is stopped and opens the style in your default browser
   (xdg-open), where Stylus shows its install page: click **Install style**. Apply in
   a front end does the same for every site that is on (the stylus option `open`,
   `apply` by default; `never` leaves it to Install). The CLI's `apply`, `toggle`,
   `dark`/`light` and `theme`, which keybindings and login scripts run, never open
   the browser. Stylus
   asks for that click itself, so no program can install a style for you. The server
   keeps running while a browser module (firefox or stylus) is on.
4. **Later applies** reach Stylus through the style's update address, while the server
   runs: Stylus checks on its own schedule (its options set how often), or at once with
   *Check for updates* in Stylus's style manager. To see each apply instantly instead,
   tick **Live reload** on the install page and leave that tab open.

The styles set every color themselves, whichever of its own modes a site is in, but
a site in the other mode can still show its own colors in a corner the style does
not reach yet. The sites follow the desktop: themer sets the desktop's color scheme
(`color-scheme` in gsettings, which Firefox reads through the portal) to the mode, so
with Firefox's **Settings, General, Website appearance** on *Automatic* and YouTube's
and Wikipedia's own appearance on *device theme* / *Automatic*, they match.

A site you do not want themed: switch it off under Modules, stylus, What it themes
(`themer module option stylus youtube off`). Like every module, stylus has `mode`, so the
sites can stay dark on a light desktop. Without the server, a style can be installed
from its file (`file:///home/you/.config/themer/stylus/youtube.user.css`; in Chromium,
first allow Stylus access to file URLs), with Live reload on.

## Front ends

The CLI, the TUI, `themer-gtk`, `themer-qt` and `themer-web` are faces of one layer
(`src/front.c`); every setting and action is reachable from each, and Apply runs the
same `themer apply` whichever one you used.

All four are one design. They have the same five pages in the same order, use the
same words, and draw the same control for the same kind of setting. The words are
not written into each front end: `src/text.c` holds the pages and every heading,
label, note, button, tip and status line. The TUI, `themer-gtk` and `themer-qt`
read it with `front_text()`, and `themer-web` serves it as `/api/text`, which its
page fills itself from.

| Page | What is on it |
|---|---|
| **Theme** | the color scheme (find, Import…, Remove, Restore hidden), the mode, the template set (New set…, Import theme…, Check), and colors of your own over the scheme's |
| **Fonts** | the interface, terminal and console fonts, and the rendering (hinting, antialiasing, subpixel order) |
| **Icons** | the icon theme, drawn from its own files, and the mouse cursor and its size |
| **Modules** | every module, in three groups: **Loaded**, **Unloaded** and **Not installed** (a module whose program is missing cannot be loaded); the selected one with its switch, its description, and its options under **Look** and **What it themes** |
| **System** | the server (themer-web: start or stop it, its port), the environment variables, and About |

Apply (Ctrl+S), **Revert to applied**, **Remove all styles** and Quit are the File menu
in the windows, the buttons at the top of the web page, and the last rows of the TUI's
menu (`a`, `r`, `x`, `q`). **Remove all styles** (`themer remove`) takes every module's
work out, so each program is back to its own look and your configs are as they were;
your settings stay, the login's `themer apply` leaves things so, and Apply (or `themer
restore`) puts the styles back. Leaving with unapplied changes asks first everywhere,
and so do removing a scheme and Remove all styles. The status line says the same thing after the same action in every front end.

`themer-gtk` and `themer-qt` are the TUI as a window, like nwg-look: the pages on the
left, a preview on the right, Apply at the bottom. The preview is the real thing, not
a mock-up. The window restyles itself from the `gtk.css` (or `qt.qss` and palette) the
gtk (or qt) module would write for the selected theme, so the widgets there look the
way every GTK (or Qt) app will after Apply. The Icons page shows the icon theme's own
icons and draws the cursor theme's own files at the chosen size. The Terminal tab
shows the terminal font and colors, and the swatches show the roles and the sixteen
terminal colors. Nothing is written until Apply. `themer-web` is the same page in the
browser, and the page itself is its preview. The TUI's main menu has the pages as
sections, and shows each section's note under the list.

Template sets on the Theme page: **New set…** copies the selected set under a new
name into `~/.config/themer/templates/`. **Import theme…** turns a theme you already
have into part of a set (a GTK theme directory, a `.qss`, a `chrome/` directory...).
**Check** renders the selected set for every theme and shows the report. In the TUI
these are under *Template set* (`n`, `i`, `c`).

Controls: on/off is a switch. A few short choices are buttons side by side, with the
chosen one in the accent (the mode, a module's font). A number in a range is a slider
with its value. The template set is a list shown by its title (Themer Default;
`themer_default` is its directory name). A font is a family and a size, set apart. The
size limits are the same everywhere (fonts 4 to 72 points, the cursor 8 to 256 pixels).

**Modules**: each option shows what it does; a changed one shows its default and a
Reset button. A module's switch and its options take effect at once; Apply is for the
theme, fonts, icons and cursor. In the TUI: `l` into the options, `h`/`l` change one,
enter picks from a list or types a value, `r` resets, `i` runs an option's action
(Install for a Stylus site).

**System**, the server: `themer-web` puts the same settings on a page on localhost,
for when the browser is what you have open, and it serves the stylus module's
userstyles. Start and stop it here (or with `themer server start`), on the port set
here (7331). It runs only while a browser module is on, and stops by itself a few
seconds after the last one is turned off. It answers only requests for
`localhost:PORT` and changes nothing without the page's own header, so other sites
open in the browser cannot drive it. Apply in any front end also opens each Stylus
site that is on (see [Stylus](#stylus-web-sites)), starting the server if it is stopped.

<p align="center">
  <img src="screenshots/themer-qt-dark.png" width="49%" alt="themer-qt, nord dark">
  <img src="screenshots/themer-qt-system.png" width="49%" alt="themer-qt, system page">
</p>
<p align="center">
  <img src="screenshots/themer-qt-icons.png" width="49%" alt="themer-qt, icons page">
  <img src="screenshots/themer-gtk-modules.png" width="49%" alt="themer-gtk, modules page">
</p>
<p align="center"><sub>themer-qt and themer-gtk: the System page holds the server, the environment and About; the Icons page draws the icon and cursor themes' own files; the Modules page holds every module's options.</sub></p>

`themer-qt` needs no moc. `make gui` repeats what the build found; `make themer-gtk` or
`make themer-qt` builds one on purpose.

## What apply covers

The ground nwg-look and qt5ct/qt6ct cover, from one state, whichever front end set it:

| Module | Writes |
|---|---|
| **gtk** | `~/.config/gtk-3.0/settings.ini` and `gtk-4.0/settings.ini`: theme, icon theme, cursor theme and size, font, prefer-dark, Xft antialias/hinting/hintstyle/rgba; the same keys in `~/.gtkrc-2.0` (GTK 2) and `~/.config/xsettingsd/xsettingsd.conf` (xsettingsd gets SIGHUP); the `org.gnome.desktop.interface` gsettings keys (gtk-theme, icon-theme, cursor-theme, cursor-size, font-name, monospace-font-name, color-scheme, font-antialiasing, font-hinting, font-rgba-order) when gsettings is installed (`gsettings=off` turns that off) |
| **qt** | `~/.config/themer/qt/theme.conf` and `themer.qss` for themer's platform theme plugin (style, palette, icon theme, fonts, stylesheet), and `qt5ct.conf` / `qt6ct.conf` with the same where those tools are installed. qt5ct's Interface page is yours and is left as it is |
| **cursor** | `~/.icons/default/index.theme` and `~/.local/share/icons/default/index.theme` (`Inherits=`), `XCURSOR_THEME` and `XCURSOR_SIZE` in environment.d, `Xcursor.theme/size` in `~/.Xresources` (merged with xrdb when an X server runs); sway gets `seat * xcursor_theme` from the sway module, i3 reads the Xresources |
| **fontconfig** | `~/.config/fontconfig/conf.d/50-themer-rendering.conf`: antialias, hinting, hintstyle, rgba, lcdfilter, for everything that is not GTK |

Icon and cursor themes are listed from `~/.icons`, `~/.local/share/icons`,
`/usr/share/icons` and `/usr/local/share/icons`; a cursor theme is one with a `cursors/`
directory. Any name can also be typed.

## Qt

Qt programs take a palette, fonts, an icon theme and a style only from a *platform
theme* plugin, which is why qt5ct and qt6ct exist. themer brings its own:
`gui/qpa/themerqpa.cpp`, built once per Qt major that `make` finds and installed under
`PREFIX/lib/themer/qt/qt5/` and `qt6/`. The qt module writes
`~/.config/themer/qt/theme.conf` (style, icon theme, fonts, the palette) and
`themer.qss`, and sets `QT_QPA_PLATFORMTHEME=themer` and `QT_PLUGIN_PATH` in its
environment file. The plugin watches those files, so an apply restyles running Qt
programs at once. The style is Fusion under the set's stylesheet: Themer Default's draws
after Adwaita (rounded controls, buttons in the `control` tints, views on `view`, the
accent for selection and anything switched on), Themer Chicago's is flat and bevelled.

Where the plugin is not installed for your Qt, the module falls back to qt5ct (and
qt6ct) when they are installed; the `platformtheme` option forces either. Qt reads the
environment at start — see [Environment](#environment).

## Environment

Several things are environment variables: `QT_QPA_PLATFORMTHEME` and `QT_PLUGIN_PATH`,
`XCURSOR_THEME` and `XCURSOR_SIZE`, the fcitx input-method variables, and whatever you
set with `themer env`. themer writes them to `~/.config/environment.d/*themer*.conf`,
which reaches programs when the session is started through systemd (a display manager,
or sway as a user service).

When you start the compositor yourself from a tty, nothing reads environment.d. Source
the script themer also writes, from your shell profile, before the compositor starts:

```sh
. ~/.config/themer/env.sh      # in ~/.profile, ~/.zprofile, or ~/.bash_profile
```

Check with `echo $QT_QPA_PLATFORMTHEME` in a terminal inside the session: empty means
the variables are not reaching your programs, and Qt will look plain however the rest
is set up. themer never writes `/etc/environment`; if that is where your session reads
from, the two lines to add are:

```
QT_QPA_PLATFORMTHEME=themer
QT_PLUGIN_PATH=/usr/local/lib/themer/qt/qt5:/usr/local/lib/themer/qt/qt6
```

## Modules

**Detection.** A module themes one program, and is loadable only while that program is
installed: `present()` in the module API looks for the program itself (its binary on
`PATH`, its Flatpak, or, for a toolkit, its library), never for a config file. A
module whose program is missing is off, whatever was saved, and cannot be switched on;
apply removes anything it left from before. Every front end lists the modules in three
groups, **Loaded**, **Unloaded** and **Not installed** (`themer module list` prints
the same under `# ` headings). Modules that theme no one program (cursor, fontconfig,
web, example) are always loadable.

All on by default unless noted:

| Module | Themes |
|---|---|
| **gtk** | a GTK 3/4 theme written to `~/.config/themer/gtk/`, reached by GTK through a link `~/.themes/<set>` named after the template set (`themer_default`, labelled "Themer Default"); no `GTK_THEME` is set, since that would override the file and go stale. libadwaita programs, which load no named theme, get the set's `libadwaita.css` (or its whole `gtk.css`) through `~/.config/gtk-4.0/gtk.css`. Options `gtk2`, `gtk3`, `gtk4` turn each version off. Restarts the GTK file-chooser portal on apply so Firefox's file dialogs on Wayland follow without a logout. A GTK app keeps the theme it loaded at launch: restart it, or log out |
| **qt** | Qt 5 and 6: palette, flat stylesheet, icons, fonts, through the platform theme plugin; where qt5ct/qt6ct are installed their configs are written too. Options `qt5`, `qt6`, `platformtheme` |
| **cursor** | the mouse cursor theme and size: Xcursor default theme, `XCURSOR_*`, Xresources |
| **fontconfig** | font rendering for everything that reads fontconfig |
| **firefox** | the browser chrome (`chrome/userChrome.css`) and Firefox's own pages (`chrome/userContent.css`), as marked blocks in the profile |
| **thunderbird** | the mail window and the panes Thunderbird draws as content, the same way |
| **stylus** | userstyles for web sites, for the Stylus extension in Firefox or Chromium: `~/.config/themer/stylus/<site>.user.css`, one per `*.user.css` in the set (YouTube and Wikipedia ship). Installed once through the server (`themer server install`); Stylus then picks up every apply on its own |
| **sway** | window and bar colours, the font, `$menu`, the seat cursor; loads only where sway is in use. Borders, gaps and titlebars stay in your own config |
| **i3** | the same for i3 |
| **waybar** | the bar's style; owns `style.css` (yours is kept in `~/.config/themer/originals/` and put back on unload); under sway it replaces sway's own bar. `config.jsonc` is not touched |
| **foot** | font and colours in `foot.ini` (`[colors-dark]` and `[colors-light]`, foot 1.15+, plus `initial-color-theme`) |
| **nvim** | a `themer` colorscheme, selected from the end of `init.lua` or `init.vim` |
| **fcitx** | fcitx5 input-method variables and candidate-window theme |
| **wmenu** | a launcher script, `~/.config/themer/bin/themer-wmenu`, in the theme's colours |
| **dmenu** | the same for dmenu |
| **newt** | `NEWT_COLORS` for nmtui and other newt dialogs, each colour mapped to the nearest terminal colour. Set in environment.d; copy the line to your shell profile for programs started elsewhere |
| **st** *(off)* | a colours header; st's `config.h` needs one edit, see `modules/st.c` |
| **web** | themer-web's own page: its look is the set's `web/page.css` (Themer Default's after Adwaita, Themer Chicago's bevelled), rendered as you browse; `mode` and `template` pin it like any module, off leaves the browser's own controls |
| **hyprland** | border colours (the active one keeps Hyprland's two-colour gradient under Themer Default), group borders, the groupbar, the background and the font. Written to `~/.config/hypr/themer.lua` (loaded from the end of `hyprland.lua` with `pcall(require, "themer")`) and/or `themer.conf` (sourced from the end of `hyprland.conf`), for whichever of the two you have. Themer Chicago also squares the corners and turns shadows off |
| **hyprlock** | with no `hyprlock.conf` of yours, a whole lock screen (hyprlock's own example, in the theme); with yours, `$themer_bg`, `$themer_accent`, `$themer_font` and the rest in `~/.config/hypr/themer-colors.conf`, sourced from the top of your file for your widgets to use |
| **niri** | focus ring, border, urgent and insert-hint colours and the workspace background, in `~/.config/niri/themer.kdl`, included from the end of `config.kdl` (niri 25.11 and later; older ones are left alone) |
| **river** | background and border colours as `riverctl` calls in `~/.config/river/themer`, run from the end of your `init` (a shell init; river-classic, the 0.3 line with `riverctl`) |
| **bspwm** | border and preselection colours as `bspc config` calls in `~/.config/bspwm/themer`, run from the end of your `bspwmrc` and at once while bspwm runs |
| **dwm** | a header with `fonts[]`, `dmenufont[]` and `colors[][3]`; dwm's `config.h` needs one edit, see `modules/dwm.c` (`$DWM_SRC` gets a copy) |
| **labwc** | title bars, borders, menus and on-screen displays at the end of `themerc-override` (over whatever theme you use), and the fonts in `rc.xml`'s `<theme>`, after yours; Themer Chicago adds square corners |
| **openbox** | a theme, `Themer`, in `~/.local/share/themes/Themer/openbox-3/`, selected and given the fonts by two marked blocks in `~/.config/openbox/rc.xml` (Clearlooks recoloured under Themer Default; flat, bevelled title bars under Themer Chicago) |
| **kitty** | colors, font and tab bar in `~/.config/kitty/themer.conf`, included from the end of `kitty.conf`; running kittys reload (SIGUSR1) |
| **alacritty** | colors and font in `~/.config/alacritty/themer.toml`, added to `general.import` in `alacritty.toml` (Alacritty 0.14+) |
| **ghostty** | colors and font in `~/.config/ghostty/themer`, read with `config-file = ?themer` from the end of Ghostty's config; Ghostty 1.2+ reloads (SIGUSR2) |
| **wezterm** | a `themer` color scheme in `~/.config/themer/wezterm/`, selected (with the font and window frame) from `wezterm.lua` |
| **tmux** | the status line, pane borders, messages, menus and copy mode in `~/.config/themer/tmux/themer.conf`, sourced from the end of `~/.tmux.conf` (or `~/.config/tmux/tmux.conf`); a running server re-reads it |
| **rofi** | the rofi launcher (X11, and Wayland from rofi 2.0): a theme, `~/.local/share/rofi/themes/themer.rasi`, selected by a marked `@theme` line at the end of `~/.config/rofi/config.rasi` |
| **fuzzel** | the fuzzel launcher (Wayland): its font, `[colors]` and `[border]` keys in `~/.config/fuzzel/fuzzel.ini`, set in place |
| **mako** | mako notifications (Wayland): font, colors, border and progress bar in `~/.config/mako/config`, set in place, plus the border of low and high urgency |
| **dunst** | dunst notifications: a drop-in, `~/.config/dunst/dunstrc.d/90-themer.conf`; your `dunstrc` is not edited |
| **swaync** | SwayNotificationCenter: its color names and custom properties, and the font, in a marked block at the end of `~/.config/swaync/style.css` |
| **swaylock** | the swaylock lock screen (and swaylock-effects): the ring, its inside, line and text in every state, and the font, in `~/.config/swaylock/config`, set in place |
| **btop** | the system monitor's colors as a theme of its own, `~/.config/btop/themes/themer.theme`, selected by `color_theme` in `btop.conf` (changed in place; Themer Chicago also squares the boxes) |
| **zathura** | the document viewer's colors and font, a marked block at the end of `zathurarc` |
| **bat** | syntax colors from the terminal's palette: `--theme="ansi"` (or `base16`) at the end of bat's `config` |
| **lazygit** | the git interface's colors, `~/.config/themer/lazygit/config.yml`, merged over yours through `LG_CONFIG_FILE` (environment.d) |
| **fzf** | the fuzzy finder's colors, `~/.config/themer/fzf/fzfrc`, through `FZF_DEFAULT_OPTS_FILE` (environment.d; fzf 0.47+) |
| **vivid** | `LS_COLORS` for ls, fd, eza, tree and completion: a vivid theme, `~/.config/vivid/themes/themer.yml`, and its output in environment.d |
| **starship** | the prompt's colors: a `themer` palette in `starship.toml` that defines every color name as the theme's, and `palette = "themer"` |
| **helix** | Helix: a `themer` theme in `~/.config/helix/themes/themer.toml` (the interface in the theme's roles, the syntax in its terminal colors), selected with `theme = "themer"` in `config.toml`: your theme line changed in place, or a marked block at the top when you have none |
| **vscode** | VS Code, VSCodium and Code - OSS (and their Flatpaks): VS Code's own Default Dark or Light Modern, recolored, through four top-level keys of each one's `User/settings.json`; no extension is installed |
| **kde** | KDE programs (Dolphin, Kate, Okular, KWin's title bars): the theme as a KDE color scheme, `~/.local/share/color-schemes/Themer.colors`, and the same keys set in `~/.config/kdeglobals` with the fonts |
| **ly** | the login manager's colours (screen, text, the box, errors, the animations), written to `~/.config/themer/system/ly.conf` and put into `/etc/ly` by the root service (`sudo themer system install`) |
| **example** *(off)* | the template module to copy |

Built-in targets, not modules: **env** (`environment.d/50-themer.conf` and `env.sh`),
and **tty** (the console's palette and font). Its system side needs root: see
[ly and the console](#ly-and-the-console).

**How modules change files.** Every write goes through the `tm_*` helpers in
`themer_module.h` and is recorded in `~/.config/themer/undo/<module>.manifest`. Before
a module first edits one of your files in place (your sway config, `foot.ini`,
`settings.ini`...), the file is copied to `~/.config/themer/originals/`. Unloading puts
that copy back byte for byte when the file is as the module left it; when you have
changed the file since, the module's edits are taken out one by one instead (marked
blocks cut, ini keys and replaced lines given their old values, switched-off lines
switched back on), so your own changes stay. Files themer generated are deleted (only
while still its own), and directories it created are removed when empty. Every file is
written to a temporary file and renamed into place, so a crash never leaves one half
written, and a symlinked dotfile stays a symlink. Applying runs unload-then-load, so
re-applying never duplicates anything; editing your configs while a module is on is
fine. After a change each module's reload hook tells its running program: sway and i3
are reloaded when their socket variable is set, waybar gets SIGUSR2, xsettingsd gets
SIGHUP.

### Window managers

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

### Terminals

**kitty** gets `themer.conf` next to `kitty.conf`, and a marked block at the end of
`kitty.conf` (created when there is none) that includes it, so the theme is read
after your own settings: the sixteen colors, background, foreground, cursor,
selection, URL and window-border colors, the tab bar, and the font. Themer Default
keeps kitty's own tab bar in the theme's colors, the focused tab in the accent;
Themer Chicago draws flat separated tabs on a grey bar. Running kittys re-read their
config (SIGUSR1) after every apply.

**alacritty** writes `themer.toml` next to `alacritty.toml` and imports it
(`general.import`, Alacritty 0.14 and later). TOML allows a table once and wants the
root's keys before any table, so the import goes where your file already has one:
added to your `import` line (last in a one-line list, so the theme wins over your other
imports; first in a list spread over lines; the line is put back on unload), under
your `[general]` table in a marked block, or as `general.import` in a block at the top
of the file when it has neither (the file is created when there is none). Alacritty
reads imports before the file itself, so colors you set in `alacritty.toml` still win
over the theme's: take them out to follow the theme. Alacritty watches its config and
its imports, so there is nothing to reload.

**ghostty** writes `~/.config/ghostty/themer` and reads it with `config-file =
?themer` from a block at the end of `config` (or `config.ghostty` when that is the only
one; `config` is created when there is neither). Ghostty reads a `config-file` after
the file that names it, and `config` after `config.ghostty`, so the theme wins over the
same keys in your config. The font replaces your `font-family` list instead of
joining it. Ghostty 1.2 and later reload on SIGUSR2; an older Ghostty would quit on it,
so the module asks for the version first and leaves those alone.

**wezterm** writes a color scheme, `themer`, to `~/.config/themer/wezterm/themer.toml`
and selects it from `wezterm.lua` (`~/.wezterm.lua` when it exists, else
`~/.config/wezterm/wezterm.lua`): a marked block right after the line that makes the
config table (`local config = wezterm.config_builder()` or `local config = {}`) adds the
scheme's directory, picks the scheme, and sets the font and the window frame. Lines
further down your file still win, so a `config.color_scheme = ...` of your own keeps
your scheme. Without a `wezterm.lua` the block is a whole config of its own, which
unload takes away again. A `wezterm.lua` of another shape (one that returns a table
built in place) is left alone: the scheme is written, and a message gives the two
settings to add. The block names the theme and its main colors, so the file changes
with the theme and WezTerm, which watches its config, reloads. Themer Chicago turns the
fancy tab bar off for WezTerm's plain one, grey with the focused tab in the accent.

**tmux** writes `~/.config/themer/tmux/themer.conf` and sources it (`source-file -q`)
from a block at the end of `~/.tmux.conf`, or of `~/.config/tmux/tmux.conf` when only
that one exists (`~/.tmux.conf` is created when there is neither). Themer Default keeps
tmux's look: the status line in the accent where tmux's is green, messages in the
warning color where tmux's are yellow, the active pane border, copy-mode selections
and menus in the accent. Themer Chicago draws the status line as a grey taskbar with
the current window raised in the accent. Every option is set with `set -gq`, so an
older tmux skips the ones it does not have. A running server sources the file again
after every apply; options the theme stops setting keep their value until the server
restarts.

### Launchers, notifications and the lock screen

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

### The command-line tools

Each keeps its own look, in the theme's colors: the hues each tool uses for its parts
come from the theme's terminal colors, the background and text from the theme's, and
selections and the focused frame are the accent, as everywhere. All of them need the
program itself installed.

**btop** reads themes from `~/.config/btop/themes/`; themer writes `themer.theme`
there (btop's default theme, hue for hue: boxes, meters and graphs in the terminal
colors, gradients fading in from the background as btop's do) and sets
`color_theme = "themer"` in `~/.config/btop/btop.conf`. btop rewrites `btop.conf`
itself whenever a setting changes, keeping each line where it is, so the line is
changed in place rather than in a marked block, and put back on unload. A set may
change more lines the same way in `templates/<set>/btop/settings`: Themer Chicago sets
`rounded_corners = False`. Before btop's first run there is no `btop.conf`; the lines
then go in a marked block of a new file, which btop fills out on its next exit. A
running btop takes the theme when restarted.

**zathura**: a marked block of `set` lines at the end of `~/.config/zathura/zathurarc`,
read last, so it overrides the same settings above it. The page area and status bar
sit on the background, the input bar and completion on the surface, search hits in a
see-through accent. Pages keep their own colors unless recolor mode is on (Ctrl-R):
then they are drawn in the theme's text and background (`recolor-lightcolor`,
`recolor-darkcolor`). `:source` in a running zathura reads it again.

**bat** has no recolorable default theme (its Monokai Extended is fixed colors), so
the module selects one of bat's own themes that draws in the terminal's palette:
`ansi` (the eight colors) or `base16` (the sixteen), as `--theme=` at the end of
`~/.config/bat/config` (or `$BAT_CONFIG_PATH`), after your own options. The terminal
modules set that palette from the theme, so bat follows every theme and both modes
with nothing generated; a `.tmTheme` in fixed colors would need `bat cache --build`
after every apply, and would be wrong in a terminal themer does not reach.

**lazygit** merges a list of config files, later ones over earlier ones, when
`LG_CONFIG_FILE` names them. themer writes the theme's `gui.theme` keys to
`~/.config/themer/lazygit/config.yml` and sets
`LG_CONFIG_FILE=<your config.yml>,<that file>` in
`~/.config/environment.d/40-themer-lazygit.conf`. lazygit will not start when a listed
file is missing, so when you have no `~/.config/lazygit/config.yml` (or the older
`~/.config/jesseduffield/lazygit/config.yml`) themer creates one holding only an empty
marked block, and removes it on unload if nothing else was written to it.

**fzf** (0.47 and later) reads `$FZF_DEFAULT_OPTS_FILE` before `$FZF_DEFAULT_OPTS`;
themer writes `--color` options to `~/.config/themer/fzf/fzfrc` and sets the variable
in `~/.config/environment.d/40-themer-fzf.conf`. fzf's text and background stay the
terminal's, as in fzf's own scheme; the current line is the surface; matches, prompt
and markers keep fzf's hues. Options of your own in `FZF_DEFAULT_OPTS` still win.

**vivid** generates `LS_COLORS`. themer writes `~/.config/vivid/themes/themer.yml`
(vivid's default theme, molokai, in the theme's colors), runs `vivid generate` on it,
and puts the result in `~/.config/environment.d/40-themer-lscolors.conf`. A shell that
sets `LS_COLORS` itself (`eval "$(dircolors)"`, `LS_COLORS="$(vivid generate molokai)"`)
overrides the session's; write `vivid generate themer` there instead.

**starship**'s modules name their colors (`bold green`, `purple`, `bright-black`), and
starship looks a name up in the selected palette before the terminal's. themer adds a
`[palettes.themer]` table that defines all sixteen names as the theme's terminal colors
(a marked block at the end of `~/.config/starship.toml`, or `$STARSHIP_CONFIG`) and
selects it with `palette = "themer"`. That is a top-level key, which TOML allows only
before the first table: your own `palette = ...` line is changed in place (and put
back on unload), and without one the line goes in a marked block at the very top. The
prompt's layout and symbols stay yours.

Variables set through environment.d reach programs started from the session after the
next login.

### Editors and KDE

**helix**. Writes `~/.config/helix/themes/themer.toml` from `templates/<set>/helix/themer.toml`. Helix's theme keys come from a `[palette]` of the theme's roles and its terminal colors: `ui.*` in the roles (the status line and the selected menu item in the accent, popups on the surface color, the selection in the `control_active` tone), the syntax in ansi1–ansi6, comments in the `disabled` tone. In `~/.config/helix/config.toml` your `theme =` line becomes `theme = "themer"` and comes back on unload; without one, a marked block at the top of the file holds it, since a top-level key must come before any table. A running Helix takes it with `:config-reload`. Loadable when `hx` or `helix` is on PATH, or the Flatpak `com.helix_editor.Helix` is installed.

**vscode**. Sets four top-level keys in `User/settings.json` with `tm_json_set`, which leaves the rest of the file (comments included) as it is and takes them out again, or puts your values back, on unload: `workbench.colorTheme` (Default Dark Modern or Default Light Modern, by the mode), `workbench.colorCustomizations` (`templates/<set>/vscode/colors.json`: the workbench in the roles and Adwaita tones, the terminal in the theme's colors), `editor.tokenColorCustomizations` (`tokens.json`: the syntax in the terminal colors), and with `font=terminal` the editor's and the integrated terminal's font. The editor and the chrome around it keep the Modern themes' relation: on the dark side the chrome a step darker than the editor, on the light side the editor white. VS Code watches its settings, so a running one changes at once. Settings directories: `~/.config/Code` for `code` (and `~/.config/Code - OSS`, where Arch's `code` keeps them, when it is there), `~/.config/Code - OSS` for `code-oss`, `~/.config/VSCodium` for `codium`, and `~/.var/app/com.visualstudio.code/config/Code` and `~/.var/app/com.vscodium.codium/config/VSCodium` for the Flatpaks. A missing `settings.json` is created holding only these. While the module is on, your own `workbench.colorCustomizations` is replaced by the theme's.

**kde**. KDE programs take their palette from `kdeglobals`. The module writes the theme as a full KDE color scheme to `~/.local/share/color-schemes/Themer.colors` (so System Settings lists it) and sets the same keys in `~/.config/kdeglobals` with `tm_ini`, each restored on unload: the Window, View, Button, Selection, Tooltip, Complementary and Header sets (views in the `view` tone, buttons and headers in `raised`, selection in the accent; links in ansi4, visited ansi5, negative/neutral/positive text in the urgent color, the warning color and ansi2, each moved until it reads 4.5:1), KWin's title bar colors under `[WM]`, `[General] ColorScheme=Themer`, the fonts (`font`, `menuFont`, `toolBarFont`, `smallestReadableFont`, `fixed`, `[WM] activeFont`) and the icon theme. Plasma tells running programs; outside Plasma, restart them. Loadable when KDE Frameworks are installed (`libKF6ConfigCore` or `libKF5ConfigCore`, or `kreadconfig6`/`kreadconfig5`).

### ly and the console

ly and the console's palette and font are system settings that only root can
change. Every apply, as you, writes what they should be into
`~/.config/themer/system/` (`tty.palette` in setvtrgb's format, `console.conf`, and
`ly.conf` while the ly module is on). Once, as root,

```
sudo themer system install
```

(run it from an installed themer: `make install` puts it where only root can change
it, and install refuses a binary anyone else could, since the service runs it as root)
sets up, for the init system that is running, what puts them into effect as root, at
boot and whenever a file there changes:

| init | what install writes | enabled with |
| --- | --- | --- |
| systemd | `themer-system-<you>.path` watching the directory, running `themer-system-<you>.service` (`themer system sync <you>`) | `systemctl enable --now` |
| OpenRC | `/etc/init.d/themer-system-<you>` | `rc-update add … default` |
| runit | `/etc/sv/themer-system-<you>` (Artix: `/etc/runit/sv`) | a link in `/var/service` (Artix: `/run/runit/service`) |
| s6 (s6-rc) | `/etc/s6/sv/themer-system-<you>`, a longrun | added to the `default` bundle, `s6-db-reload` |
| dinit | `/etc/dinit.d/themer-system-<you>` | `dinitctl enable` |
| SysV init | `/etc/init.d/themer-system-<you>` (LSB) | `update-rc.d` or `chkconfig` |

Except under systemd, the service runs `themer system watch <you>`, which syncs at start
and again whenever the directory changes (inotify), and looks every half minute while
your home is not there yet (mounted or decrypted after boot). With none of these,
install syncs once and tells you the command for your init to run.

The sync puts the palette in with setvtrgb (kept in `/etc/themer/tty.palette` for boot),
the font in `/etc/vconsole.conf`, ly's colors in `/etc/ly/config.lua` (or
`config.ini`). It does not touch GRUB. It changes these key by key: for each key, `/etc/themer/backup/` records
the line that was there (or that there was none) and the line themer wrote. When your
file goes away (Remove all styles, the ly module off), and on `sudo themer system
remove`, only the lines that still read as themer wrote them go back, so whatever else
was changed in those files in the meantime stays. Remove also takes the service away.
`themer system status` says what is in place. It is installed for one user at a time.

The sync runs as root on files you can write, so it trusts nothing in them: it reads
them as you (never through a link, only regular files of yours, a few kilobytes at
most), and takes only what each may hold (sixteen palette lines of 0–255, a font name,
ly's ten color keys with `0x` values). A file holding
anything else is refused and the setting is left as it was.

Without root, a console you log in to still gets the palette at once: `env.sh`, sourced
from your profile (see [Environment](#environment)), sets it with the console's own
escape sequences when `TERM` is `linux`. The background and the text are palette
entries 0 and 7, which the console draws the screen and plain text in.

### Where themer keeps its files

Everything of themer's own is under `~/.config/themer`; the configs a module generates
go where each program reads them.

```
~/.config/themer/
  state            your settings: theme, mode, fonts, icons, modules, options, port
  themes/          your colour schemes (imported ones too); .removed: hidden built-ins
  templates/       template sets (the built-in ones mirrored here, to edit)
  modules/         modules of your own (.so)
  undo/            what each module wrote, so it can be taken back
  originals/       your files as they were before a module edited them
  stylus/          the rendered userstyles
  bin/             the launcher scripts ($menu for sway and i3)
  qt/ gtk/ ...     files a module keeps for itself
  cache/           font specimens, the TUI's error log, themer-gtk's empty base theme
  env.sh, tty.rgb  the environment and the console palette
```

### Module options

Set them on the Modules page of any front end, or with `themer module option`; `themer
module reset` puts one back to its default. Two options every module has:

- **`mode`** (`auto` \| `dark` \| `light`, default `auto`): which side of the theme the
  program uses. `auto` follows the global mode; `dark` or `light` pins that program to
  that side whatever the global mode is, say a dark terminal on a light desktop. The
  host renders the module from that side, so it works for third-party modules too.
  Modules that draw no colours (cursor, fontconfig) have none.
- **`template`**: the template set it is drawn from. Unset, it follows the global set
  (`themer_default` by default); set, it pins that module to a set of its own.

| Module | Option | Values | Default |
|---|---|---|---|
| gtk | `font` | `ui` \| `terminal` | `ui` |
| | `gtk2`, `gtk3`, `gtk4` | `on` \| `off` | `on` |
| | `gsettings`, `xsettingsd` | `on` \| `off` | `on` |
| qt | `font` | `ui` \| `terminal` | `ui` |
| | `qt5`, `qt6` | `on` \| `off` | `on` |
| | `platformtheme` | `auto` \| `themer` \| `qt5ct` | `auto` (the plugin when installed) |
| firefox | `font` | `own` (Firefox's, the system font) \| `ui` \| `terminal` | `own` |
| | `icons` | `0`–`8`, glyph size in points above the text | `2` |
| | `size` | icon size in pixels, `0` = text size plus `icons` | `0` |
| | `style` | `firefox` (Firefox's own icons) \| `image` (the icon theme's icons, recoloured) \| `mask` (the icon theme's shapes in the bar ink) | `image` |
| | `recolor` | `all` \| `symbolic` | `all` |
| | `raster` | `color` \| `ink` | `color` |
| thunderbird | `font` | `own` (Thunderbird's, the system font) \| `ui` \| `terminal` | `own` |
| stylus | `font` | `site` (each site's own) \| `ui` \| `terminal` | `site` |
| | `open` | `apply` (Apply in a front end opens each site that is on in the browser, for Stylus to install or update) \| `never` | `apply` |
| | one per site: `youtube`, ... | `on` \| `off` (off leaves the site alone) | `on` |
| waybar | `font` | `ui` \| `terminal` | `terminal` |
| | `padding` | `tight` \| `normal` \| `loose` | `normal` |
| fcitx, wmenu, dmenu | `font` | `ui` \| `terminal` | `ui` |
| sway, i3 | `font` | `ui` \| `terminal` | `ui` |
| | `menu` | `auto` (the first themed one: fuzzel, rofi, wmenu, dmenu; fuzzel and wmenu only under sway) \| `fuzzel` \| `rofi` \| `wmenu` \| `dmenu` \| `none` | `auto` |
| | `bar` | `auto` (your `bar { }` blocks switched off and replaced: by waybar under sway when the waybar module is on, by themer's bar otherwise, which keeps your bar's `status_command`) \| `themer` (always themer's bar) \| `keep` (yours as it is) | `auto` |
| hyprland, hyprlock, labwc, openbox | `font` | `ui` \| `terminal` | `ui` |
| dwm | `font` | `ui` \| `terminal` | `terminal` |
| kitty, alacritty, ghostty, wezterm | `font` | `terminal` \| `ui` | `terminal` |
| rofi, fuzzel, mako, dunst, swaync, swaylock | `font` | `ui` \| `terminal` | `ui` |
| zathura | `font` | `terminal` (zathura's own default is monospace) \| `ui` | `terminal` |
| bat | `theme` | `ansi` (the terminal's eight colors) \| `base16` (all sixteen) | `ansi` |
| vscode | `font` | `own` (VS Code's) \| `terminal` (the editor's and its terminal's) | `own` |
| kde | `font` | `ui` \| `terminal` (menus, toolbars, windows; the fixed font is always the terminal font) | `ui` |
| example | `style` | `short` \| `long` | |

## Templates

A module's files come from templates. A set is one directory,
`~/.config/themer/templates/<set>/`, holding one subdirectory per module (modules with
the same output share one) and one file per output:

```
gtk/          gtk.css                        GTK 3 and 4
qt/           qt.qss, colors.conf, *.svg     the plugin, qt5ct and qt6ct
firefox/      userChrome.css, userContent.css
thunderbird/  tokens.css, chrome.css, content.css
stylus/       youtube.user.css, wikipedia.user.css, ...  Stylus userstyles, one per site
wm/           themer.conf, bar.conf          sway and i3
launcher/     launcher                       wmenu and dmenu
waybar/       style.css
nvim/         themer.lua, select.lua, select.vim
fcitx/        theme.conf
newt/         newt.conf
st/           colors.h
web/          page.css                       themer-web's own page
example/      colors
set.conf      name=, description=, author=, url=   (optional)
```

Two sets are built in:

- **themer_default** (Themer Default), the default: GNOME's Adwaita in the theme's
  colours. GTK 3 and GTK 4 get GTK's own Adwaita (see below), libadwaita programs keep
  libadwaita's look with its named colours set from the theme, Qt gets a stylesheet
  drawn after Adwaita, and Firefox, Thunderbird, YouTube and Wikipedia stay as they
  come, recoloured: only their colour variables are set, nothing is moved, resized or
  rounded differently.
- **themer_chicago** (Themer Chicago): flat, square and outlined everywhere, with a
  Chicago bevel on controls and sway-style tabs, and the icon theme's icons in the
  Firefox toolbar.

They are installed under `PREFIX/share/themer/templates`, and every start of themer
mirrors them into `~/.config/themer/templates/<set>/`, which is the copy that is used:
edit it there. The mirror keeps itself current without losing your edits:

- a file you have not touched follows the built-in when `make install` brings a new one;
- a file you changed is yours and stays (themer says so once if the built-in moved on,
  naming the new one under `PREFIX` to compare);
- a file you delete comes back;
- a copy left by an old build that differs is replaced and kept beside as `<file>.themer-old`;
- a mirrored file whose built-in is gone after an upgrade is removed again, unless you changed it.

How it tells the two apart is `.seeded` in that directory, a hash of each file as
themer last wrote it. Your own sets live next to them, with names of letters, digits,
`-` and `_` (not a built-in set's name).

**Switching sets:** `themer set NAME` makes every module render from `NAME` and
applies. A set need not cover every module: what it lacks comes from `themer_default`,
so a set that restyles only Firefox still applies to the whole desktop. One module can
be pinned to another set with its `template` option: `themer module option firefox
template themer_chicago` gives Firefox the Chicago chrome under Themer Default.

To work on the look, edit `~/.config/themer/templates/<set>/` and apply. For a
variant of your own:

```sh
themer set copy themer_default mine
# edit ~/.config/themer/templates/mine/
themer set mine
```

**Themer Default's GTK files** are not written by hand. `tools/adwaita/build.sh`
compiles the SCSS of GTK 3's Adwaita and GTK 4's default theme (from a GTK source tree)
with every colour left open: where Adwaita works out a shade at build time (darken,
mix, transparentize), the output has the GTK expression that works it out from the
theme's colours when GTK loads the file (`mix()`, `alpha()`), scaled so that Adwaita's
own palette comes out as Adwaita. The few base colours are named at the top of each
file from the theme (`@define-color themer_bg_color #{{bg}};` and so on), so editing
those recolours the whole theme. Re-run it to follow a new GTK release:

```sh
git clone --depth 1 -b gtk-3-24 https://gitlab.gnome.org/GNOME/gtk.git gtk3
git clone --depth 1 -b gtk-4-24 https://gitlab.gnome.org/GNOME/gtk.git gtk4
tools/adwaita/build.sh gtk3 gtk4 /path/to/dart-sass/sass
```

GTK 3 picks `gtk-dark.css` and GTK 4 its `gtk4-dark.css` in dark mode, as for any
theme; the GTK 4 files avoid the relative colours GTK took in 4.16, so they load in
older GTK 4 too.

## Writing a template set

A set is a directory of ordinary theme files with `{{variables}}` in them. Nothing
else: no build, no manifest format, and a set may cover one module or all of them. The
quickest start is a theme you already use:

```sh
themer set import mylook gtk ~/.themes/Gruvbox-Material          # a GTK theme dir
themer set import mylook qt ~/.config/qt5ct/qss/mine.qss         # a Qt stylesheet
themer set import mylook firefox ~/.mozilla/firefox/x.default/chrome
themer set import mylook waybar ~/.config/waybar/style.css
themer set check mylook
themer set mylook
```

Import takes a file or a directory. It knows the usual layouts (a GTK theme's `gtk-3.0/`
and `gtk-4.0/`, a profile's `chrome/`) and otherwise takes the directory as it is. Each
text file goes under the name the module renders, with a `generated by themer` marker
line at the top, and **every colour in it that equals one of the current theme's colours
is replaced by that colour's variable**: `#282828` in a file imported under gruvbox dark
becomes `#{{bg}}`, `#83a598` becomes `#{{acc}}`. So import with the theme and mode the
look was made for, and the file follows every theme from then on. Colours that match
nothing stay literal and are listed in the report. Everything that is not text (images,
fonts, `assets/`) is copied as it is and placed beside the module's output on apply, so
`url("assets/x.png")` keeps working.

What each module directory takes, and what becomes of it on apply:

| Directory | File | On apply |
|---|---|---|
| `gtk/` | `gtk.css` | rendered to `~/.config/themer/gtk/gtk-3.0/gtk.css` and `gtk-4.0/gtk.css`. The set's `gtk.css` is the whole theme: there is no Adwaita underneath it |
| | `gtk-dark.css` | optional: what GTK loads in dark mode |
| | `gtk4.css`, `gtk4-dark.css` | optional: GTK 4's own `gtk.css` and `gtk-dark.css`, in place of the shared ones (which then go to GTK 3 only). A set with them is a whole GTK 4 theme and is named for GTK 4; without them GTK 4 keeps its own theme beneath the set's `gtk.css` |
| | `libadwaita.css` | optional: what libadwaita programs get through `~/.config/gtk-4.0/gtk.css`, which they read instead of a named theme; without it they get the whole `gtk.css` (so the set restyles them), with it only what it says (Themer Default sets libadwaita's named colours) |
| | any other `.css` / anything else | rendered / copied into both directories |
| `qt/` | `qt.qss` | to `~/.config/themer/qt/themer.qss` (and qt5ct/qt6ct's copies). `{{qt_assets}}` is the directory it lands in: `url({{qt_assets}}/x.svg)` for images, since a Qt stylesheet's `url()` is not relative to the stylesheet |
| | `*.svg` | rendered like templates (arrows, check marks in the theme's colours) and written beside the stylesheet |
| | `colors.conf` | the qt5ct/qt6ct palette; `{{qt_active}}`, `{{qt_disabled}}`, `{{qt_inactive}}` are the role lists built from the theme |
| `firefox/` | `userChrome.css`, `userContent.css` | a marked block at the end of the profile's files; other files copied into `chrome/`. Variables: `{{tab_font}}`, `{{tab_size}}`, `{{glyph_size}}`, `{{icon_hover_filter}}`, and nine per icon, data URIs from the icon theme: `{{icon_X}}` `{{icon_X_hover}}` (the icon in the bar's ink and hover ink), `{{raster_X}}` `{{tint_X}}` `{{tint_X_hover}}` (the icon as a mask and its colours, for bitmaps and `style=mask`), `{{menu_X}}` `{{menu_X_hover}}` `{{mask_X}}` `{{menu_tint_X}}` (the same for menus). Unused forms are `none` or `transparent`. X is one of: back forward reload stop home newtab menu star star_off downloads extensions reader identity close prev next down sidebar history library bookmarks folder search more screenshot account sound muted translate window private password print save zoom_in zoom_out fullscreen settings tools help quit shield tag appearance access info. Rules between `/* themer:icons */` and `/* themer:icons end */` are left out with `style=firefox` |
| `thunderbird/` | `tokens.css`, `chrome.css`, `content.css` | tokens+chrome → userChrome, tokens+content → userContent. `{{font_family}}`, `{{font_size}}` |
| `stylus/` | `<site>.user.css` | a UserCSS file (`/* ==UserStyle==` header, `@-moz-document domain(...)`), rendered to `~/.config/themer/stylus/<site>.user.css`; every such file in the set is a site, and the built-in set's sites are kept when a set adds one. `{{font_family}}`, `{{font_size}}`, `{{font_rule}}` (a complete `font-family` declaration, empty with `font=site`). The marker goes in `@description` |
| `wm/` | `themer.conf`, `bar.conf` | included from the sway/i3 config. `{{font_family}}`, `{{font_size}}`, `{{bar_block}}`, `{{cursor_block}}` |
| `launcher/` | `launcher` | the wmenu/dmenu wrapper script. `{{menu}}`, `{{args}}` |
| `waybar/` | `style.css` | replaces waybar's `style.css`. `{{font_family}}`, `{{font_size}}`, `{{ws_pad}}`, `{{mod_pad}}` |
| `nvim/` | `themer.lua`, `select.lua`, `select.vim` | the colorscheme and the line that selects it |
| `fcitx/` | `theme.conf` | the fcitx5 classic UI theme. `{{font_family}}`, `{{font_size}}` |
| `newt/` | `newt.conf` | `NEWT_COLORS`. `{{newt_colors}}` |
| `st/` | `colors.h` | st's `colorname[]` header |
| `web/` | `page.css` | themer-web's page: its look (colours, edges, corners, padding), served as `/page.css`; the layout is the page's own. The shared names are CSS variables there (`var(--acc)`), so the page redraws as you browse |
| `hyprland/` | `themer.lua`, `themer.conf` | written to `~/.config/hypr/`; `themer.lua` loaded from `hyprland.lua`, `themer.conf` sourced from `hyprland.conf`. Variables: `{{font_family}}`, `{{font_size}}` |
| `hyprlock/` | `hyprlock.conf`, `colors.conf` | `hyprlock.conf` is the whole lock screen when you have none; `colors.conf` becomes `themer-colors.conf`, sourced from yours. Variables: `{{font_family}}`, `{{font_size}}` |
| `niri/` | `themer.kdl` | written to `~/.config/niri/`, included from `config.kdl` |
| `river/` | `themer` | `riverctl` calls, a shell file run from `init` |
| `bspwm/` | `themer` | `bspc config` calls, a shell file run from `bspwmrc` |
| `dwm/` | `colors.h` | the header (`~/.config/themer/dwm/colors.h`, and `$DWM_SRC/themer-colors.h`). Variables: `{{font_family}}`, `{{font_size}}` |
| `labwc/` | `themerc`, `theme.xml` | `themerc` goes at the end of `themerc-override`; `theme.xml` is the inside of `<theme>` in `rc.xml`. Variables: `{{font_family}}`, `{{font_size}}` |
| `openbox/` | `themerc`, `fonts.xml` | the theme's `openbox-3/themerc`; `fonts.xml` goes in `rc.xml`'s `<theme>`. Variables: `{{font_family}}`, `{{font_size}}`, `{{acc_hi}}` and `{{acc_lo}}` (the accent a step lighter and darker) |

Every file sees the shared variables plus its module's own. Rendered text files must
start with the marker (import adds it): that is how themer tells its own files from
yours when it overwrites and removes them. An unknown `{{name}}` is an error, reported
on apply, so a typo cannot reach the output.

| Shared variables | |
|---|---|
| roles | `bg fg surf acc accfg urg warn line on_urg on_warn` |
| Chicago tones | `face light shade pressed field hover_bg hover_fg disabled bar input bar_text input_text tab_text tab_active_text tab_hover_text hover_text glyph_hover` |
| Adwaita tones | `view raised sidebar control control_hover control_active` |
| terminal | `ansi0` … `ansi15` |
| other | `mode` (dark/light), `theme`, `icons`, `cursor`, `cursor_size`, `hinting`, `antialias` (1/0), `rgba`, `ui_font`, `ui_size`, `mono_font`, `mono_size`, `tty_font`, `pad_control`, `pad_field`, `pad_cell`, `pad_surface`, `pad_tip` |

To share a set, give people the directory; it goes under
`~/.config/themer/templates/<name>/` (or `PREFIX/share/themer/templates/` for a package)
and shows up in `themer set list`.

## Design language

Every set draws every module from the same roles (listed in `themer_module.h`), so GTK,
Qt, Firefox, sway/i3 and waybar look like one design:

| Role | Used for |
|---|---|
| `bg`, `fg` | windows, bars, menus; text on them |
| `surf` | controls, inactive tabs, fields in the bar |
| `acc`, `accfg` | the selected tab, focus rings, progress; text on acc |
| `urg`, `warn` | urgent windows and tabs; modes and warnings (text: `on_urg`, `on_warn`) |
| `line` | every 1px outline |

The tones are derived from the roles in `src/palette.c`, two families, one per
built-in set (any set may use either):

- **Adwaita tones** (Themer Default): `view` is the colour of views and fields
  (toward white on light themes, a little darker than the window on dark ones);
  `raised` header bars, popovers, cards and menus (as light as a view on light themes,
  a step toward the text on dark ones); `sidebar` a step toward the text; `control`,
  `control_hover` and `control_active` the buttons, as libadwaita draws them: the text
  over the window at 10%, 20% and 30%.
- **Chicago tones** (Themer Chicago): `light` and `shade` are the Chicago bevel (raised
  controls lit at top and left, shaded at bottom and right; sunken fields the other way
  round); `hover_bg`/`hover_fg` are the accent when it reads 3:1 on the background,
  otherwise the background moved toward the text until it does; `bar` and `input` are
  the browser bar and its search field a step lighter; `pad_*` is one spacing scale
  (4px 12px for buttons, tabs and menu items; 4px 6px for fields; 3px 6px for list
  cells; 2px 0 for menus; 4px 8px for tooltips). Tabs look like sway/i3 tabs in GTK
  notebooks, Firefox and sway's tabbed layout. `glyph_hover`, the accent made to read on
  the bar, is Themer Default's link colour too.

## Readability

Every theme is made readable when it is loaded, for both modes: text reads at 4.5:1 on
the surface it sits on, the selected-text colour reads on the accent, and the urgent,
warning and terminal colours read at 4.5:1 on the background. A colour that fails is
moved toward the theme's own text colour; black or white replaces it only where no move
works. `themer check` lists every colour that is adjusted. Text on the accent, the
urgent colour and the warning colour is chosen per colour (`accfg`, `on_urg`,
`on_warn`), since one text colour cannot read on all three.

## Firefox

**Themer Default** leaves Firefox as it comes, in the theme's colours: it sets only the
colours Firefox's own theme starts from — the tab strip on the window colour, the
toolbars, the selected tab and the panels a step raised (`raised`), the URL field a
little toward the text and a step lighter (`input`) while it has focus, the text, and
the accent for focus rings, the selected suggestion, links and the primary button.
The tints Firefox works out from those (hovers, presses, the boxes in the URL bar,
separators) it still works out itself, as for any theme; its layout, icons, corners
and animations are its own and follow its updates, and so is its font (the system's,
which the gtk module sets) unless the `font` option says otherwise. The same goes for
the Stylus styles under Themer Default: YouTube and Wikipedia keep their own shapes and
hover effects and take only the colours.

**Fonts are an option.** The `font` option of firefox and thunderbird is `own` by
default (the program's own font, the system's), and stylus's is `site` (each site's
own); `ui` or `terminal` sets the theme's font instead, in either set. The icon options (`icons`, `size`, `recolor`, `raster`, `style`) are
for sets that draw the toolbar icons, as Themer Chicago does; under Themer Default
Firefox's own icons take the text colour.

**Themer Chicago:** the chrome is three stacked rows at the top of the window, all the same height: the
tabs, the navigation buttons with the URL bar, and the bookmarks. No rounded corners or
shadows. Tabs are raised controls with the Chicago bevel, flush in one row, like GTK
notebook tabs: the selected tab takes the accent, the others sit on the surface colour,
hover is the hover colour, a tab that needs attention the urgent colour, a loading tab
the warning colour. The URL bar is a sunken field in the terminal font with the accent
outline when focused; its suggestions hang from it in the same field. Toolbar buttons
center their icons with no hover box: the hover is the icon's ink alone. Popups, the
Library, dialogs and the sidebar are flat and outlined.

Firefox's own pages (settings, add-ons, downloads, the new tab, every `about:` page) are
covered by `userContent.css`, which sets Firefox's in-content variables to the theme's
roles and gives buttons and fields the bevel, and also covers `file://` listings and the
PDF viewer. The GTK file chooser is GTK's: the gtk module themes it.

The profile written to is the one a running Firefox holds, otherwise the one used most
recently; the module prints which, and whether a restart is needed (`userChrome.css` is
read at profile startup only). It needs
`toolkit.legacyUserProfileCustomizations.stylesheets = true` in `about:config`; the
module does not write `user.js`. Profiles are looked for in `~/.config/mozilla/firefox`,
`~/.mozilla/firefox`, the snap and the Flatpak.

**Icons:** every icon in the chrome comes from the installed icon theme: the toolbar
buttons (back, forward, reload, home, new tab, menu, downloads, extensions, bookmark,
reader, the site identity, tab close, sidebar, history, library, screenshot, account,
print, save, settings, zoom, fullscreen), the tab strip's scroll arrows and audio
marks, the findbar's arrows, the bookmarks toolbar's folders and chevron, the close
buttons of notifications and panels, and the app menu's items. Toolbar icons are
recolored to the bar's ink with a hover variant; icons inside menus are the shape as
a mask over the text color, so they follow hover and selection like their label.
Icons are looked up in the theme, searched along
its `Inherits` chain, then hicolor, then Adwaita. Icons are embedded in the stylesheet
as `list-style-image`, so Firefox needs no file access. By default each icon is the
theme's own, recoloured to the bar's ink; `style=mask` draws every icon's shape as a CSS
mask over the ink instead, in a fixed box, which evens out an icon theme with mixed
artwork. A glyph missing from every theme in the chain comes from the bundled Adwaita
icons, so every button has an icon whatever the icon theme.
`style=firefox` leaves the icon theme out of Firefox altogether: Firefox draws its own
icons, in the bar's ink as it does for any theme, and only their size follows the
`size` and `icons` options. The template marks every icon rule between
`/* themer:icons */` and `/* themer:icons end */` comments, and the module cuts those
stretches out; a set of your own does the same with its icon rules.

**How the template reaches Firefox's own rules:** Firefox draws the chrome from design
tokens declared on `:root` and, inside its custom elements, on `:host` in shadow trees
that `userChrome.css` cannot reach. The template declares every token on `*` with
`!important`, which wins in both places, under both the names of Firefox 115–140 and
those of the "Nova" design in 158. Checked with `test/firefox.sh` in Firefox 140 (ESR)
and 158.

Limit: a stylesheet cannot move Firefox's page-action buttons into the nav bar; use
Customize Toolbar once to drag them there.

## Stylus (web sites)

The firefox module themes the browser; the stylus module themes pages, through the
[Stylus](https://github.com/openstyles/stylus) extension, which runs in Firefox and
the Chromium family. It renders every `<site>.user.css` of the template set into
`~/.config/themer/stylus/`, as UserCSS: the metadata header Stylus reads, then the
rules under `@-moz-document domain("...")`. YouTube ships: its `--yt-spec-*`
variables set to the roles on both of YouTube's own modes, the accent on the selected
chip and guide entry and on the player's progress bar; Themer Default keeps YouTube's
own shapes, Themer Chicago squares the corners and gives buttons and chips the bevel.

Wikipedia ships too, for every language and for desktop and mobile: the Codex
design tokens Wikipedia's skins draw from set to the roles, on its day, night and
automatic modes alike; infobox, navbox and table headers on the face color (a cell
that carries a color of its own, such as a party's, keeps it); links in the accent,
visited ones in ansi5, missing pages in the urgent color; message boxes outlined with
a colored stripe for their kind. Photos are untouched; drawings that are black on
transparent (formulas, the wordmark, pictures Wikipedia marks to invert) are inverted
in dark mode, as Wikipedia does at night (the stylus module's `{{image_filter}}`).

Stylus keeps its styles inside the browser, where nothing outside can write, so each
style is installed once by hand (see [Setting up Firefox, Thunderbird and
Stylus](#setting-up-firefox-thunderbird-and-stylus)). The style's `@updateURL` points
back at themer-web and its `@version` grows with every apply, so Stylus's own update
check brings each apply in while the server runs. Each site has its own switch
(`themer module option stylus youtube off`): off leaves the site alone. A set adds a
site by adding `stylus/<site>.user.css`; the module finds it and gives it a switch,
nothing in the module changes. Not checked against live YouTube or Wikipedia here: the selectors and variable
names are from their current stylesheets (Wikipedia's from the Codex token list), and the rendered file is checked to
parse (every rule kept) in Chromium.

## Thunderbird

Thunderbird (115 and newer) draws its interface from design tokens much like Firefox's.
The set's `thunderbird/tokens.css` sets them all to the theme's roles, on every element,
so the shadow trees of Thunderbird's custom elements follow too. The folder pane,
message list and message header are content documents since 115, so the tokens go into
`userContent.css` as well. Themer Default sets only the colours: content and lists on
`view`, panes and windows on `bg`, toolbars and the message header on `raised`, buttons
on the `control` tints, the selected folder, message and space in the accent; sizes,
corners and shadows stay Thunderbird's. Themer Chicago maps content and lists to the
field colour, toolbars and the spaces bar to the bar colour, raises controls on `face`
with the bevel and squares the corners.

The profile is found as for Firefox. It needs
`toolkit.legacyUserProfileCustomizations.stylesheets = true` in the config editor, and
a restart after applying. The selectors and token names are taken from Thunderbird's
own stylesheets; not checked by running Thunderbird here.

## Colour schemes

220 schemes ship, each a `.theme` file with a `[dark]` and a `[light]` section giving
`bg`, `fg`, `surf`, `acc`, `accfg`, `urg`, `warn` and `ansi0`–`ansi15`. A handful were
taken by hand from the projects' own palettes (catppuccin, gruvbox, solarized, nord,
dracula, habamax...); the rest are the whole Gogh collection of terminal schemes
(`test/gogh-schemes.txt`), imported with `themer scheme import`. Backgrounds run from
black through mid greys to coloured, and the lights from white through cream. Each
file's first lines name its source.

**Your own.** `themer scheme import FILE [NAME]` (or **Import…** on the Theme page,
`i` in the TUI's scheme list) takes:

- a themer `.theme` file, as it is;
- Gogh lines, `name= #000000 #aa0000 ...` with the sixteen colours, any number of them;
- Xresources (`*color0: #...`, `*background:`), kitty (`color0 #...`), foot
  (`regular0=...`, `bright0=...`) or alacritty (`[colors.normal] black = "#..."`).

From the sixteen colours it picks the roles: the background (the scheme's own, or
`ansi0`), the white that reads best on it, a surface a step toward it, the red and the
yellow that read better as urgent and warning, and as the accent the most saturated
of the rest that reads 3:1 (yellows weighted down, so it is not the warning colour).
The side the scheme is (dark or light) comes from it; the other side is derived: the
same hues with the lightness flipped, each colour moved until it reads. The readability
rules check the rest on load. Imports land in `~/.config/themer/themes/`.

**Removing.** `themer scheme remove NAME` (**Remove**, `d`) deletes one of yours; a
built-in one is hidden instead, its name kept in `~/.config/themer/themes/.removed`, so
the install is left alone. `themer scheme restore` (**Restore hidden**, `r`) brings
them back. The scheme in use cannot be removed: switch first.

## Fonts

The font browser lists every installed family, from `fc-list` when present and from
the name table of each `.ttf`/`.otf`/`.ttc` under the standard font directories. The UI
font goes to GTK, Qt, waybar, sway/i3 and fcitx; the terminal font to foot and the Qt
fixed font. Ctrl-P in the font browser writes an HTML specimen in the theme's colours
to `~/.config/themer/cache/fonts/` and opens it.

## Writing a module

A module themes one program. It is one C file, built into a shared object
(`name.so`) that themer finds at startup. themer hands it the theme (colours, fonts,
options), and the module writes the program's config through themer's `tm_*` helpers.
The helpers record every change, so themer can undo the module's writes without the
module's help: you write a `load`, and unload comes for free.

The pieces, in the order you meet them:

| File | What it is |
|---|---|
| `include/themer_module.h` | the whole API, every field and helper commented (installed as `PREFIX/include/themer/themer_module.h`) |
| `modules/example.c` | a working module that renders a template: copy it |
| `templates/themer_default/example/colors` | its template |
| `modules/*.c` | the shipped modules, from the small (`newt.c`, `cursor.c`) to the large (`firefox.c`) |

### 1. Write it

The smallest module writes one file:

```c
#include "themer_module.h"

static int
present(const TmTheme *t)
{
	(void)t;
	return tm_have("myapp");          /* is myapp on PATH? */
}

static int
load(const TmTheme *t)
{
	char p[1024];

	tm_path(p, sizeof p, t, ".config/myapp/colors");     /* $HOME/.config/myapp/colors */
	return tm_writef(p, "# generated by themer\nbg=#%s\nfg=#%s\naccent=#%s\n",
	                 t->bg, t->fg, t->acc);
}

static const TmModule mod = {
	.name = "myapp",
	.desc = "myapp: its colors in ~/.config/myapp/colors",
	.default_on = 1,
	.present = present,
	.load = load,
};
TM_EXPORT(mod)
```

The `generated by themer` line is required: it is how themer tells its own files from
yours, and it refuses to overwrite a file without it.

### 2. Build and install

```sh
cc -shared -fPIC -I/usr/local/include/themer -o myapp.so myapp.c
mkdir -p ~/.config/themer/modules && cp myapp.so ~/.config/themer/modules/
```

Use your `PREFIX` in place of `/usr/local`. Modules are found in `$THEMER_MODULES`, then
`~/.config/themer/modules/`, then `PREFIX/lib/themer/modules/`; the first with a given
name wins, so a copy in your home overrides a shipped module of the same name.

### 3. Try it

```sh
themer module list                    # myapp is listed: on/off, "absent" if present() said no
themer module load myapp              # turn it on and write its files now
cat ~/.config/myapp/colors
cat ~/.config/themer/undo/myapp.manifest   # what it wrote, as themer recorded it
themer toggle                         # dark/light: the file follows
themer module unload myapp            # everything it wrote is gone or put back
```

For a module with templates, `themer template render MODULE SET FILE` prints one
rendered file without applying anything, and names any `{{variable}}` that is not
defined.

### Lifecycle

| Hook | Called | Must |
|---|---|---|
| `present(t)` | before every load, and to list the module | return 1 if the program is installed or configured here, 0 if not. Optional: without it the module always writes |
| `load(t)` | on every apply, on `module load`, when one of its options changes | write everything, return 0. themer first undoes what the last load wrote, so `load` always starts from the user's own files and never needs to check for its own leftovers |
| `unload()` | on `module unload`, and before every load, after the recorded changes are undone | clean up what the helpers could not record (rarely needed). Optional |
| `reload()` | after every apply, load and unload, of any module | tell the running program to re-read its config (a signal, an IPC call through `tm_run`). Optional. Must do nothing when the program is not running |
| `more_options(o, max)` | once, when the module is found | add options known only at run time; return how many. Optional |

When `load` returns non-zero, apply reports `module NAME failed` and carries on with the
other modules. What the failed load had already written stays recorded, and the next
apply or unload takes it away.

When `present` returns 0 the module stays on but writes nothing (anything from an
earlier load is taken away); it takes effect once the program appears. The front ends
show it as *(not installed)*.

### TmModule fields

| Field | Required | Meaning |
|---|---|---|
| `name` | yes | the name users type: `themer module load NAME`, option keys `NAME.option`. Letters, digits, `-`, `_` |
| `desc` | yes | one line, shown in every front end: `"myapp: what it themes and where"` |
| `load` | yes | see Lifecycle |
| `templates` | no | the template directory it renders from, `templates/<set>/<templates>/`. Modules may share one (sway and i3 both use `wm`) |
| `default_on` | no | 1 to be on in a fresh install |
| `present`, `unload`, `reload`, `more_options` | no | see Lifecycle |
| `options`, `noptions` | no | its options: an array of `TmOption` and its length (`TM_COUNT(opts)`) |
| `colorless` | no | 1 for a module that draws no colours (cursor, fontconfig): it gets no `mode` option |
| `watches_modules` | no | 1 when its output depends on which other modules are on (`tm_loaded`): it is reloaded whenever another module is switched |

### What the module is given: TmTheme

| Fields | Contents |
|---|---|
| `bg fg surf acc accfg urg warn line on_urg on_warn` | the colour roles, 6 hex digits without `#` (see [Design language](#design-language) for what each is for) |
| `ansi[0..15]` | the terminal colours |
| `dark` | 1 in dark mode, 0 in light (already resolved from the module's `mode` option) |
| `theme` | the theme's name, e.g. `gruvbox` |
| `ui_font ui_size mono_font mono_size tty_font` | the fonts; `tm_font_for(t, "NAME.font", ...)` picks one from a `ui\|terminal` option |
| `icons cursor cursor_size` | the icon and cursor themes |
| `hinting antialias rgba` | font rendering |
| `web_port` | where themer-web serves on localhost |
| `home` | `$HOME` (use `tm_path` to build paths under it) |
| `opts nopts`, `loaded nloaded` | read them with `tm_opt` and `tm_loaded` |

The colours in `TmTheme` are the roles only. The derived tones (`hover_bg`,
`bar_text`, `light`, `shade`, ...) are available by name with `tm_shared(t, name, ...)`,
and in templates as variables.

### Options

```c
static const TmOption opts[] = {
	/* name     description shown under it          choices         default  min max */
	{ "font",  "font for myapp's interface",         "ui|terminal",  "ui",    0,  0 },
	{ "size",  "border width in pixels",             NULL,           "1",     0,  8 },
	{ "tabs",  "theme the tab bar",                  "on|off",       "on",    0,  0 },
	TM_OPT_TEMPLATE,                     /* last, when the module renders templates */
};
```

Read a value with `tm_opt(t, "myapp.font")`: the user's value, or the default when
they set none. Values are checked when set, so a choice option is always one of its
choices and a ranged one an integer in range. themer adds a `mode` option (`auto|dark|light`) to every module that is not
`colorless` and renders the module from that side of the theme, so the module needs no
code for it.

Every front end draws an option from its fields alone, so a module never writes UI
code:

| The option has | Drawn as | Section |
|---|---|---|
| choices `on\|off` | a switch | What it themes |
| up to 4 choices, 32 characters in all | a row of buttons | Look |
| more or longer choices | a drop-down list | Look |
| `max > min` | a slider from `min` to `max` | Look |
| none of these | a text field | Look |

### Templates

A module that renders templates keeps its file contents out of the C code, so a template
set can restyle it without a rebuild:

```c
static int
load(const TmTheme *t)
{
	char body[8192], p[1024];

	if (tm_render(body, sizeof body, "myapp", tm_opt(t, "myapp.template"), "colors.conf",
	              t, NULL, 0) < 0)
		return -1;
	tm_path(p, sizeof p, t, ".config/myapp/colors.conf");
	return tm_writef(p, "%s", body);
}
```

That renders `templates/<set>/myapp/colors.conf` (set `.templates = "myapp"`). The
template sees every shared variable (see [Writing a template set](#writing-a-template-set))
and starts with its own marker line:

```
# generated by themer
background = #{{bg}}
foreground = #{{fg}}
selection  = #{{acc}}
```

Variables of the module's own are exported with `TM_VARS`, so `themer template render`
and `themer set check` see them too; `modules/example.c` shows one. Ship the template in
your set's directory, or in `~/.config/themer/templates/themer_default/myapp/` to add it
to the default set.

### Helpers

Every helper that changes a file is recorded and undone on unload. Paths are absolute;
build them with `tm_path`.

| Helper | Does | Returns | Undone on unload |
|---|---|---|---|
| `tm_path(buf, n, t, rel)` | `$HOME/rel` into `buf` | 0 | — |
| `tm_writef(path, fmt, ...)` | writes a whole file of your own (marker line required); never overwrites a file without the marker | 0, -1 | deleted |
| `tm_writex(path, fmt, ...)` | the same, executable (a script) | 0, -1 | deleted |
| `tm_replacef(path, fmt, ...)` | takes over a file the program has only one of (waybar's `style.css`): the user's is kept and put back | 0, -1 | the user's file restored |
| `tm_blockf(path, id, prefix, suffix, top, fmt, ...)` | a marked block in the user's file, first (`top = 1`) or last | 0, -1 | block cut out |
| `tm_blockf_after(path, id, prefix, suffix, after, fmt, ...)` | a marked block right after the last line starting with `after` (`[general]`, a TOML table), or last | 0, -1 | block cut out |
| `tm_json_set(path, key, value)` | one top-level key of a JSON (or JSONC) file, set to JSON text; the rest of the file as it is | 0, -1 | old value back, or the key taken out |
| `tm_ini(path, section, key, value)` | one `key=value` in an ini file | 0, -1 | old value back |
| `tm_setline(path, prefix, text)` | replaces the last line starting with `prefix`, in place | 0, 1 (no such line), -1 | old line back |
| `tm_disable_block(path, keyword)` | comments out every top-level `keyword { ... }` | count, -1 | commented back in |
| `tm_mkdirs(path)` | creates directories | 0, -1 | removed when empty |
| `tm_copy(src, dst)`, `tm_write_asset(path, data, len)` | an image, a font, a binary | 0, -1 | deleted |
| `tm_copy_tree(srcdir, dstdir, skip)` | every file of a template directory except `skip` | count, -1 | deleted |
| `tm_symlink(target, path)` | a symbolic link | 0, -1 | removed |
| `tm_render(out, n, dir, set, file, t, extra, nextra)` | one template into `out` | 0, -1 | — |
| `tm_template_dir`, `tm_template_names`, `tm_set_info` | where a set's files are, which there are, its label | 0 or count, -1 | — |
| `tm_opt(t, "mod.opt")`, `tm_loaded(t, "mod")`, `tm_font_for(...)` | options, other modules, the font an option picks | value or NULL, 1/0, — | — |
| `tm_shared(t, name, out, n)` | a derived tone by name (`hover_bg`) | 0, -1 | — |
| `tm_contrast`, `tm_mix`, `tm_ensure_contrast`, `tm_text_on` | colour arithmetic (WCAG contrast, mixing in tenths) | see the header | — |
| `tm_have(prog)` | is `prog` on `PATH` | 1, 0 | — |
| `tm_have_flatpak(id)`, `tm_have_lib(soname)` | is the Flatpak app, or the shared library, installed (for `present()`) | 1, 0 | — |
| `tm_signal(name, sig)` | signals the user's processes with that name (a reload) | count | — |
| `tm_run(argv)` | runs a program without a shell, output discarded | exit status, -1 | — |

### Rules

- **Write only through the helpers.** A file written with `fopen` is not recorded, so
  unload cannot take it away.
- **Never replace a user's file with `tm_writef`.** Edit it with `tm_blockf`, `tm_ini` or
  `tm_setline`; take it over only with `tm_replacef`.
- **Put generated files under `~/.config/themer/`** when the program can include them
  from there, and add one include line to the user's config with `tm_blockf`: the
  user's file stays theirs.
- **Run programs with `tm_run`**, never `system()`: names with spaces or quotes are
  passed as they are.
- **Keep `reload` harmless** when the program is not running, and `load` free of
  questions: it runs unattended on every apply.
- **Use the roles as the other modules do** (the table in [Design language](#design-language)), so
  the program looks like the rest of the desktop.

### Compatibility

Modules are compiled against the layout of `TmModule` and `TmTheme`, numbered by
`TM_ABI` in the header. A module built for another number is skipped with a message,
never loaded, so a stale module cannot crash themer. After a `make install` that changes
the number, rebuild the modules in `~/.config/themer/modules/`.

## Upgrading

- From a build that rendered the theme into `~/.config/gtk-3.0/gtk.css`: the gtk
  module moves that file to `gtk.css.themer-old` on the next apply (a user stylesheet
  outranks the theme, so left there it would keep feeding its colours to programs that
  read the named colours); delete the copy once nothing is missed.
- From a build whose templates were `~/.config/themer/templates/<dir>/<set>/`: the
  layout is `<set>/<dir>/` now; the old directories are not read, delete them. A saved
  set name "Themer Chicago" is read as `themer_chicago`, and so is a saved template
  option of `default`.
- From a build before Themer Default: your state file names the set you used, so
  nothing changes on its own; `themer set themer_default` switches to the new
  default. Under a set with GTK 4 files of its own (`gtk4.css`, as Themer Default
  has), GTK 4 programs get the set's theme by name too (`gtk-theme-name` in
  `~/.config/gtk-4.0/settings.ini`), as GTK 3 ones do; under any other set GTK 4
  keeps its own theme beneath the set's `gtk.css`, as before.
- From a build with the single `wm` module: `themer module unload wm` with that build
  first. From a build whose waybar module used a marked block: `themer module unload
  waybar` with that build.
- Modules built before TM_ABI 6 must be rebuilt (TmModule gained `colorless`,
  `more_options` and `watches_modules`; TmTheme gained `web_port`).
- From a build that kept its undo logs in `~/.local/state/themer` and the files it
  replaced as `<file>.themer-orig`: both are still read, so unload works as before;
  new loads keep them in `~/.config/themer/undo/` and `originals/`.
- The stylus module's `server` and `port` options are gone: the server is started by
  hand (Server, or `themer server start`), its port is in the state (`themer server
  port N`). sway/i3's `bar` option is `auto|themer|keep` (a saved `on` reads as
  `themer`, `off` as `keep`).
- The sway/i3 border, gaps and titlebar options and the qt interface options are gone:
  set those in your own config; a saved value is ignored.

## Status and license

**0.1.** Proven on sway (Wayland) with GTK 3.24 and 4, Qt 5.15 and 6, Firefox 140,
fcitx5, waybar and foot; the i3/X11 paths run in `make check` but have had less time
on a real X session. `set import` is a converter of best effort: an imported theme is a
starting point to edit, not a finished set.

GPL-2.0-only, see [LICENSE](LICENSE). Copyright (c) 2026 Ronald.
