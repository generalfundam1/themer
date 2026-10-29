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
	echo "warning: running as root on a systemd system; the tty target touches /etc"
fail=0
t() { printf '%s\n' "$*"; }
bad() { printf 'FAIL: %s\n' "$*"; fail=1; }

# the programs the modules theme: a module whose program is not installed cannot be
# loaded, so each one tested gets a stand-in on PATH (wmenu has none: it stays absent)
mkdir -p "$HOME/stubs"
for p in foot sway waybar nvim firefox thunderbird st nmtui qtpaths; do
	printf '#!/bin/sh\nexit 0\n' > "$HOME/stubs/$p"; chmod +x "$HOME/stubs/$p"
done
export PATH="$HOME/stubs:$PATH"

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
# the derived side keeps the accent's terminal color and needs no readability fixes;
# rederive makes the same file again
cp "$HOME/.config/themer/themes/mine-x.theme" "$HOME/mine-x.before"
./themer scheme rederive >/dev/null 2>&1 || bad "scheme rederive failed"
cmp -s "$HOME/mine-x.before" "$HOME/.config/themer/themes/mine-x.theme" || bad "rederive does not give what import gave"
./themer check 2>/dev/null | sed -n '/^mine-x /,/^[^ ]/p' | grep -q 'light .*->' && bad "a derived light side needs readability fixes"
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
[ -L "$HOME/.themes/Themer Default/gtk-3.0" ] || bad ".themes/Themer Default/gtk-3.0 is not a link to themer's files"
grep -q '^gtk-theme-name=Themer Default$' "$HOME/.config/gtk-3.0/settings.ini" || bad "GTK 3 theme not named by the set's label"
[ -e "$HOME/.themes/Themer Default/gtk-3.0/gtk.css" ] || bad "theme not reachable through the link"
expect .config/gtk-4.0/gtk.css
# themer_default, the default set: GTK's own Adwaita in the theme's colors, the
# dark variant beside it, GTK 4's from its own files, libadwaita recolored only
grep -q "GTK 3's Adwaita (light)" "$HOME/.config/themer/gtk/gtk-3.0/gtk.css" || bad "gtk-3.0/gtk.css is not Adwaita"
grep -q "GTK 3's Adwaita (dark)" "$HOME/.config/themer/gtk/gtk-3.0/gtk-dark.css" || bad "gtk-3.0/gtk-dark.css is not Adwaita-dark"
grep -q "GTK 4's Adwaita (light)" "$HOME/.config/themer/gtk/gtk-4.0/gtk.css" || bad "gtk-4.0/gtk.css is not GTK 4's (gtk4.css)"
grep -q "GTK 4's Adwaita (dark)" "$HOME/.config/themer/gtk/gtk-4.0/gtk-dark.css" || bad "gtk-4.0/gtk-dark.css is not GTK 4's (gtk4-dark.css)"
[ -e "$HOME/.config/themer/gtk/gtk-3.0/libadwaita.css" ] && bad "libadwaita.css written for GTK 3"
grep -q 'gtk-4.0/libadwaita.css' "$HOME/.config/gtk-4.0/gtk.css" || bad "gtk-4.0/gtk.css does not import libadwaita.css"
grep -q '^gtk-theme-name=Themer Default$' "$HOME/.config/gtk-4.0/settings.ini" || bad "GTK 4 not given the theme name"
[ -L "$HOME/.themes/Themer Default/gtk-4.0" ] || bad "Themer Default has GTK 4 files: its theme should offer them"
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
	# a stand-in xdg-open: Apply must not open anything
	mkdir -p "$HOME/bin"
	printf '#!/bin/sh\necho "$1" >> "$HOME/opened"\n' > "$HOME/bin/xdg-open"; chmod +x "$HOME/bin/xdg-open"
	rm -f "$HOME/opened"
	PATH="$HOME/bin:$PWD:$PATH" ./themer-web -p 7390 >"$HOME/web.log" 2>&1 &
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
	sleep 0.3   # xdg-open runs detached
	grep -qx "http://localhost:7390/" "$HOME/opened" 2>/dev/null || bad "themer-web opened the wrong page"
	rm -f "$HOME/opened"
	[ "$(code --path-as-is "$w/stylus/../state")" = 404 ] || bad "themer-web: a path outside the styles was served"
	post /api/set '{"theme":"gruvbox","mode":"light"}' | grep -q '"theme":"gruvbox","mode":"light"' ||
		bad "themer-web: set did not take"
	grep -q '"dirty":true' "$HOME/state.json" && bad "themer-web: a fresh server says it has unapplied changes"
	post /api/apply '{}' | grep -q '"failures":0' || bad "themer-web: apply failed"
	curl -s "$w/api/state" | grep -q '"dirty":false' || bad "themer-web: still unapplied after apply"
	# the words: the five pages every front end has, and no text left empty
	curl -s "$w/api/text" | grep -q '"pages":\[{"id":"theme","title":"Theme"},{"id":"fonts","title":"Fonts"},{"id":"icons","title":"Icons"},{"id":"modules","title":"Modules"},{"id":"system","title":"System"}\]' ||
		bad "themer-web: the pages are not the five every front end has"
	curl -s "$w/api/text" | grep -q '":""' && bad "themer-web: a text is empty"
	curl -s "$w/api/state" | grep -q '"state":"on","group":0,"note":"",' || bad "themer-web: a module's state is not the shared one"
	sleep 0.3
	[ -e "$HOME/opened" ] && bad "Apply opened the stylus sites"
	rm -f "$HOME/opened"
	post /api/apply '{}' | grep -q '"message":"applied and saved' || bad "themer-web: apply gives no status line"
	sleep 0.3
	[ -e "$HOME/opened" ] && bad "Apply opened the stylus sites"
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
	grep -q '^QT_QPA_PLATFORMTHEME=themer$' "$HOME/.config/environment.d/60-themer-qt.conf" || bad "env: themer platform theme not selected"
	grep -q '^QT_PLUGIN_PATH=' "$HOME/.config/environment.d/60-themer-qt.conf" || bad "env: QT_PLUGIN_PATH missing"
else
	grep -q '^QT_QPA_PLATFORMTHEME=qt5ct$' "$HOME/.config/environment.d/60-themer-qt.conf" || bad "env: qt5ct fallback not selected"
fi
./themer module option qt platformtheme qt5ct >/dev/null 2>&1 || bad "set qt.platformtheme"
grep -q '^QT_QPA_PLATFORMTHEME=qt5ct$' "$HOME/.config/environment.d/60-themer-qt.conf" || bad "qt.platformtheme=qt5ct not applied"
./themer module option qt platformtheme auto >/dev/null 2>&1
expect .config/nvim/colors/themer.lua
expect .config/sway/themer.conf
# the user's own style.css is kept under ~/.config/themer/originals while waybar is on
[ -e "$HOME/.config/themer/originals/waybar$HOME/.config/waybar/style.css" ] || bad "waybar: the user's style.css was not kept in originals/"
# a module whose program is absent writes nothing and cannot be loaded
if command -v wmenu >/dev/null 2>&1; then
	expect .config/themer/bin/themer-wmenu
	else
	[ -e "$HOME/.config/themer/bin/themer-wmenu" ] && bad "wmenu script written without wmenu"
	./themer module list | grep -q '^wmenu  *off  *absent' || bad "absent module not listed as such"
	./themer module list | grep -A99 '^# Not installed' | grep -q '^wmenu ' || bad "absent module not listed under Not installed"
	./themer module load wmenu >/dev/null 2>&1 && bad "a module whose program is not installed was loaded"
	./themer module list | grep -q '^# Loaded' || bad "module list not grouped"
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
# every site of the set is rendered, the largest (GitHub) included
for f in templates/themer_default/stylus/*.user.css; do
	[ -s "$HOME/.config/themer/stylus/${f##*/}" ] || bad "stylus: ${f##*/} not rendered"
done
# every module's Themer Default files: its own, rendered
grep -q 'as it comes' "$HOME/.config/themer/stylus/youtube.user.css" || bad "stylus: youtube is not Themer Default's"
grep -q 'as they come' "$HOME/.config/themer/stylus/wikipedia.user.css" || bad "stylus: wikipedia is not Themer Default's"
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
# a line the user changes while themed stays theirs on unload; the rest comes back
cp "$HOME/.config/foot/foot.ini" "$HOME/foot.themed"
sed -i 's/^background=2e3440$/background=123456/' "$HOME/.config/foot/foot.ini"
./themer module unload foot >/dev/null 2>&1
grep -q '^background=123456$' "$HOME/.config/foot/foot.ini" || bad "unload put back a line the user had changed"
grep -q '^font=monospace:size=9$' "$HOME/.config/foot/foot.ini" || bad "unload did not put back the user's font line"
grep -q '^foreground=' "$HOME/.config/foot/foot.ini" && bad "unload left themer's foreground"
printf 'font=monospace:size=9\n[colors]\nbackground=000000\n' > "$HOME/.config/foot/foot.ini"
./themer module load foot >/dev/null 2>&1
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
# waybar is installed here (a stand-in) and its module is on: it is sway's bar
grep -q 'swaybar_command waybar' "$HOME/.config/sway/themer.conf" || bad "sway: waybar not made the bar while its module is on"
./themer module option sway bar themer >/dev/null 2>&1
grep -q 'status_command i3blocks' "$HOME/.config/sway/themer.conf" || bad "themer's bar lost the user's status_command"
./themer module reset sway bar >/dev/null 2>&1
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
grep -q '/qt5' "$HOME/.config/environment.d/60-themer-qt.conf" && bad "qt5 plugin path still in env with qt5 off"
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
[ -e "$HOME/.themes/Themer Default" ] && bad "Themer Default's theme left under Themer Chicago"
[ -L "$HOME/.themes/Themer Chicago/gtk-3.0" ] || bad "no Themer Chicago theme under Themer Chicago"
grep -q '^gtk-theme-name=Themer Chicago$' "$HOME/.config/gtk-4.0/settings.ini" || bad "GTK 4 not told the theme's name"
[ -e "$HOME/.themes/Themer Chicago/gtk-4.0" ] && bad "Themer Chicago offers GTK 4 a theme (it has no GTK 4 files: GTK 4's own theme stays beneath)"
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

# ---- the terminals: kitty, alacritty, ghostty, wezterm, tmux ----
# Self-contained: stand-in programs on PATH (the modules load only where their
# program is installed), seeded configs, load, apply again, both sets and modes,
# then unload gives every file back byte for byte.
t "terminals: kitty, alacritty, ghostty, wezterm, tmux"
stubs="$HOME/stubs-terminals"; mkdir -p "$stubs"
for p in kitty alacritty ghostty wezterm tmux; do
	printf '#!/bin/sh\nexit 0\n' > "$stubs/$p"; chmod +x "$stubs/$p"
done
oldpath=$PATH; PATH="$stubs:$PATH"
mkdir -p "$HOME/.config/kitty" "$HOME/.config/alacritty" "$HOME/.config/ghostty" "$HOME/.config/wezterm"
printf '# mine\nfont_size 9\n' > "$HOME/.config/kitty/kitty.conf"
printf '[general]\nimport = ["~/.config/alacritty/mine.toml"]\n\n[font]\nsize = 9\n' > "$HOME/.config/alacritty/alacritty.toml"
printf 'font-size = 9\n' > "$HOME/.config/ghostty/config"
printf "local wezterm = require 'wezterm'\nlocal config = wezterm.config_builder()\nconfig.font_size = 9\nreturn config\n" > "$HOME/.config/wezterm/wezterm.lua"
printf 'set -g mouse on\n' > "$HOME/.tmux.conf"
mkdir -p "$HOME/orig-terminals" && cp -r "$HOME/.config/kitty" "$HOME/.config/alacritty" "$HOME/.config/ghostty" \
	"$HOME/.config/wezterm" "$HOME/.tmux.conf" "$HOME/orig-terminals/"
for m in kitty alacritty ghostty wezterm tmux; do
	./themer module load $m >/dev/null 2>&1 || bad "$m did not load"
done
./themer apply >/dev/null 2>&1
grep -q '^include themer.conf$' "$HOME/.config/kitty/kitty.conf" || bad "kitty: themer.conf not included"
grep -q '^color15 #' "$HOME/.config/kitty/themer.conf" || bad "kitty: palette not written"
grep -q '^import = \["~/.config/alacritty/mine.toml", "~/.config/alacritty/themer.toml"\]$' "$HOME/.config/alacritty/alacritty.toml" ||
	bad "alacritty: themer.toml not added to the user's imports"
grep -q '^\[colors.bright\]' "$HOME/.config/alacritty/themer.toml" || bad "alacritty: colors not written"
grep -q '^config-file = ?themer$' "$HOME/.config/ghostty/config" || bad "ghostty: themer not read from config"
grep -q '^palette = 15=#' "$HOME/.config/ghostty/themer" || bad "ghostty: palette not written"
sed -n '3p' "$HOME/.config/wezterm/wezterm.lua" | grep -q 'themer:wezterm begin' || bad "wezterm: the block is not right after local config"
grep -q '^name = "themer"' "$HOME/.config/themer/wezterm/themer.toml" || bad "wezterm: scheme not written"
grep -q 'source-file -q ~/.config/themer/tmux/themer.conf' "$HOME/.tmux.conf" || bad "tmux: theme not sourced"
grep -q '^set -gq status-style "bg=#' "$HOME/.config/themer/tmux/themer.conf" || bad "tmux: status line not themed"
[ "$(grep -c 'themer:kitty begin' "$HOME/.config/kitty/kitty.conf")" = 1 ] || bad "kitty: block repeated by apply"
[ "$(grep -c 'themer.toml' "$HOME/.config/alacritty/alacritty.toml")" = 1 ] || bad "alacritty: import repeated by apply"
# the files parse: TOML with python's tomllib, tmux with tmux itself when installed
if command -v python3 >/dev/null 2>&1; then
	for f in .config/alacritty/alacritty.toml .config/alacritty/themer.toml .config/themer/wezterm/themer.toml; do
		python3 -c "import tomllib, sys; tomllib.load(open(sys.argv[1], 'rb'))" "$HOME/$f" 2>/dev/null || bad "$f is not valid TOML"
	done
fi
./themer set themer_chicago >/dev/null 2>&1
grep -q '^tab_bar_style separator' "$HOME/.config/kitty/themer.conf" || bad "kitty: Chicago's tab bar not rendered"
grep -q 'use_fancy_tab_bar = false' "$HOME/.config/wezterm/wezterm.lua" || bad "wezterm: Chicago's plain tab bar not selected"
PATH=$oldpath
if command -v tmux >/dev/null 2>&1; then
	tmux -L themercheck -f /dev/null new-session -d 2>/dev/null &&
		{ tmux -L themercheck source-file "$HOME/.config/themer/tmux/themer.conf" 2>/dev/null || bad "tmux: themer.conf has errors"; }
	tmux -L themercheck kill-server 2>/dev/null
fi
PATH="$stubs:$PATH"
./themer light >/dev/null 2>&1; ./themer dark >/dev/null 2>&1; ./themer set themer_default >/dev/null 2>&1
for m in kitty alacritty ghostty wezterm tmux; do ./themer module unload $m >/dev/null 2>&1; done
for f in kitty/kitty.conf alacritty/alacritty.toml ghostty/config wezterm/wezterm.lua; do
	cmp -s "$HOME/orig-terminals/${f%%/*}/${f#*/}" "$HOME/.config/$f" || bad "unload did not restore .config/$f"
done
cmp -s "$HOME/orig-terminals/.tmux.conf" "$HOME/.tmux.conf" || bad "unload did not restore .tmux.conf"
for f in .config/kitty/themer.conf .config/alacritty/themer.toml .config/ghostty/themer .config/themer/wezterm .config/themer/tmux; do
	[ -e "$HOME/$f" ] && bad "unload left $f"
done
rm -rf "$HOME/.config/kitty" "$HOME/.config/alacritty" "$HOME/.config/ghostty" "$HOME/.config/wezterm" "$HOME/.tmux.conf" "$HOME/orig-terminals"
PATH=$oldpath

# ---- launchers, notifications, the lock screen: rofi, fuzzel, mako, dunst, swaync,
# swaylock. Self-contained: stand-ins for the programs, the user's own configs, load,
# apply again, the other set and mode, unload and compare byte for byte. ----
t "rofi, fuzzel, mako, dunst, swaync, swaylock: written, idempotent, restored"
for p in rofi fuzzel mako makoctl dunst dunstctl swaync swaync-client swaylock; do
	printf '#!/bin/sh\nexit 0\n' > "$HOME/stubs/$p"; chmod +x "$HOME/stubs/$p"
done
mkdir -p "$HOME/.config/rofi" "$HOME/.config/fuzzel" "$HOME/.config/mako" "$HOME/.config/swaylock" \
         "$HOME/.config/swaync" "$HOME/groupC"
printf 'configuration {\n  modi: "drun,run";\n}\n@theme "gruvbox-dark"\n' > "$HOME/.config/rofi/config.rasi"
printf '[main]\nfont=Mine:size=9\nterminal=foot\n\n[colors]\nbackground=000000ff\n' > "$HOME/.config/fuzzel/fuzzel.ini"
printf 'font=Mine 9\nmax-visible=3\n\n[app-name=spotify]\ninvisible=1\n' > "$HOME/.config/mako/config"
printf 'ignore-empty-password\ncolor=000000\nshow-failed-attempts\n' > "$HOME/.config/swaylock/config"
printf '.notification { margin: 4px; }\n' > "$HOME/.config/swaync/style.css"
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css; do
	cp "$HOME/.config/$f" "$HOME/groupC/$(echo "$f" | tr / _)"
done
for m in rofi fuzzel mako dunst swaync swaylock; do
	./themer module load "$m" >/dev/null 2>&1 || bad "load $m"
done
expect .local/share/rofi/themes/themer.rasi
grep -q '^@theme ".*/.local/share/rofi/themes/themer.rasi"$' "$HOME/.config/rofi/config.rasi" || bad "rofi: theme not selected in config.rasi"
grep -q '^@theme "gruvbox-dark"$' "$HOME/.config/rofi/config.rasi" || bad "rofi: the user's own @theme line lost"
grep -q 'selected-normal-background: *#[0-9a-f]\{6\};' "$HOME/.local/share/rofi/themes/themer.rasi" || bad "rofi: no selection color"
grep -q '^terminal=foot$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: the user's key lost"
grep -q '^background=[0-9a-f]\{8\}$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: background not RRGGBBAA"
[ "$(grep -c '^\[colors\]' "$HOME/.config/fuzzel/fuzzel.ini")" = 1 ] || bad "fuzzel: [colors] repeated"
grep -q '^font=.*:size=[0-9]*$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: font not set"
sed -n '1,/^\[/p' "$HOME/.config/mako/config" | grep -q '^background-color=#' || bad "mako: colors not in the global section"
grep -q '^max-visible=3$' "$HOME/.config/mako/config" || bad "mako: the user's key lost"
grep -q '^\[urgency=high\]$' "$HOME/.config/mako/config" || bad "mako: no urgent border"
expect .config/dunst/dunstrc.d/90-themer.conf
expect .config/dunst/dunstrc
grep -q 'frame_color = "#[0-9a-f]\{6\}"' "$HOME/.config/dunst/dunstrc.d/90-themer.conf" || bad "dunst: no frame color"
grep -q '^\.notification { margin: 4px; }$' "$HOME/.config/swaync/style.css" || bad "swaync: the user's rule lost"
grep -q 'themer:swaync begin' "$HOME/.config/swaync/style.css" || bad "swaync: no block"
grep -q -- '--noti-bg: [0-9]' "$HOME/.config/swaync/style.css" || bad "swaync: no custom properties"
grep -q '^ignore-empty-password$' "$HOME/.config/swaylock/config" || bad "swaylock: the user's flag lost"
[ "$(grep -c '^color=' "$HOME/.config/swaylock/config")" = 1 ] || bad "swaylock: color repeated"
grep -q '^ring-wrong-color=[0-9a-f]\{6\}$' "$HOME/.config/swaylock/config" || bad "swaylock: no wrong-password ring"
# again: the same bytes
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css dunst/dunstrc.d/90-themer.conf; do
	cp "$HOME/.config/$f" "$HOME/groupC/again_$(echo "$f" | tr / _)"
done
./themer apply >/dev/null 2>&1
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css dunst/dunstrc.d/90-themer.conf; do
	cmp -s "$HOME/.config/$f" "$HOME/groupC/again_$(echo "$f" | tr / _)" || bad "$f changed on a second apply"
done
# Chicago: square, 1px
./themer set themer_chicago >/dev/null 2>&1
grep -q '^radius=0$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: Chicago is not square"
grep -q '^border-size=1$' "$HOME/.config/mako/config" || bad "mako: Chicago's border is not 1px"
grep -q 'frame_width = 1' "$HOME/.config/dunst/dunstrc.d/90-themer.conf" || bad "dunst: Chicago's frame is not 1px"
grep -q 'border-radius: 0;' "$HOME/.config/swaync/style.css" || bad "swaync: Chicago is not square"
grep -q 'border: *1px solid 0px 0px;' "$HOME/.local/share/rofi/themes/themer.rasi" || bad "rofi: Chicago's separators are not solid"
./themer set themer_default >/dev/null 2>&1
grep -q '^radius=12$' "$HOME/.config/fuzzel/fuzzel.ini" || bad "fuzzel: back to Themer Default"
for m in rofi fuzzel mako dunst swaync swaylock; do
	./themer module unload "$m" >/dev/null 2>&1 || bad "unload $m"
done
for f in rofi/config.rasi fuzzel/fuzzel.ini mako/config swaylock/config swaync/style.css; do
	cmp -s "$HOME/.config/$f" "$HOME/groupC/$(echo "$f" | tr / _)" || bad "$f not restored"
done
[ -e "$HOME/.config/dunst" ] && bad "dunst: files left after unload"
[ -e "$HOME/.local/share/rofi" ] && bad "rofi: theme left after unload"
# swaync without a style.css of the user's: themer's imports the system's first, and goes
rm "$HOME/.config/swaync/style.css"
./themer module load swaync >/dev/null 2>&1
if [ -r /etc/xdg/swaync/style.css ]; then
	head -4 "$HOME/.config/swaync/style.css" | grep -q '@import url("file:///etc/xdg/swaync/style.css");' ||
		bad "swaync: the system's style not imported first"
fi
./themer module unload swaync >/dev/null 2>&1
[ -e "$HOME/.config/swaync/style.css" ] && bad "swaync: the created style.css left behind"
cp "$HOME/groupC/swaync_style.css" "$HOME/.config/swaync/style.css"
rm -rf "$HOME/groupC"

# ---- the command-line tools: btop, zathura, bat, lazygit, fzf, vivid, starship ----
# Self-contained: stand-ins for the programs, the user's own files, then load,
# re-apply, theme and set changes, and unload putting every file back.
t "tools: btop, zathura, bat, lazygit, fzf, vivid, starship"
for p in btop zathura bat lazygit fzf starship; do
	printf '#!/bin/sh\nexit 0\n' > "$HOME/stubs/$p"; chmod +x "$HOME/stubs/$p"
done
# a stand-in vivid: checks the theme file it is given, prints an LS_COLORS from it
cat > "$HOME/stubs/vivid" <<'V'
#!/bin/sh
[ "$1" = generate ] && [ -f "$2" ] || exit 1
echo "di=38;2;$(sed -n 's/^  cyan: "\(.*\)"/\1/p' "$2"):ln=1"
V
chmod +x "$HOME/stubs/vivid"
mkdir -p "$HOME/.config/btop" "$HOME/.config/zathura" "$HOME/.config/bat" "$HOME/.config/lazygit" "$HOME/tools-orig"
printf '#? Config file for btop v. 1.4.5\ncolor_theme = "Default"\nrounded_corners = True\n' > "$HOME/.config/btop/btop.conf"
printf 'set selection-clipboard clipboard\nset default-bg "#000000"\n' > "$HOME/.config/zathura/zathurarc"
printf -- '--style="numbers,changes"\n--theme="TwoDark"\n' > "$HOME/.config/bat/config"
printf 'gui:\n  showIcons: true\n' > "$HOME/.config/lazygit/config.yml"
printf 'add_newline = false\npalette = "gruvbox"\n\n[character]\nsuccess_symbol = "[>](bold green)"\n\n[palettes.gruvbox]\ngreen = "#00ff00"\n' > "$HOME/.config/starship.toml"
for f in btop/btop.conf zathura/zathurarc bat/config lazygit/config.yml starship.toml; do
	cp "$HOME/.config/$f" "$HOME/tools-orig/$(echo "$f" | tr / _)"
done
for m in btop zathura bat lazygit fzf vivid starship; do
	./themer module load "$m" >/dev/null 2>&1 || bad "$m did not load"
done
grep -q '^color_theme = "themer"' "$HOME/.config/btop/btop.conf" || bad "btop: color_theme not changed in place"
grep -q 'theme\[main_bg\]="#' "$HOME/.config/btop/themes/themer.theme" || bad "btop: no theme file"
tail -1 "$HOME/.config/zathura/zathurarc" | grep -q 'themer:zathura end' || bad "zathura: the block is not last"
grep -q '^set highlight-color "rgba(' "$HOME/.config/zathura/zathurarc" || bad "zathura: highlight not rgba()"
tail -2 "$HOME/.config/bat/config" | grep -q -- '--theme="ansi"' || bad "bat: --theme not last"
grep -q "^LG_CONFIG_FILE=$HOME/.config/lazygit/config.yml,$HOME/.config/themer/lazygit/config.yml\$" \
	"$HOME/.config/environment.d/40-themer-lazygit.conf" || bad "lazygit: LG_CONFIG_FILE"
grep -q "^FZF_DEFAULT_OPTS_FILE=$HOME/.config/themer/fzf/fzfrc\$" "$HOME/.config/environment.d/40-themer-fzf.conf" ||
	bad "fzf: FZF_DEFAULT_OPTS_FILE"
grep -q -- '--color=pointer:#' "$HOME/.config/themer/fzf/fzfrc" || bad "fzf: no colors"
grep -q '^LS_COLORS=di=38;2;[0-9a-f]\{6\}:ln=1$' "$HOME/.config/environment.d/40-themer-lscolors.conf" ||
	bad "vivid: LS_COLORS not from vivid's output"
grep -q '^palette = "themer"$' "$HOME/.config/starship.toml" || bad "starship: palette not selected in place"
if command -v python3 >/dev/null 2>&1; then
	python3 -c "import tomllib,sys; d=tomllib.load(open(sys.argv[1],'rb')); assert d['palette']=='themer' and d['palettes']['themer']['green'].startswith('#') and d['add_newline'] is False" \
		"$HOME/.config/starship.toml" 2>/dev/null || bad "starship: starship.toml is not valid TOML"
fi
cp "$HOME/.config/starship.toml" "$HOME/tools-s1"
./themer apply >/dev/null 2>&1
cmp -s "$HOME/tools-s1" "$HOME/.config/starship.toml" || bad "starship: a second apply changed starship.toml"
[ "$(grep -c 'themer:zathura begin' "$HOME/.config/zathura/zathurarc")" = 1 ] || bad "zathura: the block twice"
./themer set themer_chicago >/dev/null 2>&1
grep -q '^rounded_corners = False' "$HOME/.config/btop/btop.conf" || bad "btop: Chicago's boxes not square"
./themer set themer_default >/dev/null 2>&1
grep -q '^rounded_corners = True' "$HOME/.config/btop/btop.conf" || bad "btop: Themer Default did not give the corners back"
# no btop.conf yet (btop writes it on its first run): a block, gone again on unload
./themer module unload btop >/dev/null 2>&1; mv "$HOME/.config/btop/btop.conf" "$HOME/tools-btop"
./themer module load btop >/dev/null 2>&1
grep -q 'color_theme = "themer"' "$HOME/.config/btop/btop.conf" || bad "btop: no color_theme without a btop.conf"
./themer module unload btop >/dev/null 2>&1
[ -e "$HOME/.config/btop/btop.conf" ] && bad "btop: the btop.conf themer made was left behind"
mv "$HOME/tools-btop" "$HOME/.config/btop/btop.conf"
# lazygit refuses a missing listed file: one is made when there is none, and taken away
./themer module unload lazygit >/dev/null 2>&1; mv "$HOME/.config/lazygit/config.yml" "$HOME/tools-lg"
./themer module load lazygit >/dev/null 2>&1
[ -e "$HOME/.config/lazygit/config.yml" ] || bad "lazygit: no config.yml for LG_CONFIG_FILE to list"
./themer module unload lazygit >/dev/null 2>&1
[ -e "$HOME/.config/lazygit/config.yml" ] && bad "lazygit: the config.yml themer made was left behind"
mv "$HOME/tools-lg" "$HOME/.config/lazygit/config.yml"
# starship with no palette line: one at the very top, before the tables
./themer module unload starship >/dev/null 2>&1
printf '[character]\nsuccess_symbol = "[>](bold green)"\n' > "$HOME/.config/starship.toml"; cp "$HOME/.config/starship.toml" "$HOME/tools-s2"
./themer module load starship >/dev/null 2>&1
head -2 "$HOME/.config/starship.toml" | grep -q '^palette = "themer"$' || bad "starship: palette line not at the top"
./themer module unload starship >/dev/null 2>&1
cmp -s "$HOME/tools-s2" "$HOME/.config/starship.toml" || bad "starship: not restored (no palette line)"
cp "$HOME/tools-orig/starship.toml" "$HOME/.config/starship.toml"
./themer module unload btop >/dev/null 2>&1
for m in btop zathura bat lazygit fzf vivid starship; do ./themer module unload "$m" >/dev/null 2>&1; done
for f in btop/btop.conf zathura/zathurarc bat/config lazygit/config.yml starship.toml; do
	cmp -s "$HOME/tools-orig/$(echo "$f" | tr / _)" "$HOME/.config/$f" || bad "$f not restored"
done
for f in .config/environment.d/40-themer-lazygit.conf .config/environment.d/40-themer-fzf.conf \
         .config/environment.d/40-themer-lscolors.conf .config/btop/themes/themer.theme \
         .config/vivid/themes/themer.yml .config/themer/fzf/fzfrc .config/themer/lazygit/config.yml; do
	[ -e "$HOME/$f" ] && bad "$f left behind"
done
rm -f "$HOME/stubs/btop" "$HOME/stubs/zathura" "$HOME/stubs/bat" "$HOME/stubs/lazygit" "$HOME/stubs/fzf" \
      "$HOME/stubs/vivid" "$HOME/stubs/starship"

# ---- helix, vscode, kde: each with its program stubbed on PATH ----
t "helix, vscode, kde: the theme in, your files back"
mkdir -p "$HOME/stubE" "$HOME/.config/helix" "$HOME/.config/Code/User"
for p in hx code kreadconfig6; do printf '#!/bin/sh\n' > "$HOME/stubE/$p"; chmod +x "$HOME/stubE/$p"; done
printf 'theme = "onedark"\n\n[editor]\nline-number = "relative"\n' > "$HOME/.config/helix/config.toml"
printf '{\n    // mine\n    "editor.fontSize": 13,\n    "workbench.colorTheme": "Monokai"\n}\n' > "$HOME/.config/Code/User/settings.json"
printf '[General]\nfont=Noto Sans,10,-1,5,50,0,0,0,0,0\n\n[KDE]\nSingleClick=false\n' > "$HOME/.config/kdeglobals"
cp "$HOME/.config/helix/config.toml" "$HOME/helix.orig"
cp "$HOME/.config/Code/User/settings.json" "$HOME/code.orig"
cp "$HOME/.config/kdeglobals" "$HOME/kde.orig"
# without their programs they are not loadable
for m in helix vscode kde; do
	./themer module load $m >/dev/null 2>&1 && bad "$m loaded without its program"
done
for m in helix vscode kde; do
	PATH="$HOME/stubE:$PATH" ./themer module load $m >/dev/null 2>&1 || bad "$m did not load"
done
PATH="$HOME/stubE:$PATH" ./themer apply >/dev/null 2>&1
expect .config/helix/themes/themer.toml
grep -qx 'theme = "themer"' "$HOME/.config/helix/config.toml" || bad "helix: theme not selected"
grep -q 'theme = "onedark"' "$HOME/.config/helix/config.toml" && bad "helix: two theme lines"
grep -q '^"ui.background" = { bg = "bg" }' "$HOME/.config/helix/themes/themer.toml" || bad "helix: theme not rendered"
grep -q '"workbench.colorTheme": "Default \(Dark\|Light\) Modern"' "$HOME/.config/Code/User/settings.json" || bad "vscode: theme not set"
grep -q '"workbench.colorCustomizations": {' "$HOME/.config/Code/User/settings.json" || bad "vscode: colors not set"
grep -q '// mine' "$HOME/.config/Code/User/settings.json" || bad "vscode: the user's comment lost"
grep -q '"editor.fontSize": 13' "$HOME/.config/Code/User/settings.json" || bad "vscode: the user's setting lost"
grep -q 'editor.fontFamily' "$HOME/.config/Code/User/settings.json" && bad "vscode: font=own set a font"
if command -v python3 >/dev/null 2>&1; then
	python3 -c 'import tomllib,sys; tomllib.load(open(sys.argv[1],"rb")); tomllib.load(open(sys.argv[2],"rb"))' \
		"$HOME/.config/helix/themes/themer.toml" "$HOME/.config/helix/config.toml" 2>/dev/null || bad "helix: not valid TOML"
	python3 -c 'import json,re,sys; json.loads(re.sub(r"(?m)^\s*//.*$","",open(sys.argv[1]).read()))' \
		"$HOME/.config/Code/User/settings.json" 2>/dev/null || bad "vscode: settings.json not valid JSON"
fi
expect .local/share/color-schemes/Themer.colors
grep -qx 'ColorScheme=Themer' "$HOME/.config/kdeglobals" || bad "kde: scheme not selected"
grep -q '^\[Colors:View\]' "$HOME/.config/kdeglobals" || bad "kde: no view colors"
grep -q '^BackgroundNormal=[0-9]*,[0-9]*,[0-9]*$' "$HOME/.config/kdeglobals" || bad "kde: colors not r,g,b"
grep -qx 'SingleClick=false' "$HOME/.config/kdeglobals" || bad "kde: the user's key lost"
PATH="$HOME/stubE:$PATH" ./themer module option vscode font terminal >/dev/null 2>&1
grep -q '"editor.fontFamily": "' "$HOME/.config/Code/User/settings.json" || bad "vscode: font=terminal not set"
PATH="$HOME/stubE:$PATH" ./themer module reset vscode font >/dev/null 2>&1
for m in helix vscode kde; do
	PATH="$HOME/stubE:$PATH" ./themer module unload $m >/dev/null 2>&1
done
cmp -s "$HOME/.config/helix/config.toml" "$HOME/helix.orig" || bad "helix: config.toml not restored"
cmp -s "$HOME/.config/Code/User/settings.json" "$HOME/code.orig" || bad "vscode: settings.json not restored"
cmp -s "$HOME/.config/kdeglobals" "$HOME/kde.orig" || bad "kde: kdeglobals not restored"
[ -e "$HOME/.config/helix/themes/themer.toml" ] && bad "helix: theme file left behind"
[ -e "$HOME/.local/share/color-schemes/Themer.colors" ] && bad "kde: scheme file left behind"
rm -rf "$HOME/stubE" "$HOME/helix.orig" "$HOME/code.orig" "$HOME/kde.orig" "$HOME/.config/helix" "$HOME/.config/Code" "$HOME/.config/kdeglobals"

# ---- Restore originals: every file back, the settings kept, apply waits for Apply ----
t "remove: all styles off and back"
./themer originals >/dev/null 2>&1 || bad "themer originals failed"
grep -qx 'removed=1' "$HOME/.config/themer/state" || bad "remove: not saved as removed"
cmp -s "$HOME/orig/.config/foot/foot.ini" "$HOME/.config/foot/foot.ini" || bad "remove: foot.ini not put back"
cmp -s "$HOME/orig/.config/sway/config" "$HOME/.config/sway/config" || bad "remove: sway config not put back"
cmp -s "$HOME/orig/.mozilla/firefox/abc.default/chrome/userChrome.css" "$HOME/.mozilla/firefox/abc.default/chrome/userChrome.css" || bad "remove: userChrome.css not put back"
[ -e "$HOME/.config/themer/system/tty.palette" ] && bad "remove: the console palette left"
[ -e "$HOME/.config/environment.d/50-themer.conf" ] && bad "remove: the environment left"
./themer apply >/dev/null 2>&1
[ -e "$HOME/.config/sway/themer.conf" ] && bad "remove: themer apply (the login's) put the styles back"
./themer resume >/dev/null 2>&1 || bad "themer resume failed"
grep -q 'removed=' "$HOME/.config/themer/state" && bad "restore: still saved as removed"
expect .config/sway/themer.conf
grep -q '^[0-9]*,[0-9]*,' "$HOME/.config/themer/system/tty.palette" || bad "tty.palette is not in setvtrgb's format"
[ "$(wc -l < "$HOME/.config/themer/system/tty.palette")" = 3 ] || bad "tty.palette is not three lines"
grep -q 'TERM.*linux' "$HOME/.config/themer/env.sh" || bad "env.sh does not set the console palette"
[ -n "$(env -i HOME="$HOME" TERM=linux sh -c '. "$HOME/.config/themer/env.sh"')" ] || bad "env.sh prints no palette on the console"
[ -z "$(env -i HOME="$HOME" TERM=linux SSH_TTY=/dev/pts/1 sh -c '. "$HOME/.config/themer/env.sh"')" ] || bad "env.sh repaints the palette over ssh"
./themer system status >/dev/null 2>&1 || bad "themer system status failed"
if [ "$(id -u)" != 0 ]; then
	./themer system install >/dev/null 2>&1 && bad "themer system install ran without root"
fi

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
	grep -v '^\.config/themer/\(state\|tty\.rgb\|env\.sh\|templates/\|undo/dirs\.pending\|system/\(tty\.palette\|console\.conf\)\)' |
	grep -v '^\.config/environment\.d/50-themer\.conf$' |
	grep -v '^\.config/\(foot\|sway\|waybar\|nvim\|qt6ct\|gtk-3\.0\|thunderbird\)/' |
	grep -vx '\.config/\(zathura/zathurarc\|lazygit/config\.yml\|rofi/config\.rasi\|mako/config\|starship\.toml\|btop/btop\.conf\|labwc/themerc-override\|labwc/rc\.xml\|openbox/rc\.xml\|bspwm/bspwmrc\|fuzzel/fuzzel\.ini\|hypr/hyprland\.lua\|hypr/hyprland\.conf\|hypr/hyprlock\.conf\|bat/config\|swaylock/config\|river/init\|swaync/style\.css\|niri/config\.kdl\)' || true)
[ -z "$left" ] || bad "files left after unload: [$left]
$left"
[ ! -e "$HOME/.local/state/themer" ] || [ -z "$(ls -A "$HOME/.local/state/themer")" ] ||
	bad "manifests left: $(ls "$HOME/.local/state/themer")"

# a config that arrives with a block of ours in it (a dotfile from a machine where
# themer ran): unload takes the block out and keeps the rest
H2=$(mktemp -d)
mkdir -p "$H2/bin" "$H2/.config/sway"; printf '#!/bin/sh\n' > "$H2/bin/sway"; chmod +x "$H2/bin/sway"
printf 'set $mod Mod4\n# themer:wm begin\ninclude /old/machine/themer.conf\n# themer:wm end\nbindsym $mod+Return exec foot\n' > "$H2/.config/sway/config"
HOME=$H2 PATH="$H2/bin:$PATH" ./themer module load sway >/dev/null 2>&1
HOME=$H2 PATH="$H2/bin:$PATH" ./themer module unload sway >/dev/null 2>&1
grep -q 'themer:wm\|/old/machine' "$H2/.config/sway/config" && bad "a block of ours from elsewhere stayed after unload"
grep -q '^bindsym $mod+Return exec foot$' "$H2/.config/sway/config" || bad "the user's sway config lost a line"
rm -rf "$H2"

if [ "$fail" = 0 ]; then
	t "ok"
else
	exit 1
fi
