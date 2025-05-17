/* bz-window.c
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

#include "bz-backend.h"
#include "bz-background.h"
#include "bz-browse-widget.h"
#include "bz-flatpak-instance.h"
#include "bz-search-widget.h"
#include "bz-transaction-manager.h"
#include "bz-update-dialog.h"
#include "bz-window.h"

struct _BzWindow
{
  AdwApplicationWindow parent_instance;

  BzFlatpakInstance *flatpak;
  GListStore        *remote;
  GHashTable        *id_to_entry_hash;

  GListStore *bg_entries;
  BzEntry    *pending_installation;

  /* Template widgets */
  BzBackground         *background;
  BzBrowseWidget       *browse;
  GtkButton            *refresh;
  GtkButton            *search;
  AdwToastOverlay      *toasts;
  AdwSpinner           *spinner;
  BzTransactionManager *transaction_mgr;
  AdwStatusPage        *status;
};

G_DEFINE_FINAL_TYPE (BzWindow, bz_window, ADW_TYPE_APPLICATION_WINDOW)

static void
refresh_clicked (GtkButton *button,
                 BzWindow  *self);
static void
search_clicked (GtkButton *button,
                BzWindow  *self);

static void
gather_entries_progress (BzEntry  *entry,
                         BzWindow *self);

static DexFuture *
refresh_then (DexFuture *future,
              BzWindow  *self);
static DexFuture *
fetch_refs_then (DexFuture *future,
                 BzWindow  *self);
static DexFuture *
fetch_updates_then (DexFuture *future,
                    BzWindow  *self);
static DexFuture *
refresh_catch (DexFuture *future,
               BzWindow  *self);
static DexFuture *
refresh_finally (DexFuture *future,
                 BzWindow  *self);

static void
search_selected_changed (BzSearchWidget *search,
                         GParamSpec     *pspec,
                         BzWindow       *self);

static void
install_confirmation_response (AdwAlertDialog *alert,
                               gchar          *response,
                               BzWindow       *self);

static void
update_dialog_response (BzUpdateDialog *dialog,
                        const char     *response,
                        BzWindow       *self);

static void
refresh (BzWindow *self);

static void
browse (BzWindow *self);

static void
install (BzWindow *self,
         BzEntry  *entry);

static void
try_install (BzWindow *self,
             BzEntry  *entry);

static void
update (BzWindow *self,
        BzEntry **updates,
        guint     n_updates);

static void
search (BzWindow *self);

static void
bz_window_dispose (GObject *object)
{
  BzWindow *self = BZ_WINDOW (object);

  g_clear_pointer (&self->id_to_entry_hash, g_hash_table_unref);
  g_clear_object (&self->remote);
  g_clear_object (&self->flatpak);
  g_clear_object (&self->pending_installation);
  g_clear_object (&self->bg_entries);

  G_OBJECT_CLASS (bz_window_parent_class)->dispose (object);
}

static void
bz_window_class_init (BzWindowClass *klass)
{
  GObjectClass   *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = bz_window_dispose;

  g_type_ensure (BZ_TYPE_BACKGROUND);
  g_type_ensure (BZ_TYPE_TRANSACTION_MANAGER);
  g_type_ensure (BZ_TYPE_BROWSE_WIDGET);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kolunmi/bazaar/bz-window.ui");
  gtk_widget_class_bind_template_child (widget_class, BzWindow, background);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, browse);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, spinner);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, status);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, toasts);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, refresh);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, search);
  gtk_widget_class_bind_template_child (widget_class, BzWindow, transaction_mgr);
}

static void
bz_window_init (BzWindow *self)
{
  GtkEventController *motion_controller = NULL;

  self->remote = g_list_store_new (BZ_TYPE_ENTRY);

  gtk_widget_init_template (GTK_WIDGET (self));
  // bz_browse_widget_set_model (self->browse, G_LIST_MODEL (self->remote));

  g_signal_connect (self->refresh, "clicked", G_CALLBACK (refresh_clicked), self);
  g_signal_connect (self->search, "clicked", G_CALLBACK (search_clicked), self);

  motion_controller = gtk_event_controller_motion_new ();
  gtk_event_controller_set_propagation_limit (motion_controller, GTK_LIMIT_NONE);
  bz_background_set_motion_controller (
      self->background,
      GTK_EVENT_CONTROLLER_MOTION (motion_controller));
  gtk_widget_add_controller (GTK_WIDGET (self), motion_controller);

  refresh (self);
}

static void
refresh_clicked (GtkButton *button,
                 BzWindow  *self)
{
  refresh (self);
}

static void
search_clicked (GtkButton *button,
                BzWindow  *self)
{
  search (self);
}

static void
gather_entries_progress (BzEntry  *entry,
                         BzWindow *self)
{
  g_list_store_append (self->remote, entry);
}

static DexFuture *
refresh_then (DexFuture *future,
              BzWindow  *self)
{
  g_autoptr (GError) local_error  = NULL;
  const GValue *value             = NULL;
  DexFuture    *ref_remote_future = NULL;

  value         = dex_future_get_value (future, &local_error);
  self->flatpak = g_value_dup_object (value);
  bz_transaction_manager_set_backend (self->transaction_mgr, BZ_BACKEND (self->flatpak));

  gtk_widget_set_sensitive (GTK_WIDGET (self->search), self->remote != NULL);

  ref_remote_future = bz_backend_retrieve_remote_entries (
      BZ_BACKEND (self->flatpak),
      (BzBackendGatherEntriesFunc) gather_entries_progress,
      g_object_ref (self), g_object_unref);
  ref_remote_future = dex_future_then (
      ref_remote_future, (DexFutureCallback) fetch_refs_then,
      g_object_ref (self), g_object_unref);
  ref_remote_future = dex_future_then (
      ref_remote_future, (DexFutureCallback) fetch_updates_then,
      g_object_ref (self), g_object_unref);

  return ref_remote_future;
}

static DexFuture *
fetch_refs_then (DexFuture *future,
                 BzWindow  *self)
{
  guint n_entries            = 0;
  g_autoptr (GHashTable) set = NULL;

  n_entries = g_list_model_get_n_items (G_LIST_MODEL (self->remote));

  self->id_to_entry_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
  for (guint i = 0; i < n_entries; i++)
    {
      g_autoptr (BzFlatpakEntry) entry = NULL;
      const char *name                 = NULL;

      entry = g_list_model_get_item (G_LIST_MODEL (self->remote), i);
      name  = bz_flatpak_entry_get_name (entry);

      g_hash_table_replace (
          self->id_to_entry_hash,
          g_strdup (name),
          g_steal_pointer (&entry));
    }

  g_clear_object (&self->bg_entries);
  self->bg_entries = g_list_store_new (BZ_TYPE_ENTRY);
  set              = g_hash_table_new (g_direct_hash, g_direct_equal);

  for (guint safe = 0, found = 0;
       safe < 1000 && found < MIN (20, n_entries);
       safe++)
    {
      guint i                   = 0;
      g_autoptr (BzEntry) entry = NULL;

      i = g_random_int_range (0, n_entries);
      if (g_hash_table_contains (set, GUINT_TO_POINTER (i)))
        continue;

      entry = g_list_model_get_item (G_LIST_MODEL (self->remote), i);
      if (bz_entry_get_icon_paintable (entry) == NULL)
        continue;
      if (!g_str_has_prefix (bz_flatpak_entry_get_name (BZ_FLATPAK_ENTRY (entry)), "org.gnome."))
        continue;

      g_list_store_append (self->bg_entries, entry);
      g_hash_table_add (set, GUINT_TO_POINTER (i));
      found++;
    }

  bz_background_set_entries (self->background, G_LIST_MODEL (self->bg_entries));

  adw_toast_overlay_add_toast (
      self->toasts,
      adw_toast_new_format ("Discovered %d Apps", n_entries));

  return bz_backend_retrieve_update_ids (BZ_BACKEND (self->flatpak));
}

static DexFuture *
fetch_updates_then (DexFuture *future,
                    BzWindow  *self)
{
  g_autoptr (GError) local_error = NULL;
  const GValue *value            = NULL;
  g_autoptr (GPtrArray) names    = NULL;

  value = dex_future_get_value (future, NULL);
  names = g_value_dup_boxed (value);

  if (names->len > 0)
    {
      g_autoptr (GListStore) updates = NULL;

      updates = g_list_store_new (BZ_TYPE_ENTRY);
      for (guint i = 0; i < names->len; i++)
        {
          const char *name  = NULL;
          BzEntry    *entry = NULL;

          name  = g_ptr_array_index (names, i);
          entry = g_hash_table_lookup (self->id_to_entry_hash, name);

          /* FIXME address all refs */
          if (entry != NULL)
            g_list_store_append (updates, entry);
        }

      if (g_list_model_get_n_items (G_LIST_MODEL (updates)) > 0)
        {
          AdwDialog *update_dialog = NULL;

          update_dialog = bz_update_dialog_new (G_LIST_MODEL (updates));
          g_signal_connect (update_dialog, "response", G_CALLBACK (update_dialog_response), self);

          adw_dialog_present (update_dialog, GTK_WIDGET (self));
        }
    }

  return dex_future_new_true ();
}

static DexFuture *
refresh_catch (DexFuture *future,
               BzWindow  *self)
{
  g_autoptr (GError) local_error = NULL;

  dex_future_get_value (future, &local_error);
  adw_toast_overlay_add_toast (self->toasts, adw_toast_new_format ("Failed! %s", local_error->message));

  return dex_future_new_true ();
}

static DexFuture *
refresh_finally (DexFuture *future,
                 BzWindow  *self)
{
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->status), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->browse), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->refresh), TRUE);

  return dex_future_new_true ();
}

static void
search_selected_changed (BzSearchWidget *search,
                         GParamSpec     *pspec,
                         BzWindow       *self)
{
  BzEntry   *entry  = NULL;
  GtkWidget *dialog = NULL;

  entry = bz_search_widget_get_selected (search);
  if (entry != NULL && BZ_IS_FLATPAK_ENTRY (entry))
    try_install (self, entry);

  dialog = gtk_widget_get_ancestor (GTK_WIDGET (search), ADW_TYPE_DIALOG);
  if (dialog != NULL)
    adw_dialog_close (ADW_DIALOG (dialog));
}

static void
install_confirmation_response (AdwAlertDialog *alert,
                               gchar          *response,
                               BzWindow       *self)
{
  if (self->pending_installation != NULL &&
      g_strcmp0 (response, "install") == 0)
    install (self, self->pending_installation);
  else
    g_clear_object (&self->pending_installation);
}

static void
update_dialog_response (BzUpdateDialog *dialog,
                        const char     *response,
                        BzWindow       *self)
{
  g_autoptr (GListModel) updates = NULL;

  updates = bz_update_dialog_was_accepted (dialog);

  if (updates != NULL)
    {
      guint                n_updates   = 0;
      g_autofree BzEntry **updates_buf = NULL;

      n_updates   = g_list_model_get_n_items (updates);
      updates_buf = g_malloc_n (n_updates, sizeof (*updates_buf));

      for (guint i = 0; i < n_updates; i++)
        updates_buf[i] = g_list_model_get_item (updates, i);

      update (self, updates_buf, n_updates);

      for (guint i = 0; i < n_updates; i++)
        g_object_unref (updates_buf[i]);
    }
}

void
bz_window_refresh (BzWindow *self)
{
  g_return_if_fail (BZ_IS_WINDOW (self));

  if (gtk_widget_get_sensitive (GTK_WIDGET (self->refresh)))
    refresh (self);
  else
    adw_toast_overlay_add_toast (
        self->toasts,
        adw_toast_new_format ("Can't refresh right now!"));
}

void
bz_window_browse (BzWindow *self)
{
  g_return_if_fail (BZ_IS_WINDOW (self));

  browse (self);
}

void
bz_window_search (BzWindow *self)
{
  g_return_if_fail (BZ_IS_WINDOW (self));

  if (gtk_widget_get_sensitive (GTK_WIDGET (self->search)))
    search (self);
  else
    adw_toast_overlay_add_toast (
        self->toasts,
        adw_toast_new_format ("Can't search right now!"));
}

static void
refresh (BzWindow *self)
{
  DexFuture *future = NULL;

  gtk_widget_set_visible (GTK_WIDGET (self->spinner), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->status), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->browse), FALSE);

  gtk_widget_set_sensitive (GTK_WIDGET (self->refresh), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->search), FALSE);

  g_clear_pointer (&self->id_to_entry_hash, g_hash_table_unref);
  g_list_store_remove_all (self->remote);
  g_clear_object (&self->flatpak);

  future = bz_flatpak_instance_new ();
  future = dex_future_then (
      future, (DexFutureCallback) refresh_then,
      g_object_ref (self), g_object_unref);
  future = dex_future_catch (
      future, (DexFutureCallback) refresh_catch,
      g_object_ref (self), g_object_unref);
  future = dex_future_finally (
      future, (DexFutureCallback) refresh_finally,
      g_object_ref (self), g_object_unref);
  dex_future_disown (future);
}

static void
browse (BzWindow *self)
{
  bz_background_set_entries (self->background, NULL);

  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->status), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->browse), TRUE);
}

static void
install (BzWindow *self,
         BzEntry  *entry)
{
  g_autoptr (BzTransaction) transaction = NULL;

  transaction = bz_transaction_new_full (
      &entry, 1,
      NULL, 0,
      NULL, 0);
  bz_transaction_manager_add (self->transaction_mgr, transaction);
}

static void
try_install (BzWindow *self,
             BzEntry  *entry)
{
  AdwDialog *alert = NULL;

  g_clear_object (&self->pending_installation);
  self->pending_installation = g_object_ref (entry);

  alert = adw_alert_dialog_new (NULL, NULL);
  adw_alert_dialog_format_heading (
      ADW_ALERT_DIALOG (alert), "Confirm Transaction");
  adw_alert_dialog_format_body_markup (
      ADW_ALERT_DIALOG (alert),
      "You are about to install the following Flatpak:\n\n<b>%s</b>\n<tt>%s</tt>\n\nAre you sure?",
      bz_entry_get_title (entry),
      /* TODO: make this fully backend agnostic */
      bz_flatpak_entry_get_name (BZ_FLATPAK_ENTRY (entry)));
  adw_alert_dialog_add_responses (
      ADW_ALERT_DIALOG (alert),
      "cancel", "Cancel",
      "install", "Install",
      NULL);
  adw_alert_dialog_set_response_appearance (
      ADW_ALERT_DIALOG (alert), "install", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (alert), "cancel");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (alert), "cancel");

  g_signal_connect (alert, "response", G_CALLBACK (install_confirmation_response), self);
  adw_dialog_present (alert, GTK_WIDGET (self));
}

static void
update (BzWindow *self,
        BzEntry **updates,
        guint     n_updates)
{
  g_autoptr (BzTransaction) transaction = NULL;

  transaction = bz_transaction_new_full (
      NULL, 0,
      updates, n_updates,
      NULL, 0);
  bz_transaction_manager_add (self->transaction_mgr, transaction);
}

static void
search (BzWindow *self)
{
  GtkWidget *search_widget = NULL;
  AdwDialog *dialog        = NULL;

  search_widget = bz_search_widget_new (G_LIST_MODEL (self->remote));
  dialog        = adw_dialog_new ();

  g_signal_connect (search_widget, "notify::selected",
                    G_CALLBACK (search_selected_changed), self);

  adw_dialog_set_child (dialog, search_widget);
  adw_dialog_set_content_width (dialog, 1500);
  adw_dialog_set_content_height (dialog, 1200);

  adw_dialog_present (dialog, GTK_WIDGET (self));
}
