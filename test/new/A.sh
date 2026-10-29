# ---- window managers: hyprland, hyprlock, niri, river, bspwm, dwm, labwc, openbox ----
t "window managers: hooked into the user's configs, put back byte for byte"
for p in Hyprland hyprctl hyprlock riverctl bspwm bspc dwm labwc openbox; do
	printf '#!/bin/sh\nexit 0\n' > "$HOME/stubs/$p"; chmod +x "$HOME/stubs/$p"
done
printf '#!/bin/sh\necho "niri 26.04 (test)"\n' > "$HOME/stubs/niri"; chmod +x "$HOME/stubs/niri"
mkdir -p "$HOME/.config/hypr" "$HOME/.config/niri" "$HOME/.config/river" "$HOME/.config/bspwm" \
         "$HOME/.config/labwc" "$HOME/.config/openbox" "$HOME/wmorig" "$HOME/dwmsrc"
printf 'hl.config({ general = { gaps_in = 5 } })\n' > "$HOME/.config/hypr/hyprland.lua"
printf 'general {\n    gaps_in = 5\n}\n' > "$HOME/.config/hypr/hyprland.conf"
printf 'input-field {\n    size = 200, 50\n}\n' > "$HOME/.config/hypr/hyprlock.conf"
printf 'layout {\n    gaps 16\n}\n' > "$HOME/.config/niri/config.kdl"
printf '#!/bin/sh\nriverctl map normal Super Return spawn foot\n' > "$HOME/.config/river/init"
printf '#!/usr/bin/env bash\nbspc monitor -d I II III\n' > "$HOME/.config/bspwm/bspwmrc"
printf 'border.width: 2\n' > "$HOME/.config/labwc/themerc-override"
printf '<labwc_config>\n  <theme>\n    <name>Clearlooks</name>\n    <font name="sans" size="10" />\n  </theme>\n</labwc_config>\n' > "$HOME/.config/labwc/rc.xml"
printf '<openbox_config>\n<theme>\n  <name>Clearlooks</name>\n  <font place="ActiveWindow">\n    <name>sans</name>\n    <size>8</size>\n  </font>\n</theme>\n<desktops>\n  <names>\n    <name>one</name>\n  </names>\n</desktops>\n</openbox_config>\n' > "$HOME/.config/openbox/rc.xml"
for f in hypr/hyprland.lua hypr/hyprland.conf hypr/hyprlock.conf niri/config.kdl river/init bspwm/bspwmrc labwc/themerc-override labwc/rc.xml openbox/rc.xml; do
	mkdir -p "$HOME/wmorig/$(dirname $f)"; cp "$HOME/.config/$f" "$HOME/wmorig/$f"
done
wmset=$(sed -n 's/^set=//p' "$HOME/.config/themer/state")
for m in hyprland hyprlock niri river bspwm dwm labwc openbox; do
	DWM_SRC="$HOME/dwmsrc" ./themer module load $m >/dev/null 2>&1 || bad "$m did not load"
done
for s in themer_chicago themer_default; do
	DWM_SRC="$HOME/dwmsrc" ./themer set $s >/dev/null 2>&1
	grep -q 'pcall(require, "themer")' "$HOME/.config/hypr/hyprland.lua" || bad "hyprland: themer.lua not loaded from hyprland.lua"
	grep -q '^source = .*/.config/hypr/themer.conf$' "$HOME/.config/hypr/hyprland.conf" || bad "hyprland: themer.conf not sourced"
	grep -q 'active_border' "$HOME/.config/hypr/themer.lua" || bad "hyprland: no border colors"
	grep -q '^source = .*themer-colors.conf$' "$HOME/.config/hypr/hyprlock.conf" || bad "hyprlock: colors not sourced into the user's config"
	grep -q '^\$themer_accent = rgb(' "$HOME/.config/hypr/themer-colors.conf" || bad "hyprlock: no \$themer_accent"
	grep -q '^include "themer.kdl"$' "$HOME/.config/niri/config.kdl" || bad "niri: not included"
	grep -q 'active-color "#' "$HOME/.config/niri/themer.kdl" || bad "niri: no focus ring colors"
	grep -q '\. "$HOME/.config/river/themer"' "$HOME/.config/river/init" || bad "river: init does not run the colors"
	grep -q '^riverctl border-color-focused 0x' "$HOME/.config/river/themer" || bad "river: no border colors"
	grep -q '\. "$HOME/.config/bspwm/themer"' "$HOME/.config/bspwm/bspwmrc" || bad "bspwm: bspwmrc does not run the colors"
	sh -n "$HOME/.config/bspwm/bspwmrc" && sh -n "$HOME/.config/river/init" || bad "river/bspwm: init no longer parses"
	grep -q 'SchemeSel' "$HOME/dwmsrc/themer-colors.h" || bad "dwm: header not written to DWM_SRC"
	grep -q '^window.active.title.bg.color: #' "$HOME/.config/labwc/themerc-override" || bad "labwc: no title colors"
	grep -q '<font place="" name=' "$HOME/.config/labwc/rc.xml" || bad "labwc: no font"
	expect .local/share/themes/Themer/openbox-3/themerc
	grep -q '<name>Themer</name>' "$HOME/.config/openbox/rc.xml" || bad "openbox: theme not selected"
	if command -v python3 >/dev/null 2>&1; then
		for x in labwc/rc.xml openbox/rc.xml; do
			python3 -c 'import sys, xml.dom.minidom as m; m.parse(sys.argv[1])' "$HOME/.config/$x" 2>/dev/null || bad "$x is not XML any more"
		done
		python3 - "$HOME/.config/openbox/rc.xml" <<'PY' || bad "openbox: the Themer name is not the theme's first, or the fonts are not last"
import sys, xml.dom.minidom as m
th = m.parse(sys.argv[1]).getElementsByTagName('theme')[0]
kids = [c for c in th.childNodes if c.nodeType == 1]
names = [c for c in kids if c.tagName == 'name']
fonts = [c for c in kids if c.tagName == 'font']
sys.exit(not (names[0].firstChild.data == 'Themer' and fonts[-1].getElementsByTagName('name')[0].firstChild.data != 'sans'))
PY
	fi
done
cp "$HOME/.config/hypr/themer.lua" "$HOME/hl1"; ./themer apply >/dev/null 2>&1
cmp -s "$HOME/hl1" "$HOME/.config/hypr/themer.lua" || bad "hyprland: a second apply changed themer.lua"
for m in hyprland hyprlock niri river bspwm dwm labwc openbox; do ./themer module unload $m >/dev/null 2>&1; done
for f in hypr/hyprland.lua hypr/hyprland.conf hypr/hyprlock.conf niri/config.kdl river/init bspwm/bspwmrc labwc/themerc-override labwc/rc.xml openbox/rc.xml; do
	cmp -s "$HOME/wmorig/$f" "$HOME/.config/$f" || bad "$f not restored by unload"
done
for f in .config/hypr/themer.lua .config/hypr/themer.conf .config/hypr/themer-colors.conf .config/niri/themer.kdl \
         .config/river/themer .config/bspwm/themer .config/themer/dwm/colors.h dwmsrc/themer-colors.h \
         .local/share/themes/Themer/openbox-3/themerc; do
	[ -e "$HOME/$f" ] && bad "$f left after unload"
done
# no hyprlock.conf of the user's: a whole one is written, and taken away again
rm "$HOME/.config/hypr/hyprlock.conf"
./themer module load hyprlock >/dev/null 2>&1
grep -q 'generated by themer' "$HOME/.config/hypr/hyprlock.conf" 2>/dev/null || bad "hyprlock: no lock screen written"
./themer module unload hyprlock >/dev/null 2>&1
[ -e "$HOME/.config/hypr/hyprlock.conf" ] && bad "hyprlock: its lock screen left after unload"
# a niri without include lines is left alone
printf '#!/bin/sh\necho "niri 25.08 (test)"\n' > "$HOME/stubs/niri"
./themer module load niri >/dev/null 2>&1
grep -q themer "$HOME/.config/niri/config.kdl" && bad "niri: an include written for a niri older than 25.11"
./themer module unload niri >/dev/null 2>&1
# not installed: not loadable
rm "$HOME/stubs/labwc"
./themer module load labwc >/dev/null 2>&1 && grep -q themer "$HOME/.config/labwc/rc.xml" && bad "labwc loaded without labwc"
rm -f "$HOME/stubs/Hyprland" "$HOME/stubs/hyprctl" "$HOME/stubs/hyprlock" "$HOME/stubs/niri" "$HOME/stubs/riverctl" \
      "$HOME/stubs/bspwm" "$HOME/stubs/bspc" "$HOME/stubs/dwm" "$HOME/stubs/openbox"
./themer set "$wmset" >/dev/null 2>&1
