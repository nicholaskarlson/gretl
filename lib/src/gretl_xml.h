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
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef GRETL_XML_H
#define GRETL_XML_H

#ifndef GRETLCLI

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#define XUC const xmlChar *

int gretl_xml_open_doc_root (const char *fname,
			     const char *rootname,
			     xmlDocPtr *pdoc, 
			     xmlNodePtr *pnode);

void gretl_xml_put_double (const char *tag, double x, FILE *fp);

void gretl_xml_put_double_array (const char *tag, double *x, int n,
				 FILE *fp);

void gretl_xml_put_strings_array (const char *tag, const char **strs, int n,
				  FILE *fp);

void gretl_xml_put_list (const char *tag, const int *list, FILE *fp);

int gretl_xml_get_prop_as_int (xmlNodePtr node, const char *tag,
			       int *targ);

int gretl_xml_get_prop_as_double (xmlNodePtr node, const char *tag,
				  double *targ);

int *gretl_xml_node_get_list (xmlNodePtr node, xmlDocPtr doc, int *err);

double *gretl_xml_get_doubles_array (xmlNodePtr node, xmlDocPtr doc,
				     int *err);

#endif /* !GRETLCLI */

int gretl_write_gdt (const char *fname, const int *list, 
		     const double **Z, const DATAINFO *pdinfo, 
		     GretlDataFormat fmt, PATHS *ppaths);

int gretl_read_gdt (double ***pZ, DATAINFO **ppdinfo, char *fname,
		    PATHS *ppaths, int data_status, PRN *prn, int gui);

char *gretl_get_gdt_description (const char *fname);


#endif /* GRETL_XML_H */
