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

#include "libgretl.h"
#include "libset.h"
#include "gretl_restrict.h"
#include "gretl_xml.h"
#include "bootstrap.h"

#define BDEBUG 0

enum {
    BOOT_CI          = 1 << 0,  /* compute confidence interval */
    BOOT_PVAL        = 1 << 1,  /* compute p-value */
    BOOT_RESAMPLE_U  = 1 << 2,  /* resample the empirical residuals */
    BOOT_NORMAL_U    = 1 << 3,  /* simulate normal residuals */
    BOOT_STUDENTIZE  = 1 << 4,  /* studentize, when doing confidence interval */
    BOOT_GRAPH       = 1 << 5,  /* graph the distribution */
    BOOT_LDV         = 1 << 6,  /* model includes lagged dep var */
    BOOT_RESTRICT    = 1 << 7,  /* called via "restrict" command */
    BOOT_F_FORM      = 1 << 8,  /* compute F-statistics */
    BOOT_FREE_RQ     = 1 << 9,  /* free restriction matrices */
    BOOT_SAVE        = 1 << 10, /* save results vector */
    BOOT_VERBOSE     = 1 << 11  /* for debugging */
};

#define resampling(b) (b->flags & BOOT_RESAMPLE_U)
#define verbose(b) (b->flags & BOOT_VERBOSE)

typedef struct boot_ boot;

struct boot_ {
    int flags;          /* option flags */
    int B;              /* number of replications */
    int k;              /* number of coefficients in model */
    int T;              /* number of observations used */
    int p;              /* index number of coeff to examine */
    int g;              /* number of restrictions */
    int mci;            /* model command index */
    int ldvpos;         /* col. number of lagged dep var in X matrix */
    gretl_matrix *y;    /* holds original, then artificial, dep. var. */
    gretl_matrix *X;    /* independent variables */
    gretl_matrix *b0;   /* coefficients used to generate dep var */
    gretl_matrix *u0;   /* original residuals for resampling */
    gretl_matrix *R;    /* LHS restriction matrix */
    gretl_matrix *q;    /* RHS restriction matrix */
    gretl_matrix *w;    /* weights for WLS */
    double SE;          /* original std. error of residuals */
    double point;       /* point estimate of coeff */
    double se0;         /* original std. error for coeff of interest */
    double test0;       /* original test statistic */
    double b_p;         /* test value for coeff */
    double a;           /* alpha, for confidence interval */
    char vname[VNAMELEN]; /* name of variable analysed */
};

static gretl_vector *bs_data;
static char bs_vname[VNAMELEN];

static void boot_destroy (boot *bs)
{
    gretl_matrix_free(bs->y);
    gretl_matrix_free(bs->X);
    gretl_matrix_free(bs->b0);
    gretl_matrix_free(bs->u0);
    gretl_matrix_free(bs->w);

    if (bs->flags & BOOT_FREE_RQ) {
	gretl_matrix_free(bs->R);
	gretl_matrix_free(bs->q);
    }

    free(bs);
}

static int 
make_model_matrices (boot *bs, const MODEL *pmod, const double **Z)
{
    double xti;
    int T = pmod->nobs;
    int k = pmod->ncoeff;
    int needw = 0;
    int i, s, t;

    bs->y = gretl_column_vector_alloc(T);
    bs->X = gretl_matrix_alloc(T, k);
    bs->b0 = gretl_column_vector_alloc(k);
    bs->u0 = gretl_column_vector_alloc(T);

    if (pmod->ci == WLS || pmod->ci == HSK) {
	needw = 1;
	bs->w = gretl_column_vector_alloc(T);
    }

    if (bs->y == NULL || bs->X == NULL || bs->b0 == NULL || 
	bs->u0 == NULL || (needw && bs->w == NULL)) {
	return E_ALLOC;
    }

    for (i=0; i<k; i++) {
	bs->b0->val[i] = pmod->coeff[i];
    }    

    s = 0;
    for (t=pmod->t1; t<=pmod->t2; t++) {
	if (!na(pmod->uhat[t])) {
	    bs->y->val[s] = Z[pmod->list[1]][t];
	    if (pmod->ci == WLS) {
		bs->w->val[s] = sqrt(Z[pmod->nwt][t]);
		bs->y->val[s] *= bs->w->val[s];
		bs->u0->val[s] = bs->y->val[s];
	    } else {
		bs->u0->val[s] = pmod->uhat[t];
	    }
	    for (i=2; i<=pmod->list[0]; i++) {
		xti = Z[pmod->list[i]][t];
		if (pmod->ci == WLS) {
		    xti *= bs->w->val[s];
		    bs->u0->val[s] -= bs->b0->val[i-2] * xti;
		}
		gretl_matrix_set(bs->X, s, i-2, xti);
	    }
	    s++;
	}
    }

    return 0;
}

static int make_bs_flags (gretlopt opt)
{
    int flags = 0;

    if (opt & OPT_P) {
	flags |= BOOT_PVAL;
    } else {
	flags |= BOOT_CI;
    }

    if (opt & OPT_N) {
	flags |= BOOT_NORMAL_U;
    } else {
	flags |= BOOT_RESAMPLE_U;
    }

    if (opt & OPT_G) {
	flags |= BOOT_GRAPH;
    }

    if (opt & OPT_T) {
	flags |= BOOT_STUDENTIZE;
    }

    if (opt & OPT_R) {
	flags |= BOOT_RESTRICT;
    }

    if (opt & OPT_F) {
	flags |= BOOT_F_FORM;
    }

    if (opt & OPT_S) {
	flags |= BOOT_SAVE;
    }

#if BDEBUG > 1
    flags |= BOOT_VERBOSE;
#endif

    return flags;
}

/* check in with libset for current default number of replications if
   need be; in addition, alpha * (B + 1) should be an integer, when
   constructing confidence intervals
*/

int maybe_adjust_B (int B, double a, int flags)
{
    if (B <= 0) {
	B = get_bootstrap_replications();
    }

    if (flags & BOOT_CI) {
	double x;

	if (B % 10 == 0) {
	    B--;
	}

	x = a * (B + 1);
	while (x - floor(x) > 1e-13) {
	    x = a * (++B + 1);
	}
    }

    return B;
}

static boot *boot_new (const MODEL *pmod,
		       const double **Z,
		       int B, gretlopt opt)
{
    boot *bs;
    int ldv;

    bs = malloc(sizeof *bs);
    if (bs == NULL) {
	return NULL;
    }

    bs->y = NULL;
    bs->X = NULL;
    bs->b0 = NULL;
    bs->u0 = NULL;
    bs->w = NULL;

    bs->R = NULL;
    bs->q = NULL;

    if (make_model_matrices(bs, pmod, Z)) {
	boot_destroy(bs);
	return NULL;
    }

    bs->flags = make_bs_flags(opt);

    bs->mci = pmod->ci;
    bs->a = 0.05; /* make configurable? */
    bs->B = maybe_adjust_B(B, bs->a, bs->flags);

    bs->p = 0;
    bs->g = 0;

    ldv = gretl_model_get_int(pmod, "ldepvar");
    if (ldv > 0) {
	bs->flags |= BOOT_LDV;
	bs->ldvpos = ldv - 2;
    } else {
	bs->ldvpos = -1;
    }

    *bs->vname = '\0';

    bs->SE = NADBL;
    bs->point = NADBL;
    bs->se0 = NADBL;
    bs->test0 = NADBL;
    bs->b_p = NADBL;

    bs->k = bs->X->cols;
    bs->T = bs->X->rows;

    return bs;
}

static void make_normal_y (boot *bs)
{
    double xti;
    int i, t;

    /* generate scaled normal errors */
    gretl_rand_normal(bs->y->val, 0, bs->T - 1);
    for (t=0; t<bs->T; t++) {
	bs->y->val[t] *= bs->SE;
    }

    /* construct y recursively */
    for (t=0; t<bs->X->rows; t++) {
	for (i=0; i<bs->X->cols; i++) {
	    if (t > 0 && i == bs->ldvpos) {
		gretl_matrix_set(bs->X, t, i, bs->y->val[t-1]);
	    } 
	    xti = gretl_matrix_get(bs->X, t, i);
	    bs->y->val[t] += bs->b0->val[i] * xti;
	}
    }  	
}

static void 
resample_vector (const gretl_matrix *u0, gretl_matrix *u,
		 double *z)
{
    int T = u->rows;
    int i, t;

    /* generate uniform random series */
    gretl_rand_uniform(z, 0, T - 1);

    /* sample from source vector based on indices */
    for (t=0; t<T; t++) {
	i = T * z[t];
	if (i > T - 1) {
	    i = T - 1;
	}
	u->val[t] = u0->val[i];
    }
}

static void 
make_resampled_y (boot *bs, double *z)
{
    double xti;
    int i, t;

    /* resample the residuals, into y */
    resample_vector(bs->u0, bs->y, z);

    /* construct y recursively */
    for (t=0; t<bs->X->rows; t++) {
	for (i=0; i<bs->X->cols; i++) {
	    if (t > 0 && i == bs->ldvpos) {
		gretl_matrix_set(bs->X, t, i, bs->y->val[t-1]);
	    }
	    xti = gretl_matrix_get(bs->X, t, i);
	    bs->y->val[t] += bs->b0->val[i] * xti;
	}
    }
}

/* when doing a bootstrap p-value: run the restricted regression; save
   the coefficient vector in bs->b0 and residuals in bs->u0
*/

static int do_restricted_ols (boot *bs)
{
    double s2 = 0.0;
    int err = 0;
    
    err = gretl_matrix_restricted_ols(bs->y, bs->X, bs->R, bs->q, bs->b0, 
				      NULL, bs->u0, &s2);

#if BDEBUG
    if (1) {
	int i;

	fprintf(stderr, "Restricted estimates (err = %d):\n", err);
	for (i=0; i<bs->b0->rows; i++) {
	    fprintf(stderr, "b[%d] = %g\n", i, bs->b0->val[i]);
	}
	fprintf(stderr, "s2 = %g\n", s2);
	fprintf(stderr, "bs->ldvpos = %d\n", bs->ldvpos);
    }
#endif

    if (!err) {
	bs->SE = sqrt(s2);
    }

    return err;
}

static int bs_store_result (boot *bs, double *xi)
{
    int i;

    if (bs_data != NULL) {
	gretl_matrix_free(bs_data);
	bs_data = NULL;
    }

    bs_data = gretl_column_vector_alloc(bs->B);
    if (bs_data == NULL) {
	return E_ALLOC;
    }

    for (i=0; i<bs->B; i++) {
	bs_data->val[i] = xi[i];
    }

    *bs_vname = '\0';

    if (bs->flags & BOOT_F_FORM) {
	strcpy(bs_vname, "F_test");
    } else {
	if (bs->flags & (BOOT_PVAL | BOOT_STUDENTIZE)) {
	    strcpy(bs_vname, "t_");
	} else {
	    strcpy(bs_vname, "b_");
	}
	strncat(bs_vname, bs->vname, VNAMELEN - 3);
    }

    return 0;
}

static void bs_print_result (boot *bs, double *xi, int tail, PRN *prn)
{
    if (bs->flags & BOOT_RESTRICT) {
	pputs(prn, "\n  ");
    } else {
	pprintf(prn, _("For the coefficient on %s (point estimate %g)"), 
		bs->vname, bs->point);
	pputs(prn, ":\n\n  ");
    }

    if (bs->flags & (BOOT_CI | BOOT_GRAPH)) {
	qsort(xi, bs->B, sizeof *xi, gretl_compare_doubles);
    }

    if (bs->flags & BOOT_PVAL) {
	double pv = (double) tail / bs->B;

	pprintf(prn, "%s = %d / %d = %g", _("p-value"), tail, bs->B, pv);
    } else {
	double ql, qu;
	int i, j;

	i = bs->a * (bs->B + 1) / 2.0;
	ql = xi[i-1];

	j = bs->B - i + 1;
	qu = xi[j-1];

	if (bs->flags & BOOT_STUDENTIZE) {
	    double cl = ql;

	    ql = bs->point - bs->se0 * qu;
	    qu = bs->point - bs->se0 * cl;
	    pprintf(prn, _("Studentized %g%% confidence interval = %g to %g"), 
		    100 * (1 - bs->a), ql, qu);
	} else {
	    pprintf(prn, _("%g%% confidence interval = %g to %g"), 
		    100 * (1 - bs->a), ql, qu);
	}
    }
    
    pputs(prn, "\n\n");
    pprintf(prn, _("Based on %d replications"), bs->B);
    pputs(prn, ", ");
    if (bs->flags & BOOT_RESAMPLE_U) {
	pputs(prn, _("using resampled residuals"));
    } else {
	pputs(prn, _("with simulated normal errors"));
    }
    pputc(prn, '\n');

    if (bs->flags & BOOT_LDV) {
	pprintf(prn, "(%s)",  _("recognized lagged dependent variable"));
	pputc(prn, '\n');
    }

    if (bs->flags & BOOT_GRAPH) {
	int (*kdfunc) (const double *, int, const char *);
	void *handle;
	char label[48];
	int err;

	kdfunc = get_plugin_function("array_kernel_density", &handle);
	if (kdfunc == NULL) {
	    return;
	}

	if (bs->flags & BOOT_F_FORM) {
	    strcpy(label, G_("bootstrap F-test"));
	} else if (bs->flags & (BOOT_PVAL | BOOT_STUDENTIZE)) {
	    strcpy(label, G_("bootstrap t-ratio"));
	} else {
	    strcpy(label, G_("bootstrap coefficient"));
	} 

	err = (*kdfunc)(xi, bs->B, label);
	close_plugin(handle);
    }
}

/* Davidson and MacKinnon, ETM, p. 163 */

static void rescale_residuals (boot *bs)
{
    double s;
    int t, k = bs->k;

    if (bs->flags & BOOT_PVAL) {
	k--;
    }

    s = sqrt((double) bs->T / (bs->T - k));

    for (t=0; t<bs->T; t++) {
	bs->u0->val[t] *= s;
    }
}

/* we do the following on each replication if we're bootstrapping
   an F-test (not for just a single-coefficient p-value) */

static double bs_F_test (const gretl_matrix *b,
			 const gretl_matrix *V, 
			 boot *bs, int *errp)
{
    gretl_vector *br = NULL;
    gretl_matrix *RVR = NULL;
    double test = 0.0;
    int err = 0;

    br = gretl_column_vector_alloc(bs->g);
    RVR = gretl_matrix_alloc(bs->R->rows, bs->R->rows);

    if (br == NULL || RVR == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    err = gretl_matrix_multiply(bs->R, b, br);
    if (err) {
	fprintf(stderr, "Failed: gretl_matrix_multiply(R, b, br)\n");
	goto bailout;
    }

    if (!gretl_is_zero_matrix(bs->q)) {
	err = gretl_matrix_subtract_from(br, bs->q);
	if (err) {
	    fprintf(stderr, "Failed: gretl_matrix_subtract_from(br, q)\n");
	    goto bailout;
	}
    }

    gretl_matrix_qform(bs->R, GRETL_MOD_NONE, V,
		       RVR, GRETL_MOD_NONE);

    err = gretl_invert_symmetric_matrix(RVR);

    if (!err) {
	test = gretl_scalar_qform(br, RVR, &err);
    }

    if (!err) {
	test /= bs->g;
    }

 bailout:

    gretl_vector_free(br);
    gretl_matrix_free(RVR);

    *errp = err;

    return test;
}

#if 0

static char *squarable_cols_mask (const gretl_matrix *X, int *n)
{
    const double *xi = X->val;
    char *mask;
    int i, t;

    mask = calloc(X->cols, 1);
    if (mask == NULL) {
	return NULL;
    }

    for (i=0; i<X->cols; i++) {
	for (t=0; t<X->rows; t++) {
	    if (xi[t] != 1.0 && xi[t] != 0.0) {
		mask[i] = 1;
		*n += 1;
		break;
	    }
	}
	xi += X->rows;
    }

    return mask;
}

static int hsk_transform_data (boot *bs, gretl_matrix *b,
			       gretl_matrix *yh)
{
    gretl_matrix *X = NULL;
    gretl_matrix *g = NULL;
    int Xcols = bs->k;
    double *xi, *xj;
    double ut;
    char *mask = NULL;
    int bigX = 0;
    int i, t, err = 0;

    gretl_matrix_multiply(bs->X, b, yh);

    /* calculate log of uhat squared */
    for (t=0; t<bs->T; t++) {
	ut = bs->y->val[t] - yh->val[t];
	bs->w->val[t] = log(ut * ut);
    } 

    mask = squarable_cols_mask(bs->X, &Xcols);
    if (mask == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    X = gretl_matrix_alloc(bs->T, Xcols);
    if (X == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    if (Xcols > bs->k) {
	bigX = 1;
    }
	    
    g = gretl_column_vector_alloc(Xcols);
    if (g == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    xi = bs->X->val;
    xj = X->val;

    for (i=0; i<bs->k; i++) {
	for (t=0; t<bs->T; t++) {
	    xj[t] = xi[t];
	}
	xi += bs->T;
	xj += bs->T;
    }

    if (bigX) {
	xi = bs->X->val;
	for (i=0; i<bs->k; i++) {
	    if (mask[i]) {
		for (t=0; t<bs->T; t++) {
		    xj[t] = xi[t] * xi[t];
		}
		xj += bs->T;
	    }
	    xi += bs->T;
	}
    }

    err = gretl_matrix_ols(bs->w, X, g, NULL, NULL, NULL);
    if (err) {
	goto bailout;
    }

    /* form weighted dataset */

    gretl_matrix_multiply(X, g, yh);
    for (t=0; t<bs->T; t++) {
	bs->w->val[t] = sqrt(1.0 / exp(yh->val[t]));
    }

    gretl_matrix_reuse(X, bs->T, bs->k);
    xi = X->val;
    for (i=0; i<bs->k; i++) {
	for (t=0; t<bs->T; t++) {
	    xi[t] *= bs->w->val[t];
	}
	xi += bs->T;
    }
    for (t=0; t<bs->T; t++) {
	bs->w->val[t] *= bs->y->val[t];
    }

    /* do WLS */
    err = gretl_matrix_ols(bs->w, X, b, NULL, NULL, NULL);

 bailout:

    gretl_matrix_free(g);
    gretl_matrix_free(X);
    free(mask);

    return err;
}

#endif

/* do the actual bootstrap analysis: the objective is either to form a
   confidence interval or to compute a p-value; the methodology is
   either to resample the original residuals or to simulate normal
   errors with the empirically given variance.
*/

static int real_bootstrap (boot *bs, PRN *prn)
{
    gretl_matrix *XTX = NULL;   /* X'X */
    gretl_matrix *XTXI = NULL;  /* X'X^{-1} */
    gretl_matrix *b = NULL;     /* re-estimated coeffs */
    gretl_matrix *yh = NULL;    /* fitted values */
    gretl_matrix *V = NULL;     /* covariance matrix */
    double *z = NULL;
    double *xi = NULL;
    int k = bs->k;
    int p = bs->p;
    int tail = 0;
    int i, t, err = 0;

    if (bs->flags & BOOT_PVAL) {
	err = do_restricted_ols(bs);
	if (err) {
	    return err;
	}
    }

    b = gretl_column_vector_alloc(k);
    XTX = gretl_matrix_alloc(k, k);
    yh = gretl_column_vector_alloc(bs->T);

    if (b == NULL || XTX == NULL || yh == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    if (resampling(bs)) {
	/* resampling index array */
	z = malloc(bs->T * sizeof *z);
	if (z == NULL) {
	    err = E_ALLOC;
	    goto bailout;
	}
	rescale_residuals(bs);
    }

    if (bs->flags & (BOOT_CI | BOOT_GRAPH | BOOT_SAVE)) {
	/* array for storing results */
	xi = malloc(bs->B * sizeof *xi);
	if (xi == NULL) {
	    err = E_ALLOC;
	    goto bailout;
	}
    }	

    if (bs->flags & BOOT_F_FORM) {
	/* covariance matrix for F-test */
	V = gretl_matrix_alloc(XTX->rows, XTX->cols);
	if (V == NULL) {
	    err = E_ALLOC;
	    goto bailout;
	}
    }	    

    gretl_matrix_multiply_mod(bs->X, GRETL_MOD_TRANSPOSE,
			      bs->X, GRETL_MOD_NONE,
			      XTX, GRETL_MOD_NONE);

    XTXI = gretl_matrix_alloc(XTX->rows, XTX->cols);
    if (XTXI == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    err = gretl_matrix_cholesky_decomp(XTX);
    if (!err) {
	err = gretl_inverse_from_cholesky_decomp(XTXI, XTX);
    }

    if (verbose(bs)) {
	pprintf(prn, "%13s %13s\n", "b", "tval");
    }

    /* carry out B replications */

    for (i=0; i<bs->B && !err; i++) {
	double vpp, se, SSR, s2, ut, test;

#if BDEBUG > 1
	fprintf(stderr, "real_bootstrap: round %d\n", i);
#endif

	if (bs->flags & BOOT_NORMAL_U) {
	    make_normal_y(bs);
	} else {
	    make_resampled_y(bs, z); 
	}

	if (bs->ldvpos >= 0) {
	    /* X matrix includes lagged dependent variable, so it has
	       to be modified */
	    for (t=1; t<bs->T; t++) {
		gretl_matrix_set(bs->X, t, bs->ldvpos, bs->y->val[t-1]);
	    }
	    gretl_matrix_multiply_mod(bs->X, GRETL_MOD_TRANSPOSE,
				      bs->X, GRETL_MOD_NONE,
				      XTX, GRETL_MOD_NONE);
	    err = gretl_matrix_cholesky_decomp(XTX);
	    if (!err) {
		err = gretl_inverse_from_cholesky_decomp(XTXI, XTX);
	    }
	}

	if (!err) {
	    /* form X'y */
	    gretl_matrix_multiply_mod(bs->X, GRETL_MOD_TRANSPOSE,
				      bs->y, GRETL_MOD_NONE,
				      b, GRETL_MOD_NONE);
	}

	if (!err) {
	    /* solve for current parameter estimates */
	    err = gretl_cholesky_solve(XTX, b);
	}

#if 0
	if (!err && bs->mci == HSK) {
	    err = hsk_transform_data(bs, b, yh);
	}
#endif

	if (err) {
	    break;
	}	

	/* form fitted values */
	gretl_matrix_multiply(bs->X, b, yh);

	/* residuals, etc */
	SSR = 0.0;
	for (t=0; t<bs->T; t++) {
	    ut = bs->y->val[t] - yh->val[t];
	    SSR += ut * ut;
	} 
	s2 = SSR / (bs->T - k);

	/* F-test, if wanted */
	if (bs->flags & BOOT_F_FORM) {
	    gretl_matrix_copy_values(V, XTXI);
	    gretl_matrix_multiply_by_scalar(V, s2);
	    test = bs_F_test(b, V, bs, &err);
	    if (test > bs->test0) {
		tail++;
	    }
	    if (bs->flags & (BOOT_GRAPH | BOOT_SAVE)) {
		xi[i] = test;
	    }
	    continue;
	}

	/* coeff, standard error and t-stat */
	vpp = gretl_matrix_get(XTXI, p, p);
	se = sqrt(s2 * vpp);
	test = (b->val[p] - bs->b_p) / se;

	if (verbose(bs)) {
	    pprintf(prn, "%13g %13g\n", b->val[p], test);
	}

	if (bs->flags & BOOT_CI) {
	    if (bs->flags & BOOT_STUDENTIZE) {
		xi[i] = test;
	    } else {
		xi[i] = b->val[p];
	    }
	} else {
	    /* doing p-value */
	    if (bs->flags & (BOOT_GRAPH | BOOT_SAVE)) {
		xi[i] = test;
	    }
	    if (fabs(test) > fabs(bs->test0)) {
		tail++;
	    }
	}
    }

    if (!err) {
	if (bs->flags & BOOT_SAVE) {
	    bs_store_result(bs, xi);
	}
	bs_print_result(bs, xi, tail, prn);
    }

 bailout:

    gretl_matrix_free(b);
    gretl_matrix_free(XTX);
    gretl_matrix_free(XTXI);
    gretl_matrix_free(yh);
    gretl_matrix_free(V);

    free(z);
    free(xi);
    
    return err;
}

/* add basic restriction matrices R and q when doing a p-value
   calculation for a single variable */

static int bs_add_restriction (boot *bs, int p)
{
    bs->R = gretl_zero_matrix_new(1, bs->b0->rows);
    bs->q = gretl_zero_matrix_new(1, 1);

    if (bs->R == NULL || bs->q == NULL) {
	return E_ALLOC;
    }

    bs->R->val[p] = 1.0;

    return 0;
}

/**
 * bootstrap_analysis:
 * @pmod: model to be examined.
 * @p: 0-based index number of the coefficient to analyse.
 * @B: number of replications.
 * @Z: data array.
 * @pdinfo: dataset information.
 * @opt: option flags -- may contain %OPT_P to compute p-value
 * (default is to calculate confidence interval), %OPT_N
 * to use simulated normal errors (default is to resample the
 * empirical residuals), %OPT_G to display graph.
 * @prn: printing struct.
 *
 * Calculates a bootstrap confidence interval or p-value for
 * a given coefficient in a given model, estimated via OLS.
 * If the first lag of the dependent variable is present as a
 * regressor it is handled correctly but more complex lag
 * autoregressive schemes are not (yet) handled.
 * 
 * Returns: 0 on success, non-zero code on error.
 */

int bootstrap_analysis (MODEL *pmod, int p, int B, const double **Z,
			const DATAINFO *pdinfo, gretlopt opt,
			PRN *prn)
{
    boot *bs = NULL;
    int err = 0;

    if (!bootstrap_ok(pmod->ci)) {
	return E_NOTIMP;
    }

    if (p < 0 || p >= pmod->ncoeff) {
	return E_DATA;
    }

    bs = boot_new(pmod, Z, B, opt);
    if (bs == NULL) {
	err = E_ALLOC;
    }    

    if (!err && (bs->flags & BOOT_PVAL)) {
	err = bs_add_restriction(bs, p);
    }

    if (!err) {
	int v = pmod->list[p+2];

	bs->p = p;  /* coeff to examine */
	if (pmod->ci == HSK) {
	    bs->SE = gretl_model_get_double(pmod, "sigma_orig");
	} else {
	    bs->SE = pmod->sigma;
	}
	strcpy(bs->vname, pdinfo->varname[v]);
	bs->point = pmod->coeff[p];
	bs->se0 = pmod->sderr[p];
	bs->test0 = pmod->coeff[p] / pmod->sderr[p];
	if (bs->flags & BOOT_PVAL) {
	    bs->b_p = 0.0;
	} else {
	    bs->b_p = bs->point;
	}
	err = real_bootstrap(bs, prn);
    }

    boot_destroy(bs);

    return err;
}

/**
 * bootstrap_test_restriction:
 * @pmod: model to be examined.
 * @R: left-hand restriction matrix, as in Rb = q.
 * @q: right-hand restriction matrix.
 * @test: initial test statistic.
 * @g: number of restrictions.
 * @Z: data array.
 * @pdinfo: dataset information.
 * @prn: printing struct.
 *
 * Calculates a bootstrap p-value for the restriction on the
 * coefficients of @pmod represented by the matrices @R and @q.
 * If the first lag of the dependent variable is present as a
 * regressor it is handled correctly but more complex lag
 * autoregressive schemes are not (yet) handled.
 * 
 * Returns: 0 on success, non-zero code on error.
 */

int bootstrap_test_restriction (MODEL *pmod, gretl_matrix *R, 
				gretl_matrix *q, double test, int g,
				const double **Z, const DATAINFO *pdinfo, 
				PRN *prn)
{
    boot *bs = NULL;
    gretlopt bopt;
    int B = 0;
    int err = 0;

#if BDEBUG
    fprintf(stderr, "bootstrap_test_restriction: on input test = %g, g = %d\n",
	    test, g);
    gretl_matrix_print(R, "R");
    gretl_matrix_print(q, "q");
#endif    

    bopt = (OPT_P | OPT_R | OPT_F);
    gretl_restriction_get_boot_params(&B, &bopt);

    bs = boot_new(pmod, Z, B, bopt);
    if (bs == NULL) {
	err = E_ALLOC;
    } 

    if (!err) {
	bs->R = R;
	bs->q = q;
	bs->g = g;
	bs->test0 = test;
	strcpy(bs->vname, "F-test");
	err = real_bootstrap(bs, prn);
    }

    boot_destroy(bs);

    return err;
}

int bootstrap_ok (int ci)
{
    return (ci == OLS || ci == WLS); /* HSK?? */
}

int bootstrap_save_data (const char *fname)
{
    char **S = NULL;
    int err, ns = 0;

    if (bs_data == NULL) {
	return E_DATA;
    }

    err = strings_array_add(&S, &ns, bs_vname);
    if (err) {
	return err;
    }

    err = gretl_write_matrix_as_gdt(fname, bs_data, (const char **) S, 
				    NULL);

    gretl_matrix_free(bs_data);
    bs_data = NULL;
    free_strings_array(S, ns);
    *bs_vname = '\0';

    return err;
}



