# Modules: tests and screenshots

For 1.1 every module was tested against the real program wherever it could be had: built from
source or taken from a release, run with what themer writes in both template sets, dark and
light, several colour schemes and every option value; a second apply had to change nothing,
and unloading had to leave the user's config byte for byte as before (edits made while themed
kept). A few programs could not be built in the test environment; their configuration was
checked against the program's own source instead, and they are marked so. Every screenshot is
of the real program, each module in two colour schemes of its own: Themer Default on the
left, Themer Chicago on the right.

✔ tested on the program · ✔ fixed: tested, a bug found and fixed for 1.1 · ◐ partly tested ·
◯ source: not run, checked against the program's source

| Module | | Tested on |
|---|---|---|
| [sway](#sway) | ✔ | sway 1.12 on wlroots 0.20.2, built from source |
| [i3](#i3) | ✔ | i3 4.25.1, built from source |
| [hyprland](#hyprland) | ◯ source | Hyprland 0.57 source |
| [niri](#niri) | ✔ | niri 26.04, built from source |
| [river](#river) | ◯ source | river 0.3 source |
| [labwc](#labwc) | ✔ | labwc 0.20.2, built from source |
| [bspwm](#bspwm) | ✔ | bspwm 0.9.12, built from source |
| [dwm](#dwm) | ✔ | dwm 6.4, built from source with the generated header |
| [openbox](#openbox) | ✔ | Openbox 3.6.1, built from source |
| [waybar](#waybar) | ✔ | Waybar 0.15.0, built from source |
| [dmenu](#dmenu) | ✔ fixed | dmenu 5.4, built from source |
| [wmenu](#wmenu) | ✔ | wmenu 0.2.0, built from source |
| [rofi](#rofi) | ✔ | rofi 2.0.0 (X11), built from source |
| [fuzzel](#fuzzel) | ✔ | fuzzel 1.15.0, built from source |
| [mako](#mako) | ✔ | mako 1.11.0, built from source |
| [dunst](#dunst) | ✔ | dunst 1.13.2, built from source |
| [swaync](#swaync) | ✔ fixed · ◯ source | SwayNotificationCenter 0.12 source |
| [swaylock](#swaylock) | ✔ | swaylock 1.8.6, built from source |
| [hyprlock](#hyprlock) | ◯ source | hyprlock 0.9.6 source |
| [foot](#foot) | ✔ | foot 1.28.0 (release binary) |
| [kitty](#kitty) | ✔ | kitty 0.49.2 (release bundle) |
| [alacritty](#alacritty) | ✔ | Alacritty 0.17.0 (an AppImage build of the release) |
| [wezterm](#wezterm) | ✔ fixed | WezTerm 20240203 (AppImage) |
| [ghostty](#ghostty) | ✔ | Ghostty 1.4.0 (an AppImage build of the release) |
| [st](#st) | ✔ fixed | st 0.9.3, built from source |
| [gtk](#gtk) | ✔ fixed | GTK 3.24 and 4.14 (test programs) |
| [qt](#qt) | ✔ fixed | Qt 5.15 with themer's plugin and with qt5ct 1.2 (built from source) |
| [kde](#kde) | ✔ fixed | KDE KConfig 5.115 and KColorScheme, built from source |
| [fontconfig](#fontconfig) | ✔ | fontconfig 2.15 |
| [cursor](#cursor) | ✔ fixed | X cursors on Xvfb, with xrdb |
| [fcitx](#fcitx) | ✔ fixed | fcitx5 5.1.7, built from source |
| [firefox](#firefox) | ✔ fixed | Firefox 159.0 (official build, repackaged as an AppImage) |
| [thunderbird](#thunderbird) | ✔ fixed | Thunderbird 159.0 (official build, repackaged as an AppImage) |
| [vscode](#vscode) | ✔ fixed | VSCodium 1.143 |
| [web](#web) | ✔ | themer-web, in Chromium and Firefox 159 |
| [helix](#helix) | ✔ fixed | Helix 25.07.1 |
| [nvim](#nvim) | ✔ fixed | Neovim 0.12.6 |
| [zathura](#zathura) | ✔ | zathura 0.5.14 (GTK 3), built from source |
| [tmux](#tmux) | ✔ | tmux 3.4 |
| [btop](#btop) | ✔ | btop 1.4.6 |
| [lazygit](#lazygit) | ✔ fixed | lazygit 0.66.2 |
| [fzf](#fzf) | ✔ | fzf 0.74.5 |
| [bat](#bat) | ✔ | bat 0.26.1 |
| [starship](#starship) | ✔ | starship 1.27.0 |
| [vivid](#vivid) | ✔ | vivid 0.11.1 with GNU ls |
| [newt](#newt) | ✔ fixed | newt 0.52.24 and whiptail, built from source |
| [tty](#tty) | ◐ | kbd's setvtrgb parser and the kernel's palette escapes (no console here) |
| [ly](#ly) | ◯ source | ly 1.5.0 source |

## Window managers

### sway

✔ **sway 1.12 on wlroots 0.20.2, built from source.** `sway -C` clean in every set, mode and option; a running sway reloads on apply; the themed bar can be swapped for waybar.

| Themer Default · horizon, light | Themer Chicago · ubuntu, dark |
|---|---|
| ![sway in Themer Default](screenshots/modules/sway-default.png) | ![sway in Themer Chicago](screenshots/modules/sway-chicago.png) |

### i3

✔ **i3 4.25.1, built from source.** `i3 -C` clean; reloads live. i3's stock config binds `dmenu_run` directly, so the themed launcher is used only where your binding uses `$menu`.

| Themer Default · espresso-libre, dark | Themer Chicago · morada, light |
|---|---|
| ![i3 in Themer Default](screenshots/modules/i3-default.png) | ![i3 in Themer Chicago](screenshots/modules/i3-chicago.png) |

### hyprland

◯ source **Hyprland 0.57 source.** Not built: Hyprland 0.57 needs a C++26 compiler. Both the Lua and the `.conf` forms checked against stand-ins that follow Hyprland's own config rules.

_No screenshot: the program could not be run here._

### niri

✔ **niri 26.04, built from source.** `niri validate` clean everywhere, run nested in sway.

| Themer Default · hax0r-blue, light | Themer Chicago · japanesque, light |
|---|---|
| ![niri in Themer Default](screenshots/modules/niri-default.png) | ![niri in Themer Chicago](screenshots/modules/niri-chicago.png) |

### river

◯ source **river 0.3 source.** Not built: river is written in Zig and no Zig toolchain could be had. Checked against river's parser with a stand-in `riverctl`.

_No screenshot: the program could not be run here._

### labwc

✔ **labwc 0.20.2, built from source.** No log errors; every theme key exists in labwc; a running labwc reloads on apply.

| Themer Default · hurtado, light | Themer Chicago · sublette, light |
|---|---|
| ![labwc in Themer Default](screenshots/modules/labwc-default.png) | ![labwc in Themer Chicago](screenshots/modules/labwc-chicago.png) |

### bspwm

✔ **bspwm 0.9.12, built from source.** Colours taken live; shell, bash and missing bspwmrc each handled; unload leaves bspwmrc byte-identical and executable.

| Themer Default · gotham, dark | Themer Chicago · kibble, light |
|---|---|
| ![bspwm in Themer Default](screenshots/modules/bspwm-default.png) | ![bspwm in Themer Chicago](screenshots/modules/bspwm-chicago.png) |

### dwm

✔ **dwm 6.4, built from source with the generated header.** Bar and borders match the theme exactly. Unload removes the header and leaves your source tree as it was.

| Themer Default · adventuretime, dark | Themer Chicago · obsidian, light |
|---|---|
| ![dwm in Themer Default](screenshots/modules/dwm-default.png) | ![dwm in Themer Chicago](screenshots/modules/dwm-chicago.png) |

### openbox

✔ **Openbox 3.6.1, built from source.** The Themer theme loads, colours and fonts show, reconfigures live; unload switches back to your theme.

| Themer Default · peppermint, dark | Themer Chicago · elementary, dark |
|---|---|
| ![openbox in Themer Default](screenshots/modules/openbox-default.png) | ![openbox in Themer Chicago](screenshots/modules/openbox-chicago.png) |

## Bars, launchers, notifications and lock screens

### waybar

✔ **Waybar 0.15.0, built from source.** GTK's CSS parser reports nothing on the themed style; a running bar reloads.

| Themer Default · cyberpunk-neon, dark | Themer Chicago · maia, light |
|---|---|
| ![waybar in Themer Default](screenshots/modules/waybar-default.png) | ![waybar in Themer Chicago](screenshots/modules/waybar-chicago.png) |

### dmenu

✔ fixed **dmenu 5.4, built from source.** Fixed: dmenu refused the colours (they had no `#`) and ignored the font, so the launcher never appeared.

| Themer Default · bluloco, dark | Themer Chicago · warmneon, dark |
|---|---|
| ![dmenu in Themer Default](screenshots/modules/dmenu-default.png) | ![dmenu in Themer Chicago](screenshots/modules/dmenu-chicago.png) |

### wmenu

✔ **wmenu 0.2.0, built from source.** Runs clean from the launcher script and from sway's `$menu`.

| Themer Default · nighty, light | Themer Chicago · frontend-delight, dark |
|---|---|
| ![wmenu in Themer Default](screenshots/modules/wmenu-default.png) | ![wmenu in Themer Chicago](screenshots/modules/wmenu-chicago.png) |

### rofi

✔ **rofi 2.0.0 (X11), built from source.** `rofi -dump-theme` clean in every set and option; your own rofi theme comes back on unload.

| Themer Default · sat, light | Themer Chicago · nordic, dark |
|---|---|
| ![rofi in Themer Default](screenshots/modules/rofi-default.png) | ![rofi in Themer Chicago](screenshots/modules/rofi-chicago.png) |

### fuzzel

✔ **fuzzel 1.15.0, built from source.** `fuzzel --check-config` clean.

| Themer Default · fairyfloss-dark, light | Themer Chicago · vibrantink, light |
|---|---|
| ![fuzzel in Themer Default](screenshots/modules/fuzzel-default.png) | ![fuzzel in Themer Chicago](screenshots/modules/fuzzel-chicago.png) |

### mako

✔ **mako 1.11.0, built from source.** Real notifications; a running mako takes a new theme on apply.

| Themer Default · alienblood, dark | Themer Chicago · fairyfloss, light |
|---|---|
| ![mako in Themer Default](screenshots/modules/mako-default.png) | ![mako in Themer Chicago](screenshots/modules/mako-chicago.png) |

### dunst

✔ **dunst 1.13.2, built from source.** No warnings; after a live reload the next notification matched in all 16 combinations.

| Themer Default · hax0r-r3d, light | Themer Chicago · desert, dark |
|---|---|
| ![dunst in Themer Default](screenshots/modules/dunst-default.png) | ![dunst in Themer Chicago](screenshots/modules/dunst-chicago.png) |

### swaync

✔ fixed · ◯ source **SwayNotificationCenter 0.12 source.** Not built (needs Vala and GTK 4.16). The source check found a real bug, now fixed: swaync 0.12 wants `--noti-bg` as `r, g, b`, so notifications had lost their background.

_No screenshot: the program could not be run here._

### swaylock

✔ **swaylock 1.8.6, built from source.** Locks a headless sway; its debug log shows every themer line read without error.

| Themer Default · blazer, dark | Themer Chicago · 3024, light |
|---|---|
| ![swaylock in Themer Default](screenshots/modules/swaylock-default.png) | ![swaylock in Themer Chicago](screenshots/modules/swaylock-chicago.png) |

### hyprlock

◯ source **hyprlock 0.9.6 source.** The program needs a newer compiler; its own config parser was built and accepts everything themer writes.

_No screenshot: the program could not be run here._

## Terminals

### foot

✔ **foot 1.28.0 (release binary).** `foot --check-config` clean everywhere. (foot 1.28 itself rejects the old `[colors]` section in a user's file.)

| Themer Default · rosepine, dark | Themer Chicago · neopolitan, dark |
|---|---|
| ![foot in Themer Default](screenshots/modules/foot-default.png) | ![foot in Themer Chicago](screenshots/modules/foot-chicago.png) |

### kitty

✔ **kitty 0.49.2 (release bundle).** No config errors in any set, mode or option; live reload works.

| Themer Default · pencil, dark | Themer Chicago · brogrammer, light |
|---|---|
| ![kitty in Themer Default](screenshots/modules/kitty-default.png) | ![kitty in Themer Chicago](screenshots/modules/kitty-chicago.png) |

### alacritty

✔ **Alacritty 0.17.0 (an AppImage build of the release).** Every form of the import line loads without warnings; live reload works.

| Themer Default · greyscale, dark | Themer Chicago · tomorrow-night, dark |
|---|---|
| ![alacritty in Themer Default](screenshots/modules/alacritty-default.png) | ![alacritty in Themer Chicago](screenshots/modules/alacritty-chicago.png) |

### wezterm

✔ fixed **WezTerm 20240203 (AppImage).** Fixed: Themer Default's tabs came out black and grey (the tab bar ignores a scheme's tab colours); `colors.tab_bar` is now set too.

| Themer Default · slate, light | Themer Chicago · hardcore, light |
|---|---|
| ![wezterm in Themer Default](screenshots/modules/wezterm-default.png) | ![wezterm in Themer Chicago](screenshots/modules/wezterm-chicago.png) |

### ghostty

✔ **Ghostty 1.4.0 (an AppImage build of the release).** `ghostty +validate-config` clean; reloads on SIGUSR2. Its window chrome now follows the theme too (`window-theme = ghostty`).

| Themer Default · flat, light | Themer Chicago · gruvbox, light |
|---|---|
| ![ghostty in Themer Default](screenshots/modules/ghostty-default.png) | ![ghostty in Themer Chicago](screenshots/modules/ghostty-chicago.png) |

### st

✔ fixed **st 0.9.3, built from source.** Fixed: the header lacked st's default foreground/background/cursor entries, so st read past its colour table.

| Themer Default · ciapre, light | Themer Chicago · crimson, light |
|---|---|
| ![st in Themer Default](screenshots/modules/st-default.png) | ![st in Themer Chicago](screenshots/modules/st-chicago.png) |

## Toolkits and desktop settings

### gtk

✔ fixed **GTK 3.24 and 4.14 (test programs).** GTK's CSS parser: no errors anywhere. Fixed: gsettings keys are now put back on unload; Chicago's GTK 3 check boxes were invisible; turning GTK 3 or 4 off left a broken link.

| Themer Default · suckless, light | Themer Chicago · breath, light |
|---|---|
| ![gtk in Themer Default](screenshots/modules/gtk-default.png) | ![gtk in Themer Chicago](screenshots/modules/gtk-chicago.png) |

### qt

✔ fixed **Qt 5.15 with themer's plugin and with qt5ct 1.2 (built from source).** Fixed: qt5ct never loaded the stylesheet (wrong section); unfocused selections were invisible; Chicago list text shifted on hover.

| Themer Default · icorangeppl, light | Themer Chicago · aci, dark |
|---|---|
| ![qt in Themer Default](screenshots/modules/qt-default.png) | ![qt in Themer Chicago](screenshots/modules/qt-chicago.png) |

### kde

✔ fixed **KDE KConfig 5.115 and KColorScheme, built from source.** Every colour set and font read back correctly. Fixed: warning text on the selection was unreadable; inactive colours now match.

| Themer Default · solarized-darcula, dark | Themer Chicago · neonnight, light |
|---|---|
| ![kde in Themer Default](screenshots/modules/kde-default.png) | ![kde in Themer Chicago](screenshots/modules/kde-chicago.png) |

### fontconfig

✔ **fontconfig 2.15.** Every hinting, antialias and subpixel setting gives the expected `fc-match` values.

| Themer Default · habamax, light | Themer Chicago · palenight, dark |
|---|---|
| ![fontconfig in Themer Default](screenshots/modules/fontconfig-default.png) | ![fontconfig in Themer Chicago](screenshots/modules/fontconfig-chicago.png) |

### cursor

✔ fixed **X cursors on Xvfb, with xrdb.** Fixed: unloading left themer's cursor in the running X server until the next login.

| Themer Default · redalert, dark | Themer Chicago · catppuccin, light |
|---|---|
| ![cursor in Themer Default](screenshots/modules/cursor-default.png) | ![cursor in Themer Chicago](screenshots/modules/cursor-chicago.png) |

### fcitx

✔ fixed **fcitx5 5.1.7, built from source.** Fixed: the theme was in a format fcitx5 does not read (the window fell back to white); fonts and the dark theme are now set too.

| Themer Default · panda, dark | Themer Chicago · seafoampastel, dark |
|---|---|
| ![fcitx in Themer Default](screenshots/modules/fcitx-default.png) | ![fcitx in Themer Chicago](screenshots/modules/fcitx-chicago.png) |

## Browsers, mail and editors

### firefox

✔ fixed **Firefox 159.0 (official build, repackaged as an AppImage).** Fixed: icons lost with `style=mask`, a doubled account icon, a serif fallback font, and the `size` option.

| Themer Default · jackiebrown, dark | Themer Chicago · solarized, light |
|---|---|
| ![firefox in Themer Default](screenshots/modules/firefox-default.png) | ![firefox in Themer Chicago](screenshots/modules/firefox-chicago.png) |

### thunderbird

✔ fixed **Thunderbird 159.0 (official build, repackaged as an AppImage).** Fixed: the selected message was not highlighted, folders had boxes, and Chicago's spaces bar covered the folder pane.

| Themer Default · highway, light | Themer Chicago · purplepeopleeater, light |
|---|---|
| ![thunderbird in Themer Default](screenshots/modules/thunderbird-default.png) | ![thunderbird in Themer Chicago](screenshots/modules/thunderbird-chicago.png) |

### vscode

✔ fixed **VSCodium 1.143.** All 144 colour ids valid; live recolouring works. Fixed: newer builds renamed the Modern themes, and the module now uses the name the installed build knows.

| Themer Default · flatland, dark | Themer Chicago · irblack, dark |
|---|---|
| ![vscode in Themer Default](screenshots/modules/vscode-default.png) | ![vscode in Themer Chicago](screenshots/modules/vscode-chicago.png) |

### web

✔ **themer-web, in Chromium and Firefox 159.** No console errors in either set or mode; follows theme changes live.

| Themer Default · ateliersulphurpool, light | Themer Chicago · solarized-dark, light |
|---|---|
| ![web in Themer Default](screenshots/modules/web-default.png) | ![web in Themer Chicago](screenshots/modules/web-chicago.png) |

### helix

✔ fixed **Helix 25.07.1.** Fixed: a config written `theme="x"` got a second `theme` key (Helix refused the file); picker matches were invisible on the selected row.

| Themer Default · zenburn, dark | Themer Chicago · gogh, light |
|---|---|
| ![helix in Themer Default](screenshots/modules/helix-default.png) | ![helix in Themer Chicago](screenshots/modules/helix-chicago.png) |

### nvim

✔ fixed **Neovim 0.12.6.** Fixed: search highlights were unreadable in 23 of 440 scheme/mode cases.

| Themer Default · frontend-galaxy, light | Themer Chicago · medallion, light |
|---|---|
| ![nvim in Themer Default](screenshots/modules/nvim-default.png) | ![nvim in Themer Chicago](screenshots/modules/nvim-chicago.png) |

### zathura

✔ **zathura 0.5.14 (GTK 3), built from source.** Colours and `:source` reload work. zathura 0.5.x prints four `index-*` errors that are its own bug, fixed in newer zathura.

| Themer Default · hybrid, light | Themer Chicago · paulmillr, light |
|---|---|
| ![zathura in Themer Default](screenshots/modules/zathura-default.png) | ![zathura in Themer Chicago](screenshots/modules/zathura-chicago.png) |

## Command-line tools

### tmux

✔ **tmux 3.4.** No errors even with every quiet option made strict; a running server takes new colours on apply.

| Themer Default · relaxed, light | Themer Chicago · novel, light |
|---|---|
| ![tmux in Themer Default](screenshots/modules/tmux-default.png) | ![tmux in Themer Chicago](screenshots/modules/tmux-chicago.png) |

### btop

✔ **btop 1.4.6.** No log errors; Chicago squares the boxes; unload restores your lines even after btop rewrote its config.

| Themer Default · hopscotch, light | Themer Chicago · doom-one, light |
|---|---|
| ![btop in Themer Default](screenshots/modules/btop-default.png) | ![btop in Themer Chicago](screenshots/modules/btop-chicago.png) |

### lazygit

✔ fixed **lazygit 0.66.2.** Fixed: under a popup, a panel's selected line was unreadable.

| Themer Default · wombat, light | Themer Chicago · dimmed-monokai, dark |
|---|---|
| ![lazygit in Themer Default](screenshots/modules/lazygit-default.png) | ![lazygit in Themer Chicago](screenshots/modules/lazygit-chicago.png) |

### fzf

✔ **fzf 0.74.5.** Clean in both sets and modes; unload leaves your files exactly as they were.

| Themer Default · spacegray, dark | Themer Chicago · tin, light |
|---|---|
| ![fzf in Themer Default](screenshots/modules/fzf-default.png) | ![fzf in Themer Chicago](screenshots/modules/fzf-chicago.png) |

### bat

✔ **bat 0.26.1.** No warnings; your edits made while themed survive unload.

| Themer Default · treehouse, light | Themer Chicago · ibm3270-hc, light |
|---|---|
| ![bat in Themer Default](screenshots/modules/bat-default.png) | ![bat in Themer Chicago](screenshots/modules/bat-chicago.png) |

### starship

✔ **starship 1.27.0.** Finds the themer palette without warnings; your own `palette` line comes back exactly.

| Themer Default · iceberg, dark | Themer Chicago · broadcast, light |
|---|---|
| ![starship in Themer Default](screenshots/modules/starship-default.png) | ![starship in Themer Chicago](screenshots/modules/starship-chicago.png) |

### vivid

✔ **vivid 0.11.1 with GNU ls.** `ls` takes the generated LS_COLORS. Entries with a backslash are now left out, since systemd and env.sh read them differently.

| Themer Default · foxnightly, light | Themer Chicago · jubi, dark |
|---|---|
| ![vivid in Themer Default](screenshots/modules/vivid-default.png) | ![vivid in Themer Chicago](screenshots/modules/vivid-chicago.png) |

### newt

✔ fixed **newt 0.52.24 and whiptail, built from source.** Fixed: the colour mapping is rewritten; unreadable pairs over all 440 scheme/mode cases went from 356 to 0.

| Themer Default · jellybeans, light | Themer Chicago · seashells, light |
|---|---|
| ![newt in Themer Default](screenshots/modules/newt-default.png) | ![newt in Themer Chicago](screenshots/modules/newt-chicago.png) |

## The console and login

### tty

◐ **kbd's setvtrgb parser and the kernel's palette escapes (no console here).** The palette file passes setvtrgb's own parser; env.sh's escapes set the same 16 colours. The pictures are terminal captures in the console palette, not photos of a console.

| Themer Default · hipstergreen, dark | Themer Chicago · gooey, light |
|---|---|
| ![tty in Themer Default](screenshots/modules/tty-default.png) | ![tty in Themer Chicago](screenshots/modules/tty-chicago.png) |

### ly

◯ source **ly 1.5.0 source.** Not built: ly needs Zig 0.17, which has no release. Config keys checked against ly's source; LuaJIT loads the result. Fixed: the box outline used the text colour.

_No screenshot: the program could not be run here._

## Stylus (web sites)

The Stylus styles cannot be run against the live sites in the test environment. Each is
rendered for both sets in a dark and a light theme and parsed by Chromium's CSS parser (every
rule kept), and the variable names come from the sites' published design systems where they
have one (GitHub's Primer, Stack Overflow's Stacks, MDN, Wikipedia's Codex) and otherwise from
catppuccin/userstyles, which tracks the sites as they change. Sites rename their generated
class names now and then; a style that has gone stale shows as a part of the page left in
the site's own colours. Sites: Bluesky, Canvas, ChatGPT, Claude, DuckDuckGo, GitHub, Gmail, Google Drive, Google Search, Instagram, LinkedIn, MDN, PyPI, Reddit (new and old), Spotify, Stack Overflow and Stack Exchange, Twitch, WhatsApp, Wikipedia and its sister sites, X, YouTube, npm.

