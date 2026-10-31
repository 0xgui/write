#pragma once

// GTK-free helpers for the essays folder (GLib only), so they can be unit tested.

#include <glib.h>

#include <string>
#include <vector>

// The first non-blank line, trimmed, without a leading Markdown "#".
std::string essay_title(const char* text);
// True once that first line is followed by a line break, so it reads as finished.
bool essay_title_finished(const char* text);
// A readable, filesystem-safe name: lower case, no slashes or control
// characters, at most about 60 bytes, cut at a word.
std::string essay_slug(const std::string& title);
// "2026-10-31 on solitude.txt", or "2026-10-31 2214.txt" while there is no title.
std::string essay_file_name(const std::string& date, const std::string& time, const std::string& title);
// `directory/name`, or `directory/name 2.txt`, `3`, ... if that is already taken.
std::string essay_unused_path(const std::string& directory, const std::string& name);

struct Essay {
  std::string path;
  std::string title;  // the first line, or the file name without ".txt"
  gint64 modified_us = 0;
};

// The .txt files in `directory`, most recently written first.
std::vector<Essay> essays_list(const std::string& directory);
// Case-insensitive match of `query` against the title or file name.
bool essay_matches(const Essay& essay, const std::string& query);

// The Documents folder's "Essays", or ~/Essays when there is no Documents folder.
std::string default_essays_dir(const char* documents_dir, const std::string& home);
// Expands a leading "~/" so the folder can be written by hand in settings.ini.
std::string expand_home(const std::string& path, const std::string& home);
