/*
 *  Copyright (c) by Allin Cottrell
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.
 *
 */

#include "gretl.h"
#include "treeutils.h"
#include "ssheet.h"
#include <errno.h>
#include <ctype.h>
#include <float.h>

#define SSDEBUG

typedef struct {
    GtkWidget *view;
    GtkWidget *win;
    GtkWidget *locator;
    GtkWidget *popup;
    GtkCellRenderer *dumbcell;
    GtkCellRenderer *datacell;
    gchar location[20];
    gint datacols, datarows;
    gint padcols, totcols;
    gint n_scalars;
    guint cid;
    guint point;
} spreadsheet;

enum {
    SHEET_AT_END,
    SHEET_AT_POINT
};

static void sheet_add_var_callback (gpointer data, guint code, GtkWidget *w);
static void sheet_add_obs_callback (gpointer data, guint where, GtkWidget *w);
static void get_data_from_sheet (GtkWidget *w, spreadsheet *sheet);
static void set_up_sheet_column (GtkTreeViewColumn *column, gint width);
static gint get_data_col_width (void);
static int add_data_column (spreadsheet *sheet);

static GtkItemFactoryEntry sheet_items[] = {
    { N_("/_Observation"), NULL, NULL, 0, "<Branch>", GNULL },
    { N_("/Observation/_Append obs"), NULL, sheet_add_obs_callback, SHEET_AT_END, 
      NULL, GNULL },
    { N_("/Observation/_Insert obs"), NULL, sheet_add_obs_callback, SHEET_AT_POINT, 
      NULL, GNULL },
    { N_("/Add _Variable"), NULL, sheet_add_var_callback, 0, NULL, GNULL }
};

static int sheet_modified;

/* .................................................................. */

static char *single_underscores (char *targ, const char *src)
{
    char *p = targ;

    while (*src) {
	if (*src == '_' && *(src + 1) == '_') {
	    src++;
	    *p++ = '_';
	} else {
	    *p++ = *src;
	}
	src++;
    }
    *p = '\0';

    return targ;
}

/* .................................................................. */

static void set_locator_label (spreadsheet *sheet, GtkTreePath *path,
			       GtkTreeViewColumn *column)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(sheet->view));
    GtkTreeIter iter;
    gchar *row_label;
    const gchar *col_label;
    char tmp[9];

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, 0, &row_label, -1);
    col_label = gtk_tree_view_column_get_title(column);
    single_underscores(tmp, col_label);
    sprintf(sheet->location, "%s, %s", tmp, row_label);
    gtk_statusbar_pop(GTK_STATUSBAR(sheet->locator), sheet->cid);
    gtk_statusbar_push(GTK_STATUSBAR(sheet->locator), 
		       sheet->cid, sheet->location);
    g_free(row_label);
}

/* .................................................................. */

static void move_to_next_cell (spreadsheet *sheet, GtkTreePath *path,
			       GtkTreeViewColumn *column)
{
    GtkTreeView *view = GTK_TREE_VIEW(sheet->view);
    gint nextrow;

    nextrow = gtk_tree_path_get_indices(path)[0] + 1;

    if (nextrow < sheet->datarows) {
	GtkTreePath *newpath;
	gchar pstr[8];

	sprintf(pstr, "%d", nextrow);
	newpath = gtk_tree_path_new_from_string(pstr);
	if (newpath != NULL) {
	    gtk_tree_view_set_cursor(view, newpath, column, FALSE);
	    set_locator_label(sheet, newpath, column);
	    gtk_tree_path_free(newpath);
	}
    }
}

/* .................................................................. */

static gint sheet_cell_edited (GtkCellRendererText *cell,
			       const gchar *path_string,
			       const gchar *new_text,
			       spreadsheet *sheet)
{
    if (check_atof(new_text)) {
	errbox(get_gretl_errmsg());
    } else {
	GtkTreeView *view = GTK_TREE_VIEW(sheet->view);
	GtkTreeModel *model = gtk_tree_view_get_model(view);
	GtkTreeViewColumn *column;
	GtkTreePath *path;
	GtkTreeIter iter;
	gchar *old_text;
	gint colnum;

	gtk_tree_view_get_cursor(view, NULL, &column);
	colnum = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "colnum"));
	path = gtk_tree_path_new_from_string (path_string);
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, colnum, &old_text, -1);

	if (strcmp(old_text, new_text)) {
	    gtk_list_store_set(GTK_LIST_STORE(model), &iter, 
			       colnum, new_text, -1);
	    sheet_modified = 1;
	}
	move_to_next_cell(sheet, path, column);
	gtk_tree_path_free(path);
	g_free(old_text);
    }

    return FALSE;
}

/* .................................................................. */

static int real_add_new_var (spreadsheet *sheet, const char *varname)
{
    GtkTreeViewColumn *column;
    GList *collist = NULL, *clstart = NULL;
    gint i, oldcols, cols;
    char tmp[16];

    oldcols = sheet->totcols;

#ifdef SSDEBUG
    fprintf(stderr, "real_add_new_var: sheet->totcols=%d\n", oldcols);
#endif

    if (add_data_column(sheet)) return 1;

#ifdef SSDEBUG
    fprintf(stderr, "real_add_new_var: now: sheet->totcols=%d, oldcols=%d\n", 
	    sheet->totcols, oldcols);
#endif

    if (sheet->totcols == oldcols) {
	/* remove a padding column from the tree view */
	column = gtk_tree_view_get_column(GTK_TREE_VIEW(sheet->view), 
					  sheet->datacols);
	gtk_tree_view_remove_column(GTK_TREE_VIEW(sheet->view), column);
    }

    column = gtk_tree_view_column_new();
    double_underscores(tmp, varname);
    gtk_tree_view_column_set_title(column, tmp);
    set_up_sheet_column(column, get_data_col_width());

    cols = gtk_tree_view_insert_column(GTK_TREE_VIEW(sheet->view), 
				       column, sheet->datacols);
#ifdef SSDEBUG
    fprintf(stderr, "inserted new tree view col at pos %d, cols now = %d\n", 
	    sheet->datacols, cols);
#endif
	
    clstart = collist = gtk_tree_view_get_columns(GTK_TREE_VIEW(sheet->view));

    i = 0;
    while (collist != NULL && i < sheet->totcols - 2) {
	column = GTK_TREE_VIEW_COLUMN(collist->data);

#ifdef SSDEBUG
	fprintf(stderr, "setting up new tree view, column %d\n", i);
#endif	

	if (i < sheet->datacols) {
	    gint editcol = (i > 0)? sheet->totcols - 1 : sheet->totcols - 2;

	    gtk_tree_view_column_set_attributes (column, 
						 sheet->datacell,
						 "text", i,
						 "editable", editcol,
						 NULL);
	} else {	    
	    gtk_tree_view_column_clear(column);
	}     

	if (i == sheet->datacols) {
	    gtk_tree_view_column_pack_start(column, sheet->datacell, TRUE);
	    gtk_tree_view_column_set_attributes (column,
						 sheet->datacell,
						 "text", i, 
						 "editable", sheet->totcols - 1, 
						 NULL);
	    g_object_set_data(G_OBJECT(column), "colnum", GINT_TO_POINTER(i));
	} 
	else if (i > sheet->datacols) {
	    gtk_tree_view_column_pack_start(column, sheet->dumbcell, TRUE);
	    gtk_tree_view_column_set_attributes (column,
						 sheet->dumbcell,
						 "text", i, 
						 "editable", sheet->totcols - 2,
						 NULL);
	    g_object_set_data(G_OBJECT(column), "colnum", GINT_TO_POINTER(0));
	}

	collist = collist->next;
	i++;
    }

    if (clstart) g_list_free(clstart);

    sheet_modified = 1;

    return 0;
}

/* .................................................................. */

static void real_add_new_obs (spreadsheet *sheet, const char *obsname)
{
    GtkTreeView *view = GTK_TREE_VIEW(sheet->view);
    gint pointpath = 0;
    GtkListStore *store;
    GtkTreeIter iter;
    gchar rowlabel[10];
    gint i;

    store = GTK_LIST_STORE(gtk_tree_view_get_model(view));

    if (sheet->point == SHEET_AT_END) {
	gtk_list_store_append(store, &iter);
    } 
    else if (sheet->point == SHEET_AT_POINT) {
	GtkTreePath *path;
	GtkTreeViewColumn *column;

	gtk_tree_view_get_cursor(view, &path, &column);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, path);
	pointpath = gtk_tree_path_get_indices(path)[0];
	gtk_list_store_insert(store, &iter, pointpath);
	gtk_tree_path_free(path);
    } 
    else return;

    sheet->datarows += 1;

    if (datainfo->markers) {
	gtk_list_store_set(store, &iter, 0, obsname, -1);
    } else if (sheet->point == SHEET_AT_END) {
	ntodate(rowlabel, sheet->datarows - 1, datainfo);
	gtk_list_store_set(store, &iter, 0, rowlabel, -1);
    }

    for (i=1; i<=sheet->datacols; i++) {
	gtk_list_store_set(store, &iter, i, "", -1);
    }

    for (i=1; i<=sheet->padcols; i++) {
	gtk_list_store_set(store, &iter, sheet->datacols + i, "", -1);
    }

    gtk_list_store_set(store, &iter, 
		       sheet->totcols - 2, FALSE, 
		       sheet->totcols - 1, TRUE,
		       -1);

    if (sheet->point == SHEET_AT_POINT && !datainfo->markers) {
	for (i=pointpath; i<sheet->datarows; i++) {
	    ntodate(rowlabel, i, datainfo);
	    gtk_list_store_set(store, &iter, 0, rowlabel, -1);
	    gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}
    } 

    if (sheet->point == SHEET_AT_END) {
	GtkTreePath *path;
	GtkTreeViewColumn *column;
	GtkAdjustment *adj;
	gchar *pathstr;
	gdouble adjval;

	pathstr = g_strdup_printf("%d", sheet->datarows - 1);
	path = gtk_tree_path_new_from_string(pathstr);
	column = gtk_tree_view_get_column(view, 1);
	gtk_tree_view_set_cursor(view, path, column, FALSE);
	adj = 
	    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(gtk_widget_get_ancestor
								    (GTK_WIDGET(view),
								     GTK_TYPE_BIN)));
	adjval = gtk_adjustment_get_value(adj);
	gtk_adjustment_set_value (adj, adjval + 30); /* why is this hack needed? */
	gtk_tree_path_free(path);
	g_free(pathstr);
    }

    sheet_modified = 1;
}

/* ........................................................... */

static void name_new_var (GtkWidget *widget, dialog_t *ddata) 
{
    spreadsheet *sheet = (spreadsheet *) ddata->data;
    const gchar *buf;
    char varname[9];

    buf = gtk_entry_get_text (GTK_ENTRY (ddata->edit));

    if (blank_entry(buf, ddata)) return;
    if (validate_varname(buf)) return;

    *varname = 0;
    strncat(varname, buf, 8);

    close_dialog(ddata);

    if (real_add_new_var(sheet, varname)) {
	errbox(_("Out of memory attempting to add variable"));
    }
}

/* ........................................................... */

static void name_new_obs (GtkWidget *widget, dialog_t *ddata) 
{
    spreadsheet *sheet = (spreadsheet *) ddata->data;
    const gchar *buf;
    char obsmarker[9];

    buf = gtk_entry_get_text (GTK_ENTRY (ddata->edit));
    if (blank_entry(buf, ddata)) return;

    *obsmarker = 0;
    strncat(obsmarker, buf, 8);

    close_dialog(ddata);
    real_add_new_obs(sheet, obsmarker);
}

/* ........................................................... */

static void name_var_dialog (spreadsheet *sheet) 
{
    edit_dialog (_("gretl: name variable"), 
		 _("Enter name for new variable\n"
		   "(max. 8 characters)"),
		 NULL, name_new_var, sheet, 
		 0, 0);
}

/* ........................................................... */

static void new_case_dialog (spreadsheet *sheet) 
{
    edit_dialog (_("gretl: case marker"), 
		 _("Enter case marker for new obs\n"
		   "(max. 8 characters)"),
		 NULL, name_new_obs, sheet, 
		 0, 0);
}

/* ........................................................... */

static int add_data_column (spreadsheet *sheet)
{
    GType *types;
    GtkListStore *old_store, *new_store;
    GtkTreeIter old_iter, new_iter;
    gint i, row, newcol;
    int totcols = sheet->totcols;
    int padcols = sheet->padcols;

    /* This is relatively complex because, so far as I can tell, you can't
       append or insert additional columns in a GtkListStore: we have to
       create a whole new liststore and copy the old info across.
    */

#ifdef SSDEBUG
    fprintf(stderr, "add_data_column: totcols=%d, padcols=%d\n", 
	    totcols, padcols);
#endif    

    if (padcols > 0) padcols--;
    else totcols++;

    /* make an expanded column types list */
    types = mymalloc(totcols * sizeof *types);
    if (types == NULL) {
	return 1;
    }

    sheet->datacols += 1;
    sheet->totcols = totcols;
    sheet->padcols = padcols;

#ifdef SSDEBUG
    fprintf(stderr, "add_data_column: now sheet->totcols=%d, sheet->padcols=%d,"
	    " sheet->datacols=%d\n", sheet->totcols, sheet->padcols, sheet->datacols);
#endif 

    /* configure the types */
    for (i=0; i<sheet->totcols; i++) {
	if (i >= sheet->totcols - 2) types[i] = G_TYPE_BOOLEAN;
	else types[i] = G_TYPE_STRING;
    }

    newcol = sheet->datacols;

    old_store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sheet->view)));
    new_store = gtk_list_store_newv(sheet->totcols, types);
    free(types);

    /* go to start of old and new lists */
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(old_store), &old_iter);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(new_store), &new_iter);

    /* construct the new table */
    for (row=0; row<sheet->datarows; row++) {
	int col;
	gchar *str;

	gtk_list_store_append(new_store, &new_iter);

	for (col=0; col<sheet->totcols; col++) {
	    if (col < newcol) {
		/* copy labels and original data */
		gtk_tree_model_get(GTK_TREE_MODEL(old_store), &old_iter, col, &str, -1);
		gtk_list_store_set(new_store, &new_iter, col, str, -1);
		g_free(str);
	    }
	    else if (col >= newcol && col <= newcol + sheet->padcols) {
		/* new data values (and any padding) set blank */
		gtk_list_store_set(new_store, &new_iter, col, "", -1);
	    }
	    else {
		/* editable flags at end of row */
		gboolean editable = (col == sheet->totcols - 1);

		gtk_list_store_set(new_store, &new_iter, col, editable, -1);
	    }
	}
	gtk_tree_model_iter_next(GTK_TREE_MODEL(old_store), &old_iter);
    }    

    gtk_tree_view_set_model(GTK_TREE_VIEW(sheet->view), GTK_TREE_MODEL(new_store));
    g_object_unref(G_OBJECT(new_store));

    return 0;
}

/* ........................................................... */

static void sheet_add_var_callback (gpointer data, guint u, GtkWidget *w)
{
    spreadsheet *sheet = (spreadsheet *) data;

    name_var_dialog(sheet);
}

/* ........................................................... */

static void sheet_add_obs_callback (gpointer data, guint where, GtkWidget *w)
{
    spreadsheet *sheet = (spreadsheet *) data;

    sheet->point = where;

    if (datainfo->markers) {
	new_case_dialog(sheet);
    } else {
	real_add_new_obs(sheet, "");
    }
}

/* ........................................................... */

static void popup_sheet_add_obs (GtkWidget *w, spreadsheet *sheet)
{
    sheet_add_obs_callback(sheet, SHEET_AT_END, NULL);
}

static void popup_sheet_insert_obs (GtkWidget *w, spreadsheet *sheet)
{
    sheet_add_obs_callback(sheet, SHEET_AT_POINT, NULL);
}

static void popup_sheet_add_var (GtkWidget *w, spreadsheet *sheet)
{
    sheet_add_var_callback(sheet, 0, NULL);
}

/* ........................................................... */

static void build_sheet_popup (GtkWidget **popup, spreadsheet *sheet)
{
    if (*popup != NULL) return;

    *popup = gtk_menu_new();

    add_popup_item(_("Add Variable"), *popup, 
		   G_CALLBACK(popup_sheet_add_var),
		   sheet);
    add_popup_item(_("Add Observation"), *popup,
		   G_CALLBACK(popup_sheet_add_obs),
		   sheet);
    add_popup_item(_("Insert Observation"), *popup,
		   G_CALLBACK(popup_sheet_insert_obs),
		   sheet);
}

/* ......................................................... */

static gboolean update_cell_position (GtkTreeView *view, spreadsheet *sheet)
{
    GtkTreePath *path;
    GtkTreeViewColumn *column;
    static gint oldrow, oldcol;

    /* this is connected to the "cursor-changed" signal */

#ifdef SSDEBUG
    fprintf(stderr, "update_cell_position:\n");
#endif

    gtk_tree_view_get_cursor(view, &path, &column);

    if (path && column) {
	gint newrow = gtk_tree_path_get_indices(path)[0];
	gint newcol = 
	    GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "colnum"));

	if (newrow != oldrow || newcol != oldcol) {
#ifdef SSDEBUG
	    fprintf(stderr, " activating cell(%d, %d)\n", newrow, newcol);
#endif
	    set_locator_label(sheet, path, column);
	    oldrow = newrow;
	    oldcol = newcol;
	    gtk_tree_view_set_cursor(view, path, column, 
				     FALSE); /* start editing? */
	} else {
#ifdef SSDEBUG
	   fprintf(stderr, " still in cell(%d, %d)\n", oldrow, oldcol); 
#endif
	}
	gtk_tree_path_free(path);
    }

    return TRUE;
}

/* ........................................................... */

static void get_data_from_sheet (GtkWidget *w, spreadsheet *sheet)
{
    gint i, t, n = datainfo->n, oldv = datainfo->v; 
    gint orig_cols, newvars, newobs, missobs = 0;
    GtkTreeView *view = GTK_TREE_VIEW(sheet->view);
    GtkTreeIter iter;
    GtkTreeViewColumn *column;
    GtkTreeModel *model;
    gint colnum;

    newobs = sheet->datarows - n;
    orig_cols = datainfo->v - 1 - sheet->n_scalars;
    newvars = sheet->datacols - orig_cols;

    model = gtk_tree_view_get_model(view);

    if (newobs > 0) {
	if (grow_nobs(newobs, &Z, datainfo)) {
	    errbox(_("Failed to allocate memory for new data"));
	    return;
	}
	n = datainfo->n;
    }

    if (newvars > 0) {
	if (dataset_add_vars(newvars, &Z, datainfo)) {
	    errbox(_("Failed to allocate memory for new data"));
	    return;
	}
	for (i=0; i<newvars; i++) { 
	    const gchar *newname;

	    column = gtk_tree_view_get_column(view, orig_cols + 1 + i);
	    newname = gtk_tree_view_column_get_title(column);
	    strcpy(datainfo->varname[i + oldv], newname);
	    strcpy(VARLABEL(datainfo, i + oldv), "");
	}
    }

    colnum = 0;
    for (i=1; i<datainfo->v; i++) {
	if (datainfo->vector[i] == 0) continue;
	colnum++;
	gtk_tree_model_get_iter_first(model, &iter);	
	for (t=0; t<n; t++) {
	    gchar *numstr;

	    gtk_tree_model_get(model, &iter, colnum, &numstr, -1);
	    if (*numstr) {
		Z[i][t] = atof(numstr); 
	    } else {
		Z[i][t] = NADBL;
		missobs = 1;
	    }
	    g_free(numstr);
	    gtk_tree_model_iter_next(model, &iter);
	}
    }

    if (datainfo->markers && datainfo->S != NULL) {
	gtk_tree_model_get_iter_first(model, &iter);
	for (t=0; t<n; t++) {
	    gchar *marker;

	    gtk_tree_model_get(model, &iter, 0, &marker, -1);
	    strcpy(datainfo->S[t], marker);
	    g_free(marker);
	    gtk_tree_model_iter_next(model, &iter);
	}
    }

    data_status |= (GUI_DATA | MODIFIED_DATA);
    register_data(NULL, NULL, 0);

    if (missobs) {
	infobox(_("Warning: there were missing observations"));
    } else {
	infobox(_("Data updated OK"));
    }

    sheet_modified = 0;
}

/* ........................................................... */

static void select_first_editable_cell (spreadsheet *sheet)
{
    GtkTreeView *view = GTK_TREE_VIEW(sheet->view);
    GtkTreePath *path;
    GtkTreeViewColumn *column;

    path = gtk_tree_path_new_from_string("0");
    column = gtk_tree_view_get_column(view, 1);
    gtk_tree_view_set_cursor(view, path, column, FALSE);
    set_locator_label(sheet, path, column);

    gtk_tree_path_free(path);
}

/* ........................................................... */

static int add_data_to_sheet (spreadsheet *sheet, int new)
{
    gchar rowlabel[10];
    gint i, t, colnum, n = datainfo->n;
    GtkTreeView *view = GTK_TREE_VIEW(sheet->view);
    GtkTreeIter iter;
    GtkListStore *store;

    store = GTK_LIST_STORE(gtk_tree_view_get_model(view));

    /* insert observation markers */
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    for (t=0; t<n; t++) {
	if (!new && datainfo->markers) {
	    strcpy(rowlabel, datainfo->S[t]);
	} else {
	    ntodate(rowlabel, t, datainfo);
	}
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, rowlabel, -1);
    }

    sheet->datarows = t;

    /* insert data values */
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    for (t=0; t<n; t++) {
	if (new) {
	    /* insert "missing" values */
	    for (i=1; i<datainfo->v; i++) {
		gtk_list_store_set(store, &iter, i, "", -1);
	    }
	} else {	
	    /* insert actual values */
	    char numstr[32];

	    colnum = 0;
	    for (i=1; i<datainfo->v; i++) {
		/* don't put scalars into the spreadsheet */
		if (datainfo->vector[i] == 0) continue;
		if (na(Z[i][t])) {
		    *numstr = '\0';
		} else {
		    sprintf(numstr, "%.*g", DBL_DIG, Z[i][t]);
		}
		colnum++;
		gtk_list_store_set(store, &iter, colnum, numstr, -1);
	    }
	}
	gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }

    /* insert padding cols if needed */
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    for (t=0; t<n; t++) {
	for (i=0; i<sheet->padcols; i++) {
	    gtk_list_store_set(store, &iter, i + datainfo->v, "", -1);
	}
	gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }

    /* set the editable flags */
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    for (t=0; t<n; t++) {
	gtk_list_store_set(store, &iter, 
			   sheet->totcols - 2, FALSE,
			   sheet->totcols - 1, TRUE, -1);
	gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }

    return 0;
}

/* ........................................................... */

static gint get_string_width (const gchar *str)
{
    gint width;

    GtkWidget *w;
    PangoLayout *pl;
    PangoContext *pc;

    w = gtk_label_new(NULL);
    pc = gtk_widget_get_pango_context(w);

    pl = pango_layout_new(pc);
    pango_layout_set_text(pl, str, -1);
    pango_layout_get_pixel_size(pl, &width, NULL);

    gtk_widget_destroy(w);
    g_object_unref(G_OBJECT(pl));

    return width;
}

static gint get_obs_col_width (void)
{
    static gint width;

    if (width == 0) width = get_string_width("XXXXXXXXX");
    return width;
}

static gint get_data_col_width (void)
{
    static gint width;

    if (width == 0) width = get_string_width("-00.000000");
    return width;
}

/* ........................................................... */

static void set_up_sheet_column (GtkTreeViewColumn *column, gint width)
{
    gtk_tree_view_column_set_alignment(column, 0.5); /* header centered */
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_column_set_min_width(column, width);
}

/* ........................................................... */

static void create_sheet_cell_renderers (spreadsheet *sheet)
{
    GtkCellRenderer *r;

    r = gtk_cell_renderer_text_new();
    g_object_set(r, "ypad", 1, NULL);
    g_object_set(r, "xalign", 1.0, NULL);
    g_object_set(r, "background", "gray", NULL);
    sheet->dumbcell = r;

    r = gtk_cell_renderer_text_new();
    g_object_set(r, "ypad", 1, NULL);
    g_object_set(r, "xalign", 1.0, NULL);
    g_signal_connect (G_OBJECT (r), "edited",
		      G_CALLBACK (sheet_cell_edited), sheet);
    sheet->datacell = r;
}

/* Below: prevent cursor movement outside of the data area */

static gint catch_spreadsheet_key (GtkWidget *view, GdkEventKey *key, 
				   spreadsheet *sheet)
{
    if (key->keyval == GDK_Right || key->keyval == GDK_Left) {
	GtkTreeViewColumn *column;
	gpointer p;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(view), NULL, &column);
	p = g_object_get_data(G_OBJECT(column), "colnum");
	if (p != NULL) {
	    int colnum = GPOINTER_TO_INT(p);

	    if (key->keyval == GDK_Left && colnum == 1) {
		return TRUE;
	    }
	    if (key->keyval == GDK_Right && colnum == sheet->datacols) {
		return TRUE;
	    }
	}
    }

    return FALSE;
}

static gint catch_spreadsheet_click (GtkWidget *view, GdkEvent *event,
				     spreadsheet *sheet)
{   
    GdkModifierType mods; 
    gint ret = FALSE;

    if (event->type != GDK_BUTTON_PRESS) {
	return FALSE;
    }

    gdk_window_get_pointer(view->window, NULL, NULL, &mods);

    if (mods & GDK_BUTTON3_MASK) {
	GdkEventButton *bevent = (GdkEventButton *) event;

	gtk_menu_popup (GTK_MENU(sheet->popup), NULL, NULL, NULL, NULL,
			bevent->button, bevent->time);
	return TRUE;
    }	    
	
    if (mods & GDK_BUTTON1_MASK) {
	GdkEventButton *bevent = (GdkEventButton *) event;
	GtkTreePath *path;
	GtkTreeViewColumn *column;

#ifdef SSDEBUG
	fprintf(stderr, "Got button 1 click\n");
#endif

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(sheet->view),
				      (gint) bevent->x, 
				      (gint) bevent->y,
				      &path, &column,
				      NULL, NULL);
	if (column != NULL) {
	    gpointer p = g_object_get_data(G_OBJECT(column), "colnum");
	    gint colnum = GPOINTER_TO_INT(p);

	    if (colnum == 0) {
		/* don't respond to a click in a non-data column */
		ret = TRUE;
	    } else {
		/* otherwise start editing on clicking cell */
		gtk_tree_view_set_cursor(GTK_TREE_VIEW(sheet->view), 
					 path, column, TRUE);
		ret = TRUE;
	    }
	}
	gtk_tree_path_free(path);
    }

    return ret;
}

/* ........................................................... */

static GtkWidget *data_sheet_new (spreadsheet *sheet, gint nobs, gint nvars)
{
    GtkListStore *store; 
    GtkWidget *view;
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;
    GType *types;
    gint i, width, colnum;

    sheet->datacols = nvars - 1; /* don't show the constant */

    /* we'll drop any scalar variables from the spreadsheet */
    for (i=1; i<datainfo->v; i++) {
	if (datainfo->vector[i] == 0) {
	    sheet->datacols -= 1;
	    sheet->n_scalars += 1;
	}
    }

    if (sheet->datacols < 6) {
	sheet->padcols = 6 - sheet->datacols;
    } else {
	sheet->padcols = 0;
    }

    /* obs, data, padding, boolean cols */
    sheet->totcols = 1 + sheet->datacols + sheet->padcols + 2;

    types = mymalloc(sheet->totcols * sizeof *types);
    if (types == NULL) return NULL;

    /* configure the types */
    for (i=0; i<sheet->totcols; i++) {
	if (i >= sheet->totcols - 2) types[i] = G_TYPE_BOOLEAN;
	else types[i] = G_TYPE_STRING;
    }

    store = gtk_list_store_newv (sheet->totcols, types);
    free(types);

    view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(G_OBJECT(store));

    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW(view), TRUE);

    /* build and attach the (two) cell renderers */
    create_sheet_cell_renderers(sheet);

    /* construct the observation marker column */
    width = get_obs_col_width();
    column = gtk_tree_view_column_new_with_attributes (NULL,
						       sheet->dumbcell,
						       "text", 0, 
						       "editable", 
						       sheet->totcols - 2,
						       NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
    set_up_sheet_column(column, width);
    g_object_set_data(G_OBJECT(column), "colnum", GINT_TO_POINTER(0));

    /* construct the data columns */
    width = get_data_col_width();
    colnum = 0;
    for (i=1; i<nvars; i++) {
	char tmp[16];

	if (datainfo->vector[i] == 0) continue;
	colnum++;
	double_underscores(tmp, datainfo->varname[i]);
	column = gtk_tree_view_column_new_with_attributes (tmp,
							   sheet->datacell,
							   "text", 
							   colnum, 
							   "editable", 
							   sheet->totcols - 1,
							   NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
	set_up_sheet_column(column, width);
	g_object_set_data(G_OBJECT(column), "colnum", GINT_TO_POINTER(i));
    }

    /* add some padding columns if needed */
    for (i=0; i<sheet->padcols; i++) {
	column = gtk_tree_view_column_new_with_attributes (NULL,
							   sheet->dumbcell,
							   "text", 
							   i + sheet->datacols + 1,
							   "editable", 
							   sheet->totcols - 2,
							   NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
	set_up_sheet_column(column, width);
	g_object_set_data(G_OBJECT(column), "colnum", GINT_TO_POINTER(0));
    }

    /* set the selection property on the tree view */
    select = gtk_tree_view_get_selection (GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(select, GTK_SELECTION_NONE);

    g_signal_connect (G_OBJECT(view), "cursor-changed",
		      G_CALLBACK(update_cell_position), sheet);
    g_signal_connect (G_OBJECT(view), "key_press_event",
		      G_CALLBACK(catch_spreadsheet_key), sheet);

    return view;
}

/* ........................................................... */

static void free_spreadsheet (GtkWidget *widget, spreadsheet **psheet) 
{
    spreadsheet *sheet = *psheet;

    gtk_widget_destroy(sheet->popup);
    free(sheet);
    *psheet = NULL;
}

/* ........................................................... */

static spreadsheet *sheet_new (void)
{
    spreadsheet *sheet;

    sheet = mymalloc(sizeof *sheet);
    if (sheet == NULL) return NULL;

    sheet->view = NULL;
    sheet->win = NULL;
    sheet->locator = NULL;
    sheet->popup = NULL;
    sheet->dumbcell = NULL;
    sheet->datacell = NULL;
    sheet->datacols = sheet->datarows = 0;
    sheet->padcols = sheet->totcols = 0;
    sheet->n_scalars = 0;
    sheet->cid = 0;

    return sheet;
}

/* ........................................................... */

static gint maybe_exit_sheet (GtkWidget *w, spreadsheet *sheet)
{
    int resp;

    if (sheet_modified) {
	resp = yes_no_dialog ("gretl", 
			      _("Do you want to save changes you have\n"
				"made to the current data set?"), 1);
	if (resp == GRETL_YES) {
	    get_data_from_sheet(NULL, sheet);
	}
	else if (resp == GRETL_CANCEL || resp == -1) return FALSE;
    }
  
    gtk_widget_destroy(sheet->win);

    return FALSE;
}

/* ........................................................... */

void show_spreadsheet (DATAINFO *pdinfo) 
{
    static spreadsheet *sheet;    
    GtkWidget *tmp, *button_box;
    GtkWidget *scroller, *main_vbox;
    GtkWidget *status_box, *mbar;
    GtkItemFactory *ifac;
    int sheetwidth;
    int err = 0;

    if (sheet != NULL) {
	gdk_window_raise(sheet->win->window);
	return;
    }

    sheet = sheet_new();
    if (sheet == NULL) return;

    if (pdinfo == NULL && datainfo->v == 1) {
	errbox(_("Please add a variable to the dataset first"));
	return;
    }

    sheetwidth = get_obs_col_width() + 6 * get_data_col_width() + 14;

    sheet->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(sheet->win), _("gretl: edit data"));
    gtk_window_set_default_size(GTK_WINDOW(sheet->win), sheetwidth, 400);

    main_vbox = gtk_vbox_new (FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 5); 
    gtk_container_add(GTK_CONTAINER(sheet->win), main_vbox);
    gtk_widget_show(main_vbox);

    /* add menu bar */
    ifac = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>", 
				NULL);
#ifdef ENABLE_NLS
    gtk_item_factory_set_translate_func(ifac, menu_translate, NULL, NULL);
#endif
    gtk_item_factory_create_items(ifac, 
				  sizeof sheet_items / sizeof sheet_items[0],
				  sheet_items, sheet);
    mbar = gtk_item_factory_get_widget(ifac, "<main>");
    gtk_box_pack_start(GTK_BOX(main_vbox), mbar, FALSE, FALSE, 0);
    gtk_widget_show(mbar);

    build_sheet_popup(&sheet->popup, sheet);

    status_box = gtk_hbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(status_box), 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), status_box, FALSE, FALSE, 0);
    gtk_widget_show(status_box);

    sheet->locator = gtk_statusbar_new(); 
    gtk_widget_set_size_request(sheet->locator, 2 * get_obs_col_width(), 20);
    gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(sheet->locator), FALSE);
    sheet->cid = gtk_statusbar_get_context_id (GTK_STATUSBAR(sheet->locator), 
					       "current row and column");
    gtk_box_pack_start(GTK_BOX(status_box), sheet->locator, FALSE, FALSE, 0);
    gtk_widget_show(sheet->locator);

    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroller), 
					 GTK_SHADOW_IN);

    gtk_box_pack_start(GTK_BOX(main_vbox), scroller, TRUE, TRUE, 0);
    gtk_widget_show(scroller);

    sheet->view = data_sheet_new(sheet, datainfo->n, datainfo->v);
    gtk_container_add(GTK_CONTAINER(scroller), sheet->view);
    gtk_widget_show(sheet->view);

    /* apply and close buttons */
    button_box = gtk_hbox_new (FALSE, 5);
    gtk_box_set_homogeneous (GTK_BOX (button_box), TRUE);
    gtk_box_pack_start (GTK_BOX (main_vbox), button_box, FALSE, FALSE, 0);
    gtk_widget_show(button_box);

    g_signal_connect(G_OBJECT(sheet->win), "destroy",
		     G_CALLBACK(free_spreadsheet), &sheet);

    tmp = gtk_button_new_from_stock(GTK_STOCK_APPLY);
    gtk_box_pack_start (GTK_BOX (button_box), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(get_data_from_sheet), sheet);
    gtk_widget_show(tmp);

    tmp = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    gtk_box_pack_start (GTK_BOX (button_box), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(maybe_exit_sheet), sheet);
    gtk_widget_show(tmp);

    g_signal_connect (G_OBJECT(sheet->view), "button_press_event",
		      G_CALLBACK(catch_spreadsheet_click),
		      sheet);

    if (pdinfo == NULL) {
	err = add_data_to_sheet(sheet, 0);
    } else {
	err = add_data_to_sheet(sheet, 1);
    }

    if (err) {
	gtk_widget_destroy(sheet->win);
	return;
    }

    select_first_editable_cell(sheet);
    sheet_modified = 0;

    gtk_widget_show(sheet->win);
}

