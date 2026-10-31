#pragma once

#include <string>

// The writer's preferences, kept in a small GKeyFile between launches.
enum class Paper { Light, Dark, Night };

constexpr int kDefaultFontSize = 20;
constexpr int kSmallestFontSize = 14;
constexpr int kLargestFontSize = 32;

struct Settings {
  Paper paper = Paper::Light;
  bool paper_saved = false;  // false until the writer picks a paper
  int font_size = kDefaultFontSize;
  bool show_word_count = true;
};

Paper next_paper(Paper paper);
const char* paper_name(Paper paper);  // "Light paper", "Dark paper", "Night paper"

Settings settings_load(const std::string& settings_path);
bool settings_save(const std::string& settings_path, const Settings& settings);
