/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * published by the Free Software Foundation; either version 2 of the
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __E_PATH__
#define __E_PATH__

#include <glib.h>

typedef gboolean (*EPathFindFoldersCallback) (const char *physical_path,
					      const char *path,
					      gpointer user_data);

char *   e_path_to_physical  (const char *prefix, const char *vpath);

gboolean e_path_find_folders (const char *prefix,
			      EPathFindFoldersCallback callback,
			      gpointer data);

#endif /* __E_PATH__ */
