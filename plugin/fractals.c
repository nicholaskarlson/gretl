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

#include "libgretl.h"
#include "libset.h"

#define MINSAMP 8
#define LOG2 0.6931471805599453

#define HDEBUG 0

static int 
do_hurst_plot (int n, double **Z, double *yhat, const char *vname)
{
    FILE *fp = NULL;
    int t, err;

    if ((err = gnuplot_init(PLOT_RANGE_MEAN, &fp))) {
	return err;
    }

    fprintf(fp, "# rescaled range plot for %s\n", vname);
    fputs("set nokey\n", fp);
    fprintf(fp, "set title '%s %s'\n", 
	    I_("Rescaled-range plot for"), vname);
    fprintf(fp, "set xlabel '%s'\nset ylabel '%s'\n",
	    I_("log(sample size)"), I_("log(R/S)"));
    fputs("plot \\\n'-' using 1:2 w points", fp);
    fputs(" ,\\\n'-' using 1:2 w lines\n", fp);

#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "C");
#endif

    for (t=0; t<n; t++) {
	fprintf(fp, "%g %g\n", Z[2][t], Z[1][t]);
    }
    fputs("e\n", fp);

    for (t=0; t<n; t++) {
	fprintf(fp, "%g %g\n", Z[2][t], yhat[t]);
    }
    fputs("e\n", fp);

#ifdef ENABLE_NLS
    setlocale(LC_NUMERIC, "");
#endif

    fclose(fp);

    return 0;
}

#define log_2(x) (log(x) / LOG2)

#ifdef USE_DMA

static int dma_get_depth (int N)
{
    int n, depth = 0;

    for (n=MINSAMP; n<N-1; n += 8) {
	depth++;
    }

    return depth;
}

static void calc_xma (double *xma, const double *x, int N, int n)
{
    int t, k;

    for (t=n-1; t<N; t++) {
	xma[t] = 0.0;
	for (k=0; k<n; k++) {
	    xma[t] += x[t - k];
	}
	xma[t] /= n;
    }
}

static double 
calc_sigma_dma (const double *x, double *xma, int N, int n)
{
    double sdma = 0.0;
    int t;

    calc_xma(xma, x, N, n);

    for (t=n-1; t<N; t++) {
	double d = x[t] - xma[t];
	
	sdma += d * d;
    }

    return sqrt(sdma / (N - n));
}

static int hurst_calc_dma (const double *x, int N, int depth,
			   double **Z, PRN *prn)
{
    double *xma;
    int i, n;

    xma = malloc(N * sizeof *xma);
    if (xma == NULL) return 1;

    n = 8;
    for (i=0; i<depth; i++) {
	double RS = calc_sigma_dma(x, xma, N, n);
	
	Z[1][i] = log_2(RS);
	Z[2][i] = log_2(n);

	pprintf(prn, "%4d %10.5g %10.5g %10.5g\n", n, RS, Z[2][i], Z[1][i]);

	n += 8;
    }

    free(xma);

    return 0;
}

#endif /* USE_DMA */

static double get_xbar (const double *x, int n)
{
    double xsum = 0.0;
    int i, m = n;

    for (i=0; i<n; i++) {
	if (na(x[i])) {
	    m--;
	} else {
	    xsum += x[i];
	}
    }

    return xsum / m;
}

static double cum_range (const double *x, int n, double xbar)
{
    double w, wmin, wmax;
    int i;

    w = wmin = wmax = 0.0;

    for (i=1; i<n; i++) {
	if (na(x[i-1])) {
	    continue;
	}
	w += x[i-1] - xbar;
	if (w > wmax) {
	    wmax = w;
	} else if (w < wmin) {
	    wmin = w;
	}
    }

    return wmax - wmin;
}

static double stdev (const double *x, int n, double xbar)
{
    double dev, ssx = 0.0;
    int i, m = n;

    for (i=0; i<n; i++) {
	if (na(x[i])) {
	    m--;
	} else {
	    dev = x[i] - xbar;
	    ssx += dev * dev;
	}
    }

    if (ssx > 0.0) {
	dev = sqrt(ssx / m);
    } else {
	dev = 0.0;
    }

    return dev;
}

static int hurst_calc (const double *x, int n, int depth,
		       double **Z, PRN *prn)
{
    int m, i, j;

# if HDEBUG
    fprintf(stderr, "\nmax depth = %d\n", depth);
# endif

    pprintf(prn, "%5s%11s%11s%11s\n", "Size", "R/S(avg)",
	    "log(Size)", "log(R/S)");

    for (i=0, m=n; i<depth; i++, m/=2) {
	double RS = 0.0;
	int nsub = n / m;

# if HDEBUG
	fprintf(stderr, "nsub = %d\n", nsub);
	fprintf(stderr, "calculating at m = %d...\n", m);
# endif

	for (j=0; j<nsub; j++) {
	    double xbar, r, s;

	    xbar = get_xbar(x + j*m, m);
	    r = cum_range(x + j*m, m, xbar);
	    s = stdev(x + j*m, m, xbar);
# if HDEBUG
	    fprintf(stderr, "range x + %d (%d) = %g\n", j*m, m, r);
	    fprintf(stderr, "stdev x + %d (%d) = %g\n", j*m, m, s);
# endif
	    RS += r / s;
	}

	RS /= nsub;
	
	Z[1][i] = log_2(RS);
	Z[2][i] = log_2(m);

	pprintf(prn, "%4d %10.5g %10.5g %10.5g\n", m, RS, Z[2][i], Z[1][i]);
    }

    return 0;
}

static int get_depth (int T)
{
    int m = T;
    int depth = 0;

    while (m >= MINSAMP) {
	m /= 2;
	depth++;
    }

    return depth;
}

/* drop first/last observations from sample if missing obs 
   encountered */

static int h_adjust_t1t2 (int v, const double **Z, int *t1, int *t2)
{
    int t, t1min = *t1, t2max = *t2;
    int miss = 0;

    for (t=t1min; t<t2max; t++) {
	if (na(Z[v][t])) t1min += 1;
	else break;
    }

    for (t=t2max; t>t1min; t--) {
	if (na(Z[v][t])) t2max -= 1;
	else break;
    }

    *t1 = t1min; *t2 = t2max;

    for (t=t1min; t<t2max; t++) {
	if (na(Z[v][t])) {
	    miss++;
	}
    }

    return miss;
}

int hurst_exponent (int vnum, const double **Z, const DATAINFO *pdinfo, 
		    PRN *prn)
{
    double **hZ;
    DATAINFO *hinfo;
    MODEL hmod;
    int hlist[4] = { 3, 1, 0, 2 };
    int k, T;
    int t1, t2;
    int missing;
    int err = 0;

    t1 = pdinfo->t1;
    t2 = pdinfo->t2;

    missing = h_adjust_t1t2(vnum, Z, &t1, &t2);

    T = t2 - t1 + 1;

    if (T - missing < 96) {
	pputs(prn, _("Sample is too small for Hurst exponent\n"));
	errmsg(err, prn);
	return 1;
    } else if (missing) {
	pputs(prn, _("Warning: there were missing values\n"));
    }

#ifdef USE_DMA
    k = dma_get_depth(T);
#else
    k = get_depth(T);
#endif

    hinfo = create_new_dataset(&hZ, 3, k, 0);
    if (hinfo == NULL) return E_ALLOC;

    pprintf(prn, _("Rescaled range figures for %s"), 
	    pdinfo->varname[vnum]);
    pputc(prn, '\n');
    pputs(prn, _("(logs are to base 2)"));
    pputs(prn, "\n\n");

#ifdef USE_DMA
    hurst_calc_dma(Z[vnum] + t1, T, k, hZ, prn);
#else
    /* do the rescaled range calculations */
    hurst_calc(Z[vnum] + t1, T, k, hZ, prn);
#endif

    strcpy(hinfo->varname[1], "RSavg");
    strcpy(hinfo->varname[2], "size");

    hmod = lsq(hlist, &hZ, hinfo, OLS, OPT_A, 0.0);

    if ((err = hmod.errcode)) {
	pputs(prn, _("Error estimating Hurst exponent model\n"));
	errmsg(err, prn);
    } else {
	pprintf(prn, "\n%s (n = %d)\n\n", _("Regression results"), k);
	pprintf(prn, "          %12s  %11s\n", "coeff", "std. error"); 
	pprintf(prn, "Intercept %12.6g   %g\n", hmod.coeff[0], hmod.sderr[0]);
	pprintf(prn, "Slope     %12.6g   %g\n", hmod.coeff[1], hmod.sderr[1]);
	pputc(prn, '\n');
	pprintf(prn, "%s = %g\n", _("Estimated Hurst exponent"), hmod.coeff[1]);
    }

    if (!gretl_in_batch_mode() && !gretl_looping()) {
	err = do_hurst_plot(k, hZ, hmod.yhat, pdinfo->varname[vnum]);
    }

    clear_model(&hmod);
    free_Z(hZ, hinfo);
    clear_datainfo(hinfo, CLEAR_FULL);
    free(hinfo);

    return err;
}


    
