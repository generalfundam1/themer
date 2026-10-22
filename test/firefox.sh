#!/bin/sh
# Renders the Firefox chrome for real: FIREFOX=/path/to/firefox sh test/firefox.sh [theme] [dark|light]
# Makes a throwaway profile, applies the theme to it, starts Firefox under Xvfb with
# Marionette, and writes screenshots and the toolbar geometry to test/firefox/out/.
# Needs xvfb-run and python3. Any Firefox 115 or newer (or a Firefox-based browser
# with a stock chrome) will do.
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
user_pref("marionette.port", 2828);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.policy.dataSubmissionPolicyBypassNotification", true);
user_pref("browser.toolbars.bookmarks.visibility", "always");
user_pref("browser.startup.page", 0);
user_pref("browser.aboutwelcome.enabled", false);
PREFS
touch "$prof/prefs.js"
./themer theme "${1:-gruvbox}" >/dev/null 2>&1
./themer "${2:-dark}" 2>&1 | grep -i "firefox profile"
xvfb-run -a -s "-screen 0 1400x900x24" "$FIREFOX" --marionette -remote-allow-system-access --allow-downgrade \
	--profile "$prof" --no-remote --width 1400 --height 900 about:blank >"$out/firefox.log" 2>&1 &
pid=$!
sleep 8
python3 test/firefox/shots.py "$out"
kill $pid 2>/dev/null; sleep 1
rm -rf "$HOME"
echo "screenshots in $out"
