/* bz-application.c
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
#include <glib/gi18n.h>

#include "bz-application.h"
#include "bz-window.h"

struct _BzApplication
{
  AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (BzApplication, bz_application, ADW_TYPE_APPLICATION)

BzApplication *
bz_application_new (const char       *application_id,
                    GApplicationFlags flags)
{
  g_return_val_if_fail (application_id != NULL, NULL);

  return g_object_new (
      BZ_TYPE_APPLICATION,
      "application-id", application_id,
      "flags", flags,
      "resource-base-path", "/io/github/kolunmi",
      NULL);
}

static void
bz_application_activate (GApplication *app)
{
  GtkWindow *window;

  g_assert (BZ_IS_APPLICATION (app));

  window = gtk_application_get_active_window (GTK_APPLICATION (app));

  if (window == NULL)
    {
      g_autoptr (GtkCssProvider) css = NULL;

      css = gtk_css_provider_new ();
      gtk_css_provider_load_from_resource (
          css, "/io/github/kolunmi/bazaar/gtk/styles.css");
      gtk_style_context_add_provider_for_display (
          gdk_display_get_default (),
          GTK_STYLE_PROVIDER (css),
          GTK_STYLE_PROVIDER_PRIORITY_USER);

      window = g_object_new (
          BZ_TYPE_WINDOW,
          "application", app,
          NULL);
    }

  gtk_window_present (window);
}

static void
bz_application_class_init (BzApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  app_class->activate = bz_application_activate;
}

static void
bz_application_refresh_action (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
  BzApplication *self   = user_data;
  GtkWindow     *window = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  bz_window_refresh (BZ_WINDOW (window));
}

static void
bz_application_browse_action (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
  BzApplication *self   = user_data;
  GtkWindow     *window = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  bz_window_browse (BZ_WINDOW (window));
}

static void
bz_application_search_action (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
  BzApplication *self   = user_data;
  GtkWindow     *window = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  bz_window_search (BZ_WINDOW (window));
}

static void
bz_application_about_action (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
  static const char *developers[] = { "Adam Masciola", NULL };
  BzApplication     *self         = user_data;
  GtkWindow         *window       = NULL;

  g_assert (BZ_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  adw_show_about_dialog (
      GTK_WIDGET (window),
      "application-name", "Bazaar",
      "application-icon", "io.github.kolunmi.bazaar",
      "developer-name", "Adam Masciola",
      "translator-credits", _ ("translator-credits"),
      "version", "0.1.0",
      "developers", developers,
      "copyright", "© 2025 Adam Masciola",
      NULL);
}

static void
bz_application_quit_action (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       user_data)
{
  BzApplication *self = user_data;

  g_assert (BZ_IS_APPLICATION (self));

  g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
  {    "quit",    bz_application_quit_action },
  {   "about",   bz_application_about_action },
  {  "search",  bz_application_search_action },
  {  "browse",  bz_application_browse_action },
  { "refresh", bz_application_refresh_action },
};

static void
bz_application_init (BzApplication *self)
{
  g_action_map_add_action_entries (
      G_ACTION_MAP (self),
      app_actions,
      G_N_ELEMENTS (app_actions),
      self);
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.quit",
      (const char *[]) { "<primary>q", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.search",
      (const char *[]) { "<primary>f", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.browse",
      (const char *[]) { "<primary>b", NULL });
  gtk_application_set_accels_for_action (
      GTK_APPLICATION (self),
      "app.refresh",
      (const char *[]) { "<primary>r", NULL });
}
