// Small, dependency-free tests for the GTK-free storage code: `make test`.
#include <glib.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/storage.h"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string read_file(const std::string& path) {
  char* text = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(path.c_str(), &text, &length, nullptr)) return "<missing>";
  std::string result(text, length);
  g_free(text);
  return result;
}

int count_entries(const std::string& directory) {
  int count = 0;
  if (GDir* dir = g_dir_open(directory.c_str(), 0, nullptr)) {
    while (g_dir_read_name(dir) != nullptr) ++count;
    g_dir_close(dir);
  }
  return count;
}

std::string oldest_name(const std::string& directory) {
  std::string oldest;
  if (GDir* dir = g_dir_open(directory.c_str(), 0, nullptr)) {
    while (const char* name = g_dir_read_name(dir)) {
      if (oldest.empty() || name < oldest) oldest = name;
    }
    g_dir_close(dir);
  }
  return oldest;
}

}  // namespace

int main() {
  char* root_c = g_dir_make_tmp("write-test-XXXXXX", nullptr);
  const std::string root = root_c;
  g_free(root_c);

  check(word_count("") == 0, "empty text has no words");
  check(word_count("  one two\nthree ") == 3, "counts words across whitespace");

  const std::string doc_dir = root + "/docs";
  g_mkdir_with_parents(doc_dir.c_str(), 0700);
  const std::string doc = doc_dir + "/note.txt";
  std::string error;
  check(atomic_write(doc, "first", 5, &error), "atomic write creates a file");
  check(read_file(doc) == "first", "atomic write stores contents");
  check(atomic_write(doc, "second draft", 12, &error), "atomic write replaces a file");
  check(read_file(doc) == "second draft", "atomic write replaces contents");
  check(count_entries(doc_dir) == 1, "atomic write leaves no temp files or ~ copies");
  check(!atomic_write(root + "/missing/dir/x.txt", "x", 1, &error) && !error.empty(),
        "atomic write reports failure");
  check(file_mtime(doc) > 0 && file_mtime(root + "/nope") == -1, "file_mtime");

  check(backup_due(0, 1), "first backup is always due");
  check(!backup_due(1000, 1000 + kBackupIntervalUs - 1), "backup not due within 5 minutes");
  check(backup_due(1000, 1000 + kBackupIntervalUs), "backup due after 5 minutes");

  const std::string backups = root + "/backups";
  const gint64 start = G_GINT64_CONSTANT(1700000000) * G_USEC_PER_SEC;
  for (int i = 0; i < 25; ++i) {
    const std::string text = "version " + std::to_string(i);
    check(write_backup(backups, doc, text.data(), text.size(), start + i * kBackupIntervalUs, 20, &error),
          "write_backup succeeds");
  }
  const std::string dir = backup_dir(backups, doc);
  check(count_entries(dir) == 20, "only the newest 20 backups are kept");
  // The oldest kept snapshot is version 5.
  bool found_oldest = false;
  bool found_pruned = false;
  if (GDir* d = g_dir_open(dir.c_str(), 0, nullptr)) {
    while (const char* name = g_dir_read_name(d)) {
      const std::string contents = read_file(dir + "/" + name);
      if (contents == "version 5") found_oldest = true;
      if (contents == "version 4") found_pruned = true;
    }
    g_dir_close(d);
  }
  check(found_oldest && !found_pruned, "pruning removes the oldest backups");
  check(write_backup(backups, doc, "version 24", 10, start + 99 * kBackupIntervalUs, 20, &error)
        && !found_pruned && read_file(dir + "/" + oldest_name(dir)) == "version 5", "identical consecutive backups are skipped");
  check(backup_dir(backups, doc) != backup_dir(backups, doc + "2"), "backups are separated per document");

  char* argv[] = {const_cast<char*>("rm"), const_cast<char*>("-rf"), const_cast<char*>(root.c_str()), nullptr};
  g_spawn_sync(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

  if (failures == 0) std::puts("storage tests passed");
  return failures == 0 ? 0 : 1;
}
