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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef GRETL_EQUATION_SYSTEM_H
#define GRETL_EQUATION_SYSTEM_H

typedef struct _gretl_equation_system gretl_equation_system;

enum gretl_system_types {
    SUR = 0,
    THREESLS,
    FIML
};

enum system_save_flags {
    GRETL_SYSTEM_SAVE_UHAT = 1 << 0,
    GRETL_SYSTEM_SAVE_YHAT = 1 << 1
};

gretl_equation_system *system_start (const char *line);

int gretl_equation_system_append (gretl_equation_system *sys, 
				  int *list);

int system_parse_line (gretl_equation_system *sys,
		       const char *line, 
		       const DATAINFO *pdinfo);

int gretl_equation_system_finalize (gretl_equation_system *sys, 
				    double ***pZ, DATAINFO *pdinfo,
				    PRN *prn);

void gretl_equation_system_destroy (gretl_equation_system *sys);

int system_save_uhat (const gretl_equation_system *sys);
int system_save_yhat (const gretl_equation_system *sys);

int system_n_equations (const gretl_equation_system *sys);
int system_n_identities (const gretl_equation_system *sys);

int system_n_obs (const gretl_equation_system *sys);
void system_set_n_obs (gretl_equation_system *sys, int n);

int system_max_indep_vars (const gretl_equation_system *sys);
int system_n_indep_vars (const gretl_equation_system *sys);

int system_adjust_t1t2 (const gretl_equation_system *sys,
			int *t1, int *t2, const double **Z);

int *system_get_list (const gretl_equation_system *sys, int i);

int *compose_tsls_list (const gretl_equation_system *sys, int i);

int system_get_depvar (const gretl_equation_system *sys, int i);

const char *gretl_system_short_string (const MODEL *pmod);

int system_get_type (const gretl_equation_system *sys);

int *system_get_endog_vars (const gretl_equation_system *sys);
int *system_get_instr_vars (const gretl_equation_system *sys);

void system_attach_uhat (gretl_equation_system *sys, gretl_matrix *u);

const gretl_matrix *system_get_uhat (const gretl_equation_system *sys);

void system_attach_models (gretl_equation_system *sys, MODEL **models);

MODEL *system_get_model (const gretl_equation_system *sys, int i);

double system_get_ll (const gretl_equation_system *sys);
double system_get_llu (const gretl_equation_system *sys);
void system_set_ll (gretl_equation_system *sys, double ll);
void system_set_llu (gretl_equation_system *sys, double llu);

int system_get_df (const gretl_equation_system *sys);

int rhs_var_in_identity (const gretl_equation_system *sys, int lhsvar,
			 int rhsvar);

void print_fiml_sys_info (const gretl_equation_system *sys, 
			  const DATAINFO *pdinfo, PRN *prn);

#endif /* GRETL_EQUATION_SYSTEM_H */
