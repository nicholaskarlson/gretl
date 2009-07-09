/* 
 *  gretl -- Gnu Regression, Econometrics and Time-series Library
 *  Copyright (C) 2001 Allin Cottrell and Riccardo "Jack" Lucchetti
 * 
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifndef GUI_UTILS_H
#define GUI_UTILS_H

int copyfile (const char *src, const char *dest);

FILE *gretl_tempfile_open (char *fname);

int gretl_tempname (char *fname);

void delete_widget (GtkWidget *widget, gpointer data);

void *mymalloc (size_t size); 

void *myrealloc (void *ptr, size_t size);

void nomem (void);

void mark_dataset_as_modified (void);

void register_data (int flag);

void register_startup_data (const char *fname);

void do_open_data (windata_t *vwin, int code);

void verify_open_data (windata_t *vwin, int code);

void verify_open_session (void);

void free_windata (GtkWidget *w, gpointer data);

void mark_vwin_content_saved (windata_t *vwin);

void mark_vwin_content_changed (windata_t *vwin);

void vwin_set_filename (windata_t *vwin, const char *fname);

int latex_is_ok (void);

gint query_save_text (GtkWidget *w, GdkEvent *event, windata_t *vwin);

void auto_save_plot (windata_t *vwin);

void view_window_save (GtkWidget *widget, windata_t *vwin);

void buf_edit_save (GtkWidget *widget, windata_t *vwin);

windata_t *view_buffer (PRN *prn, int hsize, int vsize, 
			const char *title, int role,
			gpointer data);

windata_t *view_file (const char *filename, int editable, int del_file, 
		      int hsize, int vsize, int role);

windata_t *
view_help_file (const char *filename, int role, GtkActionEntry *menu_items,
		const gchar *ui_info);

windata_t *edit_buffer (char **pbuf, int hsize, int vsize, 
			char *title, int role);

windata_t *vwin_first_child (windata_t *vwin);

int view_model (PRN *prn, MODEL *pmod, int hsize, int vsize, 
		char *title);

void view_window_set_editable (windata_t *vwin);

int gui_validate_varname (const char *varname, GretlType t);

gretlopt get_tex_eqn_opt (void);

gint popup_menu_handler (GtkWidget *widget, GdkEvent *event,
			 gpointer data);

void add_popup_item (const gchar *label, GtkWidget *menu,
		     GCallback callback, gpointer data);

void *gui_get_plugin_function (const char *funcname, 
			       void **phandle);

int get_imported_data (char *fname, int ftype, int append);

char *double_underscores (char *targ, const char *src);

void verbose_gerror_report (GError *gerr, const char *src);

int gretl_file_get_contents (const gchar *fname, gchar **contents);

void start_R (const char *buf, int send_data, int interactive);

void run_ox_script (gchar *buf);

const char *print_today (void);

#ifndef G_OS_WIN32
int browser_open (const char *url);
#endif

#if defined(HAVE_FLITE) || defined(G_OS_WIN32)
enum {
    AUDIO_TEXT = 0,
    AUDIO_LISTBOX
} audio_render_keys;

void audio_render_window (windata_t *vwin, int key);
void stop_talking (void);
#endif

#endif /* GUI_UTILS_H */
