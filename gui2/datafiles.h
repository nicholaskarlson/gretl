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

/* datafiles.h for gretl */

#ifndef DATAFILES_H
#define DATAFILES_H

enum {
    VIEW_FN_PKG_INFO,
    VIEW_FN_PKG_CODE,
    VIEW_FN_PKG_SAMPLE,
    EDIT_FN_PKG,
    DELETE_FN_PKG,
    CALL_FN_PKG,
    MENU_ADD_FN_PKG,
    MENU_REMOVE_FN_PKG,
    VIEW_PKG_RESOURCES,
    VIEW_PKG_DOC
};

void browser_open_data (GtkWidget *w, gpointer data);

void browser_open_ps (GtkWidget *w, gpointer data);

void browser_edit_func (GtkWidget *w, gpointer data);

void browser_call_func (GtkWidget *w, gpointer data);

void destroy_file_collections (void);

void show_files (GtkAction *action, gpointer p);

void display_files (int role, gpointer p);

void show_native_dbs (void);

gint populate_filelist (windata_t *fdata, gpointer p);

char *strip_extension (char *s);

char *maybe_ellipsize_string (char *s, int maxlen);

windata_t *display_function_package_data (const char *pkgname, 
					  const char *path,
					  int role);

void maybe_update_gfn_browser (const char *pkgname,
			       const char *version,
			       const char *date,
			       const char *descrip,
			       const char *fname,
			       int uses_subdir,
			       int pdfdoc);

void maybe_update_pkg_registry_window (const char *pkgname,
				       int code);

void set_alternate_gfn_dir (windata_t *vwin, char *path);

gchar *gfn_browser_get_alt_path (void);

void start_new_function_package (GtkWidget *w, gpointer data);

windata_t *get_local_viewer (int remote_role);

void listbox_select_first (windata_t *vwin);

#endif /* DATAFILES_H */
