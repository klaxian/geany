/*
 *      keyfile.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2005-2007 Enrico Tröger <enrico.troeger@uvena.de>
 *      Copyright 2006-2007 Nick Treleaven <nick.treleaven@btinternet.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */

/*
 * geany.conf preferences file loading and saving.
 */

#include <stdlib.h>
#include <string.h>

#include "geany.h"

#ifdef HAVE_VTE
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "support.h"
#include "keyfile.h"
#include "ui_utils.h"
#include "utils.h"
#include "document.h"
#include "sciwrappers.h"
#include "encodings.h"
#include "vte.h"
#include "main.h"
#include "msgwindow.h"
#include "search.h"
#include "project.h"
#include "editor.h"


static gchar *scribble_text = NULL;
static GPtrArray *session_files = NULL;
static gint hpan_position;
static gint vpan_position;


static void save_recent_files(GKeyFile *config)
{
	gchar **recent_files = g_new0(gchar*, app->mru_length + 1);
	guint i;

	for (i = 0; i < app->mru_length; i++)
	{
		if (! g_queue_is_empty(app->recent_queue))
		{
			// copy the values, this is necessary when this function is called from the
			// preferences dialog or when quitting is canceled to keep the queue intact
			recent_files[i] = g_strdup(g_queue_peek_nth(app->recent_queue, i));
		}
		else
		{
			recent_files[i] = NULL;
			break;
		}
	}
	// There is a bug in GTK2.6 g_key_file_set_string_list, we must NULL terminate.
	recent_files[app->mru_length] = NULL;
	g_key_file_set_string_list(config, "files", "recent_files",
				(const gchar**)recent_files, app->mru_length);
	g_strfreev(recent_files);
}


static void save_session_files(GKeyFile *config)
{
	gint idx;
	gboolean have_session_files;
	gchar *tmp;
	gchar *entry = g_malloc(14);
	guint i = 0, j = 0, max;

	if (cl_options.load_session)
	{
		// store the filenames to reopen them the next time
		max = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
		for(i = 0; i < max; i++)
		{
			idx = document_get_n_idx(i);
			if (idx >= 0 && doc_list[idx].file_name)
			{
				gchar *fname;
				filetype *ft = doc_list[idx].file_type;

				if (ft == NULL)	// can happen when saving a new file when quitting
					ft = filetypes[GEANY_FILETYPES_ALL];
				g_snprintf(entry, 13, "FILE_NAME_%d", j);
				fname = g_strdup_printf("%d:%d:%s", sci_get_current_position(doc_list[idx].sci),
					ft->uid, doc_list[idx].file_name);
				g_key_file_set_string(config, "files", entry, fname);
				g_free(fname);
				j++;
			}
		}
		// if open filenames less than saved session files, delete existing entries in the list
		have_session_files = TRUE;
		i = j;
		while (have_session_files)
		{
			g_snprintf(entry, 13, "FILE_NAME_%d", i);
			tmp = g_key_file_get_string(config, "files", entry, NULL);
			if (tmp == NULL)
			{
				have_session_files = FALSE;
			}
			else
			{
				g_key_file_remove_key(config, "files", entry, NULL);
				g_free(tmp);
				i++;
			}
		}
	}
	g_free(entry);
}


void configuration_save()
{
	GKeyFile *config = g_key_file_new();
	gchar *configfile = g_strconcat(app->configdir, G_DIR_SEPARATOR_S, "geany.conf", NULL);
	gchar *data;
	GtkTextBuffer *buffer;
	GtkTextIter start, end;

	g_key_file_load_from_file(config, configfile, G_KEY_FILE_NONE, NULL);

	// gets the text from the scribble textview
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(app->window, "textview_scribble")));
	gtk_text_buffer_get_bounds(buffer, &start, &end);
	scribble_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

	// store basic settings
	if (app->pref_main_save_winpos)
	{
		g_key_file_set_integer(config, PACKAGE, "treeview_position",
				gtk_paned_get_position(GTK_PANED(lookup_widget(app->window, "hpaned1"))));
		g_key_file_set_integer(config, PACKAGE, "msgwindow_position",
				gtk_paned_get_position(GTK_PANED(lookup_widget(app->window, "vpaned1"))));
	}

	g_key_file_set_integer(config, PACKAGE, "mru_length", app->mru_length);
	g_key_file_set_integer(config, PACKAGE, "long_line_type", app->long_line_type);
	g_key_file_set_integer(config, PACKAGE, "tab_pos_editor", app->tab_pos_editor);
	g_key_file_set_integer(config, PACKAGE, "tab_pos_msgwin", app->tab_pos_msgwin);
	g_key_file_set_integer(config, PACKAGE, "tab_pos_sidebar", app->tab_pos_sidebar);
	g_key_file_set_integer(config, PACKAGE, "autocompletion_max_height", app->autocompletion_max_height);
	g_key_file_set_integer(config, PACKAGE, "long_line_column", app->long_line_column);
	g_key_file_set_string(config, PACKAGE, "long_line_color", app->long_line_color);
	g_key_file_set_boolean(config, PACKAGE, "beep_on_errors", app->beep_on_errors);
	g_key_file_set_boolean(config, PACKAGE, "sidebar_symbol_visible", app->sidebar_symbol_visible);
	g_key_file_set_boolean(config, PACKAGE, "sidebar_openfiles_visible", app->sidebar_openfiles_visible);
	g_key_file_set_boolean(config, PACKAGE, "sidebar_visible", app->sidebar_visible);
	g_key_file_set_boolean(config, PACKAGE, "statusbar_visible", app->statusbar_visible);
	g_key_file_set_boolean(config, PACKAGE, "msgwindow_visible", app->msgwindow_visible);
	g_key_file_set_boolean(config, PACKAGE, "use_folding", editor_prefs.folding);
	g_key_file_set_boolean(config, PACKAGE, "unfold_all_children", editor_prefs.unfold_all_children);
	g_key_file_set_boolean(config, PACKAGE, "show_editor_scrollbars", editor_prefs.show_scrollbars);
	g_key_file_set_integer(config, PACKAGE, "indent_mode", editor_prefs.indent_mode);
	g_key_file_set_boolean(config, PACKAGE, "use_tab_to_indent", app->use_tab_to_indent);
	g_key_file_set_boolean(config, PACKAGE, "use_indicators", editor_prefs.use_indicators);
	g_key_file_set_boolean(config, PACKAGE, "show_indent_guide", editor_prefs.show_indent_guide);
	g_key_file_set_boolean(config, PACKAGE, "show_white_space", editor_prefs.show_white_space);
	g_key_file_set_boolean(config, PACKAGE, "show_markers_margin", app->show_markers_margin);
	g_key_file_set_boolean(config, PACKAGE, "show_linenumber_margin", app->show_linenumber_margin);
	g_key_file_set_boolean(config, PACKAGE, "line_breaking", editor_prefs.line_breaking);
	g_key_file_set_boolean(config, PACKAGE, "show_line_endings", editor_prefs.show_line_endings);
	g_key_file_set_boolean(config, PACKAGE, "fullscreen", app->fullscreen);
	g_key_file_set_boolean(config, PACKAGE, "tab_order_ltr", app->tab_order_ltr);
	g_key_file_set_boolean(config, PACKAGE, "show_notebook_tabs", app->show_notebook_tabs);
	g_key_file_set_boolean(config, PACKAGE, "brace_match_ltgt", app->brace_match_ltgt);
	g_key_file_set_boolean(config, PACKAGE, "switch_msgwin_pages", app->switch_msgwin_pages);
	g_key_file_set_boolean(config, PACKAGE, "auto_focus", app->auto_focus);
	g_key_file_set_boolean(config, PACKAGE, "auto_close_xml_tags", editor_prefs.auto_close_xml_tags);
	g_key_file_set_boolean(config, PACKAGE, "auto_complete_constructs", editor_prefs.auto_complete_constructs);
	g_key_file_set_boolean(config, PACKAGE, "auto_complete_symbols", editor_prefs.auto_complete_symbols);
#ifdef HAVE_VTE
	g_key_file_set_boolean(config, "VTE", "load_vte", vte_info.load_vte);
	if (vte_info.load_vte && vc != NULL)
	{
		gchar *tmp_string;

		g_key_file_set_string(config, "VTE", "emulation", vc->emulation);
		g_key_file_set_string(config, "VTE", "font", vc->font);
		g_key_file_set_boolean(config, "VTE", "scroll_on_key", vc->scroll_on_key);
		g_key_file_set_boolean(config, "VTE", "scroll_on_out", vc->scroll_on_out);
		g_key_file_set_boolean(config, "VTE", "ignore_menu_bar_accel", vc->ignore_menu_bar_accel);
		g_key_file_set_boolean(config, "VTE", "follow_path", vc->follow_path);
		g_key_file_set_boolean(config, "VTE", "run_in_vte", vc->run_in_vte);
		g_key_file_set_integer(config, "VTE", "scrollback_lines", vc->scrollback_lines);
		g_key_file_set_string(config, "VTE", "font", vc->font);
		g_key_file_set_string(config, "VTE", "shell", vc->shell);
		tmp_string = utils_get_hex_from_color(vc->colour_fore);
		g_key_file_set_string(config, "VTE", "colour_fore", tmp_string);
		g_free(tmp_string);
		tmp_string = utils_get_hex_from_color(vc->colour_back);
		g_key_file_set_string(config, "VTE", "colour_back", tmp_string);
		g_free(tmp_string);
		vte_get_working_directory();	// refresh vte_info.dir
		g_key_file_set_string(config, "VTE", "last_dir", vte_info.dir);
	}
#endif
	g_key_file_set_string(config, PACKAGE, "default_open_path", app->default_open_path);
	g_key_file_set_string(config, PACKAGE, "custom_date_format", app->custom_date_format);
	g_key_file_set_string(config, PACKAGE, "context_action_cmd", app->context_action_cmd);
	if (app->custom_commands != NULL)
	{
		g_key_file_set_string_list(config, PACKAGE, "custom_commands",
				(const gchar**) app->custom_commands, g_strv_length(app->custom_commands));
	}

	g_key_file_set_string(config, PACKAGE, "editor_font", app->editor_font);
	g_key_file_set_string(config, PACKAGE, "tagbar_font", app->tagbar_font);
	g_key_file_set_string(config, PACKAGE, "msgwin_font", app->msgwin_font);
	g_key_file_set_string(config, PACKAGE, "scribble_text", scribble_text);
	if (app->pref_main_save_winpos && ! app->fullscreen)
	{
		gtk_window_get_position(GTK_WINDOW(app->window), &app->geometry[0], &app->geometry[1]);
		gtk_window_get_size(GTK_WINDOW(app->window), &app->geometry[2], &app->geometry[3]);
		if (gdk_window_get_state(app->window->window) & GDK_WINDOW_STATE_MAXIMIZED)
			app->geometry[4] = 1;
		else
			app->geometry[4] = 0;

		g_key_file_set_integer_list(config, PACKAGE, "geometry", app->geometry, 5);
	}
	g_key_file_set_integer(config, PACKAGE, "pref_editor_tab_width", editor_prefs.tab_width);
	g_key_file_set_boolean(config, PACKAGE, "pref_editor_use_tabs", editor_prefs.use_tabs);
	g_key_file_set_boolean(config, PACKAGE, "pref_main_confirm_exit", app->pref_main_confirm_exit);
	g_key_file_set_boolean(config, PACKAGE, "pref_main_suppress_search_dialogs", app->pref_main_suppress_search_dialogs);
	g_key_file_set_boolean(config, PACKAGE, "pref_main_load_session", app->pref_main_load_session);
	g_key_file_set_boolean(config, PACKAGE, "pref_main_save_winpos", app->pref_main_save_winpos);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show", app->toolbar_visible);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_search", app->pref_toolbar_show_search);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_goto", app->pref_toolbar_show_goto);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_zoom", app->pref_toolbar_show_zoom);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_undo", app->pref_toolbar_show_undo);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_navigation", app->pref_toolbar_show_navigation);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_compile", app->pref_toolbar_show_compile);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_colour", app->pref_toolbar_show_colour);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_fileops", app->pref_toolbar_show_fileops);
	g_key_file_set_boolean(config, PACKAGE, "pref_toolbar_show_quit", app->pref_toolbar_show_quit);
	g_key_file_set_integer(config, PACKAGE, "pref_toolbar_icon_style", app->toolbar_icon_style);
	g_key_file_set_integer(config, PACKAGE, "pref_toolbar_icon_size", app->toolbar_icon_size);
	g_key_file_set_boolean(config, PACKAGE, "pref_editor_new_line", editor_prefs.new_line);
	g_key_file_set_boolean(config, PACKAGE, "pref_editor_replace_tabs", editor_prefs.replace_tabs);
	g_key_file_set_boolean(config, PACKAGE, "pref_editor_trail_space", editor_prefs.trail_space);
	g_key_file_set_boolean(config, PACKAGE, "pref_editor_disable_dnd", editor_prefs.disable_dnd);
	g_key_file_set_string(config, PACKAGE, "pref_editor_default_encoding", encodings[editor_prefs.default_encoding].charset);
	g_key_file_set_string(config, PACKAGE, "pref_template_developer", app->pref_template_developer);
	g_key_file_set_string(config, PACKAGE, "pref_template_company", app->pref_template_company);
	g_key_file_set_string(config, PACKAGE, "pref_template_mail", app->pref_template_mail);
	g_key_file_set_string(config, PACKAGE, "pref_template_initial", app->pref_template_initial);
	g_key_file_set_string(config, PACKAGE, "pref_template_version", app->pref_template_version);

	// store tools settings
	g_key_file_set_string(config, "tools", "make_cmd", app->tools_make_cmd ? app->tools_make_cmd : "");
	g_key_file_set_string(config, "tools", "term_cmd", app->tools_term_cmd ? app->tools_term_cmd : "");
	g_key_file_set_string(config, "tools", "browser_cmd", app->tools_browser_cmd ? app->tools_browser_cmd : "");
	g_key_file_set_string(config, "tools", "print_cmd", app->tools_print_cmd ? app->tools_print_cmd : "");
	g_key_file_set_string(config, "tools", "grep_cmd", app->tools_grep_cmd ? app->tools_grep_cmd : "");

	g_key_file_set_string(config, "search", "fif_extra_options", search_prefs.fif_extra_options ? search_prefs.fif_extra_options : "");

	project_save_prefs(config);	// save project filename, etc.
	save_recent_files(config);
	save_session_files(config);

	// write the file
	data = g_key_file_to_data(config, NULL, NULL);
	utils_write_file(configfile, data);
	g_free(data);

	g_key_file_free(config);
	g_free(configfile);
	g_free(scribble_text);
}


static void load_file_lists(GKeyFile *config)
{
	gchar **recent_files;
	guint i;
	gsize len = 0;
	gboolean have_session_files;
	gchar *entry = g_malloc(14);
	gchar *tmp_string;
	GError *error = NULL;

	recent_files = g_key_file_get_string_list(config, "files", "recent_files", &len, NULL);
	if (recent_files != NULL)
	{
		for (i = 0; (i < len) && (i < app->mru_length); i++)
		{
			gchar *filename = g_strdup(recent_files[i]);
			g_queue_push_tail(app->recent_queue, filename);
		}
	}
	g_strfreev(recent_files);

	session_files = g_ptr_array_new();
	have_session_files = TRUE;
	i = 0;
	while (have_session_files)
	{
		g_snprintf(entry, 13, "FILE_NAME_%d", i);
		tmp_string = g_key_file_get_string(config, "files", entry, &error);
		if (! tmp_string || error)
		{
			g_error_free(error);
			error = NULL;
			have_session_files = FALSE;
		}
		g_ptr_array_add(session_files, tmp_string);
		i++;
	}
	g_free(entry);
}


#define GEANY_GET_SETTING(propertyname, value, default_value) \
	if (g_object_class_find_property( \
		G_OBJECT_GET_CLASS(G_OBJECT(gtk_settings_get_default())), propertyname)) \
			g_object_get(G_OBJECT(gtk_settings_get_default()), propertyname, &value, \
				NULL); \
	else \
		value = default_value;

gboolean configuration_load()
{
	gint *geo;
	gchar *configfile = g_strconcat(app->configdir, G_DIR_SEPARATOR_S, "geany.conf", NULL);
	gchar *tmp_string, *tmp_string2;
	const gchar *default_charset = NULL;
	GKeyFile *config = g_key_file_new();
	GError *error = NULL;

	if (! g_file_test(configfile, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK))
	{	// config file does not (yet) exist, so try to load a global config file which may be
		// created by distributors
		geany_debug("No config file found, try to use global configuration.");
		setptr(configfile, g_strconcat(app->datadir, G_DIR_SEPARATOR_S "geany.conf", NULL));
	}

	g_key_file_load_from_file(config, configfile, G_KEY_FILE_NONE, NULL);

	app->toolbar_visible = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show", TRUE);
	{
		GtkIconSize tb_iconsize;
		GtkToolbarStyle tb_style;
		GEANY_GET_SETTING("gtk-toolbar-style", tb_style, GTK_TOOLBAR_ICONS);
		GEANY_GET_SETTING("gtk-toolbar-icon-size", tb_iconsize, GTK_ICON_SIZE_LARGE_TOOLBAR);
		app->toolbar_icon_style = utils_get_setting_integer(config, PACKAGE, "pref_toolbar_icon_style", tb_style);
		app->toolbar_icon_size = utils_get_setting_integer(config, PACKAGE, "pref_toolbar_icon_size", tb_iconsize);
	}
	app->beep_on_errors = utils_get_setting_boolean(config, PACKAGE, "beep_on_errors", TRUE);
	app->mru_length = utils_get_setting_integer(config, PACKAGE, "mru_length", GEANY_DEFAULT_MRU_LENGTH);
	app->long_line_type = utils_get_setting_integer(config, PACKAGE, "long_line_type", 0);
	app->long_line_color = utils_get_setting_string(config, PACKAGE, "long_line_color", "#C2EBC2");
	app->long_line_column = utils_get_setting_integer(config, PACKAGE, "long_line_column", 72);
	app->autocompletion_max_height = utils_get_setting_integer(config, PACKAGE, "autocompletion_max_height", GEANY_MAX_AUTOCOMPLETE_HEIGHT);
	app->tab_pos_editor = utils_get_setting_integer(config, PACKAGE, "tab_pos_editor", GTK_POS_TOP);
	app->tab_pos_msgwin = utils_get_setting_integer(config, PACKAGE, "tab_pos_msgwin",GTK_POS_LEFT);
	app->tab_pos_sidebar = utils_get_setting_integer(config, PACKAGE, "tab_pos_sidebar", GTK_POS_TOP);
	app->sidebar_symbol_visible = utils_get_setting_boolean(config, PACKAGE, "sidebar_symbol_visible", TRUE);
	app->sidebar_openfiles_visible = utils_get_setting_boolean(config, PACKAGE, "sidebar_openfiles_visible", TRUE);
	app->sidebar_visible = utils_get_setting_boolean(config, PACKAGE, "sidebar_visible", TRUE);
	app->statusbar_visible = utils_get_setting_boolean(config, PACKAGE, "statusbar_visible", TRUE);
	app->msgwindow_visible = utils_get_setting_boolean(config, PACKAGE, "msgwindow_visible", TRUE);
	editor_prefs.line_breaking = utils_get_setting_boolean(config, PACKAGE, "line_breaking", FALSE); //default is off for better performance
	editor_prefs.indent_mode = utils_get_setting_integer(config, PACKAGE, "indent_mode", INDENT_ADVANCED);
	app->use_tab_to_indent = utils_get_setting_boolean(config, PACKAGE, "use_tab_to_indent", FALSE);
	editor_prefs.use_indicators = utils_get_setting_boolean(config, PACKAGE, "use_indicators", TRUE);
	editor_prefs.show_indent_guide = utils_get_setting_boolean(config, PACKAGE, "show_indent_guide", FALSE);
	editor_prefs.show_white_space = utils_get_setting_boolean(config, PACKAGE, "show_white_space", FALSE);
	editor_prefs.show_line_endings = utils_get_setting_boolean(config, PACKAGE, "show_line_endings", FALSE);
	editor_prefs.auto_close_xml_tags = utils_get_setting_boolean(config, PACKAGE, "auto_close_xml_tags", TRUE);
	editor_prefs.auto_complete_constructs = utils_get_setting_boolean(config, PACKAGE, "auto_complete_constructs", TRUE);
	editor_prefs.auto_complete_symbols = utils_get_setting_boolean(config, PACKAGE, "auto_complete_symbols", TRUE);
	editor_prefs.folding = utils_get_setting_boolean(config, PACKAGE, "use_folding", TRUE);
	editor_prefs.unfold_all_children = utils_get_setting_boolean(config, PACKAGE, "unfold_all_children", FALSE);
	editor_prefs.show_scrollbars = utils_get_setting_boolean(config, PACKAGE, "show_editor_scrollbars", TRUE);
	app->show_markers_margin = utils_get_setting_boolean(config, PACKAGE, "show_markers_margin", TRUE);
	app->show_linenumber_margin = utils_get_setting_boolean(config, PACKAGE, "show_linenumber_margin", TRUE);
	app->fullscreen = utils_get_setting_boolean(config, PACKAGE, "fullscreen", FALSE);
	app->tab_order_ltr = utils_get_setting_boolean(config, PACKAGE, "tab_order_ltr", TRUE);
	app->show_notebook_tabs = utils_get_setting_boolean(config, PACKAGE, "show_notebook_tabs", TRUE);
	app->brace_match_ltgt = utils_get_setting_boolean(config, PACKAGE, "brace_match_ltgt", FALSE);
	app->switch_msgwin_pages = utils_get_setting_boolean(config, PACKAGE, "switch_msgwin_pages", FALSE);
	app->auto_focus = utils_get_setting_boolean(config, PACKAGE, "auto_focus", FALSE);
	app->custom_date_format = utils_get_setting_string(config, PACKAGE, "custom_date_format", "");
	app->context_action_cmd = utils_get_setting_string(config, PACKAGE, "context_action_cmd", "");
	app->default_open_path = utils_get_setting_string(config, PACKAGE, "default_open_path", "");
	app->custom_commands = g_key_file_get_string_list(config, PACKAGE, "custom_commands", NULL, NULL);
	app->editor_font = utils_get_setting_string(config, PACKAGE, "editor_font", GEANY_DEFAULT_FONT_EDITOR);
	app->tagbar_font = utils_get_setting_string(config, PACKAGE, "tagbar_font", GEANY_DEFAULT_FONT_SYMBOL_LIST);
	app->msgwin_font = utils_get_setting_string(config, PACKAGE, "msgwin_font", GEANY_DEFAULT_FONT_MSG_WINDOW);
	scribble_text = utils_get_setting_string(config, PACKAGE, "scribble_text",
				_("Type here what you want, use it as a notice/scratch board"));

	geo = g_key_file_get_integer_list(config, PACKAGE, "geometry", NULL, &error);
	if (error)
	{
		app->geometry[0] = -1;
		g_error_free(error);
		error = NULL;
	}
	else
	{
		app->geometry[0] = geo[0];
		app->geometry[1] = geo[1];
		app->geometry[2] = geo[2];
		app->geometry[3] = geo[3];
		app->geometry[4] = geo[4];
	}
	hpan_position = utils_get_setting_integer(config, PACKAGE, "treeview_position", 156);
	vpan_position = utils_get_setting_integer(config, PACKAGE, "msgwindow_position", (geo) ?
				(GEANY_MSGWIN_HEIGHT + geo[3] - 440) :
				(GEANY_MSGWIN_HEIGHT + GEANY_WINDOW_DEFAULT_HEIGHT - 440));


	editor_prefs.tab_width = utils_get_setting_integer(config, PACKAGE, "pref_editor_tab_width", 4);
	editor_prefs.use_tabs = utils_get_setting_boolean(config, PACKAGE, "pref_editor_use_tabs", TRUE);
	// use current locale encoding as default for new files (should be in most cases UTF-8)
	g_get_charset(&default_charset);
	tmp_string = utils_get_setting_string(config, PACKAGE, "pref_editor_default_encoding",
																			default_charset);
	if (tmp_string)
	{
		const GeanyEncoding *enc = encodings_get_from_charset(tmp_string);
		if (enc != NULL)
			editor_prefs.default_encoding = enc->idx;
		else
			editor_prefs.default_encoding = GEANY_ENCODING_UTF_8;

		g_free(tmp_string);
	}
	app->pref_main_confirm_exit = utils_get_setting_boolean(config, PACKAGE, "pref_main_confirm_exit", FALSE);
	app->pref_main_suppress_search_dialogs = utils_get_setting_boolean(config, PACKAGE, "pref_main_suppress_search_dialogs", FALSE);
	app->pref_main_load_session = utils_get_setting_boolean(config, PACKAGE, "pref_main_load_session", TRUE);
	app->pref_main_save_winpos = utils_get_setting_boolean(config, PACKAGE, "pref_main_save_winpos", TRUE);
	app->pref_toolbar_show_search = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_search", TRUE);
	app->pref_toolbar_show_goto = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_goto", TRUE);
	app->pref_toolbar_show_zoom = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_zoom", FALSE);
	app->pref_toolbar_show_compile = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_compile", TRUE);
	app->pref_toolbar_show_undo = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_undo", FALSE);
	app->pref_toolbar_show_navigation = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_navigation", TRUE);
	app->pref_toolbar_show_colour = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_colour", TRUE);
	app->pref_toolbar_show_fileops = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_fileops", TRUE);
	app->pref_toolbar_show_quit = utils_get_setting_boolean(config, PACKAGE, "pref_toolbar_show_quit", TRUE);
#ifdef HAVE_VTE
	vte_info.load_vte = utils_get_setting_boolean(config, "VTE", "load_vte", TRUE);
	if (vte_info.load_vte)
	{
		struct passwd *pw = getpwuid(getuid());
		gchar *shell = (pw != NULL) ? pw->pw_shell : "/bin/sh";

		vc = g_new0(VteConfig, 1);
		vte_info.dir = utils_get_setting_string(config, "VTE", "last_dir", NULL);
		if ((vte_info.dir == NULL || utils_str_equal(vte_info.dir, "")) && pw != NULL)
			// last dir is not set, fallback to user's home directory
			vte_info.dir = g_strdup(pw->pw_dir);
		else if (vte_info.dir == NULL && pw == NULL)
			// fallback to root
			vte_info.dir = g_strdup("/");

		vc->emulation = utils_get_setting_string(config, "VTE", "emulation", "xterm");
		vc->shell = utils_get_setting_string(config, "VTE", "shell", shell);
		vc->font = utils_get_setting_string(config, "VTE", "font", "Monospace 10");
		vc->scroll_on_key = utils_get_setting_boolean(config, "VTE", "scroll_on_key", TRUE);
		vc->scroll_on_out = utils_get_setting_boolean(config, "VTE", "scroll_on_out", TRUE);
		vc->ignore_menu_bar_accel = utils_get_setting_boolean(config, "VTE", "ignore_menu_bar_accel", FALSE);
		vc->follow_path = utils_get_setting_boolean(config, "VTE", "follow_path", FALSE);
		vc->run_in_vte = utils_get_setting_boolean(config, "VTE", "run_in_vte", FALSE);
		vc->scrollback_lines = utils_get_setting_integer(config, "VTE", "scrollback_lines", 500);
		vc->colour_fore = g_new0(GdkColor, 1);
		vc->colour_back = g_new0(GdkColor, 1);
		tmp_string = utils_get_setting_string(config, "VTE", "colour_fore", "#ffffff");
		gdk_color_parse(tmp_string, vc->colour_fore);
		g_free(tmp_string);
		tmp_string = utils_get_setting_string(config, "VTE", "colour_back", "#000000");
		gdk_color_parse(tmp_string, vc->colour_back);
		g_free(tmp_string);
	}
#endif
	app->pref_template_developer = utils_get_setting_string(config, PACKAGE, "pref_template_developer", g_get_real_name());
	app->pref_template_company = utils_get_setting_string(config, PACKAGE, "pref_template_company", "");
	tmp_string = utils_get_initials(app->pref_template_developer);
	app->pref_template_initial = utils_get_setting_string(config, PACKAGE, "pref_template_initial", tmp_string);
	g_free(tmp_string);

	app->pref_template_version = utils_get_setting_string(config, PACKAGE, "pref_template_version", "1.0");

	tmp_string2 = utils_get_hostname();
	tmp_string = g_strdup_printf("%s@%s", g_get_user_name(), tmp_string2);
	app->pref_template_mail = utils_get_setting_string(config, PACKAGE, "pref_template_mail", tmp_string);
	g_free(tmp_string);
	g_free(tmp_string2);

	editor_prefs.replace_tabs = utils_get_setting_boolean(config, PACKAGE, "pref_editor_replace_tabs", FALSE);
	editor_prefs.new_line = utils_get_setting_boolean(config, PACKAGE, "pref_editor_new_line", TRUE);
	editor_prefs.trail_space = utils_get_setting_boolean(config, PACKAGE, "pref_editor_trail_space", FALSE);
	editor_prefs.disable_dnd = utils_get_setting_boolean(config, PACKAGE, "pref_editor_disable_dnd", FALSE);

	tmp_string = g_find_program_in_path(GEANY_DEFAULT_TOOLS_MAKE);
	app->tools_make_cmd = utils_get_setting_string(config, "tools", "make_cmd", tmp_string);
	g_free(tmp_string);

	tmp_string = g_find_program_in_path(GEANY_DEFAULT_TOOLS_TERMINAL);
	app->tools_term_cmd = utils_get_setting_string(config, "tools", "term_cmd", tmp_string);
	g_free(tmp_string);

	tmp_string = g_find_program_in_path(GEANY_DEFAULT_TOOLS_BROWSER);
	app->tools_browser_cmd = utils_get_setting_string(config, "tools", "browser_cmd", tmp_string);
	g_free(tmp_string);

	tmp_string2 = g_find_program_in_path(GEANY_DEFAULT_TOOLS_PRINTCMD);
#ifdef G_OS_WIN32
	// single quote paths on Win32 for g_spawn_command_line_async
	tmp_string = g_strconcat("'", tmp_string2, "' '%f'", NULL);
#else
	tmp_string = g_strconcat(tmp_string2, " %f", NULL);
#endif
	app->tools_print_cmd = utils_get_setting_string(config, "tools", "print_cmd", tmp_string);
	g_free(tmp_string);
	g_free(tmp_string2);

	tmp_string = g_find_program_in_path(GEANY_DEFAULT_TOOLS_GREP);
	app->tools_grep_cmd = utils_get_setting_string(config, "tools", "grep_cmd", tmp_string);
	g_free(tmp_string);

	search_prefs.fif_extra_options = utils_get_setting_string(config, "search", "fif_extra_options", "");

	project_load_prefs(config);
	load_file_lists(config);

	g_key_file_free(config);
	g_free(configfile);
	g_free(geo);
	return TRUE;
}


gboolean configuration_open_files()
{
	gint i;
	guint x, pos, y, len;
	gboolean ret = FALSE, failure = FALSE;

	document_delay_colourise();

	i = app->tab_order_ltr ? 0 : (session_files->len - 1);
	while (TRUE)
	{
		gchar *tmp = g_ptr_array_index(session_files, i);

		if (tmp && *tmp)
		{
			const gchar *file;
			gchar *locale_filename, **array;
			gint uid = -1;
			x = 0;
			y = 0;

			// yes it is :, it should be a ;, but now it is too late to change it
			array = g_strsplit(tmp, ":", 3);
			len = g_strv_length(array);

			// read position
			if (len > 0 && array[0]) pos = atoi(array[0]);
			else pos = 0;

			// read filetype (only if there are more than two fields, otherwise we have the old format)
			if (len > 2 && array[1])
			{
				uid = atoi(array[1]);
				file = array[2];
			}
			else file = array[1];

			// try to get the locale equivalent for the filename, fallback to filename if error
			locale_filename = utils_get_locale_from_utf8(file);
			g_strfreev(array);

			if (g_file_test(locale_filename, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK))
			{
				filetype *ft = filetypes_get_from_uid(uid);
				document_open_file(-1, locale_filename, pos, FALSE, ft, NULL);
				ret = TRUE;
			}
			else
			{
				failure = TRUE;
				geany_debug("Could not find file '%s'.", file);
			}

			g_free(locale_filename);
		}
		g_free(tmp);

		if (app->tab_order_ltr)
		{
			i++;
			if (i >= (gint)session_files->len) break;
		}
		else
		{
			i--;
			if (i < 0) break;
		}
	}
	document_colourise_new();

	g_ptr_array_free(session_files, TRUE);
	if (failure)
		msgwin_status_add(_("Failed to load one or more session files."));
	return ret;
}


/* set some settings which are already read from the config file, but need other things, like the
 * realisation of the main window */
void configuration_apply_settings()
{
	if (scribble_text)
	{	// update the scribble widget, because now it's realized
		gtk_text_buffer_set_text(
				gtk_text_view_get_buffer(GTK_TEXT_VIEW(lookup_widget(app->window, "textview_scribble"))),
				scribble_text, -1);
	}
	g_free(scribble_text);

	// set the position of the hpaned and vpaned
	if (app->pref_main_save_winpos)
	{
		gtk_paned_set_position(GTK_PANED(lookup_widget(app->window, "hpaned1")), hpan_position);
		gtk_paned_set_position(GTK_PANED(lookup_widget(app->window, "vpaned1")), vpan_position);
	}

	// set fullscreen after initial draw so that returning to normal view is the right size.
	// fullscreen mode is disabled by default, so act only if it is true
	if (app->fullscreen)
	{
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(lookup_widget(app->window, "menu_fullscreen1")), TRUE);
		app->fullscreen = TRUE;
		ui_set_fullscreen();
	}
}


#ifdef GEANY_DEBUG
// Geany data file generation is only available with a debug build of Geany.

static void generate_filetype_extensions(const gchar *output_dir);


/* Generate the config files in "data/" from defaults */
void configuration_generate_data_files()
{
	gchar *cur_dir, *gen_dir;

	cur_dir = g_get_current_dir();
	gen_dir = g_strconcat(cur_dir, G_DIR_SEPARATOR_S, "data", NULL);
	g_free(cur_dir);

	if (! g_file_test(gen_dir, G_FILE_TEST_IS_DIR))
	{
		g_print("%s does not exist!\n", gen_dir);
		return;
	}
	g_print("Generating system files in %s:\n", gen_dir);
	// currently only filetype extensions are auto-generated.
	generate_filetype_extensions(gen_dir);
	g_free(gen_dir);
}


/* This will write the default settings for the system filetype_extensions.conf */
static void generate_filetype_extensions(const gchar *output_dir)
{
	guint i;
	gchar *configfile = g_strconcat(output_dir, G_DIR_SEPARATOR_S, "filetype_extensions.conf", NULL);
	gchar *data, *basename;
	GKeyFile *config;

	config = g_key_file_new();
	// add filetype keys
	for (i = 0; i < GEANY_MAX_FILE_TYPES; i++)
	{
		g_key_file_set_string_list(config, "Extensions", filetypes[i]->name,
			(const gchar**) filetypes[i]->pattern, g_strv_length(filetypes[i]->pattern));
	}
	// add comment
	g_key_file_set_comment(config, "Extensions", NULL,
		"Filetype extension configuration file for Geany\n"
		"Insert as many items as you want, seperate them with a \";\".\n"
		"See Geany's main documentation for details.", NULL);

	// write the file
	g_print("%s: ", __func__);
	data = g_key_file_to_data(config, NULL, NULL);
	basename = g_path_get_basename(configfile);

	if (utils_write_file(configfile, data) == 0)
		g_print("wrote file %s.\n", basename);
	else
		g_print("*** ERROR: error writing file %s\n", basename);
	g_free(basename);

	g_free(data);
	g_key_file_free(config);
}

#endif


void configuration_read_filetype_extensions()
{
	guint i;
	gsize len = 0;
	gchar *sysconfigfile = g_strconcat(app->datadir, G_DIR_SEPARATOR_S,
		"filetype_extensions.conf", NULL);
	gchar *userconfigfile = g_strconcat(app->configdir, G_DIR_SEPARATOR_S,
		"filetype_extensions.conf", NULL);
	gchar **list;
	GKeyFile *sysconfig = g_key_file_new();
	GKeyFile *userconfig = g_key_file_new();

	g_key_file_load_from_file(sysconfig, sysconfigfile, G_KEY_FILE_NONE, NULL);
	g_key_file_load_from_file(userconfig, userconfigfile, G_KEY_FILE_NONE, NULL);

	// read the keys
	for (i = 0; i < GEANY_MAX_FILE_TYPES; i++)
	{
		gboolean userset =
			g_key_file_has_key(userconfig, "Extensions", filetypes[i]->name, NULL);
		list = g_key_file_get_string_list(
			(userset) ? userconfig : sysconfig, "Extensions", filetypes[i]->name, &len, NULL);
		if (list && len > 0)
		{
			g_strfreev(filetypes[i]->pattern);
			filetypes[i]->pattern = list;
		}
		else g_strfreev(list);
	}

	g_free(sysconfigfile);
	g_free(userconfigfile);
	g_key_file_free(sysconfig);
	g_key_file_free(userconfig);
}


void configuration_read_autocompletions()
{
	gsize i, j, len = 0, len_keys = 0;
	gchar *sysconfigfile = g_strconcat(app->datadir, G_DIR_SEPARATOR_S,
		"autocomplete.conf", NULL);
	gchar *userconfigfile = g_strconcat(app->configdir, G_DIR_SEPARATOR_S,
		"autocomplete.conf", NULL);
	gchar **groups_user, **groups_sys;
	gchar **keys_user, **keys_sys;
	gchar *value;
	GKeyFile *sysconfig = g_key_file_new();
	GKeyFile *userconfig = g_key_file_new();
	GHashTable *tmp;

	g_key_file_load_from_file(sysconfig, sysconfigfile, G_KEY_FILE_NONE, NULL);
	g_key_file_load_from_file(userconfig, userconfigfile, G_KEY_FILE_NONE, NULL);

	// keys are strings, values are GHashTables, so use g_free and g_hash_table_destroy
	editor_prefs.auto_completions =
		g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_destroy);

	// first read all globally defined auto completions
	groups_sys = g_key_file_get_groups(sysconfig, &len);
	for (i = 0; i < len; i++)
	{
		keys_sys = g_key_file_get_keys(sysconfig, groups_sys[i], &len_keys, NULL);
		// create new hash table for the read section (=> filetype)
		tmp = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
		g_hash_table_insert(editor_prefs.auto_completions, g_strdup(groups_sys[i]), tmp);

		for (j = 0; j < len_keys; j++)
		{
			g_hash_table_insert(tmp, g_strdup(keys_sys[j]),
						utils_get_setting_string(sysconfig, groups_sys[i], keys_sys[j], ""));
		}
		g_strfreev(keys_sys);
	}

	// now read defined completions in user's configuration directory and add / replace them
	groups_user = g_key_file_get_groups(userconfig, &len);
	for (i = 0; i < len; i++)
	{
		keys_user = g_key_file_get_keys(userconfig, groups_user[i], &len_keys, NULL);

		tmp = g_hash_table_lookup(editor_prefs.auto_completions, groups_user[i]);
		if (tmp == NULL)
		{	// new key found, create hash table
			tmp = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
			g_hash_table_insert(editor_prefs.auto_completions, g_strdup(groups_user[i]), tmp);
		}
		for (j = 0; j < len_keys; j++)
		{
			value = g_hash_table_lookup(tmp, keys_user[j]);
			if (value == NULL)
			{	// value = NULL means the key doesn't yet exist, so insert
				g_hash_table_insert(tmp, g_strdup(keys_user[j]),
						utils_get_setting_string(userconfig, groups_user[i], keys_user[j], ""));
			}
			else
			{	// old key and value will be freed by destroy function (g_free)
				g_hash_table_replace(tmp, g_strdup(keys_user[j]),
						utils_get_setting_string(userconfig, groups_user[i], keys_user[j], ""));
			}
		}
		g_strfreev(keys_user);
	}

	g_free(sysconfigfile);
	g_free(userconfigfile);
	g_strfreev(groups_sys);
	g_strfreev(groups_user);
	g_key_file_free(sysconfig);
	g_key_file_free(userconfig);
}
