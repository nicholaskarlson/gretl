/*
 *  Copyright (c) 2004 by Allin Cottrell
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

#include "libgretl.h"
#include "internal.h"

#define is_model_ci(c) (c == OLS || c == CORC || c == HILU || \
                        c == WLS || c == POOLED || c == HCCM || \
                        c == HSK || c == ADD || c == LAD || \
                        c == OMIT || c == TSLS || c == LOGIT || \
                        c == PROBIT || c == TOBIT || c == ARMA || \
                        c == AR || c == LOGISTIC || c == NLS || \
                        c == GARCH)

struct gretl_opt {
    int ci;
    unsigned long o;
    const char *longopt;
};

struct flag_match {
    unsigned long o;
    unsigned char c;
};

/* Below: This is used as a one-way mapping from the long form
   to the char, so a given char can have more than one long-form
   counterpart. */

struct gretl_opt gretl_opts[] = {
    { ADD,      OPT_Q, "quiet" },
    { ADDTO,    OPT_Q, "quiet" },
    { ARMA,     OPT_N, "native" },
    { ARMA,     OPT_V, "verbose" },
    { ARMA,     OPT_X, "x-12-arima" },
    { BXPLOT,   OPT_O, "notches" },
    { COINT2,   OPT_O, "verbose" },
    { EQNPRINT, OPT_O, "complete" },
    { TABPRINT, OPT_O, "complete" },
    { FCASTERR, OPT_O, "plot" },
    { GARCH,    OPT_R, "robust" },
    { GARCH,    OPT_V, "verbose" },
    { GNUPLOT,  OPT_O, "with-lines" },
    { GNUPLOT,  OPT_M, "with-impulses" },
    { GNUPLOT,  OPT_S, "suppress-fitted" },
    { GNUPLOT,  OPT_Z, "dummy" },
    { GRAPH,    OPT_O, "tall" },
    { IMPORT,   OPT_O, "box1" },
    { LEVERAGE, OPT_O, "save" },
    { LMTEST,   OPT_L, "logs" },
    { LMTEST,   OPT_M, "autocorr" },
    { LMTEST,   OPT_O, "autocorr" },
    { LMTEST,   OPT_S, "squares" },    
    { LMTEST,   OPT_W, "white" },
    { MEANTEST, OPT_O, "unequal-vars" },
    { OLS,      OPT_O, "vcv" }, 
    { OLS,      OPT_R, "robust" },
    { OLS,      OPT_Q, "quiet" },
    { OMIT,     OPT_Q, "quiet" },
    { OMITFROM, OPT_Q, "quiet" },
    { OUTFILE,  OPT_A, "append" },
    { OUTFILE,  OPT_C, "close" },
    { OUTFILE,  OPT_W, "write" },
    { PANEL,    OPT_C, "cross-section" },
    { PANEL,    OPT_S, "time-series" },
    { PCA,      OPT_A, "save-all" },
    { PCA,      OPT_O, "save" },
    { PERGM,    OPT_O, "bartlett" },
    { PLOT,     OPT_O, "one-scale" },
    { PRINT,    OPT_O, "byobs" },
    { PRINT,    OPT_T, "ten" },
    { SMPL,     OPT_O, "dummy" },
    { SMPL,     OPT_M, "no-missing" },
    { SMPL,     OPT_R, "restrict" },
    { SPEARMAN, OPT_O, "verbose" },
    { SQUARE,   OPT_O, "cross" },
    { STORE,    OPT_C, "csv" },
    { STORE,    OPT_M, "gnu-octave" },
    { STORE,    OPT_R, "gnu-R" },
    { STORE,    OPT_T, "traditional" },
    { STORE,    OPT_Z, "gzipped" },
    { TOBIT,    OPT_V, "verbose" },
    { VAR,      OPT_Q, "quiet" },    
    { 0,        0L,    NULL }
};

const char **get_opts_for_command (int ci)
{
    int i, j, nopt = 0;
    const char **ret = NULL;

    if (is_model_ci(ci) && ci != OLS && ci != LAD) {
	nopt++; /* vcv */
    }

    for (i=0; gretl_opts[i].ci != 0; i++) {
	if (gretl_opts[i].ci == ci) nopt++;
    }

    if (nopt == 0) return NULL;

    ret = malloc((nopt + 1) * sizeof *ret);
    if (ret == NULL) return NULL;

    j = 0;
    for (i=0; gretl_opts[i].ci != 0; i++) {
	if (gretl_opts[i].ci == ci) {
	    ret[j++] = gretl_opts[i].longopt;
	}
    }

    if (is_model_ci(ci) && ci != OLS && ci != LAD) {
	ret[j++] = "vcv";
    }

    ret[j] = NULL;

    return ret;
}

struct flag_match flag_matches[] = {
    { OPT_A, 'a' },
    { OPT_B, 'b' },
    { OPT_C, 'c' },
    { OPT_D, 'd' },
    { OPT_I, 'i' },
    { OPT_L, 'l' },
    { OPT_M, 'm' },
    { OPT_N, 'n' },
    { OPT_O, 'o' },
    { OPT_Q, 'q' },
    { OPT_R, 'r' },
    { OPT_S, 's' },
    { OPT_T, 't' },
    { OPT_V, 'v' },
    { OPT_W, 'w' },
    { OPT_X, 'x' },
    { OPT_Z, 'z' },
    { 0L,   '\0' }
};

/* note: 'f' is not treated as an option flag for now */

#define isflag(c) (c == 'a' || c == 'b' || c == 'c' || c == 'd' || \
                   c == 'i' || c == 'l' || c == 'm' || \
                   c == 'n' || c == 'o' || c == 'q' || c == 'r' || \
                   c == 's' || c == 't' || c == 'v' || c == 'w' || \
                   c == 'x' || c == 'z')

static unsigned long opt_from_flag (unsigned char c)
{
    int i;

    for (i=0; flag_matches[i].c != '\0'; i++) {
	if (c == flag_matches[i].c) return flag_matches[i].o;
    }

    return 0L;
}

static int opt_is_valid (unsigned long opt, int ci, char c)
{
    int i;

    if (opt == OPT_O && is_model_ci(ci)) {
	return 1;
    }

    for (i=0; gretl_opts[i].ci != 0; i++) {
	if (ci == gretl_opts[i].ci && opt == gretl_opts[i].o) {
	    return 1;
	}
    }

    if (c != 0) {
	sprintf(gretl_errmsg, "Invalid option '-%c'", c);
    } 

    return 0;
}

static unsigned long get_short_opts (char *line, int ci, int *err)
{
    char *p = strchr(line, '-');
    unsigned long opt, ret = 0L;

    while (p != NULL) {
	unsigned char c, prev;
	int match = 0;
	size_t n = strlen(p);

	c = *(p + 1);
	prev = *(p - 1);
	
	if (isspace(prev) && isflag(c) && (n == 2 || isspace(*(p + 2)))) {
	    opt = opt_from_flag(c);
	    if (!opt_is_valid(opt, ci, c)) {
		*err = 1;
		return 0L;
	    }
	    ret |= opt;
	    _delete(p, 0, 2);
	    match = 1;
	}
	if (!match) p++;
	p = strchr(p, '-');
    }

    return ret;
}

static int is_long_opt (const char *lopt)
{
    int i, ret = 0;

    for (i=0; gretl_opts[i].o != 0; i++) {
	if (!strcmp(lopt, gretl_opts[i].longopt)) {
	    ret = 1;
	    break;
	}
    }

    return ret;
}

static int valid_long_opt (int ci, const char *lopt)
{
    int i;
    int opt = 0L;

    if (is_model_ci(ci) && ci != LAD && !strcmp(lopt, "vcv")) {
	return OPT_O;
    }

    for (i=0; gretl_opts[i].o != 0; i++) {
	if (ci == gretl_opts[i].ci && !strcmp(lopt, gretl_opts[i].longopt)) {
	    opt = gretl_opts[i].o;
	}
    }

    return opt;
}
  
static unsigned long get_long_opts (char *line, int ci, int *err)
{
    char *p = strstr(line, "--");
    unsigned long match, ret = 0L;

    while (p != NULL) {
	char longopt[32];

	sscanf(p + 2, "%31s", longopt);
	match = valid_long_opt(ci, longopt);
	if (match > 0) {
	    ret |= match;
	    _delete(p, 0, 2 + strlen(longopt));
	} else if (is_long_opt(longopt)) {
	    /* recognized option, but not valid for the command */
	    sprintf(gretl_errmsg, "Invalid option '--%s'", longopt);
	    *err = 1;
	    return 0L;
	} else {
	    p += 2;
	}
	p = strstr(p, "--");
    }

    return ret;
}

static void get_cmdword (const char *line, char *word)
{
    if (!sscanf(line, "%*s <- %8s", word)) {
	sscanf(line, "%8s", word);
    }
}

int catchflags (char *line, unsigned long *oflags)
     /* check for option flags in line: if found, chop them out 
	and set oflags value accordingly.  
	Strip trailing semicolon while we're at it.
	Return 0 if all is OK, 1 if there's an invalid option.
     */
{
    int n = strlen(line);
    unsigned long opt;
    char cmdword[9] = {0};
    int ci, err = 0;

    *oflags = 0L;
    *gretl_errmsg = '\0';

    if (n < 2 || *line == '#') return 0;

    /* to enable reading of trad. esl input files */
    if (line[n-2] == ';' && isspace(line[n-1])) {
	line[n-2] = '\0';
    } else if (line[n-1] == ';') {
	line[n-1] = '\0';
    }

    /* some commands do not take a "flag", and "-%c" may have
       some other meaning */
    get_cmdword(line, cmdword);

    if (!strcmp(cmdword, "genr") || !strcmp(cmdword, "sim") ||
	!strcmp(cmdword, "label")) return 0;

    if (strstr(line, "end nls")) {
	ci = NLS;
    } else {
	ci = gretl_command_number(cmdword);
    }

    if (ci == 0) return 0;

    /* try for short-form options (e.g. "-o") */
    opt = get_short_opts(line, ci, &err);
    if (!err && opt) {
	*oflags |= opt;
    }

    /* try for long-form options (e.g. "--vcv") */
    if (!err) {
	opt = get_long_opts(line, ci, &err);
	if (!err && opt) {
	    *oflags |= opt;
	}
    }

    return err;
}

const char *print_flags (unsigned long flags, int ci)
{
    static char flagstr[64];
    char fbit[20];
    int i;

    flagstr[0] = '\0';

    if (flags == 0L) return flagstr;

    /* special: -o (--vcv) can be used with several model
       commands */
    if ((flags & OPT_O) && is_model_ci(ci)) {
	strcat(flagstr, " --vcv");
	flags &= ~OPT_O;
    }

    for (i=0; gretl_opts[i].ci != 0; i++) {
	if (ci == gretl_opts[i].ci && (flags & gretl_opts[i].o)) {
	    sprintf(fbit, " --%s", gretl_opts[i].longopt);
	    strcat(flagstr, fbit);
	}
    }

    return flagstr;
}
