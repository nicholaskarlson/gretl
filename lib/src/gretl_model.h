/* gretl - The Gnu Regression, Econometrics and Time-series Library
 * Copyright (C) 1999-2004 Ramu Ramanathan and Allin Cottrell
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License 
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this software; if not, write to the 
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef GRETL_MODEL_H
#define GRETL_MODEL_H

typedef struct VCV_ VCV;

struct VCV_ {
    int ci;
    int *list;
    double *vec;
};

/**
 * free_model:
 * @p: pointer to #MODEL.
 *
 * Free allocated content of MODEL then the pointer itself.
 */

#define free_model(p) if (p != NULL) { \
                             clear_model(p); \
                             free(p); \
                          }

#define AR_MODEL(c) (c == AR || \
                     c == ARMA || \
                     c == CORC || \
                     c == GARCH || \
                     c == HILU || \
                     c == PWE)

#define SIMPLE_AR_MODEL(c) (c == AR || \
                            c == CORC || \
                            c == HILU || \
                            c == PWE)

#define ML_ESTIMATOR(c) (c == ARMA || \
                         c == GARCH || \
                         c == LOGIT || \
                         c == POISSON || \
                         c == PROBIT || \
                         c == TOBIT)

#define LIMDEP(c) (c == LOGIT || \
                   c == PROBIT || \
                   c == TOBIT)

#define LSQ_MODEL(c) (c == CORC || \
                      c == HCCM || \
                      c == HILU || \
                      c == HSK || \
                      c == OLS || \
                      c == PWE || \
                      c == WLS)

#define is_model_ref_cmd(c) (c == ADD || \
	                     c == ARCH || \
	                     c == CHOW || \
	                     c == CUSUM || \
	                     c == FCAST || \
	                     c == FCASTERR || \
	                     c == FIT || \
                             c == LEVERAGE || \
	                     c == LMTEST || \
                             c == OMIT || \
	                     c == RESTRICT || \
                             c == VIF)

typedef enum {
    GRETL_TEST_ADD,
    GRETL_TEST_ARCH,
    GRETL_TEST_AUTOCORR,
    GRETL_TEST_CHOW,
    GRETL_TEST_CUSUM,
    GRETL_TEST_GROUPWISE,
    GRETL_TEST_LOGS,
    GRETL_TEST_NORMAL,
    GRETL_TEST_OMIT,
    GRETL_TEST_RESET,
    GRETL_TEST_SQUARES,
    GRETL_TEST_WHITES,
    GRETL_TEST_MAX
} ModelTestType;

MODEL *gretl_model_new (void);

void gretl_model_init (MODEL *pmod);

void gretl_model_smpl_init (MODEL *pmod, const DATAINFO *pdinfo);

void impose_model_smpl (const MODEL *pmod, DATAINFO *pdinfo);

void gretl_model_set_auxiliary (MODEL *pmod, ModelAuxCode aux);

void clear_model (MODEL *pmod);

int gretl_model_set_data_with_destructor (MODEL *pmod, const char *key, void *ptr, 
					  size_t size, void (*destructor) (void *));

int gretl_model_set_data (MODEL *pmod, const char *key, void *ptr, size_t size);

int gretl_model_destroy_data_item (MODEL *pmod, const char *key);

int gretl_model_detach_data_item (MODEL *pmod, const char *key);

int gretl_model_set_int (MODEL *pmod, const char *key, int val);

int gretl_model_set_double (MODEL *pmod, const char *key, double val);

void *gretl_model_get_data (const MODEL *pmod, const char *key);

void *gretl_model_get_data_and_size (const MODEL *pmod, const char *key,
				     size_t *sz);

int gretl_model_get_int (const MODEL *pmod, const char *key);

double gretl_model_get_double (const MODEL *pmod, const char *key);

void free_vcv (VCV *vcv);

int gretl_model_new_vcv (MODEL *pmod, int *nelem);

VCV *gretl_model_get_vcv (MODEL *pmod);

void debug_print_model_info (const MODEL *pmod, const char *msg);

int copy_model (MODEL *targ, const MODEL *src, const DATAINFO *pdinfo);

int swap_models (MODEL **targ, MODEL **src);

int is_model_cmd (const char *line);

int is_quiet_model_test (int ci, gretlopt opt);

int command_ok_for_model (int test_ci, int model_ci);

int get_model_count (void);

void reset_model_count (void);

int model_count_plus (void);

void model_count_minus (void);

void set_model_id (MODEL *pmod);

ModelTest *new_test_on_model (MODEL *pmod, ModelTestType ttype);

void model_test_set_teststat (ModelTest *test, unsigned char ts);
void model_test_set_order (ModelTest *test, int order);
void model_test_set_dfn (ModelTest *test, int df);
void model_test_set_dfd (ModelTest *test, int df);
void model_test_set_value (ModelTest *test, double val);
void model_test_set_pvalue (ModelTest *test, double pval);
void model_test_set_param (ModelTest *test, const char *s);
void model_test_set_allocated_param (ModelTest *test, char *s);

void gretl_model_test_print (const MODEL *pmod, int i, PRN *prn);
void gretl_model_print_last_test (const MODEL *pmod, PRN *prn);

void model_list_to_string (int *list, char *buf);

int highest_numbered_var_in_model (const MODEL *pmod, 
				   const DATAINFO *pdinfo);

int mle_aic_bic (MODEL *pmod, int addk);

double coeff_pval (const MODEL *pmod, double x, int df);

#endif /* GRETL_MODEL_H */
