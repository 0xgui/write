#pragma once

#include <array>

// Every shortcut Write knows, in the order the Ctrl+/ sheet lists them.
// tests/shortcuts_test.cpp checks the README keybinds table against this list.
struct Shortcut {
  const char* keys;
  const char* action;
};

inline constexpr std::array<Shortcut, 20> kShortcuts = {{
    {"Enter", "New paragraph"},
    {"Shift+Enter", "New line"},
    {"Ctrl+N", "New essay"},
    {"Ctrl+E", "Switch essay"},
    {"Ctrl+O", "Open a text file"},
    {"Ctrl+S", "Save now"},
    {"Ctrl+Shift+S", "Save as"},
    {"Ctrl+F", "Find"},
    {"Ctrl+R", "Reload from disk"},
    {"Ctrl+Shift+D", "Paper: Light, Dark, Night"},
    {"Ctrl+Shift+F", "Focus dimming"},
    {"Ctrl+Shift+T", "Typewriter scrolling"},
    {"Ctrl+Shift+W", "Word count"},
    {"Ctrl+=", "Larger text"},
    {"Ctrl+-", "Smaller text"},
    {"Ctrl+0", "Reset text size"},
    {"Ctrl+Shift+I", "Show the typeface"},
    {"F11", "Fullscreen, no header"},
    {"Ctrl+/", "These shortcuts"},
    {"F1", "These shortcuts"},
}};
