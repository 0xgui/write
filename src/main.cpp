#include <gtk/gtk.h>

#include "ui.h"

int main(int argc, char** argv) {
  GtkApplication* app = gtk_application_new("com.silvags.Write", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
