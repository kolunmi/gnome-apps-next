/* bz-update-dialog.c
 *
 * Copyright 2025 Adam Masciola
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include "bz-entry.h"
#include "bz-update-dialog.h"

struct _BzUpdateDialog
{
  AdwAlertDialog parent_instance;

  GListModel *updates;
  gboolean    install_accepted;

  /* Template widgets */
  GtkListView    *list_view;
  GtkNoSelection *selection_model;
};

G_DEFINE_FINAL_TYPE (BzUpdateDialog, bz_update_dialog, ADW_TYPE_ALERT_DIALOG)

static void
on_response (AdwAlertDialog *alert,
             gchar          *response,
             BzUpdateDialog *self);

static void
bz_update_dialog_dispose (GObject *object)
{
  BzUpdateDialog *self = BZ_UPDATE_DIALOG (object);

  g_clear_object (&self->updates);

  G_OBJECT_CLASS (bz_update_dialog_parent_class)->dispose (object);
}

static void
bz_update_dialog_class_init (BzUpdateDialogClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = bz_update_dialog_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/bazaar/bz-update-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, BzUpdateDialog, list_view);
  gtk_widget_class_bind_template_child (widget_class, BzUpdateDialog, selection_model);
}

static void
bz_update_dialog_init (BzUpdateDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
  g_signal_connect (self, "response", G_CALLBACK (on_response), self);
}

static void
on_response (AdwAlertDialog *alert,
             gchar          *response,
             BzUpdateDialog *self)
{
  self->install_accepted = g_strcmp0 (response, "install") == 0;
}

AdwDialog *
bz_update_dialog_new (GListModel *updates)
{
  BzUpdateDialog *update_dialog = NULL;

  g_return_val_if_fail (G_IS_LIST_MODEL (updates), NULL);
  g_return_val_if_fail (g_list_model_get_item_type (updates) == BZ_TYPE_ENTRY, NULL);

  update_dialog          = g_object_new (BZ_TYPE_UPDATE_DIALOG, NULL);
  update_dialog->updates = g_object_ref (updates);

  gtk_no_selection_set_model (update_dialog->selection_model, updates);

  return ADW_DIALOG (update_dialog);
}

GListModel *
bz_update_dialog_was_accepted (BzUpdateDialog *self)
{
  g_return_val_if_fail (BZ_IS_UPDATE_DIALOG (self), FALSE);

  return self->install_accepted ? g_object_ref (self->updates) : NULL;
}
