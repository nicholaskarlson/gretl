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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/* settings.c for gretl */

#include "gretl.h"
#include <unistd.h>

#ifdef USE_GNOME
# include <gconf/gconf-client.h>
#endif

#ifdef G_OS_WIN32
# include <windows.h>
#else
# include <dirent.h>
# include "gtkfontselhack.h"
#endif

#if !defined(G_OS_WIN32) && !defined(USE_GNOME)
char rcfile[MAXLEN];
#endif

extern GtkWidget *toolbar_box;

extern int want_toolbar;
extern char calculator[MAXSTR];
extern char editor[MAXSTR];
extern char Rcommand[MAXSTR];
extern char dbproxy[21];

#ifdef HAVE_TRAMO
extern char tramo[MAXSTR];
extern char tramodir[MAXSTR];
#endif

#ifdef HAVE_X12A
extern char x12a[MAXSTR];
extern char x12adir[MAXSTR];
#endif

int use_proxy;

/* filelist stuff */
#define MAXRECENT 4

static void printfilelist (int filetype, FILE *fp);

static char datalist[MAXRECENT][MAXSTR], *datap[MAXRECENT];
static char sessionlist[MAXRECENT][MAXSTR], *sessionp[MAXRECENT];
static char scriptlist[MAXRECENT][MAXSTR], *scriptp[MAXRECENT];

static void make_prefs_tab (GtkWidget *notebook, int tab);
static void apply_changes (GtkWidget *widget, gpointer data);
#ifndef G_OS_WIN32
static void read_rc (void);
#endif

/* font handling */
#ifdef G_OS_WIN32
static char fixedfontname[MAXLEN] = "Courier New 10";
#else
static char fixedfontname[MAXLEN] = "Monospace 10";
#endif

#if defined(G_OS_WIN32)
static char appfontname[MAXLEN] = "tahoma 8";
#elif !defined(USE_GNOME)
static char appfontname[MAXLEN] = "Sans 10";
#endif

PangoFontDescription *fixed_font;

static int usecwd;
int olddat;
int jwdata;
int useqr;
#ifdef ENABLE_NLS
static int lcnumeric = 1;
#endif

typedef struct {
    char *key;         /* config file variable name */
    char *description; /* How the field will show up in the options dialog */
    char *link;        /* in case of radio button pair, alternate string */
    void *var;         /* pointer to variable */
    char type;         /* 'U' user string
			  'R' root string
			  'B' boolean (user) 
			  'I' "invisible" (user) string 
		       */
    int len;           /* storage size for string variable (also see Note) */
    short tab;         /* which tab (if any) does the item fall under? */
    GtkWidget *widget;
} RCVARS;

/* Note: actually "len" above is overloaded: if an rc_var is of type 'B'
   (boolean) and not part of a radio group, then a non-zero value for
   len will link the var's toggle button with the sensitivity of the
   preceding rc_var's entry field.  For example, the "use_proxy" button
   controls the sensitivity of the "dbproxy" entry widget. */

RCVARS rc_vars[] = {
    {"gretldir", N_("Main gretl directory"), NULL, paths.gretldir, 
     'R', MAXLEN, 1, NULL},
    {"userdir", N_("User's gretl directory"), NULL, paths.userdir, 
     'U', MAXLEN, 1, NULL},
    {"expert", N_("Expert mode (no warnings)"), NULL, &expert, 
     'B', 0, 1, NULL},
    {"updater", N_("Tell me about gretl updates"), NULL, &updater, 
     'B', 0, 1, NULL},
    {"toolbar", N_("Show gretl toolbar"), NULL, &want_toolbar, 
     'B', 0, 1, NULL},
#ifdef ENABLE_NLS
    {"lcnumeric", N_("Use locale setting for decimal point"), NULL, &lcnumeric, 
     'B', 0, 1, NULL},
#endif
    {"gnuplot", N_("Command to launch gnuplot"), NULL, paths.gnuplot, 
     'R', MAXLEN, 3, NULL},
    {"Rcommand", N_("Command to launch GNU R"), NULL, Rcommand, 
     'R', MAXSTR, 3, NULL},
    {"viewdvi", N_("Command to view DVI files"), NULL, viewdvi, 
     'R', MAXSTR, 3, NULL},
    {"calculator", N_("Calculator"), NULL, calculator, 
     'U', MAXSTR, 3, NULL},
    {"editor", N_("Editor"), NULL, editor, 
     'U', MAXSTR, 3, NULL},
#ifdef HAVE_X12A
    {"x12a", N_("path to x12arima"), NULL, x12a, 
     'R', MAXSTR, 3, NULL},
#endif
#ifdef HAVE_TRAMO
    {"tramo", N_("path to tramo"), NULL, tramo, 
     'R', MAXSTR, 3, NULL},
#endif
#ifdef OLD_HAVE_X12A
    {"x12adir", N_("X-12-ARIMA working directory"), NULL, x12adir, 
     'R', MAXSTR, 3, NULL},
#endif
#ifdef OLD_HAVE_TRAMO
    {"tramodir", N_("TRAMO working directory"), NULL, tramodir, 
     'R', MAXSTR, 3, NULL},
#endif
    {"binbase", N_("gretl database directory"), NULL, paths.binbase, 
     'U', MAXLEN, 2, NULL},
    {"ratsbase", N_("RATS data directory"), NULL, paths.ratsbase, 
     'U', MAXLEN, 2, NULL},
    {"dbhost_ip", N_("Database server IP"), NULL, paths.dbhost_ip, 
     'U', 16, 2, NULL},
    {"dbproxy", N_("HTTP proxy (ipnumber:port)"), NULL, dbproxy, 
     'U', 21, 2, NULL},
    {"useproxy", N_("Use HTTP proxy"), NULL, &use_proxy, 
     'B', 1, 2, NULL},
    {"usecwd", N_("Use current working directory as default"), 
     N_("Use gretl user directory as default"), &usecwd, 'B', 0, 4, NULL},
    {"olddat", N_("Use \".dat\" as default datafile suffix"), 
     N_("Use \".gdt\" as default suffix"), &olddat, 'B', 0, 5, NULL},
    {"jwdata", N_("Show Wooldridge data first"), 
     N_("Show Ramanathan data first"), &jwdata, 'B', 0, 5, NULL},
    {"useqr", N_("Use QR decomposition"), 
     N_("Use Cholesky decomposition"), &useqr, 'B', 0, 1, NULL},
    {"Fixed_font", N_("Fixed font"), NULL, fixedfontname, 'U', MAXLEN, 0, NULL},
#ifndef USE_GNOME
    {"App_font", N_("Menu font"), NULL, appfontname, 'U', MAXLEN, 0, NULL},
#endif
    {"Png_font", N_("PNG graph font"), NULL, paths.pngfont, 'I', 16, 0, NULL},
    {NULL, NULL, NULL, NULL, 0, 0, 0, NULL}   
};

/* ........................................................... */

void set_fixed_font (void)
{
    if (fixed_font != NULL) 
	pango_font_description_free(fixed_font);

    /* set a monospaced font for various windows */
    fixed_font = pango_font_description_from_string(fixedfontname);
}

/* ........................................................... */

#ifndef USE_GNOME
void set_app_font (const char *fontname)
{
    GtkSettings *settings;

    if (fontname != NULL && *fontname == 0) return;

    settings = gtk_settings_get_default();

    if (fontname == NULL) {
	g_object_set(G_OBJECT(settings), "gtk-font-name", appfontname, NULL);
    } else {
	GtkWidget *w;
	PangoFontDescription *pfd;
	PangoContext *pc;
	PangoFont *pfont;

	w = gtk_label_new(NULL);
	pfd = pango_font_description_from_string(fontname);
	pc = gtk_widget_get_pango_context(w);
	pfont = pango_context_load_font(pc, pfd);

	if (pfont != NULL) {
	    strcpy(appfontname, fontname);
	    g_object_set(G_OBJECT(settings), "gtk-font-name", appfontname, NULL);
	}

	gtk_widget_destroy(w);
	pango_font_description_free(pfd);
    }
}
#endif

/* .................................................................. */

void get_default_dir (char *s)
{
    *s = 0;

    if (usecwd) {
	char *test = getcwd(s, MAXLEN);

	if (test == NULL) {
	    strcpy(s, paths.userdir);
	} else {
	    strcat(s, SLASHSTR);
	}
    } else {
	strcpy(s, paths.userdir);   
    } 
}

/* ........................................................... */

#if defined(HAVE_TRAMO) || defined (HAVE_X12A)

#ifdef HAVE_TRAMO
void set_tramo_ok (int set)
{
    static int ok;

    if (set >= 0) ok = set;
    if (mdata != NULL) {
	flip(mdata->ifac, "/Variable/TRAMO analysis", ok);
    }
}
#endif

#ifdef HAVE_X12A
void set_x12a_ok (int set)
{
    static int ok;

    if (set >= 0) ok = set;
    if (mdata != NULL) {
	flip(mdata->ifac, "/Variable/X-12-ARIMA analysis", ok);
    }
}
#endif

#ifdef G_OS_WIN32
static int win32_check_for_prog (const char *prog)
{
    int ret = 1;
    char tmp[MAXLEN];
    WIN32_FIND_DATA find_data;
    HANDLE hfind;

    if (prog == NULL || *prog == 0) return 0;

    hfind = FindFirstFile(prog, &find_data);
    if (hfind == INVALID_HANDLE_VALUE) ret = 0;
    FindClose(hfind);

    if (ret == 0) {
	char *p;

	ret = SearchPath(NULL, prog, NULL, MAXLEN, tmp, &p);
    }

    return ret;
}
#else
static int unix_check_for_prog (const char *prog)
{
    char tmp[MAXLEN];

    if (prog == NULL || *prog == 0) return 0;

    sprintf(tmp, "%s > /dev/null 2>&1", prog);
    return (system(tmp) == 0);
}
#endif

static int check_for_prog (const char *prog)
{
#ifdef G_OS_WIN32
    return win32_check_for_prog(prog);
#else
    return unix_check_for_prog(prog);
#endif
}

static void set_tramo_x12a_dirs (void)
{
#ifndef G_OS_WIN32
    char cmd[MAXLEN];
    DIR *test;
#endif

#ifdef HAVE_TRAMO 
    set_tramo_ok(check_for_prog(tramo));
    if (*tramodir == 0) {
	build_path(paths.userdir, "tramo", tramodir, NULL);
    }
#endif
#ifdef HAVE_X12A
    set_x12a_ok(check_for_prog(x12a));
    if (*x12adir == 0) {
	build_path(paths.userdir, "x12arima", x12adir, NULL);
    }
#endif

#ifndef G_OS_WIN32
    /* don't make dir structure (yet) if userdir doesn't exist */
    test = opendir(paths.userdir);
    if (test == NULL) {
	return;
    } else {
	closedir(test);
    }
# ifdef HAVE_X12A
    sprintf(cmd, "mkdir -p %s", x12adir);
    system(cmd);
# endif
# ifdef HAVE_TRAMO
    sprintf(cmd, "mkdir -p %s/output", tramodir);
    system(cmd);
    sprintf(cmd, "mkdir -p %s/graph/acf", tramodir);
    system(cmd);
    sprintf(cmd, "mkdir -p %s/graph/filters", tramodir);
    system(cmd);
    sprintf(cmd, "mkdir -p %s/graph/forecast", tramodir);
    system(cmd);
    sprintf(cmd, "mkdir -p %s/graph/series", tramodir);
    system(cmd);
    sprintf(cmd, "mkdir -p %s/graph/spectra", tramodir);
    system(cmd);
# endif /* HAVE_TRAMO */
#endif /* not win32 */
}
#endif /* tramo or x12a */

/* ........................................................... */

#if !defined(G_OS_WIN32) && !defined(USE_GNOME)
void set_rcfile (void) 
{
    char *tmp;

    tmp = getenv("HOME");
    strcpy(rcfile, tmp);
    strcat(rcfile, "/.gretl2rc");
    read_rc(); 
}
#endif

#ifdef USE_GNOME
void set_rcfile (void)
{
    read_rc();
}
#endif

/* .................................................................. */

void options_dialog (gpointer data) 
{
    GtkWidget *tempwid, *dialog, *notebook;

    dialog = gtk_dialog_new ();
    gtk_window_set_title (GTK_WINDOW (dialog), _("gretl: options"));
    gtk_container_set_border_width 
	(GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 10);
    gtk_container_set_border_width 
	(GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
    gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 15);
    gtk_box_set_homogeneous (GTK_BOX (GTK_DIALOG (dialog)->action_area), TRUE);
    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
    g_signal_connect (G_OBJECT (dialog), "delete_event", 
		      G_CALLBACK (delete_widget), 
		      dialog);
   
    notebook = gtk_notebook_new ();
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), 
			notebook, TRUE, TRUE, 0);
    gtk_widget_show (notebook);

    make_prefs_tab (notebook, 1);
    make_prefs_tab (notebook, 2);
    make_prefs_tab (notebook, 3);
    make_prefs_tab (notebook, 4);
    make_prefs_tab (notebook, 5);
   
    tempwid = standard_button (GTK_STOCK_OK);
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG 
				 (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT (tempwid), "clicked", 
		      G_CALLBACK (apply_changes), NULL);
    g_signal_connect (G_OBJECT (tempwid), "clicked", 
		      G_CALLBACK (delete_widget), 
		      dialog);
    gtk_widget_show (tempwid);

    tempwid = standard_button (GTK_STOCK_CANCEL);
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG 
				 (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT (tempwid), "clicked", 
		      G_CALLBACK (delete_widget), 
		      dialog);
    gtk_widget_show (tempwid);

    tempwid = standard_button (GTK_STOCK_APPLY);
    GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (GTK_DIALOG 
				 (dialog)->action_area), 
			tempwid, TRUE, TRUE, 0);
    g_signal_connect (G_OBJECT (tempwid), "clicked", 
		      G_CALLBACK (apply_changes), NULL);
    gtk_widget_grab_default (tempwid);
    gtk_widget_show (tempwid);

    gtk_widget_show (dialog);
}

/* .................................................................. */

static void flip_sensitive (GtkWidget *w, gpointer data)
{
    GtkWidget *entry = GTK_WIDGET(data);
    
    gtk_widget_set_sensitive(entry, GTK_TOGGLE_BUTTON(w)->active);
}

/* .................................................................. */

void filesel_set_path_callback (const char *setting, char *strvar)
{
    int i = 0;

    while (rc_vars[i].key != NULL) {
	if (rc_vars[i].var == (void *) strvar) {
	    /* FIXME: utf-8 issues here?? */
	    gtk_entry_set_text(GTK_ENTRY(rc_vars[i].widget), 
			       setting);
	    break;
	}
	i++;
    }
}

/* .................................................................. */

static void browse_button_callback (GtkWidget *w, RCVARS *rc)
{
    file_selector(_(rc->description), SET_PATH, rc->var);
}

/* .................................................................. */

static GtkWidget *make_path_browse_button (RCVARS *rc)
{
    GtkWidget *b;

    b = gtk_button_new_with_label(_("Browse..."));
    g_signal_connect(G_OBJECT(b), "clicked",
		     G_CALLBACK(browse_button_callback), 
		     rc);
    return b;
}

/* .................................................................. */

static void make_prefs_tab (GtkWidget *notebook, int tab) 
{
    GtkWidget *box, *b_table, *s_table, *tempwid = NULL;
    int i, s_len, b_len, b_col;
    int s_count = 0, b_count = 0;
    RCVARS *rc = NULL;
   
    box = gtk_vbox_new (FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (box), 10);
    gtk_widget_show (box);

    if (tab == 1)
	tempwid = gtk_label_new (_("General"));
    else if (tab == 2)
	tempwid = gtk_label_new (_("Databases"));
    else if (tab == 3)
	tempwid = gtk_label_new (_("Programs"));
    else if (tab == 4)
	tempwid = gtk_label_new (_("Open/Save path"));
    else if (tab == 5)
	tempwid = gtk_label_new (_("Data files"));
    
    gtk_widget_show (tempwid);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), box, tempwid);   

    s_len = 1;
    s_table = gtk_table_new (s_len, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (s_table), 5);
    gtk_table_set_col_spacings (GTK_TABLE (s_table), 5);
    gtk_box_pack_start (GTK_BOX (box), s_table, FALSE, FALSE, 0);
    gtk_widget_show (s_table);

    b_len = b_col = 0;
    b_table = gtk_table_new (1, 2, FALSE);
    gtk_table_set_row_spacings (GTK_TABLE (b_table), 5);
    gtk_table_set_col_spacings (GTK_TABLE (b_table), 5);
    gtk_box_pack_start (GTK_BOX (box), b_table, FALSE, FALSE, 10);
    gtk_widget_show (b_table);

    i = 0;
    while (rc_vars[i].key != NULL) {
	rc = &rc_vars[i];
	if (rc->tab == tab) {
	    if (rc->type == 'B' && rc->link == NULL) { 
		/* simple boolean variable */
		b_count++;
		tempwid = gtk_check_button_new_with_label 
		    (_(rc->description));
		gtk_table_attach_defaults 
		    (GTK_TABLE (b_table), tempwid, b_col, b_col + 1, 
		     b_len, b_len + 1);
		if (*(int *)(rc->var)) {
		    gtk_toggle_button_set_active 
			(GTK_TOGGLE_BUTTON (tempwid), TRUE);
		} else {
		    gtk_toggle_button_set_active 
			(GTK_TOGGLE_BUTTON (tempwid), FALSE);
		}
		/* special case: link between toggle and preceding entry */
		if (rc->len) {
		    gtk_widget_set_sensitive(rc_vars[i-1].widget,
					     GTK_TOGGLE_BUTTON(tempwid)->active);
		    g_signal_connect(G_OBJECT(tempwid), "clicked",
				     G_CALLBACK(flip_sensitive),
				     rc_vars[i-1].widget);
		}
		/* end link to entry */
		gtk_widget_show (tempwid);
		rc->widget = tempwid;
		b_col++;
		if (b_col == 2) {
		    b_col = 0;
		    b_len++;
		    gtk_table_resize (GTK_TABLE (b_table), b_len + 1, 2);
		}
	    } else if (rc->type == 'B') { 
		/* radio-button dichotomy */
		int val = *(int *)(rc->var);
		GSList *group;

		b_count++;
		b_len += 3;
		gtk_table_resize (GTK_TABLE(b_table), b_len + 1, 2);

		/* first a separator for the group */
		tempwid = gtk_hseparator_new ();
		gtk_table_attach_defaults 
		    (GTK_TABLE (b_table), tempwid, b_col, b_col + 1, 
		     b_len - 3, b_len - 2);  
		gtk_widget_show (tempwid);

		/* then a first button */
		tempwid = gtk_radio_button_new_with_label(NULL, 
							  _(rc->link));
		gtk_table_attach_defaults 
		    (GTK_TABLE (b_table), tempwid, b_col, b_col + 1, 
		     b_len - 2, b_len - 1);    
		if (!val) {
		    gtk_toggle_button_set_active 
			(GTK_TOGGLE_BUTTON(tempwid), TRUE);
		}
		gtk_widget_show (tempwid);
		group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(tempwid));

		/* and a second button */
		tempwid = gtk_radio_button_new_with_label(group, 
							  _(rc->description));
		gtk_table_attach_defaults 
		    (GTK_TABLE (b_table), tempwid, b_col, b_col + 1, 
		     b_len - 1, b_len);  
		if (val) {
		    gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON(tempwid), TRUE);
		}
		rc->widget = tempwid;
		gtk_widget_show (tempwid);
	    } else if (rc->type != 'I') { /* string variable */
		s_count++;
		s_len++;
		gtk_table_resize (GTK_TABLE (s_table), s_len, 
				  (tab == 3)? 3 : 2);
		tempwid = gtk_label_new (_(rc->description));
		gtk_misc_set_alignment (GTK_MISC (tempwid), 1, 0.5);
		gtk_table_attach_defaults (GTK_TABLE (s_table), 
					   tempwid, 0, 1, s_len - 1, s_len);
		gtk_widget_show (tempwid);

		tempwid = gtk_entry_new ();
		gtk_table_attach_defaults (GTK_TABLE (s_table), 
					   tempwid, 1, 2, s_len-1, s_len);
		gtk_entry_set_text (GTK_ENTRY (tempwid), rc->var);
		gtk_widget_show (tempwid);
		rc->widget = tempwid;
		/* program browse button */
		if (tab == 3 && strstr(rc->description, "directory") == NULL) {
		    tempwid = make_path_browse_button(rc);
		    gtk_table_attach_defaults(GTK_TABLE(s_table), 
					      tempwid, 2, 3, s_len-1, s_len);
		    gtk_widget_show(tempwid);
		}		
	    }
	} /* end if (rc->tab == tab) */
	i++;
    } /* end of loop over rc_vars[i].key */

    if (b_count == 0) gtk_widget_destroy(b_table);
    if (s_count == 0) gtk_widget_destroy(s_table);

}

/* .................................................................. */

#ifdef ENABLE_NLS
static void set_lcnumeric (void)
{
    if (lcnumeric) {
#ifdef G_OS_WIN32
	char *lang = getenv("LANG");

	if (lang != NULL && !strcmp(lang, "es")) {
	    setlocale(LC_NUMERIC, "Spanish");
	}
	else if (lang != NULL && !strcmp(lang, "fr")) {
	    setlocale(LC_NUMERIC, "French");
	}
	else setlocale(LC_NUMERIC, "");
	putenv("LC_NUMERIC=");
#else
	putenv("LC_NUMERIC=");
	setlocale(LC_NUMERIC, "");
#endif
    } else {
	putenv("LC_NUMERIC=C");
	setlocale(LC_NUMERIC, "C");
    }
    reset_local_decpoint();
}
#endif

/* .................................................................. */

static void apply_changes (GtkWidget *widget, gpointer data) 
{
    const gchar *tempstr;
    extern void show_toolbar (void);
    int i = 0;
#ifdef ENABLE_NLS
    int lcnum_bak = lcnumeric;
#endif

    while (rc_vars[i].key != NULL) {
	if (rc_vars[i].widget != NULL) {
	    if (rc_vars[i].type == 'B') {
		if (GTK_TOGGLE_BUTTON(rc_vars[i].widget)->active)
		    *(int *)(rc_vars[i].var) = TRUE;
		else *(int *)(rc_vars[i].var) = FALSE;
	    } 
	    if (rc_vars[i].type == 'U' || rc_vars[i].type == 'R') {
		tempstr = gtk_entry_get_text
		    (GTK_ENTRY(rc_vars[i].widget));
		if (tempstr != NULL && strlen(tempstr)) 
		    strncpy(rc_vars[i].var, tempstr, rc_vars[i].len - 1);
	    }
	}
	i++;
    }

    write_rc();

    if (toolbar_box == NULL && want_toolbar)
	show_toolbar();
    else if (toolbar_box != NULL && !want_toolbar) {
	gtk_widget_destroy(toolbar_box);
	toolbar_box = NULL;
    }

    set_use_qr(useqr);

#ifdef ENABLE_NLS
    set_lcnumeric();
    if (lcnumeric != lcnum_bak) 
	infobox(_("Please restart gretl to ensure consistent results"));
#endif

#if defined(HAVE_TRAMO) || defined (HAVE_X12A)
    set_tramo_x12a_dirs();
#endif

    proxy_init(dbproxy);
}

/* .................................................................. */

#ifndef USE_GNOME
static void str_to_boolvar (char *s, void *b)
{
    if (strcmp(s, "true") == 0 || strcmp(s, "1") == 0) {
	*(int *)b = TRUE;
    } else {
	*(int *)b = FALSE;
    }	
}

static void boolvar_to_str (void *b, char *s)
{
    if (*(int *)b) strcpy(s, "true");
    else strcpy(s, "false");
}
#endif

/* .................................................................. */

#if defined(USE_GNOME)

void write_rc (void) 
{
    GConfClient *client;   
    char key[MAXSTR];
    int i = 0;

    client = gconf_client_get_default();

    while (rc_vars[i].key != NULL) {
	sprintf(key, "/apps/gretl/%s", rc_vars[i].key);
	if (rc_vars[i].type == 'B') {
	    gboolean val = *(gboolean *) rc_vars[i].var;

	    gconf_client_set_bool (client, key, val, NULL);
	} else {
	    gconf_client_set_string (client, key, rc_vars[i].var, NULL);
	}
	i++;
    }

    g_object_unref(G_OBJECT(client));

    printfilelist(FILE_LIST_DATA, NULL);
    printfilelist(FILE_LIST_SESSION, NULL); 
    printfilelist(FILE_LIST_SCRIPT, NULL);

    set_paths(&paths, 0, 1);
}

static void read_rc (void) 
{
    GConfClient *client;
    GError *error = NULL;
    GSList *flist = NULL;
    gchar *value;
    char key[MAXSTR];
    int i = 0;
    static char *sections[] = {
	"recent_data_files",
	"recent_session_files",
	"recent_script_files"
    };	

    client = gconf_client_get_default();

    while (rc_vars[i].key != NULL) {
	sprintf(key, "/apps/gretl/%s", rc_vars[i].key);
	if (rc_vars[i].type == 'B') {
	    gboolean val;

	    val = gconf_client_get_bool (client, key, &error);
	    if (error) {
		fprintf(stderr, "Error reading %s\n", rc_vars[i].key);
		g_clear_error(&error);
	    } else {
		*(int *) rc_vars[i].var = val;
	    }
	} else {
	    value = gconf_client_get_string (client, key, &error);

	    if (error) {
		fprintf(stderr, "Error reading %s\n", rc_vars[i].key);
		g_clear_error(&error);
	    } else if (value != NULL) {
		char *s = (char *) rc_vars[i].var;

		*s = 0;
		strncat(s, value, rc_vars[i].len - 1);
		g_free(value);
	    }
	}
	i++;
    }

    /* initialize lists of recently opened files */
    for (i=0; i<MAXRECENT; i++) { 
	datalist[i][0] = 0;
	sessionlist[i][0] = 0;
	scriptlist[i][0] = 0;
    }

    for (i=0; i<3; i++) {
	int j;

	sprintf(key, "/apps/gretl/%s", sections[i]);
	flist = gconf_client_get_list (client, key,
				       GCONF_VALUE_STRING, NULL);
	if (flist != NULL) {
	    for (j=0; j<MAXRECENT; j++) {
		if (i == 0) strcpy(datalist[j], flist->data);
		else if (i == 1) strcpy(sessionlist[j], flist->data);
		else if (i == 2) strcpy(scriptlist[j], flist->data);
		flist = flist->next;
	    }
	    g_slist_free(flist);
	    flist = NULL;
	}
    }

    g_object_unref(G_OBJECT(client));

    set_use_qr(useqr);

    set_paths(&paths, 0, 1); /* 0 = not defaults, 1 = gui */
#if defined(HAVE_TRAMO) || defined (HAVE_X12A)
    set_tramo_x12a_dirs();
#endif
#ifdef ENABLE_NLS
    set_lcnumeric();
#endif
}

/* end of gnome versions, now win32 */
#elif defined(G_OS_WIN32)

static const char *get_reg_base (const char *key)
{
    if (strncmp(key, "x12a", 4) == 0) {
	return "x12arima";
    }
    if (strncmp(key, "tramo", 5) == 0) {
	return "tramo";
    }
    return "gretl";
}

void write_rc (void) 
{
    int i = 0;
    char val[6];

    while (rc_vars[i].key != NULL) {
	if (rc_vars[i].type == 'B') {
	    boolvar_to_str(rc_vars[i].var, val);
	    write_reg_val(HKEY_CURRENT_USER, "gretl", rc_vars[i].key, val);
	} else {
	    write_reg_val((rc_vars[i].type == 'R')? 
			  HKEY_CLASSES_ROOT : HKEY_CURRENT_USER, 
			  get_reg_base(rc_vars[i].key),
			  rc_vars[i].key, rc_vars[i].var);
	}
	i++;
    }

    printfilelist(FILE_LIST_DATA, NULL); 
    printfilelist(FILE_LIST_SESSION, NULL);
    printfilelist(FILE_LIST_SCRIPT, NULL);

    set_paths(&paths, 0, 1);
}

void read_rc (void) 
{
    int i = 0;
    char rpath[MAXSTR], value[MAXSTR];

    while (rc_vars[i].key != NULL) {
	if (read_reg_val((rc_vars[i].type == 'R')? 
			 HKEY_CLASSES_ROOT : HKEY_CURRENT_USER, 
			 get_reg_base(rc_vars[i].key),
			 rc_vars[i].key, value) == 0) {
	    if (rc_vars[i].type == 'B') {
		str_to_boolvar(value, rc_vars[i].var);
	    } else {
		strncpy(rc_vars[i].var, value, rc_vars[i].len - 1);
	    }
	}
	i++;
    }

    /* initialize lists of recently opened files */
    for (i=0; i<MAXRECENT; i++) { 
	datalist[i][0] = 0;
	sessionlist[i][0] = 0;
	scriptlist[i][0] = 0;
    }
    /* get recent file lists */
    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "recent data files\\%d", i);
	if (read_reg_val(HKEY_CURRENT_USER, "gretl", rpath, value) == 0) 
	    strcpy(datalist[i], value);
	else break;
    }    
    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "recent session files\\%d", i);
	if (read_reg_val(HKEY_CURRENT_USER, "gretl", rpath, value) == 0) 
	    strcpy(sessionlist[i], value);
	else break;
    } 
    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "recent script files\\%d", i);
	if (read_reg_val(HKEY_CURRENT_USER, "gretl", rpath, value) == 0) 
	    strcpy(scriptlist[i], value);
	else break;
    }

    set_use_qr(useqr);

    set_paths(&paths, 0, 1);
#if defined(HAVE_TRAMO) || defined (HAVE_X12A)
    set_tramo_x12a_dirs();
#endif
    set_fixed_font();
    set_app_font(NULL);

#ifdef ENABLE_NLS
    set_lcnumeric();
#endif
}

#else /* end of win32 versions, now plain GTK */

void write_rc (void) 
{
    FILE *rc;
    int i;
    char val[6];

    rc = fopen(rcfile, "w");
    if (rc == NULL) {
	errbox(_("Couldn't open config file for writing"));
	return;
    }

    fprintf(rc, "# gretl config file (note: not used by gnome version)\n");

    i = 0;
    while (rc_vars[i].var != NULL) {
	fprintf(rc, "# %s\n", rc_vars[i].description);
	if (rc_vars[i].type == 'B') {
	    boolvar_to_str(rc_vars[i].var, val);
	    fprintf(rc, "%s = %s\n", rc_vars[i].key, val);
	} else {
	    fprintf(rc, "%s = %s\n", rc_vars[i].key, (char *) rc_vars[i].var);
	}
	i++;
    }

    printfilelist(FILE_LIST_DATA, rc);
    printfilelist(FILE_LIST_SESSION, rc);
    printfilelist(FILE_LIST_SCRIPT, rc);

    fclose(rc);
    set_paths(&paths, 0, 1);
}

static void read_rc (void) 
{
    FILE *rc;
    int i, j;
    char line[MAXLEN], key[32], linevar[MAXLEN];
    int gotrecent = 0;

    if ((rc = fopen(rcfile, "r")) == NULL) return;

    i = 0;
    while (rc_vars[i].var != NULL) {
	if (fgets(line, MAXLEN, rc) == NULL) 
	    break;
	if (line[0] == '#') 
	    continue;
	if (!strncmp(line, "recent ", 7)) {
	    gotrecent = 1;
	    break;
	}
	if (sscanf(line, "%s", key) == 1) {
	    strcpy(linevar, line + strlen(key) + 3); 
	    chopstr(linevar); 
	    for (j=0; rc_vars[j].key != NULL; j++) {
		if (!strcmp(key, rc_vars[j].key)) {
		    if (rc_vars[j].type == 'B') {
			str_to_boolvar(linevar, rc_vars[j].var);
		    } else {
			strcpy(rc_vars[j].var, linevar);
		    }
		}
	    }
	}
	i++;
    }

    /* get lists of recently opened files */
    for (i=0; i<MAXRECENT; i++) { 
	datalist[i][0] = 0;
	sessionlist[i][0] = 0;
	scriptlist[i][0] = 0;
    }

    if (gotrecent || (fgets(line, MAXLEN, rc) != NULL && 
		      strncmp(line, "recent data files:", 18) == 0)) {
	i = 0;
	while (fgets(line, MAXLEN, rc) && i<MAXRECENT) {
	    if (strncmp(line, "recent session files:", 21) == 0)
		break;
	    chopstr(line);
	    if (strlen(line)) 
		strcpy(datalist[i++], line);
	}
    }

    if (strncmp(line, "recent session files:", 21) == 0) {
	i = 0;
	while (fgets(line, MAXLEN, rc) && i<MAXRECENT) {
	    if (strncmp(line, "recent script files:", 20) == 0)
		break;
	    chopstr(line);
	    if (strlen(line)) 
		strcpy(sessionlist[i++], line);
	}
    }

    if (strncmp(line, "recent script files:", 20) == 0) {
	i = 0;
	while (fgets(line, MAXLEN, rc) && i<MAXRECENT) {
	    chopstr(line);
	    if (strlen(line)) 
		strcpy(scriptlist[i++], line);
	}
    }

    fclose(rc);

    set_use_qr(useqr);

    set_paths(&paths, 0, 1);
#if defined(HAVE_TRAMO) || defined(HAVE_X12A)
    set_tramo_x12a_dirs();
#endif
#ifdef ENABLE_NLS
    set_lcnumeric();
#endif
}

#endif /* end of "plain gtk" versions of read_rc, write_rc */

/* font selection: non-Windows version first */
#ifndef G_OS_WIN32

static void font_selection_ok (GtkWidget *w, GtkFontSelectionHackDialog *fs)
{
    gint mono = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(fs), "mono"));
    gchar *fontname;

    fontname = gtk_font_selection_hack_dialog_get_font_name(fs);

# ifdef USE_GNOME /* gnome handles the app font */
    if (mono && strlen(fontname)) {
	strcpy(fixedfontname, fontname);
	set_fixed_font();
	write_rc();
    }
# else
    if (strlen(fontname)) {
	if (mono) {
	    strcpy(fixedfontname, fontname);
	    set_fixed_font();
	} else {
	    set_app_font(fontname);
	}
	write_rc();
    }
# endif /* USE_GNOME */

    g_free(fontname);
    gtk_widget_destroy(GTK_WIDGET(fs));
}

void font_selector (gpointer data, guint fixed, GtkWidget *widget)
{
    static GtkWidget *fontsel = NULL;

    if (fontsel != NULL) {
	if (!GTK_WIDGET_VISIBLE(fontsel)) gtk_widget_show (fontsel);
        gdk_window_raise(fontsel->window);
        return;
    }
	
    if (fixed) {
	fontsel = gtk_font_selection_hack_dialog_new 
	    (_("Font for gretl output windows"));
	gtk_font_selection_hack_dialog_set_filter
	    (GTK_FONT_SELECTION_HACK_DIALOG (fontsel), 
	     GTK_FONT_HACK_LATIN_MONO);
	gtk_font_selection_hack_dialog_set_font_name 
	    (GTK_FONT_SELECTION_HACK_DIALOG (fontsel), fixedfontname);
	g_object_set_data(G_OBJECT(fontsel), "mono", GINT_TO_POINTER(1));
    } else {
# ifdef USE_GNOME
	return; /* shouldn't be reached: gnome handles the app font */
# else
	fontsel = gtk_font_selection_hack_dialog_new 
	    (_("Font for menus and labels"));
	gtk_font_selection_hack_dialog_set_filter
	    (GTK_FONT_SELECTION_HACK_DIALOG (fontsel), 
	     GTK_FONT_HACK_LATIN);
	gtk_font_selection_hack_dialog_set_font_name 
	    (GTK_FONT_SELECTION_HACK_DIALOG (fontsel), appfontname);
	g_object_set_data(G_OBJECT(fontsel), "mono", GINT_TO_POINTER(0));
# endif /* USE_GNOME */
    }

    gtk_window_set_position (GTK_WINDOW (fontsel), GTK_WIN_POS_MOUSE);

    g_signal_connect (G_OBJECT(fontsel), "destroy",
		      G_CALLBACK(gtk_widget_destroyed),
		      &fontsel);
    g_signal_connect (G_OBJECT 
		      (GTK_FONT_SELECTION_HACK_DIALOG 
		       (fontsel)->ok_button),
		      "clicked", G_CALLBACK(font_selection_ok),
		      GTK_FONT_SELECTION_HACK_DIALOG (fontsel));
    g_signal_connect(G_OBJECT(GTK_FONT_SELECTION_HACK_DIALOG(fontsel)->cancel_button),
		     "clicked", 
		     G_CALLBACK(delete_widget),
		     fontsel);

    gtk_widget_show (fontsel);	
}

#else /* end non-win32 font selection, start win32 */

static const char *font_weight_string (int weight)
{
    if (weight >= FW_THIN && weight <= FW_LIGHT) return " Light";
    if (weight >= FW_NORMAL && weight <= FW_DEMIBOLD) return "";
    if (weight >= FW_BOLD) return " Bold";
    return "";
}

static void fontname_to_win32 (const char *src, int fixed,
			       char *name, int *pts)
{
    int sz;

    if (sscanf(src, "%31[^0123456789]%d", name, &sz) == 2) {
	size_t i, n = strlen(name);

	for (i=n-1; i>0; i--) {
	    if (name[i] == ' ') name[i] = 0;
	    else break;
	}
	*pts = sz * 10; /* measured in tenths of a point */
    } else {
	*name = 0;
	strncat(name, src, 31);
	if (fixed) *pts = 100;
	else *pts = 80;
    }
}

void font_selector (gpointer data, guint fixed, GtkWidget *widget)
{
    CHOOSEFONT cf;            /* common dialog box structure */
    LOGFONT lf;               /* logical font structure */
    char fontname[48];

    ZeroMemory(&cf, sizeof cf);
    cf.lStructSize = sizeof cf;
    cf.Flags = CF_SCREENFONTS | CF_TTONLY | CF_LIMITSIZE | CF_INITTOLOGFONTSTRUCT;
    cf.nSizeMin = 6;
    cf.nSizeMax = 24;

    ZeroMemory(&lf, sizeof lf);

    cf.Flags |= CF_NOSCRIPTSEL;
    
    lf.lfWeight = FW_REGULAR;
    lf.lfCharSet = DEFAULT_CHARSET;

    if (fixed) {
	cf.Flags |= CF_FIXEDPITCHONLY;
	fontname_to_win32(fixedfontname, 1, lf.lfFaceName, &(cf.iPointSize));
    } else {
	fontname_to_win32(appfontname, 0, lf.lfFaceName, &(cf.iPointSize));
    }

    cf.lpLogFont = &lf;

    if (ChooseFont(&cf) == TRUE && *(cf.lpLogFont->lfFaceName)) {
	sprintf(fontname, "%s%s %d", cf.lpLogFont->lfFaceName, 
		font_weight_string(cf.lpLogFont->lfWeight), 
		cf.iPointSize / 10);
	if (fixed) {
	    strcpy(fixedfontname, fontname);
	    set_fixed_font();
	} else {
	    set_app_font(fontname);
	}
	write_rc();
    }
}

#endif /* end win32 font selection */

/* .................................................................. */

void init_fileptrs (void)
{
    int i;
    
    for (i=0; i<MAXRECENT; i++) {
	datap[i] = datalist[i];
	sessionp[i] = sessionlist[i];
	scriptp[i] = scriptlist[i];
    }
}

/* .................................................................. */

static void clear_files_list (int filetype, char **filep)
{
    GtkWidget *w;
    char tmpname[MAXSTR];
    gchar itempath[80];
    int i, pindex = -1;
    const gchar *fpath[] = {
	N_("/File/Open data"), 
	N_("/Session"),
	N_("/File/Open command file")
    };

    if (filetype == FILE_LIST_DATA) 
	pindex = 0;
    else if (filetype == FILE_LIST_SESSION)
	pindex = 1;
    else if (filetype == FILE_LIST_SCRIPT)
	pindex = 2;

    if (pindex == -1) return;

    for (i=0; i<MAXRECENT; i++) {
	sprintf(itempath, "%s/%d. %s", fpath[pindex],
		i+1, endbit(tmpname, filep[i], 0));
	w = gtk_item_factory_get_widget(mdata->ifac, itempath);
	if (w != NULL) {
	    gtk_item_factory_delete_item(mdata->ifac, itempath);
	}
    }
}

/* .................................................................. */

void mkfilelist (int filetype, const char *fname)
{
    char *tmp[MAXRECENT-1];
    char **filep;
    int i, match = -1;

    if (filetype == FILE_LIST_DATA) filep = datap;
    else if (filetype == FILE_LIST_SESSION) filep = sessionp;
    else if (filetype == FILE_LIST_SCRIPT) filep = scriptp;
    else return;

    /* see if this file is already on the list */
    for (i=0; i<MAXRECENT; i++) {
        if (strcmp(filep[i], fname) == 0) {
            match = i;
            break;
        }
    }
    if (match == 0) return; /* file is on top: no change in list */

    /* clear menu files list before rebuilding */
    clear_files_list(filetype, filep);
    
    /* save pointers to current order */
    for (i=0; i<MAXRECENT-1; i++) tmp[i] = filep[i];

    /* copy fname into array, if not already present */
    if (match == -1) {
        for (i=1; i<MAXRECENT; i++) {
            if (filep[i][0] == '\0') {
                strcpy(filep[i], fname);
                match = i;
                break;
	    }
	    if (match == -1) {
		match = MAXRECENT - 1;
		strcpy(filep[match], fname);
	    }
	}
    } 

    /* set first pointer to new file */
    filep[0] = filep[match];

    /* rearrange other pointers */
    for (i=1; i<=match; i++) {
	filep[i] = tmp[i-1];
    }

    add_files_to_menu(filetype);
}

/* .................................................................. */

void delete_from_filelist (int filetype, const char *fname)
{
    char *tmp[MAXRECENT];
    char **filep;
    int i, match = -1;

    if (filetype == FILE_LIST_DATA) filep = datap;
    else if (filetype == FILE_LIST_SESSION) filep = sessionp;
    else if (filetype == FILE_LIST_SCRIPT) filep = scriptp;
    else return;

    /* save pointers to current order */
    for (i=0; i<MAXRECENT; i++) {
	tmp[i] = filep[i];
	if (!strcmp(filep[i], fname)) match = i;
    }

    if (match == -1) return;

    /* clear menu files list before rebuilding */
    clear_files_list(filetype, filep);

    for (i=match; i<MAXRECENT-1; i++) {
	filep[i] = tmp[i+1];
    }

    filep[MAXRECENT-1] = tmp[match];
    filep[MAXRECENT-1][0] = '\0';

    add_files_to_menu(filetype);
    /* need to save to file at this point? */
}

/* .................................................................. */

char *endbit (char *dest, char *src, int addscore)
{
    /* take last part of src filename */
    if (strrchr(src, SLASH))
	strcpy(dest, strrchr(src, SLASH) + 1);
    else
	strcpy(dest, src);

    if (addscore != 0) {
	/* then either double (1) or delete (-1) any underscores */
	char mod[MAXSTR];
	size_t i, j, n;

	n = strlen(dest);
	j = 0;
	for (i=0; i<=n; i++) {
	    if (dest[i] != '_')
		mod[j++] = dest[i];
	    else {
		if (addscore == 1) {
		    mod[j++] = '_';
		    mod[j++] = dest[i];
		} 
	    }
	}
	strcpy(dest, mod);
    }
    return dest;
}

/* .................................................................. */

#if defined(USE_GNOME)

static void printfilelist (int filetype, FILE *fp)
     /* param fp is ignored */
{
    GConfClient *client;
    GSList *flist = NULL;
    gchar *key;
    int i;
    char **filep;
    static char *sections[] = {
	"recent_data_files",
	"recent_session_files",
	"recent_script_files"
    };

    switch (filetype) {
    case FILE_LIST_DATA: filep = datap; break;
    case FILE_LIST_SESSION: filep = sessionp; break;
    case FILE_LIST_SCRIPT: filep = scriptp; break;
    default: return;
    }

    client = gconf_client_get_default();

    for (i=0; i<MAXRECENT; i++) {
	flist = g_slist_append (flist, g_strdup(filep[i]));
    }

    key = g_strdup_printf("/apps/gretl/%s", sections[filetype - 1]);

    gconf_client_set_list (client, key, GCONF_VALUE_STRING, 
			   flist, NULL);

    g_free(key);
    g_slist_free(flist);
    g_object_unref(G_OBJECT(client));
}

#elif defined(G_OS_WIN32)

static void printfilelist (int filetype, FILE *fp)
     /* param fp is ignored */
{
    int i;
    char **filep;
    char rpath[MAXLEN];
    static char *sections[] = {
	"recent data files",
	"recent session files",
	"recent script files"
    };

    switch (filetype) {
    case FILE_LIST_DATA: filep = datap; break;
    case FILE_LIST_SESSION: filep = sessionp; break;
    case FILE_LIST_SCRIPT: filep = scriptp; break;
    default: return;
    }

    for (i=0; i<MAXRECENT; i++) {
	sprintf(rpath, "%s\\%d", sections[filetype - 1], i);
	write_reg_val(HKEY_CURRENT_USER, "gretl", rpath, filep[i]);
    }
}

#else /* "plain" version follows */

static void printfilelist (int filetype, FILE *fp)
{
    int i;
    char **filep;

    if (filetype == FILE_LIST_DATA) {
	fprintf(fp, "recent data files:\n");
	filep = datap;
    } else if (filetype == FILE_LIST_SESSION) {
	fprintf(fp, "recent session files:\n");
	filep = sessionp;
    } else if (filetype == FILE_LIST_SCRIPT) {
	fprintf(fp, "recent script files:\n");
	filep = scriptp;
    } else 
	return;

    for (i=0; i<MAXRECENT; i++) {
	if (filep[i][0]) 
	    fprintf(fp, "%s\n", filep[i]);
	else break;
    }
}

#endif 

/* ........................................................... */

static void set_data_from_filelist (gpointer data, guint i, 
				    GtkWidget *widget)
{
    strcpy(trydatfile, datap[i]);
    if (strstr(trydatfile, ".csv")) delimiter_dialog();
    verify_open_data(NULL, 0);
}

/* ........................................................... */

static void set_session_from_filelist (gpointer data, guint i, 
				       GtkWidget *widget)
{
    strcpy(tryscript, sessionp[i]);
    verify_open_session(NULL);
}

/* ........................................................... */

static void set_script_from_filelist (gpointer data, guint i, 
				      GtkWidget *widget)
{
    strcpy(tryscript, scriptp[i]);
    do_open_script();
}

/* .................................................................. */

void add_files_to_menu (int filetype)
{
    int i;
    char **filep, tmp[MAXSTR];
    void (*callfunc)();
    GtkItemFactoryEntry fileitem;
    GtkWidget *w;
    const gchar *msep[] = {
	N_("/File/Open data/sep"),
	N_("/Session/sep"),
	N_("/File/Open command file/sep")
    };
    const gchar *mpath[] = {
	N_("/File/Open data"),
	N_("/Session"),
	N_("/File/Open command file")
    };

    fileitem.path = NULL;

    if (filetype == FILE_LIST_DATA) {
	callfunc = set_data_from_filelist;
	filep = datap;
    } else if (filetype == FILE_LIST_SESSION) {
	callfunc = set_session_from_filelist;
	filep = sessionp;
    } else if (filetype == FILE_LIST_SCRIPT) {
	callfunc = set_script_from_filelist;
	filep = scriptp;
    }
    else return;

    /* See if there are any files to add */
    if (filep[0][0] == '\0') {
	return;
    } else {
	gchar *itemtype = "<Separator>";
	GtkWidget *w;

	/* is a separator already in place? */
	w = gtk_item_factory_get_widget(mdata->ifac, msep[filetype - 1]);
	if (w == NULL) {
	    fileitem.path = g_strdup(msep[filetype - 1]);
	    fileitem.accelerator = NULL;
	    fileitem.callback = NULL;
	    fileitem.callback_action = 0;
	    fileitem.item_type = itemtype;
	    gtk_item_factory_create_item(mdata->ifac, &fileitem, NULL, 1);
	    g_free(fileitem.path);
	}
    }

    /* put the files under the menu separator */
    for (i=0; i<MAXRECENT; i++) {
	if (filep[i][0]) {
	    fileitem.accelerator = NULL;
	    fileitem.callback_action = i; 
	    fileitem.item_type = NULL;
	    fileitem.path = g_strdup_printf("%s/%d. %s", mpath[filetype - 1],
					    i+1, endbit(tmp, filep[i], 1));
	    fileitem.callback = callfunc; 
	    gtk_item_factory_create_item(mdata->ifac, &fileitem, NULL, 1);
	    g_free(fileitem.path);
	    w = gtk_item_factory_get_widget_by_action(mdata->ifac, i);
	    if (w != NULL) {
		gretl_tooltips_add(w, filep[i]);
	    } 
	} else break;
    }
}

static double scale_round (double val)
{
    return val * 255.0 / 65535.0;
}

static void color_ok (GtkWidget *button, GtkWidget *w)
{
    GtkWidget *csel;
    GdkColor color;
    char color_string[12];
    gint i;

    csel = GTK_COLOR_SELECTION_DIALOG(w)->colorsel;
    gtk_color_selection_get_current_color(GTK_COLOR_SELECTION(csel), &color);

    sprintf(color_string, "x%2x%2x%2x",
	    (guint) (scale_round (color.red)),
	    (guint) (scale_round (color.green)),
	    (guint) (scale_round (color.blue)));

    i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "colnum"));

    set_gnuplot_pallette(i, color_string);
  
    gtk_widget_destroy(w);
}

static void color_cancel (GtkWidget *button, GtkWidget *w)
{
    gtk_widget_destroy(w);
}

void gnuplot_color_selector (GtkWidget *w, gpointer p)
{
    GtkWidget *cdlg;
    GtkWidget *button;
    gint i = GPOINTER_TO_INT(p);

    fprintf(stderr, "Doing selection for color %d\n", i);

    cdlg = gtk_color_selection_dialog_new("gretl color selection");

    fprintf(stderr, "cdlg = %p\n", (void *) cdlg);

    g_object_set_data(G_OBJECT(cdlg), "colnum", GINT_TO_POINTER(i));

    button = GTK_COLOR_SELECTION_DIALOG(cdlg)->ok_button;
    g_signal_connect(G_OBJECT(button), "clicked", 
		     G_CALLBACK(color_ok), cdlg);

    button = GTK_COLOR_SELECTION_DIALOG(cdlg)->cancel_button;
    g_signal_connect(G_OBJECT(button), "clicked", 
		     G_CALLBACK(color_cancel), cdlg);
    
    gtk_dialog_run(GTK_DIALOG(cdlg));
}

#ifndef G_OS_WIN32

static int validate_dir (const char *dirname)
{
    DIR *test;
    int err = 0;

    test = opendir(dirname);

    if (test != NULL) {
	fprintf(stderr, "Working dir already exists, OK\n");
	closedir(test);
    } else {
	gchar *sysbuf = g_strdup_printf("mkdir -p %s", dirname);

	/* FIXME: use mkdir() function? */
	
	err = system(sysbuf);
	if (err) {
	    sprintf(errtext, _("Couldn't create directory '%s'"), dirname);
	    errbox(errtext);
	} else {
	    infobox(_("Working directory created OK"));
	}
	g_free(sysbuf);
    }

    return err;
}

static void real_set_userdir (GtkWidget *widget, dialog_t *ddata)
{
    const gchar *dirname;

    dirname = gtk_entry_get_text (GTK_ENTRY (ddata->edit));

    if (validate_dir(dirname)) {
	return;
    } else {
	size_t n = strlen(dirname);

	strcpy(paths.userdir, dirname);
	if (dirname[n - 1] != '/') {
	    strcat(paths.userdir, "/");
	}
	set_tramo_x12a_dirs();
	close_dialog(ddata);
    }
}

void first_time_set_user_dir (void)
{
    DIR *test;

    /* see if the already-specified userdir exists */
    if (*paths.userdir != '\0') {
	test = opendir(paths.userdir);
	if (test != NULL) {
	    closedir(test);
	    return;
	}
    }
	
    /* user dir is not specified, or doesn't exist */
    edit_dialog (_("gretl: working directory"), 
                 _("You seem to be using gretl for the first time.\n"
		   "Please enter a directory for gretl user files."),
                 paths.userdir, 
                 real_set_userdir, NULL, 
                 CREATE_USERDIR, 0);
}

#endif /* G_OS_WIN32 */
