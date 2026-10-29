#!/bin/sh
# make check: runs the built themer against a throwaway HOME.
#   1. every template of every module renders for every theme, dark and light
#      (so no template uses a variable that is not defined)
#   2. apply writes the expected files, with user configs edited in place
#   3. unloading every module puts the user's files back byte for byte and
#      leaves nothing of themer's behind
set -u

cd "$(dirname "$0")/.."
export THEMER_THEMES="$PWD/themes" THEMER_MODULES="$PWD/modules" THEMER_TEMPLATES="$PWD/templates" THEMER_QT_PLUGINS="$PWD/gui/qpa"
export THEMER_ICONS="$PWD/icons"
export HOME="$(mktemp -d)"
trap 'rm -rf "$HOME"' EXIT
unset SWAYSOCK I3SOCK
[ "$(id -u)" = 0 ] && [ -d /run/systemd/system ] &&
	echo "warning: running as root on a systemd system; the tty and grub targets touch /etc"
fail=0
t() { printf '%s\n' "$*"; }
bad() { printf 'FAIL: %s\n' "$*"; fail=1; }

# the user's files, seeded before the first apply so every later apply edits them
mkdir -p "$HOME/.config/foot" "$HOME/.config/sway" "$HOME/.config/waybar" "$HOME/.config/nvim" \
         "$HOME/.config/qt6ct" "$HOME/.config/gtk-3.0"
printf 'font=monospace:size=9\n[colors]\nbackground=000000\n' > "$HOME/.config/foot/foot.ini"
# set $menu with a tab and a line longer than any fixed buffer: unload must give it
# back byte for byte (the undo log escapes tabs and keeps lines of any length)
printf 'set $mod Mod4\nset $menu\tdmenu_run -l 10 # %0300d\nbindsym $mod+d exec $menu\nbar {\n  status_command i3blocks\n  colors { background #000000 }\n}\n' 0 > "$HOME/.config/sway/config"
printf '* { color: red; }\n' > "$HOME/.config/waybar/style.css"
printf '{ "modules-left": ["sway/workspaces"] }\n' > "$HOME/.config/waybar/config.jsonc"
printf -- '-- my init\nvim.o.number = true\n' > "$HOME/.config/nvim/init.lua"
printf '[Appearance]\nstyle=Windows\n[Fonts]\nfixed="Mono,9"\n' > "$HOME/.config/qt6ct/qt6ct.conf"
printf '[Settings]\ngtk-theme-name=Adwaita\n' > "$HOME/.config/gtk-3.0/settings.ini"
# a Firefox profile with a userChrome.css of the user's own
mkdir -p "$HOME/.mozilla/firefox/abc.default/chrome"
printf '[Profile0]\nName=default\nIsRelative=1\nPath=abc.default\nDefault=1\n' > "$HOME/.mozilla/firefox/profiles.ini"
printf '/* mine */\n#nav-bar { color: red; }\n' > "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css"
touch "$HOME/.mozilla/firefox/abc.default/prefs.js"
# a Thunderbird profile with a userChrome.css of the user's own
mkdir -p "$HOME/.config/thunderbird/tb.default/chrome"
printf '[Profile0]\nName=default\nIsRelative=1\nPath=tb.default\nDefault=1\n' > "$HOME/.config/thunderbird/profiles.ini"
printf '/* mine */\n#spacesToolbar { display: none; }\n' > "$HOME/.config/thunderbird/tb.default/chrome/userChrome.css"
touch "$HOME/.config/thunderbird/tb.default/prefs.js"
mkdir -p "$HOME/orig" && cp -r "$HOME/.config" "$HOME/.mozilla" "$HOME/orig/"

# ---- 1. render every template ----
t "render: every template, every theme, both modes"
# set check renders every file of the set for every theme in both modes, in one
# process, with each module's own variables
for set in themer_default themer_chicago; do
	./themer set check $set >"$HOME/check" 2>&1 || bad "$set: $(grep -v '^ ' "$HOME/check" | tail -20)"
	grep -q ' 0 failed' "$HOME/check" || bad "$set: render failures: $(tail -1 "$HOME/check")"
done

# ---- 2. apply ----
t "schemes: import (Gogh, Xresources, kitty, alacritty), remove, restore"
d="$HOME/schemes"; mkdir -p "$d"
printf '*background: #1d2021\n*foreground: #ebdbb2\n' > "$d/x.Xresources"
i=0; for c in 282828 cc241d 98971a d79921 458588 b16286 689d6a a89984 928374 fb4934 b8bb26 fabd2f 83a598 d3869b 8ec07c ebdbb2; do
	printf '*color%d: #%s\n' $i $c >> "$d/x.Xresources"; printf 'color%d #%s\n' $i $c >> "$d/k.conf"; i=$((i + 1))
done
printf '[colors.primary]\nbackground = "#fdf6e3"\n[colors.normal]\nblack = "#073642"\nred = "#dc322f"\ngreen = "#859900"\nyellow = "#b58900"\nblue = "#268bd2"\nmagenta = "#d33682"\ncyan = "#2aa198"\nwhite = "#eee8d5"\n[colors.bright]\nblack = "#002b36"\nred = "#cb4b16"\ngreen = "#586e75"\nyellow = "#657b83"\nblue = "#839496"\nmagenta = "#6c71c4"\ncyan = "#93a1a1"\nwhite = "#fdf6e3"\n' > "$d/a.toml"
./themer scheme import "$d/x.Xresources" mine-x >/dev/null || bad "import Xresources"
./themer scheme import "$d/k.conf" mine-k >/dev/null || bad "import kitty"
./themer scheme import "$d/a.toml" mine-a >/dev/null || bad "import alacritty"
[ "$(./themer scheme import test/gogh-schemes.txt 2>/dev/null | grep -c imported)" = 226 ] || bad "import of the Gogh list did not give 226 schemes"
for n in mine-x mine-k mine-a 3024night; do ./themer list | grep -qx "$n" || bad "imported $n not listed"; done
grep -q '^\[light\]' "$HOME/.config/themer/themes/mine-a.theme" && grep -q '^\[dark\]' "$HOME/.config/themer/themes/mine-a.theme" ||
	bad "an imported scheme lacks a side"
./themer theme mine-x >/dev/null 2>&1 || bad "an imported scheme does not apply"
./themer scheme remove mine-x >/dev/null 2>&1 && bad "the scheme in use was removed"
./themer theme nord >/dev/null 2>&1
./themer scheme remove mine-x >/dev/null || bad "remove an imported scheme"
[ -e "$HOME/.config/themer/themes/mine-x.theme" ] && bad "a removed own scheme is still there"
./themer scheme remove habamax >/dev/null || bad "hide a built-in scheme"
./themer list | grep -qx habamax && bad "a hidden built-in scheme is still listed"
./themer scheme restore >/dev/null || bad "restore"
./themer list | grep -qx habamax || bad "a restored scheme is not listed"
rm -rf "$HOME/.config/themer/themes" "$d"

t "apply: user configs edited in place"
./themer theme nord >/dev/null 2>&1
# a gtk-3.0/gtk.css rendered by an old build is moved aside; the user's own is kept
mkdir -p "$HOME/.config/gtk-3.0"
printf '/* generated by themer (nord, dark) */\n@define-color theme_base_color #262626;\n' >"$HOME/.config/gtk-3.0/gtk.css"
./themer dark >"$HOME/out" 2>&1 || bad "apply failed: $(cat "$HOME/out")"

expect() { [ -e "$HOME/$1" ] || bad "missing $1"; }
expect .config/themer/gtk/gtk-3.0/gtk.css
[ -L "$HOME/.themes/themer_default" ] || bad ".themes/themer_default is not a link to themer's files"
[ -e "$HOME/.themes/themer_default/gtk-3.0/gtk.css" ] || bad "theme not reachable through the link"
expect .config/gtk-4.0/gtk.css
# themer_default, the default set: GTK's own Adwaita in the theme's colors, the
# dark variant beside it, GTK 4's from its own files, libadwaita recolored only
grep -q "GTK 3's Adwaita (light)" "$HOME/.config/themer/gtk/gtk-3.0/gtk.css" || bad "gtk-3.0/gtk.css is not Adwaita"
grep -q "GTK 3's Adwaita (dark)" "$HOME/.config/themer/gtk/gtk-3.0/gtk-dark.css" || bad "gtk-3.0/gtk-dark.css is not Adwaita-dark"
grep -q "GTK 4's Adwaita (light)" "$HOME/.config/themer/gtk/gtk-4.0/gtk.css" || bad "gtk-4.0/gtk.css is not GTK 4's (gtk4.css)"
grep -q "GTK 4's Adwaita (dark)" "$HOME/.config/themer/gtk/gtk-4.0/gtk-dark.css" || bad "gtk-4.0/gtk-dark.css is not GTK 4's (gtk4-dark.css)"
[ -e "$HOME/.config/themer/gtk/gtk-3.0/libadwaita.css" ] && bad "libadwaita.css written for GTK 3"
grep -q 'gtk-4.0/libadwaita.css' "$HOME/.config/gtk-4.0/gtk.css" || bad "gtk-4.0/gtk.css does not import libadwaita.css"
grep -q '^gtk-theme-name=themer_default$' "$HOME/.config/gtk-4.0/settings.ini" || bad "GTK 4 not given the theme name"
grep -q '^@define-color themer_bg_color #2e3440;$' "$HOME/.config/themer/gtk/gtk-3.0/gtk-dark.css" || bad "Adwaita not given the theme's colors"
grep -q '^@define-color window_bg_color #2e3440;$' "$HOME/.config/themer/gtk/gtk-4.0/libadwaita.css" || bad "libadwaita not given the theme's colors"
grep -qE '\{\{|@@#' "$HOME/.config/themer/gtk/gtk-3.0/gtk.css" && bad "gtk.css has template or build leftovers"
[ -e "$HOME/.config/gtk-3.0/gtk.css" ] && bad "old themer gtk-3.0/gtk.css not retired"
[ -e "$HOME/.config/gtk-3.0/gtk.css.themer-old" ] || bad "old gtk-3.0/gtk.css not kept as .themer-old"
printf 'button { color: red; }\n' >"$HOME/.config/gtk-3.0/gtk.css"
./themer dark >/dev/null 2>&1
grep -q 'color: red' "$HOME/.config/gtk-3.0/gtk.css" || bad "the user's own gtk-3.0/gtk.css was touched"
expect .config/qt6ct/qss/themer/themer.qss
expect .config/themer/qt/theme.conf
expect .config/themer/stylus/youtube.user.css
expect .config/themer/stylus/wikipedia.user.css
grep -q "filter: invert(1) hue-rotate(180deg)" "$HOME/.config/themer/stylus/wikipedia.user.css" || bad "wikipedia: drawings not inverted in dark mode"
grep -q "^@updateURL *http://localhost:7331/stylus/wikipedia.user.css" "$HOME/.config/themer/stylus/wikipedia.user.css" || bad "wikipedia userstyle has no @updateURL"
head -c 512 "$HOME/.config/themer/stylus/youtube.user.css" | grep -q 'generated by themer' || bad "userstyle has no marker"
head -1 "$HOME/.config/themer/stylus/youtube.user.css" | grep -q '==UserStyle==' || bad "userstyle does not start with the UserCSS header"
# Themer Default keeps the sites' own fonts; Themer Chicago sets the font option's
grep -q 'font-family' "$HOME/.config/themer/stylus/youtube.user.css" && bad "Themer Default's YouTube style sets a font"
./themer module option stylus font ui >/dev/null 2>&1 || bad "set stylus.font ui"
grep -q 'font-family' "$HOME/.config/themer/stylus/youtube.user.css" || bad "Themer Default's YouTube style ignores font=ui"
./themer module option stylus template themer_chicago >/dev/null 2>&1
grep -q 'font-family' "$HOME/.config/themer/stylus/youtube.user.css" || bad "userstyle has no font rule"
./themer module option stylus font site >/dev/null 2>&1 || bad "set stylus.font"
grep -q 'font-family' "$HOME/.config/themer/stylus/youtube.user.css" && bad "stylus font=site still sets a font"
./themer module reset stylus font >/dev/null 2>&1
./themer module reset stylus template >/dev/null 2>&1
# Firefox and Thunderbird: their own font unless the option sets one
grep -q 'font-family' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" && bad "Themer Default's Firefox sets a font with font=own"
./themer module option firefox font terminal >/dev/null 2>&1 || bad "set firefox.font"
grep -q 'font-family: "monospace"' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox font=terminal not set"
./themer module reset firefox font >/dev/null 2>&1
grep -q 'font-family' "$HOME/.config/thunderbird/tb.default/chrome/userContent.css" && bad "Themer Default's Thunderbird sets a font with font=own"
./themer module option thunderbird font ui >/dev/null 2>&1 || bad "set thunderbird.font"
grep -q 'font-family: "' "$HOME/.config/thunderbird/tb.default/chrome/userContent.css" || bad "thunderbird font=ui not set"
./themer module reset thunderbird font >/dev/null 2>&1
grep -q '^@updateURL *http://localhost:7331/stylus/youtube.user.css' "$HOME/.config/themer/stylus/youtube.user.css" || bad "userstyle has no @updateURL"
# names typed in are checked: no newline into config files, no path as a set
./themer font ui "$(printf 'Sans\nexec touch /tmp/x')" 11 >/dev/null 2>&1 && bad "a font name with a newline was taken"
./themer module option gtk template ../.config/nvim >/dev/null 2>&1 && bad "a path was taken as a template set"
grep -q 'exec touch' "$HOME/.config/sway/themer.conf" 2>/dev/null && bad "a newline in a font name reached the sway config"
# one switch per site: off removes that site's style, on brings it back
./themer module options stylus | grep -q '^youtube  *= on' || bad "stylus has no youtube switch"
./themer module option stylus youtube off >/dev/null 2>&1 || bad "set stylus.youtube"
[ -e "$HOME/.config/themer/stylus/youtube.user.css" ] && bad "stylus.youtube off left the style"
./themer module option stylus youtube on >/dev/null 2>&1
[ -e "$HOME/.config/themer/stylus/youtube.user.css" ] || bad "stylus.youtube on did not render the style"
./themer module option stylus wikipedia off >/dev/null 2>&1 || bad "set stylus.wikipedia"
[ -e "$HOME/.config/themer/stylus/wikipedia.user.css" ] && bad "stylus.wikipedia off left the style"
[ -e "$HOME/.config/themer/stylus/youtube.user.css" ] || bad "stylus.wikipedia off took youtube with it"
./themer module option stylus wikipedia on >/dev/null 2>&1
grep -q '^@version *1\.[0-9]*\.[0-9]*' "$HOME/.config/themer/stylus/youtube.user.css" || bad "userstyle version does not grow"

# ---- the web front end: serves the page, the state, the styles; applies ----
if command -v curl >/dev/null 2>&1; then
	t "themer-web: page, state, styles, apply"
	mkdir -p "$HOME/.config/themer/themes"
	printf '[dark]\nbg=zz\n' > "$HOME/.config/themer/themes/000broken.theme"
	./themer-web -p 7390 >"$HOME/web.log" 2>&1 &
	webpid=$!
	sleep 1
	w=http://127.0.0.1:7390
	code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }
	post() { curl -s -H 'X-Themer: 1' -H 'Content-Type: application/json' -d "$2" "$w$1"; }
	# a POST without X-Themer, or a request for another Host, is refused
	[ "$(code -d '{}' "$w/api/apply")" = 403 ] || bad "themer-web: a POST without X-Themer was taken"
	[ "$(code -H 'Host: evil.example:7390' "$w/api/state")" = 403 ] || bad "themer-web: another Host was answered"
	curl -s "$w/" | grep -q '<title>themer</title>' || bad "themer-web: no page"
	curl -s "$w/page.css" | grep -q "after GNOME's Adwaita" || bad "themer-web: /page.css is not the set's web/page.css"
	curl -s "$w/theme.css" | grep -q -- '--control_hover: #' || bad "themer-web: /theme.css lacks the Adwaita tones"
	curl -s "$w/api/state" > "$HOME/state.json"
	grep -q '"theme":"' "$HOME/state.json" && grep -q '"name":"stylus"' "$HOME/state.json" ||
		bad "themer-web: the state is missing things"
	# a broken theme file leaves the JSON well formed: no element is skipped
	# with its separator left behind
	grep -q '\[,\|,,\|,\]' "$HOME/state.json" && bad "themer-web: malformed state JSON"
	curl -s "$w/stylus/youtube.user.css" | head -c 512 | grep -q 'generated by themer' || bad "themer-web: no userstyle"
	curl -s "$w/theme.css" | head -1 | grep -q '^:root {' || bad "themer-web: no theme.css"
	# icons: the selected theme's own file, or 404; never a path outside the themes
	mkdir -p "$HOME/.icons/T/48x48/apps"
	printf '[Icon Theme]\nDirectories=48x48/apps\n[48x48/apps]\nSize=48\n' > "$HOME/.icons/T/index.theme"
	printf '<svg xmlns="http://www.w3.org/2000/svg"><path fill="#bebebe"/></svg>' > "$HOME/.icons/T/48x48/apps/tm-sym-symbolic.svg"
	cp "$HOME/.icons/T/48x48/apps/tm-sym-symbolic.svg" "$HOME/.icons/T/48x48/apps/tm-own.svg"
	post /api/set '{"icons":"T"}' >/dev/null
	curl -s "$w/icon/tm-own" | grep -q 'fill="#bebebe"' || bad "themer-web: icon not served as is"
	curl -s "$w/icon/tm-sym" | grep -q 'fill="#bebebe"' && bad "themer-web: symbolic icon not recolored"
	[ "$(code "$w/icon/tm-none")" = 404 ] || bad "themer-web: missing icon not 404"
	[ "$(code "$w/icon/..%2f..%2fetc%2fpasswd")" = 404 ] || bad "themer-web: icon path escapes"
	post /api/set "{\"icons\":\"$(sed 's/.*"icons":"\([^"]*\)".*/\1/' "$HOME/state.json")\"}" >/dev/null
	[ "$(code -H 'X-Themer: 1' -H 'Content-Length: 99999999' -d x "$w/api/set")" = 413 ] ||
		bad "themer-web: a huge Content-Length was not refused"
	# every setting reaches the page: colors of your own, the console font, the
	# environment, and Install beside a stylus site in its options
	curl -s "$w/api/state" > "$HOME/st.json"
	grep -q '"overrides":{"bg":""' "$HOME/st.json" || bad "themer-web: no color overrides in the state"
	grep -q '"tty_font":"' "$HOME/st.json" || bad "themer-web: no console font in the state"
	grep -q '"env":\[\["' "$HOME/st.json" || bad "themer-web: no environment in the state"
	grep -q '"name":"youtube"[^}]*"action":"Install","link":"/stylus/youtube.user.css"' "$HOME/st.json" ||
		bad "themer-web: no Install action on the stylus site"
	post /api/set '{"color":"acc","value":"12ab34"}' | grep -q '"acc":"12ab34"' || bad "themer-web: color override not set"
	[ "$(code -H 'X-Themer: 1' -d '{"color":"acc","value":"zz"}' "$w/api/set")" = 400 ] || bad "themer-web: a bad color was taken"
	post /api/set '{"color":"acc","value":""}' | grep -q '"overrides":{[^}]*"acc":""' || bad "themer-web: color override not cleared"
	post /api/set '{"env":"TM_TEST","value":"a b"}' | grep -q '\["TM_TEST","a b"\]' || bad "themer-web: env not set"
	[ "$(code -H 'X-Themer: 1' -d '{"env":"1BAD","value":"x"}' "$w/api/set")" = 400 ] || bad "themer-web: a bad variable name was taken"
	post /api/set '{"env":"TM_TEST","value":""}' | grep -q 'TM_TEST' && bad "themer-web: env not removed"
	post /api/set '{"tty_font":"ter-v20n"}' | grep -q '"tty_font":"ter-v20n"' || bad "themer-web: console font not set"
	[ "$(code -H 'X-Themer: 1' -d '{"tty_font":"../x"}' "$w/api/set")" = 400 ] || bad "themer-web: a bad console font was taken"
	post /api/revert '{}' >/dev/null
	# another front end saves while this one browses: its change comes in, the
	# browsing stays, and saving a module does not write the browsing
	th0=$(sed -n 's/^theme=//p' "$HOME/.config/themer/state")
	post /api/set '{"theme":"gruvbox"}' >/dev/null
	m0=$(sed -n 's/^mode=//p' "$HOME/.config/themer/state"); m1=$([ "$m0" = dark ] && echo light || echo dark)
	./themer $m1 >/dev/null 2>&1
	curl -s "$w/api/state" | grep -q "\"theme\":\"gruvbox\",\"mode\":\"$m1\"" || bad "themer-web: a change made elsewhere did not merge"
	post /api/module '{"name":"st","on":"true"}' >/dev/null
	grep -qx "theme=$th0" "$HOME/.config/themer/state" || bad "themer-web: a module switch saved the browsed theme"
	post /api/module '{"name":"st","on":"false"}' >/dev/null
	./themer $m0 >/dev/null 2>&1; post /api/revert '{}' >/dev/null
	# started by hand while running: opens the running one's page instead
	mkdir -p "$HOME/bin"
	printf '#!/bin/sh\necho "$1" >> "$HOME/opened"\n' > "$HOME/bin/xdg-open"; chmod +x "$HOME/bin/xdg-open"
	PATH="$HOME/bin:$PWD:$PATH" ./themer-web -p 7390 >/dev/null 2>&1 || bad "themer-web did not open the running server"
	grep -qx "http://localhost:7390/" "$HOME/opened" 2>/dev/null || bad "themer-web opened the wrong page"
	rm -f "$HOME/opened"
	[ "$(code --path-as-is "$w/stylus/../state")" = 404 ] || bad "themer-web: a path outside the styles was served"
	post /api/set '{"theme":"gruvbox","mode":"light"}' | grep -q '"theme":"gruvbox","mode":"light"' ||
		bad "themer-web: set did not take"
	grep -q '"dirty":true' "$HOME/state.json" && bad "themer-web: a fresh server says it has unapplied changes"
	post /api/apply '{}' | grep -q '"failures":0' || bad "themer-web: apply failed"
	curl -s "$w/api/state" | grep -q '"dirty":false' || bad "themer-web: still unapplied after apply"
	kill $webpid 2>/dev/null; wait $webpid 2>/dev/null
	rm -f "$HOME/.config/themer/themes/000broken.theme"
	grep -q '^theme=gruvbox' "$HOME/.config/themer/state" || bad "themer-web apply did not save the state"
	# back to what the later checks expect
	./themer theme nord >/dev/null 2>&1; ./themer dark >/dev/null 2>&1
	# without a browser module it declines to run
	./themer module unload firefox >/dev/null 2>&1; ./themer module unload stylus >/dev/null 2>&1
	./themer-web -p 7391 2>&1 | grep -q "no browser module" || bad "themer-web ran without a browser module"
	./themer module load firefox >/dev/null 2>&1; ./themer module load stylus >/dev/null 2>&1
	# server install: starts the server and opens each style in the browser (a stand-in xdg-open)
	mkdir -p "$HOME/bin"
	printf '#!/bin/sh\necho "$1" >> "$HOME/opened"\n' > "$HOME/bin/xdg-open"; chmod +x "$HOME/bin/xdg-open"
	./themer apply >/dev/null 2>&1; ./themer server port 7392 >/dev/null
	PATH="$HOME/bin:$PWD:$PATH" ./themer server install >/dev/null 2>&1 || bad "server install failed"
	sleep 0.3
	grep -qx "http://localhost:7392/stylus/youtube.user.css" "$HOME/opened" 2>/dev/null || bad "server install opened the wrong address"
	[ "$(code http://127.0.0.1:7392/stylus/youtube.user.css)" = 200 ] || bad "server install: the style is not served"
	PATH="$HOME/bin:$PWD:$PATH" ./themer server install nosuch >/dev/null 2>&1 && bad "server install took a site that is not on"
	PATH="$PWD:$PATH" ./themer server stop >/dev/null 2>&1; ./themer server port 7331 >/dev/null; rm -f "$HOME/opened"
fi
expect .config/themer/qt/themer.qss
grep -q '^active=#' "$HOME/.config/themer/qt/theme.conf" || bad "theme.conf has no palette"
if [ -e gui/qpa/qt5/platformthemes/libthemer.so ] || [ -e gui/qpa/qt6/platformthemes/libthemer.so ]; then
	grep -q '^QT_QPA_PLATFORMTHEME=themer$' "$HOME/.config/environment.d/40-themer-qt.conf" || bad "env: themer platform theme not selected"
	grep -q '^QT_PLUGIN_PATH=' "$HOME/.config/environment.d/40-themer-qt.conf" || bad "env: QT_PLUGIN_PATH missing"
else
	grep -q '^QT_QPA_PLATFORMTHEME=qt5ct$' "$HOME/.config/environment.d/40-themer-qt.conf" || bad "env: qt5ct fallback not selected"
fi
./themer module option qt platformtheme qt5ct >/dev/null 2>&1 || bad "set qt.platformtheme"
grep -q '^QT_QPA_PLATFORMTHEME=qt5ct$' "$HOME/.config/environment.d/40-themer-qt.conf" || bad "qt.platformtheme=qt5ct not applied"
./themer module option qt platformtheme auto >/dev/null 2>&1
expect .config/nvim/colors/themer.lua
expect .config/sway/themer.conf
# the user's own style.css is kept under ~/.config/themer/originals while waybar is on
[ -e "$HOME/.config/themer/originals/waybar$HOME/.config/waybar/style.css" ] || bad "waybar: the user's style.css was not kept in originals/"
# a module whose program is absent writes nothing, but stays on for when it appears
if command -v wmenu >/dev/null 2>&1; then
	expect .config/themer/bin/themer-wmenu
	else
	[ -e "$HOME/.config/themer/bin/themer-wmenu" ] && bad "wmenu script written without wmenu"
	./themer module list | grep -q '^wmenu  *on  *absent' || bad "absent module not listed as such"
fi
expect .config/environment.d/50-themer.conf
expect .config/themer/env.sh
( . "$HOME/.config/themer/env.sh" && [ -n "$QT_QPA_PLATFORMTHEME" ] && [ -n "$XCURSOR_THEME" ] ) || bad "env.sh does not export the variables"
expect .config/environment.d/40-themer-newt.conf
grep -q 'themer:firefox begin' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox chrome block missing"
grep -q '^/\* mine \*/' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "user's userChrome.css lost"
# themer_default: Firefox's own look, only its colors set
grep -q -- '--toolbar-bgcolor: #' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox colors not set"
grep -q 'border-radius: 0' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" && bad "themer_default squared Firefox's corners"
grep -q 'border-radius: 0' "$HOME/.config/themer/stylus/youtube.user.css" && bad "themer_default squared YouTube's corners"
grep -q -- '--yt-spec-base-background: #' "$HOME/.config/themer/stylus/youtube.user.css" || bad "youtube colors not set"
grep -q -- '--background-color-base: #' "$HOME/.config/themer/stylus/wikipedia.user.css" || bad "wikipedia colors not set"
# every module's Themer Default files: its own, rendered
grep -q 'as it comes' "$HOME/.config/themer/stylus/youtube.user.css" || bad "stylus: youtube is not Themer Default's"
grep -q 'as it comes' "$HOME/.config/themer/stylus/wikipedia.user.css" || bad "stylus: wikipedia is not Themer Default's"
grep -q '^@updateURL *http://localhost:7331/stylus/youtube.user.css' "$HOME/.config/themer/stylus/youtube.user.css" || bad "stylus: youtube has no @updateURL"
grep -q 'as it comes' "$HOME/.config/thunderbird/tb.default/chrome/userChrome.css" || bad "thunderbird is not Themer Default's"
grep -q "after GNOME's top bar" "$HOME/.config/waybar/style.css" || bad "waybar is not Themer Default's"
grep -q "after GNOME's Adwaita" "$HOME/.config/qt6ct/qss/themer/themer.qss" || bad "qt is not Themer Default's"
grep -q -- '--input-background-color: #' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox: Firefox 158's URL field not colored"
grep -q 'background-color: color-mix\|--button-background-color: #' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" && grep -q -- '--button-background-color: #' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" && bad "firefox: Themer Default overrides Firefox's own button tints"
grep -q 'themer:firefox-content begin' "$HOME/.mozilla/firefox/abc.default/chrome/userContent.css" || bad "firefox content block missing"
grep -q 'themer:thunderbird begin' "$HOME/.config/thunderbird/tb.default/chrome/userChrome.css" || bad "thunderbird chrome block missing"
grep -q '^/\* mine \*/' "$HOME/.config/thunderbird/tb.default/chrome/userChrome.css" || bad "user's thunderbird userChrome.css lost"
grep -q 'themer:thunderbird-content begin' "$HOME/.config/thunderbird/tb.default/chrome/userContent.css" || bad "thunderbird content block missing"
grep -q -- '--layout-background-0: #' "$HOME/.config/thunderbird/tb.default/chrome/userContent.css" || bad "thunderbird tokens not in userContent.css"
grep -q 'background=2e3440' "$HOME/.config/foot/foot.ini" || bad "foot colors not set"
# the host's mode option: foot pinned to light under a dark system, then back
./themer module options foot | grep -q '^mode  *= auto' || bad "foot has no mode option"
./themer module options cursor | grep -q '^mode ' && bad "a colorless module got a mode option"
./themer module option foot mode light >/dev/null 2>&1 || bad "set foot.mode"
grep -q '^initial-color-theme=light' "$HOME/.config/foot/foot.ini" || bad "foot.mode light not rendered light"
grep -q 'gtk-application-prefer-dark-theme=1' "$HOME/.config/gtk-3.0/settings.ini" || bad "foot.mode changed gtk's mode"
./themer module option foot mode purple >/dev/null 2>&1 && bad "foot.mode took a bad value"
./themer module option foot mode auto >/dev/null 2>&1
grep -q '^initial-color-theme=dark' "$HOME/.config/foot/foot.ini" || bad "foot.mode auto did not follow the global mode"
grep -q '^font=monospace:size=10' "$HOME/.config/foot/foot.ini" || bad "foot top-level font not updated"
grep -q 'themer:wm begin' "$HOME/.config/sway/config" || bad "sway include block missing"
grep -q 'set \$menu .*themer-wmenu\|set \$menu .*themer-dmenu' "$HOME/.config/sway/config" ||
	t "  (no wmenu/dmenu installed here: \$menu left as is)"
grep -q 'themer:nvim begin' "$HOME/.config/nvim/init.lua" || bad "nvim select block missing"
grep -q 'style=Fusion' "$HOME/.config/qt6ct/qt6ct.conf" || bad "qt6ct style not set"
grep -q 'generated by themer' "$HOME/.config/waybar/style.css" || bad "waybar style.css not replaced"
grep -q '^mode=dark$' "$HOME/.config/themer/state" || bad "state file mode"

# the mode reaches the desktop color-scheme, which browsers pass on to sites as
# prefers-color-scheme: light says prefer-light (a stand-in gsettings, a stand-in bus)
mkdir -p "$HOME/gsbin"
printf '#!/bin/sh\necho "$*" >> "$HOME/gs.log"\n' > "$HOME/gsbin/gsettings"; chmod +x "$HOME/gsbin/gsettings"
DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent PATH="$HOME/gsbin:$PATH" ./themer light >/dev/null 2>&1
grep -q "color-scheme 'prefer-light'" "$HOME/gs.log" || bad "light mode did not set color-scheme prefer-light"
DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent PATH="$HOME/gsbin:$PATH" ./themer dark >/dev/null 2>&1
grep -q "color-scheme 'prefer-dark'" "$HOME/gs.log" || bad "dark mode did not set color-scheme prefer-dark"
rm -f "$HOME/gs.log"

# colors of your own, the console font, the environment, from the command line
./themer color acc '#12AB34' >/dev/null 2>&1 || bad "themer color acc failed"
./themer color | grep -q '^acc *12ab34  (yours)$' || bad "themer color does not list the override"
./themer color acc zz >/dev/null 2>&1 && bad "themer color took a bad value"
./themer color acc reset >/dev/null 2>&1; ./themer color | grep -q '^acc .*(yours)' && bad "themer color reset kept the override"
./themer font tty ter-v20n >/dev/null 2>&1; grep -q '^tty_font=ter-v20n$' "$HOME/.config/themer/state" || bad "themer font tty not saved"
./themer font tty '../x' >/dev/null 2>&1 && bad "themer font tty took a path"
./themer env 'TM_TEST=x y' >/dev/null 2>&1; ./themer env | grep -qx 'TM_TEST=x y' || bad "themer env not listed"
./themer env '1BAD=x' >/dev/null 2>&1 && bad "themer env took a bad name"
./themer env 'TM_TEST=' >/dev/null 2>&1; ./themer env | grep -q TM_TEST && bad "themer env NAME= did not remove it"
# applying twice must not duplicate anything
./themer apply >/dev/null 2>&1 || bad "second apply failed"
[ "$(grep -c 'themer:wm begin' "$HOME/.config/sway/config")" = 1 ] || bad "sway block duplicated"
# the user's bar gives way to themer's, which keeps its status line
[ "$(grep -c '^#themer-off: ' "$HOME/.config/sway/config")" = 4 ] || bad "the user's bar block was not switched off (exactly)"
grep -q 'status_command i3blocks' "$HOME/.config/sway/themer.conf" || bad "themer's bar lost the user's status_command"
[ "$(grep -c '^background=' "$HOME/.config/foot/foot.ini")" = 2 ] || bad "foot keys duplicated"

# a module option round-trips, and a bad value is refused
./themer module option waybar padding loose >/dev/null 2>&1 || bad "set waybar.padding"
./themer module option waybar padding huge >/dev/null 2>&1 && bad "waybar.padding huge accepted"
./themer module option sway border 3 >/dev/null 2>&1 && bad "removed option sway.border accepted"
./themer module option gtk font nope >/dev/null 2>&1 && bad "gtk.font nope accepted"
# Themer Chicago's Firefox, pinned with the module's template option: the icon
# theme's icons, and the toolbars Firefox hides kept hidden
./themer module option firefox template themer_chicago >/dev/null 2>&1 || bad "set firefox.template"
grep -q 'open-menu\|data:image/svg+xml' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "no icon embedded (fallback icons not found)"
grep -q '#TabsToolbar)\[collapsed\]:not(\[collapsed="false"\])' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox: a toolbar Firefox hides with a bare collapsed attribute is forced visible"
grep -q ':root:not(\[inFullscreen\]) #navigator-toolbox { margin-top: 0' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox: Chicago can leave the toolbox pulled up after fullscreen"
grep -q -- '--toolbar-bgcolor' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox.template themer_chicago not rendered"
# firefox icons as masks: no list-style images, the shapes as masks over the ink
./themer module option firefox style mask >/dev/null 2>&1 || bad "set firefox.style"
grep -q '#back-button { list-style-image: none' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox mask style not rendered"
grep -q 'mask: url(' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox mask style has no masks"
# style=firefox: Firefox's own icons, the icon rules cut out, the size kept
./themer module option firefox style firefox >/dev/null 2>&1 || bad "set firefox.style firefox"
grep -q "list-style-image" "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" && bad "firefox style=firefox kept icon rules"
grep -q "themer:icons" "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" && bad "firefox style=firefox left a marker"
grep -q "glyph-size" "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox style=firefox lost the icon size"
./themer module option firefox style image >/dev/null 2>&1
grep -q '#back-button { list-style-image: url(' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "firefox image style not restored"
./themer module reset firefox template >/dev/null 2>&1
grep -q 'border-radius: 0' "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" && bad "firefox.template reset did not go back to the global set"

# the cursor, icon, and rendering settings reach every file that reads them
./themer cursor Bibata 32 >/dev/null 2>&1 || bad "cursor command"
grep -q '^Inherits=Bibata$' "$HOME/.icons/default/index.theme" || bad "Xcursor default theme"
grep -q '^XCURSOR_SIZE=32$' "$HOME/.config/environment.d/40-themer-cursor.conf" || bad "XCURSOR env"
grep -q '^Xcursor.theme: Bibata$' "$HOME/.Xresources" || bad "Xresources cursor"
grep -q '^gtk-cursor-theme-name=Bibata$' "$HOME/.config/gtk-3.0/settings.ini" || bad "gtk3 cursor"
grep -q '^gtk-cursor-theme-size=32$' "$HOME/.config/gtk-4.0/settings.ini" || bad "gtk4 cursor size"
grep -q 'gtk-cursor-theme-name="Bibata"' "$HOME/.gtkrc-2.0" || bad "gtkrc-2.0 cursor"
grep -q 'Gtk/CursorThemeName "Bibata"' "$HOME/.config/xsettingsd/xsettingsd.conf" || bad "xsettingsd cursor"
grep -q '^seat \* xcursor_theme Bibata 32$' "$HOME/.config/sway/themer.conf" || bad "sway seat cursor"
./themer icons Papirus >/dev/null 2>&1 || bad "icons command"
grep -q '^icon_theme=Papirus$' "$HOME/.config/qt6ct/qt6ct.conf" || bad "qt6ct icon theme"
[ -e "$HOME/.config/qt5ct/qt5ct.conf" ] && bad "qt5ct.conf written without qt5ct installed or configured"
# the version toggles: gtk4 off leaves GTK 4 alone, qt5 off leaves its plugin path out
./themer module option gtk gtk4 off >/dev/null 2>&1 || bad "set gtk.gtk4"
[ -e "$HOME/.config/themer/gtk/gtk-4.0/gtk.css" ] && bad "gtk-4.0 theme written with gtk4 off"
grep -q 'themer:gtk begin' "$HOME/.config/gtk-4.0/gtk.css" 2>/dev/null && bad "gtk-4.0 import block written with gtk4 off"
[ -e "$HOME/.config/themer/gtk/gtk-3.0/gtk.css" ] || bad "gtk-3.0 theme missing with gtk4 off"
./themer module option gtk gtk4 on >/dev/null 2>&1
[ -e "$HOME/.config/themer/gtk/gtk-4.0/gtk.css" ] || bad "gtk-4.0 theme not back with gtk4 on"
./themer module option qt qt5 off >/dev/null 2>&1 || bad "set qt.qt5"
grep -q '/qt5' "$HOME/.config/environment.d/40-themer-qt.conf" && bad "qt5 plugin path still in env with qt5 off"
./themer module option qt qt5 on >/dev/null 2>&1
grep -q '^gtk-icon-theme-name=Papirus$' "$HOME/.config/gtk-3.0/settings.ini" || bad "gtk3 icon theme"
./themer render hinting full antialias off >/dev/null 2>&1 || bad "render command"
grep -q 'hintfull' "$HOME/.config/fontconfig/conf.d/50-themer-rendering.conf" || bad "fontconfig hinting"
grep -q '^gtk-xft-antialias=0$' "$HOME/.config/gtk-3.0/settings.ini" || bad "gtk3 antialias"
./themer render hinting sideways >/dev/null 2>&1 && bad "bad hinting accepted"
grep -q 'double_click_interval' "$HOME/.config/qt6ct/qt6ct.conf" && bad "qt6ct interface keys written (the user's own)"

# a template set that covers one module: the rest falls back to the built-in set
./themer set copy themer_chicago themer_default >/dev/null 2>&1 && bad "set copy over a built-in set"
./themer set copy themer_chicago mine >/dev/null 2>&1 || bad "set copy"
rm -rf "$HOME"/.config/themer/templates/mine/{gtk,qt,firefox,wm,launcher,waybar,nvim,fcitx,st,example,thunderbird}
sed -i 's/generated by themer/generated by themer (MINE)/' "$HOME/.config/themer/templates/mine/newt/newt.conf"
./themer set mine >/dev/null 2>&1
grep -q '^set=mine$' "$HOME/.config/themer/state" || bad "set not saved"
grep -q 'MINE' "$HOME/.config/environment.d/40-themer-newt.conf" || bad "set mine not rendered"
grep -q 'generated by themer' "$HOME/.config/themer/gtk/gtk-3.0/gtk.css" || bad "gtk fallback to built-in set failed (theme is named after the set in use)"
./themer set nope >/dev/null 2>&1 && bad "unknown set accepted"
./themer set themer_chicago >/dev/null 2>&1
grep -q 'MINE' "$HOME/.config/environment.d/40-themer-newt.conf" && bad "set not switched back"

# an existing theme becomes a set: files templated, colors that match the theme
# become variables, assets copied and placed beside the output, undone on unload
mkdir -p "$HOME/imp/Mine/gtk-3.0/assets" "$HOME/imp/Mine/gtk-4.0" "$HOME/imp/ff/chrome/icons"
printf 'window { background-color: #2e3440; color: #ff0000; }\nbutton { background-image: url("assets/b.png"); }\n' > "$HOME/imp/Mine/gtk-3.0/gtk.css"
printf 'window { background-color: #2e3440; }\n' > "$HOME/imp/Mine/gtk-4.0/gtk.css"
printf 'PNG' > "$HOME/imp/Mine/gtk-3.0/assets/b.png"
printf '#nav-bar { background: #2e3440 url("icons/a.svg"); }\n' > "$HOME/imp/ff/chrome/userChrome.css"
printf '<svg/>' > "$HOME/imp/ff/chrome/icons/a.svg"
./themer set import imported gtk "$HOME/imp/Mine" > "$HOME/imp/out" 2>&1 || bad "set import gtk: $(cat "$HOME/imp/out")"
grep -q '1 left' "$HOME/imp/out" || bad "import report: literal color not counted"
grep -q '#{{bg}}' "$HOME/.config/themer/templates/imported/gtk/gtk.css" || bad "import: color not templated"
grep -q '^/\* generated by themer' "$HOME/.config/themer/templates/imported/gtk/gtk.css" || bad "import: no marker"
[ -e "$HOME/.config/themer/templates/imported/gtk/gtk4.css" ] || bad "import: gtk-4.0/gtk.css not taken as gtk4.css"
[ -e "$HOME/.config/themer/templates/imported/gtk/assets/b.png" ] || bad "import: asset not copied"
./themer set import imported firefox "$HOME/imp/ff" >/dev/null 2>&1 || bad "set import firefox"
./themer set check imported > "$HOME/imp/out" 2>&1 || bad "set check imported: $(cat "$HOME/imp/out")"
./themer set imported >"$HOME/imp/out" 2>&1 || bad "set imported: $(cat "$HOME/imp/out")"
[ -e "$HOME/.config/themer/gtk/gtk-3.0/assets/b.png" ] || bad "gtk asset not placed"
grep -q '^window { background-color: #2e3440' "$HOME/.config/themer/gtk/gtk-4.0/gtk.css" || bad "gtk4.css not used for GTK 4"
grep -q '^gtk-theme-name=imported$' "$HOME/.config/gtk-3.0/settings.ini" || bad "gtk theme not named after the set"
grep -q '@import url("file://' "$HOME/.config/gtk-4.0/gtk.css" || bad "gtk-4.0 import block missing"
[ -e "$HOME/.mozilla/firefox/abc.default/chrome/icons/a.svg" ] || bad "firefox asset not placed"
./themer set themer_chicago >/dev/null 2>&1
[ -e "$HOME/.themes/imported" ] && bad "the imported set's theme link left after switching sets"
[ -e "$HOME/.config/themer/gtk/gtk-3.0/assets/b.png" ] && bad "gtk asset left after switching sets"
grep -q '^Name=Themer Chicago$' "$HOME/.config/themer/gtk/index.theme" || bad "GTK theme label"
# from Themer Default to Themer Chicago and back: each set's GTK files and nothing else
[ -e "$HOME/.config/themer/gtk/gtk-3.0/gtk-dark.css" ] && bad "Themer Default's gtk-dark.css left under Themer Chicago"
[ -e "$HOME/.config/themer/gtk/gtk-4.0/libadwaita.css" ] && bad "Themer Default's libadwaita.css left under Themer Chicago"
grep -q 'gtk-4.0/gtk.css' "$HOME/.config/gtk-4.0/gtk.css" || bad "Themer Chicago: libadwaita programs do not get the whole gtk.css"
[ -L "$HOME/.themes/themer_default" ] && bad "Themer Default's theme link left under Themer Chicago"
grep -q '^gtk-theme-name=' "$HOME/.config/gtk-4.0/settings.ini" && bad "GTK 4 given Themer Chicago by name (it has no GTK 4 files: GTK 4's own theme stays beneath)"
./themer set themer_default >/dev/null 2>&1
grep -q '^Name=Themer Default$' "$HOME/.config/themer/gtk/index.theme" || bad "GTK theme label (Themer Default)"
grep -q 'gtk-4.0/libadwaita.css' "$HOME/.config/gtk-4.0/gtk.css" || bad "back on Themer Default: libadwaita.css not imported"
[ "$(grep -c 'themer:gtk begin' "$HOME/.config/gtk-4.0/gtk.css")" = 1 ] || bad "gtk-4.0/gtk.css import block duplicated"
./themer set themer_chicago >/dev/null 2>&1
# the names Themer Chicago had before there were two built-in sets
sed -i 's/^set=.*/set=default/' "$HOME/.config/themer/state"
[ "$(./themer set)" = themer_chicago ] || bad "a saved set=default is not read as themer_chicago"
sed -i 's/^set=.*/set=Themer Chicago/' "$HOME/.config/themer/state"
[ "$(./themer set)" = themer_chicago ] || bad "a saved set=Themer Chicago is not read as themer_chicago"
[ -e "$HOME/.mozilla/firefox/abc.default/chrome/icons/a.svg" ] && bad "firefox asset left after switching sets"
./themer set list | grep -q 'imported from' || bad "set.conf description not listed"
rm -rf "$HOME/.config/themer/templates/imported" "$HOME/imp"

# the built-in set is mirrored into the config directory and editable there
[ -e "$HOME/.config/themer/templates/themer_chicago/gtk/gtk.css" ] || bad "built-in set not mirrored"
[ -e "$HOME/.config/themer/templates/.seeded" ] || bad "no .seeded record"
sed -i 's/generated by themer/generated by themer (EDITED)/' "$HOME/.config/themer/templates/themer_chicago/newt/newt.conf"
./themer apply >/dev/null 2>&1
grep -q 'EDITED' "$HOME/.config/environment.d/40-themer-newt.conf" || bad "edited built-in not used"
./themer apply >/dev/null 2>&1
grep -q 'EDITED' "$HOME/.config/themer/templates/themer_chicago/newt/newt.conf" || bad "user edit overwritten by sync"
# an unchanged mirrored file follows a changed built-in; a deleted one comes back
cp templates/themer_chicago/newt/newt.conf "$HOME/newt.orig"
echo "# upstream change" >> templates/themer_chicago/newt/newt.conf
rm "$HOME/.config/themer/templates/themer_chicago/example/colors"
./themer apply >/dev/null 2>&1
[ -e "$HOME/.config/themer/templates/themer_chicago/example/colors" ] || bad "deleted mirror file not restored"
grep -q 'EDITED' "$HOME/.config/themer/templates/themer_chicago/newt/newt.conf" || bad "edited file replaced by upstream change"
cp "$HOME/newt.orig" templates/themer_chicago/newt/newt.conf
# a built-in that goes away takes its unchanged mirror with it; an edited one stays
mkdir -p templates/gone/newt && cp templates/themer_chicago/newt/newt.conf templates/gone/newt/
./themer apply >/dev/null 2>&1
[ -e "$HOME/.config/themer/templates/gone/newt/newt.conf" ] || bad "new built-in set not mirrored"
rm -rf templates/gone
./themer apply >/dev/null 2>&1
[ -e "$HOME/.config/themer/templates/gone" ] && bad "dropped built-in set still mirrored"
# a stale copy from an old build (no record) is replaced, kept as .themer-old
rm -f "$HOME/.config/themer/templates/.seeded"
echo "stale" > "$HOME/.config/themer/templates/themer_chicago/gtk/gtk.css"
./themer apply >/dev/null 2>&1
grep -q 'stale' "$HOME/.config/themer/templates/themer_chicago/gtk/gtk.css" && bad "stale copy kept"
[ -e "$HOME/.config/themer/templates/themer_chicago/gtk/gtk.css.themer-old" ] || bad "stale copy not kept aside"
rm -rf "$HOME/.config/themer/templates"

# ---- 3. unload everything ----
t "unload: user files restored, nothing left behind"
for mod in $(./themer module list | awk '$2=="on"{print $1}'); do
	./themer module unload "$mod" >/dev/null 2>&1 || bad "unload $mod"
done
for f in foot/foot.ini sway/config waybar/style.css nvim/init.lua qt6ct/qt6ct.conf gtk-3.0/settings.ini; do
	cmp -s "$HOME/orig/.config/$f" "$HOME/.config/$f" || bad "$f not restored"
done
cmp -s "$HOME/orig/.mozilla/firefox/abc.default/chrome/userChrome.css" "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "userChrome.css not restored"
[ ! -e "$HOME/.mozilla/firefox/abc.default/chrome/userContent.css" ] || bad "userContent.css left behind"
cmp -s "$HOME/orig/.config/thunderbird/tb.default/chrome/userChrome.css" "$HOME/.config/thunderbird/tb.default/chrome/userChrome.css" || bad "thunderbird userChrome.css not restored"
[ ! -e "$HOME/.config/thunderbird/tb.default/chrome/userContent.css" ] || bad "thunderbird userContent.css left behind"
left=$(cd "$HOME" && find .config .themes .local -type f 2>/dev/null |
	grep -v '^\.config/themer/\(state\|tty\.rgb\|env\.sh\|templates/\)' |
	grep -v '^\.config/environment\.d/50-themer\.conf$' |
	grep -v '^\.config/\(foot\|sway\|waybar\|nvim\|qt6ct\|gtk-3\.0\|thunderbird\)/' || true)
[ -z "$left" ] || bad "files left after unload: [$left]
$left"
[ ! -e "$HOME/.local/state/themer" ] || [ -z "$(ls -A "$HOME/.local/state/themer")" ] ||
	bad "manifests left: $(ls "$HOME/.local/state/themer")"

if [ "$fail" = 0 ]; then
	t "ok"
else
	exit 1
fi
