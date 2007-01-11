/* for inclusion in interact.c: provides a mechanism for handling
   the fallout from the auto-generation of lags when an estimation
   command using the syntax of "foo(-1)" in the regression list,
   or "foo(-1 to -4)", etc.
*/

struct Laginfo_ {
    int *reflist;
    int *genlist;
    int **lag_lists;
};

#undef LLDEBUG

static Laginfo *list_lag_info_new (void)
{
    Laginfo *linfo = malloc(sizeof *linfo);

    if (linfo != NULL) {
	linfo->reflist = NULL;
	linfo->genlist = NULL;
	linfo->lag_lists = NULL;
    }

    return linfo;
}

static void list_lag_info_destroy (Laginfo *linfo)
{
    if (linfo != NULL) {
	if (linfo->reflist != NULL) {
	    int i;

	    for (i=0; i<linfo->reflist[0]; i++) {
		free(linfo->lag_lists[i]);
	    }
	    free(linfo->lag_lists);
	    free(linfo->reflist);
	}
	free(linfo->genlist);
	free(linfo);
    }
}

static void cmd_lag_info_destroy (CMD *cmd)
{
    list_lag_info_destroy(cmd->linfo);
    cmd->linfo = NULL;
}

static const int *
get_lag_list_by_varnum (int v, const Laginfo *linfo)
{
    int *list = NULL;
    int i;

    if (linfo != NULL && linfo->reflist != NULL) {
	for (i=1; i<=linfo->reflist[0]; i++) {
	    if (linfo->reflist[i] == v) {
		list = linfo->lag_lists[i-1];
		break;
	    }
	}
    }

    return list;
}

static int add_lagv_to_genlist (int lv, Laginfo *linfo)
{
    int *genlist;
    int n, err = 0;

    if (linfo->genlist != NULL) {
	n = linfo->genlist[0] + 2;
    } else {
	n = 2;
    }

    genlist = realloc(linfo->genlist, n * sizeof *genlist);
    if (genlist != NULL) {
	linfo->genlist = genlist;
	genlist[n - 1] = lv;
	genlist[0] = n - 1;
    } else {
	err = E_ALLOC;
    }

    return err;
}

static int add_lag_to_laglist (int i, int lag, Laginfo *linfo)
{
    int err = 0;

    if (linfo->lag_lists[i] == NULL) {
	linfo->lag_lists[i] = gretl_list_new(1);
	if (linfo->lag_lists[i] != NULL) {
	    linfo->lag_lists[i][1] = lag;
	} else {
	    err = E_ALLOC;
	}
    } else {
	int n = linfo->lag_lists[i][0] + 2;
	int *laglist;

	laglist = realloc(linfo->lag_lists[i], n * sizeof *laglist);
	if (laglist != NULL) {
	    linfo->lag_lists[i] = laglist;
	    laglist[n - 1] = lag;
	    laglist[0] = n - 1;
	} else {
	    err = E_ALLOC;
	}
    }

    return err;
}

static int laginfo_add_lags_list (int n, Laginfo *linfo)
{
    int **llists;
    int err = 0;

    llists = realloc(linfo->lag_lists, n * sizeof *llists);

    if (llists != NULL) {
#if LLDEBUG
	fprintf(stderr, " realloced lag_lists, size %d\n", n);
#endif
	linfo->lag_lists = llists;
	linfo->lag_lists[n - 1] = NULL;
    } else {
	err = E_ALLOC;
    }

    return err;
}

static int laginfo_expand_reflist (int n, int v, Laginfo *linfo)
{
    int *reflist;
    int l0, err = 0;

    if (linfo->reflist == NULL) {
	l0 = 1;
    } else {
	l0 = linfo->reflist[0] + 1;
    }

#if LLDEBUG
    fprintf(stderr, " now reallocing %p, %d elements\n", 
	    (void *) linfo->reflist, n);
#endif

    reflist = realloc(linfo->reflist, n * sizeof *reflist);

    if (reflist != NULL) {
	linfo->reflist = reflist;
	linfo->reflist[0] = l0;
	linfo->reflist[l0] = v;
    } else {
	err = E_ALLOC;
    }

    return err;
}

static int add_to_list_lag_info (int v, int lag, int lagv, CMD *cmd)
{
    int add_to_reflist = 0;
    int nl, llnum = 0, err = 0;

#if LLDEBUG
    fprintf(stderr, "*** add_to_list_lag_info: v=%d, lag=%d, lagv=%d\n",
	    v, lag, lagv);
#endif

    if (cmd->linfo == NULL) {
	cmd->linfo = list_lag_info_new();
	if (cmd->linfo == NULL) {
	    return E_ALLOC;
	}
	add_to_reflist = 1;
    }

    if (!add_to_reflist) {
	if (get_lag_list_by_varnum(v, cmd->linfo) == NULL) {
	    /* there's no list already started for this var */
	    add_to_reflist = 1;
	}
    }

    if (cmd->linfo->reflist == NULL) {
	nl = 1;
    } else {
	nl = cmd->linfo->reflist[0] + 1;
    }

    if (add_to_reflist) {
	err = laginfo_add_lags_list(nl, cmd->linfo);
	if (!err) {
	    err = laginfo_expand_reflist(nl + 1, v, cmd->linfo);
	}
    }

    if (!err) {
	err = add_lagv_to_genlist(lagv, cmd->linfo);
#if LLDEBUG
	fprintf(stderr, " add_lagv_to_genlist: lagv = %d, err = %d\n", 
		lagv, err);
#endif
    }

    if (!err) {
	llnum = cmd->linfo->reflist[0] - 1;
#if LLDEBUG
	printlist(cmd->linfo->reflist, "reflist");
	fprintf(stderr, " llnum = %d\n", llnum);
	fprintf(stderr, " doing add_lag_to_laglist(%d, %d, %p)\n", 
		llnum, lag, (void *) cmd->linfo);
#endif
	err = add_lag_to_laglist(llnum, lag, cmd->linfo);
    }

    return err;
}

static int var_lags_contiguous (const int *laglist)
{
    int i, ret = 1;

    /* actual lags */
    for (i=2; i<=laglist[0]; i++) {
	if (laglist[i] != laglist[i-1] + 1) {
	    ret = 0;
	    break;
	}
    }

    if (ret == 0) {
	/* check for contiguous leads? */
	int test = 1;

	for (i=2; i<=laglist[0]; i++) {
	    if (laglist[i] != laglist[i-1] - 1) {
		test = 0;
		break;
	    }
	}
	if (test == 1) {
	    ret = 1;
	}
    }	

    return ret;    
}

static const char *lag_sign_str (int lag)
{
    if (lag > 0) return "-";
    if (lag < 0) return "+";
    else return "";
}

/* returns number of bytes printed */

static int print_var_lags (const int *laglist, PRN *prn)
{
    char tmp[32];
    int lag, lsign;
    int lmax = laglist[0];
    int i, ret = 0;
    
    if (lmax == 1) {
	lsign = laglist[1];
	lag = abs(laglist[1]);
	sprintf(tmp, "(%s%d)", lag_sign_str(lsign), lag);
	ret += pputs(prn, tmp);	
    } else if (var_lags_contiguous(laglist)) {
	/* first lag */
	lsign = laglist[1];
	lag = abs(laglist[1]);
	sprintf(tmp, "(%s%d to ", lag_sign_str(lsign), lag);
	ret += pputs(prn, tmp);	
	/* last lag */ 
	lsign = laglist[lmax];
	lag = abs(laglist[lmax]);
	sprintf(tmp, "%s%d)", lag_sign_str(lsign), lag);
	ret += pputs(prn, tmp);	
    } else {
	pputc(prn, '(');
	ret++;
	for (i=1; i<=lmax; i++) {
	    lsign = laglist[i];
	    lag = fabs(laglist[i]);
	    sprintf(tmp, "%s%d", lag_sign_str(lsign), lag);
	    ret += pputs(prn, tmp);
	    if (i < lmax) {
		ret += pputs(prn, ", ");
	    } else {
		pputc(prn, ')');
		ret++;
	    }
	}
    }

    return ret;
}

static int 
print_lags_by_varnum (int v, const Laginfo *linfo, 
		      const DATAINFO *pdinfo, 
		      PRN *prn)
{
    const int *laglist = NULL;
    int ret = 0;

    laglist = get_lag_list_by_varnum(v, linfo);
    if (laglist != NULL) {
	pputc(prn, ' ');
	ret = 1 + pputs(prn, pdinfo->varname[v]);
	ret += print_var_lags(laglist, prn);
    } 

    return ret;
}

static int is_auto_generated_lag (int v, const Laginfo *linfo)
{
    int i, ret = 0;

    if (linfo != NULL && linfo->genlist != NULL) {
	for (i=1; i<=linfo->genlist[0]; i++) {
	    if (v == linfo->genlist[i]) {
		ret = i;
		break;
	    }
	}
    }

    return ret;
}

static int is_first_lag (int v, const Laginfo *linfo, int *src)
{
    int i, j, k = 0, ret = 0;

    for (i=0; i<linfo->reflist[0]; i++) {
	for (j=0; j<linfo->lag_lists[i][0]; j++) {
	    k++;
	    if (k == v) {
		if (j == 0) {
		    ret = 1;
		    if (src != NULL) {
			*src = linfo->reflist[i+1];
		    }
		}
		break;
	    }
	}
    }

    return ret;
}



	    

    
