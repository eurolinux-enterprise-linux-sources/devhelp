/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008 Imendio AB
 * Copyright (C) 2008 Sven Herzberg
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

#include "config.h"
#include <string.h>
#include <glib/gi18n-lib.h>
#include "dh-assistant-view.h"
#include "dh-link.h"
#include "dh-util.h"
#include "dh-book-manager.h"
#include "dh-book.h"

typedef struct {
        DhBookManager *book_manager;
        DhLink        *link;
        gchar         *current_search;
        gboolean       snippet_loaded;
} DhAssistantViewPriv;

enum {
        SIGNAL_OPEN_URI,
        SIGNAL_LAST
};
static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (DhAssistantView, dh_assistant_view, WEBKIT_TYPE_WEB_VIEW);

#define GET_PRIVATE(instance) G_TYPE_INSTANCE_GET_PRIVATE \
  (instance, DH_TYPE_ASSISTANT_VIEW, DhAssistantViewPriv)

static void
view_finalize (GObject *object)
{
        DhAssistantViewPriv *priv = GET_PRIVATE (object);

        if (priv->link) {
                g_object_unref (priv->link);
        }

        if (priv->book_manager) {
                g_object_unref (priv->book_manager);
        }

        g_free (priv->current_search);

        G_OBJECT_CLASS (dh_assistant_view_parent_class)->finalize (object);
}

static gboolean
assistant_decide_policy (WebKitWebView           *web_view,
                         WebKitPolicyDecision    *decision,
                         WebKitPolicyDecisionType decision_type)
{
        DhAssistantViewPriv            *priv;
        const gchar                    *uri;
        WebKitNavigationPolicyDecision *navigation_decision;
        WebKitNavigationType            navigation_type;
        WebKitURIRequest               *request;

        if (decision_type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
                webkit_policy_decision_ignore (decision);

                return TRUE;
        }

        priv = GET_PRIVATE (web_view);
        navigation_decision = WEBKIT_NAVIGATION_POLICY_DECISION (decision);
        navigation_type = webkit_navigation_policy_decision_get_navigation_type (navigation_decision);
        if (navigation_type != WEBKIT_NAVIGATION_TYPE_LINK_CLICKED) {
                if (! priv->snippet_loaded) {
                        priv->snippet_loaded = TRUE;
                        webkit_policy_decision_use (decision);
                }

                webkit_policy_decision_ignore (decision);

                return TRUE;
        }

        request = webkit_navigation_policy_decision_get_request (navigation_decision);
        uri = webkit_uri_request_get_uri (request);
        if (strcmp (uri, "about:blank") == 0) {
                webkit_policy_decision_use (decision);

                return TRUE;
        }

        g_signal_emit (web_view, signals[SIGNAL_OPEN_URI], 0, uri);
        webkit_policy_decision_ignore (decision);

        return TRUE;
}

static gboolean
assistant_button_press_event (GtkWidget      *widget,
                              GdkEventButton *event)
{
        /* Block webkit's builtin context menu. */
        if (event->button != 1) {
                return TRUE;
        }

        return GTK_WIDGET_CLASS (dh_assistant_view_parent_class)->button_press_event (widget, event);
}

static void
dh_assistant_view_class_init (DhAssistantViewClass* klass)
{
        GObjectClass       *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass     *widget_class = GTK_WIDGET_CLASS (klass);
        WebKitWebViewClass *web_view_class = WEBKIT_WEB_VIEW_CLASS (klass);

        object_class->finalize = view_finalize;

        widget_class->button_press_event = assistant_button_press_event;
        web_view_class->decide_policy = assistant_decide_policy;

        g_type_class_add_private (klass, sizeof (DhAssistantViewPriv));

        signals[SIGNAL_OPEN_URI] = g_signal_new ("open-uri",
                                                 G_TYPE_FROM_CLASS (object_class),
                                                 0, 0,
                                                 NULL, NULL,
                                                 NULL,
                                                 G_TYPE_NONE, 1,
                                                 G_TYPE_STRING);
}

static void
dh_assistant_view_init (DhAssistantView *view)
{
}

GtkWidget*
dh_assistant_view_new (void)
{
        return g_object_new (DH_TYPE_ASSISTANT_VIEW, NULL);
}

static const gchar *
find_in_buffer (const gchar *buffer,
                const gchar *key,
                gsize        length,
                gsize        key_length)
{
        gsize m = 0;
        gsize i = 0;

        while (i < length) {
                if (key[m] == buffer[i]) {
                        m++;
                        if (m == key_length) {
                                return buffer + i - m + 1;
                        }
                } else {
                        m = 0;
                }
                i++;
        }

        return NULL;
}

/**
 * dh_assistant_view_set_link:
 * @view: an devhelp assistant view
 * @link: the #DhLink
 *
 * Open @link in the assistant view, if %NULL the view will be blanked.
 *
 * Return value: %TRUE if the requested link is open, %FALSE otherwise.
 **/
gboolean
dh_assistant_view_set_link (DhAssistantView *view,
                            DhLink          *link)
{
        DhAssistantViewPriv *priv;
        gchar               *uri;
        const gchar         *anchor;
        gchar               *filename;
        GMappedFile         *file;
        const gchar         *contents;
        gsize                length;
        gchar               *key;
        gsize                key_length;
        gsize                offset = 0;
        const gchar         *start;
        const gchar         *end;

        g_return_val_if_fail (DH_IS_ASSISTANT_VIEW (view), FALSE);

        priv = GET_PRIVATE (view);

        if (priv->link == link) {
                return TRUE;
        }

        if (priv->link) {
                dh_link_unref (priv->link);
                priv->link = NULL;
        }

        if (link) {
                link = dh_link_ref (link);
        } else {
                webkit_web_view_load_uri (WEBKIT_WEB_VIEW (view), "about:blank");
                return TRUE;
        }

        uri = dh_link_get_uri (link);
        anchor = strrchr (uri, '#');
        if (anchor) {
                filename = g_strndup (uri, anchor - uri);
                anchor++;
        } else {
                g_free (uri);
                return FALSE;
        }

        if (g_str_has_prefix (filename, "file://"))
            offset = 7;

        file = g_mapped_file_new (filename + offset, FALSE, NULL);
        if (!file) {
                g_free (filename);
                g_free (uri);
                return FALSE;
        }

        contents = g_mapped_file_get_contents (file);
        length = g_mapped_file_get_length (file);

        key = g_strdup_printf ("<a name=\"%s\"", anchor);
        g_free (uri);
        key_length = strlen (key);

        start = find_in_buffer (contents, key, length, key_length);
        g_free (key);

        end = NULL;

        if (start) {
                const gchar *start_key;
                const gchar *end_key;

                length -= start - contents;

                start_key = "<pre class=\"programlisting\">";

                start = find_in_buffer (start,
                                        start_key,
                                        length,
                                        strlen (start_key));

                end_key = "<div class=\"refsect";

                if (start) {
                        end = find_in_buffer (start, end_key,
                                              length - strlen (start_key),
                                              strlen (end_key));
                        if (!end) {
                                end_key = "<div class=\"footer";
                                end = find_in_buffer (start, end_key,
                                                      length - strlen (start_key),
                                                      strlen (end_key));
                        }
                }
        }

        if (start && end) {
                gchar       *buf;
                gboolean     break_line;
                const gchar *function;
                gchar       *stylesheet;
                gchar       *stylesheet_uri;
                gchar       *stylesheet_html = NULL;
                gchar       *javascript;
                gchar       *javascript_uri;
                gchar       *javascript_html = NULL;
                gchar       *html;

                buf = g_strndup (start, end-start);

                /* Try to reformat function signatures so they take less
                 * space and look nicer. Don't reformat things that don't
                 * look like functions.
                 */
                switch (dh_link_get_link_type (link)) {
                case DH_LINK_TYPE_FUNCTION:
                        break_line = TRUE;
                        function = "onload=\"reformatSignature()\"";
                        break;
                case DH_LINK_TYPE_MACRO:
                        break_line = TRUE;
                        function = "onload=\"cleanupSignature()\"";
                        break;
                default:
                        break_line = FALSE;
                        function = "";
                        break;
                }

                if (break_line) {
                        gchar *name;

                        name = strstr (buf, dh_link_get_name (link));
                        if (name && name > buf) {
                                name[-1] = '\n';
                        }
                }

                stylesheet = dh_util_build_data_filename ("devhelp",
                                                          "assistant",
                                                          "assistant.css",
                                                          NULL);
                stylesheet_uri = dh_util_create_data_uri_for_filename (stylesheet,
                                                                       "text/css");
                g_free (stylesheet);
                if (stylesheet_uri)
                        stylesheet_html = g_strdup_printf ("<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\"/>",
                                                           stylesheet_uri);
                g_free (stylesheet_uri);

                javascript = dh_util_build_data_filename ("devhelp",
                                                          "assistant",
                                                          "assistant.js",
                                                          NULL);
                javascript_uri = dh_util_create_data_uri_for_filename (javascript,
                                                                       "application/javascript");
                g_free (javascript);

                if (javascript_uri)
                        javascript_html = g_strdup_printf ("<script src=\"%s\"></script>", javascript_uri);
                g_free (javascript_uri);

                html = g_strdup_printf (
                        "<html>"
                        "<head>"
                        "%s"
                        "%s"
                        "</head>"
                        "<body %s>"
                        "<div class=\"title\">%s: <a href=\"%s\">%s</a></div>"
                        "<div class=\"subtitle\">%s %s</div>"
                        "<div class=\"content\">%s</div>"
                        "</body>"
                        "</html>",
                        stylesheet_html,
                        javascript_html,
                        function,
                        dh_link_get_type_as_string (link),
                        dh_link_get_uri (link),
                        dh_link_get_name (link),
                        _("Book:"),
                        dh_link_get_book_name (link),
                        buf);
                g_free (buf);

                g_free (stylesheet_html);
                g_free (javascript_html);

                priv->snippet_loaded = FALSE;
                webkit_web_view_load_html (
                        WEBKIT_WEB_VIEW (view),
                        html,
                        filename);
                g_free (html);
        } else {
                webkit_web_view_load_uri (WEBKIT_WEB_VIEW (view), "about:blank");
        }

#if GLIB_CHECK_VERSION(2,21,3)
        g_mapped_file_unref (file);
#else
        g_mapped_file_free (file);
#endif

        g_free (filename);

        return TRUE;
}

gboolean
dh_assistant_view_search (DhAssistantView *view,
                          const gchar     *str)
{
        DhAssistantViewPriv *priv;
        const gchar         *name;
        DhLink              *link;
        DhLink              *exact_link;
        DhLink              *prefix_link;
        GList               *books;

        g_return_val_if_fail (DH_IS_ASSISTANT_VIEW (view), FALSE);
        g_return_val_if_fail (str, FALSE);

        priv = GET_PRIVATE (view);

        /* Filter out very short strings. */
        if (strlen (str) < 4) {
                return FALSE;
        }

        if (priv->current_search && strcmp (priv->current_search, str) == 0) {
                return FALSE;
        }
        g_free (priv->current_search);
        priv->current_search = g_strdup (str);

        prefix_link = NULL;
        exact_link = NULL;

        for (books = dh_book_manager_get_books (priv->book_manager);
             !exact_link && books;
             books = g_list_next (books)) {
                GList *l;

                for (l = dh_book_get_keywords (DH_BOOK (books->data));
                     l && exact_link == NULL;
                     l = l->next) {
                        DhLinkType type;

                        link = l->data;

                        type = dh_link_get_link_type (link);

                        if (type == DH_LINK_TYPE_BOOK ||
                            type == DH_LINK_TYPE_PAGE ||
                            type == DH_LINK_TYPE_KEYWORD) {
                                continue;
                        }

                        name = dh_link_get_name (link);
                        if (strcmp (name, str) == 0) {
                                exact_link = link;
                        }
                        else if (g_str_has_prefix (name, str)) {
                                /* Prefer shorter prefix matches. */
                                if (!prefix_link) {
                                        prefix_link = link;
                                }
                                else if (strlen (dh_link_get_name (prefix_link)) > strlen (name)) {
                                        prefix_link = link;
                                }
                        }
                }
        }

        if (exact_link) {
                /*g_print ("exact hit: '%s' '%s'\n", exact_link->name, str);*/
                dh_assistant_view_set_link (view, exact_link);
        }
        else if (prefix_link) {
                /*g_print ("prefix hit: '%s' '%s'\n", prefix_link->name, str);*/
                dh_assistant_view_set_link (view, prefix_link);
        } else {
                /*g_print ("no hit\n");*/
                /*assistant_view_set_link (view, NULL);*/
                return FALSE;
        }

        return TRUE;
}

void
dh_assistant_view_set_book_manager (DhAssistantView *view,
                                    DhBookManager   *book_manager)
{
        DhAssistantViewPriv *priv;

        g_return_if_fail (DH_IS_ASSISTANT_VIEW (view));
        g_return_if_fail (DH_IS_BOOK_MANAGER (book_manager));

        priv = GET_PRIVATE (view);

        priv->book_manager = g_object_ref (book_manager);
}
