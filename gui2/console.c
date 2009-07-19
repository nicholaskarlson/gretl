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

/* console.c for gretl */

#include "gretl.h"
#include "console.h"
#include "menustate.h"
#include "dlgutils.h"

#ifdef G_OS_WIN32
# include "gretlwin32.h"
#endif

#include "libset.h"
#include "monte_carlo.h"
#include "gretl_func.h"
#include "cmd_private.h"

#define DEFAULT_HLINES 32

static gchar *cbuf; /* the console command line */
static char **cmd_history;
static int hl, hlmax, hlines;

static gint console_key_handler (GtkWidget *cview, GdkEventKey *key, 
				 gpointer p);
static gint console_mouse_handler (GtkWidget *w, GdkEventButton *event,
				   gpointer p);
static void console_paste_handler (GtkWidget *w, gpointer p);
static gint console_click_handler (GtkWidget *w, GdkEventButton *event,
				   gpointer p);

#define CDEBUG 0 /* not ready! */

#if CDEBUG
static int console_get_line (void *);
#endif

static ExecState *gretl_console_init (void)
{
    ExecState *s;
    char *hstr;
    int i;

    cbuf = NULL;

    s = mymalloc(sizeof *s);
    if (s == NULL) {
	return NULL;
    }    

    hlines = 0;

    hstr = getenv("GRETL_HISTORY_LINES");
    if (hstr != NULL) {
	hlines = atoi(hstr);
    }

    if (hlines <= 2 || hlines > 128) {
	hlines = DEFAULT_HLINES;
    }

    cmd_history = mymalloc(hlines * sizeof *cmd_history);
    if (cmd_history == NULL) {
	free(s);
	return NULL;
    }

    for (i=0; i<hlines; i++) {
	cmd_history[i] = NULL;
    }

    hlmax = 0;
    hl = -1;

    set_gretl_echo(1);
#if CDEBUG
    set_debug_read_func(console_get_line);
#endif

    gretl_exec_state_init(s, CONSOLE_EXEC, NULL, 
			  get_lib_cmd(), models, NULL);

    return s;
}

static void gretl_console_free (GtkWidget *cview, windata_t *vwin)
{
    ExecState *s;
    int i;

    if (cmd_history != NULL) {
	for (i=0; i<hlines; i++) {
	    free(cmd_history[i]);
	}
	free(cmd_history);
	cmd_history = NULL;
    }

    hlines = hlmax = 0;
    hl = -1;

    g_free(cbuf);
    cbuf = NULL;

    s = g_object_get_data(G_OBJECT(cview), "ExecState");

    if (s != NULL) {
	if (s->prn != NULL) {
	    infobox(_("Closing redirected output file"));
	    gretl_print_destroy(s->prn);
	}
	free(s);
    }
}

static int push_history_line (const char *line)
{
    int i;

    /* drop last entry */
    free(cmd_history[hlines-1]);

    /* push the other lines down one place */
    for (i=hlines-1; i>0; i--) {
	cmd_history[i] = cmd_history[i-1];
    }

    /* add the new line */
    cmd_history[0] = g_strdup(line);
    
    if (hlmax < hlines) {
	hlmax++;
    }

    hl = -1;

    return 0;
}

static void beep (void)
{
#ifdef G_OS_WIN32
    MessageBeep(MB_ICONEXCLAMATION);
#else
    putchar('\a');
    fflush(stdout);
#endif
}

static char *pop_history_line (int keyval)
{
    static int beeptime;
    char *ret = NULL;

    if (keyval == GDK_Up) {
	if (hl < hlmax) hl++;
	if (hl == hlmax) {
	    hl--;
	    beep();
	} else {
	    ret = cmd_history[hl];
	}
    } else if (keyval == GDK_Down) {
	if (hl >= 0) hl--;
	if (hl < 0) {
	    if (beeptime) {
		beep();
	    }
	    beeptime = 1;
	} else {
	    beeptime = 0;
	    ret = cmd_history[hl];
	}
    }

    return ret;
}

static void console_scroll_to_end (GtkWidget *cview,
				   GtkTextBuffer *buf, 
				   GtkTextIter *start)
{
    GtkTextMark *mark;

    gtk_text_buffer_place_cursor(buf, start);
    mark = gtk_text_buffer_create_mark(buf, NULL, start, FALSE);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(cview), mark);
}

enum {
    SAMPLE_RECORD,
    SAMPLE_CHECK
};

static int console_sample_handler (const DATAINFO *pdinfo, int code)
{
    static int pd, t1, t2, ts;
    static double sd0;
    int ret = 0;

    if (code == SAMPLE_RECORD) {
	pd = pdinfo->pd;
	t1 = pdinfo->t1;
	t2 = pdinfo->t2;
	ts = pdinfo->structure;
	sd0 = pdinfo->sd0;
    } else if (code == SAMPLE_CHECK && pdinfo->v > 0) {
	if (pdinfo->pd != pd ||
	    pdinfo->t1 != t1 ||
	    pdinfo->t2 != t2 ||
	    pdinfo->structure != ts ||
	    pdinfo->sd0 != sd0) {
	    ret = 1;
	}
    }

    return ret;
}

void console_record_sample (const DATAINFO *pdinfo)
{
    console_sample_handler(pdinfo, SAMPLE_RECORD);
}

int console_sample_changed (const DATAINFO *pdinfo)
{
    return console_sample_handler(pdinfo, SAMPLE_CHECK);
}

static void print_result_to_console (GtkTextBuffer *buf,
				     GtkTextIter *start,
				     ExecState *s)
{
    const char *getbuf = gretl_print_get_buffer(s->prn);

    if (!g_utf8_validate(getbuf, -1, NULL)) {
	gchar *trbuf = my_locale_to_utf8(getbuf);

	fprintf(stderr, "console text did not validate as utf8\n");
	if (trbuf != NULL) {
	    gtk_text_buffer_insert(buf, start, trbuf, -1);
	    g_free(trbuf);
	}
    } else {
	gtk_text_buffer_insert(buf, start, getbuf, -1);
    }
}

static ExecState *dstate; /* for debugging */
static int enter_pressed;

#if CDEBUG

static int console_get_line (void *p)
{
    dstate = (ExecState *) p;

    fprintf(stderr, "console_get_line called\n"); 

    while (!enter_pressed) {
	fprintf(stderr, "waiting for enter...\n");
	sleep(1);
	if (gtk_events_pending()) {
	    gtk_main_iteration();
	}
    }

    fprintf(stderr, "got enter_pressed\n"); 	    

    while (enter_pressed) {
	fprintf(stderr, "waiting for !enter...\n");
	sleep(1);
	if (0 && gtk_events_pending()) {
	    gtk_main_iteration();
	}
    }   

    fprintf(stderr, "got !enter_pressed\n"); 

    dstate = NULL;

    return 0;
}

#endif

/* callback from Enter key in gretl console */

static void console_exec (GtkWidget *cview)
{
    ExecState *state;
    GtkTextBuffer *buf;
    GtkTextIter start, end;
    char execline[MAXLINE];
    int debugging = 0;
    int coding = 0;
    int err = 0;

    if (dstate == NULL) {
	state = g_object_get_data(G_OBJECT(cview), "ExecState");
    } else {
	fprintf(stderr, "enter_pressed\n");
	enter_pressed = 1;
	debugging = 1;
	state = dstate;
    }

    /* get into printing position */
    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cview));
    gtk_text_buffer_get_end_iter(buf, &end);
    start = end;
    gtk_text_iter_set_line_offset(&start, 2);

    /* the global variable 'cbuf' has been filled out, 
       triggered by Enter */
    top_n_tail(cbuf, 0, NULL);

    if (debugging) {
	*state->line = '\0';
	strncat(state->line, cbuf, MAXLINE - 2);
	strcat(state->line, "\n");
    } else {
	if (state->prn == NULL && bufopen(&state->prn)) {
	    err = E_ALLOC;
	} else {
	    *execline = 0;
	    strncat(execline, cbuf, MAXLINE - 1);
	}
    }

    g_free(cbuf);
    cbuf = NULL;
    
    if (err) {
	return;
    }

    if (!debugging) {
	console_record_sample(datainfo);
	push_history_line(execline);
	state->line = execline;
	state->flags = CONSOLE_EXEC;

	/* actually execute the command line */
	err = gui_exec_line(state, &Z, datainfo);

	while (!err && gretl_execute_loop()) {
	    err = gretl_loop_exec(state, &Z, datainfo);
	}
    }

    gtk_text_buffer_get_end_iter(buf, &start);
    gtk_text_buffer_insert(buf, &start, "\n", 1);

    if (printing_is_redirected(state->prn)) {
	gretl_print_reset_buffer(state->prn);
    } else {
	print_result_to_console(buf, &start, state);
	gretl_print_destroy(state->prn);
	state->prn = NULL;
    }

    if (state->cmd->ci == QUIT) {
	gtk_widget_destroy(gtk_widget_get_toplevel(cview));
	return;
    }

    coding = gretl_compiling_loop() || gretl_compiling_function();

    /* set up promt for next command */
    gtk_text_buffer_insert_with_tags_by_name(buf, &start, 
					     (coding)? "> " : "? ", 
					     2, "redtext", NULL);

    /* scroll to end of buffer */
    console_scroll_to_end(cview, buf, &start);

    /* update variable listing in main window if needed */
    if (check_dataset_is_changed()) {
	mark_dataset_as_modified();
	populate_varlist();
    }

    /* update sample info and options if needed */
    if (console_sample_changed(datainfo)) {
	set_sample_label(datainfo);
    }

    enter_pressed = 0;
}

void show_gretl_console (void)
{
    static GtkWidget *console_view;
    char fname[MAXLEN];
    windata_t *vwin;
    GtkTextBuffer *buf;
    GtkTextIter end;
    ExecState *cstate;
    PRN *prn;
    const char *intro = 
	N_("gretl console: type 'help' for a list of commands\n? ");

    if (console_view != NULL) {
	gtk_window_present(GTK_WINDOW(gtk_widget_get_toplevel(console_view)));
	return;
    }

    if (user_fopen("console_tmp", fname, &prn)) {
	return;
    }

    cstate = gretl_console_init();
    if (cstate == NULL) {
	gretl_print_destroy(prn);
	return;
    }

    pputs(prn, _(intro));
    gretl_print_destroy(prn);

    vwin = view_file(fname, 1, 1, 78, 400, CONSOLE);
    g_object_set_data(G_OBJECT(vwin->text), "ExecState", cstate);

    console_view = vwin->text;

    g_signal_connect(G_OBJECT(vwin->text), "paste-clipboard",
		     G_CALLBACK(console_paste_handler), NULL);
    g_signal_connect(G_OBJECT(vwin->text), "button-press-event",
		     G_CALLBACK(console_click_handler), NULL);
    g_signal_connect(G_OBJECT(vwin->text), "button-release-event",
		     G_CALLBACK(console_mouse_handler), NULL);
    g_signal_connect(G_OBJECT(vwin->text), "key-press-event",
		     G_CALLBACK(console_key_handler), vwin);
    g_signal_connect(G_OBJECT(vwin->text), "destroy",
		     G_CALLBACK(gretl_console_free), vwin);
    g_signal_connect(G_OBJECT(vwin->text), "destroy",
		     G_CALLBACK(gtk_widget_destroyed),
		     &console_view);

    /* go to end of last line of text */
    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(vwin->text));
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_place_cursor(buf, &end);

    gtk_widget_grab_focus(vwin->text);
}

#define IS_BACKKEY(k) (k == GDK_BackSpace || k == GDK_Left)

static int bslash_cont (const gchar *line)
{
    int bslash = ends_with_backslash(line);

    if (cbuf == NULL) {
	cbuf = g_strdup(line);
    } else {
	gint len = strlen(cbuf) + strlen(line) + 1;

	cbuf = g_realloc(cbuf, len);
	strcat(cbuf, line);
    }

    if (bslash) {
	char *p = strrchr(cbuf, '\\');

	if (p - cbuf > 0 && !isspace(*(p - 1))) {
	    *p = ' ';
	} else {
	    *p = 0;
	}
    }

    return bslash;
}

const char *console_varname_complete (const char *s)
{
    size_t n = strlen(s);
    int i;

    for (i=0; i<datainfo->v; i++) {
	if (!strncmp(s, datainfo->varname[i], n)) {
	    return datainfo->varname[i];
	}
    }

    return NULL;
}  

static gint console_key_handler (GtkWidget *cview, GdkEventKey *key, 
				 gpointer p)
{
    gint last_line, curr_line, line_pos;
    GtkTextBuffer *buf;
    GtkTextIter iter;
    gint ret = FALSE;

 start_again:

    /* where are we? */
    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cview));
    gtk_text_buffer_get_iter_at_mark(buf, &iter, gtk_text_buffer_get_insert(buf));
    curr_line = gtk_text_iter_get_line(&iter);    
    line_pos = gtk_text_iter_get_line_index(&iter);
    last_line = gtk_text_buffer_get_line_count(buf) - 1;

    /* if at start of command line, backspacing does nothing */
    if (IS_BACKKEY(key->keyval) && line_pos < 3) {
	return TRUE;
    }

    /* if not on prompt line, return to (the end of) it */
    if (curr_line < last_line) {
	GtkTextIter end;

	gtk_text_buffer_get_end_iter(buf, &end);
	gtk_text_buffer_place_cursor(buf, &end);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(cview), TRUE);	
	goto start_again;
    }

    if (key->keyval == GDK_Return) {
	/* the Return key executes the command, unless backslash-
	   continuation is happening */
	GtkTextIter start, end;	
	gchar *line;
	int cont = 0;

	start = end = iter;
	gtk_text_iter_set_line_index(&start, 2);
	gtk_text_iter_forward_to_line_end(&end);
	line = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

	if (line != NULL) {
	    cont = bslash_cont(line);
	    g_free(line);
	}

	if (cont) {
	    gtk_text_buffer_insert_with_tags_by_name(buf, &end, 
						     "\n> ", 3,
						     "redtext", NULL);
	    console_scroll_to_end(cview, buf, &end);
	} else {
	    console_exec(cview);
#ifdef G_OS_WIN32
	    gtk_window_present(GTK_WINDOW(gtk_widget_get_toplevel(cview)));
	    gtk_widget_grab_focus(cview);
#endif
	}

	key->keyval = GDK_End;
    } else if (key->keyval == GDK_Up || key->keyval == GDK_Down) {
	/* up/down arrows: navigate the command history */
	char *histline;

	histline = pop_history_line(key->keyval);

	if (histline != NULL || key->keyval == GDK_Down) {
	    GtkTextIter start, end;

	    start = end = iter;
	    gtk_text_iter_set_line_index(&start, 2);
	    gtk_text_iter_forward_to_line_end(&end);
	    gtk_text_buffer_delete(buf, &start, &end);
	    if (histline != NULL) {
		gtk_text_buffer_insert(buf, &start, histline, strlen(histline));
	    }
	}
	
	ret = TRUE;
    } else if (key->keyval == GDK_Tab) {
	/* tab completion for gretl commands, variable names */
	const char *complete = NULL;
	GtkTextIter start, end;	
	gchar *bit = NULL;
	int offset;

	start = end = iter;
	if (!gtk_text_iter_starts_word(&start)) {
	    gtk_text_iter_backward_word_start(&start);
	}
	offset = gtk_text_iter_get_line_offset(&start);
	if (!gtk_text_iter_ends_word(&end)) {
	    gtk_text_iter_forward_word_end(&end);
	}
	bit = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

	if (bit != NULL && *bit != '\0') {
	    if (offset == 2) {
		complete = gretl_command_complete(bit);
	    } else {
		complete = console_varname_complete(bit);
	    }
	    if (complete != NULL) {
		gtk_text_buffer_delete(buf, &start, &end);
		gtk_text_buffer_insert(buf, &start, complete, 
				       strlen(complete));
	    }
	}
	g_free(bit);
	ret = TRUE;
    } else {
	GdkModifierType mods = widget_get_pointer_mask(cview);

	if (mods & GDK_CONTROL_MASK && 
	    gdk_keyval_to_upper(key->keyval) == GDK_A) {
	    /* Ctrl-A: go to start of typing area */
	    GtkTextIter where = iter;

	    gtk_text_iter_set_line_index(&where, 2);	
	    gtk_text_buffer_place_cursor(buf, &where);
	    ret = TRUE;
	}
    }

    return ret;
}

static gint on_last_line (GtkWidget *cview)
{
    GtkTextBuffer *buf;
    GtkTextIter iter;
    
    buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cview));
    gtk_text_buffer_get_iter_at_mark(buf, &iter, gtk_text_buffer_get_insert(buf));

    return (gtk_text_iter_get_line(&iter) == 
	    gtk_text_buffer_get_line_count(buf) - 1);
}

static gint console_mouse_handler (GtkWidget *cview, GdkEventButton *event,
				   gpointer p)
{
    int lline = on_last_line(cview);

    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(cview), lline);

    return FALSE;
}

static gint console_paste_text (GtkWidget *cview, GdkAtom atom)
{
    GtkClipboard *cb;
    gchar *cliptext;

    cb = gtk_clipboard_get(atom);
    cliptext = gtk_clipboard_wait_for_text(cb);

    if (cliptext != NULL) {
	GtkTextBuffer *buf;
	GtkTextIter iter;
	char *p;

	p = strchr(cliptext, '\n');
	if (p != NULL) {
	    *p = '\0';
	}

#if 0
	fprintf(stderr, "cliptext: '%s'\n", cliptext);
#endif

	buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(cview));
	gtk_text_buffer_get_end_iter(buf, &iter);
	gtk_text_buffer_insert(buf, &iter, cliptext, -1);

	g_free(cliptext);
    }

    return TRUE;
}

/* paste from clipboard: only accept plain text, and 
   automatically paste onto the command line
*/

static void console_paste_handler (GtkWidget *w, gpointer p)
{
#if 0
    return console_paste_text(w, GDK_SELECTION_CLIPBOARD);
#endif
}

static gint console_click_handler (GtkWidget *w, 
				   GdkEventButton *event,
				   gpointer p)
{
    GdkModifierType mods = widget_get_pointer_mask(w);

    if (mods & GDK_BUTTON2_MASK) {
	return console_paste_text(w, GDK_SELECTION_PRIMARY);
    }

    return FALSE;
}
