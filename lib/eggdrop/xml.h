/* xml.h: header for xml.c
 *
 * Copyright (C) 2002, 2003, 2004 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id: xml.h,v 1.16 2004/06/23 17:24:43 wingman Exp $
 */

#ifndef _EGG_XML_H_
#define _EGG_XML_H_

#include <stdarg.h>			/* va_list		*/

typedef struct xml_node xml_node_t;

#define XML_NONE	(1 << 0)
#define XML_INDENT	(1 << 1)
#define XML_TRIM_TEXT	(1 << 2)

typedef struct
{
	char *name;
	char *value;
	int len;
} xml_attr_t;

typedef struct
{
	char *key;
	char value;
} xml_amp_conversion_t;

typedef enum
{
	XML_ELEMENT = 0,		/* <element /> 		*/
	XML_PROCESSING_INSTRUCTION,	/* <?pi?>		*/
	XML_COMMENT,			/* <!-- comment -->	*/
	XML_CDATA_SECTION,		/* <![CDATA[ ... ]]>	*/
	XML_DOCUMENT,			/* none			*/
} xml_node_type_t;

struct xml_node
{

	/* axis */
	xml_node_t *next;
	xml_node_t *prev;
	xml_node_t *parent;
	xml_node_t **children;
	int nchildren;

	/* data */
	char *name;
	char *text;
	int len;
	int whitespace;
	xml_node_type_t type;

	xml_attr_t *attributes;
	int nattributes;

	void *client_data;
};

xml_node_t *xml_node_vlookup(xml_node_t *root, va_list args, int create);
xml_node_t *xml_node_lookup(xml_node_t *root, int create, ...);
xml_node_t *xml_node_path_lookup(xml_node_t *root, const char *path, int index, int create);

char *xml_node_fullname(xml_node_t *node);

xml_node_t *xml_node_new(void);
void xml_node_delete(xml_node_t *node);
void xml_node_delete_callbacked(xml_node_t *node, void (*callback)(void *));

const char *xml_node_type(xml_node_t *node);

xml_node_t *xml_root_element(xml_node_t *node);
xml_node_t *xml_node_select(xml_node_t *parent, const char *path);
char *xml_node_path(xml_node_t *node);

void xml_node_append(xml_node_t *parent, xml_node_t *child);
void xml_node_remove(xml_node_t *parent, xml_node_t *child);

xml_attr_t *xml_node_lookup_attr(xml_node_t *node, const char *name);
void xml_node_append_attr(xml_node_t *parent, xml_attr_t *attr);
void xml_node_remove_attr(xml_node_t *parent, xml_attr_t *attr);

int xml_node_get_str(char **str, xml_node_t *node, ...);
int xml_node_set_str(const char *str, xml_node_t *node, ...);

int xml_node_get_int(int *value, xml_node_t *node, ...);
int xml_node_set_int(int value, xml_node_t *node, ...);

void xml_attr_set_int(xml_node_t *node, const char *name, int value);
int xml_attr_get_int(xml_node_t *node, const char *name);
void xml_attr_set_str(xml_node_t *node, const char *name, const char *value);
char *xml_attr_get_str(xml_node_t *node, const char *name);

const char *xml_last_error(void);

int xml_load(FILE *fd, xml_node_t **node, int options);
int xml_load_file(const char *file, xml_node_t **node, int options);
int xml_load_str(char *str, xml_node_t **node, int options);

int xml_save(FILE *fd, xml_node_t *node, int options);
int xml_save_file(const char *file, xml_node_t *node, int options);
int xml_save_str(char **str, xml_node_t *node, int options);

#endif /* !_EGG_XML_H_ */
