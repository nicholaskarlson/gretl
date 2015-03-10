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

#include "gretl.h"
#include "var.h"
#include "dlgutils.h"
#include "guiprint.h"
#include "session.h"
#include "tabwin.h"
#include "toolbar.h"
#include "cmdstack.h"
#include "winstack.h"

#if GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 20
# include "spinner.h"
#endif

#include "uservar.h"

#define WDEBUG 0

enum {
    WINDOW_NEXT,
    WINDOW_PREV
};

static gint select_other_window (gpointer self, int seq);

/* Below: apparatus for keeping track of open gretl windows.

   This provides the basis for a pop-up listing of windows as
   a means of navigating the multi-window gretl GUI; it also
   gives the basis for checking whether a window performing
   a given role is already open, so as to avoid duplication,
   and for closing any windows that are "invalidated" when
   the gretl session is switched (e.g. by opening a new
   data file).
*/

/* get the top-level widget associated with pre-defined
   @action 
*/

static GtkWidget *window_from_action (GtkAction *action)
{
    GtkWidget *w = NULL;

    if (action != NULL) {
	const gchar *name = gtk_action_get_name(action);

	if (name != NULL) {
#ifdef _WIN64
	    /* note: win64 uses the LLP64 data model, so
	       it's necessary to use a long long to hold a
	       pointer; on other 64-bit systems plain long
	       is 64 bits (LP64).
	    */
	    unsigned long long ull = strtoull(name, NULL, 16);
	    w = (GtkWidget *) ull;
#else
	    unsigned long ul = strtoul(name, NULL, 16);
	    w = (GtkWidget *) ul;
#endif
	}
    }

    return w;
}

static windata_t *vwin_from_action (GtkAction *action)
{
    GtkWidget *w = window_from_action(action);
    
    if (w != NULL) {
	return g_object_get_data(G_OBJECT(w), "vwin");
    }

    return NULL;
}

/* callback to bring a selected window to the top */

static void gretl_window_raise (GtkAction *action, gpointer data)
{
    GtkWidget *w = window_from_action(action);

    if (w != NULL && GTK_IS_WINDOW(w)) {
	gtk_window_present(GTK_WINDOW(w));
    }
}

/* select an icon to represent a window playing
   the given @role in the gretl GUI */

static const gchar *window_list_icon (int role)
{
    const gchar *id = NULL;

    if (role == MAINWIN) {
	id = GRETL_STOCK_GRETL;
    } else if (role == VIEW_MODEL || role == VAR ||
	       role == VECM || role == SYSTEM) {
	id = GRETL_STOCK_MODEL;
    } else if (role == CONSOLE) {
	id = GRETL_STOCK_CONSOLE;
    } else if (role >= EDIT_HEADER && 
	       role < EDIT_MAX) {
	id = GTK_STOCK_EDIT;
    } else if (role == GNUPLOT) {
	id = GRETL_STOCK_SCATTER;
    } else if (BROWSER_ROLE(role)) {
	id = GTK_STOCK_INDEX;
    } else if (help_role(role)) {
	id = GRETL_STOCK_BOOK;
    } else if (role == STAT_TABLE) {
	id = GRETL_STOCK_CALC;
    } else if (role == VIEW_SCRIPT ||
	       role == VIEW_PKG_SAMPLE) {
	id = GTK_STOCK_EXECUTE;
    } else if (role == OPEN_SESSION) {
	id = GRETL_STOCK_ICONS;
    } else if (role == PRINT || 
	       role == SCRIPT_OUT ||
	       role == VIEW_LOG) {
	id = GRETL_STOCK_PAGE;
    } else if (role == SSHEET) {
	id = GRETL_STOCK_TABLE;
    } else if (role == SAVE_FUNCTIONS) {
	id = GRETL_STOCK_TOOLS;
    }

    return id;
}

static int n_listed_windows;
static GtkActionGroup *window_group;

static const gchar *get_window_title (GtkWidget *w)
{
    const gchar *s = NULL;

    if (GTK_IS_WINDOW(w)) {
	s = gtk_window_get_title(GTK_WINDOW(w));

	if (s != NULL && !strncmp(s, "gretl", 5)) {
	    s += 5;
	    s += strspn(s, " ");
	    if (*s == ':') {
		s++;
		s += strspn(s, " ");
	    }
	}
    }

    return s;
}

/* callback to be invoked just before destroying a window that's 
   on the list of open windows: remove its entry from the list
*/

static void window_list_remove (GtkWidget *w, GtkActionGroup *group)
{
    GtkAction *action;
    char aname[32];

    sprintf(aname, "%p", (void *) w);

#if WDEBUG
    fprintf(stderr, "window_list_remove: %s\n", aname);
#endif

    action = gtk_action_group_get_action(group, aname);
    if (action != NULL) {
	gtk_action_group_remove_action(group, action);
	n_listed_windows--;
    }    
}

static char *winname_double_underscores (const gchar *src)
{
    char *s, *targ;
    int i, u = 0;

    for (i=0; src[i]; i++) {
	if (src[i] == '_') {
	    u++;
	}
    }

    targ = malloc(strlen(src) + u + 1);
    s = targ;

    for (i=0; src[i]; i++) {
	if (src[i] == '_' && src[i+1] != '_') {
	    *s++ = '_';
	    *s++ = '_';
	} else {
	    *s++ = src[i];
	}
    }

    *s = '\0';

    return targ;
}

/* callback for command-accent on Mac or Alt-PgUp/PgDn on
   X11 and Windows: switch window-focus within gretl
*/

static gint maybe_select_other_window (GdkEventKey *event,
				       gpointer data)
{
#ifdef MAC_NATIVE
    if (cmd_key(event)) {
	if (event->keyval == GDK_asciitilde) {
	    return select_other_window(data, WINDOW_PREV);
	} else if (event->keyval == GDK_grave) {
	    return select_other_window(data, WINDOW_NEXT);
	}
    }
#else
    if (event->state & GDK_MOD1_MASK) {
	if (event->keyval == GDK_Page_Up || 
	    event->keyval == GDK_KP_Page_Up) {
	    return select_other_window(data, WINDOW_PREV);
	} else if (event->keyval == GDK_Page_Down ||
		   event->keyval == GDK_KP_Page_Down) {
	    return select_other_window(data, WINDOW_NEXT);
	}
    }
#endif	

    return FALSE;
}

static gint catch_winlist_key (GtkWidget *w, GdkEventKey *event, 
			       gpointer data)
{
#ifdef MAC_NATIVE
    if ((event->state & GDK_MOD1_MASK) && event->keyval == alt_w_key) {
	/* alt-w -> Sigma */
	window_list_popup(w, (GdkEvent *) event, data);
	return TRUE;
    }
# ifdef GRETL_MACINT
    if (cmd_key(event) && mac_hide_unhide(event)) {
	return TRUE;
    }
# endif  
#else /* non-Mac */
    if (event->state & GDK_MOD1_MASK) {
	if (event->keyval == GDK_w) {
	    window_list_popup(w, (GdkEvent *) event, data);
	    return TRUE;
	}
    }
#endif

    return maybe_select_other_window(event, data);
}

void window_list_add (GtkWidget *w, int role)
{
    GtkActionEntry entry = { 
	/* name, stock_id, label, accelerator, tooltip, callback */
	NULL, NULL, NULL, NULL, NULL, G_CALLBACK(gretl_window_raise) 
    };
    const char *label;
    char *modlabel = NULL;
    char aname[32];

    if (window_group == NULL) {
	/* create the window list action group */
	window_group = gtk_action_group_new("WindowList");
    }

    /* set up an action entry for window @w */

    sprintf(aname, "%p", (void *) w);
    entry.name = aname;
    entry.stock_id = window_list_icon(role);

#if WDEBUG
    fprintf(stderr, "window_list_add: %s\n", aname);
#endif

    if (role == MAINWIN) {
	label = _("Main window");
    } else {
	label = get_window_title(w);
	if (label != NULL && strchr(label, '_') != NULL) {
	    modlabel = winname_double_underscores(label);
	}
    }

    if (label == NULL) {
	return;
    }

    entry.label = modlabel != NULL ? modlabel : label,

    /* add new action entry to group */
    gtk_action_group_add_actions(window_group, &entry, 1, NULL);

    if (role != MAINWIN) {
	/* attach time to window */
	g_object_set_data(G_OBJECT(w), "time", GUINT_TO_POINTER(time(NULL)));
	/* attach callback to remove from window list */
	g_signal_connect(G_OBJECT(w), "destroy", 
			 G_CALLBACK(window_list_remove), 
			 window_group);
    }

    g_signal_connect(G_OBJECT(w), "key-press-event", 
		     G_CALLBACK(catch_winlist_key), w);

    n_listed_windows++;

    free(modlabel);
}

/* GCompareFunc: returns "a negative integer if the first value comes
   before the second, 0 if they are equal, or a positive integer if
   the first value comes after the second." 
*/

static gint sort_window_list (gconstpointer a, gconstpointer b)
{
    GtkWidget *wa = window_from_action((GtkAction *) a);
    GtkWidget *wb = window_from_action((GtkAction *) b);
    guint ta, tb;

    /* sort main window first, otherwise by time when the 
       window was created */

    if (wa == mdata->main) return -1;
    if (wb == mdata->main) return 1;

    /* bullet-proofing */
    if (wa == NULL || wb == NULL) {
	return 0;
    }

    ta = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wa), "time"));
    tb = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(wb), "time"));

    return ta - tb;
}

/* use real UTF-8 bullet character if possible, otherwise asterisk */

static void make_bullet (char *bullet)
{
    GtkSettings *settings = gtk_settings_get_default();
    gchar *fontname = NULL;

    g_object_get(G_OBJECT(settings), "gtk-font-name", &fontname, NULL);

    if (fontname != NULL) {
	PangoFontDescription *desc;

	desc = pango_font_description_from_string(fontname);
	if (font_has_symbol(desc, 0x2022)) {
	    sprintf(bullet, " %c%c%c", 0xE2, 0x80, 0xA2);
	} 
	if (desc != NULL) {
	    pango_font_description_free(desc);
	}
    }

    if (*bullet == '\0') {
	strcpy(bullet, " *");
    }
}

#if GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION < 16

static const gchar *gtk_action_get_label (GtkAction *action)
{
    static char aname[32];
    gchar *tmp = NULL;

    g_object_get(action, "label", &tmp, NULL);

    *aname = '\0';
    if (tmp != NULL) {
	strncat(aname, tmp, 31);
	g_free(tmp);
    }

    return aname;
}

static void gtk_action_set_label (GtkAction *action,
				  const gchar *label)
{
    g_object_set(action, "label", label, NULL);
}

#endif /* remedial functions for older GTK */

/* show a bullet or asterisk next to the entry for 
   the current window */

static void maybe_revise_action_label (GtkAction *action,
				       GtkWidget *test)
{
    static char bullet[5];
    static int blen;
    const gchar *label = gtk_action_get_label(action);
    gchar *repl = NULL;
    int n = strlen(label);
    int marked = 0;

    if (*bullet == '\0') {
	make_bullet(bullet);
	blen = strlen(bullet);
    }

    if (n > blen && !strcmp(label + n - blen, bullet)) {
	marked = 1;
    }

    if (test == window_from_action(action)) {
	if (!marked) {
	    /* add asterisk */
	    repl = g_strdup_printf("%s %s", label, bullet);
	}
    } else if (marked) {
	/* remove asterisk */
	repl = g_strndup(label, strlen(label) - blen);
    }
    
    if (repl != NULL) {
	gtk_action_set_label(action, repl);
	g_free(repl);
    }	
}

static gboolean winlist_popup_done (GtkMenuShell *mshell,
				    GtkWidget *window)
{
    windata_t *vwin = window_get_active_vwin(window);

    if (vwin != NULL) {
	/* don't leave focus on the winlist button */
	if (vwin->role == VIEW_MODEL || 
	    vwin->role == VAR || 
	    vwin->role == VECM) {
	    gtk_widget_grab_focus(vwin->text);
	} else if (vwin == mdata) {
	    gtk_widget_grab_focus(vwin->listbox);
	}
    }

    return FALSE;
}

static void add_cascade_item (GtkWidget *menu,
			      GtkWidget *item)
{
    GtkWidget *image;

    item = gtk_image_menu_item_new_with_label(_("Arrange"));
    image = gtk_image_new_from_stock(GRETL_STOCK_WINLIST, 
				     GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), 
				  image);
    g_signal_connect(G_OBJECT(item), "activate", 
		     G_CALLBACK(cascade_session_windows), 
		     NULL);
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void add_log_item (GtkWidget *menu, 
			  GtkWidget *item)
{
    GtkWidget *image;

    item = gtk_image_menu_item_new_with_label(_("command log"));
    image = gtk_image_new_from_stock(GRETL_STOCK_PAGE, 
				     GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), 
				  image);
    g_signal_connect(G_OBJECT(item), "activate", 
		     G_CALLBACK(view_command_log), 
		     NULL);
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

static void add_iconview_item (GtkWidget *menu,
			       GtkWidget *item)
{
    GtkWidget *image;

    item = gtk_image_menu_item_new_with_label(_("icon view"));
    image = gtk_image_new_from_stock(GRETL_STOCK_ICONS, 
				     GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), 
				  image);
    g_signal_connect(G_OBJECT(item), "activate", 
		     G_CALLBACK(view_session), 
		     NULL);
    gtk_widget_show(item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

/* pop up a list of open windows from which the user can
   select one to raise and focus */

void window_list_popup (GtkWidget *src, GdkEvent *event, 
			gpointer data)
{
    static GtkWidget *menu;
    GdkEventType evtype;
    GList *wlist = gtk_action_group_list_actions(window_group);
    GList *list;
    GtkWidget *item, *lwin;
    GtkWidget *thiswin = NULL;
    GtkAction *action;
    int log_up = 0;
    int icons_up = 0;

    if (menu != NULL) {
	/* we need to make sure this is up to date */
	gtk_widget_destroy(menu);
	menu = NULL;
    }

    if (n_listed_windows > 1) {
	wlist = g_list_sort(wlist, sort_window_list);
    }

    if (data != NULL) {
	thiswin = GTK_WIDGET(data);
    }    

    menu = gtk_menu_new();
    list = wlist;

    while (list) {
	action = (GtkAction *) list->data;
	lwin = window_from_action(action);
	if (is_command_log_viewer(lwin)) {
	    log_up = 1;
	} else if (widget_is_iconview(lwin)) {
	    icons_up = 1;
	}
	if (n_listed_windows > 1 && thiswin != NULL) {
	    maybe_revise_action_label(action, thiswin);
	}
	gtk_action_set_accel_path(action, NULL);
	item = gtk_action_create_menu_item(action);
	gtk_widget_show(item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	list = list->next;
    }

    g_list_free(wlist);

    if (n_listed_windows > 1) {
	add_cascade_item(menu, item);
    }

    if (!log_up || !icons_up) {
	item = gtk_separator_menu_item_new();
	gtk_widget_show(item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	if (!log_up) {
	    add_log_item(menu, item);
	}
	if (!icons_up) {
	    add_iconview_item(menu, item);
	}
    }

    if (thiswin != NULL) {
	g_signal_connect(G_OBJECT(menu), "deactivate", 
			 G_CALLBACK(winlist_popup_done),
			 thiswin);
    }

    evtype = event != NULL ? event->type : 0;

    if (evtype == GDK_BUTTON_PRESS) {
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		       event->button.button, event->button.time);
    } else if (evtype == GDK_KEY_PRESS) {
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		       0, event->key.time);
    } else {
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		       0, gtk_get_current_event_time());
    }
}

void vwin_winlist_popup (GtkWidget *src, GdkEvent *event, 
			 windata_t *vwin)
{
    /* Note: this function may look redundant, given the
       window_list_popup() function, but it's not. This is
       due to the presence of tabbed windows whose content
       (scripts, models) can be dragged out of their
       tabbed context: in that case @vwin's associated
       "toplevel" can change between invocations of
       this function and therefore cannot be "hard-wired"
       into the callback setup, rather it must be
       evaluated on each invocation.
    */
    window_list_popup(src, event, vwin_toplevel(vwin));
}

/* on exiting, check for any editing windows with unsaved
   changes, and if we find any give the user a chance to
   save the changes, or to cancel the exit
*/

gboolean window_list_exit_check (void)
{
    gboolean ret = FALSE;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;
	GtkWidget *w;

	while (list) {
	    w = window_from_action((GtkAction *) list->data);
	    if (w != NULL) {
		vwin = g_object_get_data(G_OBJECT(w), "vwin");
		if (vwin != NULL && vwin_is_editing(vwin)) {
		    if (vwin_content_changed(vwin)) {
			gtk_window_present(GTK_WINDOW(vwin->main));
			ret = query_save_text(NULL, NULL, vwin);
		    }
		}
		if (vwin == NULL && g_object_get_data(G_OBJECT(w), "tabwin")) {
		    ret = tabwin_exit_check(w);
		}
	    }
	    list = list->next;
	}

	g_list_free(list);
    }

    return ret;
}

/* windows that should _not_ be automatically closed when
   closing the current gretl session (e.g. on opening a
   new data file)
*/

#define other_dont_close(r) (r == SCRIPT_OUT ||		\
			     r == EDIT_PKG_CODE  ||	\
			     r == EDIT_PKG_SAMPLE ||	\
			     r == VIEW_LOG ||		\
			     r == VIEW_SCRIPT ||	\
			     r == VIEW_PKG_SAMPLE ||	\
	                     r == TEXTBOOK_DATA ||	\
			     r == PS_FILES ||		\
			     r == NATIVE_DB ||		\
			     r == REMOTE_DB ||		\
			     r == FUNC_FILES ||		\
			     r == REMOTE_FUNC_FILES ||	\
			     r == CONSOLE)


static int keep_window_open (GtkWidget *w, gretlopt opt)
{
    const gchar *wname = gtk_widget_get_name(w);

    /* FIXME maybe keep plot windows open if opt & OPT_P? */

    if (wname != NULL && strcmp(wname, "pkg-editor") == 0) {
	return 1;
    } else {
	return 0;
    }
}

/* called from session.c on switching the session: close all
   windows that ought to be closed, but be careful not to
   close ones that need to stay open!
*/

void close_session_windows (gretlopt opt)
{
    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;
	GtkWidget *w;

	while (list) {
	    w = window_from_action((GtkAction *) list->data);
	    if (w != NULL) {
		vwin = g_object_get_data(G_OBJECT(w), "vwin");
		if (vwin == mdata) {
		    ; /* main window: no-op! */
		} else if (vwin != NULL && (vwin_editing_script(vwin->role) ||
					    help_role(vwin->role) ||
					    other_dont_close(vwin->role))) {
		    ; /* no-op */
		} else if (vwin == NULL && g_object_get_data(G_OBJECT(w), "tabwin")) {
		    /* tabbed script editor stays open, but tabbed model
		       viewer should be closed */
		    tabwin_close_models_viewer(w);
		} else if (w != NULL && !keep_window_open(w, opt)) {
		    gtk_widget_destroy(w);
		}
	    }
	    list = list->next;
	}

	g_list_free(list);
    }
}

void cascade_session_windows (void)
{
    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	GList *slist = g_list_sort(list, sort_window_list);
	GtkWidget *w;
	gint x = 50, y = 50;
	gint d = 30;

	while (slist) {
	    w = window_from_action((GtkAction *) slist->data);
	    if (w != NULL) {
		gtk_window_move(GTK_WINDOW(w), x, y);
		gtk_window_present(GTK_WINDOW(w));
		x += d;
		y += d;
	    }
	    slist = slist->next;
	}
	g_list_free(list);
    }
}

static gint select_other_window (gpointer self, int seq)
{
    if (n_listed_windows > 1) {
	GList *wlist = gtk_action_group_list_actions(window_group);
	GList *mylist;
	GtkWidget *w;

	wlist = g_list_sort(wlist, sort_window_list);

	/* find the window from which the keystroke emanated, @self,
	   and then select the next or previous window in the list,
	   wrapping around at the ends */

	mylist = g_list_first(wlist);
	while (mylist) {
	    w = window_from_action((GtkAction *) mylist->data);
	    if (w == (GtkWidget *) self) {
		if (seq == WINDOW_PREV) {
		    mylist = mylist->prev != NULL ? mylist->prev : 
			g_list_last(wlist);
		} else {
		    mylist = mylist->next != NULL ? mylist->next : 
			g_list_first(wlist);
		}
		gretl_window_raise((GtkAction *) mylist->data, NULL);
		break;
	    }
	    mylist = mylist->next;
	}
	g_list_free(wlist);
	return TRUE;
    }

    return FALSE;
}

windata_t *get_editor_for_file (const char *filename)
{
    windata_t *ret = NULL;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;
	GtkWidget *w;

	while (list != NULL && ret == NULL) {
	    w = window_from_action((GtkAction *) list->data);
	    if (w != NULL) {
		vwin = g_object_get_data(G_OBJECT(w), "vwin");
		if (vwin != NULL && vwin_is_editing(vwin)) {
		    if (!strcmp(filename, vwin->fname)) {
			ret = vwin;
		    }
		}
		if (vwin == NULL && g_object_get_data(G_OBJECT(w), "tabwin")) {
		    ret = tabwin_get_editor_for_file(filename, w);
		}
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

static int db_role_matches (windata_t *vwin, int code)
{
    int ret = 0;

    if (code == NATIVE_SERIES) {
	ret = vwin->role == code;
    } else {
	ret = (vwin->role == NATIVE_SERIES ||
	       vwin->role == RATS_SERIES ||
	       vwin->role == PCGIVE_SERIES ||
	       vwin->role == REMOTE_SERIES);
    }

    if (ret) {
	ret = *vwin->fname != '\0';
    }

    return ret;
}

static windata_t *
real_get_browser_for_database (const char *filename, int code)
{
    windata_t *ret = NULL;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;

	while (list != NULL && ret == NULL) {
	    vwin = vwin_from_action((GtkAction *) list->data);
	    if (vwin != NULL && db_role_matches(vwin, code)) {
		if (!strncmp(filename, vwin->fname, 
			     strlen(vwin->fname))) {
		    ret = vwin;
		}
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

windata_t *get_browser_for_database (const char *filename)
{
    return real_get_browser_for_database(filename, 0);
}

windata_t *get_browser_for_gretl_database (const char *filename)
{
    return real_get_browser_for_database(filename, NATIVE_SERIES);
}

windata_t *get_viewer_for_data (const gpointer data)
{
    windata_t *ret = NULL;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;

	while (list != NULL && ret == NULL) {
	    vwin = vwin_from_action((GtkAction *) list->data);
	    if (vwin != NULL && vwin->data == data) {
		ret = vwin;
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

windata_t *get_browser_for_role (int role)
{
    windata_t *ret = NULL;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;

	while (list != NULL && ret == NULL) {
	    vwin = vwin_from_action((GtkAction *) list->data);
	    if (vwin != NULL && vwin->role == role) {
		ret = vwin;
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

int get_script_output_number (void)
{
    int ret = 0;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;

	while (list != NULL) {
	    vwin = vwin_from_action((GtkAction *) list->data);
	    if (vwin != NULL && vwin->role == SCRIPT_OUT) {
		ret++;
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

windata_t *get_unique_output_viewer (void)
{
    windata_t *ret = NULL;
    int vcount = 0;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	windata_t *vwin;

	while (list != NULL) {
	    vwin = vwin_from_action((GtkAction *) list->data);
	    if (vwin != NULL && vwin->role == SCRIPT_OUT) {
		vcount++;
		if (vcount == 1) {
		    ret = vwin;
		} else {
		    ret = NULL;
		    break;
		}
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

GtkWidget *get_window_for_data (const gpointer data)
{
    GtkWidget *ret = NULL;

    /* this handles the case where the window in question
       is not part of a windata_t "viewer": e.g. a
       spreadsheet window editing a matrix
    */

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	GtkWidget *w;
	gpointer p;

	while (list != NULL && ret == NULL) {
	    w = window_from_action((GtkAction *) list->data);
	    if (w != NULL) {
		p = g_object_get_data(G_OBJECT(w), "object");
		if (p == data) {
		    ret = w;
		}
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

void maybe_close_window_for_user_var (const gpointer data,
				      GretlObjType otype)
{
    if (otype == GRETL_OBJ_BUNDLE) {
	void *ptr = user_var_get_value((user_var *) data);
	windata_t *vwin = get_viewer_for_data(ptr);

	if (vwin != NULL) {
	    vwin->data = NULL; /* don't double-free */
	    gtk_widget_destroy(vwin->main);
	}
    } else {
	GtkWidget *w = get_window_for_data(data);

	if (w != NULL) {
	    if (otype == GRETL_OBJ_MATRIX) {
		/* don't double-free */
		g_object_set_data(G_OBJECT(w), "object", NULL);
	    }
	    gtk_widget_destroy(w);
	}
    }
}

GtkWidget *get_window_for_plot (const char *plotfile)
{
    GtkWidget *ret = NULL;

    /* special for plot windows */

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	GtkWidget *w;
	gchar *test;

	while (list != NULL && ret == NULL) {
	    w = window_from_action((GtkAction *) list->data);
	    if (w != NULL) {
		test = g_object_get_data(G_OBJECT(w), "plot-filename");
		if (test != NULL && strstr(test, plotfile) != NULL) {
		    ret = w;
		}
	    }	    
	    list = list->next;
	}
	g_list_free(list);
    }

    return ret;
}

int highest_numbered_variable_in_winstack (void)
{
    int m_vmax, vmax = 0;

    if (n_listed_windows > 1) {
	GList *list = gtk_action_group_list_actions(window_group);
	tabwin_t *tabwin;
	windata_t *vwin;
	GtkWidget *w;

	while (list != NULL) {
	    vwin = NULL;
	    w = window_from_action((GtkAction *) list->data);
	    if (w != NULL) {
		tabwin = g_object_get_data(G_OBJECT(w), "tabwin");
		if (tabwin == NULL) {
		    vwin = g_object_get_data(G_OBJECT(w), "vwin");
		}
		if (tabwin != NULL) {
		    m_vmax = highest_numbered_var_in_tabwin(tabwin, dataset);
		    if (m_vmax > vmax) {
			vmax = m_vmax;
		    }
		} else if (vwin != NULL && vwin->role == VIEW_MODEL) {
		    const MODEL *pmod = vwin->data;

		    m_vmax = highest_numbered_var_in_model(pmod, dataset);
		    if (m_vmax > vmax) {
			vmax = m_vmax;
		    }
		} else if (vwin != NULL && (vwin->role == VAR || vwin->role == VECM)) {
		    const GRETL_VAR *var = vwin->data;

		    m_vmax = gretl_VAR_get_highest_variable(var);
		    if (m_vmax > vmax) {
			vmax = m_vmax;
		    }		    
		}
	    }
	    list = list->next;
	}
	g_list_free(list);
    }

    return vmax;
}

/* end of window-list apparatus */

windata_t *vwin_new (int role, gpointer data)
{
    windata_t *vwin = mymalloc(sizeof *vwin);

    if (vwin != NULL) {
	memset(vwin, 0, sizeof *vwin);
	vwin->role = role;
	vwin->data = data;
    }

    return vwin;
}

windata_t *
gretl_viewer_new_with_parent (windata_t *parent, int role, 
			      const gchar *title, 
			      gpointer data)
{
    windata_t *vwin = vwin_new(role, data);

    if (vwin == NULL) {
	return NULL;
    }

    vwin->main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(vwin->main), title);
    g_object_set_data(G_OBJECT(vwin->main), "vwin", vwin);

    vwin->vbox = gtk_vbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vwin->vbox), 4);
    gtk_container_add(GTK_CONTAINER(vwin->main), vwin->vbox);

    if (parent != NULL) {
	vwin_add_child(parent, vwin);
    }

    if (role != MAINWIN) {
	g_signal_connect(G_OBJECT(vwin->main), "destroy", 
			 G_CALLBACK(free_windata), vwin);
    }

    if (title != NULL) {
	window_list_add(vwin->main, role);
#ifndef G_OS_WIN32
	set_wm_icon(vwin->main);
#endif
    }

    return vwin;
}

windata_t *gretl_viewer_new (int role, const gchar *title, 
			     gpointer data)
{
    return gretl_viewer_new_with_parent(NULL, role, title,
					data);
}

GtkWidget *vwin_toplevel (windata_t *vwin)
{
    if (vwin == NULL) {
	return NULL;
    } else {
	return vwin->topmain != NULL ? vwin->topmain : vwin->main;
    }
}

static GtkWidget *real_add_winlist (windata_t *vwin,
				    GtkWidget *window,
				    GtkWidget *hbox)
{
    GtkWidget *button, *img, *tbar;
    GtkToolItem *item;

    button = gtk_button_new();
    item = gtk_tool_item_new();
    tbar = gretl_toolbar_new();
    
    gtk_widget_set_tooltip_text(GTK_WIDGET(item), _("Windows"));
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    img = gtk_image_new_from_stock(GRETL_STOCK_WINLIST, GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(button), img);
    gtk_container_add(GTK_CONTAINER(item), button);

    if (vwin != NULL) {
	g_signal_connect(G_OBJECT(button), "button-press-event", 
			 G_CALLBACK(vwin_winlist_popup), vwin);
    } else {
	g_signal_connect(G_OBJECT(button), "button-press-event", 
			 G_CALLBACK(window_list_popup), window);
    }
    gtk_toolbar_insert(GTK_TOOLBAR(tbar), item, -1);
    gtk_widget_show_all(tbar);
    gtk_box_pack_end(GTK_BOX(hbox), tbar, FALSE, FALSE, 0);

    return tbar;
}

void vwin_add_winlist (windata_t *vwin)
{
    GtkWidget *hbox = gtk_widget_get_parent(vwin->mbar);

    if (g_object_get_data(G_OBJECT(hbox), "winlist") == NULL) {
	GtkWidget *winlist;
	
	winlist = real_add_winlist(vwin, NULL, hbox);
	g_object_set_data(G_OBJECT(hbox), "winlist", winlist);
    }
}

void window_add_winlist (GtkWidget *window, GtkWidget *hbox)
{
    if (g_object_get_data(G_OBJECT(hbox), "winlist") == NULL) {
	GtkWidget *winlist;

	winlist = real_add_winlist(NULL, window, hbox);
	g_object_set_data(G_OBJECT(hbox), "winlist", winlist);
    }    
}

static void destroy_hbox_child (GtkWidget *w, gpointer p)
{
    if (GTK_IS_SPINNER(w)) {
	gtk_spinner_stop(GTK_SPINNER(w));
    }
    gtk_widget_destroy(w);
}

static int want_winlist (windata_t *vwin)
{
    GtkWidget *hbox = gtk_widget_get_parent(vwin->mbar);

    return g_object_get_data(G_OBJECT(hbox), "winlist") == NULL;
}

void vwin_pack_toolbar (windata_t *vwin)
{
    if (vwin->topmain != NULL) {
	/* @vwin is embedded in a tabbed window */
	tabwin_register_toolbar(vwin);
    } else {
	GtkWidget *hbox;

	/* check for presence of a temporary "top-hbox" -- as
	   in a script output window that's waiting for full
	   output
	*/
	hbox = g_object_get_data(G_OBJECT(vwin->main), "top-hbox");

	if (hbox != NULL) {
	    gtk_container_foreach(GTK_CONTAINER(hbox), destroy_hbox_child, NULL);
	    gtk_box_pack_start(GTK_BOX(hbox), vwin->mbar, FALSE, FALSE, 0);
	    gtk_widget_show_all(hbox);
	} else {
	    hbox = gtk_hbox_new(FALSE, 0);
	    gtk_box_pack_start(GTK_BOX(vwin->vbox), hbox, FALSE, FALSE, 0);

	    if (vwin->role == VIEW_MODEL || vwin->role == VAR ||
		vwin->role == VECM) {
		/* model viewer: the menubar extends full-length */
		gtk_box_pack_start(GTK_BOX(hbox), vwin->mbar, TRUE, TRUE, 0);
	    } else {
		gtk_box_pack_start(GTK_BOX(hbox), vwin->mbar, FALSE, FALSE, 0);
	    }
	    if (window_is_tab(vwin)) {
		/* here we're re-packing vwin->mbar: move it up top */
		gtk_box_reorder_child(GTK_BOX(vwin->vbox), hbox, 0);
	    }
	    gtk_widget_show_all(hbox);
	}
    }

    if (want_winlist(vwin)) {
	vwin_add_winlist(vwin);
    }
}

void vwin_reinstate_toolbar (windata_t *vwin)
{
    GtkWidget *hbox;

    hbox = g_object_get_data(G_OBJECT(vwin->main), "top-hbox");

    if (hbox != NULL) {
	/* destroy the temporary stuff, put the "real" stuff back,
	   and drop the extra references 
	*/
	GtkWidget *winlist;
	
	gtk_container_foreach(GTK_CONTAINER(hbox), destroy_hbox_child, NULL);
	gtk_box_pack_start(GTK_BOX(hbox), vwin->mbar, FALSE, FALSE, 0);
	g_object_unref(G_OBJECT(vwin->mbar));
	winlist = g_object_get_data(G_OBJECT(hbox), "winlist");
	if (winlist != NULL) {
	    gtk_box_pack_end(GTK_BOX(hbox), winlist, FALSE, FALSE, 0);
	    g_object_unref(G_OBJECT(winlist));
	}
	gtk_widget_show_all(hbox);
    }
}

windata_t *gretl_browser_new (int role, const gchar *title)
{
    windata_t *vwin = vwin_new(role, NULL);

    if (vwin == NULL) {
	return NULL;
    }

    vwin->main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(vwin->main), title);
    g_object_set_data(G_OBJECT(vwin->main), "vwin", vwin);

    g_signal_connect(G_OBJECT(vwin->main), "destroy",
		     G_CALLBACK(free_windata), vwin);

    window_list_add(vwin->main, role);
#ifndef G_OS_WIN32
    set_wm_icon(vwin->main);
#endif

    return vwin;
}

void gretl_viewer_present (windata_t *vwin)
{
    if (window_is_tab(vwin)) {
	tabwin_tab_present(vwin);
    } else {
	gtk_window_present(GTK_WINDOW(vwin->main));
    }
}

void gretl_viewer_destroy (windata_t *vwin)
{
    if (window_is_tab(vwin)) {
	tabwin_tab_destroy(vwin);
    } else {
	gtk_widget_destroy(vwin->main);
    }
}

void gretl_viewer_set_title (windata_t *vwin, const char *title)
{
    if (window_is_tab(vwin)) {
	if (!strncmp(title, "gretl: ", 7)) {
	    title += 7;
	}
	tabwin_tab_set_title(vwin, title);
    } else {
	gtk_window_set_title(GTK_WINDOW(vwin->main), title);
    }
}

/* When we add popup menus as callbacks for buttons on @vwin's
   toolbar, we want to record pointers to them so we're
   able to destroy them when @vwin is closed, otherwise
   we'd be leaking memory.
*/

void vwin_record_toolbar_popup (windata_t *vwin, GtkWidget *menu)
{
    GList *plist;

    plist = g_object_get_data(G_OBJECT(vwin->mbar), "toolbar-popups");
    plist = g_list_append(plist, menu);
    g_object_set_data(G_OBJECT(vwin->mbar), "toolbar-popups", plist);
}

static void trash_toolbar_popup (gpointer data, gpointer p)
{
    gtk_widget_destroy(GTK_WIDGET(data));
}

void vwin_free_toolbar_popups (windata_t *vwin)
{
    if (vwin->mbar != NULL) {
	GList *plist;

	plist = g_object_get_data(G_OBJECT(vwin->mbar), "toolbar-popups");
	if (plist != NULL) {
	    g_list_foreach(plist, trash_toolbar_popup, NULL);
	}
	g_list_free(plist);
    }
}
