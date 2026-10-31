#include "essays.h"

#include <glib/gstdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr gsize kLongestSlug = 60;

std::string trim(const std::string& text) {
  const auto first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// The first non-blank line and whether a line break ends it.
std::string first_line(const char* text, bool* finished) {
  *finished = false;
  const char* line = text;
  while (*line != '\0') {
    const char* end = std::strchr(line, '\n');
    const std::string candidate = trim(end != nullptr ? std::string(line, end) : std::string(line));
    if (!candidate.empty()) {
      *finished = end != nullptr;
      return candidate;
    }
    if (end == nullptr) break;
    line = end + 1;
  }
  return "";
}

std::string casefold(const std::string& text) {
  char* folded = g_utf8_casefold(text.c_str(), -1);
  std::string result = folded;
  g_free(folded);
  return result;
}

}  // namespace

std::string essay_title(const char* text) {
  bool finished = false;
  std::string title = first_line(text, &finished);
  const auto start = title.find_first_not_of("# ");
  return start == std::string::npos ? "" : title.substr(start);
}

bool essay_title_finished(const char* text) {
  bool finished = false;
  return !first_line(text, &finished).empty() && finished;
}

std::string essay_slug(const std::string& title) {
  if (!g_utf8_validate(title.c_str(), -1, nullptr)) return "";
  char* lower = g_utf8_strdown(title.c_str(), -1);
  std::string slug;
  bool space = false;
  for (const char* p = lower; *p != '\0'; p = g_utf8_next_char(p)) {
    const gunichar c = g_utf8_get_char(p);
    // Keep letters, digits and gentle punctuation; everything else is a space.
    const bool keep = g_unichar_isalnum(c) || (c < 0x80 && std::strchr("'-,&()", static_cast<int>(c)) != nullptr);
    if (!keep) {
      space = !slug.empty();
      continue;
    }
    if (space) slug += ' ';
    space = false;
    slug.append(p, g_utf8_next_char(p) - p);
  }
  g_free(lower);
  if (slug.size() > kLongestSlug) {
    // Cut at the last whole word that fits, never inside a UTF-8 character.
    std::string cut = slug.substr(0, kLongestSlug + 1);
    const auto at = cut.rfind(' ');
    cut = at != std::string::npos && at > 0 ? cut.substr(0, at) : slug.substr(0, kLongestSlug);
    const char* end = nullptr;
    g_utf8_validate(cut.c_str(), static_cast<gssize>(cut.size()), &end);
    slug = cut.substr(0, end - cut.c_str());
  }
  while (!slug.empty() && (slug.back() == ' ' || slug.back() == '-' || slug.back() == ',')) slug.pop_back();
  while (!slug.empty() && slug.front() == '-') slug.erase(0, 1);
  return slug;
}

std::string essay_file_name(const std::string& date, const std::string& time, const std::string& title) {
  const std::string slug = essay_slug(title);
  return date + " " + (slug.empty() ? time : slug) + ".txt";
}

std::string essay_unused_path(const std::string& directory, const std::string& name) {
  const std::string stem = name.size() > 4 && name.ends_with(".txt") ? name.substr(0, name.size() - 4) : name;
  std::string path = directory + "/" + stem + ".txt";
  for (int n = 2; g_file_test(path.c_str(), G_FILE_TEST_EXISTS); ++n) {
    path = directory + "/" + stem + " " + std::to_string(n) + ".txt";
  }
  return path;
}

std::vector<Essay> essays_list(const std::string& directory) {
  std::vector<Essay> essays;
  GDir* dir = g_dir_open(directory.c_str(), 0, nullptr);
  if (dir == nullptr) return essays;
  while (const char* name = g_dir_read_name(dir)) {
    if (name[0] == '.' || !g_str_has_suffix(name, ".txt")) continue;
    Essay essay;
    essay.path = directory + "/" + name;
    GStatBuf info;
    if (g_stat(essay.path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) continue;
    essay.modified_us = static_cast<gint64>(info.st_mtim.tv_sec) * G_USEC_PER_SEC + info.st_mtim.tv_nsec / 1000;
    char* text = nullptr;
    if (g_file_get_contents(essay.path.c_str(), &text, nullptr, nullptr) && g_utf8_validate(text, -1, nullptr)) {
      essay.title = essay_title(text);
    }
    g_free(text);
    if (essay.title.empty()) essay.title = std::string(name, std::strlen(name) - 4);
    essays.push_back(std::move(essay));
  }
  g_dir_close(dir);
  std::sort(essays.begin(), essays.end(), [](const Essay& a, const Essay& b) {
    return a.modified_us != b.modified_us ? a.modified_us > b.modified_us : a.path < b.path;
  });
  return essays;
}

bool essay_matches(const Essay& essay, const std::string& query) {
  const std::string needle = casefold(trim(query));
  if (needle.empty()) return true;
  char* base = g_path_get_basename(essay.path.c_str());
  const bool found = casefold(essay.title).find(needle) != std::string::npos
                  || casefold(base).find(needle) != std::string::npos;
  g_free(base);
  return found;
}

std::string default_essays_dir(const char* documents_dir, const std::string& home) {
  // GLib may report the home folder itself when no Documents folder is configured.
  if (documents_dir != nullptr && *documents_dir != '\0' && home != documents_dir) {
    return std::string(documents_dir) + "/Essays";
  }
  return home + "/Essays";
}

std::string expand_home(const std::string& path, const std::string& home) {
  if (path == "~") return home;
  if (path.starts_with("~/")) return home + path.substr(1);
  return path;
}
