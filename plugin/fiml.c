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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include "libgretl.h"
#include "gretl_matrix.h"
#include "gretl_matrix_private.h"

#define FDEBUG 0

typedef struct fiml_system_ fiml_system;

struct fiml_system_ {
    int n;                  /* number of observations per equation */
    int g;                  /* number of (stochastic) equations */
    int gn;                 /* convenience: g * n = number of obs in stacked vectors */
    int totk;               /* total right-hand side vars */
    int nendo;              /* total number of endogenous vars */
    int nexo;               /* total number of exogenous vars */

    double ll;              /* log-likelihood */

    gretl_matrix *uhat;     /* structural-form residuals, all equations */
    gretl_matrix *sigma;    /* cross-equation covariance matrix */
    gretl_matrix *psi;      /* Cholesky decomp of sigma-inverse */
    gretl_matrix *Stmp;     /* workspace */

    gretl_matrix *G;        /* Gamma matrix: coeffs for endogenous vars */
    gretl_matrix *B;        /* coeffs for exogenous and predetermined vars */
    gretl_matrix *Gtmp;     /* workspace */

    gretl_vector *arty;     /* stacked gn-vector: LHS of artificial regression */
    gretl_matrix *artx;     /* stacked matrix of transformed indep vars: RHS */
    gretl_matrix *artb;     /* coefficient vector from artificial regression */
    gretl_matrix *btmp;     /* workspace */

    gretl_matrix *WB1;      /* exog vars times coeffs */
    gretl_matrix *WB2;      /* exog vars times coeffs, times Gamma-inverse */

    gretl_equation_system *sys; /* pointer to "parent" equation system */
};

static void fiml_system_destroy (fiml_system *fsys)
{
    gretl_matrix_free(fsys->uhat);
    gretl_matrix_free(fsys->sigma);
    gretl_matrix_free(fsys->psi);
    gretl_matrix_free(fsys->Stmp);

    gretl_matrix_free(fsys->G);
    gretl_matrix_free(fsys->B);
    gretl_matrix_free(fsys->Gtmp);

    gretl_vector_free(fsys->arty);
    gretl_matrix_free(fsys->artx);
    gretl_vector_free(fsys->artb);
    gretl_vector_free(fsys->btmp);

    gretl_matrix_free(fsys->WB1);
    gretl_matrix_free(fsys->WB2);

    free(fsys);
}

static fiml_system *fiml_system_new (gretl_equation_system *sys)
{
    fiml_system *fsys;
    int *endog_vars;
    int *exog_vars;

    fsys = malloc(sizeof *fsys);
    if (fsys == NULL) return NULL;

    fsys->sys = sys;

    fsys->g = system_n_equations(sys);
    fsys->n = system_n_obs(sys);
    fsys->gn = fsys->g * fsys->n;
    fsys->totk = system_n_indep_vars(sys);

    endog_vars = system_get_endog_vars(sys);
    exog_vars = system_get_instr_vars(sys);

    fsys->nendo = endog_vars[0];
    fsys->nexo = exog_vars[0];

    fsys->ll = 0.0;

    fsys->uhat = NULL;
    fsys->sigma = NULL;
    fsys->psi = NULL;
    fsys->Stmp = NULL;

    fsys->G = NULL;
    fsys->B = NULL;
    fsys->Gtmp = NULL;

    fsys->arty = NULL;
    fsys->artx = NULL;
    fsys->artb = NULL;
    fsys->btmp = NULL;

    fsys->WB1 = NULL;
    fsys->WB2 = NULL;

    fsys->uhat = gretl_matrix_alloc(fsys->n, fsys->g);
    fsys->sigma = gretl_matrix_alloc(fsys->g, fsys->g);
    fsys->psi = gretl_matrix_alloc(fsys->g, fsys->g);
    fsys->Stmp = gretl_matrix_alloc(fsys->g, fsys->g);

    fsys->G = gretl_matrix_alloc(fsys->nendo, fsys->nendo);
    fsys->B = gretl_matrix_alloc(fsys->nexo, fsys->nendo);
    fsys->Gtmp = gretl_matrix_alloc(fsys->nendo, fsys->nendo);

    fsys->arty = gretl_column_vector_alloc(fsys->gn);
    fsys->artx = gretl_matrix_alloc(fsys->gn, fsys->totk);
    fsys->artb = gretl_column_vector_alloc(fsys->totk);
    fsys->btmp = gretl_column_vector_alloc(fsys->totk);

    fsys->WB1 = gretl_matrix_alloc(fsys->n, fsys->nendo);
    fsys->WB2 = gretl_matrix_alloc(fsys->n, fsys->nendo);

    if (fsys->uhat == NULL || fsys->sigma == NULL || fsys->psi == NULL ||
	fsys->Stmp == NULL || fsys->G == NULL || fsys->B == NULL ||
	fsys->arty == NULL || fsys->artx == NULL || fsys->artb == NULL ||
	fsys->WB1 == NULL || fsys->WB2 == NULL || fsys->Gtmp == NULL ||
	fsys->btmp == NULL) {
	fiml_system_destroy(fsys);
	fsys = NULL;
    }	

    return fsys;
}

/* calculate FIML residuals as YG - WB */

static void fiml_form_uhat (fiml_system *fsys, const double **Z, int t1)
{
    const int *enlist = system_get_endog_vars(fsys->sys);
    const int *exlist = system_get_instr_vars(fsys->sys);
    double bij, gij;
    double y, x;
    int i, j, t;

    for (j=0; j<fsys->nendo; j++) {
	for (t=0; t<fsys->n; t++) {
	    y = 0.0;
	    for (i=0; i<fsys->nendo; i++) {
		gij = gretl_matrix_get(fsys->G, i, j);
		y += Z[enlist[i + 1]][t + t1] * gij;
	    }
	    x = 0.0;
	    for (i=0; i<fsys->nexo; i++) {
		bij = gretl_matrix_get(fsys->B, i, j);
		x += Z[exlist[i + 1]][t + t1] * bij;
	    } 
	    gretl_matrix_set(fsys->WB1, t, j, x);
	    if (j < fsys->g) {
		gretl_matrix_set(fsys->uhat, t, j, y - x);
	    }	
	}
    }

#if FDEBUG
    gretl_matrix_print(fsys->uhat, "fiml uhat", NULL);
#endif
}

/* use the full residuals matrix to form the cross-equation covariance
   matrix; then invert this and do a Cholesky decomposition to find
   psi-transpose
*/ 

static int 
fiml_form_sigma_and_psi (fiml_system *fsys, const double **Z, int t1)
{
    int err;

    /* YG - WB */
    fiml_form_uhat(fsys, Z, t1);

    /* Davidson and MacKinnon, ETM, equation (12.81) */

    err = gretl_matrix_multiply_mod(fsys->uhat, GRETL_MOD_TRANSPOSE,
				    fsys->uhat, GRETL_MOD_NONE,
				    fsys->sigma);

    gretl_matrix_divide_by_scalar(fsys->sigma, fsys->n);

#if FDEBUG
    gretl_matrix_print(fsys->sigma, "fiml Sigma", NULL);
#endif

    if (!err) {
	gretl_matrix_copy_values(fsys->psi, fsys->sigma);
	err = gretl_invert_symmetric_matrix(fsys->psi);
    }

#if FDEBUG
    gretl_matrix_print(fsys->psi, "Sigma-inverse", NULL);
#endif

    if (!err) {
	err = gretl_matrix_cholesky_decomp(fsys->psi);
	/* we actually want the transpose of psi (ETM, under eq (12.86) */
	gretl_square_matrix_transpose(fsys->psi);
	gretl_matrix_zero_lower(fsys->psi);
    }

#if FDEBUG
    gretl_matrix_print(fsys->psi, "fiml Psi-transpose", NULL);
#endif

    return err;
}

static void 
fiml_transcribe_results (fiml_system *fsys, const double **Z, int t1,
			 gretl_matrix *sigma)
{
    MODEL *pmod;
    const double *y;
    double u;
    int i, t;

    /* correct uhat and yhat; also correct ESS/SSR and standard error,
       per equation */

    for (i=0; i<fsys->g; i++) {
	pmod = system_get_model(fsys->sys, i);
	y = Z[pmod->list[1]];
	pmod->ess = 0.0;
	for (t=0; t<fsys->n; t++) {
	    u = gretl_matrix_get(fsys->uhat, t, i);
	    pmod->uhat[t + t1] = u;
	    pmod->yhat[t + t1] = y[t + t1] - u;
	    pmod->ess += u * u;
	}
	pmod->sigma = sqrt(pmod->ess / pmod->nobs);
    }

    /* no df correction for pmod->sigma or sigma matrix */
    
    gretl_matrix_copy_values(sigma, fsys->sigma);
}

/* form the LHS stacked vector for the artificial regression */

static void fiml_form_depvar (fiml_system *fsys)
{
    double u, p, x;
    int i, j, k, t;

    k = 0;
    for (i=0; i<fsys->g; i++) { /* loop across equations */
	for (t=0; t<fsys->n; t++) { /* loop across obs */
	    x = 0.0;
	    for (j=0; j<fsys->g; j++) {
		p = gretl_matrix_get(fsys->psi, i, j);
		u = gretl_matrix_get(fsys->uhat, t, j);
		x += p * u;
	    }
	    gretl_vector_set(fsys->arty, k++, x);
	}
    }

#if FDEBUG > 1
    gretl_matrix_print(fsys->arty, "fiml artificial Y", NULL);
#endif
}

static int on_exo_list (const int *exlist, int v)
{
    int i;

    for (i=1; i<=exlist[0]; i++) {
	if (exlist[i] == v) return 1;
    }

    return 0;
}

static int endo_var_number (const int *enlist, int v)
{
    int i;

    for (i=1; i<=enlist[0]; i++) {
	if (enlist[i] == v) return i - 1;
    }

    return -1;
}

/* form the RHS matrix for the artificial regression */

static void 
fiml_form_indepvars (fiml_system *fsys, const double **Z, int t1)
{
    const int *enlist = system_get_endog_vars(fsys->sys);
    const int *exlist = system_get_instr_vars(fsys->sys);
    int i, j, k, t;
    int bigrow, bigcol = 0;
    double p, xjt;

    gretl_matrix_zero(fsys->artx);

    for (i=0; i<fsys->g; i++) { /* loop across equations */
	const int *list = system_get_list(fsys->sys, i);

	for (j=2; j<=list[0]; j++) { /* loop across RHS vars */
	    const double *xj = NULL;
	    int vj = 0;

	    if (on_exo_list(exlist, list[j])) {
		/* the variable is exogenous or predetermined */
		xj = Z[list[j]] + t1;
	    } else {
		/* RHS endogenous variable */
		vj = endo_var_number(enlist, list[j]);
	    }

	    for (t=0; t<fsys->n; t++) { /* loop across obs */
		for (k=0; k<fsys->g; k++) { /* loop across vertical blocks */
		    bigrow = k * fsys->n + t;
		    p = gretl_matrix_get(fsys->psi, k, i);
		    if (p != 0.0) {
			if (xj != NULL) {
			    xjt = xj[t];
			} else {
			    xjt = gretl_matrix_get(fsys->WB2, t, vj);
			}
			gretl_matrix_set(fsys->artx, bigrow, bigcol, xjt * p);
		    }
		}
	    }
	    bigcol++;
	}
    }

#if FDEBUG > 1
    gretl_matrix_print(fsys->artx, "fiml artificial X", NULL);
#endif
}

#if FDEBUG

/* check: set initial residual matrix based on 3SLS */

static void fiml_uhat_init (fiml_system *fsys)
{
    const gretl_matrix *uhat = system_get_uhat(fsys->sys);
    double x;
    int i, t;

    for (i=0; i<fsys->g; i++) {
	for (t=0; t<fsys->n; t++) {
	    x = gretl_matrix_get(uhat, i, t);
	    gretl_matrix_set(fsys->uhat, t, i, x);
	}
    }

    gretl_matrix_print(fsys->uhat, "uhat from 3SLS", NULL);
}

/* test for loglikelihood and gradient calculations: initialize
   coefficients from "known" MLE: see
   http://www.stanford.edu/~clint/bench/kleinfm2.tsp
*/

static void klein_MLE_init (fiml_system *fsys)
{
    MODEL *pmod;
    int i, j, k = 0;
    const double klein_params[] = {
	18.34327218344233, -.2323887662328997,  .3856730901020590, .8018443391624640,
	27.26386576310186, -.8010060259538374, 1.051852141335067, -.1480990630401963,
	5.794287580524262,  .2341176397831136,  .2846766802287325, .2348346571073103
    };

    for (i=0; i<fsys->g; i++) {
	pmod = system_get_model(fsys->sys, i);
	for (j=0; j<pmod->ncoeff; j++) {
	    pmod->coeff[j] = klein_params[k++];
	}
    }
}

#endif

static int 
rhs_var_in_eqn (const gretl_equation_system *sys, int eq, int v)
{
    const int *list = system_get_list(sys, eq);

    if (list != NULL) {
	int i;

	for (i=2; i<=list[0]; i++) {
	    if (list[i] == v) return i;
	}
    }

    return 0;
}

/* initialize Gamma matrix based on 3SLS estimates plus identities */

static void fiml_G_init (fiml_system *fsys, const DATAINFO *pdinfo)
{
    const int *enlist = system_get_endog_vars(fsys->sys);
    const MODEL *pmod;
    int lv, rv;
    int i, j, vi;

    for (j=0; j<fsys->nendo; j++) {
	/* outer loop across columns (equations) */
	lv = enlist[j + 1];
	for (i=0; i<fsys->nendo; i++) {
	    rv = enlist[i + 1];
	    if (j == i) {
		gretl_matrix_set(fsys->G, i, j, 1.0);
	    } else if (j < fsys->g) {
		/* column pertains to stochastic equation */
		vi = rhs_var_in_eqn(fsys->sys, j, rv);
		if (vi > 0) {
		    pmod = system_get_model(fsys->sys, j);
		    gretl_matrix_set(fsys->G, i, j, -pmod->coeff[vi-2]);
		} else {
		    gretl_matrix_set(fsys->G, i, j, 0.0);
		}
	    } else {
		/* column pertains to identity */
		vi = -1 * rhs_var_in_identity(fsys->sys, lv, rv);
		gretl_matrix_set(fsys->G, i, j, vi);
	    }
	}
    }

#if FDEBUG
    printf("Order of columns (and rows):");
    for (i=1; i<=enlist[0]; i++) {
	printf(" %s", pdinfo->varname[enlist[i]]);
    }
    putchar('\n');
    gretl_matrix_print(fsys->G, "fiml Gamma", NULL);
#endif
}

/* update Gamma matrix with revised parameter estimates */

static void fiml_G_update (fiml_system *fsys)
{
    const int *enlist = system_get_endog_vars(fsys->sys);    
    const MODEL *pmod;
    int i, j, vi;

    for (j=0; j<fsys->g; j++) {
	for (i=0; i<fsys->nendo; i++) {
	    if (j != i) {
		vi = rhs_var_in_eqn(fsys->sys, j, enlist[i + 1]);
		if (vi > 0) {
		    pmod = system_get_model(fsys->sys, j);
		    gretl_matrix_set(fsys->G, i, j, -pmod->coeff[vi-2]);
		} 
	    }
	}
    }

#if FDEBUG
    gretl_matrix_print(fsys->G, "fiml Gamma", NULL);
#endif
}

/* initialize B matrix based on 3SLS estimates and identities */

static void fiml_B_init (fiml_system *fsys, const DATAINFO *pdinfo)
{
    const int *enlist = system_get_endog_vars(fsys->sys);
    const int *exlist = system_get_instr_vars(fsys->sys);
    const MODEL *pmod;
    int lv, rv;
    int i, j, vi;

    for (j=0; j<fsys->nendo; j++) {
	lv = enlist[j + 1];
	/* outer loop across columns (equations) */
	for (i=0; i<fsys->nexo; i++) {
	    rv = exlist[i + 1];
	    if (j < fsys->g) {
		/* column pertains to stochastic equation */
		vi = rhs_var_in_eqn(fsys->sys, j, rv);
		if (vi > 0) {
		    pmod = system_get_model(fsys->sys, j);
		    gretl_matrix_set(fsys->B, i, j, pmod->coeff[vi-2]);
		} else {
		    gretl_matrix_set(fsys->B, i, j, 0.0);
		}
	    } else {
		vi = rhs_var_in_identity(fsys->sys, lv, rv);
		gretl_matrix_set(fsys->B, i, j, vi);
	    }
	}
    }

#if FDEBUG
    printf("Order of columns:");
    for (i=1; i<=enlist[0]; i++) {
	printf(" %s", pdinfo->varname[enlist[i]]);
    }
    putchar('\n');
    printf("Order of rows:");
    for (i=1; i<=exlist[0]; i++) {
	printf(" %s", pdinfo->varname[exlist[i]]);
    }
    putchar('\n');
    gretl_matrix_print(fsys->B, "fiml B", NULL);
#endif
}

/* update B matrix with revised parameter estimates */

static void fiml_B_update (fiml_system *fsys)
{
    const int *exlist = system_get_instr_vars(fsys->sys);
    const MODEL *pmod;
    int i, j, vi;

    for (j=0; j<fsys->g; j++) {
	for (i=0; i<fsys->nexo; i++) {
	    vi = rhs_var_in_eqn(fsys->sys, j, exlist[i + 1]);
	    if (vi > 0) {
		pmod = system_get_model(fsys->sys, j);
		gretl_matrix_set(fsys->B, i, j, pmod->coeff[vi-2]);
	    } 
	}
    }

#if FDEBUG
    gretl_matrix_print(fsys->B, "fiml B", NULL);
#endif
}

#define LN_2_PI 1.837877066409345

/* calculate log-likelihood for FIML system */

static int fiml_ll (fiml_system *fsys, const double **Z, int t1)
{
    double tr;
    double ldetG;
    double ldetS;
    int i, j, t;
    int err;

    fsys->ll = 0.0;

    /* form \hat{\Sigma} (ETM, equation 12.81); invert and
       Cholesky-decompose to get \Psi while we're at it 
    */
    err = fiml_form_sigma_and_psi(fsys, Z, t1);
    if (err) {
	fprintf(stderr, "fiml_form_sigma_and_psi: failed\n");
	return err;
    }

    /* note: make copies because the determinant calculations destroy 
       the original matrix */

    gretl_matrix_copy_values(fsys->Gtmp, fsys->G);
    ldetG = gretl_matrix_log_abs_determinant(fsys->Gtmp);
    if (na(ldetG)) {
	return 1;
    }

    gretl_matrix_copy_values(fsys->Stmp, fsys->sigma);
    ldetS = gretl_matrix_log_determinant(fsys->Stmp);
    if (na(ldetS)) {
	return 1;
    }

    /* Davidson and MacKinnon, ETM, equation (12.80) */

    fsys->ll -= (fsys->gn / 2.0) * LN_2_PI;
    fsys->ll -= (fsys->n / 2.0) * ldetS;
    fsys->ll += fsys->n * ldetG;

    gretl_matrix_copy_values(fsys->Stmp, fsys->sigma);
    err = gretl_invert_symmetric_matrix(fsys->Stmp);   
    if (err) {
	return err;
    }

    tr = 0.0;
    for (i=0; i<fsys->g; i++) {
	double epe, eit, ejt, sij;

	for (j=0; j<fsys->g; j++) {
	    epe = 0.0;
	    for (t=0; t<fsys->n; t++) {
		eit = gretl_matrix_get(fsys->uhat, t, i);
		ejt = gretl_matrix_get(fsys->uhat, t, j);
		epe += eit * ejt;
	    }
	    sij = gretl_matrix_get(fsys->Stmp, i, j);
	    tr += sij * epe;
	}
    }

    fsys->ll -= 0.5 * tr;

    return 0;
}

/* calculate instrumented version of endogenous variables, using
   the "restricted reduced form": WB\Gamma^{-1}.  Davidson and
   MacKinnon, ETM, equation (12.70)
*/

static int fiml_endog_rhs (fiml_system *fsys, const double **Z, int t1)
{
    int err;

    gretl_matrix_copy_values(fsys->Gtmp, fsys->G);
    err = gretl_invert_general_matrix(fsys->Gtmp);

#if FDEBUG
    if (!err) {
	gretl_matrix_print(fsys->Gtmp, "G-inverse", NULL);
    } else {
	fprintf(stderr, "inversion of G failed\n");
    }
#endif

    if (!err) {
	gretl_matrix_multiply(fsys->WB1, fsys->Gtmp, fsys->WB2);
    }

    return err;
}

static void copy_estimates_to_btmp (fiml_system *fsys)
{
    const MODEL *pmod;
    int i, j, k = 0;

    for (i=0; i<fsys->g; i++) {
	pmod = system_get_model(fsys->sys, i);
	for (j=0; j<pmod->ncoeff; j++) {
	    gretl_vector_set(fsys->btmp, k++, pmod->coeff[j]);
	}
    }
}

/* adjust parameter estimates based on results of the artificial
   regression 
*/

static int
fiml_adjust_estimates (fiml_system *fsys, const double **Z, int t1,
		       double *instep)
{
    MODEL *pmod;
    double llbak = fsys->ll;
    double minstep = 1.0e-06;
    double step = 4.0;
    int improved = 0;
    int err = 0;

    /* make a backup copy of the current parameter estimates */
    copy_estimates_to_btmp(fsys);

#if FDEBUG
    gretl_matrix_print(fsys->btmp, "parameter estimates", NULL);
    gretl_matrix_print(fsys->artb, "estimated gradients", NULL);
#endif

    while (!improved && !err && step > minstep) {
	double bk, delta;
	int i, j, k = 0;

	/* new coeff = old + gradient * step */
	for (i=0; i<fsys->g; i++) {
	    pmod = system_get_model(fsys->sys, i);
	    for (j=0; j<pmod->ncoeff; j++) {
		bk = gretl_vector_get(fsys->btmp, k);
		delta = gretl_vector_get(fsys->artb, k) * step;
		pmod->coeff[j] = bk + delta;
		k++;
	    }
	}

	/* write the new estimates into the G and B matrices */
	fiml_G_update(fsys);
	fiml_B_update(fsys);

	/* has the likelihood improved? */
	err = fiml_ll(fsys, Z, t1);
	if (!err) {
	    if (fsys->ll > llbak) {
		*instep = step;
		improved = 1;
	    } else {
		step /= 2.0;
	    } 
	}
    }

    return err;
}

/* get standard errors for FIML estimates from the covariance
   matrix of the artificial OLS regression
*/

static int fiml_get_std_errs (fiml_system *fsys)
{
    gretl_matrix *vcv;
    double s2;
    int err;

    vcv = gretl_matrix_alloc(fsys->totk, fsys->totk);
    if (vcv == NULL) {
	return E_ALLOC;
    }

    /* These are "Rhat" standard errors: check Calzolari
       and Panattoni on this */

    err = gretl_matrix_svd_ols(fsys->arty, fsys->artx, fsys->artb, 
			       vcv, NULL, &s2);
    if (!err) {
	MODEL *pmod;
	int i, j, k = 0;

	gretl_matrix_divide_by_scalar(vcv, s2);

	for (i=0; i<fsys->g; i++) {
	    pmod = system_get_model(fsys->sys, i);
	    for (j=0; j<pmod->ncoeff; j++) {
		pmod->sderr[j] = sqrt(gretl_matrix_get(vcv, k, k));
		k++;
	    }
	}
    }

    /* fixme: further use for vcv? */
    gretl_matrix_free(vcv);

    return err;
}

/* Driver function for FIML as described in Davidson and MacKinnon,
   ETM, chap 12, section 5.
*/

#define FIML_ITER_MAX 250

int fiml_driver (gretl_equation_system *sys, const double **Z, 
		 gretl_matrix *sigma, const DATAINFO *pdinfo, 
		 PRN *prn)
{
    fiml_system *fsys;
    double llbak;
    double crit = 1.0;
    double tol = 1.0e-12; /* over-ambitious? */
    int iters = 0;
    int err = 0;

    fsys = fiml_system_new(sys);
    if (fsys == NULL) {
	return E_ALLOC;
    }

    pputs(prn, "\n*** FIML: experimental, work in progress ***\n\n");

#if FDEBUG
# ifdef KLEIN_INIT
    /* check ll calc. and gradients: 
       try starting from "known" MLE values for Klein model 1 */
    klein_MLE_init(fsys);
# else
    /* check uhat calculation: set intial uhat based from 3SLS */
    fiml_uhat_init(fsys);
# endif
#endif

    /* intialize Gamma coefficient matrix */
    fiml_G_init(fsys, pdinfo);

    /* intialize B coefficient matrix */
    fiml_B_init(fsys, pdinfo);

    /* initial loglikelihood */
    err = fiml_ll(fsys, Z, pdinfo->t1);
    if (err) {
	fprintf(stderr, "fiml_ll: failed\n");
	goto bailout;
    } else {
	llbak = fsys->ll;
	pprintf(prn, "*** initial ll = %.8g\n", fsys->ll);
    }    

    while (crit > tol && iters < FIML_ITER_MAX) {
	double step;

	/* form LHS vector for artificial regression */
	fiml_form_depvar(fsys);

	/* instrument the RHS endog vars */
	err = fiml_endog_rhs(fsys, Z, pdinfo->t1);
	if (err) {
	    fprintf(stderr, "fiml_endog_rhs: failed\n");
	    break;
	}	

	/* form RHS matrix for artificial regression */
	fiml_form_indepvars(fsys, Z, pdinfo->t1);

	/* run artificial regression (ETM, equation 12.86) */
	err = gretl_matrix_ols(fsys->arty, fsys->artx, fsys->artb, 
			       NULL, NULL, NULL);
	if (err) {
	    fprintf(stderr, "gretl_matrix_ols: failed\n");
	    break;
	}

	/* adjust param estimates based on gradients in fsys->artb */
	err = fiml_adjust_estimates(fsys, Z, pdinfo->t1, &step);
	if (err) {
	    break;
	}

	pprintf(prn, "*** iteration %3d: step = %g, ll = %.8g\n", iters + 1, 
		step, fsys->ll);

	crit = fsys->ll - llbak;
	llbak = fsys->ll;

	iters++;
    }

    if (crit > tol) {
	pprintf(prn, "\nTolerance of %g was not met\n", tol);
	err = 1;
    } else {
	pprintf(prn, "\nTolerance %g, criterion %g\n", tol, crit);
    }

    if (!err) {
	err = fiml_get_std_errs(fsys);
    }

    /* write the results into the parent system */
    fiml_transcribe_results(fsys, Z, pdinfo->t1, sigma);

 bailout:
    
    /* clean up */
    fiml_system_destroy(fsys);

    return err;
}



    
	

    
    
    

    
    
