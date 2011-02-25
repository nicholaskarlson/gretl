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

#ifndef GRETL_FOREIGN_H

int foreign_append_line(const char *line, gretlopt opt, PRN *prn);

int foreign_execute (const double **Z, const DATAINFO *pdinfo,
		     gretlopt opt, PRN *prn);

int write_gretl_R_files (const char *buf,
			 const double **Z, const DATAINFO *pdinfo,
			 gretlopt opt);

void delete_gretl_R_files (void);

int write_gretl_ox_file (const char *buf, gretlopt opt, const char **pfname);

int write_gretl_octave_file (const char *buf, gretlopt opt, 
			     const double **Z, const DATAINFO *pdinfo,
			     const char **pfname);

void delete_gretl_ox_file (void);

void delete_gretl_octave_file (void);

#ifdef USE_RLIB

int get_R_function_by_name (const char *name);

int gretl_R_get_call (const char *name, int argc);

int gretl_R_function_add_scalar (double x);

int gretl_R_function_add_vector (const double *x, int t1, int t2);

int gretl_R_function_add_matrix (const gretl_matrix *m);

int gretl_R_function_exec (const char *name, int *rtype, void **ret);

void gretl_R_reset_error (void);

void gretl_R_cleanup (void);

#endif

#endif /* GRETL_FOREIGN_H */
