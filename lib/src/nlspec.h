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

/* Private header for sharing info between nls.c and gmm.c */

#include "libgretl.h" 
#include "gretl_f2c.h"

typedef struct parm_ parm;
typedef struct ocset_ ocset;

struct _nlspec {
    int ci;             /* NLS, MLE or GMM */
    int generr;         /* error from genr */
    int mode;           /* derivatives: numeric or analytic */
    gretlopt opt;       /* can include OPT_V for verbose output; if ci = MLE
			   can also include OPT_H (Hessian) or OPT_R (QML)
			   to control the estimator of the variance matrix 
			*/
    int dv;             /* ID number of dependent variable (NLS) */
    char lhname[VNAMELEN]; /* name of LHS var in criterion function */
    int lhv;            /* ID number of LHS variable in function being
			   minimized or maximized... */
    gretl_matrix *lvec; /* or LHS vector */
    char *nlfunc;       /* string representation of function,
			   expressed in terms of the residuals (NLS,
			   GMM) or the log-likelihood (MLE)
			*/
    int nparam;         /* number of parameters */
    int ncoeff;         /* number of coefficients (allows for vector params) */
    int nvec;           /* number of vector parameters */
    int naux;           /* number of auxiliary commands */
    int ngenrs;         /* number of variable-generating formulae */
    int iters;          /* number of iterations performed */
    int fncount;        /* number of function evaluations (ML, GMM) */
    int grcount;        /* number of gradient evaluations (ML, GMM) */
    int t1;             /* starting observation */
    int t2;             /* ending observation */
    int nobs;           /* number of observations used */
    double crit;        /* criterion (minimand or maximand) */
    double tol;         /* tolerance for stopping iteration */
    parm *params;       /* array of information on function parameters
			   (see the parm_ struct above) */
    doublereal *coeff;  /* coefficient estimates */
    double *hessvec;    /* vech representation of negative inverse of
			   Hessian */
    char **aux;         /* auxiliary commands */
    GENERATOR **genrs;  /* variable-generation pointers */
    double ***Z;        /* pointer to data array */
    DATAINFO *dinfo;    /* pointer to dataset info */
    PRN *prn;           /* printing aparatus */
    ocset *oc;          /* orthogonality info (GMM) */
};

void nlspec_destroy_arrays (nlspec *s);

void oc_set_destroy (ocset *oc);

int nl_calculate_fvec (nlspec *s);

int update_coeff_values (const double *x, nlspec *s);

int check_gmm_requirements (nlspec *spec);

int 
nlspec_add_orthcond (nlspec *s, const char *str,
		     const double **Z, const DATAINFO *pdinfo);

int nlspec_add_weights (nlspec *s, const char *str);

int gmm_add_vcv (MODEL *pmod, nlspec *spec);

int gmm_calculate (nlspec *s, double *fvec, double *jac, PRN *prn);

int gmm_missval_check_etc (nlspec *s);
