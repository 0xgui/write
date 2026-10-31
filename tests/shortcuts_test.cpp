// Keeps the README keybinds table in step with the in-app sheet: `make test`.
#include <glib.h>

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "../src/shortcuts.h"

int main() {
  int failures = 0;
  char* readme = nullptr;
  if (!g_file_get_contents("README.md", &readme, nullptr, nullptr)) {
    std::fprintf(stderr, "FAIL: could not read README.md (run from the repository root)\n");
    return 1;
  }
  std::set<std::string> seen;
  for (const Shortcut& shortcut : kShortcuts) {
    if (*shortcut.keys == '\0' || *shortcut.action == '\0') {
      std::fprintf(stderr, "FAIL: empty shortcut entry\n");
      ++failures;
    }
    if (!seen.insert(shortcut.keys).second) {
      std::fprintf(stderr, "FAIL: %s is listed twice\n", shortcut.keys);
      ++failures;
    }
    const std::string row = std::string("| `") + shortcut.keys + "` |";
    if (std::strstr(readme, row.c_str()) == nullptr) {
      std::fprintf(stderr, "FAIL: README keybinds table has no row for %s\n", shortcut.keys);
      ++failures;
    }
  }
  g_free(readme);
  if (failures == 0) std::printf("shortcuts tests passed\n");
  return failures == 0 ? 0 : 1;
}
