#pragma once

// GTK-free document helpers (GLib only), so they can be unit tested.

#include <glib.h>

#include <string>

guint word_count(const char* text);
std::string document_key(const std::string& path);
