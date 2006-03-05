/*
 *  Copyright (c) by Ramu Ramanathan and Allin Cottrell
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

/* discrete.c for gretl: logit and probit models */

#include "libgretl.h"

#define TINY 1.0e-13

static int neginv (const double *xpx, double *diag, int nv);
static int cholesky_decomp (double *xpx, int nv);

#define LPDEBUG 0

static double logit (double x)
{
    double l = 1.0 / (1.0 + exp(-x));

#if LPDEBUG
    if (x > 40 || x < -40) {
	fprintf(stderr, "x = %g, logit(x) = %g\n", x, l);
    }
#endif

    return l;
}

static double logit_pdf (double x)
{
    double l, z = exp(-x);

    l = z / ((1.0 + z) * (1.0 + z));

#if LPDEBUG
    if (x > 40 || x < -40) {
	fprintf(stderr, "x = %g, logit_pdf(x) = %g\n", x, l);
    }
#endif

    if (x < 0 && isnan(l)) {
#if LPDEBUG
	fprintf(stderr, "logit_pdf(): x = %g, forcing l to zero\n", x);
#endif
	l = 0;
    }

    return l;
}

static void Lr_chisq (MODEL *pmod, double **Z)
{
    int t, zeros, ones = 0, m = pmod->nobs;
    double Lr, chisq;
    
    for (t=pmod->t1; t<=pmod->t2; t++) {
	if (Z[pmod->list[1]][t] == 1.0) {
	    ones++;
	} 
    }

    zeros = m - ones;

    Lr = (double) ones * log((double) ones / (double) m);
    Lr += (double) zeros * log((double) zeros /(double) m);

    chisq = 2.0 * (pmod->lnL - Lr);
    gretl_model_set_double(pmod, "chisq", chisq);
    
    /* McFadden pseudo-R^2 */
    pmod->rsq = 1.0 - pmod->lnL / Lr;
    pmod->adjrsq = NADBL;
}

static double 
logit_probit_llhood (const double *y, const MODEL *pmod, int opt)
{
    double q, lnL = 0.0;
    int t;

    for (t=pmod->t1; t<=pmod->t2; t++) {
	if (na(pmod->yhat[t])) {
	    continue;
	}
	q = 2.0 * y[t] - 1.0;
	if (opt == LOGIT) {
	    lnL += log(logit(q * pmod->yhat[t]));
	} else {
	    lnL += log(normal_cdf(q * pmod->yhat[t]));
	}
    }

    return lnL;
}

static int add_slopes_to_model (MODEL *pmod, double fbx)
{
    double *slopes;
    size_t ssize = pmod->ncoeff * sizeof *slopes;
    int i;

    slopes = malloc(ssize);

    if (slopes == NULL) {
	return 1;
    }

    for (i=0; i<pmod->ncoeff; i++) {
	if (pmod->list[i+2] == 0) {
	    continue;
	}
	slopes[i] = pmod->coeff[i] * fbx;
    }

    if (gretl_model_set_data(pmod, "slopes", slopes, ssize)) {
	free(slopes);
	return 1;
    }

    return 0;
}

static double *hess_wts (MODEL *pmod, const double **Z, int opt) 
{
    int t, tw, n = pmod->t2 - pmod->t1 + 1;
    double q, bx, xx;
    double *w;

    w = malloc(n * sizeof *w);
    if (w == NULL) {
	return NULL;
    }

    for (t=pmod->t1; t<=pmod->t2; t++) {
	tw = t - pmod->t1;
	if (model_missing(pmod, t)) {
	    w[tw] = NADBL;
	    continue;
	}

	q = 2.0 * Z[pmod->list[1]][t] - 1.0;
	bx = pmod->yhat[t];

	if (opt == LOGIT) {
	    w[tw] = -1.0 * logit(bx) * (1.0 - logit(bx));
	} else {
	    xx = (q * normal_pdf(q * bx)) / normal_cdf(q * bx);
	    w[tw] = -xx * (xx + bx);
	}
    }

    return w;
}

static double *hessian (MODEL *pmod, const double **Z, int opt) 
{
    int i, j, li, lj, m, t;
    const int l0 = pmod->list[0];
    double xx, *wt, *xpx;

    i = l0 - 1;
    m = i * (i + 1) / 2;

    xpx = malloc(m * sizeof *xpx);
    if (xpx == NULL) {
	return NULL;
    }

    wt = hess_wts(pmod, Z, opt);
    if (wt == NULL) {
	free(xpx);
	return NULL;
    }

    m = 0;
    for (i=2; i<=l0; i++) {
	li = pmod->list[i];
	for (j=i; j<=l0; j++) {
	    lj = pmod->list[j];
	    xx = 0.0;
	    for (t=pmod->t1; t<=pmod->t2; t++) {
		if (!model_missing(pmod, t)) {
		    xx += wt[t - pmod->t1] * Z[li][t] * Z[lj][t];
		}
	    }
	    if (floateq(xx, 0.0) && li == lj) {
		free(xpx);
		free(wt);
		return NULL;
	    }
	    xpx[m++] = -xx;
	}
    }

    free(wt);

    return xpx; 
}

static int 
compute_QML_vcv (MODEL *pmod, const double **Z)
{
    gretl_matrix *G = NULL;
    gretl_matrix *H = NULL;
    gretl_matrix *S = NULL;
    gretl_matrix *tmp = NULL;

    const double *y = Z[pmod->list[1]];
    const double *xi;

    double x;
    int k = pmod->ncoeff;
    int T = pmod->nobs;
    int i, j, t, gt, err = 0;

    G = gretl_matrix_alloc(k, T);
    H = gretl_matrix_alloc(k, k);
    S = gretl_matrix_alloc(k, k);
    tmp = gretl_matrix_alloc(k, k);

    if (G == NULL || H == NULL || S == NULL || tmp == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    /* compute gradient or score matrix */
    for (i=0; i<k; i++) {
	xi = Z[pmod->list[i+2]];
	gt = 0;
	for (t=pmod->t1; t<=pmod->t2; t++) {
	    if (!na(pmod->yhat[t])) {
		/* score for obs t */
		if (pmod->ci == LOGIT) {
		    x = (y[t] - logit(pmod->yhat[t])) * xi[t];
		} else {
		    double c = normal_cdf(pmod->yhat[t]);

		    x = (y[t] - c) * normal_pdf(pmod->yhat[t]) * xi[t] /
			(c * (1.0 - c));
		}
		gretl_matrix_set(G, i, gt++, x);
	    }
	}
    }

    /* transcribe Hessian from model */
    for (i=0; i<k; i++) {
	for (j=0; j<=i; j++) {
	    x = pmod->xpx[ijton(i, j, k)];
	    gretl_matrix_set(H, i, j, x);
	    if (i != j) {
		gretl_matrix_set(H, j, i, x);
	    }
	}
    }   

    gretl_invert_symmetric_matrix(H);
    gretl_matrix_multiply_by_scalar(H, -1.0);

    /* form S = GG' */
    gretl_matrix_multiply_mod(G, GRETL_MOD_NONE,
			      G, GRETL_MOD_TRANSPOSE,
			      S);

    /* form sandwich: H^{-1} S H^{-1} */
    gretl_matrix_multiply(H, S, tmp);
    gretl_matrix_multiply(tmp, H, S);

    pmod->vcv = malloc((k * (k + 1) / 2) * sizeof *pmod->vcv);
    if (pmod->vcv == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    /* set VCV and std err values */
    for (i=0; i<k; i++) {
	for (j=0; j<=i; j++) {
	    x = gretl_matrix_get(S, i, j);
	    pmod->vcv[ijton(i, j, k)] = x;
	    if (i == j) {
		pmod->sderr[i] = sqrt(x);
	    }
	}
    }

    gretl_model_set_int(pmod, "robust", 1);

 bailout:

    gretl_matrix_free(G);
    gretl_matrix_free(H);
    gretl_matrix_free(S);
    gretl_matrix_free(tmp);

    return err;
}

/**
 * logit_probit:
 * @list: dependent variable plus list of regressors.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @ci: command index: if = %LOGIT, perform logit regression, otherwise
 * perform probit regression.
 * @opt: if includes %OPT_R form robust (QML) estimates of standard
 * errors and covariance matrix.
 *
 * Computes estimates of the discrete model specified by @list,
 * using an estimator determined by the value of @ci.  Uses the
 * BRMR auxiliary regression; see Davidson and MacKinnon.
 * 
 * Returns: a #MODEL struct, containing the estimates.
 */

MODEL logit_probit (const int *list, double ***pZ, DATAINFO *pdinfo, 
		    int ci, gretlopt opt)
{
    int i, t, v, depvar = list[1];
    int nx = list[0] - 1;
    int oldt1 = pdinfo->t1;
    int oldt2 = pdinfo->t2;
    int oldv = pdinfo->v;
    int itermax = 250;
    double tol = 1.0e-9; /* ? 1.0e-9 */
    int *dmodlist = NULL;
    int *act_pred = NULL;
    MODEL dmod;
    int dummy;
    double xx, zz, fx, Fx, fbx;
    double lldiff, llbak;
    int iters;
    double *xbar = NULL;
    double *beta = NULL;

    gretl_model_init(&dmod);
    
    /* check whether depvar is binary */
    dummy = gretl_isdummy(pdinfo->t1, pdinfo->t2, (*pZ)[depvar]);
    if (!dummy) {
	dmod.errcode = E_UNSPEC;
	sprintf(gretl_errmsg, _("The dependent variable '%s' is not a 0/1 "
				"variable.\n"), pdinfo->varname[depvar]);
	return dmod;
    }

    dmodlist = gretl_list_new(list[0]);
    if (dmodlist == NULL) {
	dmod.errcode = E_ALLOC;
	return dmod;
    } 

    /* allocate space for means of indep vars */
    xbar = malloc(nx * sizeof *xbar);
    if (xbar == NULL) {
	dmod.errcode = E_ALLOC;
	return dmod;
    }

    /* space for coefficient vector */
    beta = malloc(nx * sizeof *beta);
    if (beta == NULL) {
	dmod.errcode = E_ALLOC;
	goto bailout;
    }

    /* space for actual/predicted matrix */
    act_pred = malloc(4 * sizeof *act_pred);
    if (act_pred != NULL) {
	for (i=0; i<4; i++) {
	    act_pred[i] = 0;
	}
    }

    /* make room for full set of transformed vars */
    if (dataset_add_series(list[0], pZ, pdinfo)) {
	dmod.errcode = E_ALLOC;
	return dmod;
    }

    v = oldv; /* the first newly created variable */

    varlist_adjust_sample(list, &pdinfo->t1, &pdinfo->t2, 
			  (const double **) *pZ);

    dmod = lsq(list, pZ, pdinfo, OLS, OPT_A);
    if (dmod.list[0] != list[0]) {
	dmod.errcode = E_DATA;
	goto bailout;
    }

    for (i=2; i<=list[0]; i++) {
	dmodlist[i] = v + i - 1;
	beta[i-2] = dmod.coeff[i-2];
	xbar[i-2] = 0.0;
	for (t=dmod.t1; t<=dmod.t2; t++) {
	    if (!model_missing(&dmod, t)) {
		xbar[i-2] += (*pZ)[list[i]][t];
	    }
	}
	xbar[i-2] /= dmod.nobs;
    }

    dmodlist[0] = list[0];
    dmodlist[1] = v; /* dep var is the newly created one */

    llbak = -1.0e9;
    
    /* BRMR, Davidson and MacKinnon, ETM, p. 461 */

    for (iters=0; iters<itermax; iters++) {
	double wt;

	/* construct BRMR dataset */
	for (t=dmod.t1; t<=dmod.t2; t++) {
	    xx = dmod.yhat[t];
	    if (na(xx)) {
		(*pZ)[v][t] = NADBL;
	    } else {
		if (ci == LOGIT) {
		    fx = logit_pdf(xx);
		    Fx = logit(xx);
		} else {
		    fx = normal_pdf(xx);
		    Fx = normal_cdf(xx);
		}

		if (Fx < 1.0) {
		    wt = 1.0 / sqrt(Fx * (1.0 - Fx));
		} else {
		    wt = 0.0;
		}

		(*pZ)[v][t] = wt * ((*pZ)[depvar][t] - Fx);
#if LPDEBUG
		fprintf(stderr, "creating Z[%d][%d] = %g * (%g - %g) = %g\n",
			v, t, wt, (*pZ)[depvar][t], Fx, (*pZ)[v][t]);
#endif
		wt *= fx;
		for (i=2; i<=dmodlist[0]; i++) {
		    (*pZ)[dmodlist[i]][t] = wt * (*pZ)[list[i]][t];
#if LPDEBUG
		    fprintf(stderr, "creating Z[%d][%d] = %g * %g = %g\n",
			    dmodlist[i], t, wt, (*pZ)[list[i]][t], 
			    (*pZ)[dmodlist[i]][t]);
#endif
		}
	    }
	}

	dmod.lnL = logit_probit_llhood((*pZ)[depvar], &dmod, ci);

	lldiff = fabs(dmod.lnL - llbak);
	if (lldiff < tol) {
	    break; 
	}

#if LPDEBUG
	fprintf(stderr, "\n*** iteration %d: log-likelihood = %g\n", iters, dmod.lnL);
#endif

	llbak = dmod.lnL;
	clear_model(&dmod);
	dmod = lsq(dmodlist, pZ, pdinfo, OLS, OPT_A);
	if (dmod.errcode) {
	    fprintf(stderr, "logit_probit: dmod errcode = %d\n", dmod.errcode);
	    if (iters > 0) {
		dmod.errcode = E_NOCONV;
	    }
#if LPDEBUG
	    fputs("BRMR dataset\n", stderr);
	    for (t=dmod.t1; t<=dmod.t2; t++) {
		for (i=1; i<=dmodlist[0]; i++) {
		    fprintf(stderr, "%g ", (*pZ)[dmodlist[i]][t]);
		}
		fputc('\n', stderr);
	    }
#endif
	    goto bailout;
	}

	/* update coefficient estimates: FIXME stepsize? */
	for (i=0; i<dmod.ncoeff; i++) {
	    if (isnan(dmod.coeff[i])) {
		fprintf(stderr, "BRMR produced NaN coeff\n");
		dmod.errcode = E_NOCONV;
		goto bailout;
	    }
	    beta[i] += dmod.coeff[i];
	}

	/* calculate yhat */
	for (t=dmod.t1; t<=dmod.t2; t++) {
	    if (na(dmod.yhat[t])) {
		continue;
	    }
	    dmod.yhat[t] = 0.0;
	    for (i=0; i<dmod.ncoeff; i++) {
		dmod.yhat[t] += beta[i] * (*pZ)[list[i+2]][t];
	    }
	}
    }

    if (lldiff > tol) {
	dmod.errcode = E_NOCONV;
	goto bailout;
    }

    gretl_model_set_int(&dmod, "iters", iters);

    /* re-establish original list in model */
    for (i=1; i<=list[0]; i++) {
	dmod.list[i] = list[i];
    }

    /* transcribe coefficients */
    for (i=0; i<dmod.ncoeff; i++) {
	dmod.coeff[i] = beta[i];
    }

    dmod.lnL = logit_probit_llhood((*pZ)[depvar], &dmod, ci);
    Lr_chisq(&dmod, *pZ);
    dmod.ci = ci;

    /* calculate standard errors etc using the Hessian */

    if (dmod.vcv != NULL) {
	free(dmod.vcv);
	dmod.vcv = NULL;
    }

    if (dmod.xpx != NULL) {
	free(dmod.xpx);
    }

    dmod.xpx = hessian(&dmod, (const double **) *pZ, ci);
    if (dmod.xpx == NULL) {
	dmod.errcode = E_ALLOC;
	strcpy(gretl_errmsg, _("Failed to construct Hessian matrix"));
	goto bailout;
    } 

    if (opt & OPT_R) {
	dmod.errcode = compute_QML_vcv(&dmod, (const double **) *pZ);
	if (dmod.errcode) {
	    goto bailout;
	}
    } else {    
	/* obtain negative inverse of Hessian */
	double *xpx = NULL, *diag = NULL;

	cholesky_decomp(dmod.xpx, dmod.ncoeff); 
	diag = malloc(dmod.ncoeff * sizeof *diag); 
	if (diag == NULL) {
	    dmod.errcode = E_ALLOC;
	    goto bailout;
	}

	xpx = copyvec(dmod.xpx, dmod.ncoeff * (dmod.ncoeff + 1) / 2);
	if (xpx == NULL) {
	    free(diag);
	    dmod.errcode = E_ALLOC;
	    goto bailout;
	}

	neginv(xpx, diag, dmod.ncoeff);

	for (i=0; i<dmod.ncoeff; i++) {
	    dmod.sderr[i] = sqrt(diag[i]);
	}

	free(diag);
	free(xpx);
    }

    /* apparatus for calculating slopes at means */
    xx = 0.0;
    for (i=0; i<dmod.ncoeff; i++) {
	xx += dmod.coeff[i] * xbar[i];
    }

    if (ci == LOGIT) {
	fbx = logit_pdf(xx);
#if LPDEBUG
	fprintf(stderr, "xx = %.8g, fbx = %.8g\n", xx, fbx);
#endif
    } else {
	fbx = normal_pdf(xx);
    }

    if (add_slopes_to_model(&dmod, fbx)) {
	dmod.errcode = E_ALLOC;
	goto bailout;
    }

    /* calculate additional statistics */
    xx = 0.0;
    for (t=dmod.t1; t<=dmod.t2; t++) {
	double xb = dmod.yhat[t];

	if (model_missing(&dmod, t)) {
	    continue;
	}
	zz = (*pZ)[depvar][t];
	xx += zz;

	if (act_pred != NULL) {
	    i = 2 * (floateq(zz, 1.0)) + (xb > 0.0);
	    act_pred[i] += 1;
	}

	if (dmod.ci == LOGIT) {
	   dmod.yhat[t] = exp(xb) / (1.0 + exp(xb)); 
	} else {
	   dmod.yhat[t] = normal_cdf(xb); 
	}
	dmod.uhat[t] = zz - dmod.yhat[t];
    }

    xx /= dmod.nobs;
    dmod.ybar = xx;
    dmod.sdy = fbx;

    if (act_pred != NULL) {
	gretl_model_set_data(&dmod, "discrete_act_pred", act_pred, 
			     4 * sizeof *act_pred);
    }

    mle_criteria(&dmod, 0);

    dmod.ID = model_count_plus();

 bailout:

    free(xbar);
    free(dmodlist);
    free(beta);

    pdinfo->t1 = oldt1;
    pdinfo->t2 = oldt2;

    dataset_drop_last_variables(pdinfo->v - oldv, pZ, pdinfo);

    return dmod;
}

/* Solves for diagonal elements of X'X inverse matrix.
   X'X must be Cholesky-decomposed already.
*/

static int neginv (const double *xpx, double *diag, int nv)
{
    int kk, l, m, k, i, j;
    const int nxpx = nv * (nv + 1) / 2;
    double d, e, *tmp;

    tmp = malloc((nv + 1) * sizeof *tmp);
    if (tmp == NULL) {
	return 1;
    }

    for (i=0; i<=nv; i++) {
	tmp[i] = 0.0;
    }

    kk = 0;

    for (l=1; l<=nv-1; l++) {
        d = xpx[kk];
        tmp[l] = d;
        e = d * d;
        m = 0;
        if (l > 1) {
	    for (j=1; j<=l-1; j++) {
		m += nv - j;
	    }
	}
        for (i=l+1; i<=nv; i++) {
            d = 0.0;
            k = i + m - 1;
            for (j=l; j<=i-1; j++) {
                d += tmp[j] * xpx[k];
                k += nv - j;
            }
            d = -d * xpx[k];
            tmp[i] = d;
            e += d * d;
        }
        kk += nv + 1 - l;
        diag[l-1] = e;
    }

    diag[nv - 1] = xpx[nxpx - 1] * xpx[nxpx - 1];

    free(tmp);

    return 0;
}

/* Cholesky decomposition of X'X */

static int cholesky_decomp (double *xpx, int nv)
{
    int i, j, k, kk, l, jm1;
    double e, d, d1, test, xx;

    e = 1.0 / sqrt(xpx[0]);
    xpx[0] = e;

    for (i=1; i<nv; i++) {
	xpx[i] *= e;
    }

    kk = nv;

    for (j=2; j<=nv; j++) {
	/* diagonal elements */
        d = d1 = 0.0;
        k = jm1 = j - 1;
        for (l=1; l<=jm1; l++) {
            xx = xpx[k];
            d += xx * xx;
            k += nv-l;
        }
        test = xpx[kk] - d;
        if (test / xpx[kk] < TINY) {
	    return 1;
	}
        e = 1 / sqrt(test);
        xpx[kk] = e;
        /* off-diagonal elements */
        for (i=j+1; i<=nv; i++) {
            kk++;
            d = 0.0;
            k = j - 1;
            for (l=1; l<=jm1; l++) {
                d += xpx[k] * xpx[k-j+i];
                k += nv - l;
            }
            xpx[kk] = (xpx[kk] - d) * e;
        }
        kk++;
    }

    return 0; 
}

/* logistic model: doesn't exactly belong here but it seems like
   the most suitable place for it 
*/

int logistic_ymax_lmax (const double *y, const DATAINFO *pdinfo,
			double *ymax, double *lmax)
{
    int t;

    *ymax = 0.0;

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	if (na(y[t])) {
	    continue;
	}
	if (y[t] <= 0.0) {
	    strcpy(gretl_errmsg, _("Illegal non-positive value of the "
				   "dependent variable"));
	    return 1;
	}
	if (y[t] > *ymax) {
	    *ymax = y[t];
	}
    }

    if (*ymax < 1.0) {
	*lmax = 1.0;
    } else if (*ymax < 100.0) {
	*lmax = 100.0;
    } else {
	*lmax = 1.1 * *ymax;
    }
	    
    return 0;
}

static double real_get_lmax (const double *y, const DATAINFO *pdinfo,
			     const char *lmstr)
{
    double lmax, ymax = 0.0;
    int err;

    err = logistic_ymax_lmax(y, pdinfo, &ymax, &lmax);

    if (err) {
	return NADBL;
    }

    if (lmstr != NULL && *lmstr != '\0') {
	lmax = atof(lmstr + 5);
	if (lmax <= ymax) {
	    gretl_errmsg_set(_("Invalid value for the maximum of the "
			       "dependent variable"));
	    lmax = NADBL;
	}
    }	
	    
    return lmax;
}

static int make_logistic_depvar (double ***pZ, DATAINFO *pdinfo, 
				 int dv, double lmax)
{
    int t, v = pdinfo->v;

    if (dataset_add_series(1, pZ, pdinfo)) {
	return 1;
    }

    for (t=0; t<pdinfo->n; t++) {
	double p = (*pZ)[dv][t];

	if (na(p)) {
	    (*pZ)[v][t] = NADBL;
	} else {
	    (*pZ)[v][t] = log(p / (lmax - p));
	}
    }

    return 0;
}

static int rewrite_logistic_stats (const double **Z, const DATAINFO *pdinfo,
				   MODEL *pmod, int dv, double lmax)
{
    int t;
    double x;

    pmod->ybar = gretl_mean(pmod->t1, pmod->t2, Z[dv]);
    pmod->sdy = gretl_stddev(pmod->t1, pmod->t2, Z[dv]);

    /* make the VCV matrix before messing with the model stats */
    makevcv(pmod);

    pmod->ess = 0.0;
    for (t=0; t<pdinfo->n; t++) {
	x = pmod->yhat[t];
	if (na(x)) {
	    continue;
	}
	pmod->yhat[t] = lmax / (1.0 + exp(-x));
	pmod->uhat[t] = Z[dv][t] - pmod->yhat[t];
	pmod->ess += pmod->uhat[t] * pmod->uhat[t];
    }

    pmod->sigma = sqrt(pmod->ess / pmod->dfd);

    pmod->tss = 0.0;
    for (t=pmod->t1; t<=pmod->t2; t++) {
	x = Z[dv][t];
	if (!na(x)) {
	    pmod->tss += (x - pmod->ybar) * (x - pmod->ybar);
	}
    }

    pmod->fstt = pmod->dfd * (pmod->tss - pmod->ess) / (pmod->dfn * pmod->ess);

    pmod->rsq = pmod->adjrsq = NADBL;

    if (pmod->tss > 0) {
	pmod->rsq = 1.0 - (pmod->ess / pmod->tss);
	if (pmod->dfd > 0) {
	    double den = pmod->tss * pmod->dfd;

	    pmod->adjrsq = 1.0 - (pmod->ess * (pmod->nobs - 1) / den);
	}
    }

    pmod->list[1] = dv;
    gretl_model_set_double(pmod, "lmax", lmax);
    pmod->ci = LOGISTIC;
    ls_criteria(pmod);

    return 0;
}

/**
 * logistic_model:
 * @list: dependent variable plus list of regressors.
 * @pZ: pointer to data matrix.
 * @pdinfo: information on the data set.
 * @param: may contain "ymax=value" for user setting of the
 * asymptotic maximum of the dependent variable.
 *
 * Estimate the model given in @list using the logistic transformation
 * of the dependent variable.
 * 
 * Returns: a #MODEL struct, containing the estimates.
 */

MODEL logistic_model (const int *list, double ***pZ, DATAINFO *pdinfo,
		      const char *param) 
{
    double lmax;
    int *llist = NULL;
    int dv = list[1];
    MODEL lmod;

    gretl_model_init(&lmod); 

    llist = gretl_list_copy(list);
    if (llist == NULL) {
	lmod.errcode = E_ALLOC;
	return lmod;
    }

    lmax = real_get_lmax((*pZ)[dv], pdinfo, param);

    if (na(lmax)) {
	lmod.errcode = E_DATA;
    } else if (lmax == 0.0) {
	lmod.errcode = E_CANCEL;
    }

    if (!lmod.errcode) {
	if (make_logistic_depvar(pZ, pdinfo, dv, lmax)) {
	    lmod.errcode = E_ALLOC;	
	}
    }

    if (lmod.errcode) {
	free(llist);
	return lmod;
    }

    llist[1] = pdinfo->v - 1;

    lmod = lsq(llist, pZ, pdinfo, OLS, OPT_A);
    if (!lmod.errcode) {
	rewrite_logistic_stats((const double **) *pZ, pdinfo, &lmod,
			       dv, lmax);
	set_model_id(&lmod);
    }

    dataset_drop_last_variables(1, pZ, pdinfo);
    free(llist);
    
    return lmod;
}
