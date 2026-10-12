#include "storage.h"

guint word_count(const char* text) {
  guint count = 0;
  bool in_word = false;

  for (const char* cursor = text; *cursor != '\0'; cursor = g_utf8_next_char(cursor)) {
    if (g_unichar_isspace(g_utf8_get_char(cursor))) {
      in_word = false;
    } else if (!in_word) {
      in_word = true;
      ++count;
    }
  }

  return count;
}

std::string document_key(const std::string& path) {
  char* checksum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.c_str(), -1);
  std::string key = checksum;
  g_free(checksum);
  return key;
}
