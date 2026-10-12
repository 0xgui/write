#pragma once

#include <glib.h>

#include <string>

// Remembered cursor offsets, keyed by a hash of each document path.
GKeyFile* positions_load(const std::string& positions_path);
int positions_lookup(GKeyFile* positions, const std::string& document_path);
void positions_store(GKeyFile* positions, const std::string& positions_path,
                     const std::string& document_path, int offset);
