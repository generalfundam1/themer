#!/bin/sh
# Renders the Firefox chrome for real, for a look by eye:
#   FIREFOX=/path/to/firefox sh test/firefox.sh [theme] [dark|light]
# Makes a throwaway profile, applies the theme to it, starts Firefox on a virtual
# display (Xvfb) and writes screenshots of the whole screen to test/firefox/out/:
# the chrome with a blank page, and about:preferences. Needs Xvfb and ImageMagick's
# import. Any Firefox 115 or newer (or a Firefox-based browser with a stock chrome).
set -u
cd "$(dirname "$0")/.."
: "${FIREFOX:?set FIREFOX to the firefox binary}"
export THEMER_THEMES="$PWD/themes" THEMER_MODULES="$PWD/modules" THEMER_TEMPLATES="$PWD/templates" THEMER_ICONS="$PWD/icons"
export HOME="$(mktemp -d)"
out="$PWD/test/firefox/out"; mkdir -p "$out"
prof="$HOME/.mozilla/firefox/t.default"
mkdir -p "$prof/chrome"
printf '[Profile0]\nName=t\nIsRelative=1\nPath=t.default\nDefault=1\n' > "$HOME/.mozilla/firefox/profiles.ini"
cat > "$prof/user.js" <<'PREFS'
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.policy.dataSubmissionPolicyBypassNotification", true);
user_pref("browser.toolbars.bookmarks.visibility", "always");
user_pref("browser.aboutwelcome.enabled", false);
PREFS
touch "$prof/prefs.js"
./themer theme "${1:-gruvbox}" >/dev/null 2>&1
./themer "${2:-dark}" 2>&1 | grep -i "firefox profile"

display=:97
Xvfb $display -screen 0 1400x900x24 -nolisten tcp >/dev/null 2>&1 &
xvfb=$!
sleep 1
shot() {   # shot NAME URL: Firefox on URL, the screen captured once it has drawn
	DISPLAY=$display "$FIREFOX" --profile "$prof" --no-remote --width 1400 --height 900 "$2" \
		>"$out/firefox.log" 2>&1 &
	ff=$!
	sleep 8
	DISPLAY=$display import -window root "$out/$1.png"
	kill $ff 2>/dev/null; wait $ff 2>/dev/null
}
shot idle about:blank
shot preferences about:preferences
kill $xvfb 2>/dev/null
rm -rf "$HOME"
echo "screenshots in $out"
