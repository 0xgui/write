#include "settings.h"

#include <glib.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

constexpr const char* kGroup = "preferences";

const char* paper_key(Paper paper) {
  switch (paper) {
    case Paper::Dark: return "dark";
    case Paper::Night: return "night";
    case Paper::Light: break;
  }
  return "light";
}

}  // namespace

Paper next_paper(Paper paper) {
  switch (paper) {
    case Paper::Light: return Paper::Dark;
    case Paper::Dark: return Paper::Night;
    case Paper::Night: break;
  }
  return Paper::Light;
}

const char* paper_name(Paper paper) {
  switch (paper) {
    case Paper::Dark: return "Dark paper";
    case Paper::Night: return "Night paper";
    case Paper::Light: break;
  }
  return "Light paper";
}

Settings settings_load(const std::string& settings_path) {
  Settings settings;
  GKeyFile* file = g_key_file_new();
  if (g_key_file_load_from_file(file, settings_path.c_str(), G_KEY_FILE_NONE, nullptr)) {
    if (char* paper = g_key_file_get_string(file, kGroup, "paper", nullptr)) {
      for (Paper candidate : {Paper::Light, Paper::Dark, Paper::Night}) {
        if (std::strcmp(paper, paper_key(candidate)) == 0) {
          settings.paper = candidate;
          settings.paper_saved = true;
        }
      }
      g_free(paper);
    }
    GError* error = nullptr;
    const int size = g_key_file_get_integer(file, kGroup, "font-size", &error);
    if (error == nullptr) settings.font_size = std::clamp(size, kSmallestFontSize, kLargestFontSize);
    g_clear_error(&error);
    for (auto [key, value] : {std::pair{"word-count", &settings.show_word_count},
                              std::pair{"focus-dimming", &settings.focus_dimming},
                              std::pair{"typewriter-scrolling", &settings.typewriter}}) {
      const bool flag = g_key_file_get_boolean(file, kGroup, key, &error);
      if (error == nullptr) *value = flag;
      g_clear_error(&error);
    }
    for (auto [key, value] : {std::pair{"essays-folder", &settings.essays_dir},
                              std::pair{"last-document", &settings.last_document}}) {
      if (char* text = g_key_file_get_string(file, kGroup, key, nullptr)) {
        *value = text;
        g_free(text);
      }
    }
  }
  g_key_file_unref(file);
  return settings;
}

bool settings_save(const std::string& settings_path, const Settings& settings) {
  GKeyFile* file = g_key_file_new();
  // Keep keys a future version may add, and comments the writer left.
  g_key_file_load_from_file(file, settings_path.c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);
  if (settings.paper_saved) g_key_file_set_string(file, kGroup, "paper", paper_key(settings.paper));
  g_key_file_set_integer(file, kGroup, "font-size", settings.font_size);
  g_key_file_set_boolean(file, kGroup, "word-count", settings.show_word_count);
  g_key_file_set_boolean(file, kGroup, "focus-dimming", settings.focus_dimming);
  g_key_file_set_boolean(file, kGroup, "typewriter-scrolling", settings.typewriter);
  if (!settings.essays_dir.empty()) g_key_file_set_string(file, kGroup, "essays-folder", settings.essays_dir.c_str());
  if (!settings.last_document.empty()) {
    g_key_file_set_string(file, kGroup, "last-document", settings.last_document.c_str());
  }

  char* directory = g_path_get_dirname(settings_path.c_str());
  g_mkdir_with_parents(directory, 0700);
  g_free(directory);
  gsize length = 0;
  char* contents = g_key_file_to_data(file, &length, nullptr);
  const bool saved = g_file_set_contents(settings_path.c_str(), contents, static_cast<gssize>(length), nullptr);
  g_free(contents);
  g_key_file_unref(file);
  return saved;
}
