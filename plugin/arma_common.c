#define MAX_ARMA_ORDER 6
#define MAX_ARIMA_DIFF 2

enum {
    ARMA_IFC   = 1 << 0, /* specification includes a constant */
    ARMA_SEAS  = 1 << 1, /* includes seasonal component */
    ARMA_DSPEC = 1 << 2, /* input list includes differences */
    ARMA_X12A  = 1 << 3  /* using X-12-ARIMA to generate estimates */
} ArmaFlags;

struct arma_info {
    int yno;      /* ID of dependent variable */
    char flags;   /* from ArmaFlags */
    int p;        /* non-seasonal AR order */
    int d;        /* non-seasonal difference */
    int q;        /* non-seasonal MA order */
    int P;        /* seasonal AR order */
    int D;        /* seasonal difference */
    int Q;        /* seasonal MA order */
    int maxlag;   /* longest lag in model */
    int r;        /* number of other regressors (ARMAX) */
    int nc;       /* total number of coefficients */
    int t1;       /* starting observation */
    int t2;       /* ending observation */
    int pd;       /* periodicity of data */
    int T;        /* full length of data series */
    double *dy;   /* differenced dependent variable */
};

#define arma_has_const(a)      ((a)->flags & ARMA_IFC)
#define arma_has_seasonal(a)   ((a)->flags & ARMA_SEAS)
#define arma_is_arima(a)       ((a)->flags & ARMA_DSPEC)
#define arma_by_x12a(a)        ((a)->flags & ARMA_X12A)

#define set_arma_has_const(a)     ((a)->flags |= ARMA_IFC)
#define set_arma_has_seasonal(a)  ((a)->flags |= ARMA_SEAS)
#define set_arma_is_arima(a)      ((a)->flags |= ARMA_DSPEC)
#define unset_arma_is_arima(a)    ((a)->flags &= ~ARMA_DSPEC)

static void 
arma_info_init (struct arma_info *ainfo, char flags, const DATAINFO *pdinfo)
{
    ainfo->yno = 0;
    ainfo->flags = flags;

    ainfo->p = 0;
    ainfo->d = 0;
    ainfo->q = 0;
    ainfo->P = 0;
    ainfo->D = 0;
    ainfo->Q = 0; 

    ainfo->maxlag = 0;
    ainfo->r = 0;
    ainfo->nc = 0;

    ainfo->t1 = pdinfo->t1;
    ainfo->t2 = pdinfo->t2;
    ainfo->pd = pdinfo->pd;
    ainfo->T = pdinfo->n;

    ainfo->dy = NULL;
}

static int arma_list_y_position (struct arma_info *ainfo)
{
    int ypos;

    if (arma_is_arima(ainfo)) {
	ypos = (arma_has_seasonal(ainfo))? 9 : 5;
    } else {
	ypos = (arma_has_seasonal(ainfo))? 7 : 4;
    }

    return ypos;
}

#if 0

static void arma_R2_and_F (MODEL *pmod, const double *y)
{
    int t;

    pmod->tss = 0.0;

    for (t=pmod->t1; t<=pmod->t2; t++) {
	pmod->tss += (y[t] - pmod->ybar) * (y[t] - pmod->ybar);
    }

    if (!pmod->ifc) {
	double syh2 = 0.0;

	for (t=pmod->t1; t<=pmod->t2; t++) {
	    syh2 += pmod->yhat[t] * pmod->yhat[t];
	}
	pmod->fstt = pmod->dfd * syh2 / (pmod->dfn * pmod->ess);
    } else if (pmod->tss > pmod->ess) {
	pmod->fstt = pmod->dfd * (pmod->tss - pmod->ess) / (pmod->dfn * pmod->ess);
    } 

    if (!pmod->ifc) {
	double r2 = gretl_corr_rsq(pmod->t1, pmod->t2, y, pmod->yhat);

	pmod->rsq = r2;
	pmod->adjrsq = 1.0 - ((1.0 - r2) * (pmod->nobs - 1.0) / pmod->dfd);
    } else if (pmod->tss > 0) {
	pmod->rsq = 1.0 - (pmod->ess / pmod->tss);
	if (pmod->dfd > 0) {
	    double den = pmod->tss * pmod->dfd;

	    pmod->adjrsq = 1.0 - (pmod->ess * (pmod->nobs - 1) / den);
	}
    }
}

#endif

#define INT_DEBUG 1

static int arima_integrate (double *dx, const double *dxreal, double x0,
			    int t1, int t2, int d, int D, int s)
{
    double *x;
    int t;

#if INT_DEBUG
    fprintf(stderr, "arima_integrate: t1=%d, t2=%d, d=%d, D=%d, s=%d\n",
	    t1, t2, d, D, s);
#endif

    x = malloc((t2 + 1) * sizeof *x);
    if (x == NULL) {
	return E_ALLOC;
    }

    for (t=0; t<=t2; t++) {
	x[t] = 0.0;
    }    

    for (t=0; t<t1; t++) {
	dx[t] = dxreal[t];
    }

    x[t1 - 1] = x0;

    for (t=t1; t<=t2; t++) {
	x[t] = x[t-1];
	if (d > 0) {
	    x[t] += dx[t];
	} 
	if (d > 1) {
	    x[t] += dx[t];
	    x[t] -= dx[t-1];
	}
	if (D > 0) {
	    x[t] += dx[t - s + 1];
	    if (d > 0) {
		x[t] -= dx[t - s];
	    }
	    if (d > 1) {
		x[t] -= dx[t - s + 1];
		x[t] += dx[t - 2*s + 1];
	    }	    
	} 
	if (D > 1) {
	    x[t] += dx[t - s];
	    x[t] -= dx[t - 2*s + 1];
	    if (d > 0) {
		x[t] += dx[t - s + 1];
		x[t] -= dx[t - s];
		x[t] += dx[t - 2*s];
	    }
	    if (d > 1) {
		x[t] -= 2 * dx[t - s];
		x[t] += 2 * dx[t - (s+1)];
		x[t] += dx[t - 2*s];
		x[t] -= dx[t - (2*s+1)];
	    }
	}
    }

#if INT_DEBUG
    for (t=0; t<=t2; t++) {
	fprintf(stderr, "%2d: %12.5g %12.5g %12.5g\n",
		t, dx[t], x[t], dxreal[t]);
    }
#endif

    /* transcribe integrated result back into "dx" */
    for (t=0; t<=t2; t++) {
	if (t < t1) {
	    dx[t] = NADBL;
	} else {
	    dx[t] = x[t];
	}
    }

    free(x);

    return 0;
}

/* write the various statistics from ARMA estimation into
   a gretl MODEL struct */

static void write_arma_model_stats (MODEL *pmod, model_info *arma,
				    const int *list, const double **Z, 
				    const double *theta, 
				    struct arma_info *ainfo)
{
    double **series = NULL;
    const double *e = NULL;
    const double *y = NULL;
    double mean_error;
    int i, t;

    if (arma != NULL) {
	series = model_info_get_series(arma);
	e = series[0];
	pmod->lnL = model_info_get_ll(arma);
    }

    pmod->ci = ARMA;
    pmod->ifc = arma_has_const(ainfo);

    pmod->dfn = ainfo->nc - pmod->ifc;
    pmod->dfd = pmod->nobs - pmod->dfn;
    pmod->ncoeff = ainfo->nc;

    if (theta != NULL) {
	for (i=0; i<pmod->ncoeff; i++) {
	    pmod->coeff[i] = theta[i];
	}
    }

    free(pmod->list);
    pmod->list = gretl_list_copy(list);

    if (arma_is_arima(ainfo)) {
	y = ainfo->dy;
    } else {
	y = Z[ainfo->yno];
    }

    pmod->ybar = gretl_mean(pmod->t1, pmod->t2, y);
    pmod->sdy = gretl_stddev(pmod->t1, pmod->t2, y);

    mean_error = pmod->ess = 0.0;

    for (t=pmod->t1; t<=pmod->t2; t++) {
	if (e != NULL) {
	    pmod->uhat[t] = e[t];
	}
	if (!na(y[t])) {
	    pmod->yhat[t] = y[t] - pmod->uhat[t];
	    pmod->ess += pmod->uhat[t] * pmod->uhat[t];
	    mean_error += pmod->uhat[t];
	}
    }

    if (arma_is_arima(ainfo)) {
	int maxlag = ainfo->d + ainfo->D * ainfo->pd;
	int t1 = pmod->t1;
	int t1d = 0;

	for (t=0; t<t1; t++) {
	    if (na(ainfo->dy[t])) {
		t1d++;
	    } else {
		break;
	    }
	}
#if INT_DEBUG
	fprintf(stderr, "pmod->t1=%d, t1d=%d, maxlag=%d\n", 
		pmod->t1, t1d, maxlag);
#endif
	if (t1d + maxlag > t1) {
	    t1  = t1d + maxlag;
	}
	arima_integrate(pmod->yhat, ainfo->dy, Z[ainfo->yno][t1 - 1], 
			t1, pmod->t2, ainfo->d, ainfo->D, ainfo->pd);
    }

    mean_error /= pmod->nobs;
    gretl_model_set_double(pmod, "mean_error", mean_error);

    if (arma != NULL) {
	/* in X12A case we read this from file */
	pmod->sigma = sqrt(pmod->ess / pmod->nobs);
    } 

    pmod->rsq = pmod->adjrsq = pmod->fstt = NADBL;
    pmod->tss = NADBL;

#if 0
    arma_R2_and_F(pmod, y);
#endif

    if (arma != NULL) {
	mle_criteria(pmod, 1);
    }

    if (arma_has_seasonal(ainfo)) {
	gretl_model_set_int(pmod, "arma_P", ainfo->P);
	gretl_model_set_int(pmod, "arma_Q", ainfo->Q);
	gretl_model_set_int(pmod, "arma_pd", ainfo->pd);	
    }

    if (ainfo->d > 0 || ainfo->D > 0) {
	gretl_model_set_int(pmod, "arima_d", ainfo->d);
	gretl_model_set_int(pmod, "arima_D", ainfo->D);
    }

    if (ainfo->dy != NULL) {
	gretl_model_set_data(pmod, "arima_dy", ainfo->dy, 
			     ainfo->T * sizeof *ainfo->dy);
	ainfo->dy = NULL;
    }

    if (ainfo->r > 0) {
	gretl_model_set_int(pmod, "armax", 1);
    }
}

static void calc_max_lag (struct arma_info *ainfo)
{
    int pmax = ainfo->p;
    int dmax = ainfo->d;

    if (arma_has_seasonal(ainfo)) {
	pmax += ainfo->P * ainfo->pd;
	dmax += ainfo->D * ainfo->pd;
    }

    ainfo->maxlag = pmax + dmax;
}

static int 
arma_adjust_sample (const DATAINFO *pdinfo, const double **Z, const int *list,
		    struct arma_info *ainfo)
{
    int t1 = pdinfo->t1, t2 = pdinfo->t2;
    int an, i, v, t, t1min = 0;
    int vstart, pmax, anymiss;

    vstart = arma_list_y_position(ainfo);

    pmax = ainfo->p;
    if (ainfo->P > 0) {
	pmax += ainfo->P * ainfo->pd;
    }   

    for (t=0; t<=pdinfo->t2; t++) {
	anymiss = 0;
	for (i=vstart; i<=list[0]; i++) {
	    v = list[i];
	    if (na(Z[v][t])) {
		anymiss = 1;
		break;
	    }
	}
	if (anymiss) {
	    t1min++;
        } else {
	    break;
	}
    }

#if 0
    t1min += ainfo->maxlag;
#else
    if (!arma_by_x12a(ainfo)) {
	/* not required for X-12-ARIMA? */
	t1min += ainfo->maxlag;
    }
#endif

    if (t1 < t1min) {
	t1 = t1min;
    }

    for (t=pdinfo->t2; t>=t1; t--) {
	anymiss = 0;
	for (i=vstart; i<=list[0]; i++) {
	    v = list[i];
	    if (na(Z[v][t])) {
		anymiss = 1;
		break;
	    }
	}
	if (anymiss) {
	    t2--;
        } else {
	    break;
	}
    }

    for (t=t1-pmax; t<t2; t++) {
	for (i=vstart; i<=list[0]; i++) {
	    if (t < t1 && i > vstart) {
		continue;
	    }
	    v = list[i];
	    if (na(Z[v][t])) {
		char msg[64];

		sprintf(msg, _("Missing value encountered for "
			       "variable %d, obs %d"), v, t + 1);
		gretl_errmsg_set(msg);
		return 1;
	    }
	}
    }

    an = t2 - t1 + 1;
    if (an <= ainfo->nc) {
	return 1; 
    }

    ainfo->t1 = t1;
    ainfo->t2 = t2;

    return 0;
}

#define ARIMA_DEBUG 0

/* remove the intercept from list of regressors */

static int arma_remove_const (int *list, int seasonal, int diffs,
			      const double **Z, const DATAINFO *pdinfo)
{
    int xstart, ret = 0;
    int i, j;

    if (diffs) {
	xstart = (seasonal)? 10 : 6;
    } else {
	xstart = (seasonal)? 8 : 5;
    }

    for (i=xstart; i<=list[0]; i++) {
	if (list[i] == 0 || true_const(list[i], Z, pdinfo)) {
	    for (j=i; j<list[0]; j++) {
		list[j] = list[j+1];
	    }
	    list[0] -= 1;
	    ret = 1;
	    break;
	}
    }

    return ret;
}

static int check_arma_sep (int *list, int sep1, struct arma_info *ainfo)
{
    int sep2 = (sep1 == 3)? 6 : 8;
    int i, err = 0;

    for (i=sep1+1; i<=list[0]; i++) {
	if (list[i] == LISTSEP) {
	    if (i == sep2) {
		/* there's a second list separator in the right place:
		   we've got a seasonal specification */
		set_arma_has_seasonal(ainfo);
	    } else {
		err = 1;
	    }
	}
    }

    if (!err && sep1 == 4) {
	/* check for apparent but not "real" arima spec */
	if (arma_has_seasonal(ainfo)) {
	    if (list[2] == 0 && list[6] == 0) {
		gretl_list_delete_at_pos(list, 2);
		gretl_list_delete_at_pos(list, 5);
		unset_arma_is_arima(ainfo);
	    }
	} else {
	    if (list[2] == 0) {
		gretl_list_delete_at_pos(list, 2);
		unset_arma_is_arima(ainfo);
	    }
	}
    }

#if ARIMA_DEBUG
    fprintf(stderr, "check_arma_sep: returning %d\n", err);
#endif

    return err;
}

static int check_arma_list (int *list, gretlopt opt, 
			    const double **Z, const DATAINFO *pdinfo,
			    struct arma_info *ainfo)
{
    int armax = 0;
    int hadconst = 0;
    int err = 0;

    if (arma_has_seasonal(ainfo)) {
	armax = (list[0] > 7);
    } else {
	armax = (list[0] > 4);
    }

    if (list[1] < 0 || list[1] > MAX_ARMA_ORDER) {
	err = 1;
    } else if (list[2] < 0 || list[2] > MAX_ARMA_ORDER) {
	err = 1;
    } 

    if (!err) {
	ainfo->p = list[1];
	ainfo->q = list[2];
    }

    if (!err && arma_has_seasonal(ainfo)) {
	if (list[0] < 7) {
	    err = 1;
	} else if (list[4] < 0 || list[4] > MAX_ARMA_ORDER) {
	    err = 1;
	} else if (list[5] < 0 || list[5] > MAX_ARMA_ORDER) {
	    err = 1;
	} 
    }

    if (!err && arma_has_seasonal(ainfo)) {
	ainfo->P = list[4];
	ainfo->Q = list[5];
    }

    /* If there's an explicit constant in the list here, we'll remove
       it, since it is added implicitly later.  But if we're supplied
       with OPT_N (meaning: no intercept) we'll flag this by
       setting ifc = 0.  Also, if the user gave an armax list
       (specifying regressors) we'll respect the absence of a constant
       from that list by setting ifc = 0.
    */

    if (!err) {
	if (armax) {
	    hadconst = arma_remove_const(list, arma_has_seasonal(ainfo),
					 0, Z, pdinfo);
	}
	if ((opt & OPT_N) || (armax && !hadconst)) {
	    ;
	} else {
	    set_arma_has_const(ainfo);
	}
    }

    if (err) {
	gretl_errmsg_set(_("Error in arma command"));
    } else {
	ainfo->r = list[0] - ((arma_has_seasonal(ainfo))? 7 : 4);
	ainfo->nc = ainfo->p + ainfo->q + ainfo->P + ainfo->Q
	    + ainfo->r + arma_has_const(ainfo);
	ainfo->yno = (arma_has_seasonal(ainfo))? list[7] : list[4];
    }

    return err;
}

static int check_arima_list (int *list, gretlopt opt, 
			     const double **Z, const DATAINFO *pdinfo,
			     struct arma_info *ainfo)
{
    int armax = 0;
    int hadconst = 0;
    int err = 0;

#if ARIMA_DEBUG
    printlist(list, "check_arima_list");
#endif

    if (arma_has_seasonal(ainfo)) {
	armax = (list[0] > 9);
    } else {
	armax = (list[0] > 5);
    }

    if (list[1] < 0 || list[1] > MAX_ARMA_ORDER) {
	err = 1;
    } else if (list[2] < 0 || list[2] > MAX_ARIMA_DIFF) {
	err = 1;
    } else if (list[3] < 0 || list[3] > MAX_ARMA_ORDER) {
	err = 1;
    } 

    if (!err) {
	ainfo->p = list[1];
	ainfo->d = list[2];
	ainfo->q = list[3];
    }

    if (!err && arma_has_seasonal(ainfo)) {
	if (list[0] < 9) {
	    err = 1;
	} else if (list[5] < 0 || list[5] > MAX_ARMA_ORDER) {
	    err = 1;
	} else if (list[6] < 0 || list[6] > MAX_ARIMA_DIFF) {
	    err = 1;
	} else if (list[7] < 0 || list[7] > MAX_ARMA_ORDER) {
	    err = 1;
	} 
    }

    if (!err && arma_has_seasonal(ainfo)) {
	ainfo->P = list[5];
	ainfo->D = list[6];
	ainfo->Q = list[7];
    }

    /* If there's an explicit constant in the list here, we'll remove
       it, since it is added implicitly later.  But if we're supplied
       with OPT_N (meaning: no intercept) we'll flag this by
       setting ifc = 0.  Also, if the user gave an armax list
       (specifying regressors) we'll respect the absence of a constant
       from that list by setting ifc = 0.
    */

    if (!err) {
	if (armax) {
	    hadconst = arma_remove_const(list, arma_has_seasonal(ainfo),
					 1, Z, pdinfo);
	}
	if ((opt & OPT_N) || (armax && !hadconst)) {
	    ;
	} else {
	    set_arma_has_const(ainfo);
	}
    }

    if (err) {
	gretl_errmsg_set(_("Error in arma command"));
    } else {
	ainfo->r = list[0] - ((arma_has_seasonal(ainfo))? 9 : 5);
	ainfo->nc = ainfo->p + ainfo->q + ainfo->P + ainfo->Q
	    + ainfo->r + arma_has_const(ainfo);
	ainfo->yno = (arma_has_seasonal(ainfo))? list[9] : list[5];
    }

    return err;
}

static int arma_check_list (int *list, gretlopt opt,
			    const double **Z, const DATAINFO *pdinfo,
			    struct arma_info *ainfo)
{
    int sep1 = gretl_list_separator_position(list);
    int err = 0;

#if ARIMA_DEBUG
    fprintf(stderr, "arma_check_list: sep1 = %d\n", sep1);
    printlist(list, "incoming list");
#endif

    if (sep1 == 3) {
	if (list[0] < 4) {
	    err = E_PARSE;
	}
    } else if (sep1 == 4) {
	if (list[0] < 5) {
	    err = E_PARSE;
	} else {
	    set_arma_is_arima(ainfo);
	}
    } else {
	err = E_PARSE;
    }

    if (!err) {
	err = check_arma_sep(list, sep1, ainfo);
    }

    if (!err) {
	if (arma_is_arima(ainfo)) {
	    /* check for arima spec */
	    err = check_arima_list(list, opt, Z, pdinfo, ainfo);
	} else {	    
	    /* check for simple arma spec */
	    err = check_arma_list(list, opt, Z, pdinfo, ainfo);
	} 
    }

    /* catch null model */
    if (ainfo->nc == 0) {
	err = E_ARGS;
    }

#if ARIMA_DEBUG
    printlist(list, "ar(i)ma list after checking");
    fprintf(stderr, "err = %d\n", err);
#endif

    return err;
}

static double *
arima_difference (const double *x, struct arma_info *ainfo)
{
    double *dx;
    int s = ainfo->pd;
    int t, t1 = 0;

#if ARMA_DEBUG
    fprintf(stderr, "doing arima_difference: d = %d, D = %d\n",
	    ainfo->d, ainfo->D);
#endif

    dx = malloc(ainfo->T * sizeof *dx);
    if (dx == NULL) {
	return NULL;
    }

    for (t=0; t<ainfo->T; t++) {
	if (na(x[t])) {
	    t1++;
	} else {
	    break;
	}
    }

    t1 += ainfo->d + ainfo->D * s;

    for (t=0; t<t1; t++) {
	dx[t] = NADBL;
    }

    for (t=t1; t<ainfo->T; t++) {
	dx[t] = x[t];
	if (ainfo->d > 0) {
	    dx[t] -= x[t-1];
	} 
	if (ainfo->d > 1) {
	    dx[t] -= x[t-1];
	    dx[t] += x[t-2];
	}
	if (ainfo->D > 0) {
	    dx[t] -= x[t - s];
	    if (ainfo->d > 0) {
		dx[t] += x[t - (s+1)];
	    }
	    if (ainfo->d > 1) {
		dx[t] += x[t - (s+1)];
		dx[t] -= x[t - 2*s];
	    }	    
	} 
	if (ainfo->D > 1) {
	    dx[t] -= x[t - s];
	    dx[t] += x[t - 2*s];
	    if (ainfo->d > 0) {
		dx[t] -= x[t - s];
		dx[t] += x[t - (s+1)];
		dx[t] -= x[t - (2*s+1)];
	    }
	    if (ainfo->d > 1) {
		dx[t] += 2 * x[t - (s+1)];
		dx[t] -= 2 * x[t - (s+2)];
		dx[t] -= x[t - (2*s+1)];
		dx[t] += x[t - (2*s+2)];
	    }
	}
    }

    return dx;
}

