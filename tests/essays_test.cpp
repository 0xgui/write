// Tests for the GTK-free essays folder helpers: `make test`.
#include <glib.h>
#include <glib/gstdio.h>

#include <utime.h>

#include <cstdio>
#include <string>

#include "../src/essays.h"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

void write_file(const std::string& path, const char* text, gint64 mtime_s) {
  g_file_set_contents(path.c_str(), text, -1, nullptr);
  struct utimbuf times = {static_cast<time_t>(mtime_s), static_cast<time_t>(mtime_s)};
  g_utime(path.c_str(), &times);
}

}  // namespace

int main() {
  check(essay_title("\n\n  On solitude  \nIt begins.") == "On solitude", "the title is the first non-blank line");
  check(essay_title("# On solitude\n") == "On solitude", "a Markdown heading mark is dropped");
  check(essay_title(" \n\t\n").empty(), "a blank page has no title");
  check(!essay_title_finished("On solit"), "a title still being typed is unfinished");
  check(essay_title_finished("On solitude\n\n"), "a line break finishes the title");
  check(!essay_title_finished("\n\n"), "blank lines are not a title");

  check(essay_slug("On Solitude") == "on solitude", "slugs are lower case and keep spaces");
  check(essay_slug("  What/is\\a: \"home\"?  ") == "what is a home", "unsafe characters become spaces");
  check(essay_slug("Ça, c'est l'été — encore") == "ça, c'est l'été encore", "accents and gentle punctuation stay");
  check(essay_slug("../../etc/passwd") == "etc passwd", "paths cannot escape the folder");
  check(essay_slug("...").empty() && essay_slug("").empty(), "punctuation alone gives no slug");
  check(essay_slug(std::string(1, '\x01') + "bell\ttab") == "bell tab", "control characters are dropped");
  const std::string long_slug = essay_slug(
      "a very long first line that just keeps going because the writer forgot to stop and press enter");
  check(long_slug.size() <= 60 && long_slug.back() != ' ' && long_slug.starts_with("a very long first line"),
        "long titles are cut at a word");
  check(essay_slug("ééééééééééééééééééééééééééééééééééééééé").size() <= 60, "long words are cut on a character");
  check(g_utf8_validate(essay_slug("ééééééééééééééééééééééééééééééééééééééé").c_str(), -1, nullptr),
        "cut slugs stay valid UTF-8");

  check(essay_file_name("2026-10-31", "2214", "On solitude") == "2026-10-31 on solitude.txt", "named by date and title");
  check(essay_file_name("2026-10-31", "2214", "") == "2026-10-31 2214.txt", "named by date and time without a title");

  char* root_c = g_dir_make_tmp("write-essays-XXXXXX", nullptr);
  const std::string root = root_c;
  g_free(root_c);
  check(essays_list(root + "/missing").empty(), "a missing folder lists nothing");

  write_file(root + "/2026-10-29 old.txt", "Old thoughts\nbody", 1000);
  write_file(root + "/2026-10-31 on solitude.txt", "\nOn solitude\n\nIt begins.", 3000);
  write_file(root + "/2026-10-30 2214.txt", "", 2000);
  write_file(root + "/notes.md", "not an essay", 4000);
  write_file(root + "/.hidden.txt", "hidden", 5000);
  const auto essays = essays_list(root);
  check(essays.size() == 3, "only visible .txt files are listed");
  if (essays.size() == 3) {
    check(essays[0].title == "On solitude" && essays[1].title == "2026-10-30 2214" && essays[2].title == "Old thoughts",
          "newest first, titled by the first line or the file name");
    check(essay_matches(essays[0], "SOLI") && !essay_matches(essays[0], "old"), "filtering is case-insensitive");
    check(essay_matches(essays[1], "10-30"), "filtering also matches the file name");
    check(essay_matches(essays[2], "  "), "an empty filter matches everything");
  }

  check(essay_unused_path(root, "2026-10-31 on solitude.txt") == root + "/2026-10-31 on solitude 2.txt",
        "a taken name gets a number");
  write_file(root + "/2026-10-31 on solitude 2.txt", "x", 1);
  check(essay_unused_path(root, "2026-10-31 on solitude.txt") == root + "/2026-10-31 on solitude 3.txt",
        "numbers keep counting");
  check(essay_unused_path(root, "2026-11-01 fresh.txt") == root + "/2026-11-01 fresh.txt", "a free name is kept");

  check(default_essays_dir("/home/a/Documents", "/home/a") == "/home/a/Documents/Essays", "essays live in Documents");
  check(default_essays_dir(nullptr, "/home/a") == "/home/a/Essays", "without Documents, essays live at home");
  check(default_essays_dir("/home/a", "/home/a") == "/home/a/Essays", "Documents set to home is not used");
  check(expand_home("~/Essays", "/home/a") == "/home/a/Essays" && expand_home("/x", "/home/a") == "/x",
        "a leading ~ means home");

  char* argv[] = {const_cast<char*>("rm"), const_cast<char*>("-rf"), const_cast<char*>(root.c_str()), nullptr};
  g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

  if (failures == 0) std::puts("essays tests passed");
  return failures == 0 ? 0 : 1;
}
