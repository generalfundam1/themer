/* text.c: the words every front end shows, kept in one place so the four read the
 * same: the pages and their order, each section's heading and the line under it,
 * the labels, the buttons and their tips, and the status lines. The TUI, themer-gtk
 * and themer-qt read front_text(); themer-web sends the table to its page, which
 * fills every element marked data-t="key" from it. A new control takes its words
 * from here, not from a string of its own. */
#include <string.h>
#include "themer.h"

const FrontPage front_pages[FP_N] = {
	[FP_THEME]   = { "theme",   "Theme" },
	[FP_FONTS]   = { "fonts",   "Fonts" },
	[FP_ICONS]   = { "icons",   "Icons" },
	[FP_MODULES] = { "modules", "Modules" },
	[FP_SYSTEM]  = { "system",  "System" },
};

static const struct { const char *key, *text; } texts[FT_N] = {
	/* Theme */
	[FT_SCHEME]          = { "scheme", "Color scheme" },
	[FT_SCHEME_FIND]     = { "scheme_find", "Find a scheme" },
	[FT_SCHEME_IMPORT]   = { "scheme_import", "Import…" },
	[FT_SCHEME_IMPORT_TIP] = { "scheme_import_tip", "A color scheme file: a .theme, Xresources, kitty, foot or alacritty colors, or Gogh lines" },
	[FT_SCHEME_REMOVE]   = { "scheme_remove", "Remove" },
	[FT_SCHEME_REMOVE_TIP] = { "scheme_remove_tip", "Delete the selected scheme if it is yours, or hide it if it is built in" },
	[FT_SCHEME_RESTORE]  = { "scheme_restore", "Restore hidden" },
	[FT_SCHEME_RESTORE_TIP] = { "scheme_restore_tip", "Bring back every built-in scheme that was removed" },
	[FT_MODE]            = { "mode", "Mode" },
	[FT_SET]             = { "set", "Template set" },
	[FT_SET_NEW]         = { "set_new", "New set…" },
	[FT_SET_NEW_TIP]     = { "set_new_tip", "A copy of the selected set under a new name, in ~/.config/themer/templates, to edit" },
	[FT_SET_IMPORT]      = { "set_import", "Import theme…" },
	[FT_SET_IMPORT_TIP]  = { "set_import_tip", "Turn a theme you have (a GTK theme directory, a .qss, a chrome/ directory...) into part of a set" },
	[FT_SET_CHECK]       = { "set_check", "Check" },
	[FT_SET_CHECK_TIP]   = { "set_check_tip", "Render every file of the selected set for every theme, both modes" },
	[FT_SET_NOTE]        = { "set_note", "A template set is the look: the widgets, bars, and chrome every module draws. What a set lacks comes from Themer Default." },
	[FT_COLORS]          = { "colors", "Colors" },
	[FT_COLORS_NOTE]     = { "colors_note", "A color of your own over the scheme's, in both modes: pick one, or Scheme to go back to the scheme's." },
	[FT_COLOR_RESET]     = { "color_reset", "Scheme" },
	[FT_COLOR_RESET_TIP] = { "color_reset_tip", "Back to the scheme's color" },
	[FT_COLOR_SCHEMES]   = { "color_schemes", "the scheme's" },
	/* Fonts */
	[FT_FONTS]           = { "fonts", "Fonts" },
	[FT_FONT_UI]         = { "font_ui", "Interface" },
	[FT_FONT_MONO]       = { "font_mono", "Terminal" },
	[FT_FONT_TTY]        = { "font_tty", "Console" },
	[FT_FONT_SIZE_TIP]   = { "font_size_tip", "Size in points" },
	[FT_FONT_TTY_TIP]    = { "font_tty_tip", "The Linux console's font (vconsole.conf)" },
	[FT_FONTS_NOTE]      = { "fonts_note", "The interface font goes to GTK, Qt, the window manager, and the browsers; the terminal font to the terminals, editors, and Qt's fixed font; the console font to the Linux console." },
	[FT_RENDERING]       = { "rendering", "Rendering" },
	[FT_HINTING]         = { "hinting", "Hinting" },
	[FT_ANTIALIAS]       = { "antialias", "Antialiasing" },
	[FT_RGBA]            = { "rgba", "Subpixel order" },
	[FT_RENDERING_NOTE]  = { "rendering_note", "Written to the GTK settings, xsettingsd, gsettings, and fontconfig, so every toolkit draws text the same way." },
	/* Icons */
	[FT_ICONS]           = { "icons", "Icon theme" },
	[FT_ICONS_NOTE]      = { "icons_note", "Set for GTK, Qt, xsettingsd, and gsettings. Shown as GTK and Qt find them: the theme, the themes it inherits, hicolor." },
	[FT_ICON_UNDRAWN]    = { "icon_undrawn", "can't draw" },
	[FT_ICON_MISSING]    = { "icon_missing", "none" },
	[FT_CURSOR]          = { "cursor", "Mouse cursor" },
	[FT_CURSOR_THEME]    = { "cursor_theme", "Theme" },
	[FT_CURSOR_SIZE]     = { "cursor_size", "Size" },
	[FT_CURSOR_NOTE]     = { "cursor_note", "Applied through the cursor module (the Xcursor default theme, XCURSOR_THEME and XCURSOR_SIZE, Xresources), the GTK settings, gsettings, and sway's seat." },
	/* Modules */
	[FT_MODULES]         = { "modules", "Modules" },
	[FT_MODULES_NOTE]    = { "modules_note", "Each module themes one program: turned on it writes that program's settings, turned off it removes them. Switches and options take effect at once." },
	[FT_NO_OPTIONS]      = { "no_options", "This module has no options." },
	[FT_STATE_ABSENT]    = { "state_absent", "not installed" },
	[FT_STATE_ON]        = { "state_on", "on" },
	[FT_STATE_OFF]       = { "state_off", "off" },
	[FT_ABSENT_NOTE]     = { "absent_note", "Its program is not installed here, so the module cannot be loaded." },
	[FT_MODULE_ABSENT]   = { "module_absent", "its program is not installed here, so it cannot be loaded" },
	[FT_GROUP_LOADED]    = { "group_loaded", "Loaded" },
	[FT_GROUP_UNLOADED]  = { "group_unloaded", "Unloaded" },
	[FT_GROUP_ABSENT]    = { "group_absent", "Not installed" },
	[FT_DEFAULT]         = { "default", "default" },
	[FT_GLOBAL_SET]      = { "global_set", "the global set" },
	[FT_NO_CURSOR]       = { "no_cursor", "no cursor theme of that name is installed" },
	[FT_OPTION_RESET]    = { "option_reset", "Reset" },
	[FT_OPTION_RESET_TIP] = { "option_reset_tip", "Back to the default" },
	/* System */
	[FT_SERVER]          = { "server", "Server" },
	[FT_SERVER_NOTE]     = { "server_note", "themer-web: these settings in the browser, and the userstyles of the stylus module. It stops by itself when no browser module (firefox, stylus) is on." },
	[FT_SERVER_UP]       = { "server_up", "themer-web is running" },
	[FT_SERVER_DOWN]     = { "server_down", "themer-web is stopped" },
	[FT_SERVER_START]    = { "server_start", "Start" },
	[FT_SERVER_STOP]     = { "server_stop", "Stop" },
	[FT_SERVER_OPEN]     = { "server_open", "Open in the browser" },
	[FT_PORT]            = { "port", "Port" },
	[FT_PORT_NOTE]       = { "port_note", "On localhost. A running server moves to a new port. Stylus installs the userstyles from it and keeps them current while it runs; each site, with its Install button, is under Modules, stylus." },
	[FT_ENV]             = { "env", "Environment" },
	[FT_ENV_NOTE]        = { "env_note", "Variables for the whole session, written to environment.d on apply, where login sessions and systemd user services read them." },
	[FT_ENV_NONE]        = { "env_none", "No variables." },
	[FT_ENV_NAME]        = { "env_name", "NAME" },
	[FT_ENV_VALUE]       = { "env_value", "value" },
	[FT_ENV_ADD]         = { "env_add", "Add" },
	[FT_ENV_REMOVE]      = { "env_remove", "Remove" },
	[FT_ABOUT]           = { "about", "About" },
	[FT_ABOUT_NOTE]      = { "about_note", "themer " VERSION ": one theme for the whole desktop, from GTK, Qt and the browsers to the window manager, the terminals and the console. https://github.com/generalfundam1/themer" },
	/* dialogs */
	[FT_SET_NEW_TITLE]   = { "set_new_title", "New template set" },
	[FT_SET_NEW_PROMPT]  = { "set_new_prompt", "A copy of the current set under this name, in ~/.config/themer/templates/, to edit. Letters, digits, - and _." },
	[FT_SET_IMPORT_TITLE] = { "set_import_title", "Import a theme into a set" },
	[FT_SET_IMPORT_NOTE] = { "set_import_note", "The theme's files go under the module's names in the set, with every color that matches the current theme turned into a variable, so it follows the color theme. Import with the theme and mode the look was made for." },
	[FT_SET_IMPORT_SET]  = { "set_import_set", "Set" },
	[FT_SET_IMPORT_SET_HINT] = { "set_import_set_hint", "new or existing set name" },
	[FT_SET_IMPORT_MODULE] = { "set_import_module", "Module" },
	[FT_SET_IMPORT_PATH] = { "set_import_path", "Theme" },
	[FT_SET_IMPORT_PATH_HINT] = { "set_import_path_hint", "a theme directory or file" },
	[FT_SCHEME_IMPORT_TITLE] = { "scheme_import_title", "Import a color scheme" },
	[FT_FONT_FAMILY_TIP] = { "font_family_tip", "Choose the family" },
	[FT_COLOR_YOURS_TIP] = { "color_yours_tip", "Your color" },
	[FT_COLOR_PICK_TIP]  = { "color_pick_tip", "The scheme's color: pick one to use your own" },
	[FT_MODULE_SWITCH_TIP] = { "module_switch_tip", "On: this program's settings are written; off: they are removed" },
	[FT_INSTALL_TIP]     = { "install_tip", "Opens the style in your browser, where Stylus offers to install it" },
	/* what an action did, in the status line */
	[FT_SYNCED]          = { "synced", "updated with changes made in another front end" },
	[FT_MODULE_LOADED]   = { "module_loaded", "module loaded" },
	[FT_MODULE_UNLOADED] = { "module_unloaded", "module unloaded" },
	[FT_MODULE_LOAD_FAILED] = { "module_load_failed", "the module did not load; see the output" },
	[FT_MODULE_UNLOAD_FAILED] = { "module_unload_failed", "unload had errors" },
	[FT_OPTION_SAVED]    = { "option_saved", "option saved" },
	[FT_OPTION_REFUSED]  = { "option_refused", "that value is not allowed" },
	[FT_SERVER_STARTED]  = { "server_started", "themer-web started" },
	[FT_SERVER_STOPPED]  = { "server_stopped", "themer-web stopped" },
	[FT_SERVER_START_FAILED] = { "server_start_failed", "could not start themer-web (is a browser module on?)" },
	[FT_SERVER_STOP_FAILED] = { "server_stop_failed", "could not stop themer-web" },
	[FT_PORT_SAVED]      = { "port_saved", "port saved" },
	[FT_PORT_FAILED]     = { "port_failed", "could not move themer-web to that port" },
	[FT_ENV_INCOMPLETE]  = { "env_incomplete", "a variable needs a name and a value" },
	[FT_ENV_REFUSED]     = { "env_refused", "not a variable name (letters, digits and _)" },
	[FT_SCHEME_IN_USE]   = { "scheme_in_use", "that is the scheme in use: choose another first" },
	[FT_SCHEME_REMOVE_ASK] = { "scheme_remove_ask", "Yours are deleted; built-in ones are hidden until Restore hidden." },
	[FT_SCHEME_DELETED]  = { "scheme_deleted", "deleted" },
	[FT_SCHEME_HIDDEN]   = { "scheme_hidden", "hidden: Restore hidden brings it back" },
	[FT_SCHEME_REMOVE_FAILED] = { "scheme_remove_failed", "could not remove it" },
	[FT_SCHEME_RESTORED] = { "scheme_restored", "every hidden built-in scheme is back" },
	[FT_SCHEME_RESTORE_FAILED] = { "scheme_restore_failed", "could not restore" },
	[FT_SCHEME_IMPORTED] = { "scheme_imported", "imported; it is in the list" },
	[FT_SCHEME_IMPORT_NONE] = { "scheme_import_none", "nothing imported" },
	[FT_SCHEME_UNREADABLE] = { "scheme_unreadable", "cannot read that scheme" },
	[FT_SET_COPY_FAILED] = { "set_copy_failed", "could not copy the set; the name may be taken or not valid" },
	[FT_SET_IMPORT_FAILED] = { "set_import_failed", "import failed; see the report" },
	[FT_TTY_INVALID]     = { "tty_invalid", "a console font name is letters, digits, - and _" },
	[FT_FONT_INVALID]    = { "font_invalid", "not a font name" },
	/* the TUI's rows, where a size has no heading over it */
	[FT_ROW_UI_SIZE]     = { "row_ui_size", "Interface size" },
	[FT_ROW_MONO_SIZE]   = { "row_mono_size", "Terminal size" },
	[FT_ROW_CURSOR_SIZE] = { "row_cursor_size", "Cursor size" },
	/* the actions and the status line */
	[FT_APPLY]           = { "apply", "Apply" },
	[FT_REVERT]          = { "revert", "Revert to applied" },
	[FT_QUIT]            = { "quit", "Quit" },
	[FT_REMOVE]          = { "remove", "Remove all styles" },
	[FT_REMOVE_ASK]      = { "remove_ask", "Every program goes back to its own look: themer's files are removed and your configs put back as they were. Your settings stay; Apply puts the styles back." },
	[FT_REMOVED]         = { "removed", "every style removed; Apply puts them back" },
	[FT_REMOVE_FAILED]   = { "remove_failed", "styles removed, with errors: see the output" },
	[FT_REMOVED_STATE]   = { "removed_state", "styles removed: Apply puts them back" },
	[FT_UP_TO_DATE]      = { "up_to_date", "up to date" },
	[FT_UNAPPLIED]       = { "unapplied", "unapplied changes" },
	[FT_APPLYING]        = { "applying", "applying…" },
	[FT_REVERTED]        = { "reverted", "back to the applied settings" },
	[FT_QUIT_ASK]        = { "quit_ask", "There are unapplied changes." },
	[FT_DISCARD]         = { "discard", "Discard" },
	[FT_KEEP_EDITING]    = { "keep_editing", "Keep editing" },
};

/* a page by its id, or by the name of a page it took in (cursor, server, env) */
int
front_page_find(const char *name)
{
	static const struct { const char *old; int page; } moved[] = {
		{ "cursor", FP_ICONS }, { "server", FP_SYSTEM }, { "env", FP_SYSTEM }, { "environment", FP_SYSTEM },
	};

	if (!name)
		return -1;
	for (int p = 0; p < FP_N; p++)
		if (!strcmp(name, front_pages[p].id))
			return p;
	for (size_t k = 0; k < sizeof moved / sizeof moved[0]; k++)
		if (!strcmp(name, moved[k].old))
			return moved[k].page;
	return -1;
}

const char *
front_text(int id)
{
	return id >= 0 && id < FT_N && texts[id].text ? texts[id].text : "";
}

const char *
front_text_key(int id)
{
	return id >= 0 && id < FT_N && texts[id].key ? texts[id].key : "";
}
