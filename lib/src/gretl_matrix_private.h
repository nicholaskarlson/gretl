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

#ifndef GRETL_MATRIX_PRIVATE_H
#define GRETL_MATRIX_PRIVATE_H

struct _gretl_matrix {
    int packed;
    int rows;
    int cols;
    int missrows;
    int t;
    double *val;
};

#define mdx(a,i,j)   ((j)*(a)->rows+(i))
#define mdxtr(a,i,j) ((i)*(a)->rows+(j))

#endif /* GRETL_MATRIX_PRIVATE_H */
