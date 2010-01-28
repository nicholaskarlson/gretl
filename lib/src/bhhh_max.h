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

#ifndef BHHH_MAX_H
#define BHHH_MAX_H

typedef double (*LL_FUNC) (double *, 
			   gretl_matrix *, 
			   void *, 
			   int,
			   int *);

int bhhh_max (double *theta, int k, int T,
	      LL_FUNC loglik, double toler, 
	      int *itcount,
	      void *data, 
	      gretl_matrix *V,
	      gretlopt opt,
	      PRN *prn);

#endif /* BHHH_MAX_H */
