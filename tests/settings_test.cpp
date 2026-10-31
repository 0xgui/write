// Tests for the GTK-free preferences code: `make test`.
#include <glib.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <string>

#include "../src/settings.h"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void write_file(const std::string& path, const char* text) {
  g_file_set_contents(path.c_str(), text, -1, nullptr);
}

}  // namespace

int main() {
  char* root_c = g_dir_make_tmp("write-settings-XXXXXX", nullptr);
  const std::string root = root_c;
  g_free(root_c);
  const std::string path = root + "/nested/settings.ini";

  const Settings fresh = settings_load(path);
  check(fresh.paper == Paper::Light && !fresh.paper_saved, "a missing file means no saved paper");
  check(fresh.font_size == kDefaultFontSize && fresh.show_word_count, "a missing file gives the defaults");

  Settings chosen;
  chosen.paper = Paper::Night;
  chosen.paper_saved = true;
  chosen.font_size = 23;
  chosen.show_word_count = false;
  check(settings_save(path, chosen), "settings save into a new folder");
  const Settings loaded = settings_load(path);
  check(loaded.paper == Paper::Night && loaded.paper_saved, "the chosen paper survives a relaunch");
  check(loaded.font_size == 23, "the text size survives a relaunch");
  check(!loaded.show_word_count, "the word count choice survives a relaunch");

  Settings unchosen;
  unchosen.font_size = 18;
  settings_save(root + "/unchosen.ini", unchosen);
  check(!settings_load(root + "/unchosen.ini").paper_saved, "an unchosen paper is not written, so the system default still applies");

  write_file(root + "/odd.ini", "[preferences]\npaper=sepia\nfont-size=400\nword-count=maybe\n");
  const Settings odd = settings_load(root + "/odd.ini");
  check(!odd.paper_saved && odd.paper == Paper::Light, "an unknown paper is ignored");
  check(odd.font_size == kLargestFontSize, "the text size is clamped");
  check(odd.show_word_count, "a broken word-count value keeps the default");

  write_file(root + "/tiny.ini", "[preferences]\nfont-size=2\npaper=dark\n");
  const Settings tiny = settings_load(root + "/tiny.ini");
  check(tiny.font_size == kSmallestFontSize && tiny.paper == Paper::Dark, "small sizes clamp and dark loads");

  check(next_paper(Paper::Light) == Paper::Dark && next_paper(Paper::Dark) == Paper::Night
        && next_paper(Paper::Night) == Paper::Light, "papers cycle Light, Dark, Night");

  char* argv[] = {const_cast<char*>("rm"), const_cast<char*>("-rf"), const_cast<char*>(root.c_str()), nullptr};
  g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

  if (failures == 0) std::puts("settings tests passed");
  return failures == 0 ? 0 : 1;
}
