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

/* session.c for gretl */

#include "gretl.h"
#include "session.h"
#include "selector.h"
#include "boxplots.h"
#include "ssheet.h"
#include "gpt_control.h"
#include "guiprint.h"
#include "model_table.h"

#include "var.h"

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#ifdef _WIN32
# include <windows.h>
#else
# if !GLIB_CHECK_VERSION(2,0,0)
#  define OLD_GTK
# endif
#endif

#ifdef OLD_GTK
# include <gdk-pixbuf/gdk-pixbuf.h>
#endif

/* from gui_utils.c */
extern void winstack_init (void);
extern void winstack_destroy (void);
extern int winstack_match_data (gpointer p);

extern int replay; /* lib.c */

static void auto_save_gp (gpointer data, guint i, GtkWidget *w);

#include "../pixmaps/model.xpm"
#include "../pixmaps/boxplot.xpm"
#include "../pixmaps/gnuplot.xpm"
#include "../pixmaps/xfm_sc.xpm"
#include "../pixmaps/xfm_info.xpm"
#include "../pixmaps/xfm_text.xpm"
#include "../pixmaps/xfm_make.xpm"
#include "../pixmaps/rhohat.xpm"
#include "../pixmaps/summary.xpm"
#include "../pixmaps/model_table.xpm"

/* #define SESSION_DEBUG */

#define OBJECT_NAMELEN 12

typedef struct _SESSION SESSION;
typedef struct _SESSIONBUILD SESSIONBUILD;
typedef struct _gui_obj gui_obj;

struct _SESSION {
    char name[32];
    int nmodels;
    int ngraphs;
    int nvars;
    MODEL **models;
    GRAPHT **graphs;
    GRETL_VAR **vars;
    char *notes;
};

struct _SESSIONBUILD {
    int nmodels;
    int *model_ID;
    char **model_name;
};

struct _gui_obj {
    gchar *name;
    gint sort;
    gpointer data;
    GtkWidget *icon;
    GtkWidget *label;
    gint row, col;
};

enum {
    ICON_ADD_BATCH,
    ICON_ADD_SINGLE
};

enum {
    SAVEFILE_SESSION,
    SAVEFILE_SCRIPT,
    SAVEFILE_ERROR
};

static GtkTargetEntry model_table_drag_target = {
    "model_pointer", GTK_TARGET_SAME_APP, GRETL_MODEL_POINTER 
};

static char *global_items[] = {
    N_("Arrange icons"),
#ifndef GNUPLOT_PNG
    N_("Add last graph"),
#endif
    N_("Close window")
};

static char *model_items[] = {
    N_("Display"),
    N_("Add to model table"),
    N_("Delete")
};

static char *model_table_items[] = {
    N_("Display"),
    N_("Clear table"),
    N_("Help")
};

static char *var_items[] = {
    N_("Display"),
    N_("Delete")
};

static char *graph_items[] = {
    N_("Display"),
#ifndef GNUPLOT_PNG
    N_("Edit using GUI"),
#endif
    N_("Edit plot commands"),
    N_("Delete")
};

static char *dataset_items[] = {
    N_("Edit"),
    N_("Save..."),
    N_("Export as CSV..."),
    N_("Copy as CSV...")
};

static char *info_items[] = {
    N_("View"),
    N_("Edit"),
};

static char *session_items[] = {
    N_("Save"),
#ifdef GNUPLOT_PNG
    N_("Save As...")
#else
    N_("Save As..."),
    N_("Add last graph")
#endif
};

/* file-scope globals */

SESSION session;            /* hold models, graphs */
SESSIONBUILD rebuild;       /* rebuild session later */

static int session_file_open;

static GtkWidget *iconview;
static GtkWidget *icon_table;
static GtkWidget *global_popup;
static GtkWidget *session_popup;
static GtkWidget *model_popup;
static GtkWidget *model_table_popup;
static GtkWidget *var_popup;
static GtkWidget *graph_popup;
static GtkWidget *boxplot_popup;
static GtkWidget *data_popup;
static GtkWidget *info_popup;
static GtkWidget *addgraph;

static GList *icon_list;
static gui_obj *active_object;

/* private functions */
static void session_build_popups (void);
static void global_popup_activated (GtkWidget *widget, gpointer data);
static void session_popup_activated (GtkWidget *widget, gpointer data);
static void object_popup_activated (GtkWidget *widget, gpointer data);
static void data_popup_activated (GtkWidget *widget, gpointer data);
static void info_popup_activated (GtkWidget *widget, gpointer data);
static gui_obj *gui_object_new (gchar *name, int sort);
static gui_obj *session_add_icon (gpointer data, int sort, int mode);
static void session_delete_icon (gui_obj *gobj);
static void open_gui_model (gui_obj *gobj);
static void open_gui_var (gui_obj *gobj);
static void open_gui_graph (gui_obj *gobj);
static void open_boxplot (gui_obj *gobj);
static gboolean session_icon_click (GtkWidget *widget, 
				    GdkEventButton *event,
				    gpointer data);

/* ........................................................... */

static int rebuild_init (void)
{
    rebuild.nmodels = 0;

    rebuild.model_ID = malloc(sizeof *rebuild.model_ID);
    if (rebuild.model_ID == NULL) return 1;

    rebuild.model_name = malloc(sizeof *rebuild.model_name);
    if (rebuild.model_name == NULL) return 1;

    rebuild.model_name[0] = malloc(32);
    if (rebuild.model_name[0] == NULL) return 1;

    return 0;
}

/* ........................................................... */

static void free_rebuild (void)
{
    int i;

    if (rebuild.model_ID) free(rebuild.model_ID);

    if (rebuild.model_name) {
	for (i=0; i<rebuild.nmodels; i++) {
	    if (rebuild.model_name[i])
		free(rebuild.model_name[i]);
	}
	free(rebuild.model_name);
    }
}

/* .................................................................. */

static void edit_session_notes (void)
{
    edit_buffer(&session.notes, 80, 400, _("gretl: session notes"),
		EDIT_NOTES);
}

/* .................................................................. */

static int look_up_graph_by_name (const char *grname)
{
    int i;

    for (i=0; i<session.ngraphs; i++) {
	if (!strcmp(grname, (session.graphs[i])->name)) {
	    return i;
	}
    }
    return -1;
}

/* .................................................................. */

int real_add_graph_to_session (const char *fname, const char *grname,
			       int code)
{
    int replace = 0;
    int i = look_up_graph_by_name(grname);

    if (i >= 0) {
	replace = 1;
	(session.graphs[i])->sort = code;
	strcpy((session.graphs[i])->fname, fname);	
	(session.graphs[i])->ID = plot_count++;
    } else {
	i = session.ngraphs;

	if (session.ngraphs) {
	    session.graphs = myrealloc(session.graphs, 
				       (i + 1) * sizeof *session.graphs);
	} else {
	    session.graphs = mymalloc(sizeof *session.graphs);
	}

	if (session.graphs == NULL) return ADD_GRAPH_FAIL;

	session.graphs[i] = mymalloc(sizeof **session.graphs);
	if (session.graphs[i] == NULL) return ADD_GRAPH_FAIL;

	(session.graphs[i])->sort = code;

	strcpy((session.graphs[i])->fname, fname);
	strcpy((session.graphs[i])->name, grname);
	(session.graphs[i])->ID = plot_count++;
	session.ngraphs += 1;
    }
    
    session_changed(1);

    if (icon_list != NULL && !replace) {
	session_add_icon(session.graphs[i], 
			 (code == GRETL_GNUPLOT_GRAPH)? 'g' : 'b',
			 ICON_ADD_SINGLE);
    }

    return (replace)? ADD_GRAPH_REPLACE : ADD_GRAPH_OK;
}

/* .................................................................. */

void add_graph_to_session (gpointer data, guint code, GtkWidget *w)
{
    char pltname[MAXLEN], savedir[MAXLEN];
    char grname[32];
    int boxplot_count;
    int err = 0;
    
    errno = 0;

    get_default_dir(savedir);

    if (code == GRETL_GNUPLOT_GRAPH) {
#ifdef GNUPLOT_PNG
	GPT_SPEC *plot = (GPT_SPEC *) data;

#endif
	sprintf(pltname, "%ssession.Graph_%d", savedir, plot_count + 1);
	sprintf(grname, "%s %d", _("Graph"), plot_count + 1);
#ifdef GNUPLOT_PNG
	/* move temporary plot file to permanent */
	if (copyfile(plot->fname, pltname)) {
	    return;
	} 
	if (remove_png_term_from_plotfile(pltname, plot)) {
	    errbox(_("Failed to copy graph file"));
	    return;
	}
	remove(plot->fname);
	strcpy(plot->fname, pltname);
	mark_plot_as_saved(plot);	
#else
	if (copyfile(paths.plotfile, pltname)) {
	    return;
	} 
	remove(paths.plotfile);
#endif
    } 
    else if (code == GRETL_BOXPLOT) {
	boxplot_count = augment_boxplot_count();
	sprintf(pltname, "%ssession.Plot_%d", savedir, boxplot_count);
	sprintf(grname, "%s %d", _("Boxplot"), boxplot_count);
	if (copyfile(boxplottmp, pltname)) {
	    return;
	} 
	remove(boxplottmp);
    }
    else {
	errbox("bad code in add_graph_to_session");
	return;
    }

    err = real_add_graph_to_session(pltname, grname, code);

    if (err != ADD_GRAPH_FAIL) {
	infobox(_("Graph saved"));
    }
}

/* ........................................................... */

static int model_already_saved (MODEL *pmod)
{
    int i;

    for (i=0; i<session.nmodels; i++) {
	if (session.models[i] == pmod) {
	    return 1;
	}
    }
    return 0;
}

static int var_already_saved (GRETL_VAR *var)
{
    int i;

    for (i=0; i<session.nvars; i++) {
	if (session.vars[i] == var) {
	    return 1;
	}
    }
    return 0;
}

/* ........................................................... */

static int real_add_model_to_session (MODEL *pmod)
{
    int n = session.nmodels; 

    if (session.nmodels) {
	session.models = myrealloc(session.models, 
				   (n + 1) * sizeof *session.models);
    } else {
	session.models = mymalloc(sizeof *session.models);
    }

    if (session.models == NULL) return 1;

    session.models[n] = pmod;
    session.nmodels += 1;

    /* add model icon to session display */
    if (icon_list != NULL) {
	session_add_icon(session.models[n], 'm', ICON_ADD_SINGLE);
    }    

    return 0;
}

static int real_add_var_to_session (GRETL_VAR *var)
{
    int n = session.nvars; 

    if (session.nvars) {
	session.vars = myrealloc(session.vars, 
				   (n + 1) * sizeof *session.vars);
    } else {
	session.vars = mymalloc(sizeof *session.vars);
    }

    if (session.vars == NULL) return 1;

    session.vars[n] = var;
    session.nvars += 1;

    /* add var icon to session display */
    if (icon_list != NULL) {
	session_add_icon(session.vars[n], 'v', ICON_ADD_SINGLE);
    }    

    return 0;
}

/* ........................................................... */

void *get_session_object_by_name (const char *name, char *which)
{
    int i;

    for (i=0; i<session.nmodels; i++) {
	if (strcmp(name, (session.models[i])->name) == 0) {
	    *which = 'm';
	    return session.models[i];
	}
    }

    for (i=0; i<session.nvars; i++) {
	if (strcmp(name, gretl_var_get_name(session.vars[i])) == 0) {
	    *which = 'v';
	    return session.vars[i];
	}
    }

    for (i=0; i<session.ngraphs; i++) {
	if (strcmp(name, (session.graphs[i])->name) == 0) {
	    *which = 'g';
	    return session.graphs[i];
	}
    }

    return NULL;
}

/* ........................................................... */

int try_add_model_to_session (MODEL *pmod)
{
    if (model_already_saved(pmod)) return 1;
    if (real_add_model_to_session(pmod)) return 1;
    return 0;
}

int try_add_var_to_session (GRETL_VAR *var)
{
    if (var_already_saved(var)) return 1;
    if (real_add_var_to_session(var)) return 1;
    return 0;
}

/* ........................................................... */

void remember_model (gpointer data, guint close, GtkWidget *widget)
     /* called directly from model window */
{
    windata_t *mydata = (windata_t *) data;
    MODEL *pmod = (MODEL *) mydata->data;
    gchar *buf;

    if (pmod == NULL) return;

    if (model_already_saved(pmod)) {
	infobox(_("Model is already saved"));
	return;
    }

    pmod->name = g_strdup_printf("%s %d", _("Model"), pmod->ID);

    if (real_add_model_to_session(pmod)) return;

    buf = g_strdup_printf(_("%s saved"), pmod->name);
    infobox(buf);
    g_free(buf);

    session_changed(1);

    /* close model window */
    if (close) {
	gtk_widget_destroy(gtk_widget_get_toplevel(GTK_WIDGET(mydata->w)));
    } 
}

/* ........................................................... */

void remember_var (gpointer data, guint close, GtkWidget *widget)
     /* called directly from VAR window */
{
    windata_t *mydata = (windata_t *) data;
    GRETL_VAR *var = (GRETL_VAR *) mydata->data;
    gchar *buf;

    if (var == NULL) return;

    if (var_already_saved(var)) {
	infobox(_("VAR is already saved"));
	return;
    }

    gretl_var_assign_name(var);

    if (real_add_var_to_session(var)) return;

    buf = g_strdup_printf(_("%s saved"), gretl_var_get_name(var));
    infobox(buf);
    g_free(buf);

    session_changed(1);

    /* close VAR window */
    if (close) {
	gtk_widget_destroy(gtk_widget_get_toplevel(GTK_WIDGET(mydata->w)));
    } 
}

/* ........................................................... */

int session_changed (int set)
{
    static int has_changed;
    int orig;

    orig = has_changed;
    has_changed = set;
    return orig;
}

/* ........................................................... */

void session_init (void)
{
    session.models = NULL;
    session.vars = NULL;
    session.graphs = NULL;
    session.notes = NULL;
    session.nmodels = 0;
    session.ngraphs = 0;
    session.nvars = 0;
    *session.name = '\0';

    session_changed(0);
    winstack_init();
    session_file_manager(CLEAR_DELFILES, NULL);
}

/* ........................................................... */

void do_open_session (GtkWidget *w, gpointer data)
{
    dialog_t *d = NULL;
    windata_t *fwin = NULL;
    FILE *fp;
    int status;

    if (data != NULL) {    
	if (w == NULL) /* not coming from edit_dialog */
	    fwin = (windata_t *) data;
	else {
	    d = (dialog_t *) data;
	    fwin = (windata_t *) d->data;
	}
    }

    fp = fopen(tryscript, "r");
    if (fp != NULL) {
	fclose(fp);
	strcpy(scriptfile, tryscript);
    } else {
	gchar *errbuf;

	errbuf = g_strdup_printf(_("Couldn't open %s\n"), tryscript);
	errbox(errbuf);
	g_free(errbuf);
	delete_from_filelist(FILE_LIST_SESSION, tryscript);
	delete_from_filelist(FILE_LIST_SCRIPT, tryscript);
	return;
    }

    clear_data();
    free_session();
    session_init();

    fprintf(stderr, I_("\nReading session file %s\n"), scriptfile);

    status = parse_savefile(scriptfile);
    if (status == SAVEFILE_ERROR) return;
    if (status == SAVEFILE_SCRIPT) {
	do_open_script();
	return;
    }

    if (recreate_session(scriptfile)) 
	return;

    mkfilelist(FILE_LIST_SESSION, scriptfile);

    endbit(session.name, scriptfile, 0);

    /* pick up session notes, if any */
    if (status == SAVEFILE_SESSION) {
	char notesfile[MAXLEN];
	struct stat buf;

	switch_ext(notesfile, scriptfile, "Notes");
	if (stat(notesfile, &buf) == 0 && (fp = fopen(notesfile, "r"))) { 
	    char notesline[MAXLEN];

	    /* read into buffer */
	    session.notes = mymalloc(buf.st_size + 1);
	    if (session.notes == NULL) {
		fclose(fp);
		return;
	    }
	    session.notes[0] = '\0';
	    while (fgets(notesline, MAXLEN-1, fp)) {
		strcat(session.notes, notesline);
	    } 
	    fclose(fp);
	}
    }

    /* trash the practice files window that launched the query? */
    if (fwin) gtk_widget_destroy(fwin->w);    

    /* sync gui with session */
    session_file_open = 1;
    session_menu_state(TRUE);
    view_session();
}

/* ........................................................... */

void verify_clear_data (void)
{
    if (!expert) {
        if (yes_no_dialog ("gretl",                      
			   _("Clearing the data set will end\n"
			     "your current session.  Continue?"), 
			   0) != GRETL_YES) {
            return;
	}
    }
    close_session();
}

/* ........................................................... */

void free_session (void)
{
    int i;

    if (session.models) {
	for (i=0; i<session.nmodels; i++) {
	    free_model(session.models[i]);
	}
	free(session.models);
	session.models = NULL;
    }

    if (session.vars) {
	for (i=0; i<session.nvars; i++) {
	    gretl_var_free(session.vars[i]);
	}
	free(session.vars);
	session.vars = NULL;
    }

    if (session.graphs) {
	free(session.graphs);
	session.graphs = NULL;
    }

    if (session.notes) {
	free(session.notes);
	session.notes = NULL;
    }

    session.nmodels = 0;
    session.nvars = 0;
    session.ngraphs = 0;
    *session.name = '\0';
}
/* ........................................................... */

int session_file_is_open (void)
{
    return session_file_open;
}

/* ........................................................... */

void close_session (void)
{
    clear_data(); /* in gtk1 version: clear_data(1); */
    free_session();
    free_model_table_list();

    session_menu_state(FALSE);
    session_file_open = 0;
    if (iconview != NULL) 
	gtk_widget_destroy(iconview);
    session_changed(0);
    winstack_destroy();
    clear_selector();

    plot_count = 0;
    zero_boxplot_count();
}

/* ........................................................... */

#ifdef SESSION_DEBUG
void print_session (void)
{
    int i;

    printf("Session contains %d models:\n", session.nmodels);
    for (i=0; i<session.nmodels; i++) {
	printf("model %d %s\n", (session.models[i])->ID,
	       (session.models[i])->name);
    }
    printf("Session contains %d vars:\n", session.nvars);
    for (i=0; i<session.nvars; i++) {
	printf("var '%s'\n", gretl_var_get_name(session.vars[i]));
    }
    printf("Session contains %d graphs:\n", session.ngraphs);
    for (i=0; i<session.ngraphs; i++) {
	printf("graph: %s (%s)\n", (session.graphs[i])->name,
	       (session.graphs[i])->fname);
    }
}
#endif

/* ........................................................... */

int saved_objects (char *fname)
     /* see if there are saved objects from a previous session */
{
    FILE *fp;
    char line[MAXLEN];
    int saves = 0;

#ifdef SESSION_DEBUG
    fprintf(stderr, "saved_objects: checking %s\n", fname);
#endif    

    fp = fopen(fname, "r");
    if (fp == NULL) return -1;

    /* check for objects */
    while (fgets(line, MAXLEN - 1, fp)) {
	if (strncmp(line, "(* saved objects:", 17) == 0) {
	    saves = 1;
	    break;
	}
    }

    fclose(fp);
    return saves;
}

/* ........................................................... */

static int check_session_graph (const char *line, 
				char **name, char **fname)
{
    char *p;
    size_t len, lenmin = 24;
    FILE *fp;

    p = strchr(line, '"') + 1;
    if (p == NULL) {
	errbox(_("Warning: session file is corrupted"));
	return 1;
    }
    len = strcspn(p, "\"");

    if (len + 1 < lenmin) lenmin = len + 1;

    *name = malloc(lenmin);
    **name = 0;
    strncat(*name, p, lenmin - 1);

    p = strchr(p, '"') + 1; 
    if (p == NULL) {
	errbox(_("Warning: session file is corrupted"));
	free(*name);
	return 1;
    }

    *fname = g_strdup(p);
    top_n_tail(*fname);

    fp = fopen(*fname, "r");
    if (fp == NULL) {
	gchar *msg;

	msg = g_strdup_printf(_("Warning: couldn't open graph file %s"), *fname);
	errbox(msg);
	g_free(msg);		      
	free(*name);
	free(*fname);
	return 1;
    }

    fclose(fp);

    return 0;
}

#define OBJECT_IS_MODEL(object) (strcmp(object, "model") == 0)
#define OBJECT_IS_GRAPH(object) (strcmp(object, "graph") == 0)
#define OBJECT_IS_PLOT(object) (strcmp(object, "plot") == 0)

/* ........................................................... */

int parse_savefile (char *fname)
{
    FILE *fp;
    char line[MAXLEN], object[7], *tmp;
    int id, i, j, k, n;

    fp = fopen(fname, "r");
    if (fp == NULL) return SAVEFILE_ERROR;

    /* find saved objects */
    k = 0;
    while (fgets(line, MAXLEN - 1, fp)) {
	if (strncmp(line, "(* saved objects:", 17) == 0) {
	    k = 1;
	    break;
	}
    }
    if (!k) { /* no saved objects: just a regular script */
	fclose(fp);
	return SAVEFILE_SCRIPT;
    }

    if (rebuild_init()) {
	errbox(_("Out of memory!"));
	return SAVEFILE_ERROR;
    }

#ifdef SESSION_DEBUG
    fprintf(stderr, "parse_savefile (%s): got saved objects\n", fname);
#endif

    i = 0; /* models */
    k = 0; /* graphs */
    while (fgets(line, MAXLEN - 1, fp)) {
	if (strncmp(line, "*)", 2) == 0) break;

	if (sscanf(line, "%6s %d", object, &id) != 2) {
	    errbox(_("Session file is corrupted, ignoring"));
	    fclose(fp);
	    return SAVEFILE_ERROR;
	}

	if (OBJECT_IS_MODEL(object)) {
	    rebuild.nmodels += 1;
#ifdef SESSION_DEBUG
	    fprintf(stderr, "got a model to rebuild (%d)\n"
		    "rebuild.nmodels now = %d\n", id, rebuild.nmodels);
#endif
	    if (i > 0) {
		rebuild.model_ID = myrealloc(rebuild.model_ID,
					     rebuild.nmodels * 
					     sizeof *rebuild.model_ID);
		rebuild.model_name = 
		    myrealloc(rebuild.model_name,
			      rebuild.nmodels * 
			      sizeof *rebuild.model_name);
		rebuild.model_name[i] = mymalloc(32);
		if (rebuild.model_ID == NULL ||
		    rebuild.model_name == NULL ||
		    rebuild.model_name[i] == NULL) {
		    fclose(fp);
		    return SAVEFILE_ERROR;
		}
	    }
	    rebuild.model_ID[i] = id;
	    tmp = strchr(line, '"') + 1;
	    *rebuild.model_name[i] = '\0';
	    strncat(rebuild.model_name[i], tmp, 31);
	    n = strlen(rebuild.model_name[i]);
	    for (j=n; j>0; j--) {
		if (rebuild.model_name[i][j] == '"') {
		    rebuild.model_name[i][j] = '\0';
		    break;
		}
	    }
	    i++;
	    continue;
	}

	if (OBJECT_IS_GRAPH(object) || OBJECT_IS_PLOT(object)) {
	    char *grname, *grfilename;

	    if (check_session_graph(line, &grname, &grfilename)) {
		continue;
	    }
	    session.ngraphs += 1;
	    if (k > 0) {
		session.graphs = myrealloc(session.graphs,
					   session.ngraphs * 
					   sizeof *session.graphs);
	    } else {
		session.graphs = mymalloc(sizeof *session.graphs);
	    }
	    if (session.graphs == NULL) {
		fclose(fp);
		return SAVEFILE_ERROR;
	    }
	    session.graphs[k] = mymalloc(sizeof(GRAPHT));
	    if (session.graphs[k] == NULL) {
		fclose(fp);
		return SAVEFILE_ERROR;
	    }

	    strcpy((session.graphs[k])->name, grname);
	    strcpy((session.graphs[k])->fname, grfilename);
	    free(grname);
	    free(grfilename);

#ifdef SESSION_DEBUG
	    fprintf(stderr, "got graph: '%s'\n", (session.graphs[k])->fname);
#endif
	    (session.graphs[k])->ID = plot_count++;

	    if (OBJECT_IS_PLOT(object)) {
		(session.graphs[k])->sort = GRETL_BOXPLOT;
		augment_boxplot_count();
	    } else {
		(session.graphs[k])->sort = GRETL_GNUPLOT_GRAPH;
	    }
	    
	    k++;
	    continue;
	} else {
	    errbox(_("Session file is corrupted, ignoring"));
	    fclose(fp);
	    return SAVEFILE_ERROR;
	}
    }
#ifdef SESSION_DEBUG
    fprintf(stderr, "session.ngraphs = %d\n", session.ngraphs);
#endif
    fclose(fp);

    return SAVEFILE_SESSION;
}

/* ........................................................... */

int recreate_session (char *fname)
     /* called on start-up when a "session" file is loaded */
{
    PRN *prn = gretl_print_new(GRETL_PRINT_NULL, NULL);

#ifdef SESSION_DEBUG
    fprintf(stderr, "recreate_session: fname = %s\n", fname);
    print_session();
#endif

    if (execute_script(fname, NULL, prn, REBUILD_EXEC)) 
	errbox(_("Error recreating session"));
#ifdef SESSION_DEBUG
    fprintf(stderr, "recreate_session: after execute_script()\n");
    print_session();
#endif
    free_rebuild();
    gretl_print_destroy(prn);

    replay = 1; /* no fresh commands have been entered yet */
    return 0;
}

/* ........................................................... */

static void set_addgraph_mode (void)
{
    GtkWidget *gmenu = 
	gtk_item_factory_get_item(mdata->ifac, "/Session/Add last graph");

    if (gmenu == NULL || addgraph == NULL) return;

    gtk_widget_set_sensitive(addgraph, GTK_WIDGET_IS_SENSITIVE(gmenu));
}

/* ........................................................... */

void save_session_callback (GtkWidget *w, guint code, gpointer data)
{
    if (code == SAVE_AS_IS && session_file_open && scriptfile[0]) {
	save_session(scriptfile);
	session_changed(0);
    } else {
	file_selector(_("Save session"), SAVE_SESSION, NULL);
    }
}

/* ........................................................... */

#ifdef not_yet
void delete_session_callback (GtkWidget *w, guint i, gpointer data)
{
    dummy_call();
}
#endif

/* ........................................................... */

static void store_list (int *list, char *buf)
{
    int i;
    char numstr[5];

    for (i=1; i<=list[0]; i++) {
        sprintf(numstr, "%d ", list[i]);
        strcat(buf, numstr);
    }
}

/* ........................................................... */

static char *model_cmd_str (MODEL *pmod)
{
    char *str;
    
    str = malloc(MAXLEN);
    if (str == NULL) return NULL;

    sprintf(str, "%s ", gretl_commands[pmod->ci]);

    if (pmod->ci == AR) {
        store_list(pmod->arinfo->arlist, str);
        strcat(str, "; ");
    }
    store_list(pmod->list, str);    

    return str;
}

/* ........................................................... */

static gchar *graph_str (GRAPHT *graph)
{
    FILE *fp;
    gchar *buf = NULL;

    fp = fopen(graph->fname, "r");

    if (fp != NULL) {
	char xlabel[24], ylabel[24], line[48];
	int gotxy = 0;

	while (fgets(line, 47, fp) && gotxy < 2) {
	    if (strstr(line, "# timeseries")) {
		break;
	    }
	    else if (sscanf(line, "set xlabel %23s", xlabel) == 1) {
		gotxy++;
	    }
	    else if (sscanf(line, "set ylabel %23s", ylabel) == 1) {
		gotxy++;
	    }
	}
	if (gotxy == 2) {
#ifdef OLD_GTK
	    buf = g_strdup_printf("%s %s %s", ylabel, _("versus"), xlabel);
#else
	    char *str = malloc(64);
	    gsize bytes;

	    if (str != NULL) {
		sprintf(str, "%s %s %s", ylabel, _("versus"), xlabel);
		buf = g_locale_to_utf8(str, -1, NULL, &bytes, NULL);
		free(str);
	    }
#endif /* OLD_GTK */
	}
	fclose(fp);
    }
    return buf;
}

/* ........................................................... */

static char *boxplot_str (GRAPHT *graph)
{
    FILE *fp;
    char *str = NULL;

    fp = fopen(graph->fname, "r");
    if (fp != NULL) {
	char vname[VNAMELEN], line[48];

	str = malloc (MAXLEN);
	if (str == NULL) return NULL;
	*str = '\0';

	while (fgets(line, 47, fp) && strlen(str) < MAXLEN-48) {
	    chopstr(line);
	    if (sscanf(line, "%*d varname = %8s", vname) == 1) { 
		strcat(str, strchr(line, '=') + 2);
		strcat(str, " ");
	    }
	}
	fclose(fp);
    }
    return str;
}

/* ........................................................... */

static void open_gui_model (gui_obj *gobj)
{ 
    PRN *prn;
    MODEL *pmod = (MODEL *) gobj->data;

    if (bufopen(&prn)) return;

    if (printmodel(pmod, datainfo, prn))
	pmod->errcode = E_NAN;
    view_model(prn, pmod, 78, 400, gobj->name);
}

/* ........................................................... */

static void open_gui_var (gui_obj *gobj)
{ 
    PRN *prn;
    GRETL_VAR *var = (GRETL_VAR *) gobj->data;

    if (bufopen(&prn)) return;

    gretl_var_print(var, datainfo, prn);
    view_buffer(prn, 78, 450, gobj->name, VAR, var);
}

/* ........................................................... */

static void open_boxplot (gui_obj *gobj)
{
    GRAPHT *graph = (GRAPHT *) gobj->data;

    retrieve_boxplot(graph->fname);
}

/* ........................................................... */

static void delete_delfiles (const gchar *fname, gpointer p)
{
    remove (fname);
}

/* ........................................................... */

void session_file_manager (int action, const char *fname)
{
    static GList *delfiles;

    if (action == SCHEDULE_FOR_DELETION) {
	gchar *delname;

	delname = g_strdup(fname);
	delfiles = g_list_append(delfiles, delname);
    }

    else if (action == REALLY_DELETE_ALL) {
	if (delfiles != NULL) {
	    g_list_foreach(delfiles, (GFunc) delete_delfiles, NULL);
	    g_list_free(delfiles);
	    delfiles = NULL;
	}
    }

    else if (action == CLEAR_DELFILES) {
	if (delfiles != NULL) {
	    g_list_free(delfiles);
	    delfiles = NULL;
	}
    }	     
}

/* ........................................................... */

static int real_delete_model_from_session (MODEL *junk)
{
    remove_from_model_table_list(junk);

    if (session.nmodels == 1) {
	free_model(session.models[0]);
    } else {
	MODEL **ppmod;
	int i, j;

	ppmod = mymalloc((session.nmodels - 1) * sizeof *ppmod);
	if (session.nmodels > 1 && ppmod == NULL) {
	    return 1;
	}
	j = 0;
	for (i=0; i<session.nmodels; i++) {
	    if ((session.models[i])->ID != junk->ID) {
		ppmod[j++] = session.models[i];
	    } else {
		free_model(session.models[i]);
	    }
	}
	free(session.models);
	session.models = ppmod;
    }

    session.nmodels -= 1;
    session_changed(1);

    return 0;
}

/* ........................................................... */

static int real_delete_var_from_session (GRETL_VAR *junk)
{
    if (session.nvars == 1) {
	gretl_var_free(session.vars[0]);
    } else {
	GRETL_VAR **ppvar;
	int i, j;

	ppvar = mymalloc((session.nvars - 1) * sizeof *ppvar);
	if (session.nvars > 1 && ppvar == NULL) {
	    return 1;
	}
	j = 0;
	for (i=0; i<session.nvars; i++) {
	    if (session.vars[i] != junk) {
		ppvar[j++] = session.vars[i];
	    } else {
		gretl_var_free(session.vars[i]);
	    }
	}
	free(session.vars);
	session.vars = ppvar;
    }

    session.nvars -= 1;
    session_changed(1);

    return 0;
}

/* ........................................................... */

static int real_delete_graph_from_session (GRAPHT *junk)
{
    int i, j;

    if (session.ngraphs == 1) {
	session_file_manager(SCHEDULE_FOR_DELETION,
			     (session.graphs[0])->fname);
	free(session.graphs[0]);
    } else {
	GRAPHT **ppgr;

	ppgr = mymalloc((session.ngraphs - 1) * sizeof *ppgr);
	if (session.ngraphs > 1 && ppgr == NULL) {
	    return 1;
	}
	j = 0;
	for (i=0; i<session.ngraphs; i++) {
	    if ((session.graphs[i])->ID != junk->ID) { 
		ppgr[j++] = session.graphs[i];
	    } else {
		session_file_manager(SCHEDULE_FOR_DELETION,
				     (session.graphs[i])->fname);
		free(session.graphs[i]);
	    }
	}
	free(session.graphs);
	session.graphs = ppgr;
    }

    session.ngraphs -= 1;
    session_changed(1);

    return 0;
}

/* ........................................................... */

static int delete_session_object (gui_obj *obj)
{
    if (obj->sort == 'm') { /* it's a model */
	MODEL *junk = (MODEL *) obj->data;

	real_delete_model_from_session(junk);
    }
    if (obj->sort == 'v') { /* it's a VAR */
	GRETL_VAR *junk = (GRETL_VAR *) obj->data;

	real_delete_var_from_session(junk);
    }
    else if (obj->sort == 'g' || obj->sort == 'b') { /* it's a graph */    
	GRAPHT *junk = (GRAPHT *) obj->data;

	real_delete_graph_from_session(junk);
    }

    replay = 0;

    session_delete_icon(obj);

    return 0;
}

static void maybe_delete_session_object (gui_obj *obj)
{
    gchar *msg;

    if (winstack_match_data(obj->data)) {
	errbox(_("Please close this object's window first"));
	return;
    }    

    msg = g_strdup_printf(_("Really delete %s?"), obj->name);
    if (yes_no_dialog(_("gretl: delete"), msg, 0) == GRETL_YES) {
	delete_session_object(obj);
    }

    g_free(msg);
}

#ifndef OLD_GTK

static void rename_session_model (MODEL *pmod, const char *newname)
{
    char *tmp = g_strdup(newname);

    if (tmp != NULL) {
	free(pmod->name);
	pmod->name = tmp;
    }
}

static void rename_session_var (GRETL_VAR *var, const char *newname)
{
    gretl_var_assign_specific_name(var, newname);
}

static void rename_session_graph (GRAPHT *graph, const char *newname)
{
    int i;

    for (i=0; i<session.ngraphs; i++) {
	if ((session.graphs[i])->ID == graph->ID) { 
	    (session.graphs[i])->name[0] = '\0';
	    strncat((session.graphs[i])->name, newname, 
		    sizeof (session.graphs[i])->name - 1);
	    break;
	}
    }
}

static void rename_session_object (gui_obj *obj, const char *newname)
{
    if (obj->sort == 'm') { /* it's a model */
	MODEL *pmod = (MODEL *) obj->data;

	rename_session_model(pmod, newname);
    }
    if (obj->sort == 'v') { /* it's a VAR */
	GRETL_VAR *var = (GRETL_VAR *) obj->data;

	rename_session_var(var, newname);
    }
    else if (obj->sort == 'g' || obj->sort == 'b') { /* it's a graph */    
	GRAPHT *graph = (GRAPHT *) obj->data;

	rename_session_graph(graph, newname);
    }

    free(obj->name);
    obj->name = g_strdup(newname);

    replay = 0;
}

#endif /* !OLD_GTK */

static gui_obj *get_gui_obj_from_data (void *finddata)
{
    gui_obj *gobj = NULL;
    int found = 0;

    icon_list = g_list_first(icon_list);

    while (icon_list != NULL) {
	gobj = (gui_obj *) icon_list->data;

	if (gobj->data == finddata) break;
	if (icon_list->next == NULL) break;
	icon_list = icon_list->next;
    }

    return (found)? gobj : NULL;
}

void delete_model_from_session (MODEL *pmod)
{
    gui_obj *obj;

    obj = get_gui_obj_from_data((void *) pmod);

    if (winstack_match_data(pmod)) {
	errbox(_("Please close this object's window first"));
	return;
    }

    real_delete_model_from_session(pmod);
    if (obj != NULL) {
	session_delete_icon(obj);
    }
}

void delete_var_from_session (GRETL_VAR *var)
{
    gui_obj *obj;

    obj = get_gui_obj_from_data((void *) var);

    if (winstack_match_data(var)) {
	errbox(_("Please close this object's window first"));
	return;
    }

    real_delete_var_from_session(var);
    if (obj != NULL) {
	session_delete_icon(obj);
    }
}
/* ........................................................... */

static void session_view_init (void)
{
    icon_list = NULL;
    icon_table = NULL;
}

/* ........................................................... */

static void delete_icons_at_close (gui_obj *gobj, gpointer p)
{
   if (gobj->name) g_free(gobj->name); 
}

static void session_view_free (GtkWidget *w, gpointer data)
{
    iconview = NULL;

    g_list_foreach(icon_list, (GFunc) delete_icons_at_close, NULL);

    g_list_free(icon_list);
    icon_list = NULL;
}

/* ........................................................... */

#define SESSION_VIEW_COLS 4

static void session_delete_icon (gui_obj *gobj)
{
    if (gobj == NULL) return;

    if (gobj->icon && GTK_IS_WIDGET(gobj->icon))
	gtk_container_remove(GTK_CONTAINER(icon_table), gobj->icon);
    if (gobj->label && GTK_IS_WIDGET(gobj->label))
	gtk_container_remove(GTK_CONTAINER(icon_table), gobj->label);

    free(gobj->name);

    icon_list = g_list_first(icon_list);
    icon_list = g_list_remove(icon_list, gobj);
    if (icon_list == NULL) {
	fprintf(stderr, "Bad: icon_list has gone NULL\n");
    }
}

static void foreach_delete_icons (gui_obj *gobj, gpointer p)
{
    if (gobj->icon && GTK_IS_WIDGET(gobj->icon))
	gtk_container_remove(GTK_CONTAINER(icon_table), gobj->icon);
    if (gobj->label && GTK_IS_WIDGET(gobj->label))
	gtk_container_remove(GTK_CONTAINER(icon_table), gobj->label);

    free(gobj->name);
}

/* apparatus for getting a white background */

static GdkColor *get_white (void)
{
    GdkColormap *cmap;
    static GdkColor *white;

    if (white == NULL) {
	white = malloc(sizeof *white);
	cmap = gdk_colormap_get_system();
	gdk_color_parse("white", white);
	gdk_colormap_alloc_color(cmap, white, FALSE, TRUE);
    }
    return white;
}

#ifdef OLD_GTK

static void white_bg_style (GtkWidget *widget, gpointer data)
{
     GtkRcStyle *rc_style;
     GdkColor *color = get_white();

     rc_style = gtk_rc_style_new();
     rc_style->bg[GTK_STATE_NORMAL] = *color;
     rc_style->color_flags[GTK_STATE_NORMAL] |= GTK_RC_BG;

     gtk_widget_modify_style(widget, rc_style);
     gtk_rc_style_unref(rc_style);
}

#else

static void white_bg_style (GtkWidget *widget, gpointer data)
{
    gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, get_white());
}

#endif /* OLD_GTK */


static void real_pack_icon (gui_obj *gobj, int row, int col)
{
#ifndef OLD_GTK
    GdkColor *white = get_white();
#endif

    gobj->row = row;
    gobj->col = col;

    gtk_table_attach (GTK_TABLE(icon_table), gobj->icon,
		      col, col + 1, row, row + 1,
		      GTK_EXPAND, GTK_FILL, 5, 5);
    gtk_widget_show(gobj->icon);

#ifdef OLD_GTK
    white_bg_style(gobj->icon, NULL);
#else
    gtk_widget_modify_bg(gobj->icon, GTK_STATE_NORMAL, white);
#endif

    gtk_table_attach (GTK_TABLE(icon_table), gobj->label,
		      col, col + 1, row + 1, row + 2,
		      GTK_EXPAND, GTK_FILL, 5, 5);

    gtk_widget_show(gobj->label);
}

static void pack_single_icon (gui_obj *gobj)
{
    int row, col;
    gui_obj *last_obj;

    icon_list = g_list_last(icon_list);
    last_obj = icon_list->data;
    row = last_obj->row;
    col = last_obj->col;  

    icon_list = g_list_append(icon_list, gobj);

    col++;
    if (col > 0 && (col % SESSION_VIEW_COLS == 0)) {
	col = 0;
	row += 2;
	gtk_table_resize(GTK_TABLE(icon_table), 2 * row, SESSION_VIEW_COLS);
    } 

    real_pack_icon(gobj, row, col);
}

static void batch_pack_icons (void)
{
    gui_obj *gobj;
    int row = 0, col = 0;

    icon_list = g_list_first(icon_list);

    while (icon_list != NULL) {
	gobj = (gui_obj *) icon_list->data;
	real_pack_icon(gobj, row, col);
	col++;
	if (col > 0 && (col % SESSION_VIEW_COLS == 0)) {
	    col = 0;
	    row += 2;
	    gtk_table_resize(GTK_TABLE(icon_table), 2 * row, SESSION_VIEW_COLS);
	}
	if (icon_list->next == NULL) break;
	else icon_list = icon_list->next;
    }
}

static void add_all_icons (void) 
{
    int i;

    active_object = NULL;

    if (data_status) {
	session_add_icon(NULL, 'i', ICON_ADD_BATCH);     /* data info */
	session_add_icon(NULL, 'd', ICON_ADD_BATCH);     /* data file */
	session_add_icon(NULL, 'n', ICON_ADD_BATCH);     /* session notes */
	session_add_icon(NULL, 'x', ICON_ADD_BATCH);     /* summary stats */
	session_add_icon(NULL, 'r', ICON_ADD_BATCH);     /* correlation matrix */
	session_add_icon(NULL, 't', ICON_ADD_BATCH);     /* model table */
    }

    session_add_icon(NULL, 's', ICON_ADD_BATCH);         /* script file */

#ifdef SESSION_DEBUG
    fprintf(stderr, "view_session: session.nmodels = %d\n", session.nmodels);
    fprintf(stderr, "view_session: session.nvars = %d\n", session.nvars);
    fprintf(stderr, "view_session: session.ngraphs = %d\n", session.ngraphs);
#endif

    for (i=0; i<session.nmodels; i++) {
#ifdef SESSION_DEBUG
	fprintf(stderr, "adding session.models[%d] to view\n", i);
#endif
	session_add_icon(session.models[i], 'm', ICON_ADD_BATCH);
    }

    for (i=0; i<session.nvars; i++) {
#ifdef SESSION_DEBUG
	fprintf(stderr, "adding session.vars[%d] to view\n", i);
#endif
	session_add_icon(session.vars[i], 'v', ICON_ADD_BATCH);
    }

    for (i=0; i<session.ngraphs; i++) {
#ifdef SESSION_DEBUG
	fprintf(stderr, "adding session.graphs[%d] to view\n", i);
	fprintf(stderr, "this graph is of sort %d\n", (session.graphs[i])->sort);
#endif
	/* distinguish gnuplot graphs from gretl boxplots */
	session_add_icon(session.graphs[i], 
			 ((session.graphs[i])->sort == GRETL_BOXPLOT)? 'b' : 'g',
			 ICON_ADD_BATCH);
    }

    batch_pack_icons();
}

/* ........................................................... */

static void rearrange_icons (void) 
{
    g_list_foreach(icon_list, (GFunc) foreach_delete_icons, NULL);

    g_list_free(icon_list);
    icon_list = NULL;

    add_all_icons();
}

/* ........................................................... */

static gint catch_iconview_key (GtkWidget *w, GdkEventKey *key, 
				gpointer p)
{
    if (key->keyval == GDK_q) { 
        gtk_widget_destroy(w);
    }
    return FALSE;
}

/* ........................................................... */

static void object_popup_show (gui_obj *gobj, GdkEventButton *event)
{
    GtkWidget *w = NULL;

    active_object = gobj;

    switch (gobj->sort) {
    case 'm': 
	w = model_popup; 
	break;
    case 't': 
	w = model_table_popup; 
	break;
    case 'v': 
	w = var_popup; 
	break;
    case 'g': 
	w = graph_popup; 
	break;
    case 'b': 
	w = boxplot_popup; 
	break;
    case 'd': 
	w = data_popup; 
	break;
    case 'i': 
	w = info_popup; 
	break;
    case 's': 
	set_addgraph_mode(); 
	w = session_popup; 
	break;
    default: 
	break;
    }

    gtk_menu_popup(GTK_MENU(w), NULL, NULL, NULL, NULL,
		   event->button, event->time);
}

/* ........................................................... */

static gboolean session_icon_click (GtkWidget *widget, 
				    GdkEventButton *event,
				    gpointer data)
{
    gui_obj *gobj;
    GdkModifierType mods;

    if (event == NULL) return FALSE;

    gdk_window_get_pointer(widget->window, NULL, NULL, &mods);

    if (data == NULL) { /* click on window background */
	if (mods & GDK_BUTTON3_MASK) {
	    gtk_menu_popup(GTK_MENU(global_popup), NULL, NULL, NULL, NULL,
			   event->button, event->time);
	}
	return TRUE;
    }

    gobj = (gui_obj *) data;

    if (event->type == GDK_2BUTTON_PRESS) {
	switch (gobj->sort) {
	case 'm':
	    open_gui_model(gobj); break;
	case 'v':
	    open_gui_var(gobj); break;
	case 'b':
	    open_boxplot(gobj); break;
	case 'g':
	    open_gui_graph(gobj); break;
	case 'd':
	    show_spreadsheet(NULL); break;
	case 'i':
	    open_info(NULL, 0, NULL); break;
	case 's':
	    view_script_default(); break;
	case 'n':
	    edit_session_notes(); break;
	case 't':
	    display_model_table(); break;
	case 'r':
	    do_menu_op(NULL, CORR, NULL); break;
	case 'x':
	    do_menu_op(NULL, SUMMARY, NULL); break;
	}
	return TRUE;
    }

    if (mods & GDK_BUTTON3_MASK) {
	if (gobj->sort == 'm' || gobj->sort == 'g' ||
	    gobj->sort == 'd' || gobj->sort == 'i' ||
	    gobj->sort == 's' || gobj->sort == 'b' ||
	    gobj->sort == 't' || gobj->sort == 'v') {
	    object_popup_show(gobj, (GdkEventButton *) event);
	}
	return TRUE;
    }

    return FALSE;
}
/* ........................................................... */

static void global_popup_activated (GtkWidget *widget, gpointer data)
{
    gchar *item = (gchar *) data;

    if (strcmp(item, _("Arrange icons")) == 0) 
	rearrange_icons();
    else if (strcmp(item, _("Close window")) == 0) 
	gtk_widget_destroy(iconview);
#ifndef GNUPLOT_PNG
    else if (strcmp(item, _("Add last graph")) == 0)
	add_graph_to_session(NULL, GRETL_GNUPLOT_GRAPH, NULL);
#endif
}

/* ........................................................... */

static void session_popup_activated (GtkWidget *widget, gpointer data)
{
    gchar *item = (gchar *) data;

    if (strcmp(item, _("Save")) == 0) 
	save_session_callback(NULL, SAVE_AS_IS, NULL);
    else if (strcmp(item, _("Save As...")) == 0) 
	save_session_callback(NULL, SAVE_RENAME, NULL);
#ifndef GNUPLOT_PNG
    else if (strcmp(item, _("Add last graph")) == 0)
	add_graph_to_session(NULL, GRETL_GNUPLOT_GRAPH, NULL);
#endif
}

/* ........................................................... */

static void info_popup_activated (GtkWidget *widget, gpointer data)
{
    gchar *item = (gchar *) data;

    if (strcmp(item, _("View")) == 0) 
	open_info(NULL, 0, NULL);
    else if (strcmp(item, _("Edit")) == 0) 
	edit_header(NULL, 0, NULL);
}

/* ........................................................... */

static void data_popup_activated (GtkWidget *widget, gpointer data)
{
    gchar *item = (gchar *) data;

    if (strcmp(item, _("Edit")) == 0) 
	show_spreadsheet(NULL);
    else if (strcmp(item, _("Save...")) == 0) 
	file_save(mdata, SAVE_DATA, NULL);
    else if (strcmp(item, _("Export as CSV...")) == 0) 
	file_save(mdata, EXPORT_CSV, NULL);
    else if (strcmp(item, _("Copy as CSV...")) == 0) 
	csv_to_clipboard();
}

/* ........................................................... */

static void object_popup_activated (GtkWidget *widget, gpointer data)
{
    gchar *item = (gchar *) data;
    gui_obj *obj;

    obj = active_object;

    if (strcmp(item, _("Display")) == 0) {
	if (obj->sort == 'm') {
	    open_gui_model(obj);
	}
	if (obj->sort == 'v') {
	    open_gui_var(obj);
	}
	if (obj->sort == 't') {
	    display_model_table();
	}
	else if (obj->sort == 'g') {
	    open_gui_graph(obj);
	}
	else if (obj->sort == 'b') {
	    open_boxplot(obj);
	}
    } 
#ifndef GNUPLOT_PNG
    else if (strcmp(item, _("Edit using GUI")) == 0) {
	if (obj->sort == 'g') {
	    GRAPHT *graph = (GRAPHT *) obj->data;

	    start_editing_session_graph(graph->fname);
	}
    } 
#endif
    else if (strcmp(item, _("Edit plot commands")) == 0) {
	if (obj->sort == 'g' || obj->sort == 'b') {
	    GRAPHT *graph = (GRAPHT *) obj->data;

#ifdef GNUPLOT_PNG
	    remove_png_term_from_plotfile(graph->fname, NULL);
#endif
	    view_file(graph->fname, 1, 0, 78, 400, 
		      (obj->sort == 'g')? GR_PLOT : GR_BOX);
	}
    }   
    else if (strcmp(item, _("Delete")) == 0) {
	maybe_delete_session_object(obj);
    }
    else if (strcmp(item, _("Add to model table")) == 0) {
	if (obj->sort == 'm') {
	    MODEL *pmod = (MODEL *) obj->data;
	    add_to_model_table_list((const MODEL *) pmod, MODEL_ADD_FROM_MENU);
	}
    }
    else if (strcmp(item, _("Clear table")) == 0) {
	if (obj->sort == 't') {
	    free_model_table_list();
	}
    }
    else if (obj->sort == 't' && strcmp(item, _("Help")) == 0) {
	context_help(NULL, GINT_TO_POINTER(MODELTABLE));
    }
}

/* ........................................................... */

static gboolean icon_entered (GtkWidget *icon, GdkEventCrossing *event,
			      gui_obj *gobj)
{
    gtk_widget_set_state(icon, GTK_STATE_SELECTED);
    
    return FALSE;
}

static gboolean icon_left (GtkWidget *icon, GdkEventCrossing *event,
			   gui_obj *gobj)
{
    gtk_widget_set_state(icon, GTK_STATE_NORMAL);
    
    return FALSE;
}

/* ........................................................... */

static void 
model_table_data_received (GtkWidget *widget,
			   GdkDragContext *context,
			   gint x,
			   gint y,
			   GtkSelectionData *data,
			   guint info,
			   guint time,
			   gpointer p)
{
    if (info == GRETL_MODEL_POINTER && data != NULL && 
	data->type == GDK_SELECTION_TYPE_INTEGER) {
	MODEL *pmod = *(MODEL **) data->data;

	add_to_model_table_list((const MODEL *) pmod, MODEL_ADD_BY_DRAG);
    }
}

static void table_drag_setup (GtkWidget *w)
{
    gtk_drag_dest_set (w,
                       GTK_DEST_DEFAULT_ALL,
                       &model_table_drag_target, 1,
                       GDK_ACTION_COPY);

#ifdef OLD_GTK
    gtk_signal_connect (GTK_OBJECT(w), "drag_data_received",
			GTK_SIGNAL_FUNC(model_table_data_received),
			NULL);
#else
    g_signal_connect (G_OBJECT(w), "drag_data_received",
                      G_CALLBACK(model_table_data_received),
                      NULL);
#endif
}

static void drag_model (GtkWidget *w, GdkDragContext *context,
			GtkSelectionData *sel, guint info, guint t,
			MODEL *pmod)
{
    gtk_selection_data_set(sel, GDK_SELECTION_TYPE_INTEGER, 8, 
                           (const guchar *) &pmod, sizeof pmod);
}

static void model_drag_connect (GtkWidget *w, MODEL *pmod)
{
    gtk_drag_source_set(w, GDK_BUTTON1_MASK,
                        &model_table_drag_target,
                        1, GDK_ACTION_COPY);
#ifdef OLD_GTK
    gtk_signal_connect(GTK_OBJECT(w), "drag_data_get",
		       GTK_SIGNAL_FUNC(drag_model), pmod);
#else
    g_signal_connect(G_OBJECT(w), "drag_data_get",
                     G_CALLBACK(drag_model), pmod);
#endif
}

/* ........................................................... */

static gui_obj *session_add_icon (gpointer data, int sort, int mode)
{
    gui_obj *gobj;
    gchar *name = NULL;
    MODEL *pmod = NULL;
    GRETL_VAR *var = NULL;
    GRAPHT *graph = NULL;
    int icon_named = 0;

    switch (sort) {
    case 'm':
	pmod = (MODEL *) data;
	name = g_strdup(pmod->name);
	break;
    case 'v':
	var = (GRETL_VAR *) data;
	name = g_strdup(gretl_var_get_name(var));
	break;
    case 'b':
    case 'g':
	graph = (GRAPHT *) data;
	name = g_strdup(graph->name);
	break;
    case 'd':
	name = g_strdup(_("Data set"));
	break;
    case 'i':
	name = g_strdup(_("Data info"));
	break;
    case 's':
	name = g_strdup(_("Session"));
	break;
    case 'n':
	name = g_strdup(_("Notes"));
	break;
    case 'r':
	name = g_strdup(_("Correlations"));
	break;
    case 'x':
	name = g_strdup(_("Summary"));
	break;
    case 't':
	name = g_strdup(_("Model table"));
	break;
    default:
	break;
    }

    gobj = gui_object_new(name, sort);

    if (strlen(name) > OBJECT_NAMELEN) {
	gretl_tooltips_add(GTK_WIDGET(gobj->icon), name);
	icon_named = 1;
    }

    if (sort == 'm' || sort == 'g' || sort == 'b') {
	if (sort == 'm') {
	    gobj->data = pmod;
	    model_drag_connect(gobj->icon, pmod);
	} else {
	    gobj->data = graph;
	} 

	if (!icon_named) {
	    char *str = NULL;
	    
	    if (sort == 'm') {
		str = model_cmd_str(pmod);
	    } else if (sort == 'g') {
		str = graph_str(graph);
	    } else if (sort == 'b') {
		str = boxplot_str(graph);
	    }
	    if (str != NULL) {
		gretl_tooltips_add(GTK_WIDGET(gobj->icon), str);
		free(str);
	    }
	}
    }	    

    else if (sort == 'v') gobj->data = var;
    else if (sort == 'd') gobj->data = paths.datfile;
    else if (sort == 's') gobj->data = cmdfile;
    else if (sort == 't') gobj->data = NULL;

    if (mode == ICON_ADD_SINGLE) {
	pack_single_icon(gobj);
    } else if (mode == ICON_ADD_BATCH) {
	icon_list = g_list_append(icon_list, gobj);
    }

    return gobj;
}

/* apparatus for rebuilding session models */

static int silent_remember (MODEL **ppmod, DATAINFO *pdinfo)
{
    MODEL *pmod = *ppmod;
    MODEL *tmp; /* will be returned in place of the saved model */

#ifdef SESSION_DEBUG
    fprintf(stderr, "session.nmodels = %d\n", session.nmodels);
#endif

    if ((pmod->name = malloc(32)) == NULL) return 1;

    *pmod->name = 0;
    strncat(pmod->name, rebuild.model_name[session.nmodels], 31);

    if (session.nmodels == 0) {
	session.models = malloc(sizeof *session.models);
    } else {
	session.models = realloc(session.models, 
				 (session.nmodels + 1) * 
				 sizeof *session.models);
    }

    if (session.models == NULL) return 1;

    session.models[session.nmodels] = pmod;
    session.nmodels += 1;

    tmp = gretl_model_new(pdinfo);
    if (tmp == NULL) return 1;

    *ppmod = tmp; /* replaced */

#ifdef SESSION_DEBUG
    fprintf(stderr, "copied '%s' to session.models[%d]\n" 
	    " nmodels = %d\n", rebuild.model_name[session.nmodels-1], 
	    session.nmodels-1, session.nmodels); 
#endif

    return 0;
}

int clear_or_save_model (MODEL **ppmod, DATAINFO *pdinfo, 
			 int rebuilding)
{
    /* The standard behavior here is simply to clear the model
       for future use.  But if we're rebuilding a gretl session
       then we stack the given model pointer and substitute a
       new blank model for the next use.
    */

    if (rebuilding) {
	static int save;

	if (save) {
	    int i;

	    for (i=0; i<rebuild.nmodels; i++) {
		if ((*ppmod)->ID == rebuild.model_ID[i]) {
		    return silent_remember(ppmod, pdinfo);
		}
	    }
	}
	save = 1;
	clear_model(*ppmod, pdinfo);
    } else {
	/* no rebuild, no need to save */
	clear_model(*ppmod, pdinfo);
    }

    return 0;
}

void print_saved_object_specs (const char *session_base, FILE *fp)
{
    int i;
    char tmp[MAXLEN];

    fprintf(fp, "(* saved objects:\n");

    /* save session models */
    for (i=0; i<session.nmodels; i++) {
	fprintf(fp, "model %d \"%s\"\n", 
		(session.models[i])->ID, 
		(session.models[i])->name);
    }

    /* save session graphs */
    for (i=0; i<session.ngraphs; i++) {
	/* formulate save name for graph */
	sprintf(tmp, "%sGraph_%d", session_base, i + 1);
	/* does the constructed filename differ from the
	   current one? */
	if (strcmp((session.graphs[i])->fname, tmp)) {
	    if (copyfile((session.graphs[i])->fname, tmp)) {
		continue;
	    } else {
		remove((session.graphs[i])->fname);
		strcpy((session.graphs[i])->fname, tmp);
	    }
	}
	fprintf(fp, "%s %d \"%s\" %s\n", 
		((session.graphs[i])->sort == GRETL_BOXPLOT)?
		"plot" : "graph",
		(session.graphs[i])->ID, 
		(session.graphs[i])->name, 
		(session.graphs[i])->fname);
    }

    fprintf(fp, "*)\n");
}

int print_session_notes (const char *fname)
{
    int err = 0;

    if (session.notes != NULL && strlen(session.notes)) {
	FILE *fp;

	fp = fopen(fname, "w");
	if (fp != NULL) {
	    fprintf(fp, "%s", session.notes);
	    fclose(fp);
	}  else {
	    err = 1;
	}
    }

    return err;
}

#ifdef OLD_GTK

static GtkWidget *create_popup_item (GtkWidget *popup, char *str, 
				     GtkSignalFunc callback)
{
    GtkWidget *item;

    item = gtk_menu_item_new_with_label(str);
    gtk_signal_connect(GTK_OBJECT(item), "activate",
		       callback,
		       str);
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);

    return item;
}

#else 

static GtkWidget *create_popup_item (GtkWidget *popup, char *str, 
				     GtkCallback callback)
{
    GtkWidget *item;

    item = gtk_menu_item_new_with_label(str);
    g_signal_connect(G_OBJECT(item), "activate",
		     G_CALLBACK(callback),
		     str);
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup), item);

    return item;
}

#endif

/* ........................................................... */

static void session_build_popups (void)
{
    size_t i;

    if (global_popup == NULL) {
	global_popup = gtk_menu_new();
	for (i=0; i<sizeof global_items / sizeof global_items[0]; i++) {
	    create_popup_item(global_popup, 
			      _(global_items[i]), 
			      global_popup_activated);
	}
    }

    if (session_popup == NULL) {
	size_t n = sizeof session_items / sizeof session_items[0];

	session_popup = gtk_menu_new();
	for (i=0; i<n; i++) {
	    if (i < n-1) {
		create_popup_item(session_popup, 
				  _(session_items[i]), 
				  session_popup_activated);
	    } else {
		addgraph = 
		    create_popup_item(session_popup, 
				      _(session_items[i]), 
				      session_popup_activated);
	    }
	}
    }

    if (model_popup == NULL) {
	model_popup = gtk_menu_new();
	for (i=0; i<sizeof model_items / sizeof model_items[0]; i++) {
		create_popup_item(model_popup, 
				  _(model_items[i]), 
				  object_popup_activated);
	}
    }

    if (var_popup == NULL) {
	var_popup = gtk_menu_new();
	for (i=0; i<sizeof var_items / sizeof var_items[0]; i++) {
		create_popup_item(var_popup, 
				  _(var_items[i]), 
				  object_popup_activated);
	}
    }

    if (model_table_popup == NULL) {
	model_table_popup = gtk_menu_new();
	for (i=0; i<sizeof model_table_items / sizeof model_table_items[0]; i++) {
		create_popup_item(model_table_popup, 
				  _(model_table_items[i]), 
				  object_popup_activated);
	}
    }

    if (graph_popup == NULL) {
	graph_popup = gtk_menu_new();
	for (i=0; i<sizeof graph_items / sizeof graph_items[0]; i++) {
		create_popup_item(graph_popup, 
				  _(graph_items[i]), 
				  object_popup_activated);
	}
    }

    if (boxplot_popup == NULL) {
	boxplot_popup = gtk_menu_new();
	for (i=0; i<sizeof graph_items / sizeof graph_items[0]; i++) {
	    if (strstr(graph_items[i], "GUI")) continue;
		create_popup_item(boxplot_popup, 
				  _(graph_items[i]), 
				  object_popup_activated);
	}
    }

    if (data_popup == NULL) {
	data_popup = gtk_menu_new();
	for (i=0; i<sizeof dataset_items / sizeof dataset_items[0]; i++) {
		create_popup_item(data_popup, 
				  _(dataset_items[i]), 
				  data_popup_activated);	    
	}
    }

    if (info_popup == NULL) {
	info_popup = gtk_menu_new();
	for (i=0; i<sizeof info_items / sizeof info_items[0]; i++) {
		create_popup_item(info_popup, 
				  _(info_items[i]), 
				  info_popup_activated);	    
	}
    }
}

/* ........................................................... */

static void iconview_connect_signals (GtkWidget *iconview)
{
#ifdef OLD_GTK
    gtk_signal_connect(GTK_OBJECT(iconview), "destroy",
		     GTK_SIGNAL_FUNC(session_view_free), NULL);
    gtk_signal_connect(GTK_OBJECT(iconview), "key_press_event",
		     GTK_SIGNAL_FUNC(catch_iconview_key), NULL);
#else
    g_signal_connect(G_OBJECT(iconview), "destroy",
		     G_CALLBACK(session_view_free), NULL);
    g_signal_connect(G_OBJECT(iconview), "key_press_event",
		     G_CALLBACK(catch_iconview_key), NULL);
#endif
}

void view_session (void)
{
    GtkWidget *hbox, *scroller;
    gchar *title;

    if (iconview != NULL) {
	gdk_window_show(iconview->window);
	gdk_window_raise(iconview->window);
	return;
    }

    session_view_init();

    title = g_strdup_printf("gretl: %s", 
			    (session.name[0])? session.name : 
			    _("current session"));
    
    iconview = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(iconview), title);
    g_free(title);
    gtk_window_set_default_size(GTK_WINDOW(iconview), 400, 300);
    gtk_container_set_border_width(GTK_CONTAINER(iconview), 0);

    iconview_connect_signals(iconview);

    session_build_popups();

    hbox = gtk_hbox_new(FALSE,0);
    gtk_container_add(GTK_CONTAINER(iconview), hbox);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);

    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(scroller), 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
#ifdef OLD_GTK
    gtk_signal_connect(GTK_OBJECT(scroller), "button_press_event",
		       GTK_SIGNAL_FUNC(session_icon_click), NULL);
#else
    g_signal_connect(G_OBJECT(scroller), "button_press_event",
		     G_CALLBACK(session_icon_click), NULL);
#endif

    gtk_box_pack_start(GTK_BOX(hbox), scroller, TRUE, TRUE, 0); 

    icon_table = gtk_table_new(2, SESSION_VIEW_COLS, FALSE);

    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroller), 
					  icon_table);

    add_all_icons();

    gtk_widget_show(icon_table);
    gtk_widget_show(scroller);
    gtk_widget_show(hbox);
    gtk_widget_show(iconview);

    gtk_container_foreach(GTK_CONTAINER(scroller), 
			 (GtkCallback) white_bg_style, 
			 NULL);

    GTK_WIDGET_SET_FLAGS(icon_table, GTK_CAN_FOCUS);
    gtk_widget_grab_focus(icon_table);
}

/* apparatus for renaming session objects */

#if 0

static gboolean gtk_editable_get_editable (GtkEditable *e)
{
    gboolean ret;

    gtk_object_get(GTK_OBJECT(e), "editable", &ret, NULL);
    return ret;
}

static void gtk_entry_set_width_chars (GtkEntry *entry, int width)
{
    /* gtk_widget_set_usize(entry, width, height */
    return;
}

static void gtk_entry_set_has_frame (GtkEntry *entry, gboolean b)
{
    return;
}

#endif /* bodges for old gtk */

/* for now, object renaming is gtk2 only */

#ifndef OLD_GTK

static void size_name_entry (GtkWidget *w, const char *name)
{
    int n = strlen(name) + 2;

    if (n > OBJECT_NAMELEN) n = OBJECT_NAMELEN;
    gtk_entry_set_width_chars(GTK_ENTRY(w), n);    
}

static gboolean object_name_return (GtkWidget *w,
				    GdkEventKey *key,
				    gui_obj *gobj)
{
    if (!gtk_editable_get_editable(GTK_EDITABLE(gobj->label))) {
	return FALSE;
    }

    if (key->keyval == GDK_Return) {
	const gchar *newname = gtk_entry_get_text(GTK_ENTRY(gobj->label));

	gtk_editable_set_position(GTK_EDITABLE(gobj->label), 0);
	/* gtk_entry_set_has_frame(GTK_ENTRY(gobj->label), FALSE); */
	gtk_editable_set_editable(GTK_EDITABLE(gobj->label), FALSE);
	
	if (newname != NULL && *newname != '\0' &&
	    strcmp(newname, gobj->name)) {
	    rename_session_object(gobj, newname);
	    size_name_entry(gobj->label, newname);
	}

	gtk_widget_grab_focus(icon_table);

	return TRUE;
    } 

    return FALSE;
}

static gboolean start_rename_object (GtkWidget *w,
				     GdkEventButton *event,
				     gui_obj *gobj)
{
    if (gtk_editable_get_editable(GTK_EDITABLE(gobj->label))) {
	return FALSE;
    }

    gtk_entry_set_width_chars(GTK_ENTRY(gobj->label), OBJECT_NAMELEN);
    /* gtk_entry_set_has_frame(GTK_ENTRY(gobj->label), TRUE); */
    gtk_editable_set_editable(GTK_EDITABLE(gobj->label), TRUE);
    gtk_editable_select_region(GTK_EDITABLE(gobj->label), 0, -1);
    gtk_editable_set_position(GTK_EDITABLE(gobj->label), -1);
    gtk_widget_grab_focus(gobj->label);
    
    return TRUE;
}

#endif /* !OLD_GTK -- object renaming stuff */

static void make_short_label_string (char *targ, const char *src)
{
    if (strlen(src) > OBJECT_NAMELEN) {
	*targ = '\0';
	strncat(targ, src, OBJECT_NAMELEN - 3);
	strcat(targ, "...");
    } else {
	strcpy(targ, src);
    }
}

#ifdef OLD_GTK

static void create_gobj_icon (gui_obj *gobj, char **xpm)
{
    GdkPixmap *pixmap;
    GdkBitmap *mask;
    GtkStyle *style;
    GtkWidget *image;
    gchar shortname[OBJECT_NAMELEN + 1];

    style = gtk_widget_get_style(iconview);
    pixmap = gdk_pixmap_create_from_xpm_d(mdata->w->window,
					  &mask, 
					  &style->bg[GTK_STATE_NORMAL], 
					  xpm);

    gobj->icon = gtk_event_box_new();
    gtk_widget_set_usize(gobj->icon, 36, 36);

    image = gtk_pixmap_new(pixmap, mask);

    gtk_container_add(GTK_CONTAINER(gobj->icon), image);
    gtk_widget_show(image);

    if (gobj->sort == 't') table_drag_setup(gobj->icon);

    make_short_label_string(shortname, gobj->name);
    gobj->label = gtk_label_new(shortname);
    
    gtk_signal_connect(GTK_OBJECT(gobj->icon), "button_press_event",
		       GTK_SIGNAL_FUNC(session_icon_click), gobj);
    gtk_signal_connect(GTK_OBJECT(gobj->icon), "enter_notify_event",
		       GTK_SIGNAL_FUNC(icon_entered), gobj);
    gtk_signal_connect(GTK_OBJECT(gobj->icon), "leave_notify_event",
		       GTK_SIGNAL_FUNC(icon_left), gobj);
}

#else

static void create_gobj_icon (gui_obj *gobj, const char **xpm)
{
    GdkPixbuf *pbuf;
    GtkWidget *image;

    pbuf = gdk_pixbuf_new_from_xpm_data(xpm);

    gobj->icon = gtk_event_box_new();
    gtk_widget_set_size_request(gobj->icon, 36, 36);

    image = gtk_image_new_from_pixbuf(pbuf);
    g_object_unref(G_OBJECT(pbuf));

    gtk_container_add(GTK_CONTAINER(gobj->icon), image);
    gtk_widget_show(image);

    if (gobj->sort == 't') table_drag_setup(gobj->icon);

    if (gobj->sort == 'm' || gobj->sort == 'g' ||
	gobj->sort == 'v' || gobj->sort == 'b') { 
	gobj->label = gtk_entry_new();
	gtk_entry_set_text(GTK_ENTRY(gobj->label), gobj->name);
	gtk_editable_set_editable(GTK_EDITABLE(gobj->label), FALSE);
	gtk_entry_set_has_frame(GTK_ENTRY(gobj->label), FALSE);
	gtk_entry_set_max_length(GTK_ENTRY(gobj->label), 32);
	size_name_entry(gobj->label, gobj->name);
	g_signal_connect(G_OBJECT(gobj->label), "button-press-event",
			 G_CALLBACK(start_rename_object), gobj);
	g_signal_connect(G_OBJECT(gobj->label), "key-press-event",
			 G_CALLBACK(object_name_return), gobj);
    } else {
	gchar str[OBJECT_NAMELEN + 1];

	make_short_label_string(str, gobj->name);
	gobj->label = gtk_label_new(str);
    }

    g_signal_connect(G_OBJECT(gobj->icon), "button_press_event",
		     G_CALLBACK(session_icon_click), gobj);
    g_signal_connect(G_OBJECT(gobj->icon), "enter_notify_event",
		     G_CALLBACK(icon_entered), gobj);
    g_signal_connect(G_OBJECT(gobj->icon), "leave_notify_event",
		     G_CALLBACK(icon_left), gobj);
}

#endif /* OLD_GTK */

static gui_obj *gui_object_new (gchar *name, int sort)
{
    gui_obj *gobj;
    char **xpm = NULL;

    gobj = mymalloc(sizeof *gobj);
    gobj->name = name; 
    gobj->sort = sort;
    gobj->data = NULL;

    switch (sort) {
    case 'm': xpm = model_xpm; break;
    case 'v': xpm = model_xpm; break;	
    case 'b': xpm = boxplot_xpm; break;
    case 'g': xpm = gnuplot_xpm; break;
    case 'd': xpm = dot_sc_xpm; break;
    case 'i': xpm = xfm_info_xpm; break;
    case 's': xpm = xfm_make_xpm; break;
    case 'n': xpm = text_xpm; break;
    case 'r': xpm = rhohat_xpm; break;
    case 'x': xpm = summary_xpm; break;
    case 't': xpm = model_table_xpm; break;
    default: break;
    }

#ifdef OLD_GTK
    create_gobj_icon (gobj, xpm);
#else
    create_gobj_icon (gobj, (const char **) xpm);
#endif

    return gobj;
} 

/* ........................................................... */

#ifdef OLD_GTK

static void auto_save_gp (gpointer data, guint quiet, GtkWidget *w)
{
    FILE *fp;
    char msg[MAXLEN];
    gchar *savestuff;
    windata_t *mydata = (windata_t *) data;

    if ((fp = fopen(mydata->fname, "w")) == NULL) {
	sprintf(msg, _("Couldn't write to %s"), mydata->fname);
	errbox(msg); 
	return;
    }

    savestuff = gtk_editable_get_chars(GTK_EDITABLE(mydata->w), 0, -1);
    fprintf(fp, "%s", savestuff);
    g_free(savestuff); 
    fclose(fp);

    if (!quiet) infobox(_("plot commands saved"));
}

#else /* end of gtk-1.2 version, on to gtk-2.0 */

static void auto_save_gp (gpointer data, guint quiet, GtkWidget *w)
{
    FILE *fp;
    gchar *msg, *savestuff;
    windata_t *mydata = (windata_t *) data;
# ifdef ENABLE_NLS
    gsize bytes;
    gchar *trbuf;
# endif

    savestuff = textview_get_text(GTK_TEXT_VIEW(mydata->w));

    if (savestuff == NULL) return;

    if ((fp = fopen(mydata->fname, "w")) == NULL) {
	msg = g_strdup_printf(_("Couldn't write to %s"), mydata->fname);
	errbox(msg); 
	g_free(msg);
	g_free(savestuff);
	return;
    }

# ifdef ENABLE_NLS
    trbuf = g_locale_from_utf8(savestuff, -1, NULL, &bytes, NULL);
    fprintf(fp, "%s", trbuf);
    g_free(trbuf);
# else
    fprintf(fp, "%s", savestuff);
# endif

    g_free(savestuff); 
    fclose(fp);
    if (!quiet) infobox(_("plot commands saved"));
}

#endif /* gtk-2.0 branch */

#ifdef G_OS_WIN32

static char *add_pause_to_plotfile (const char *fname)
{
    FILE *fin, *fout;
    char fline[MAXLEN];
    char *tmpfile = NULL;
    int gotpause = 0;

    fin = fopen(fname, "r");
    if (fin == NULL) return NULL;

    tmpfile = g_strdup_printf("%showtmp.gp", paths.userdir);

    fout = fopen(tmpfile, "w");
    if (fout == NULL) {
	fclose(fin);
	g_free(tmpfile);
	return NULL;
    }

    while (fgets(fline, MAXLEN - 1, fin)) {
	fputs(fline, fout);
	if (strstr(fline, "pause -1")) gotpause = 1;
    }

    if (!gotpause) {
	fputs("pause -1\n", fout);
    }

    fclose(fin);
    fclose(fout);

    return tmpfile;
}

#endif /* G_OS_WIN32 */

#ifdef OLD_GTK

void gp_to_gnuplot (gpointer data, guint i, GtkWidget *w)
{
    gchar *buf = NULL;
    windata_t *mydata = (windata_t *) data;

    auto_save_gp(data, 1, NULL);

    buf = g_strdup_printf("gnuplot -persist \"%s\"", mydata->fname);

    if (system(buf))
        errbox(_("gnuplot command failed"));

    g_free(buf);
}

#else /* not gtk-1.2, but gtk-2.0 */

void gp_to_gnuplot (gpointer data, guint i, GtkWidget *w)
{
    gchar *buf = NULL;
    windata_t *mydata = (windata_t *) data;
    int err = 0;
# ifdef G_OS_WIN32
    gchar *tmpfile;
# endif

    auto_save_gp(data, 1, NULL);

# ifdef G_OS_WIN32
    tmpfile = add_pause_to_plotfile(mydata->fname);
    if (tmpfile != NULL) {
	buf = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, tmpfile);
	err = (WinExec(buf, SW_SHOWNORMAL) < 32);
	remove(tmpfile); /* is this OK? */
	g_free(tmpfile);
    } else {
	err = 1;
    }
# else
    buf = g_strdup_printf("gnuplot -persist \"%s\"", mydata->fname);
    err = gretl_spawn(buf);
# endif

    if (err) errbox(_("gnuplot command failed"));

    g_free(buf);
}

void save_plot_commands_callback (GtkWidget *w, gpointer p)
{
    auto_save_gp(p, 0, NULL);
}

#endif /* gtk versions fork */

#ifdef GNUPLOT_PNG

static void open_gui_graph (gui_obj *gobj)
{
    GRAPHT *graph = (GRAPHT *) gobj->data;

    display_session_graph_png(graph->fname);
}

#else /* non-PNG versions */

# ifdef OLD_GTK

static void open_gui_graph (gui_obj *gobj)
{
    GRAPHT *graph = (GRAPHT *) gobj->data;
    gchar *buf = NULL;

    buf = g_strdup_printf("\"%s\" -persist \"%s\"", paths.gnuplot, graph->fname);
    if (system(buf)) {
	errbox(_("gnuplot command failed"));
    }
    g_free(buf);
}

# else

static void open_gui_graph (gui_obj *gobj)
{
    GRAPHT *graph = (GRAPHT *) gobj->data;
    gchar *buf = NULL;
    int err = 0;

#  ifdef G_OS_WIN32
    buf = g_strdup_printf("\"%s\" \"%s\"", paths.gnuplot, graph->fname);
    err = (WinExec(buf, SW_SHOWNORMAL) < 32);
#  else
    buf = g_strdup_printf("\"%s\" -persist \"%s\"", paths.gnuplot, graph->fname);
    err = gretl_spawn(buf);
#  endif
    g_free(buf);

    if (err) errbox(_("gnuplot command failed"));
}

# endif /* alternate gtk versions */

#endif /* ! GNUPLOT_PNG */

