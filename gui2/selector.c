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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* selector.c for gretl */

#include "gretl.h"
#include "selector.h"
#include "treeutils.h"
#include "dlgutils.h"
#include "menustate.h"

enum {
    SR_VARLIST,
    SR_RIGHTVARS,
    SR_AUXVARS,
    SR_EXTRA
};

#define N_EXTRA 4

struct _selector {
    GtkWidget *dlg;
    GtkWidget *vbox;
    GtkWidget *action_area;
    GtkWidget *varlist;
    GtkWidget *depvar;
    GtkWidget *rightvars;
    GtkWidget *auxvars;
    GtkWidget *default_check;
    GtkWidget *add_button;
    GtkWidget *extra[N_EXTRA];
    int code;
    int active_var;
    int error;
    gretlopt opts;
    char *cmdlist;
    gpointer data;
};

#define WANT_TOGGLES(c) (c == ARMA || \
                         c == COINT || \
                         c == COINT2 || \
                         c == GARCH || \
                         c == HILU || \
                         c == OLS || \
                         c == TOBIT || \
                         c == TSLS || \
                         c == VAR)

static int default_var;
static int *xlist;
static int *auxlist;
static GtkWidget *scatters_label;
static GtkWidget *scatters_menu;

static gint dblclick_varlist_row (GtkWidget *w, GdkEventButton *event, 
				  selector *sr); 
static gint listvar_special_click (GtkWidget *widget, GdkEventButton *event, 
				   gpointer data);
static gint add_right_click (GtkWidget *widget, GdkEventButton *event, 
			     selector *sr);

static gboolean set_active_var (GtkWidget *widget, GdkEventButton *event,
				selector *sr)
{
    GtkTreeView *view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreePath *path;

    if (gtk_tree_view_get_path_at_pos(view, event->x, event->y, &path, 
				      NULL, NULL, NULL)) { 
	GtkTreeIter iter;
	gint varnum, row;

	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, 0, &varnum, -1);
	if (sr != NULL) sr->active_var = varnum;
	row = tree_path_get_row_number(path);
	g_object_set_data(G_OBJECT(widget), "active_row",
			  GINT_TO_POINTER(row));
	gtk_tree_path_free(path);
    }
    return FALSE;
}

/* build a new liststore and associated tree view, and pack into the
   given box */

static GtkWidget *var_list_box_new (GtkBox *box, selector *sr, int which) 
{
    GtkListStore *store; 
    GtkWidget *view, *scroller;
    GtkCellRenderer *renderer; 
    GtkTreeViewColumn *column;
    GtkTreeSelection *select;
    int viewsize = 100;

    store = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_STRING);

    view = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref (G_OBJECT(store));

    renderer = gtk_cell_renderer_text_new ();
    g_object_set (renderer, "ypad", 0, NULL);
    column = gtk_tree_view_column_new_with_attributes (NULL,
						       renderer,
						       "text", 
						       1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);	
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    gtk_tree_view_set_reorderable(GTK_TREE_VIEW(view), FALSE);

    select = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode (select, GTK_SELECTION_EXTENDED);

    g_signal_connect (G_OBJECT(view), "motion_notify_event",
		      G_CALLBACK(listbox_drag), NULL);

    if (which == SR_VARLIST) { 
	/* left-hand box with the options */
	g_signal_connect (G_OBJECT(view), "button_press_event",
			  G_CALLBACK(add_right_click),
			  sr);

	g_signal_connect (G_OBJECT(view), "button_press_event",
			  G_CALLBACK(set_active_var),
			  sr);

	g_signal_connect (G_OBJECT(view), "button_press_event",
			  G_CALLBACK(dblclick_varlist_row),
			  sr);
    } else if (which == SR_RIGHTVARS || which == SR_AUXVARS) { 
	/* lists of selected items */
	g_signal_connect (G_OBJECT(view), "button_press_event",
			  G_CALLBACK(set_active_var),
			  NULL);
	
	g_signal_connect (G_OBJECT(view), "button_press_event",
			  G_CALLBACK(listvar_special_click),
			  view);
    } 

    scroller = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller),
				    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroller),
                                         GTK_SHADOW_IN);    
    gtk_container_add (GTK_CONTAINER(scroller), view);

    gtk_box_pack_start(box, scroller, TRUE, TRUE, 0);

    viewsize *= gui_scale;
    gtk_widget_set_size_request(view, viewsize, -1);
    gtk_widget_show(view);
    gtk_widget_show(scroller);

    return view;
}

void clear_selector (void)
{
    default_var = 0;

    free(xlist);
    xlist = NULL;

    free(auxlist);
    auxlist = NULL;
}

/* add to "extra" var slot the current selection from sr->varlist */

static void real_set_extra_var (GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter *iter, selector *sr)
{
    gint vnum;
    gchar *vname;
    
    gtk_tree_model_get(model, iter, 0, &vnum, 1, &vname, -1);
    gtk_entry_set_text(GTK_ENTRY(sr->extra[0]), vname);
    g_free(vname);
    g_object_set_data(G_OBJECT(sr->extra[0]), "data",
		      GINT_TO_POINTER(vnum));
}

static void set_extra_var_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(sr->varlist));
    gtk_tree_selection_selected_foreach (selection, 
					 (GtkTreeSelectionForeachFunc) 
					 real_set_extra_var,
					 sr);
}

static void real_set_factor (GtkTreeModel *model, GtkTreePath *path,
			     GtkTreeIter *iter, selector *sr)
{
    gint vnum;
    gchar *vname;
    
    gtk_tree_model_get (model, iter, 0, &vnum, 1, &vname, -1);
    gtk_entry_set_text(GTK_ENTRY(sr->rightvars), vname);
    g_free(vname);
    g_object_set_data(G_OBJECT(sr->rightvars), "data",
		      GINT_TO_POINTER(vnum));
}

static void set_factor_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(sr->varlist));
    gtk_tree_selection_selected_foreach (selection, 
					 (GtkTreeSelectionForeachFunc) 
					 real_set_factor,
					 sr);
}

static void remove_specified_var_from_right (selector *sr, gint ynum)
{
    GtkTreeView *view = GTK_TREE_VIEW(sr->rightvars);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    gint rnum;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
	gtk_tree_model_get(model, &iter, 0, &rnum, -1);
	if (rnum == ynum) {
	    gtk_list_store_remove(GTK_LIST_STORE(model), &iter); 
	} else {   
	    while (gtk_tree_model_iter_next(model, &iter)) {
		gtk_tree_model_get(model, &iter, 0, &rnum, -1);
		if (rnum == ynum) {
		    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		    break;
		}
	    }
	}
    }
}

static void set_dependent_var_from_active (selector *sr)
{
    gint i = sr->active_var;

    if (sr->depvar == NULL) return;

    /* models: if we select foo as regressand, remove it from the
       list of regressors if need be */
    if (MODEL_CODE(sr->code)) {
	remove_specified_var_from_right(sr, i);
    }

    gtk_entry_set_text(GTK_ENTRY(sr->depvar), datainfo->varname[i]);
    g_object_set_data(G_OBJECT(sr->depvar), "data",
		      GINT_TO_POINTER(i));
}

static void real_set_dependent_var (GtkTreeModel *model, GtkTreePath *path,
				    GtkTreeIter *iter, selector *sr)
{
    gint vnum;
    gchar *vname;

    gtk_tree_model_get (model, iter, 0, &vnum, 1, &vname, -1);
    gtk_entry_set_text(GTK_ENTRY(sr->depvar), vname);
    g_free(vname);
    g_object_set_data(G_OBJECT(sr->depvar), "data", 
		      GINT_TO_POINTER(vnum));
}

static void set_dependent_var_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    if (sr->depvar == NULL) return;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(sr->varlist));
    gtk_tree_selection_selected_foreach (selection, 
					 (GtkTreeSelectionForeachFunc) 
					 real_set_dependent_var,
					 sr);
}

static void set_right_var_from_main (GtkTreeModel *model, GtkTreePath *path,
				     GtkTreeIter *iter, selector *sr)
{
    GtkTreeModel *rightmod;
    GtkTreeIter r_iter;
    gchar *vname = NULL;
    gchar *vnum = NULL;
    int v;

    gtk_tree_model_get(model, iter, 0, &vnum, 1, &vname, -1);

    rightmod = gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rightvars));
    if (rightmod == NULL) {
	g_free(vname);
	g_free(vnum);
	return;
    }

    v = atoi(vnum);

    if (gtk_tree_model_get_iter_first(rightmod, &r_iter)) {
	while (gtk_tree_model_iter_next(rightmod, &r_iter)) {
	    ;
	}
    }

    gtk_list_store_append(GTK_LIST_STORE(rightmod), &r_iter);
    gtk_list_store_set(GTK_LIST_STORE(rightmod), &r_iter, 
		       0, v, 1, vname, -1);

    g_free(vname);
    g_free(vnum);
}

static void set_vars_from_main (selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(mdata->listbox));
    gtk_tree_selection_selected_foreach(selection, 
					(GtkTreeSelectionForeachFunc) 
					set_right_var_from_main,
					sr);
}

static void set_single_var (selector *sr, int v)
{
    GtkTreeModel *mod;
    GtkTreeIter iter;

    mod = gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rightvars));
    if (mod == NULL) {
	return;
    }

    gtk_tree_model_get_iter_first(mod, &iter);
    gtk_list_store_append(GTK_LIST_STORE(mod), &iter);
    gtk_list_store_set(GTK_LIST_STORE(mod), &iter, 
		       0, v, 1, datainfo->varname[v], -1);
}

static int selection_at_max (selector *sr, int nsel)
{
    int ret = 0;

    if (TWO_VARS_CODE(sr->code) && nsel == 2) {
	ret = 1;
    }

    return ret;
}

static void real_add_generic (GtkTreeModel *model, GtkTreeIter *iter, 
			      selector *sr, int which)
{
    GtkWidget *list;
    GtkTreeModel *orig_model;
    GtkTreeIter orig_iter;
    gint vnum, test;
    gchar *vname = NULL;
    gint already_there = 0;
    gint at_max = 0;

    gtk_tree_model_get(model, iter, 0, &vnum, 1, &vname, -1);

    if (which == SR_AUXVARS) {
	list = sr->auxvars;
    } else {
	list = sr->rightvars;
    }

    if (!GTK_IS_TREE_VIEW(list)) return;

    orig_model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    if (orig_model == NULL) {
	g_free(vname);
	return;
    }

    if (gtk_tree_model_get_iter_first(orig_model, &orig_iter)) {
	int j = 1;

	while (1) {
	    if (selection_at_max(sr, j)) {
		at_max = 1;
		break;
	    }
	    gtk_tree_model_get(orig_model, &orig_iter, 0, &test, -1);
	    if (test == vnum) {
		already_there = 1; 
		break;
	    }
	    if (!gtk_tree_model_iter_next(orig_model, &orig_iter)) {
		break;
	    }
	    j++;
	}
    }

    if (!already_there && !at_max) {
        gtk_list_store_append(GTK_LIST_STORE(orig_model), &orig_iter);
        gtk_list_store_set(GTK_LIST_STORE(orig_model), &orig_iter, 
			   0, vnum, 1, vname, -1);
    }

    if (sr->add_button != NULL && at_max) {
	gtk_widget_set_sensitive(sr->add_button, FALSE);
    }

    g_free(vname);
}

static void add_auxvar (GtkTreeModel *model, GtkTreePath *path,
			GtkTreeIter *iter, selector *sr)
{
    real_add_generic(model, iter, sr, SR_AUXVARS);
}

static void add_to_right (GtkTreeModel *model, GtkTreePath *path,
			  GtkTreeIter *iter, selector *sr)
{
    /* models: don't add the regressand to the list of regressors */
    if (MODEL_CODE(sr->code)) {
	gint xnum, ynum;
    
	gtk_tree_model_get(model, iter, 0, &xnum, -1);
	ynum = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->depvar), "data"));
	if (xnum == ynum) {
	    return;
	}
    }
    real_add_generic(model, iter, sr, SR_RIGHTVARS);
}

static void add_auxvar_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(sr->varlist));
    gtk_tree_selection_selected_foreach (selection, 
					 (GtkTreeSelectionForeachFunc) 
					 add_auxvar,
					 sr);
}

static void add_all_to_right_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    if (!GTK_IS_TREE_VIEW(sr->varlist)) return;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(sr->varlist));
    gtk_tree_selection_select_all(selection);
    gtk_tree_selection_selected_foreach (selection, 
					 (GtkTreeSelectionForeachFunc) 
					 add_to_right,
					 sr);
}

static void add_to_right_callback (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    if (!GTK_IS_TREE_VIEW(sr->varlist)) return;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(sr->varlist));
    gtk_tree_selection_selected_foreach (selection, 
					 (GtkTreeSelectionForeachFunc) 
					 add_to_right,
					 sr);
}

static void remove_from_right_callback (GtkWidget *w, gpointer data)
{
    GtkTreeView *view = GTK_TREE_VIEW(data);
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(view);
    GtkTreePath *path;
    GtkTreeIter iter, last;
    selector *sr;
    int nsel = 0;

    if (model == NULL || selection == NULL) {
	return;
    }

    /* get to the last row */
    if (gtk_tree_model_get_iter_first(model, &iter)) {
	last = iter;
	nsel = 1;
	while (gtk_tree_model_iter_next(model, &iter)) {
	    last = iter;
	    nsel++;
	}
    } else {
	return;
    }
    
    /* work back up, deleting selected rows */
    path = gtk_tree_model_get_path (model, &last);
    while (1) {
	if (gtk_tree_model_get_iter(model, &last, path) &&
	    gtk_tree_selection_iter_is_selected(selection, &last)) {
	    gtk_list_store_remove(GTK_LIST_STORE(model), &last);
	    nsel--;
	}
	if (!gtk_tree_path_prev(path)) {
	    break;
	}
    } 

    sr = g_object_get_data(G_OBJECT(data), "selector");
    if (sr != NULL && sr->add_button != NULL &&
	!GTK_WIDGET_SENSITIVE(sr->add_button) &&
	!selection_at_max(sr, nsel)) {
	gtk_widget_set_sensitive(sr->add_button, TRUE);
    }
}

/* callbacks from button presses in list boxes: double and right
   clicks do special stuff */

static gint dblclick_varlist_row (GtkWidget *w, GdkEventButton *event, 
				  selector *sr) 
{
    if (event != NULL && event->type == GDK_2BUTTON_PRESS) { 
	set_dependent_var_from_active(sr);
	if (sr->default_check != NULL) 
	    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(sr->default_check),
					  TRUE);
    }
    return FALSE;
}

static gint listvar_special_click (GtkWidget *widget, GdkEventButton *event, 
				   gpointer data)
{
    GdkWindow *topwin;
    GdkModifierType mods;

    topwin = gtk_widget_get_parent_window(GTK_WIDGET(data));
    gdk_window_get_pointer(topwin, NULL, NULL, &mods); 

    if (mods & GDK_BUTTON2_MASK) {
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(data), TRUE);
    } else {
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(data), FALSE);
    }

    if (mods & GDK_BUTTON3_MASK) {
	remove_from_right_callback(NULL, data);
	return TRUE;
    } 

    return FALSE;
}

static gint add_right_click (GtkWidget *widget, GdkEventButton *event, 
			     selector *sr)
{
    GdkWindow *topwin;
    GdkModifierType mods;

    topwin = gtk_widget_get_parent_window(sr->varlist);
    gdk_window_get_pointer(topwin, NULL, NULL, &mods); 
    if (mods & GDK_BUTTON3_MASK) {
	add_to_right_callback(NULL, sr);
	return TRUE;
    }
    return FALSE;
}

/* end special click callbacks */

static void clear_vars (GtkWidget *w, selector *sr)
{
    GtkTreeSelection *selection;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(sr->varlist));
    gtk_tree_selection_unselect_all(selection);

    if (sr->depvar != NULL) {
	gtk_entry_set_text(GTK_ENTRY(sr->depvar), "");
    }

    if (sr->code == GR_DUMMY || sr->code == GR_3D) {
	gtk_entry_set_text(GTK_ENTRY(sr->rightvars), "");
    } else {
	clear_varlist(sr->rightvars);
	if (sr->add_button != NULL) {
	    gtk_widget_set_sensitive(sr->add_button, TRUE);
	}
    }

    if (MODEL_CODE(sr->code)) {
	GtkTreeModel *model = 
	    gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rightvars));
	GtkTreeIter iter;

	gtk_tree_model_get_iter_first(model, &iter);
	gtk_list_store_append(GTK_LIST_STORE(model), &iter);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, 
			   0, 0, 1, "const", -1);
    }
}

static void topslot_empty (int code)
{
    switch (code) {
    case GR_XY:
    case GR_3D:
    case GR_IMP:
	errbox(_("You must select an X-axis variable"));
	break;
    case SCATTERS:
	errbox(_("You must select a Y-axis variable"));
	break;
    default:
	errbox(_("You must select a dependent variable"));
    }
}

static gint varlist_row_count (GtkWidget *w)
{
    GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
    GtkTreeIter iter;
    gint n = 0;

    if (gtk_tree_model_get_iter_first(model, &iter)) {
	n = 1;
	while (gtk_tree_model_iter_next(model, &iter)) n++;
    }

    return n;
}

static void reverse_list (char *list)
{
    char *tmp, *p;
    char istr[8];

    p = strchr(list, ';');
    if (p == NULL) return;

    tmp = malloc(strlen(list) + 4);
    if (tmp == NULL) return;

    sscanf(list, "%7s", istr);

    strcpy(tmp, p + 2);
    strcat(tmp, " ; ");
    strcat(tmp, istr);

    strcpy(list, tmp);
    
    free(tmp);
}

enum cmdlist_codes {
    ADD_NOW,
    ADD_AT_END
};

static int add_to_cmdlist (selector *sr, const char *add)
{
    int n = strlen(sr->cmdlist);
    char *cmdlist = NULL;
    int err = 0;

    if (n % MAXLEN > MAXLEN - 32) {
	int blocks = 2 + n / MAXLEN;

	cmdlist = realloc(sr->cmdlist, blocks * MAXLEN);
	if (cmdlist == NULL) {
	    err = 1;
	} else {
	    sr->cmdlist = cmdlist;
	}
    }

    if (!err) {
	strcat(sr->cmdlist, add);
    }

    return err;
}

static void add_pq_vals_to_cmdlist (selector *sr)
{
    GtkAdjustment *adj;
    int vals[N_EXTRA] = {0};
    char s[8];
    int i, imax = 2;

    for (i=0; i < N_EXTRA && sr->extra[i] != NULL; i++) {
	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(sr->extra[i]));
	vals[i] = (int) adj->value;
    }

    if (vals[2] != 0 || vals[3] != 0) {
	imax = 4;
    }

    for (i=0; i<imax; i++) {
	sprintf(s, "%d ", vals[i]);
	add_to_cmdlist(sr, s);
	if (i == 1 || i == 3) {
	    add_to_cmdlist(sr, "; ");
	}
    }
}    

static gboolean construct_cmdlist (GtkWidget *w, selector *sr)
{
    gint i = 0, rows = 0;
    gchar numstr[8], endbit[12] = {0};
    GtkTreeModel *model;
    GtkTreeIter iter;

    sr->error = 0;

    sr->cmdlist = mymalloc(MAXLEN); 
    if (sr->cmdlist == NULL) {
	return FALSE;
    }

    sr->cmdlist[0] = 0;

    if (sr->code != GR_DUMMY && sr->code != GR_3D) {
	rows = varlist_row_count(sr->rightvars);
    }

    /* deal with content of first "extra" widget */
    if (sr->code == WLS) {
	const gchar *str = gtk_entry_get_text(GTK_ENTRY(sr->extra[0]));

	if (str == NULL || *str == '\0') {
	    errbox(_("You must select a weight variable"));
	    sr->error = 1;
	} else {
	    i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->extra[0]), "data"));
	    sprintf(numstr, "%d ", i);
	    add_to_cmdlist(sr, numstr);
	}
    } else if (sr->code == POISSON) {
	const gchar *str = gtk_entry_get_text(GTK_ENTRY(sr->extra[0]));

	if (str != NULL && *str != '\0') {
	    i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->extra[0]), "data"));
	    sprintf(endbit, " ; %d", i);
	}
    } else if (sr->code == AR) {
	const gchar *lags;

	lags = gtk_entry_get_text(GTK_ENTRY(sr->extra[0]));
	if (!strlen(lags)) {
	    errbox(_("You must specify a list of lags"));
	    sr->error = 1;
	} else {
	    add_to_cmdlist(sr, lags);
	    add_to_cmdlist(sr, " ; ");
	}
    } else if (sr->code == VAR || sr->code == COINT || sr->code == COINT2) {
	GtkAdjustment *adj;
 
	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(sr->extra[0]));
	i = (gint) adj->value;
	sprintf(numstr, "%d ", i);
	add_to_cmdlist(sr, numstr);
    } else if (sr->code == ARMA || sr->code == GARCH) {
	add_pq_vals_to_cmdlist(sr);
    } else if (sr->code == GR_DUMMY || sr->code == GR_3D) {
	const gchar *str = gtk_entry_get_text(GTK_ENTRY(sr->extra[0]));

	if (str == NULL || !strlen(str)) {
	    errbox(_("You must select a Y-axis variable"));
	    sr->error = 1;
	} else {
	    i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->extra[0]), "data"));
	    sprintf(numstr, "%d ", i);
	    add_to_cmdlist(sr, numstr);
	}
    }

    /* next deal with the "depvar" widget */
    if (!sr->error && sr->depvar != NULL) {
	const gchar *str = gtk_entry_get_text(GTK_ENTRY(sr->depvar));

	if (str == NULL || !strlen(str)) {
	    topslot_empty(sr->code);
	    sr->error = 1;
	} else {
	    i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->depvar), "data"));
	    if (sr->code == GR_XY || sr->code == GR_IMP) {
		sprintf(endbit, " %d", i);
	    } else {
		sprintf(numstr, "%d", i);
		add_to_cmdlist(sr, numstr);
	    }
	}
    }

    /* bail out if things have gone wrong already */
    if (sr->error) return TRUE;

    if (sr->default_check != NULL && 
	GTK_TOGGLE_BUTTON(sr->default_check)->active) {
	default_var = i;
    }

    if (sr->code == SCATTERS) {
	add_to_cmdlist(sr, ";");
    }

    if (sr->code == GR_DUMMY || sr->code == GR_3D) { /* special case */
	const gchar *str = gtk_entry_get_text(GTK_ENTRY(sr->rightvars));

	if (str == NULL || !*str) {
	    if (sr->code == GR_3D) {
		errbox(_("You must select a Z-axis variable"));
	    } else {
		errbox(_("You must select a factor variable"));
	    }
	    sr->error = 1;
	} else {
	    i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sr->rightvars), 
						  "data"));
	    sprintf(numstr, " %d", i);
	    add_to_cmdlist(sr, numstr);
	}
	return TRUE;
    }

    if (MODEL_CODE(sr->code) && rows > 0) {
	int *rlist;

	rlist = myrealloc(xlist, (rows + 1) * sizeof *rlist);
	if (rlist == NULL) {
	    return FALSE;
	}
	xlist = rlist;
	xlist[0] = rows;
    }

    model = gtk_tree_view_get_model (GTK_TREE_VIEW(sr->rightvars));
    gtk_tree_model_get_iter_first (model, &iter);
				    
    for (i=0; i<rows; i++) {
	gint rvar;
	gchar *tmp;

	gtk_tree_model_get (model, &iter, 0, &rvar, -1);
	tmp = g_strdup_printf(" %d", rvar);
	add_to_cmdlist(sr, tmp);
	g_free(tmp);
	if (MODEL_CODE(sr->code) && xlist != NULL) {
	    xlist[i+1] = rvar;
	}
	gtk_tree_model_iter_next(model, &iter);
    }

    if (sr->code == TSLS || sr->code == VAR) {
	model = gtk_tree_view_get_model (GTK_TREE_VIEW(sr->auxvars));
	gtk_tree_model_get_iter_first (model, &iter);
	rows = varlist_row_count(sr->auxvars);
	if (rows > 0) {
	    auxlist = realloc(auxlist, (rows + 1) * sizeof *auxlist);
	    if (auxlist != NULL) {
		auxlist[0] = rows;
	    }
	    add_to_cmdlist(sr, " ;");
	    for (i=0; i<rows; i++) {
		gint inst;
		gchar *tmp;

		gtk_tree_model_get (model, &iter, 0, &inst, -1);
		tmp = g_strdup_printf(" %d", inst);
		add_to_cmdlist(sr, tmp);
		g_free(tmp);
		if (auxlist != NULL) {
		    auxlist[i+1] = inst;
		}
		gtk_tree_model_iter_next(model, &iter);
	    }
	} else if (sr->code == TSLS) {
	    errbox(_("You must specify a set of instrumental variables"));
	    sr->error = 1;
	}
    }

    if (endbit[0] != '\0') {
	add_to_cmdlist(sr, endbit);
    }

    if (sr->code == SCATTERS && 
	gtk_option_menu_get_history(GTK_OPTION_MENU(scatters_menu))) {
	reverse_list(sr->cmdlist);
    }

    if (sr->error) return TRUE;

    return FALSE;
}

void delete_selection_dialog (selector *sr)
{
    gtk_widget_destroy(sr->dlg);
}

static void maybe_delete_dialog (GtkWidget *widget, selector *sr)
{
    GtkWidget *open_dialog = get_open_dialog();

    if (open_dialog != NULL && !sr->error) {
	gtk_widget_destroy(sr->dlg);
    }
}

static void cancel_selector (GtkWidget *widget, selector *sr)
{
    GtkWidget *open_dialog = get_open_dialog();

    if (open_dialog != NULL) {
	gtk_widget_destroy(sr->dlg);
    }
}

static void destroy_selector (GtkWidget *w, selector *sr) 
{
    if (SAVE_DATA_ACTION(sr->code)) {
	gtk_main_quit();
    }

    free(sr->cmdlist);
    free(sr);

    set_open_dialog(NULL);
}

static char *est_str (int cmdnum)
{
    switch (cmdnum) {
    case OLS:
	return N_("OLS");
    case HCCM:
	return N_("HCCM");
    case HSK:
	return N_("Heteroskedasticity corrected");
    case CORC:
	return N_("Cochrane-Orcutt");
    case HILU:
	return N_("Hildreth-Lu");
    case PWE:
	return N_("Prais-Winsten");
    case LOGIT:
	return N_("Logit");
    case PROBIT:
	return N_("Probit");
    case TOBIT:
	return N_("Tobit");
    case LOGISTIC:
	return N_("Logistic");
    case POISSON:
	return N_("Poisson");
    case POOLED:
	return N_("Pooled OLS");
    case WLS:
	return N_("Weighted least squares");
    case TSLS:
	return N_("Two-stage least squares");
    case AR:
	return N_("Autoregressive");
    case ARMA:
	return N_("ARMA");
    case GARCH:
	return N_("GARCH");
    case VAR:
	return N_("VAR");
    case LAD:
	return N_("LAD");
    case COINT:
    case COINT2:
	return N_("Cointegration");
#ifdef ENABLE_GMP
    case MPOLS:
	return N_("High precision OLS");
#endif
    default:
	return "";
    }
}

static char *extra_string (int cmdnum)
{
    switch (cmdnum) {
    case WLS:
	return N_("Weight variable");
    case POISSON:
	return N_("Offset variable");
    case TSLS:
	return N_("Instruments");
    case AR:
	return N_("List of AR lags");
    case GR_DUMMY:
    case GR_3D:
	return N_("Y-axis variable");
    default:
	return NULL;
    }
}

static gint flip_scatters_axis (GtkMenuItem *m, GtkOptionMenu *popdown)
{
    gint state = gtk_option_menu_get_history(popdown);

    if (state == 0) {
	gtk_label_set_text(GTK_LABEL(scatters_label), _("X-axis variables"));
    } else {
	gtk_label_set_text(GTK_LABEL(scatters_label), _("Y-axis variables"));
    }

    return FALSE;
}

static GtkWidget *
scatters_popdown (void)
{
    GtkWidget *popdown;
    GtkWidget *menu;
    GtkWidget *child;
    const char *popstrings[] = {
        N_("Y-axis variable"),
        N_("X-axis variable")
    };
    int i;

    popdown = gtk_option_menu_new();
    menu = gtk_menu_new();

    for (i=0; i<2; i++) {
        child = gtk_menu_item_new_with_label(_(popstrings[i]));
	g_signal_connect(G_OBJECT(child), "activate",
			 G_CALLBACK(flip_scatters_axis), popdown);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), child);
    }

    gtk_option_menu_set_menu(GTK_OPTION_MENU(popdown), menu);

    scatters_menu = popdown;

    return popdown;
}

static GtkWidget *
entry_with_label_and_chooser (selector *sr, GtkWidget *vbox,
			      gchar *label_string,
			      int label_active,
			      void (*clickfunc)())
{
    GtkWidget *tmp, *x_hbox;
    GtkWidget *entry;

    if (label_active) {
	tmp = scatters_popdown();
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show_all(tmp);
    } else if (label_string != NULL) {
	tmp = gtk_label_new(label_string);
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }

    x_hbox = gtk_hbox_new(FALSE, 5); 

    tmp = gtk_button_new_with_label (_("Choose->"));
    gtk_box_pack_start(GTK_BOX(x_hbox), tmp, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT(tmp), "clicked", 
		      G_CALLBACK(clickfunc), sr);
    gtk_widget_show(tmp); 

    entry = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(entry), 8);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 12);

    gtk_box_pack_start(GTK_BOX(x_hbox), entry, FALSE, FALSE, 0);
    gtk_widget_show(entry); 

    gtk_box_pack_start(GTK_BOX(vbox), x_hbox, FALSE, FALSE, 0);
    gtk_widget_show(x_hbox); 

    if (label_active || label_string != NULL) {
	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }

    return entry;
}

static void build_x_axis_section (selector *sr, GtkWidget *right_vbox)
{
    if (sr->code == SCATTERS) {
	sr->depvar = entry_with_label_and_chooser (sr, right_vbox,
						   NULL, 1,
						   set_dependent_var_callback);
    } else {
	sr->depvar = entry_with_label_and_chooser (sr, right_vbox,
						   _("X-axis variable"), 0,
						   set_dependent_var_callback);
    }
}

static void build_depvar_section (selector *sr, GtkWidget *right_vbox,
				  int preselect)
{
    GtkWidget *tmp, *depvar_hbox;
    int yvar = (preselect)? preselect : default_var;

    if (sr->code == VAR) {
        tmp = gtk_label_new (_("First dependent variable"));
    } else {
        tmp = gtk_label_new (_("Dependent variable"));
    }

    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);

    depvar_hbox = gtk_hbox_new(FALSE, 5); 

    tmp = gtk_button_new_with_label (_("Choose ->"));
    gtk_box_pack_start(GTK_BOX(depvar_hbox), tmp, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT(tmp), "clicked", 
                      G_CALLBACK(set_dependent_var_callback), sr);
    gtk_widget_show(tmp); 

    sr->depvar = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(sr->depvar), 8);
    gtk_entry_set_width_chars(GTK_ENTRY(sr->depvar), 12);

    if (yvar) {
        gtk_entry_set_text(GTK_ENTRY(sr->depvar), datainfo->varname[yvar]);
        g_object_set_data(G_OBJECT(sr->depvar), "data",
                          GINT_TO_POINTER(yvar));
    }

    gtk_box_pack_start(GTK_BOX(depvar_hbox), sr->depvar, FALSE, FALSE, 0);
    gtk_widget_show(sr->depvar); 

    gtk_box_pack_start(GTK_BOX(right_vbox), depvar_hbox, FALSE, FALSE, 0);
    gtk_widget_show(depvar_hbox); 

    sr->default_check = gtk_check_button_new_with_label(_("Set as default"));
    gtk_box_pack_start(GTK_BOX(right_vbox), sr->default_check, FALSE, FALSE, 0);
    gtk_widget_show(sr->default_check); 

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);
}

static void lag_order_spin (selector *sr, GtkWidget *right_vbox)
{
    GtkWidget *tmp, *midhbox;
    GtkObject *adj;
    gdouble order; 
    gdouble ordermax;

    if (datainfo->n < 72) {
	ordermax = datainfo->n / 2;
    } else {
	ordermax = 36;
    }

    order = (datainfo->pd > 12)? 12 : datainfo->pd;

    midhbox = gtk_hbox_new(FALSE, 5);
    tmp = gtk_label_new(_("lag order:"));
    adj = gtk_adjustment_new(order, 1, ordermax, 1, 1, 1);
    sr->extra[0] = gtk_spin_button_new (GTK_ADJUSTMENT(adj), 1, 0);
    gtk_box_pack_start (GTK_BOX (midhbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);
    gtk_box_pack_start (GTK_BOX (midhbox), sr->extra[0], FALSE, FALSE, 5);
    gtk_widget_show(sr->extra[0]);

    gtk_box_pack_start(GTK_BOX(right_vbox), midhbox, FALSE, FALSE, 0);
    gtk_widget_show(midhbox); 
}

static void dummy_box (selector *sr, GtkWidget *vbox)
{
    sr->rightvars = entry_with_label_and_chooser (sr, vbox,
						  _("Factor (dummy)"), 0,
						  set_factor_callback);
}

static void zvar_box (selector *sr, GtkWidget *vbox)
{
    sr->rightvars = entry_with_label_and_chooser (sr, vbox,
						  _("Z-axis variable"), 0,
						  set_factor_callback);
}

static void extra_var_box (selector *sr, GtkWidget *vbox)
{
    sr->extra[0] = entry_with_label_and_chooser (sr, vbox,
						 NULL, 0,
						 set_extra_var_callback);
}

static void auxiliary_varlist_box (selector *sr, GtkWidget *right_vbox)
{
    GtkWidget *tmp, *remove, *midhbox, *button_vbox;
    GtkListStore *store;
    GtkTreeIter iter;

    midhbox = gtk_hbox_new(FALSE, 5);

    button_vbox = gtk_vbox_new(TRUE, 5);

    tmp = gtk_button_new_with_label (_("Add ->"));
    gtk_box_pack_start(GTK_BOX(button_vbox), tmp, TRUE, FALSE, 0);
    g_signal_connect (G_OBJECT(tmp), "clicked", 
		      G_CALLBACK(add_auxvar_callback), sr);
    gtk_widget_show(tmp);
    
    remove = gtk_button_new_with_label (_("<- Remove"));
    gtk_box_pack_start(GTK_BOX(button_vbox), remove, TRUE, FALSE, 0);
    gtk_widget_show(remove);

    gtk_box_pack_start(GTK_BOX(midhbox), button_vbox, TRUE, TRUE, 0);
    gtk_widget_show(button_vbox);

    /* then the listing */
    sr->auxvars = var_list_box_new(GTK_BOX(midhbox), sr, SR_AUXVARS);
    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->auxvars)));
    gtk_list_store_clear (store);
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL(store), &iter);

    if (auxlist != NULL) {
	int i;

	for (i=1; i<=auxlist[0]; i++) {
	    gtk_list_store_append(store, &iter);
	    gtk_list_store_set(store, &iter, 
			       0, auxlist[i], 
			       1, datainfo->varname[auxlist[i]], 
			       -1);
	}
    } else {
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
			   0, 0, 1, "const", -1); 
    }

    /* hook up remove button to list box */
    g_signal_connect (G_OBJECT(remove), "clicked", 
		      G_CALLBACK(remove_from_right_callback), 
		      sr->auxvars);

    gtk_box_pack_start(GTK_BOX(right_vbox), midhbox, TRUE, TRUE, 0);
    gtk_widget_show(midhbox); 
}

static void build_mid_section (selector *sr, GtkWidget *right_vbox)
{
    GtkWidget *tmp;
    const char *str = _(extra_string(sr->code));

    if (str != NULL) {
	tmp = gtk_label_new(str);
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }	

    if (sr->code == WLS || sr->code == POISSON ||
	sr->code == GR_DUMMY || sr->code == GR_3D) { 
	extra_var_box(sr, right_vbox);
    } else if (sr->code == COINT || sr->code == COINT2) {
	lag_order_spin(sr, right_vbox);
    } else if (sr->code == TSLS) {
	auxiliary_varlist_box(sr, right_vbox);
    } else if (sr->code == AR) {
	sr->extra[0] = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(right_vbox), sr->extra[0], 
			   FALSE, TRUE, 0);
	gtk_widget_show(sr->extra[0]); 
    } else if (sr->code == VAR) {
	lag_order_spin (sr, right_vbox);
	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
	tmp = gtk_label_new(_("Deterministic variables"));
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
	auxiliary_varlist_box (sr, right_vbox);
    }

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);
}

static int screen_scalar (int i, int c)
{
    if ((MODEL_CODE(c) || COINT_CODE(c) || GRAPH_CODE(c) || 
	 c == LAGS || c == DIFF || c == LDIFF)
	&& datainfo->vector[i] == 0) {
	return 1;
    } else {
	return 0;
    }
}

static void selector_init (selector *sr, guint code, const char *title)
{
    GtkWidget *base, *hsep;
    int i, dlgheight = 320;
    
    if (MODEL_CODE(code) && datainfo->v > 10) {
	dlgheight = 400;
    } else if (code == WLS || code == POISSON || code == AR) {
	dlgheight = 350;
    } else if (code == TSLS) {
	dlgheight = 400;
    }

    if (code == VAR) {
	dlgheight = 450;
    }

    if (WANT_TOGGLES(code)) {
	dlgheight += 40;
    }

    if (code == ARMA && datainfo->pd > 1) {
	dlgheight += 60;
    }

    sr->varlist = NULL;
    sr->depvar = NULL;
    sr->rightvars = NULL;
    sr->auxvars = NULL;
    sr->default_check = NULL;
    sr->add_button = NULL;

    for (i=0; i<N_EXTRA; i++) {
	sr->extra[i] = NULL;
    }

    sr->cmdlist = NULL;
    sr->data = NULL;
    sr->active_var = 0;
    sr->error = 0;
    sr->opts = 0L;

    sr->code = code;
    sr->dlg = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    set_open_dialog(sr->dlg);

    gtk_window_set_title(GTK_WINDOW(sr->dlg), title);

    dlgheight *= gui_scale;
    gtk_window_set_default_size(GTK_WINDOW(sr->dlg), -1, dlgheight); 

    g_signal_connect (G_OBJECT(sr->dlg), "destroy", 
		      G_CALLBACK(destroy_selector), 
		      sr); 

    /* create equivalent of gtkdialog structure */
    base = gtk_vbox_new(FALSE, 5);
    gtk_container_add(GTK_CONTAINER(sr->dlg), base);
    gtk_widget_show(base);

    sr->vbox = gtk_vbox_new(FALSE, 0);
    gtk_widget_show(sr->vbox);

    /* make (upper) vbox expansible */
    gtk_box_pack_start(GTK_BOX(base), sr->vbox, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(sr->vbox), 5);
    gtk_box_set_spacing(GTK_BOX(sr->vbox), 5);

    hsep = gtk_hseparator_new ();
    gtk_widget_show(hsep);
    gtk_box_pack_start(GTK_BOX(base), hsep, FALSE, FALSE, 0);

    sr->action_area = gtk_hbox_new(FALSE, 0);
    gtk_widget_show(sr->action_area);

    /* hbox for buttons is not expansible */
    gtk_box_pack_start(GTK_BOX(base), sr->action_area, 
		       FALSE, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(sr->action_area), 5);
    gtk_box_set_spacing(GTK_BOX(sr->action_area), 5);
    gtk_box_set_homogeneous(GTK_BOX(sr->action_area), TRUE);
} 

static void robust_callback (GtkWidget *w, selector *sr)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	sr->opts |= OPT_R;
    } else {
	sr->opts &= ~OPT_R;
    }
}

static void robust_config_button (GtkWidget *w, GtkWidget *b)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	gtk_widget_set_sensitive(b, TRUE);
    } else {
	gtk_widget_set_sensitive(b, FALSE);
    }
}

static void verbose_callback (GtkWidget *w, selector *sr)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	sr->opts |= OPT_V;
    } else {
	sr->opts &= ~OPT_V;
    }
}

static void corc_callback (GtkWidget *w, selector *sr)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	sr->opts &= ~OPT_B;
    } else {
	sr->opts |= OPT_B;
    }
}

static void engle_granger_callback (GtkWidget *w, selector *sr)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	sr->opts &= ~OPT_N;
    } else {
	sr->opts |= OPT_N;
    }
}

#ifdef HAVE_X12A
static void x12a_callback (GtkWidget *w, selector *sr)
{
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w))) {
	sr->opts |= OPT_X;
    } else {
	sr->opts &= ~OPT_X;
    }
}
#endif

static GtkWidget *spinner_aux_label (int i)
{
    GtkWidget *hbox;
    GtkWidget *lbl;

    hbox = gtk_hbox_new(FALSE, 5);

    if (i == 0) {
	lbl = gtk_label_new(_("Non-seasonal"));
    } else {
	lbl = gtk_label_new(_("Seasonal"));
    }

    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 5);
    gtk_widget_show(lbl);

    return hbox;
}

static GtkWidget *spinner_label (int i, int code)
{
    const char *arma_strs[] = {
	N_("AR order:"),
	N_("MA order:")
    };
    const char *arch_strs[] = {
	N_("ARCH p:"),
	N_("ARCH q:")
    };
    GtkWidget *lbl = NULL;

    if (code == ARMA) {
	lbl = gtk_label_new(_(arma_strs[i % 2]));
    } else {
	lbl = gtk_label_new(_(arch_strs[i]));
    }

    return lbl;
}

static void build_pq_spinners (selector *sr)
{
    GtkWidget *hbox, *tmp;
    GtkObject *adj;
    gdouble val;
    int i, imax = 2;

    if (sr->code == ARMA && datainfo->pd > 1) {
	imax = 4;
    }

    hbox = gtk_hbox_new(FALSE, 5);

    for (i=0; i<imax; i++) {
	if (i == 2) {
	    gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 5);
	    gtk_widget_show(hbox);
	    hbox = gtk_hbox_new(FALSE, 5);
	}
	if (imax > 2 && i % 2 == 0) {
	    tmp = spinner_aux_label(i);
	    gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
	    gtk_widget_show(tmp);
	}	   
	tmp = spinner_label(i, sr->code);
	gtk_box_pack_start(GTK_BOX(hbox), tmp, FALSE, FALSE, 5);
	gtk_widget_show(tmp);

	val = (i < 2)? 1 : 0;
	adj = gtk_adjustment_new(val, 0, 4, 1, 1, 1);
	sr->extra[i] = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
	gtk_box_pack_start(GTK_BOX(hbox), sr->extra[i], FALSE, FALSE, 5);
	gtk_widget_show(sr->extra[i]);
    }

    gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 5);
    gtk_widget_show(hbox);
}

static void hc_config (GtkWidget *w, gpointer p)
{
    options_dialog(p, 4, NULL);
}

static void pack_switch (GtkWidget *b, GtkWidget *vbox, gboolean dflt)
{
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);

    gtk_box_pack_start(GTK_BOX(hbox), b, TRUE, TRUE, 0);
    gtk_widget_show(b);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), dflt);
}

static void 
build_selector_switches (selector *sr) 
{
    GtkWidget *hbox, *tmp;

    if (sr->code == OLS || sr->code == GARCH || 
	sr->code == TSLS || sr->code == VAR) {
	GtkWidget *b1, *b2;

	tmp = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(sr->vbox),
			   tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);

	b1 = gtk_check_button_new_with_label(_("Robust standard errors"));
	g_signal_connect(G_OBJECT(b1), "toggled",
			 G_CALLBACK(robust_callback), sr);
	if (using_hc_by_default()) {
	    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b1), TRUE);
	}

	hbox = gtk_hbox_new(FALSE, 5);

	gtk_box_pack_start(GTK_BOX(hbox), b1, FALSE, FALSE, 0);
	gtk_widget_show(b1);

	b2 = gtk_button_new_with_label(_("configure"));
	g_signal_connect(G_OBJECT(b2), "clicked",
			 G_CALLBACK(hc_config), sr);
	gtk_widget_set_sensitive(b2, using_hc_by_default());

	g_signal_connect(G_OBJECT(b1), "toggled",
			 G_CALLBACK(robust_config_button), b2);	

	gtk_box_pack_start(GTK_BOX(hbox), b2, FALSE, FALSE, 0);
	gtk_widget_show(b2);

	gtk_box_pack_start(GTK_BOX(sr->vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show(hbox);
    }

    if (sr->code == TOBIT || sr->code == ARMA || sr->code == GARCH ||
	sr->code == COINT2) {
	if (sr->code == COINT2) {
	    tmp = gtk_check_button_new_with_label(_("Show details of regressions"));
	} else {
	    tmp = gtk_check_button_new_with_label(_("Show details of iterations"));
	}
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(verbose_callback), sr);
	pack_switch(tmp, sr->vbox, FALSE);
    } else if (sr->code == HILU) {
	tmp = gtk_check_button_new_with_label(_("Fine-tune using Cochrane-Orcutt"));
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(corc_callback), sr);
	pack_switch(tmp, sr->vbox, TRUE);
    } else if (sr->code == COINT) {
	tmp = gtk_check_button_new_with_label
	    (_("Cointegrating regression includes a constant"));
	g_signal_connect(G_OBJECT(tmp), "toggled", 
			 G_CALLBACK(engle_granger_callback), sr);
	pack_switch(tmp, sr->vbox, TRUE);
    }

#ifdef HAVE_X12A    
    if (sr->code == ARMA) {
	tmp = gtk_check_button_new_with_label(_("Use X-12-ARIMA"));
	g_signal_connect(G_OBJECT(tmp), "toggled", G_CALLBACK(x12a_callback), sr);
	pack_switch(tmp, sr->vbox, FALSE);
    }	
#endif
} 

static void 
build_selector_buttons (selector *sr, void (*okfunc)())
{
    GtkWidget *tmp;

    tmp = gtk_button_new_from_stock (GTK_STOCK_OK);
    GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(construct_cmdlist), sr);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(okfunc), sr);
    g_signal_connect(G_OBJECT (tmp), "clicked", 
		     G_CALLBACK(maybe_delete_dialog), sr);

    gtk_widget_show(tmp);
    gtk_widget_grab_default(tmp);

    tmp = gtk_button_new_from_stock (GTK_STOCK_CLEAR);
    GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked", 
		     G_CALLBACK(clear_vars), sr);
    gtk_widget_show(tmp);

    tmp = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
    GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(tmp), "clicked",
		     G_CALLBACK(cancel_selector), sr);
    gtk_widget_show(tmp);

    if (sr->code != PRINT && !SAVE_DATA_ACTION(sr->code)) {
	tmp = gtk_button_new_from_stock (GTK_STOCK_HELP);
	GTK_WIDGET_SET_FLAGS(tmp, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(sr->action_area), tmp, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT (tmp), "clicked", 
			 G_CALLBACK(context_help), 
			 GINT_TO_POINTER(sr->code));
	gtk_widget_show(tmp);
    }
}

void selection_dialog (const char *title, void (*okfunc)(), guint cmdcode,
		       int preselect) 
{
    GtkWidget *open_dialog;
    GtkWidget *right_vbox, *tmp;
    GtkWidget *big_hbox;
    GtkWidget *button_vbox;
    GtkListStore *store;
    GtkTreeIter iter;
    selector *sr;
    gchar *topstr;
    int i;

    open_dialog = get_open_dialog();
    if (open_dialog != NULL) {
	gdk_window_raise(open_dialog->window);
	return;
    }

    sr = mymalloc(sizeof *sr);
    if (sr == NULL) return;

    selector_init(sr, cmdcode, title);

    if (MODEL_CODE(cmdcode) || COINT_CODE(cmdcode))
	topstr = _(est_str(cmdcode));
    else if (cmdcode == GR_XY)
	topstr = _("XY scatterplot");
    else if (cmdcode == GR_IMP)
	topstr = _("plot with impulses");
    else if (cmdcode == GR_3D)
	topstr = _("3D plot");
    else if (cmdcode == SCATTERS)
	topstr = _("multiple scatterplots");
    else if (cmdcode == GR_DUMMY)
	topstr = _("factorized plot");
    else
	topstr = "fixme need string";

    tmp = gtk_label_new(topstr);
    gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    /* the following encloses LHS varlist, depvar and indepvar stuff */
    big_hbox = gtk_hbox_new(FALSE, 5); 

    /* LHS: list of vars to choose from */
    sr->varlist = var_list_box_new(GTK_BOX(big_hbox), sr, SR_VARLIST);
    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->varlist)));
    gtk_list_store_clear (store);
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL(store), &iter);
    
    for (i=0; i<datainfo->v; i++) {
	if (i == 0 && !MODEL_CODE(cmdcode)) {
	    continue;
	}
        if (is_hidden_variable(i, datainfo)) {
	    continue;
	}
	if (screen_scalar(i, cmdcode)) {
	    continue;
	}
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter, 0, i, 1, datainfo->varname[i], -1);
    }

    /* RHS: vertical holder */
    right_vbox = gtk_vbox_new(FALSE, 5);

    tmp = gtk_hseparator_new();
    gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
    gtk_widget_show(tmp);

    if (MODEL_CODE(cmdcode)) { 
	/* models: top right -> dependent variable */
	build_depvar_section(sr, right_vbox, preselect);
    } else if (cmdcode == GR_XY || cmdcode == GR_IMP || cmdcode == GR_DUMMY
	       || cmdcode == SCATTERS || cmdcode == GR_3D) {
	/* graphs: top right -> x-axis variable */
	build_x_axis_section(sr, right_vbox);
    }

    /* middle right: used for some estimators and factored plot */
    if (cmdcode == WLS || cmdcode == AR || cmdcode == TSLS || 
	cmdcode == VAR || cmdcode == COINT || cmdcode == COINT2 || 
	cmdcode == POISSON || cmdcode == GR_DUMMY || cmdcode == GR_3D) {
	build_mid_section(sr, right_vbox);
    }
    
    if (cmdcode == GR_DUMMY) {
	/* special case: choose dummy var for factorized plot */
	dummy_box(sr, right_vbox);
    } else if (cmdcode == GR_3D) {
	/* special case: choose Z axis variable */
	zvar_box(sr, right_vbox);
    } else { 
	/* all other uses: scrollable list of vars */
	GtkWidget *remove;
	GtkWidget *indepvar_hbox;

	if (COINT_CODE(cmdcode)) {
	    tmp = gtk_label_new(_("Variables to test"));
	} else if (MODEL_CODE(cmdcode)) {
	    tmp = gtk_label_new(_("Independent variables"));
	} else if (cmdcode == GR_XY || cmdcode == GR_IMP) {
	    tmp = gtk_label_new(_("Y-axis variables"));
	} else if (cmdcode == SCATTERS) {
	    scatters_label = tmp = gtk_label_new(_("X-axis variables"));
	}
    
	gtk_box_pack_start(GTK_BOX(right_vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);

	indepvar_hbox = gtk_hbox_new(FALSE, 5);

	/* push/pull buttons first, in their own little vbox */
	button_vbox = gtk_vbox_new(TRUE, 5);

	tmp = gtk_button_new_with_label (_("Add ->"));
	gtk_box_pack_start(GTK_BOX(button_vbox), tmp, TRUE, FALSE, 0);
	g_signal_connect (G_OBJECT(tmp), "clicked", 
			  G_CALLBACK(add_to_right_callback), sr);
	gtk_widget_show(tmp);
    
	remove = gtk_button_new_with_label (_("<- Remove"));
	gtk_box_pack_start(GTK_BOX(button_vbox), remove, TRUE, FALSE, 0);
	gtk_widget_show(remove);

	gtk_box_pack_start(GTK_BOX(indepvar_hbox), button_vbox, TRUE, TRUE, 0);
	gtk_widget_show(button_vbox);

	/* then the listing */
	sr->rightvars = var_list_box_new(GTK_BOX(indepvar_hbox), sr, SR_RIGHTVARS);
	store = 
	    GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->rightvars)));
	gtk_list_store_clear (store);
	gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	if (MODEL_CODE(cmdcode)) {
	    if (cmdcode != VAR) {
		/* stick the constant in by default */
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter, 
				   0, 0, 
				   1, "const", 
				   -1);
	    }
	    if (xlist != NULL) {
		/* we have a saved list of regressors */
		for (i=1; i<=xlist[0]; i++) {
		    int xi = xlist[i];

		    if (xi == 0) {
			continue;
		    }
		    gtk_list_store_append(store, &iter);
		    gtk_list_store_set(store, &iter, 
				       0, xi, 
				       1, datainfo->varname[xi], 
				       -1);
		}
	    }
	} 

	/* hook remove button to listing */
	g_signal_connect (G_OBJECT(remove), "clicked", 
			  G_CALLBACK(remove_from_right_callback), 
			  sr->rightvars);

	/* pack the lower right stuff into the RHS vbox */
	gtk_box_pack_start(GTK_BOX(right_vbox), indepvar_hbox, TRUE, TRUE, 0);
	gtk_widget_show(indepvar_hbox);
    }

    /* pack the whole RHS to the right of the LHS varlist */
    gtk_box_pack_start(GTK_BOX(big_hbox), right_vbox, TRUE, TRUE, 0);
    gtk_widget_show(right_vbox);

    /* pack the whole central section into the dialog's vbox */
    gtk_box_pack_start(GTK_BOX(sr->vbox), big_hbox, TRUE, TRUE, 0);
    gtk_widget_show(big_hbox);

    /* AR and MA spinners for ARMA; also GARCH */
    if (sr->code == ARMA || sr->code == GARCH) {
	build_pq_spinners(sr);
    }

    /* toggle switches for some cases */
    if (WANT_TOGGLES(sr->code)) {
	build_selector_switches(sr);
    }

    /* buttons: OK, Clear, Cancel, Help */
    build_selector_buttons(sr, okfunc);

    gtk_widget_show(sr->dlg);
}

static char *get_topstr (int cmdnum)
{
    switch (cmdnum) {    
    case LOGS:
	return N_("Select variables for logging");
    case LAGS:
	return N_("Select variables for lagging");
    case SQUARE:
	return N_("Select variables to square");
    case DIFF:
	return N_("Select variables to difference");
    case LDIFF:
	return N_("Select variables to log-difference");
    case ADD:
	return N_("Select variables to add");
    case OMIT:
	return N_("Select variables to omit");
    case COEFFSUM:
	return N_("Select coefficients to sum");
    case SPEARMAN:
    case MEANTEST:
    case MEANTEST2:
    case VARTEST:
	return N_("Select two variables");
    case PRINT:
	return N_("Select variables to display");
    case GR_PLOT: 
    case GR_BOX: 
    case GR_NBOX:
	return N_("Select variables to plot");
    case SAVE_DATA:
    case SAVE_DATA_AS:
    case SAVE_GZDATA:
    case EXPORT_CSV:
    case EXPORT_R:
    case EXPORT_OCTAVE:
	return N_("Select variables to save");
    case COPY_CSV:
	return N_("Select variables to copy");
    default:
	return "";
    }
}

static void add_omit_list (gpointer p, selector *sr)
{
    windata_t *vwin = (windata_t *) p;
    MODEL *pmod = (MODEL *) vwin->data;
    GtkListStore *store;
    GtkTreeIter iter;
    int i;

    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->varlist)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    
    if (sr->code == OMIT || sr->code == COEFFSUM) {
	for (i=2; i<=pmod->list[0]; i++) {
	    if (pmod->list[i] == 0) {
		continue;
	    }
	    if (pmod->list[i] == LISTSEP) {
		break;
	    }
	    gtk_list_store_append(store, &iter);
	    gtk_list_store_set(store, &iter, 
			       0, pmod->list[i], 
			       1, datainfo->varname[pmod->list[i]],
			       -1);
	} 
    } else {
	for (i=1; i<datainfo->v; i++) {
	    int j, match = 0;

	    for (j=1; j<=pmod->list[0]; j++) {
		if (i == pmod->list[j]) {
		    match = 1;
		    break;
		}
	    }
	    if (match) continue;

	    gtk_list_store_append(store, &iter);
	    gtk_list_store_set(store, &iter, 
			       0, i, 
			       1, datainfo->varname[i],
			       -1);
	}
    }
}

static GtkWidget *selection_top_label (int code)
{
    GtkWidget *label = NULL;
    const char *str = get_topstr(code);

    if (strlen(str)) {
	label = gtk_label_new(_(str));
    } 

    return label;
}

void simple_selection (const char *title, void (*okfunc)(), guint cmdcode,
		       gpointer p) 
{
    GtkWidget *open_dialog;
    GtkWidget *left_vbox, *mid_vbox, *right_vbox, *tmp;
    GtkWidget *top_hbox, *big_hbox, *remove_button;
    GtkListStore *store;
    GtkTreeIter iter;
    selector *sr;
    int i, vnum = 0;

    open_dialog = get_open_dialog();

    if (open_dialog != NULL) {
	gdk_window_raise(open_dialog->window);
	return;
    }

    sr = mymalloc(sizeof *sr);
    if (sr == NULL) {
	return;
    }

    selector_init(sr, cmdcode, title);

    sr->data = p;

    tmp = selection_top_label(cmdcode);
    if (tmp != NULL) {
	gtk_box_pack_start(GTK_BOX(sr->vbox), tmp, FALSE, FALSE, 0);
	gtk_widget_show(tmp);
    }    

    /* for titles */
    top_hbox = gtk_hbox_new(FALSE, 0); 
    gtk_box_set_homogeneous(GTK_BOX(top_hbox), TRUE);

    tmp = gtk_label_new(_("Available vars"));
    gtk_box_pack_start(GTK_BOX(top_hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    tmp = gtk_label_new(" ");
    gtk_box_pack_start(GTK_BOX(top_hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    tmp = gtk_label_new(_("Selected vars"));
    gtk_box_pack_start(GTK_BOX(top_hbox), tmp, FALSE, FALSE, 5);
    gtk_widget_show(tmp);

    gtk_box_pack_start(GTK_BOX(sr->vbox), top_hbox, FALSE, FALSE, 5);
    gtk_widget_show(top_hbox);

    /* the following encloses 3 vboxes */
    big_hbox = gtk_hbox_new(FALSE, 5); 

    /* holds available var list */
    left_vbox = gtk_vbox_new(FALSE, 5);

    sr->varlist = var_list_box_new(GTK_BOX(left_vbox), sr, SR_VARLIST);
    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(sr->varlist)));
    gtk_list_store_clear(store);
    gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

    if (cmdcode == OMIT || cmdcode == ADD || cmdcode == COEFFSUM) {
        add_omit_list(p, sr);
    } else {
	int nleft = 0;

	for (i=1; i<datainfo->v; i++) {
	    if (is_hidden_variable(i, datainfo) || screen_scalar(i, cmdcode)) {
		continue;
	    }
	    gtk_list_store_append(store, &iter);
	    gtk_list_store_set(store, &iter, 0, i, 
			       1, datainfo->varname[i], 
			       -1);
	    vnum = i;
	    nleft++;
	}
	if (nleft != 1) {
	    vnum = 0;
	}
    }

    gtk_box_pack_start(GTK_BOX(big_hbox), left_vbox, TRUE, TRUE, 0);
    gtk_widget_show(left_vbox);
    
    /* middle: vertical holder for push/pull buttons */
    mid_vbox = gtk_vbox_new(FALSE, 5);

    sr->add_button = gtk_button_new_with_label (_("Select ->"));
    gtk_box_pack_start(GTK_BOX(mid_vbox), sr->add_button, TRUE, FALSE, 0);
    g_signal_connect (G_OBJECT(sr->add_button), "clicked", 
		      G_CALLBACK(add_to_right_callback), sr);
    gtk_widget_show(sr->add_button);

    if (p == NULL && !TWO_VARS_CODE(sr->code)) {
	/* data save action */
	tmp = gtk_button_new_with_label (_("All ->"));
	gtk_box_pack_start(GTK_BOX(mid_vbox), tmp, TRUE, FALSE, 0);
	g_signal_connect (G_OBJECT(tmp), "clicked", 
			  G_CALLBACK(add_all_to_right_callback), sr);
	gtk_widget_show(tmp);
    }
    
    remove_button = gtk_button_new_with_label (_("<- Remove"));
    gtk_box_pack_start(GTK_BOX(mid_vbox), remove_button, TRUE, FALSE, 0);
    gtk_widget_show(remove_button);

    gtk_box_pack_start(GTK_BOX(big_hbox), mid_vbox, TRUE, TRUE, 0);
    gtk_widget_show(mid_vbox);

    /* RHS: vertical holder for selected vars */
    right_vbox = gtk_vbox_new(FALSE, 5);

    sr->rightvars = var_list_box_new(GTK_BOX(right_vbox), sr, SR_RIGHTVARS);
    g_object_set_data(G_OBJECT(sr->rightvars), "selector", sr);

    gtk_box_pack_start(GTK_BOX(big_hbox), right_vbox, TRUE, TRUE, 0);
    gtk_widget_show(right_vbox);

    /* connect var removal signal */
    g_signal_connect (G_OBJECT(remove_button), "clicked", 
		      G_CALLBACK(remove_from_right_callback), 
		      sr->rightvars);

    /* pack the whole central section into the dialog's vbox */
    gtk_box_pack_start(GTK_BOX(sr->vbox), big_hbox, TRUE, TRUE, 0);
    gtk_widget_show(big_hbox);

    /* buttons: "OK", Clear, Cancel, Help */
    build_selector_buttons(sr, okfunc);

    if (TWO_VARS_CODE(sr->code) && mdata_selection_count() == 2) {
	set_vars_from_main(sr);
    } else if (SAVE_DATA_ACTION(sr->code) && vnum > 0) {
	set_single_var(sr, vnum);
    }

    gtk_widget_show(sr->dlg);

    if (SAVE_DATA_ACTION(sr->code)) {
	gretl_set_window_modal(sr->dlg);
    }
}

struct list_maker {
    char *liststr;
    int n_items;
    size_t len;
    int overflow;
};

static void selection_add_item (GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter *iter, struct list_maker *lmkr)
{
    gchar *varnum = NULL;

    if (lmkr->len > MAXLEN - 12) {
	lmkr->overflow = 1;
	return;
    }

    gtk_tree_model_get (model, iter, 0, &varnum, -1);
    strcat(lmkr->liststr, " ");
    strcat(lmkr->liststr, varnum);
    lmkr->len += strlen(varnum) + 1;
    g_free(varnum);
    lmkr->n_items += 1;
}

char *main_window_selection_as_string (void) 
{
    GtkTreeSelection *select;
    struct list_maker lmkr;

    lmkr.liststr = mymalloc(MAXLEN);
    if (lmkr.liststr == NULL) {
	return NULL;
    }

    lmkr.liststr[0] = 0;
    lmkr.n_items = lmkr.overflow = 0;
    lmkr.len = 0;

    select = gtk_tree_view_get_selection (GTK_TREE_VIEW(mdata->listbox));

    gtk_tree_selection_selected_foreach (select, 
					 (GtkTreeSelectionForeachFunc) 
					 selection_add_item,
					 &lmkr); 

    if (lmkr.overflow) {
	errbox(_("Too many items were selected"));
	lmkr.liststr[0] = 0;
	return lmkr.liststr;
    }

    return lmkr.liststr;
}

static const char *data_save_title (int code)
{
    switch (code) {
    case EXPORT_CSV:
	return _("Save CSV data file");
    case EXPORT_R:
    case EXPORT_R_ALT:
	return _("Save R data file");
    case EXPORT_OCTAVE:
	return _("Save octave data file");
    default:
	return _("Save data file");
    }
    return "";
}

static void data_save_selection_callback (GtkWidget *w, gpointer p)
{
    selector *sr = (selector *) p;
    int code = sr->code;

    if (sr->cmdlist == NULL || *sr->cmdlist == 0) {
	return;
    }

    if (storelist != NULL) {
	free(storelist);
	storelist = NULL;
    }

    storelist = g_strdup(sr->cmdlist);

    gtk_widget_destroy(sr->dlg);

    if (code != COPY_CSV) {
	file_selector(data_save_title(code), code, NULL);
    }
}

void data_save_selection_wrapper (int file_code)
{
    simple_selection((file_code == COPY_CSV)? 
		     _("Copy data") : _("Save data"), 
		     data_save_selection_callback, file_code, 
		     NULL);
    gtk_main(); /* the corresponding gtk_main_quit() is in
		   the function destroy_selector() */
}

/* accessor functions */

int selector_code (const selector *sr)
{
    return sr->code;
}

const char *selector_list (const selector *sr)
{
    const char *ret = NULL;

    if (sr->cmdlist != NULL && *sr->cmdlist != '\0') {
	ret = sr->cmdlist;
    }

    return ret;
}

int selector_list_hasconst (const selector *sr)
{
    int hc = sr->cmdlist != NULL && 
	strstr(sr->cmdlist, " 0") != NULL;

    return hc;
}

gpointer selector_get_data (const selector *sr)
{
    return sr->data;
}

gretlopt selector_get_opts (const selector *sr)
{
    return sr->opts;
}

int selector_error (const selector *sr)
{
    return sr->error;
}

void maybe_clear_selector (const int *dlist)
{
    int i, j;

    if (xlist != NULL) {
	for (i=1; i<=xlist[0]; i++) {
	    for (j=1; j<=dlist[0]; j++) {
		if (xlist[i] >= dlist[j]) {
		    clear_selector();
		    return;
		}
	    }
	}
    }
}
