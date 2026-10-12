#include "positions.h"

#include "storage.h"

GKeyFile* positions_load(const std::string& positions_path) {
  GKeyFile* positions = g_key_file_new();
  g_key_file_load_from_file(positions, positions_path.c_str(), G_KEY_FILE_NONE, nullptr);
  return positions;
}

int positions_lookup(GKeyFile* positions, const std::string& document_path) {
  const std::string key = document_key(document_path);
  return g_key_file_get_integer(positions, "cursor-positions", key.c_str(), nullptr);
}

void positions_store(GKeyFile* positions, const std::string& positions_path,
                     const std::string& document_path, int offset) {
  const std::string key = document_key(document_path);
  g_key_file_set_integer(positions, "cursor-positions", key.c_str(), offset);

  char* directory = g_path_get_dirname(positions_path.c_str());
  g_mkdir_with_parents(directory, 0700);
  g_free(directory);
  gsize length = 0;
  char* contents = g_key_file_to_data(positions, &length, nullptr);
  g_file_set_contents(positions_path.c_str(), contents, static_cast<gssize>(length), nullptr);
  g_free(contents);
}
