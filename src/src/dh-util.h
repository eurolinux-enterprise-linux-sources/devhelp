/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2002 Mikael Hallendal <micke@imendio.com>
 * Copyright (C) 2004,2008 Imendio AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DH_UTIL_H__
#define __DH_UTIL_H__

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include "dh-link.h"

G_BEGIN_DECLS

gchar *      dh_util_build_data_filename          (const gchar *first_part,
                                                   ...);

void         dh_util_ascii_strtitle               (gchar *str);

gchar       *dh_util_create_data_uri_for_filename (const gchar *filename,
                                                   const gchar *mime_type);

void         dh_util_view_set_font                (WebKitWebView *view,
                                                   const gchar *font_name_fixed,
                                                   const gchar *font_name_variable);

void         dh_util_window_settings_save         (GtkWindow *window,
                                                   GSettings *settings,
                                                   gboolean has_maximize);

void         dh_util_window_settings_restore      (GtkWindow *window,
                                                   GSettings *settings,
                                                   gboolean has_maximize);

void         dh_util_queue_concat                 (GQueue *q1,
                                                   GQueue *q2);

G_END_DECLS

#endif /* __DH_UTIL_H__ */
