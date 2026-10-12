#pragma once

#include <gtk/gtk.h>

void on_activate(GtkApplication* app, gpointer);
void on_open(GApplication* app, GFile** files, int count, const char* hint, gpointer);
