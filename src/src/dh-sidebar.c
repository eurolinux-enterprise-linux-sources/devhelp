/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2003 CodeFactory AB
 * Copyright (C) 2001-2003 Mikael Hallendal <micke@imendio.com>
 * Copyright (C) 2005-2008 Imendio AB
 * Copyright (C) 2010 Lanedo GmbH
 * Copyright (C) 2013 Aleksander Morgado <aleksander@gnu.org>
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

#include "config.h"

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "dh-keyword-model.h"
#include "dh-sidebar.h"
#include "dh-util.h"
#include "dh-book-manager.h"
#include "dh-book.h"
#include "dh-book-tree.h"

typedef struct {
        DhKeywordModel *model;

        DhBookManager  *book_manager;

        DhLink         *selected_link;

        GtkWidget      *entry;
        GtkWidget      *hitlist;
        GtkWidget      *sw_hitlist;
        GtkWidget      *book_tree;
        GtkWidget      *sw_book_tree;

        GCompletion    *completion;
        guint           idle_complete;
        guint           idle_filter;
} DhSidebarPrivate;

enum {
        PROP_0,
        PROP_BOOK_MANAGER
};

enum {
        LINK_SELECTED,
        LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (DhSidebar, dh_sidebar, GTK_TYPE_BOX)

/******************************************************************************/

static gboolean
sidebar_filter_idle (DhSidebar *sidebar)
{
        DhSidebarPrivate *priv;
        const gchar *str;
        DhLink      *link;
        DhLink      *book_link;

        priv = dh_sidebar_get_instance_private (sidebar);

        priv->idle_filter = 0;

        str = gtk_entry_get_text (GTK_ENTRY (priv->entry));

        book_link = dh_sidebar_get_selected_book (sidebar);

        link = dh_keyword_model_filter (priv->model,
                                        str,
                                        book_link ? dh_link_get_book_id (book_link) : NULL,
                                        NULL);

        if (link)
                g_signal_emit (sidebar, signals[LINK_SELECTED], 0, link);

        return FALSE;
}

static void
sidebar_search_run_idle (DhSidebar *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        if (!priv->idle_filter)
                priv->idle_filter =
                        g_idle_add ((GSourceFunc) sidebar_filter_idle, sidebar);
}

/******************************************************************************/

static void
sidebar_completion_add_book (DhSidebar *sidebar,
                             DhBook    *book)
{
        GList *completions;
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        if (G_UNLIKELY (!priv->completion))
                priv->completion = g_completion_new (NULL);

        completions = dh_book_get_completions (book);
        if (completions)
                g_completion_add_items (priv->completion, completions);
}

static void
sidebar_completion_delete_book (DhSidebar *sidebar,
                                DhBook    *book)
{
        GList *completions;
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        if (G_UNLIKELY (!priv->completion))
                return;

        completions = dh_book_get_completions (book);
        if (completions)
                g_completion_remove_items (priv->completion, completions);
}

static void
sidebar_book_created_or_enabled_cb (DhBookManager *book_manager,
                                    DhBook        *book,
                                    DhSidebar     *sidebar)
{
        sidebar_completion_add_book (sidebar, book);
        /* Update current search if any */
        sidebar_search_run_idle (sidebar);
}

static void
sidebar_book_deleted_or_disabled_cb (DhBookManager *book_manager,
                                     DhBook        *book,
                                     DhSidebar     *sidebar)
{
        sidebar_completion_delete_book (sidebar, book);
        /* Update current search if any */
        sidebar_search_run_idle (sidebar);
}

static void
sidebar_completion_populate (DhSidebar *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);
        GList *l;

        for (l = dh_book_manager_get_books (priv->book_manager);
             l;
             l = g_list_next (l)) {
                sidebar_completion_add_book (sidebar, DH_BOOK (l->data));
        }
}

/******************************************************************************/

static void
sidebar_selection_changed_cb (GtkTreeSelection *selection,
                              DhSidebar        *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);
        GtkTreeIter iter;

        if (gtk_tree_selection_get_selected (selection, NULL, &iter)) {
                DhLink *link;

                gtk_tree_model_get (GTK_TREE_MODEL (priv->model), &iter,
                                    DH_KEYWORD_MODEL_COL_LINK, &link,
                                    -1);

                if (link != priv->selected_link) {
                        priv->selected_link = link;
                        g_signal_emit (sidebar, signals[LINK_SELECTED], 0, link);
                }
        }
}

/* Make it possible to jump back to the currently selected item, useful when the
 * html view has been scrolled away.
 */
static gboolean
sidebar_tree_button_press_cb (GtkTreeView    *view,
                              GdkEventButton *event,
                              DhSidebar      *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);
        GtkTreePath  *path;
        GtkTreeIter   iter;
        DhLink       *link;

        gtk_tree_view_get_path_at_pos (view, event->x, event->y, &path,
                                       NULL, NULL, NULL);
        if (!path)
                return FALSE;

        gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->model), &iter, path);
        gtk_tree_path_free (path);

        gtk_tree_model_get (GTK_TREE_MODEL (priv->model),
                            &iter,
                            DH_KEYWORD_MODEL_COL_LINK, &link,
                            -1);

        priv->selected_link = link;

        g_signal_emit (sidebar, signals[LINK_SELECTED], 0, link);

        /* Always return FALSE so the tree view gets the event and can update
         * the selection etc.
         */
        return FALSE;
}

static gboolean
sidebar_entry_key_press_event_cb (GtkEntry    *entry,
                                  GdkEventKey *event,
                                  DhSidebar   *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        if (event->keyval == GDK_KEY_Tab) {
                if (event->state & GDK_CONTROL_MASK) {
                        gtk_widget_grab_focus (priv->hitlist);
                } else {
                        gtk_editable_set_position (GTK_EDITABLE (entry), -1);
                        gtk_editable_select_region (GTK_EDITABLE (entry), -1, -1);
                }
                return TRUE;
        }

        if (event->keyval == GDK_KEY_Return ||
            event->keyval == GDK_KEY_KP_Enter) {
                GtkTreeIter  iter;
                DhLink      *link;
                gchar       *name;

                /* Get the first entry found. */
                if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->model), &iter)) {
                        gtk_tree_model_get (GTK_TREE_MODEL (priv->model),
                                            &iter,
                                            DH_KEYWORD_MODEL_COL_LINK, &link,
                                            DH_KEYWORD_MODEL_COL_NAME, &name,
                                            -1);

                        gtk_entry_set_text (GTK_ENTRY (entry), name);
                        g_free (name);

                        gtk_editable_set_position (GTK_EDITABLE (entry), -1);
                        gtk_editable_select_region (GTK_EDITABLE (entry), -1, -1);

                        g_signal_emit (sidebar, signals[LINK_SELECTED], 0, link);

                        return TRUE;
                }
        }

        return FALSE;
}

static void
sidebar_entry_changed_cb (GtkEntry  *entry,
                          DhSidebar *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        /* If search entry is empty, hide the hitlist */
        if (strcmp (gtk_entry_get_text (entry), "") == 0) {
                gtk_widget_hide (priv->sw_hitlist);
                gtk_widget_show (priv->sw_book_tree);
                return;
        }

        gtk_widget_hide (priv->sw_book_tree);
        gtk_widget_show (priv->sw_hitlist);
        sidebar_search_run_idle (sidebar);
}

static gboolean
sidebar_complete_idle (DhSidebar *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);
        const gchar  *str;
        gchar        *completed = NULL;
        gsize         length;

        str = gtk_entry_get_text (GTK_ENTRY (priv->entry));

        g_completion_complete (priv->completion, str, &completed);
        if (completed) {
                length = strlen (str);

                gtk_entry_set_text (GTK_ENTRY (priv->entry), completed);
                gtk_editable_set_position (GTK_EDITABLE (priv->entry), length);
                gtk_editable_select_region (GTK_EDITABLE (priv->entry),
                                            length, -1);
                g_free (completed);
        }

        priv->idle_complete = 0;

        return FALSE;
}

static void
sidebar_entry_text_inserted_cb (GtkEntry    *entry,
                                const gchar *text,
                                gint         length,
                                gint        *position,
                                DhSidebar   *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        if (!priv->idle_complete)
                priv->idle_complete =
                        g_idle_add ((GSourceFunc) sidebar_complete_idle, sidebar);
}

/******************************************************************************/

void
dh_sidebar_set_search_string (DhSidebar   *sidebar,
                              const gchar *str)
{
        DhSidebarPrivate *priv;

        g_return_if_fail (DH_IS_SIDEBAR (sidebar));

        priv = dh_sidebar_get_instance_private (sidebar);

        g_signal_handlers_block_by_func (priv->entry,
                                         sidebar_entry_changed_cb,
                                         sidebar);

        gtk_entry_set_text (GTK_ENTRY (priv->entry), str);
        gtk_editable_set_position (GTK_EDITABLE (priv->entry), -1);
        gtk_editable_select_region (GTK_EDITABLE (priv->entry), -1, -1);

        g_signal_handlers_unblock_by_func (priv->entry,
                                           sidebar_entry_changed_cb,
                                           sidebar);

        sidebar_search_run_idle (sidebar);
}

/******************************************************************************/

void
dh_sidebar_set_search_focus (DhSidebar *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        gtk_widget_grab_focus (priv->entry);
}

/******************************************************************************/

static void
search_cell_data_func (GtkTreeViewColumn *tree_column,
                       GtkCellRenderer   *cell,
                       GtkTreeModel      *tree_model,
                       GtkTreeIter       *iter,
                       gpointer           data)
{
        DhLink       *link;
        PangoStyle    style;
        PangoWeight   weight;
        gboolean      current_book_flag;

        gtk_tree_model_get (tree_model, iter,
                            DH_KEYWORD_MODEL_COL_LINK, &link,
                            DH_KEYWORD_MODEL_COL_CURRENT_BOOK_FLAG, &current_book_flag,
                            -1);

        if (dh_link_get_flags (link) & DH_LINK_FLAGS_DEPRECATED)
                style = PANGO_STYLE_ITALIC;
        else
                style = PANGO_STYLE_NORMAL;

        /* Matches on the current book are given in bold. Note that we check the
         * current book as it was given to the DhKeywordModel. Do *not* rely on
         * the current book as given by the DhSidebar, as that will change
         * whenever a hit is clicked. */
        if (current_book_flag)
                weight = PANGO_WEIGHT_BOLD;
        else
                weight = PANGO_WEIGHT_NORMAL;

        g_object_set (cell,
                      "text", dh_link_get_name (link),
                      "style", style,
                      "weight", weight,
                      NULL);
}

/******************************************************************************/

static void
sidebar_book_tree_link_selected_cb (GObject   *ignored,
                                    DhLink    *link,
                                    DhSidebar *sidebar)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        if (link != priv->selected_link) {
                priv->selected_link = link;
                g_signal_emit (sidebar, signals[LINK_SELECTED], 0, link);
        }
}

DhLink *
dh_sidebar_get_selected_book (DhSidebar *sidebar)
{
        DhSidebarPrivate *priv;

        g_return_val_if_fail (DH_IS_SIDEBAR (sidebar), NULL);

        priv = dh_sidebar_get_instance_private (sidebar);

        return dh_book_tree_get_selected_book (DH_BOOK_TREE (priv->book_tree));
}

void
dh_sidebar_select_uri (DhSidebar   *sidebar,
                       const gchar *uri)
{
        DhSidebarPrivate *priv;

        g_return_if_fail (DH_IS_SIDEBAR (sidebar));

        priv = dh_sidebar_get_instance_private (sidebar);

        dh_book_tree_select_uri (DH_BOOK_TREE (priv->book_tree), uri);
}

/******************************************************************************/

GtkWidget *
dh_sidebar_new (DhBookManager *book_manager)
{
        return GTK_WIDGET (g_object_new (DH_TYPE_SIDEBAR,
                                         "orientation", GTK_ORIENTATION_VERTICAL,
                                         "book-manager", book_manager,
                                         NULL));
}

static void
dh_sidebar_finalize (GObject *object)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (DH_SIDEBAR (object));

        g_completion_free (priv->completion);

        G_OBJECT_CLASS (dh_sidebar_parent_class)->finalize (object);
}

static void
dh_sidebar_dispose (GObject *object)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (DH_SIDEBAR (object));

        g_clear_object (&priv->book_manager);

        G_OBJECT_CLASS (dh_sidebar_parent_class)->dispose (object);
}

static void
dh_sidebar_init (DhSidebar *sidebar)
{
        DhSidebarPrivate *priv;
        GtkCellRenderer  *cell;
        GtkWidget        *hbox;

        priv = dh_sidebar_get_instance_private (sidebar);

        gtk_container_set_border_width (GTK_CONTAINER (sidebar), 2);
        gtk_box_set_spacing (GTK_BOX (sidebar), 4);

        /* Setup keyword model */
        priv->model = dh_keyword_model_new ();

        /* Setup hitlist */
        priv->hitlist = gtk_tree_view_new ();
        gtk_tree_view_set_model (GTK_TREE_VIEW (priv->hitlist), GTK_TREE_MODEL (priv->model));
        gtk_tree_view_set_enable_search (GTK_TREE_VIEW (priv->hitlist), FALSE);

        /* Setup the top-level box with entry search and Current|All buttons */
        hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start (GTK_BOX (sidebar), hbox, FALSE, FALSE, 0);

        /* Setup the search entry */
        priv->entry = gtk_search_entry_new ();
        gtk_box_pack_start (GTK_BOX (hbox), priv->entry, TRUE, TRUE, 0);
        g_signal_connect (priv->entry, "key-press-event",
                          G_CALLBACK (sidebar_entry_key_press_event_cb),
                          sidebar);
        g_signal_connect (priv->hitlist, "button-press-event",
                          G_CALLBACK (sidebar_tree_button_press_cb),
                          sidebar);
        g_signal_connect (priv->entry, "changed",
                          G_CALLBACK (sidebar_entry_changed_cb),
                          sidebar);
        g_signal_connect (priv->entry, "insert-text",
                          G_CALLBACK (sidebar_entry_text_inserted_cb),
                          sidebar);

        /* Setup the hitlist */
        priv->sw_hitlist = gtk_scrolled_window_new (NULL, NULL);
        gtk_widget_set_no_show_all (priv->sw_hitlist, TRUE);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (priv->sw_hitlist), GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->sw_hitlist),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        cell = gtk_cell_renderer_text_new ();
        g_object_set (cell,
                      "ellipsize", PANGO_ELLIPSIZE_END,
                      NULL);
        gtk_tree_view_insert_column_with_data_func (
                GTK_TREE_VIEW (priv->hitlist),
                -1,
                NULL,
                cell,
                search_cell_data_func,
                sidebar,
                NULL);
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (priv->hitlist), FALSE);
        gtk_tree_view_set_search_column (GTK_TREE_VIEW (priv->hitlist), DH_KEYWORD_MODEL_COL_NAME);
        g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->hitlist)),
                          "changed",
                          G_CALLBACK (sidebar_selection_changed_cb),
                          sidebar);
        gtk_widget_show (priv->hitlist);
        gtk_container_add (GTK_CONTAINER (priv->sw_hitlist), priv->hitlist);
        gtk_box_pack_start (GTK_BOX (sidebar), priv->sw_hitlist, TRUE, TRUE, 0);

        /* Setup the book tree */
        priv->sw_book_tree = gtk_scrolled_window_new (NULL, NULL);
        gtk_widget_show (priv->sw_book_tree);
        gtk_widget_set_no_show_all (priv->sw_book_tree, TRUE);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (priv->sw_book_tree),
                                        GTK_POLICY_NEVER,
                                        GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (priv->sw_book_tree),
                                             GTK_SHADOW_IN);
        gtk_container_set_border_width (GTK_CONTAINER (priv->sw_book_tree), 2);

        gtk_widget_show_all (GTK_WIDGET (sidebar));
}

static void
dh_sidebar_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (DH_SIDEBAR (object));

        switch (prop_id) {
                case PROP_BOOK_MANAGER:
                        g_value_set_object (value, priv->book_manager);
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
dh_sidebar_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (DH_SIDEBAR (object));

        switch (prop_id) {
                case PROP_BOOK_MANAGER:
                        g_return_if_fail (priv->book_manager == NULL);
                        priv->book_manager = g_value_dup_object (value);
                        break;
                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
dh_sidebar_constructed (GObject *object)
{
        DhSidebar *sidebar = DH_SIDEBAR (object);
        DhSidebarPrivate *priv = dh_sidebar_get_instance_private (sidebar);

        /* Setup book manager */
        g_signal_connect (priv->book_manager,
                          "book-created",
                          G_CALLBACK (sidebar_book_created_or_enabled_cb),
                          sidebar);
        g_signal_connect (priv->book_manager,
                          "book-deleted",
                          G_CALLBACK (sidebar_book_deleted_or_disabled_cb),
                          sidebar);
        g_signal_connect (priv->book_manager,
                          "book-enabled",
                          G_CALLBACK (sidebar_book_created_or_enabled_cb),
                          sidebar);
        g_signal_connect (priv->book_manager,
                          "book-disabled",
                          G_CALLBACK (sidebar_book_deleted_or_disabled_cb),
                          sidebar);

        priv->book_tree = dh_book_tree_new (priv->book_manager);
        gtk_widget_show (priv->book_tree);
        g_signal_connect (priv->book_tree,
                          "link-selected",
                          G_CALLBACK (sidebar_book_tree_link_selected_cb),
                          sidebar);
        gtk_container_add (GTK_CONTAINER (priv->sw_book_tree), priv->book_tree);
        gtk_box_pack_end (GTK_BOX (sidebar), priv->sw_book_tree, TRUE, TRUE, 0);

        sidebar_completion_populate (sidebar);

        dh_keyword_model_set_words (priv->model, priv->book_manager);

        G_OBJECT_CLASS (dh_sidebar_parent_class)->constructed (object);
}

static void
dh_sidebar_class_init (DhSidebarClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = dh_sidebar_finalize;
        object_class->dispose = dh_sidebar_dispose;
        object_class->get_property = dh_sidebar_get_property;
        object_class->set_property = dh_sidebar_set_property;
        object_class->constructed = dh_sidebar_constructed;

        g_object_class_install_property (object_class,
                                         PROP_BOOK_MANAGER,
                                         g_param_spec_object ("book-manager",
                                                              "Book Manager",
                                                              "The book maanger",
                                                              DH_TYPE_BOOK_MANAGER,
                                                              G_PARAM_READWRITE |
                                                              G_PARAM_CONSTRUCT_ONLY));

        signals[LINK_SELECTED] =
                g_signal_new ("link_selected",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (DhSidebarClass, link_selected),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1, G_TYPE_POINTER);
}
