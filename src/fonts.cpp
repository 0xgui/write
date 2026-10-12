#include "fonts.h"

#include <fontconfig/fontconfig.h>
#include <glib.h>

#include <string>

#ifndef WRITE_DATA_DIR
#define WRITE_DATA_DIR "/usr/local/share/write"
#endif

namespace {
constexpr auto kLiterataFilename = "Literata-VariableFont_opsz,wght.ttf";
}  // namespace

bool register_bundled_literata() {
  const std::string source_tree_font = std::string("assets/") + kLiterataFilename;
  const std::string installed_font = std::string(WRITE_DATA_DIR) + "/" + kLiterataFilename;
  const std::string& font_path = g_file_test(source_tree_font.c_str(), G_FILE_TEST_EXISTS)
      ? source_tree_font
      : installed_font;

  if (!g_file_test(font_path.c_str(), G_FILE_TEST_EXISTS)) {
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
