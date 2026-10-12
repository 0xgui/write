#include "fonts.h"

#include <fontconfig/fontconfig.h>
#include <glib.h>

#include <string>
#include <vector>

#ifndef WRITE_DATA_DIR
#define WRITE_DATA_DIR "/usr/local/share/write"
#endif

namespace {
constexpr auto kLiterataFilename = "Literata-VariableFont_opsz,wght.ttf";
}  // namespace

namespace {

std::string find_literata() {
  std::vector<std::string> candidates;
  // 1. Relative to the executable: an installed prefix, or the source tree.
  if (char* exe = g_file_read_link("/proc/self/exe", nullptr)) {
    char* bin_dir = g_path_get_dirname(exe);
    candidates.push_back(std::string(bin_dir) + "/../share/write");
    candidates.push_back(std::string(bin_dir) + "/../assets");
    g_free(bin_dir);
    g_free(exe);
  }
  candidates.push_back("assets");
  // 2. XDG data directories.
  candidates.push_back(std::string(g_get_user_data_dir()) + "/write");
  for (const char* const* dir = g_get_system_data_dirs(); *dir != nullptr; ++dir) {
    candidates.push_back(std::string(*dir) + "/write");
  }
  // 3. The prefix chosen at build time.
  candidates.push_back(WRITE_DATA_DIR);

  for (const auto& dir : candidates) {
    const std::string path = dir + "/" + kLiterataFilename;
    if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return path;
  }
  return {};
}

}  // namespace

bool register_bundled_literata() {
  const std::string font_path = find_literata();
  if (font_path.empty()) {
    g_warning("Write could not find its bundled Literata font");
    return false;
  }
  if (!FcConfigAppFontAddFile(nullptr, reinterpret_cast<const FcChar8*>(font_path.c_str()))) {
    g_warning("Write could not register the bundled Literata font");
    return false;
  }
  FcConfigBuildFonts(nullptr);
  return true;
}
