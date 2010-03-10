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

#define FULL_XML_HEADERS

#include "libgretl.h"
#include "gretl_xml.h"
#include "gretl_panel.h"
#include "gretl_func.h"
#include "usermat.h"
#include "gretl_scalar.h"
#include "dbread.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#undef XML_DEBUG

#define GRETLDATA_VERSION "1.2"

#ifdef WIN32
# include <glib.h>
static xmlDocPtr gretl_xmlParseFile (const char *fname)
{
    xmlDocPtr ptr = NULL;
    FILE *fp = fopen(fname, "r");

    if (fp != NULL) {
	fclose(fp);
	ptr = xmlParseFile(fname);
    } else {
	int save_errno = errno;
	gchar *fconv;
	gsize wrote;

	fconv = g_locale_from_utf8(fname, -1, NULL, &wrote, NULL);
	if (fconv != NULL) {
	    ptr = xmlParseFile(fconv);
	    g_free(fconv);
	}
	errno = save_errno;
    }

    return ptr;
}
#else
# define gretl_xmlParseFile(f) xmlParseFile(f)
#endif

int gretl_xml_open_doc_root (const char *fname,
			     const char *rootname,
			     xmlDocPtr *pdoc, 
			     xmlNodePtr *pnode)
{
    xmlDocPtr doc;
    xmlNodePtr node;
    int err = 0;

    *pdoc = NULL;
    *pnode = NULL;

    doc = gretl_xmlParseFile(fname);
    if (doc == NULL) {
	gretl_errmsg_sprintf(_("xmlParseFile failed on %s"), fname);
	err = 1;
    }

    if (!err) {
	node = xmlDocGetRootElement(doc);
	if (node == NULL) {
	    gretl_errmsg_sprintf(_("%s: empty document"), fname);
	    xmlFreeDoc(doc);
	    err = 1;
	}
    }

    if (!err) {
	if (xmlStrcmp(node->name, (XUC) rootname)) {
	    gretl_errmsg_sprintf(_("File of the wrong type, root node not %s"),
				 rootname);
	    fprintf(stderr, "Unexpected root node '%s'\n", (char *) node->name);
	    xmlFreeDoc(doc);
	    err = 1;
	}
    }    

    if (!err) {
	*pdoc = doc;
	*pnode = node;
    }

    return err;
}

static char *compact_method_to_string (int method)
{
    if (method == COMPACT_SUM) return "COMPACT_SUM";
    else if (method == COMPACT_AVG) return "COMPACT_AVG";
    else if (method == COMPACT_SOP) return "COMPACT_SOP";
    else if (method == COMPACT_EOP) return "COMPACT_EOP";
    else return "COMPACT_NONE";
}

static int compact_string_to_int (const char *str)
{
    if (!strcmp(str, "COMPACT_SUM")) return COMPACT_SUM;
    else if (!strcmp(str, "COMPACT_AVG")) return COMPACT_AVG;
    else if (!strcmp(str, "COMPACT_SOP")) return COMPACT_SOP;
    else if (!strcmp(str, "COMPACT_EOP")) return COMPACT_EOP;
    else return COMPACT_NONE;
}

/* given a full filename in @src, write to @dest a "simple"
   counterpart without leading path or extension
*/

static char *simple_fname (char *dest, const char *src)
{
    char *p;
    const char *s;

    s = strrchr(src, SLASH);

    /* take last part of src filename */
    if (s != NULL) {
        strcpy(dest, s + 1);
    } else {
        strcpy(dest, src);
    }

    /* trash any extension */
    p = strrchr(dest, '.');
    if (p != NULL && strlen(dest) > 3) {
	*p = '\0';
    }

    return dest;
}

static int alt_puts (const char *s, FILE *fp, gzFile *fz)
{
    int ret = 0;

    if (fp != NULL) {
	ret = fputs(s, fp);
    } else if (fz != NULL) {
	ret = gzputs(fz, s);
    } 

    return ret;
}

static const char *data_structure_string (int s)
{
    switch (s) {
    case TIME_SERIES:
    case SPECIAL_TIME_SERIES:
	return "time-series";
    case STACKED_TIME_SERIES:
	return "stacked-time-series";
    case STACKED_CROSS_SECTION:
	return "stacked-cross-section";
    default:
	return "cross-section";
    }
}

static int savenum (const int *list, int i)
{
    if (list != NULL) {
	return list[i];
    } else {
	return i;
    }
}

/**
 * gretl_xml_put_int:
 * @tag: name to give value.
 * @i: value to put (as attribute)
 * @fp: file to which to write.
 * 
 * Writes to @fp a string of the form "%s=%d".
 */

void gretl_xml_put_int (const char *tag, int i, FILE *fp)
{
    fprintf(fp, "%s=\"%d\" ", tag, i);
}

/**
 * gretl_xml_put_double:
 * @tag: name to give value.
 * @x: value to put (as attribute)
 * @fp: file to which to write.
 * 
 * Writes to @fp a string of the form "%s=%.15g" if the value of
 * @x is valid, otherwise "%s=NA".
 */

void gretl_xml_put_double (const char *tag, double x, FILE *fp)
{
    if (na(x)) {
	fprintf(fp, "%s=\"NA\" ", tag);
    } else {
	fprintf(fp, "%s=\"%.15g\" ", tag, x);
    }
}

/**
 * gretl_xml_put_double_array:
 * @tag: name to give array.
 * @x: values to put.
 * @n: number of values in @x.
 * @fp: file to which to write.
 * 
 */

void gretl_xml_put_double_array (const char *tag, double *x, int n,
				 FILE *fp)
{
    int i;

    fprintf(fp, "<%s count=\"%d\">\n", tag, n);
    for (i=0; i<n; i++) {
	if (na(x[i])) {
	    fputs("NA ", fp);
	} else {
	    fprintf(fp, "%.15g ", x[i]);
	}
    }
    fprintf(fp, "</%s>\n", tag);    
}

/**
 * gretl_xml_put_strings_array:
 * @tag: name to give array.
 * @strs: array of strings to put.
 * @n: number of strings in @strs.
 * @fp: file to which to write.
 * 
 */

void gretl_xml_put_strings_array (const char *tag, const char **strs, int n,
				  FILE *fp)
{
    int i;

    fprintf(fp, "<%s count=\"%d\">\n", tag, n);
    for (i=0; i<n; i++) {
	fprintf(fp, "%s ", strs[i]);
    }
    fprintf(fp, "</%s>\n", tag); 
}

/**
 * gretl_xml_put_tagged_string:
 * @tag: name to give string.
 * @str: string to put.
 * @fp: file to which to write.
 * 
 * Write @str to @fp, enclosed in simple starting and ending 
 * tags specified by @tag.  If @str needs to have XML-special
 * characters escaped, this will be done automatically.
 * If @str is NULL, this is considered a no-op.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int gretl_xml_put_tagged_string (const char *tag, const char *str, 
				 FILE *fp)
{
    int err = 0;

    if (str == NULL) {
	return 0;
    }

    if (gretl_xml_validate(str)) {
	fprintf(fp, "<%s>%s</%s>\n", tag, str, tag);
    } else {
	char *xstr = gretl_xml_encode(str);

	if (xstr != NULL) {
	    fprintf(fp, "<%s>%s</%s>\n", tag, xstr, tag);
	    free(xstr);
	} else {
	    err = E_ALLOC;
	}
    }

    return err;
}

/**
 * gretl_xml_put_raw_string:
 * @str: string to put.
 * @fp: file to which to write.
 * 
 * Write @str to @fp.  If @str needs to have XML-special
 * characters escaped, this will be done automatically.
 * If @str is NULL, this is considered a no-op.
 *
 * Returns: 0 on success, non-zero error code on failure.
 */

int gretl_xml_put_raw_string (const char *str, FILE *fp)
{
    int err = 0;

    if (str == NULL) {
	return 0;
    }    

    if (gretl_xml_validate(str)) {
	fputs(str, fp);
    } else {
	char *xstr = gretl_xml_encode(str);

	if (xstr != NULL) {
	    fputs(xstr, fp);
	    free(xstr);
	} else {
	    err = E_ALLOC;
	}
    }

    return err;
}

/**
 * gretl_xml_put_named_list:
 * @name: name to give list.
 * @list: list of integers to be written.
 * @fp: file to which to write.
 * 
 */

void gretl_xml_put_named_list (const char *name, const int *list, FILE *fp)
{
    int i;

    if (list == NULL) {
	return;
    }

    fprintf(fp, "<list name=\"%s\">\n", name);
    for (i=0; i<=list[0]; i++) {
	fprintf(fp, "%d ", list[i]);
    }
    fputs("</list>\n", fp); 
}

/**
 * gretl_xml_put_tagged_list:
 * @tag: tag in which list should be wrapped.
 * @list: list of integers to be written.
 * @fp: file to which to write.
 * 
 */

void gretl_xml_put_tagged_list (const char *tag, const int *list, FILE *fp)
{
    int i;

    if (list == NULL) {
	return;
    }

    fprintf(fp, "<%s>\n", tag);
    for (i=0; i<=list[0]; i++) {
	if (list[i] == LISTSEP) {
	    fputs("; ", fp);
	} else {
	    fprintf(fp, "%d ", list[i]);
	}
    }
    fprintf(fp, "</%s>\n", tag); 
}

/**
 * gretl_xml_put_matrix:
 * @m: matrix to be written.
 * @name: name for matrix.
 * @fp: file to which to write.
 * 
 */

void gretl_xml_put_matrix (const gretl_matrix *m, const char *name, 
			   FILE *fp)
{
    int i, j;

    if (m == NULL) {
	return;
    }

    if (name == NULL) {
	fprintf(fp, "<gretl-matrix rows=\"%d\" cols=\"%d\">\n", 
		m->rows, m->cols);
    } else {
	fprintf(fp, "<gretl-matrix name=\"%s\" rows=\"%d\" cols=\"%d\" "
		"t1=\"%d\" t2=\"%d\">\n", 
		name, m->rows, m->cols, m->t1, m->t2);
    }

    for (i=0; i<m->rows; i++) {
	for (j=0; j<m->cols; j++) {
	    fprintf(fp, "%.15g ", gretl_matrix_get(m, i, j));
	}
	fputc('\n', fp);
    }

    fputs("</gretl-matrix>\n", fp); 
}

/**
 * gretl_xml_get_prop_as_int:
 * @node: XML node pointer.
 * @tag: name by which integer property is known.
 * @i: location to write int value.
 * 
 * Returns: 1 if an int is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_get_prop_as_int (xmlNodePtr node, const char *tag,
			       int *i)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) tag);
    int ret = 0;

    if (tmp != NULL) {
	*i = atoi((const char *) tmp);
	free(tmp);
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_get_prop_as_char:
 * @node: XML node pointer.
 * @tag: name by which character property is known.
 * @c: location to write value.
 * 
 * Returns: 1 if a char is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_get_prop_as_char (xmlNodePtr node, const char *tag,
				char *c)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) tag);
    int ret = 0;

    if (tmp != NULL) {
	*c = (char) atoi((const char *) tmp);
	free(tmp);
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_get_prop_as_uchar:
 * @node: XML node pointer.
 * @tag: name by which unsigned character property is known.
 * @u: location to write value.
 * 
 * Returns: 1 if an unsigned char is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_get_prop_as_uchar (xmlNodePtr node, const char *tag,
				 unsigned char *u)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) tag);
    int ret = 0;

    if (tmp != NULL) {
	*u = (unsigned char) atoi((const char *) tmp);
	free(tmp);
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_get_prop_as_double:
 * @node: XML node pointer.
 * @tag: name by which floating-point property is known.
 * @x: location to write double value.
 * 
 * Returns: 1 if a double is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_get_prop_as_double (xmlNodePtr node, const char *tag,
				  double *x)
{
    char *p, *s = (char *) xmlGetProp(node, (XUC) tag);
    int ret = 0;

    *x = NADBL;

    if (s != NULL) {
	p = s;
	p += strspn(p, " \r\n");
	if (strncmp(p, "NA", 2)) {
	    *x = atof(p);
	}
	free(s);
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_get_prop_as_string:
 * @node: XML node pointer.
 * @tag: name by which string property is known.
 * @pstr: location to assign string.
 * 
 * Returns: 1 if a string is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_get_prop_as_string (xmlNodePtr node, const char *tag,
				  char **pstr)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) tag);
    int ret = 0;

    if (tmp != NULL) {
	*pstr = (char *) tmp;
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_get_prop_as_bool:
 * @node: XML node pointer.
 * @tag: name by which property is known.
 * 
 * Returns: 1 if the named property is found and has value %true,
 * else 0.
 */

int gretl_xml_get_prop_as_bool (xmlNodePtr node, const char *tag)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) tag);
    int ret = 0;

    if (tmp != NULL) {
	if (!strcmp((char *) tmp, "true") || 
	    !strcmp((char *) tmp, "1")) {
	    ret = 1;
	}
	free(tmp);
    }

    return ret;
}

/**
 * gretl_xml_node_get_int:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @i: location to receive integer.
 * 
 * Returns: 1 if an int is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_node_get_int (xmlNodePtr node, xmlDocPtr doc, int *i)
{
    xmlChar *tmp;
    int ret = 0;

    tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);

    if (tmp != NULL) {
	*i = atoi((const char *) tmp);
	free(tmp);
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_node_get_double:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @x: location to receive double.
 * 
 * Returns: 1 if a double is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_node_get_double (xmlNodePtr node, xmlDocPtr doc, 
			       double *x)
{
    char *s, *p;
    int ret = 0;

    s = (char *) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);

    if (s != NULL) {
	p = s;
	p += strspn(p, " \r\n");
	if (!strncmp(p, "NA", 2)) {
	    *x = NADBL;
	} else {
	    *x = atof(p);
	}
	free(s);
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_node_get_string:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @pstr: location to receive string.
 * 
 * Returns: 1 if a string is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_node_get_string (xmlNodePtr node, xmlDocPtr doc, 
			       char **pstr)
{
    xmlChar *tmp;
    int ret = 0;

    tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);

    if (tmp != NULL) {
	*pstr = (char *) tmp;
	ret = 1;
    }

    return ret;
}

/**
 * gretl_xml_node_get_trimmed_string:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @pstr: location to receive string.
 * 
 * Reads a string from @node and trims both leading and trailing
 * white space.
 * 
 * Returns: 1 if a string is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_node_get_trimmed_string (xmlNodePtr node, xmlDocPtr doc, 
				       char **pstr)
{
    char *tmp;
    char *s;
    int i, len, ret = 0;

    tmp = (char *) xmlNodeListGetString(doc, node->xmlChildrenNode, 1);

    if (tmp != NULL) {
	s = tmp;
	s += strspn(s, " \t\n\r");
	len = strlen(s);
	for (i=len-1; i>=0; i--) {
	    if (s[i] == ' ' || s[i] == '\t' || 
		s[i] == '\r' || s[i] == '\n') {
		len--;
	    } else {
		break;
	    }
	}
	if (len == strlen(tmp)) {
	    *pstr = tmp;
	    ret = 1;
	} else if (len > 0) {
	    *pstr = gretl_strndup(s, len);
	    if (*pstr != NULL) {
		ret = 1;
	    }
	    free(tmp);
	}
    }

    return ret;
}

/**
 * gretl_xml_node_get_list:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @err: location to receive error code.
 * 
 * Returns: allocated list read from @node, or %NULL on
 * failure.
 */

int *gretl_xml_node_get_list (xmlNodePtr node, xmlDocPtr doc, int *err)
{
    xmlChar *tmp;
    const char *p;
    int *list = NULL;
    int i, n;

    tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);

    if (tmp == NULL) {
	*err = E_DATA;
    } else {
	p = (const char *) tmp;
	p += strspn(p, " \r\n"); /* skip space (get to first value) */
	if (sscanf(p, "%d", &n) != 1) {
	    *err = E_DATA;
	} else if (n == 0) {
	    free(tmp);
	    return NULL;
	} else if (n < 0) {
	    *err = E_DATA;
	} else {
	    p += strcspn(p, " \r\n"); /* skip non-space (get beyond value) */
	    list = gretl_list_new(n);
	    if (list == NULL) {
		*err = E_ALLOC;
	    }
	}

	if (list != NULL && !*err) {
	    for (i=1; i<=n && !*err; i++) {
		p += strspn(p, " \r\n"); /* skip space (get to next value) */
		if (*p == ';') {
		    list[i] = LISTSEP;
		} else if (sscanf(p, "%d", &list[i]) != 1) {
		    *err = E_DATA;
		}
		p += strcspn(p, " \r\n"); /* skip non-space (get beyond value) */
	    }
	}

	free(tmp);
    }

    if (list != NULL && *err) {
	free(list);
	list = NULL;
    }

    fprintf(stderr, "returning list = %p\n", (void *) list);

    return list;
}

/**
 * gretl_xml_child_get_string:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @name: name of child node.
 * @pstr: location to receive string.
 * 
 * Returns: 1 if a string is found and read successfully, 0
 * otherwise.
 */

int gretl_xml_child_get_string (xmlNodePtr node, xmlDocPtr doc, 
				const char *name, char **pstr)
{
    xmlNodePtr cur;
    xmlChar *tmp;
    int ret = 0;

    *pstr = NULL;

    cur = node->xmlChildrenNode;

    while (cur != NULL) {
	if (!xmlStrcmp(cur->name, (XUC) name)) {
	    tmp = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    if (tmp != NULL) {
		*pstr = (char *) tmp;
		ret = 1;
	    }
	    break;
	}
	cur = cur->next;
    }

    return ret;
}

static void *gretl_xml_get_array (xmlNodePtr node, xmlDocPtr doc,
				  GretlType type,
				  int *nelem, int *err)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) "count");
    int *ivals = NULL;
    double *xvals = NULL;
    cmplx *cvals = NULL;
    void *ptr = NULL;
    int nread = 0;
    int i, n = 0;

    *nelem = 0;

    if (tmp == NULL) {
	fprintf(stderr, "gretl_xml_get_array: failed\n");
	*err = E_DATA;
	return NULL;
    }

    n = atoi((const char *) tmp);
    free(tmp);

    if (n <= 0) {
	return NULL;
    }    

    if (type == GRETL_TYPE_INT_ARRAY) {
	ivals = malloc(n * sizeof *ivals);
	ptr = ivals;
    } else if (type == GRETL_TYPE_DOUBLE_ARRAY) {
	xvals = malloc(n * sizeof *xvals);
	ptr = xvals;
    } else if (type == GRETL_TYPE_CMPLX_ARRAY) {
	cvals = malloc(n * sizeof *cvals);
	ptr = cvals;
    }

    if (ptr == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);

    if (tmp == NULL) {
	*err = E_DATA;
    } else {
	const char *s = (const char *) tmp;
	char *test;

	errno = 0;
	
	if (type == GRETL_TYPE_DOUBLE_ARRAY) {
	    double x;

	    for (i=0; i<n && !*err && *s; i++) {
		while (isspace(*s)) s++;
		x = strtod(s, &test);
		if (errno) {
		    fprintf(stderr, "strtod failed on '%s'\n", s);
		    perror(NULL);
		    *err = E_DATA;
		} else if (!strncmp(test, "NA", 2)) {
		    x = NADBL;
		    s = test + 2;
		} else if (*test != '\0' && !isspace(*test)) {
		    *err = E_DATA;
		} else {
		    s = test;
		}
		xvals[i] = x;
		nread++;
	    }
	} if (type == GRETL_TYPE_INT_ARRAY) {
	    long kl;

	    for (i=0; i<n && !*err && *s; i++) {
		while (isspace(*s)) s++;
		kl = strtol(s, &test, 10);
		if (errno) {
		    *err = E_DATA;
		} else if (*test != '\0' && !isspace(*test)) {
		    *err = E_DATA;
		} else {
		    s = test;
		    ivals[i] = kl;
		    nread++;
		}
	    }
	} else if (type == GRETL_TYPE_CMPLX_ARRAY) { 
	    double x;
	    int n2 = n * 2;
	    int rval = 1;

	    for (i=0; i<n2 && !*err && *s; i++) {
		while (isspace(*s)) s++;
		x = strtod(s, &test);
		if (errno) {
		    *err = E_DATA;
		} else if (*test != '\0' && !isspace(*test)) {
		    *err = E_DATA;
		} else {
		    s = test;
		    if (rval) {
			cvals[nread].r = x;
			rval = 0;
		    } else {
			cvals[nread].i = x;
			rval = 1;
			nread++;
		    }
		}
	    }
	}   

	free(tmp);

	if (nread < n) {
	    fprintf(stderr, "expected %d items in array, but got %d\n", n, nread);
	    *err = E_DATA;
	}
    }

    if (ptr != NULL && *err) {
	free(ptr);
	ptr = NULL;
    }

    if (!*err) {
	*nelem = n;
    }

    return ptr;
}

/**
 * gretl_xml_get_int_array:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @nelem: location to receive number of elements in array.
 * @err: location to receive error code.
 * 
 * Returns: allocated array of integers read from @node, or %NULL on
 * failure.
 */

int *gretl_xml_get_int_array (xmlNodePtr node, xmlDocPtr doc,
			      int *nelem, int *err)
{
    return gretl_xml_get_array(node, doc, GRETL_TYPE_INT_ARRAY,
			       nelem, err);
}

/**
 * gretl_xml_get_double_array:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @nelem: location to receive number of elements in array.
 * @err: location to receive error code.
 * 
 * Returns: allocated array of doubles read from @node, or %NULL on
 * failure.
 */

double *gretl_xml_get_double_array (xmlNodePtr node, xmlDocPtr doc,
				    int *nelem, int *err)
{
    int myerr = 0;

    if (err == NULL) {
	err = &myerr;
    }
	
    return gretl_xml_get_array(node, doc, GRETL_TYPE_DOUBLE_ARRAY,
			       nelem, err);
}

/**
 * gretl_xml_get_cmplx_array:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @nelem: location to receive number of elements in array.
 * @err: location to receive error code.
 * 
 * Returns: allocated array of cmplx (complex numbers) read from 
 * @node, or %NULL on failure.
 */

cmplx *gretl_xml_get_cmplx_array (xmlNodePtr node, xmlDocPtr doc,
				  int *nelem, int *err)
{
    return gretl_xml_get_array(node, doc, GRETL_TYPE_CMPLX_ARRAY,
			       nelem, err);
}

static char *chunk_strdup (const char *src, const char **ptr, int *err)
{
    char *targ = NULL;

    if (*src == '\0') {
	*ptr = src;
    } else {
	const char *p;
	int len = 0;

	while (*src && isspace(*src)) {
	    src++;
	}

	p = src;

	while (*src && !isspace(*src)) {
	    len++;
	    src++;
	}

	if (ptr != NULL) {
	    *ptr = src;
	}

	if (len > 0) {
	    targ = gretl_strndup(p, len);
	    if (targ == NULL) {
		*err = E_ALLOC;
	    }
	}
    }

    if (targ == NULL && !*err) {
	*err = E_DATA;
    }

    return targ;
}

/**
 * gretl_xml_get_strings_array:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @nelem: location to receive number of elements in array.
 * @slop: if non-zero, allow the number of strings to fall
 * short of the recorded string count by one.
 * @err: location to receive error code.
 * 
 * Returns: allocated array of strings read from @node, or 
 * %NULL on failure.
 */

char **gretl_xml_get_strings_array (xmlNodePtr node, xmlDocPtr doc,
				    int *nelem, int slop, int *err)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) "count");
    char **S = NULL;
    const char *p;
    int i, n = 0;

    if (tmp == NULL) {
	*err = E_DATA;
	return NULL;
    }

    n = atoi((const char *) tmp);
    free(tmp);

    if (n > 0) {
	S = strings_array_new(n);
	if (S == NULL) {
	    *err = E_ALLOC;
	} else {
	    tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
	    if (tmp == NULL) {
		*err = E_DATA;
	    } else {
		p = (const char *) tmp;
		for (i=0; i<n && !*err; i++) {
		    S[i] = chunk_strdup(p, &p, err);
		    if (*err == E_DATA && i == n - 1 && slop) {
			*err = 0;
			n--;
		    }
		}
		free(tmp);
	    }
	}
    }

    if (S != NULL && *err) {
	free_strings_array(S, n);
	S = NULL;
    }

    if (!*err) {
	*nelem = n;
    }

    return S;
}

/**
 * xml_get_user_matrix:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @colnames: location to receive column strings, if present.
 * @err: location to receive error code.
 * 
 * Returns: allocated gretl matrix read from @node, or %NULL 
 * on failure.
 */

static gretl_matrix *xml_get_user_matrix (xmlNodePtr node, xmlDocPtr doc, 
					  char **colnames, int *err)
{
    gretl_matrix *m = NULL;
    xmlChar *tmp;
    const char *p;
    double x;
    int rows, cols;
    int t1 = 0, t2 = 0;
    int i, j;

    tmp = xmlGetProp(node, (XUC) "rows");
    if (tmp == NULL) {
	*err = E_DATA;
	return NULL;
    }

    if (sscanf((const char *) tmp, "%d", &rows) != 1) {
	free(tmp);
	*err = E_DATA;
	return NULL;
    }

    free(tmp);

    tmp = xmlGetProp(node, (XUC) "cols");
    if (tmp == NULL) {
	*err = E_DATA;
	return NULL;
    }

    if (sscanf((const char *) tmp, "%d", &cols) != 1) {
	free(tmp);
	*err = E_DATA;
	return NULL;
    }

    free(tmp);

    if (rows <= 0 || cols <= 0) {
	*err = E_DATA;
	return NULL;
    }

    tmp = xmlGetProp(node, (XUC) "t1");
    if (tmp != NULL) {
	t1 = atoi((char *) tmp);
	free(tmp);
    }

    tmp = xmlGetProp(node, (XUC) "t2");
    if (tmp != NULL) {
	t2 = atoi((char *) tmp);
	free(tmp);
    }

    if (colnames != NULL) {
	*colnames = (char *) xmlGetProp(node, (XUC) "colnames");
    }

    m = gretl_matrix_alloc(rows, cols);
    if (m == NULL) {
	*err = E_ALLOC;
	return NULL;
    }

    tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (tmp == NULL) {
	gretl_matrix_free(m);
	*err = E_DATA;
	return NULL;
    }

    p = (const char *) tmp;
    p += strspn(p, " \r\n");

    gretl_push_c_numeric_locale();

    for (i=0; i<rows && !*err; i++) {
	for (j=0; j<cols && !*err; j++) {
	    if (sscanf(p, "%lf", &x) != 1) {
		*err = E_DATA;
		break;
	    } else {
		gretl_matrix_set(m, i, j, x);
		p += strspn(p, " \r\n");
		p += strcspn(p, " \r\n");
	    }
	}
    }

    gretl_pop_c_numeric_locale();

    free(tmp);

    if (*err) {
	gretl_matrix_free(m);
	m = NULL;
    } else {
	m->t1 = t1;
	m->t2 = t2;
    }

    return m;
}

/**
 * gretl_xml_get_matrix:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @err: location to receive error code.
 * 
 * Returns: allocated gretl matrix read from @node, or %NULL 
 * on failure.
 */

gretl_matrix *gretl_xml_get_matrix (xmlNodePtr node, xmlDocPtr doc, 
				    int *err)
{
    return xml_get_user_matrix(node, doc, NULL, err);
}

/**
 * gretl_xml_get_submask:
 * @node: XML node pointer.
 * @doc: XML document pointer.
 * @pmask: location to receive allocated mask.
 * 
 * Returns: 0 on success, non-zero on failure.
 */

int gretl_xml_get_submask (xmlNodePtr node, xmlDocPtr doc, char **pmask)
{
    char *mask = NULL;
    int i, len;
    int err = 0;

    if (!gretl_xml_get_prop_as_int(node, "length", &len)) {
	return 1;
    }

    if (len == 0) {
	*pmask = RESAMPLED;
	return 0;
    }

    mask = calloc(len, 1);

    if (mask == NULL) {
	err = 1;
    } else {
	xmlChar *tmp = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
	if (tmp == NULL) {
	    err = 1;
	} else {
	    const char *s = (const char *) tmp;
	    int si;

	    for (i=0; i<len; i++) {
		sscanf(s, "%d", &si);
		s += strspn(s, " ");
		s += strcspn(s, " ");
		if (si != 0) {
		    mask[i] = si;
		}
	    }
	    free(tmp);
	}
    }

    if (!err) {
	*pmask = mask;
    }

    return err;
}

void gretl_xml_header (FILE *fp)
{
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", fp);
}

static int real_balanced_panel (const DATAINFO *pdinfo)
{
    const PANINFO *pan = pdinfo->paninfo;
    int *test;
    int i, s, t, T;
    int bal = 1;

    if (pan->Tmin != pan->Tmax) {
	return 0;
    }

    T = pan->Tmin;
	
    test = malloc(T * sizeof *test);
    if (test == NULL) {
	return 0;
    }

    for (i=0; i<T; i++) {
	test[i] = pan->period[i];
    }

    s = T;
    for (i=1; i<pan->nunits && bal; i++) {
	for (t=0; t<T && bal; t++) {
	    if (pan->period[s++] != test[t]) {
		bal = 0;
	    }
	}
    }
	
    free(test);

#if 0
    fprintf(stderr, "full balance test: bal = %d\n", bal);
#endif

    return bal;
}

/* should we print unit and period info for each observation
   in a panel dataset? */

static int query_print_panel_obs (const DATAINFO *pdinfo)
{
    int bal;

    if (pdinfo->paninfo == NULL ||
	pdinfo->paninfo->unit == NULL ||
	pdinfo->paninfo->period == NULL) {
	return 0;
    }

    /* printing panel obs info is redundant if the panel
       is perfectly balanced */
    bal = real_balanced_panel(pdinfo);

    return !bal;
}

/**
 * gretl_write_matrix_as_gdt:
 * @fname: name of file to write.
 * @X: matrix, variable in columns.
 * @varnames: column names.
 * @labels: descriptive labels for the variables, or %NULL.
 * 
 * Write out a .gdt data file containing the elements of
 * of the given matrix.
 * 
 * Returns: 0 on successful completion, non-zero on error.
 */

int gretl_write_matrix_as_gdt (const char *fname, 
			       const gretl_matrix *X,
			       const char **varnames, 
			       const char **labels)
{
    gzFile *fz = Z_NULL;
    char datname[MAXLEN];
    void *handle = NULL;
    char *xmlbuf = NULL;
    int (*show_progress) (long, long, int) = NULL;
    long sz = 0L;
    int T = X->rows;
    int k = X->cols;
    int i, t, err = 0;

    fz = gretl_gzopen(fname, "wb");

    if (fz == Z_NULL) {
	gretl_errmsg_sprintf(_("Couldn't open %s for writing"), fname);
	return 1;
    }

    sz = (T * k * sizeof(double));
    if (sz > 100000) {
	fprintf(stderr, I_("Writing %ld Kbytes of data\n"), sz / 1024);
    } else {
	sz = 0L;
    }

    if (sz) {
	show_progress = get_plugin_function("show_progress", &handle);
	if (show_progress == NULL) {
	    sz = 0L;
	}
    }

    if (sz) (*show_progress)(0, sz, SP_SAVE_INIT); 

    simple_fname(datname, fname);
    xmlbuf = gretl_xml_encode(datname);
    if (xmlbuf == NULL) {
	err = 1;
	goto cleanup;
    }

    gzprintf(fz, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	     "<!DOCTYPE gretldata SYSTEM \"gretldata.dtd\">\n\n"
	     "<gretldata version=\"%s\" name=\"%s\" frequency=\"1\" "
	     "startobs=\"1\" endobs=\"%d\" type=\"cross-section\">\n", 
	     GRETLDATA_VERSION, datname, T);

    free(xmlbuf);

    gretl_push_c_numeric_locale();

    gzprintf(fz, "<variables count=\"%d\">\n", k);

    for (i=0; i<k; i++) {
	gzprintf(fz, "<variable name=\"%s\"", varnames[i]);
	if (labels != NULL && labels[i] != NULL) {
	    gzprintf(fz, "\n label=\"%s\"", labels[i]);
	}
	gzputs(fz, "\n/>\n");
    }

    gzputs(fz, "</variables>\n");

    gzprintf(fz, "<observations count=\"%d\" labels=\"false\">\n", T);

    for (t=0; t<T; t++) {
	gzputs(fz, "<obs>");
	for (i=0; i<k; i++) {
	    gzprintf(fz, "%.12g ", gretl_matrix_get(X, t, i));
	}
	gzputs(fz, "</obs>\n");
	if (sz && t && (t % 50 == 0)) { 
	    (*show_progress) (50, T, SP_NONE);
	}
    }

    gzputs(fz, "</observations>\n</gretldata>\n");

 cleanup: 

    gretl_pop_c_numeric_locale();

    if (sz) {
	(*show_progress)(0, T, SP_FINISH);
	close_plugin(handle);
    } 

    gzclose(fz);

    return err;
}

/**
 * gretl_write_gdt:
 * @fname: name of file to write.
 * @list: list of variables to write (or %NULL to write all).
 * @Z: data matrix.
 * @pdinfo: data information struct.
 * @opt: if %OPT_Z write gzipped data, else uncompressed.
 * @progress: may be 1 when called from gui to display progress
 * bar in case of a large data write; generally should be 0.
 * 
 * Write out in xml a data file containing the values of the given set
 * of variables.
 * 
 * Returns: 0 on successful completion, non-zero on error.
 */

int gretl_write_gdt (const char *fname, const int *list, 
		     const double **Z, const DATAINFO *pdinfo, 
		     gretlopt opt, int progress)
{
    FILE *fp = NULL;
    gzFile *fz = Z_NULL;
    int gz = (opt & OPT_Z);
    int tsamp = pdinfo->t2 - pdinfo->t1 + 1;
    int panelobs = 0;
    int *pmax = NULL;
    char startdate[OBSLEN], enddate[OBSLEN];
    char datname[MAXLEN], freqstr[32];
    char numstr[128];
    char xmlbuf[256];
    void *handle = NULL;
    int (*show_progress) (long, long, int) = NULL;
    long sz = 0L;
    int i, t, v, nvars;
    int uerr = 0;
    int err = 0;

    if (gz) {
	fz = gretl_gzopen(fname, "wb");
	if (fz == Z_NULL) err = 1;
    } else {
	fp = gretl_fopen(fname, "wb");
	if (fp == NULL) err = 1;
    }

    if (err) {
	gretl_errmsg_sprintf(_("Couldn't open %s for writing"), fname);
	return 1;
    }

    if (list != NULL) {
	nvars = list[0];
    } else {
	nvars = pdinfo->v - 1;
    }

    pmax = malloc(nvars * sizeof *pmax);
    if (pmax == NULL) {
	err = E_ALLOC;
	goto cleanup;
    } 

    sz = (tsamp * nvars * sizeof(double));
    if (sz > 100000) {
	fprintf(stderr, I_("Writing %ld Kbytes of data\n"), sz / 1024);
	if (!progress) {
	    sz = 0L;
	}
    } else {
	sz = 0L;
    }

    if (sz) {
	show_progress = get_plugin_function("show_progress", &handle);
	if (show_progress == NULL) {
	    sz = 0L;
	}
    }

    if (sz) (*show_progress)(0, sz, SP_SAVE_INIT); 

    for (i=1; i<=nvars; i++) {
	v = savenum(list, i);
	pmax[i-1] = get_precision(&Z[v][pdinfo->t1], tsamp, 10);
    }

    ntodate(startdate, pdinfo->t1, pdinfo);
    ntodate(enddate, pdinfo->t2, pdinfo);

    simple_fname(datname, fname);
    uerr = gretl_xml_encode_to_buf(xmlbuf, datname, sizeof xmlbuf);
    if (uerr) {
	strcpy(xmlbuf, "unknown");
    }

    if (custom_time_series(pdinfo)) {
	sprintf(freqstr, "special:%d", pdinfo->pd);
    } else {
	sprintf(freqstr, "%d", pdinfo->pd);
    }

    if (gz) {
	gzprintf(fz, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		 "<!DOCTYPE gretldata SYSTEM \"gretldata.dtd\">\n\n"
		 "<gretldata version=\"%s\" name=\"%s\" frequency=\"%s\" "
		 "startobs=\"%s\" endobs=\"%s\" ", 
		 GRETLDATA_VERSION, xmlbuf, freqstr, startdate, enddate);
    } else {
	fprintf(fp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE gretldata SYSTEM \"gretldata.dtd\">\n\n"
		"<gretldata version=\"%s\" name=\"%s\" frequency=\"%s\" "
		"startobs=\"%s\" endobs=\"%s\" ", 
		GRETLDATA_VERSION, xmlbuf, freqstr, startdate, enddate);
    }

    if (gz) {
	gzprintf(fz, "type=\"%s\">\n", data_structure_string(pdinfo->structure));
    } else {
	fprintf(fp, "type=\"%s\">\n", data_structure_string(pdinfo->structure));
    }

    /* deal with description, if any */
    if (pdinfo->descrip != NULL) {
	char *dbuf = gretl_xml_encode(pdinfo->descrip);

	if (dbuf == NULL) {
	    err = 1;
	    goto cleanup;
	} else {
	    if (gz) {
		gzputs(fz, "<description>");
		gzputs(fz, dbuf);
		gzputs(fz, "</description>\n");
	    } else {
		fprintf(fp, "<description>%s</description>\n", dbuf);
	    }
	    free(dbuf);
	}
    }

    gretl_push_c_numeric_locale();

    /* then listing of variable names and labels */
    if (gz) {
	gzprintf(fz, "<variables count=\"%d\">\n", nvars);
    } else {
	fprintf(fp, "<variables count=\"%d\">\n", nvars);
    }

    for (i=1; i<=nvars; i++) {
	v = savenum(list, i);
	gretl_xml_encode_to_buf(xmlbuf, pdinfo->varname[v], sizeof xmlbuf);

	if (gz) {
	    gzprintf(fz, "<variable name=\"%s\"", xmlbuf);
	} else {
	    fprintf(fp, "<variable name=\"%s\"", xmlbuf);
	}

	if (*VARLABEL(pdinfo, v)) {
	    uerr = gretl_xml_encode_to_buf(xmlbuf, VARLABEL(pdinfo, v), sizeof xmlbuf);
	    if (!uerr) {
		if (gz) {
		    gzprintf(fz, "\n label=\"%s\"", xmlbuf);
		} else {
		    fprintf(fp, "\n label=\"%s\"", xmlbuf);
		}
	    }
	} 

	if (*DISPLAYNAME(pdinfo, v)) {
	    uerr = gretl_xml_encode_to_buf(xmlbuf, DISPLAYNAME(pdinfo, v), sizeof xmlbuf);
	    if (!uerr) {
		if (gz) {
		    gzprintf(fz, "\n displayname=\"%s\"", xmlbuf);
		} else {
		    fprintf(fp, "\n displayname=\"%s\"", xmlbuf);
		}
	    }
	}

	if (*PARENT(pdinfo, v)) {
	    uerr = gretl_xml_encode_to_buf(xmlbuf, PARENT(pdinfo, v), sizeof xmlbuf);
	    if (!uerr) {
		if (gz) {
		    gzprintf(fz, "\n parent=\"%s\"", xmlbuf);
		} else {
		    fprintf(fp, "\n parent=\"%s\"", xmlbuf);
		}
	    }
	}

	if (pdinfo->varinfo[i]->transform != 0) {
	    const char *tr = gretl_command_word(pdinfo->varinfo[i]->transform);

	    if (gz) {
		gzprintf(fz, "\n transform=\"%s\"", tr);
	    } else {
		fprintf(fp, "\n transform=\"%s\"", tr);
	    }
	}

	if (pdinfo->varinfo[i]->lag != 0) {
	    if (gz) {
		gzprintf(fz, "\n lag=\"%d\"", pdinfo->varinfo[i]->lag);
	    } else {
		fprintf(fp, "\n lag=\"%d\"", pdinfo->varinfo[i]->lag);
	    }
	}	    

	if (COMPACT_METHOD(pdinfo, v) != COMPACT_NONE) {
	    const char *meth = compact_method_to_string(COMPACT_METHOD(pdinfo, v));

	    if (gz) {
		gzprintf(fz, "\n compact-method=\"%s\"", meth);
	    } else {
		fprintf(fp, "\n compact-method=\"%s\"", meth);
	    }
	} 

	if (var_is_discrete(pdinfo, v)) {
	    alt_puts("\n discrete=\"true\"", fp, fz);
	}	    

	alt_puts("\n/>\n", fp, fz);
    }

    alt_puts("</variables>\n", fp, fz);

    panelobs = query_print_panel_obs(pdinfo);

    /* then listing of observations */
    alt_puts("<observations ", fp, fz);
    if (gz) {
	gzprintf(fz, "count=\"%d\" labels=\"%s\"",
		tsamp, (pdinfo->markers && pdinfo->S != NULL)? "true" : "false");
    } else {
	fprintf(fp, "count=\"%d\" labels=\"%s\"",
		tsamp, (pdinfo->markers && pdinfo->S != NULL)? "true" : "false");
    }
    if (panelobs) {
	alt_puts(" panel-info=\"true\"", fp, fz);
    }
    alt_puts(">\n", fp, fz);

    for (t=pdinfo->t1; t<=pdinfo->t2; t++) {
	alt_puts("<obs", fp, fz);
	if (pdinfo->markers && pdinfo->S != NULL) {
	    uerr = gretl_xml_encode_to_buf(xmlbuf, pdinfo->S[t], sizeof xmlbuf);
	    if (!uerr) {
		if (gz) {
		    gzprintf(fz, " label=\"%s\"", xmlbuf);
		} else {
		    fprintf(fp, " label=\"%s\"", xmlbuf);
		}
	    }
	} 
	if (panelobs) {
	    if (gz) {
		gzprintf(fz, " unit=\"%d\" period=\"%d\"", 
			 pdinfo->paninfo->unit[t], 
			 pdinfo->paninfo->period[t]);
	    } else {
		fprintf(fp, " unit=\"%d\" period=\"%d\"", 
			pdinfo->paninfo->unit[t], 
			pdinfo->paninfo->period[t]);
	    }	    
	}
	alt_puts(">", fp, fz);
	for (i=1; i<=nvars; i++) {
	    v = savenum(list, i);
	    if (na(Z[v][t])) {
		strcpy(numstr, "NA ");
	    } else if (pmax[i-1] == PMAX_NOT_AVAILABLE) {
		sprintf(numstr, "%.12g ", Z[v][t]);
	    } else {
		sprintf(numstr, "%.*f ", pmax[i-1], Z[v][t]);
	    }
	    alt_puts(numstr, fp, fz);
	}

	alt_puts("</obs>\n", fp, fz);

	if (sz && t && ((t - pdinfo->t1) % 50 == 0)) { 
	    (*show_progress) (50, tsamp, SP_NONE);
	}
    }

    alt_puts("</observations>\n</gretldata>\n", fp, fz);

 cleanup: 

    gretl_pop_c_numeric_locale();

    if (sz) {
	(*show_progress)(0, pdinfo->t2 - pdinfo->t1 + 1, SP_FINISH);
	close_plugin(handle);
    } 

    if (pmax) free(pmax);
    if (fp != NULL) fclose(fp);
    if (fz != Z_NULL) gzclose(fz);

    return err;
}

static void transcribe_string (char *targ, const char *src, int maxlen)
{
    *targ = '\0';

    strncat(targ, src, maxlen - 1);
}

static int process_varlist (xmlNodePtr node, DATAINFO *pdinfo, double ***pZ)
{
    xmlNodePtr cur;
    xmlChar *tmp = xmlGetProp(node, (XUC) "count");
    int i, err = 0;

    if (tmp != NULL) {
	int v;

	if (sscanf((char *) tmp, "%d", &v) == 1) {
	    pdinfo->v = v + 1;
	} else {
	    gretl_errmsg_set(_("Failed to parse count of variables"));
	    err = 1;
	}
	if (!err && dataset_allocate_varnames(pdinfo)) {
	    err = E_ALLOC;
	}
	if (!err) {
	    *pZ = malloc(pdinfo->v * sizeof **pZ);
	    if (*pZ == NULL) {
		err = E_ALLOC;
	    }
	}		
	free(tmp);
    } else {
	gretl_errmsg_set(_("Got no variables"));
	err = 1;
    }

    if (err) return 1;

    /* now get individual variable info: names and labels */
    cur = node->xmlChildrenNode;
    while (cur && xmlIsBlankNode(cur)) {
	cur = cur->next;
    }

    if (cur == 0) {
	gretl_errmsg_set(_("Got no variables"));
	return 1;
    }

    i = 1;
    while (cur != NULL) {
        if (!xmlStrcmp(cur->name, (XUC) "variable")) {
	    tmp = xmlGetProp(cur, (XUC) "name");
	    if (tmp != NULL) {
		transcribe_string(pdinfo->varname[i], (char *) tmp, VNAMELEN);
		free(tmp);
	    } else {
		gretl_errmsg_sprintf(_("Variable %d has no name"), i);
		return 1;
	    }
	    tmp = xmlGetProp(cur, (XUC) "label");
	    if (tmp != NULL) {
		transcribe_string(VARLABEL(pdinfo, i), (char *) tmp, MAXLABEL);
		free(tmp);
	    }
	    tmp = xmlGetProp(cur, (XUC) "displayname");
	    if (tmp != NULL) {
		var_set_display_name(pdinfo, i, (char *) tmp);
		free(tmp);
	    }
	    tmp = xmlGetProp(cur, (XUC) "parent");
	    if (tmp != NULL) {
		strcpy(pdinfo->varinfo[i]->parent, (char *) tmp); 
		free(tmp);
	    }
	    tmp = xmlGetProp(cur, (XUC) "transform");
	    if (tmp != NULL) {
		int ci = gretl_command_number((char *) tmp);

		pdinfo->varinfo[i]->transform = ci; 
		free(tmp);
	    }
	    tmp = xmlGetProp(cur, (XUC) "lag");
	    if (tmp != NULL) {
		pdinfo->varinfo[i]->lag = atoi((char *) tmp); 
		free(tmp);
	    }

	    tmp = xmlGetProp(cur, (XUC) "compact-method");
	    if (tmp != NULL) {
		COMPACT_METHOD(pdinfo, i) = compact_string_to_int((char *) tmp);
		free(tmp);
	    }

	    tmp = xmlGetProp(cur, (XUC) "discrete");
	    if (tmp != NULL) {
		if (!strcmp((char *) tmp, "true")) {
		    series_set_flag(pdinfo, i, VAR_DISCRETE);
		}
		free(tmp);
	    }
	    tmp = xmlGetProp(cur, (XUC) "role");
	    if (tmp != NULL) {
#if 0 /* FIXME old datafiles? */
		if (!strcmp((char *) tmp, "scalar")) {
		    char *val = (char *) xmlGetProp(cur, (XUC) "value");
		    
		    if (val) {
			double xx = atof(val);

			free(val);
			(*pZ)[i] = malloc(sizeof ***pZ);
			(*pZ)[i][0] = xx;
			set_var_scalar(pdinfo, i, 1);
		    }
		}
#endif
		free(tmp);
	    }
	    i++;
	}	    
	cur = cur->next;
    }
   
    if (i != pdinfo->v) {
	gretl_errmsg_set(_("Number of variables does not match declaration"));
	err = 1;
    } 

    return err;
}

static int process_values (double **Z, DATAINFO *pdinfo, int t, char *s)
{
    char *test;
    double x;
    int i, err = 0;

    gretl_error_clear();

    for (i=1; i<pdinfo->v && !err; i++) {
	while (isspace(*s)) s++;
	x = strtod(s, &test);
	if (errno) {
	    err = 1;
	} else if (!strncmp(test, "NA", 2)) {
	    x = NADBL;
	    s = test + 2;
	} else if (*test != '\0' && !isspace(*test)) {
	    err = 1;
	} else {
	    s = test;
	}
	if (t < pdinfo->n) {
	    Z[i][t] = x;
	}
    }	

    if (err && !gretl_errmsg_is_set()) {
	gretl_errmsg_sprintf(_("Failed to parse data values at obs %d"), t+1);
    }

    return err;
}

#define GDT_DEBUG 0

static int process_observations (xmlDocPtr doc, xmlNodePtr node, 
				 double ***pZ, DATAINFO *pdinfo,
				 long progress)
{
    xmlNodePtr cur;
    xmlChar *tmp;
    int panelobs = 0;
    int n, i, t;
    void *handle;
    int (*show_progress) (long, long, int) = NULL;
    int err = 0;

    tmp = xmlGetProp(node, (XUC) "count");
    if (tmp == NULL) {
	return E_DATA;
    } 

    if (sscanf((char *) tmp, "%d", &n) == 1) {
	pdinfo->n = n;
	free(tmp);
    } else {
	gretl_errmsg_set(_("Failed to parse number of observations"));
	free(tmp);
	return E_DATA;
    }

    if (progress > 0) {
	show_progress = get_plugin_function("show_progress", &handle);
	if (show_progress == NULL) {
	    progress = 0L;
	}
    }

    tmp = xmlGetProp(node, (XUC) "labels");
    if (tmp) {
	if (!strcmp((char *) tmp, "true")) {
	    if (dataset_allocate_obs_markers(pdinfo)) {
		return E_ALLOC;
	    }
	} else if (strcmp((char *) tmp, "false")) {
	    gretl_errmsg_set(_("labels attribute for observations must be "
			       "'true' or 'false'"));
	    return E_DATA;
	}
	free(tmp);
    } else {
	return E_DATA;
    }

    tmp = xmlGetProp(node, (XUC) "panel-info");
    if (tmp) {
	if (!strcmp((char *) tmp, "true")) {
	    err = dataset_allocate_panel_info(pdinfo);
	    if (err) {
		return err;
	    }
	    panelobs = 1;
	} 
	free(tmp);
    } 

    if (pdinfo->endobs[0] == '\0') {
	sprintf(pdinfo->endobs, "%d", pdinfo->n);
    }

    pdinfo->t2 = pdinfo->n - 1;

    for (i=0; i<pdinfo->v; i++) {
	(*pZ)[i] = malloc(pdinfo->n * sizeof ***pZ);
	if ((*pZ)[i] == NULL) {
	    return E_ALLOC;
	}
    }

    for (t=0; t<pdinfo->n; t++) {
	(*pZ)[0][t] = 1.0;
    }

    /* now get individual obs info: labels and values */
    cur = node->xmlChildrenNode;
    while (cur && xmlIsBlankNode(cur)) {
	cur = cur->next;
    }

    if (cur == NULL) {
	gretl_errmsg_set(_("Got no observations\n"));
	return E_DATA;
    }

    if (progress) {
	(*show_progress)(0L, progress, SP_LOAD_INIT);
#if GDT_DEBUG
	fprintf(stderr, "process_observations: inited progess bar (n=%d)\n",
		pdinfo->n);
#endif
    }

    t = 0;
    while (cur != NULL) {
        if (!xmlStrcmp(cur->name, (XUC) "obs")) {

	    if (pdinfo->markers) {
		tmp = xmlGetProp(cur, (XUC) "label");
		if (tmp) {
		    transcribe_string(pdinfo->S[t], (char *) tmp, OBSLEN);
		    free(tmp);
		} else {
		    gretl_errmsg_sprintf(_("Case marker missing at obs %d"), t+1);
		    return E_DATA;
		}
	    }

	    if (panelobs) {
		int j, s, ok = 0;

		tmp = xmlGetProp(cur, (XUC) "unit");
		if (tmp) {
		    ok += sscanf((char *) tmp, "%d", &j);
		    free(tmp);
		} 
		tmp = xmlGetProp(cur, (XUC) "period");
		if (tmp) {
		    ok += sscanf((char *) tmp, "%d", &s);
		    free(tmp);
		} 
		if (ok < 2) {
		    gretl_errmsg_sprintf("Panel index missing at obs %d", t+1);
		    return E_DATA;
		}
		pdinfo->paninfo->unit[t] = j;
		pdinfo->paninfo->period[t] = s;
	    }

	    tmp = xmlNodeListGetRawString(doc, cur->xmlChildrenNode, 1);

	    if (tmp) {
		if (process_values(*pZ, pdinfo, t, (char *) tmp)) {
		    return 1;
		}
		free(tmp);
		t++;
	    } else {
		gretl_errmsg_sprintf(_("Values missing at observation %d"), t+1);
		err = E_DATA;
		goto bailout;
	    }
	}	   
 
	cur = cur->next;

	if (cur != NULL && t == pdinfo->n) {
	    /* got too many observations */
	    t = pdinfo->n + 1;
	    goto bailout;
	}

	if (progress && t > 0 && t % 50 == 0) {
	    (*show_progress) (50L, (long) pdinfo->n, SP_NONE);
	}
    }

 bailout:

    if (progress) {
#if GDT_DEBUG
	fprintf(stderr, "finalizing progress bar (n = %d)\n", pdinfo->n);
#endif
	(*show_progress)(0L, (long) pdinfo->n, SP_FINISH);
	close_plugin(handle);
    }

    if (!err && t != pdinfo->n) {
	gretl_errmsg_set(_("Number of observations does not match declaration"));
	err = E_DATA;
    }

    return err;
}

static double get_gdt_version (xmlNodePtr node)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) "version");
    double v = 1.0;

    if (tmp != NULL) {
	v = dot_atof((char *) tmp);
	free(tmp);
    }

    return v;
}

static int xml_get_data_structure (xmlNodePtr node, int *dtype)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) "type");
    int err = 0;

    if (tmp == NULL) {
	gretl_errmsg_set(_("Required attribute 'type' is missing from data file"));
	err = 1;
    } else {
	if (!strcmp((char *) tmp, "cross-section")) {
	    *dtype = CROSS_SECTION;
	} else if (!strcmp((char *) tmp, "time-series")) {
	    *dtype = TIME_SERIES;
	} else if (!strcmp((char *) tmp, "stacked-time-series")) {
	    *dtype = STACKED_TIME_SERIES;
	} else if (!strcmp((char *) tmp, "stacked-cross-section")) {
	    *dtype = STACKED_CROSS_SECTION;
	} else {
	    gretl_errmsg_set(_("Unrecognized type attribute for data file"));
	    err = 1;
	}
	free(tmp);
    }

    return err;
}

static int xml_get_data_frequency (xmlNodePtr node, int *pd, int *dtype)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) "frequency");
    int err = 0;

    *pd = 1;

    if (tmp != NULL) {
	if (!strncmp((char *) tmp, "special", 7)) {
	    *dtype = SPECIAL_TIME_SERIES;
	    if (sscanf((char *) tmp + 7, ":%d", pd) == 1) {
		fprintf(stderr, "custom time series, frequency %d\n", *pd);
	    } else {
		fprintf(stderr, "custom time series, using frequency 1\n");
	    }
	} else if (sscanf((char *) tmp, "%d", pd) != 1) {
	    gretl_errmsg_set(_("Failed to parse data frequency"));
	    err = 1;
	}
	free(tmp);
    }

    return err;
}

static int xml_get_startobs (xmlNodePtr node, double *sd0, char *stobs,
			     int caldata)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) "startobs");
    int err = 0;

    if (tmp != NULL) {
	char obstr[16];

	obstr[0] = '\0';
	strncat(obstr, (char *) tmp, 15);
	charsub(obstr, ':', '.');
	
	if (strchr(obstr, '/') != NULL && caldata) {
	    long ed = get_epoch_day((char *) tmp);

	    if (ed < 0) {
		err = 1;
	    } else {
		*sd0 = ed;
	    }
	} else {
	    double x;

	    if (sscanf(obstr, "%lf", &x) != 1) {
		err = 1;
	    } else {
		*sd0 = x;
	    }
	}

	if (err) {
	    gretl_errmsg_set(_("Failed to parse startobs"));
	} else {
	    stobs[0] = '\0';
	    strncat(stobs, (char *) tmp, OBSLEN - 1);
	    colonize_obs(stobs);
	}

	free(tmp);
    }

    return err;
}

static int xml_get_endobs (xmlNodePtr node, char *endobs, int caldata)
{
    xmlChar *tmp = xmlGetProp(node, (XUC) "endobs");
    int err = 0;

    if (tmp != NULL) {
	if (caldata) {
	    long ed = get_epoch_day((char *) tmp);

	    if (ed < 0) err = 1;
	} else {
	    double x;

	    if (sscanf((char *) tmp, "%lf", &x) != 1) {
		err = 1;
	    }
	} 

	if (err) {
	    gretl_errmsg_set(_("Failed to parse endobs"));
	} else {
	    endobs[0] = '\0';
	    strncat(endobs, (char *) tmp, OBSLEN - 1);
	    colonize_obs(endobs);
	}

	free(tmp);
    }

    return err;
}

static int lag_from_label (int v, const DATAINFO *pdinfo, int *lag)
{
    const char *test = VARLABEL(pdinfo, v);
    char pm, vname[VNAMELEN];
    int pv = 0;

    if (sscanf(test, "= %15[^(](t %c %d)", vname, &pm, lag) == 3) {
	pv = series_index(pdinfo, vname);
	pv = (pv < pdinfo->v)? pv : 0;
    }

    return pv;
}

static int dummy_child_from_label (int v, const DATAINFO *pdinfo)
{
    const char *test = VARLABEL(pdinfo, v);
    char vname[VNAMELEN];
    double val;
    int pv = 0;

    if (sscanf(test, _("dummy for %s = %lf"), vname, &val) == 2 ||
	sscanf(test, "dummy for %s = %lf", vname, &val) == 2) {
	pv = series_index(pdinfo, vname);
	pv = (pv < pdinfo->v)? pv : 0;
    }

    return pv;
}

static void record_transform_info (double **Z, DATAINFO *pdinfo, double version)
{
    VARINFO *vinfo;
    int i, p, pv;

    for (i=1; i<pdinfo->v; i++) {
	vinfo = pdinfo->varinfo[i];
	if (vinfo->transform == LAGS) {
	    /* already handled */
	    continue;
	}
	pv = lag_from_label(i, pdinfo, &p);
	if (pv > 0) {
	    strcpy(vinfo->parent, pdinfo->varname[pv]);
	    vinfo->transform = LAGS;
	    vinfo->lag = p;
	} else if (version < 1.1) {
	    pv = dummy_child_from_label(i, pdinfo);
	    if (pv > 0) {
		strcpy(vinfo->parent, pdinfo->varname[pv]);
		vinfo->transform = DUMMIFY;
	    }
	}
    }
}

static void data_read_message (const char *fname, DATAINFO *pdinfo, PRN *prn)
{
    pprintf(prn, M_("\nRead datafile %s\n"), fname);
    pprintf(prn, M_("periodicity: %d, maxobs: %d\n"
		    "observations range: %s-%s\n"), 
	    (custom_time_series(pdinfo))? 1 : pdinfo->pd, 
	    pdinfo->n, pdinfo->stobs, pdinfo->endobs);
    pputc(prn, '\n');
}

static long get_filesize (const char *fname)
{
    struct stat buf;
    int err;

    err = gretl_stat(fname, &buf);

    return (err)? -1 : buf.st_size;
}

/**
 * gretl_read_gdt:
 * @fname: name of file to try.
 * @pZ: pointer to data set.
 * @pdinfo: pointer to data information struct.
 * @opt: use %OPT_B to display gui progress bar.
 * @prn: where messages should be written.
 * 
 * Read data from file into gretl's work space, allocating space as
 * required.  If the array to which @pZ points is not %NULL, attempt
 * to merge the new data with the original data.
 * 
 * Returns: 0 on successful completion, non-zero otherwise.
 */

int gretl_read_gdt (char *fname, double ***pZ, DATAINFO *pdinfo, 
		    gretlopt opt, PRN *prn) 
{
    DATAINFO *tmpdinfo;
    double **tmpZ = NULL;
    xmlDocPtr doc = NULL;
    xmlNodePtr cur;
    int gotvars = 0, gotobs = 0, err = 0;
    int caldata = 0;
    double gdtversion = 1.0;
    long fsz, progress = 0L;

    gretl_error_clear();

    /* COMPAT: Do not generate nodes for formatting spaces */
    LIBXML_TEST_VERSION
	xmlKeepBlanksDefault(0);

    fsz = get_filesize(fname);

    if (fsz < 0) {
	return E_FOPEN;
    } else if (fsz > 100000) {
	fprintf(stderr, "%s %ld bytes %s...\n", 
		(is_gzipped(fname))? I_("Uncompressing") : I_("Reading"),
		fsz, I_("of data"));
	if (opt & OPT_B) {
	    progress = fsz;
	}
    }

    check_for_console(prn);

    tmpdinfo = datainfo_new();
    if (tmpdinfo == NULL) {
	err = E_ALLOC;
	goto bailout;
    }

    doc = gretl_xmlParseFile(fname);
    if (doc == NULL) {
	gretl_errmsg_sprintf(_("xmlParseFile failed on %s"), fname);
	err = 1;
	goto bailout;
    }

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
        gretl_errmsg_sprintf(_("%s: empty document"), fname);
	err = 1;
	goto bailout;
    }

    if (xmlStrcmp(cur->name, (XUC) "gretldata")) {
        gretl_errmsg_set(_("File of the wrong type, root node not gretldata"));
	err = 1;
	goto bailout;
    }

    gdtversion = get_gdt_version(cur);

    /* set some datainfo parameters */

    err = xml_get_data_structure(cur, &tmpdinfo->structure);
    if (err) {
	goto bailout;
    } 

    err = xml_get_data_frequency(cur, &tmpdinfo->pd, &tmpdinfo->structure);
    if (err) {
	goto bailout;
    }   

    gretl_push_c_numeric_locale();

    strcpy(tmpdinfo->stobs, "1");
    caldata = dataset_is_daily(tmpdinfo) || dataset_is_weekly(tmpdinfo);

    err = xml_get_startobs(cur, &tmpdinfo->sd0, tmpdinfo->stobs, caldata);
    if (err) {
	gretl_pop_c_numeric_locale();
	goto bailout;
    }     

    *tmpdinfo->endobs = '\0';
    caldata = calendar_data(tmpdinfo);

    err = xml_get_endobs(cur, tmpdinfo->endobs, caldata);
    if (err) {
	gretl_pop_c_numeric_locale();
	goto bailout;
    }     

#if GDT_DEBUG
    fprintf(stderr, "starting to walk XML tree...\n");
#endif

    /* Now walk the tree */
    cur = cur->xmlChildrenNode;
    while (cur != NULL && !err) {
        if (!xmlStrcmp(cur->name, (XUC) "description")) {
	    tmpdinfo->descrip = (char *) 
		xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
        } else if (!xmlStrcmp(cur->name, (XUC) "variables")) {
	    if (process_varlist(cur, tmpdinfo, &tmpZ)) {
		err = 1;
	    } else {
		gotvars = 1;
	    }
	} else if (!xmlStrcmp(cur->name, (XUC) "observations")) {
	    if (!gotvars) {
		gretl_errmsg_set(_("Variables information is missing"));
		err = 1;
	    }
	    if (process_observations(doc, cur, &tmpZ, tmpdinfo, progress)) {
		err = 1;
	    } else {
		gotobs = 1;
	    }
	}
	if (!err) {
	    cur = cur->next;
	}
    }

#if GDT_DEBUG
    fprintf(stderr, "done walking XML tree...\n");
#endif

    gretl_pop_c_numeric_locale();

    if (err) {
	goto bailout;
    }

    if (!gotvars) {
	gretl_errmsg_set(_("Variables information is missing"));
	err = 1;
	goto bailout;
    }

    if (!gotobs) {
	gretl_errmsg_set(_("No observations were found"));
	err = 1;
	goto bailout;
    }

    data_read_message(fname, tmpdinfo, prn);

    err = merge_or_replace_data(pZ, pdinfo, &tmpZ, &tmpdinfo, opt, prn);

 bailout:

    if (doc != NULL) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }

    /* pre-process stacked cross-sectional panels: put into canonical
       stacked time series form
    */
    if (!err && pdinfo->structure == STACKED_CROSS_SECTION) {
	err = switch_panel_orientation(*pZ, pdinfo);
    }

    if (!err && pdinfo->structure == STACKED_TIME_SERIES) {
	if (pdinfo->paninfo == NULL) {
	    err = dataset_add_default_panel_indices(pdinfo);
	} else {
	    err = dataset_finalize_panel_indices(pdinfo);
	}
    }

    if (!err && gdtversion < 1.2) {
	record_transform_info(*pZ, pdinfo, gdtversion);
    }

    if (err && tmpdinfo != NULL) {
	destroy_dataset(tmpZ, tmpdinfo);
    }

    console_off();

#if GDT_DEBUG
    fprintf(stderr, "gretl_read_gdt: returning %d\n", err);
#endif

    return err;
}

/**
 * gretl_get_gdt_description:
 * @fname: name of file to try.
 * 
 * Read data description for gretl xml (.gdt) data file.
 * 
 * Returns: buffer containing description, or NULL on failure.
 */

char *gretl_get_gdt_description (const char *fname)
{
    xmlDocPtr doc;
    xmlNodePtr cur;
    xmlChar *buf = NULL;

    gretl_error_clear();

    LIBXML_TEST_VERSION
	xmlKeepBlanksDefault(0);

    doc = gretl_xmlParseFile(fname);
    if (doc == NULL) {
	gretl_errmsg_sprintf(_("xmlParseFile failed on %s"), fname);
	return NULL;
    }

    cur = xmlDocGetRootElement(doc);
    if (cur == NULL) {
        gretl_errmsg_sprintf(_("%s: empty document"), fname);
	xmlFreeDoc(doc);
	return NULL;
    }

    if (xmlStrcmp(cur->name, (XUC) "gretldata")) {
        gretl_errmsg_set(_("File of the wrong type, root node not gretldata"));
	xmlFreeDoc(doc);
	return NULL;
    }

    cur = cur->xmlChildrenNode;
    while (cur != NULL) {
        if (!xmlStrcmp(cur->name, (XUC) "description")) {
	    buf = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
	    break;
        }
	cur = cur->next;
    }

    xmlFreeDoc(doc);
    xmlCleanupParser();

    return (char *) buf;
}

static char *gretl_xml_get_doc_type (const char *fname, int *err)
{
    xmlDocPtr doc;
    xmlNodePtr node;
    char *ret = NULL;

    doc = gretl_xmlParseFile(fname);

    if (doc == NULL) {
	gretl_errmsg_sprintf(_("xmlParseFile failed on %s"), fname);
	*err = 1;
    } else {
	node = xmlDocGetRootElement(doc);
	if (node == NULL) {
	    gretl_errmsg_sprintf(_("%s: empty document"), fname);
	    *err = 1;
	} else {
	    ret = gretl_strdup((char *) node->name);
	    if (ret == NULL) {
		*err = 1;
	    }
	}
    }

    if (doc != NULL) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }

    return ret;
}

int load_user_matrix_file (const char *fname) 
{
    xmlDocPtr doc = NULL;
    xmlNodePtr cur = NULL;
    gretl_matrix *m;
    char *colnames;
    char *name;
    int err = 0;

    xmlKeepBlanksDefault(0);

    err = gretl_xml_open_doc_root(fname, "gretl-matrices", &doc, &cur);
    if (err) {
	return err;
    }

    cur = cur->xmlChildrenNode;
    while (cur != NULL && !err) {
        if (!xmlStrcmp(cur->name, (XUC) "gretl-matrix")) {
	    name = (char *) xmlGetProp(cur, (XUC) "name");
	    if (name == NULL) {
		err = 1;
	    } else {
		colnames = NULL;
		m = xml_get_user_matrix(cur, doc, &colnames, &err);
		if (m != NULL) {
		    err = user_matrix_add(m, name);
		    if (!err && colnames != NULL) {
			umatrix_set_colnames_from_string(m, colnames);
		    }
		}
		free(colnames);
		free(name);
	    }
	}
	cur = cur->next;
    }

    if (doc != NULL) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }

    return err;
}

int load_user_scalars_file (const char *fname) 
{
    xmlDocPtr doc = NULL;
    xmlNodePtr cur = NULL;
    char *name, *val;
    int err = 0;

    xmlKeepBlanksDefault(0);

    err = gretl_xml_open_doc_root(fname, "gretl-scalars", &doc, &cur);
    if (err) {
	return err;
    }

    cur = cur->xmlChildrenNode;
    while (cur != NULL && !err) {
        if (!xmlStrcmp(cur->name, (XUC) "gretl-scalar")) {
	    name = (char *) xmlGetProp(cur, (XUC) "name");
	    val = (char *) xmlGetProp(cur, (XUC) "value");
	    if (name == NULL || val == NULL) {
		err = 1;
	    } else {
		err = gretl_scalar_add(name, dot_atof(val));
	    }
	    free(name);
	    free(val);
	}
	cur = cur->next;
    }

    if (doc != NULL) {
	xmlFreeDoc(doc);
	xmlCleanupParser();
    }

    return err;
}

/* This is called in response to the "include" command in
   the CLI program, the GUI program, and in interact.c,
   if we detect that the named file is XML.
*/

int load_user_XML_file (const char *fname)
{
    char *rootname = NULL;
    int err = 0;

    rootname = gretl_xml_get_doc_type(fname, &err);

    if (!strcmp(rootname, "gretl-functions")) {
	err = load_function_package_from_file(fname);
    } else if (!strcmp(rootname, "gretl-matrices")) {
	err = load_user_matrix_file(fname);
    } else if (!strcmp(rootname, "gretl-scalars")) {
	err = load_user_scalars_file(fname);
    }

    free(rootname);

    return err;
}

