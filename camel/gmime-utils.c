/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* mime-utils.c : misc utilities for mime  */

/* 
 *
 * Copyright (C) 1999 Bertrand Guiheneuf <Bertrand.Guiheneuf@inria.fr> .
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */



#include "gmime-utils.h"
#include "gstring-util.h"
#include "camel-log.h"

void
gmime_write_header_pair_to_file (FILE* file, gchar* name, GString *value)
{
	g_assert(name);

	if ((value) && (value->str))
		fprintf(file, "%s: %s\n", name, value->str);
}


static void
_write_one_header_to_file (gpointer key, gpointer value, gpointer user_data)
{
	GString *header_name = (GString *)key;
	GString *header_value = (GString *)value;
	FILE *file = (FILE *)user_data;

	if ( (header_name) && (header_name->str) && 
	     (header_value) && (header_value->str) )
		fprintf(file, "%s: %s\n", header_name->str, header_value->str);		
}

void 
write_header_table_to_file (FILE *file, GHashTable *header_table)
{
	g_hash_table_foreach (header_table, 
			      _write_one_header_to_file, 
			      (gpointer)file);
}


void 
write_header_with_glist_to_file (FILE *file, gchar *header_name, GList *header_values)
{
	
	GString *current;
	
	if ( (header_name) && (header_values) )
		{
			gboolean first;
			
			fprintf(file, "%s: ", header_name);
			first = TRUE;
			while (header_values) {
				current = (GString *)header_values->data;
				if ( (current) && (current->str) ) {
					if (!first) fprintf(file, ", ");
					else first = FALSE;
					fprintf(file, "%s", current->str);
				}
				header_values = g_list_next(header_values);
			}
			fprintf(file, "\n");
		}
	
}	





/* * * * * * * * * * * */
/* scanning functions  */

static void
_store_header_pair_from_gstring (GHashTable *header_table, GString *header_line)
{
	gchar dich_result;
	GString *header_name, *header_value;
	
	g_assert (header_table);
	if ( (header_line) && (header_line->str) ) {
		dich_result = g_string_dichotomy(header_line, ':', &header_name, &header_value, DICHOTOMY_NONE);
		if (dich_result != 'o')
			camel_log(WARNING, 
				  "store_header_pair_from_gstring : dichotomy result is %c"
				  "header line is :\n--\n%s\n--\n");
		
		else {
			g_string_trim (header_value, " \t", TRIM_STRIP_LEADING | TRIM_STRIP_TRAILING);
			g_hash_table_insert (header_table, header_name, header_value);
		}
	}
		
}


GHashTable *
get_header_table_from_file (FILE *file)
{
	int next_char;

	gboolean crlf = FALSE;
	gboolean end_of_header_line = FALSE;
	gboolean end_of_headers = FALSE;
	gboolean end_of_file = FALSE;
	GString *header_line=NULL;
	GHashTable *header_table;

	header_table = g_hash_table_new (g_string_hash, g_string_equal_for_hash);
	next_char = fgetc (file);
	do {
		header_line = g_string_new("");
		end_of_header_line = FALSE;
		crlf = FALSE;
		
		/* read a whole header line */
		do {
			switch (next_char) {
			case EOF:
				end_of_file=TRUE;
				end_of_header_line = TRUE;
				break;
			case '\n': /* a blank line means end of headers */
				if (crlf) {
					end_of_headers=TRUE;
					end_of_header_line = TRUE;
				}
				else crlf = TRUE;
				break;
			case ' ':
			case 't':
				if (crlf) crlf = FALSE;
				
			default:
				if (!crlf) header_line = g_string_append_c (header_line, next_char);
				else end_of_header_line = TRUE;
			}
			/* if we have read a whole header line, we have also read
			   the first character of the next line to be sure the 
			   crlf was not followed by a space or a tab char */
			if (!end_of_header_line) next_char = fgetc (file);

		} while ( !end_of_header_line );
		if ( strlen(header_line->str) ) 
			_store_header_pair_from_gstring (header_table, header_line);
		g_string_free (header_line, FALSE);

	} while ( (!end_of_headers) && (!end_of_file) );

	return header_table;
}
		
		
GHashTable *
get_header_table_from_stream (GnomeStream *stream)
{
	int next_char;

	gboolean crlf = FALSE;
	gboolean end_of_header_line = FALSE;
	gboolean end_of_headers = FALSE;
	gboolean end_of_file = FALSE;
	GString *header_line=NULL;
	GHashTable *header_table;

	header_table = g_hash_table_new (g_string_hash, g_string_equal_for_hash);
	//next_char = fgetc (file);
	do {
		header_line = g_string_new("");
		end_of_header_line = FALSE;
		crlf = FALSE;
		
		/* read a whole header line */
		do {
			switch (next_char) {
			case EOF:
				end_of_file=TRUE;
				end_of_header_line = TRUE;
				break;
			case '\n': /* a blank line means end of headers */
				if (crlf) {
					end_of_headers=TRUE;
					end_of_header_line = TRUE;
				}
				else crlf = TRUE;
				break;
			case ' ':
			case 't':
				if (crlf) crlf = FALSE;
				
			default:
				if (!crlf) header_line = g_string_append_c (header_line, next_char);
				else end_of_header_line = TRUE;
			}
			/* if we have read a whole header line, we have also read
			   the first character of the next line to be sure the 
			   crlf was not followed by a space or a tab char */
			//if (!end_of_header_line) next_char = fgetc (file);

		} while ( !end_of_header_line );
		if ( strlen(header_line->str) ) 
			_store_header_pair_from_gstring (header_table, header_line);
		g_string_free (header_line, FALSE);

	} while ( (!end_of_headers) && (!end_of_file) );

	return header_table;
}
		
		
