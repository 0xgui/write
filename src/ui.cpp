#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "essays.h"
#include "fonts.h"
#include "positions.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"

namespace {

constexpr auto kSaveDelayMs = 350;
constexpr auto kReadingWidth = 640;
constexpr auto kBottomPaperMargin = 260;

struct Writer {
  GtkTextBuffer* buffer = nullptr;
  GtkTextView* editor = nullptr;
  GtkScrolledWindow* scroller = nullptr;
  GtkLabel* status = nullptr;
  GtkLabel* notification = nullptr;
  GtkWindow* window = nullptr;
  GtkWidget* header = nullptr;
  GtkWidget* divider = nullptr;
  GtkWidget* search_container = nullptr;
  GtkSearchEntry* search_entry = nullptr;
  GtkPopover* switcher = nullptr;
  GtkSearchEntry* switcher_entry = nullptr;
  GtkListBox* switcher_list = nullptr;
  std::vector<Essay> essays;  // what the switcher lists, newest first
  GtkCssProvider* text_css = nullptr;
  GtkCssProvider* theme_css = nullptr;
  GKeyFile* positions = nullptr;
  std::string document_path;  // empty for a new essay that has no text yet
  std::string essays_dir;
  std::string essay_started;  // "2026-10-31 2214" for the new essay's file name
  bool essay_named_by_time = false;  // renamed from its first line once that line is finished
  std::string positions_path;
  std::string settings_path;
  guint pending_save = 0;
  guint notification_timeout = 0;
  guint cursor_guard_source = 0;
  int editor_width = 0;
  Settings settings;
  bool focus_mode = false;
  bool literata_available = false;
  bool loading_document = false;
  bool save_failed = false;
  std::string backups_root;
  gint64 last_backup_us = 0;
  gint64 known_mtime = -1;  // mtime after our last load or save

  ~Writer() {
    if (notification_timeout != 0) g_source_remove(notification_timeout);
    if (cursor_guard_source != 0) g_source_remove(cursor_guard_source);
    g_clear_object(&text_css);
    g_clear_object(&theme_css);
    if (positions != nullptr) g_key_file_unref(positions);
  }
};

char* buffer_text(GtkTextBuffer* buffer) {
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, false);
}

void update_word_count(Writer* writer) {
  if (writer->save_failed) return;  // keep the "Not saved" warning visible
  char* text = buffer_text(writer->buffer);
  const auto count = word_count(text);
  char* label = g_strdup_printf("%u %s", count, count == 1 ? "word" : "words");
  gtk_label_set_text(writer->status, writer->settings.show_word_count ? label : "");
  g_free(label);
  g_free(text);
}

void show_notification(Writer* writer, const char* message, guint duration_ms = 1600);
void remember_document(Writer* writer);

void set_save_failed(Writer* writer, bool failed) {
  if (writer->save_failed == failed) return;
  writer->save_failed = failed;
  gtk_window_set_title(writer->window, failed ? "• Write — not saved" : "Write");
  if (failed) {
    gtk_label_set_text(writer->status, "Not saved");
    gtk_widget_add_css_class(GTK_WIDGET(writer->status), "save-error");
  } else {
    gtk_widget_remove_css_class(GTK_WIDGET(writer->status), "save-error");
    update_word_count(writer);
  }
}

void take_backup(Writer* writer, const char* text, gsize length) {
  const gint64 now = g_get_real_time();
  std::string error;
  if (write_backup(writer->backups_root, writer->document_path, text, length, now, kBackupsKept, &error)) {
    writer->last_backup_us = now;
  } else {
    g_warning("Write could not create a backup: %s", error.c_str());
  }
}

// Snapshots the disk version when another program changed the document since
// Write last loaded or saved it, and tells the writer how to get it back.
bool check_changed_on_disk(Writer* writer, bool about_to_save) {
  const gint64 mtime = file_mtime(writer->document_path);
  if (writer->known_mtime < 0 || mtime < 0 || mtime == writer->known_mtime) return false;
  char* disk_text = nullptr;
  gsize disk_length = 0;
  if (g_file_get_contents(writer->document_path.c_str(), &disk_text, &disk_length, nullptr)) {
    take_backup(writer, disk_text, disk_length);
    g_free(disk_text);
  }
  writer->known_mtime = mtime;
  const std::string folder = home_relative(backup_dir(writer->backups_root, writer->document_path), g_get_home_dir());
  char* message = about_to_save
      ? g_strdup_printf("File changed on disk. That version is backed up in\n%s", folder.c_str())
      : g_strdup_printf("File changed on disk. Ctrl+R loads it, backing up your text first.\nBackups: %s", folder.c_str());
  show_notification(writer, message, 12000);
  g_free(message);
  return true;
}

void save_cursor_position(Writer* writer) {
  if (writer->positions == nullptr || writer->positions_path.empty() || writer->document_path.empty()) return;
  GtkTextIter cursor;
  gtk_text_buffer_get_iter_at_mark(
      writer->buffer, &cursor, gtk_text_buffer_get_insert(writer->buffer));
  positions_store(writer->positions, writer->positions_path, writer->document_path,
                  gtk_text_iter_get_offset(&cursor));
}

void restore_cursor_position(Writer* writer) {
  if (writer->positions == nullptr) return;

  int offset = positions_lookup(writer->positions, writer->document_path);
  offset = std::clamp(offset, 0, gtk_text_buffer_get_char_count(writer->buffer));
  GtkTextIter cursor;
  gtk_text_buffer_get_iter_at_offset(writer->buffer, &cursor, offset);
  gtk_text_buffer_place_cursor(writer->buffer, &cursor);
}

bool load_document(Writer* writer, const char* path) {
  char* text = nullptr;
  gsize length = 0;
  GError* error = nullptr;
  if (!g_file_get_contents(path, &text, &length, &error)) {
    g_warning("Write could not open the document: %s", error != nullptr ? error->message : "unknown error");
    g_clear_error(&error);
    return false;
  }

  writer->loading_document = true;
  writer->document_path = path;
  writer->essay_named_by_time = false;
  gtk_text_buffer_set_text(writer->buffer, text, static_cast<gint>(length));
  restore_cursor_position(writer);
  writer->loading_document = false;
  writer->known_mtime = file_mtime(writer->document_path);
  writer->last_backup_us = 0;
  take_backup(writer, text, length);
  g_free(text);
  update_word_count(writer);
  remember_document(writer);
  return true;
}

// A new essay gets its file on the first save that has text. Until its first
// line is finished it is named by date and time, then renamed once from that line.
bool name_essay(Writer* writer, const char* text) {
  const std::string title = essay_title(text);
  const bool finished = essay_title_finished(text);
  if (writer->document_path.empty()) {
    if (title.empty() || writer->essay_started.size() < 12) return false;  // no text yet, so no empty files
    const std::string date = writer->essay_started.substr(0, 10);
    const std::string time = writer->essay_started.substr(11);
    writer->document_path = essay_unused_path(writer->essays_dir, essay_file_name(date, time, finished ? title : ""));
    writer->essay_named_by_time = !finished;
  } else if (writer->essay_named_by_time && finished) {
    writer->essay_named_by_time = false;
    char* directory = g_path_get_dirname(writer->document_path.c_str());
    const std::string renamed = essay_unused_path(
        directory, essay_file_name(writer->essay_started.substr(0, 10), "", title));
    g_free(directory);
    if (g_rename(writer->document_path.c_str(), renamed.c_str()) == 0) {
      writer->document_path = renamed;
    } else {
      g_warning("Write could not rename the essay to %s", renamed.c_str());
    }
  }
  return true;
}

void save(Writer* writer) {
  char* text = buffer_text(writer->buffer);
  const gsize length = strlen(text);
  if ((writer->document_path.empty() || writer->essay_named_by_time) && !name_essay(writer, text)) {
    g_free(text);
    return;
  }
  char* directory = g_path_get_dirname(writer->document_path.c_str());
  std::string error;

  if (g_mkdir_with_parents(directory, 0700) != 0) {
    error = "could not create the save folder";
  } else {
    check_changed_on_disk(writer, true);
    if (backup_due(writer->last_backup_us, g_get_real_time())) take_backup(writer, text, length);
    if (atomic_write(writer->document_path, text, length, &error)) {
      writer->known_mtime = file_mtime(writer->document_path);
      save_cursor_position(writer);
      if (writer->settings.last_document != writer->document_path) remember_document(writer);
      error.clear();
    } else if (error.empty()) {
      error = "unknown error";
    }
  }

  if (!error.empty()) g_warning("Write could not save the document: %s", error.c_str());
  set_save_failed(writer, !error.empty());
  g_free(directory);
  g_free(text);
}

gboolean save_when_idle(gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  writer->pending_save = 0;
  save(writer);
  return G_SOURCE_REMOVE;
}

void schedule_save(Writer* writer) {
  if (writer->pending_save != 0) {
    g_source_remove(writer->pending_save);
  }

  writer->pending_save = g_timeout_add(kSaveDelayMs, save_when_idle, writer);
}

void schedule_cursor_guard(Writer* writer);

void on_buffer_changed(GtkTextBuffer*, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  update_word_count(writer);
  schedule_cursor_guard(writer);
  if (!writer->loading_document) schedule_save(writer);
}

void center_writing_column(GObject* object, GParamSpec*, gpointer) {
  auto* editor = GTK_TEXT_VIEW(object);
  const int side_margin = std::max(112, (gtk_widget_get_width(GTK_WIDGET(editor)) - kReadingWidth) / 2);
  gtk_text_view_set_left_margin(editor, side_margin);
  gtk_text_view_set_right_margin(editor, side_margin);
}

gboolean keep_cursor_away_from_edges(gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  writer->cursor_guard_source = 0;
  GtkTextIter cursor;
  gtk_text_buffer_get_iter_at_mark(
      writer->buffer, &cursor, gtk_text_buffer_get_insert(writer->buffer));
  GdkRectangle cursor_rect;
  GdkRectangle visible_rect;
  gtk_text_view_get_iter_location(writer->editor, &cursor, &cursor_rect);
  gtk_text_view_get_visible_rect(writer->editor, &visible_rect);

  // Keep a calm lower margin. Unlike a typewriter mode, this moves only when
  // the line reaches the lower edge zone, then returns it to a natural height.
  const int lower_edge = visible_rect.y + visible_rect.height * 72 / 100;
  if (cursor_rect.y + cursor_rect.height > lower_edge) {
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(writer->scroller);
    const double target = cursor_rect.y + cursor_rect.height / 2.0 - visible_rect.height * 0.42;
    const double minimum = gtk_adjustment_get_lower(adjustment);
    const double maximum = std::max(minimum, gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment));
    gtk_adjustment_set_value(adjustment, std::clamp(target, minimum, maximum));
  }
  return G_SOURCE_REMOVE;
}

void schedule_cursor_guard(Writer* writer) {
  // Do not reset this timer on every key repeat. That would starve scrolling
  // forever when the writer holds Enter or types continuously.
  if (writer->cursor_guard_source == 0) {
    writer->cursor_guard_source = g_timeout_add(8, keep_cursor_away_from_edges, writer);
  }
}

void on_cursor_moved(GtkTextBuffer* buffer, GtkTextIter*, GtkTextMark* mark, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  if (mark != gtk_text_buffer_get_insert(buffer)) return;
  schedule_cursor_guard(writer);
}

gboolean center_writing_column_after_layout(gpointer user_data) {
  center_writing_column(G_OBJECT(user_data), nullptr, nullptr);
  return G_SOURCE_REMOVE;
}

gboolean maintain_writing_column(GtkWidget*, GdkFrameClock*, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  const int width = gtk_widget_get_width(GTK_WIDGET(writer->editor));
  if (width > 0 && width != writer->editor_width) {
    writer->editor_width = width;
    center_writing_column(G_OBJECT(writer->editor), nullptr, nullptr);
  }
  return G_SOURCE_CONTINUE;
}

void apply_font_size(Writer* writer) {
  char* css = g_strdup_printf(
      "textview, textview text { font-family: Literata, C059, serif; font-size: %dpx; line-height: 1.5; }",
      writer->settings.font_size);
  gtk_css_provider_load_from_string(writer->text_css, css);
  g_free(css);
}

struct Palette {
  const char* paper;
  const char* ink;
  const char* status;
  const char* notification;
  const char* error;
  const char* rule;
  const char* field;
  const char* field_border;
  const char* focus;
  const char* caret;
  const char* selection;
  const char* slider;
};

// Contrast of ink on paper (WCAG): Light 12.5:1, Dark 9.6:1, Night 6.8:1.
// The darker papers trade a little contrast for less glare and halation.
constexpr Palette kLight = {"#f7f3ea", "#312c25", "#91897c", "#857a6b", "#b4442f", "#ddd6ca", "#f0ebe1",
                            "#d7cfc2", "#9b8971", "#5a4a36", "rgba(155, 137, 113, 0.28)", "rgba(49, 44, 37, 0.22)"};
constexpr Palette kDark = {"#1c1a17", "#c9bfaf", "#958a7a", "#958a7a", "#d9785e", "#2f2b26", "#25221e",
                           "#3d3830", "#a08a6c", "#d8b98a", "rgba(160, 138, 108, 0.34)", "rgba(201, 191, 175, 0.20)"};
constexpr Palette kNight = {"#14110d", "#b39770", "#927a5a", "#927a5a", "#c4704f", "#251f19", "#1c1813",
                            "#332b22", "#8a6f4d", "#c9a26e", "rgba(150, 115, 70, 0.34)", "rgba(179, 151, 112, 0.16)"};

void apply_theme(Writer* writer) {
  const Palette& p = writer->settings.paper == Paper::Night ? kNight
                   : writer->settings.paper == Paper::Dark  ? kDark
                                                            : kLight;
  char* css = g_strdup_printf(R"(
      window.write-window, .header, textview, textview text { background: %s; color: %s; }
      textview text { caret-color: %s; }
      textview text selection { background-color: %s; color: %s; }
      .status { color: %s; }
      .status.save-error { color: %s; font-weight: bold; }
      .notification { color: %s; }
      separator { background: %s; }
      entry.find-entry { background: %s; color: %s; border-color: %s; caret-color: %s; }
      entry.find-entry:focus { border-color: %s; }
      entry.find-entry image { color: %s; }
      scrollbar, scrollbar trough { background: transparent; border: none; }
      scrollbar slider { background-color: %s; border: none; min-width: 4px; }
      scrollbar slider:hover, scrollbar slider:active { background-color: %s; }
      popover.essays > contents { background: %s; color: %s; border: 1px solid %s; }
      popover.essays list, popover.essays row { background: transparent; color: %s; }
      popover.essays row:selected { background-color: %s; }
      popover.essays .essay-date { color: %s; }
    )",
      p.paper, p.ink, p.caret, p.selection, p.ink, p.status, p.error, p.notification, p.rule,
      p.field, p.ink, p.field_border, p.caret, p.focus, p.status, p.slider, p.focus,
      p.paper, p.ink, p.field_border, p.ink, p.selection, p.status);
  gtk_css_provider_load_from_string(writer->theme_css, css);
  g_free(css);
  // Lets GTK's own pieces, like the file dialog, follow the paper too.
  g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme",
               writer->settings.paper != Paper::Light, nullptr);
}

// The freedesktop portal's colour scheme: 1 means the desktop prefers dark.
bool portal_prefers_dark() {
  GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  if (bus == nullptr) return false;
  GVariant* reply = g_dbus_connection_call_sync(
      bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings",
      "ReadOne", g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"), G_VARIANT_TYPE("(v)"),
      G_DBUS_CALL_FLAGS_NONE, 300, nullptr, nullptr);
  g_object_unref(bus);
  if (reply == nullptr) return false;
  GVariant* value = nullptr;
  g_variant_get(reply, "(v)", &value);
  const bool dark = g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32) && g_variant_get_uint32(value) == 1;
  g_variant_unref(value);
  g_variant_unref(reply);
  return dark;
}

bool system_prefers_dark() {
  gboolean prefer_dark = false;
  g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &prefer_dark, nullptr);
  return prefer_dark || portal_prefers_dark();
}

void save_settings(Writer* writer) {
  if (!settings_save(writer->settings_path, writer->settings)) {
    g_warning("Write could not save its preferences to %s", writer->settings_path.c_str());
  }
}

void remember_document(Writer* writer) {
  if (writer->document_path.empty()) return;
  writer->settings.last_document = writer->document_path;
  save_settings(writer);
}

void change_font_size(Writer* writer, int size, const char* message) {
  writer->settings.font_size = std::clamp(size, kSmallestFontSize, kLargestFontSize);
  apply_font_size(writer);
  save_settings(writer);
  char* text = message != nullptr ? g_strdup(message) : g_strdup_printf("Text size %d", writer->settings.font_size);
  show_notification(writer, text);
  g_free(text);
}

gboolean hide_notification(gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  writer->notification_timeout = 0;
  gtk_widget_set_visible(GTK_WIDGET(writer->notification), false);
  gtk_label_set_xalign(writer->notification, 1.0);
  gtk_label_set_justify(writer->notification, GTK_JUSTIFY_RIGHT);
  return G_SOURCE_REMOVE;
}

void show_notification(Writer* writer, const char* message, guint duration_ms) {
  if (writer->notification_timeout != 0) {
    g_source_remove(writer->notification_timeout);
  }
  gtk_label_set_text(writer->notification, message);
  gtk_widget_set_visible(GTK_WIDGET(writer->notification), true);
  writer->notification_timeout = g_timeout_add(duration_ms, hide_notification, writer);
}

void on_save_as_finished(GObject* source, GAsyncResult* result, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  GError* error = nullptr;
  GFile* selected_file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);

  if (selected_file != nullptr) {
    char* path = g_file_get_path(selected_file);
    if (path != nullptr) {
      writer->document_path = path;
      writer->essay_named_by_time = false;
      writer->known_mtime = -1;
      writer->last_backup_us = 0;
      g_free(path);
      save(writer);
    } else {
      g_warning("Write can save only to a local folder");
    }
    g_object_unref(selected_file);
  } else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_warning("Write could not choose a save location: %s", error != nullptr ? error->message : "unknown error");
  }

  g_clear_error(&error);
}

void choose_save_location(GtkButton*, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  GtkFileDialog* dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Choose where to save your writing");
  gtk_file_dialog_set_initial_name(dialog, "writing.txt");
  gtk_file_dialog_save(dialog, writer->window, nullptr, on_save_as_finished, writer);
  g_object_unref(dialog);
}

void toggle_focus_mode(Writer* writer) {
  writer->focus_mode = !writer->focus_mode;
  gtk_widget_set_visible(writer->header, !writer->focus_mode);
  gtk_widget_set_visible(writer->divider, !writer->focus_mode);
  if (writer->focus_mode) {
    gtk_window_fullscreen(writer->window);
  } else {
    gtk_window_unfullscreen(writer->window);
    show_notification(writer, "Focus mode off");
  }
}

bool select_search_match(Writer* writer, bool forward, bool start_from_document) {
  const char* query = gtk_editable_get_text(GTK_EDITABLE(writer->search_entry));
  if (*query == '\0') return false;

  GtkTextIter document_start;
  GtkTextIter document_end;
  GtkTextIter search_start;
  GtkTextIter match_start;
  GtkTextIter match_end;
  gtk_text_buffer_get_bounds(writer->buffer, &document_start, &document_end);

  if (start_from_document) {
    search_start = forward ? document_start : document_end;
  } else if (!gtk_text_buffer_get_selection_bounds(writer->buffer, &match_start, &match_end)) {
    gtk_text_buffer_get_iter_at_mark(
        writer->buffer, &search_start, gtk_text_buffer_get_insert(writer->buffer));
  } else {
    search_start = forward ? match_end : match_start;
  }

  constexpr auto flags = static_cast<GtkTextSearchFlags>(
      GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE);
  const bool found = forward
      ? gtk_text_iter_forward_search(&search_start, query, flags, &match_start, &match_end, nullptr)
      : gtk_text_iter_backward_search(&search_start, query, flags, &match_start, &match_end, nullptr);

  if (!found) {
    search_start = forward ? document_start : document_end;
    const bool wrapped = forward
        ? gtk_text_iter_forward_search(&search_start, query, flags, &match_start, &match_end, nullptr)
        : gtk_text_iter_backward_search(&search_start, query, flags, &match_start, &match_end, nullptr);
    if (!wrapped) return false;
  }

  gtk_text_buffer_select_range(writer->buffer, &match_start, &match_end);
  gtk_text_view_scroll_to_iter(writer->editor, &match_start, 0.15, false, 0.0, 0.0);
  return true;
}

void on_search_changed(GtkEditable*, gpointer user_data) {
  select_search_match(static_cast<Writer*>(user_data), true, true);
}

void on_search_activated(GtkEntry*, gpointer user_data) {
  select_search_match(static_cast<Writer*>(user_data), true, false);
}

gboolean on_search_shortcut(
    GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  if (keyval == GDK_KEY_Escape) {
    gtk_widget_set_visible(writer->search_container, false);
    gtk_widget_set_visible(GTK_WIDGET(writer->status), true);
    if (writer->focus_mode) gtk_widget_set_visible(writer->header, false);
    gtk_widget_grab_focus(GTK_WIDGET(writer->editor));
    return true;
  }
  if (keyval == GDK_KEY_Return && (state & GDK_SHIFT_MASK) != 0) {
    select_search_match(writer, false, false);
    return true;
  }
  return false;
}

void save_now(Writer* writer) {
  if (writer->pending_save != 0) {
    g_source_remove(writer->pending_save);
    writer->pending_save = 0;
  }
  save(writer);
}

void insert_paragraph_break(Writer* writer) {
  GtkTextIter start;
  GtkTextIter end;
  if (gtk_text_buffer_get_selection_bounds(writer->buffer, &start, &end)) {
    gtk_text_buffer_delete(writer->buffer, &start, &end);
  }
  gtk_text_buffer_insert_at_cursor(writer->buffer, "\n\n", -1);
  schedule_cursor_guard(writer);
}

void switch_document(Writer* writer, const char* path) {
  if (writer->pending_save != 0) {
    save_now(writer);
  } else {
    save_cursor_position(writer);
  }
  if (g_file_test(path, G_FILE_TEST_EXISTS)) {
    load_document(writer, path);
    return;
  }
  writer->loading_document = true;
  writer->document_path = path;
  writer->essay_named_by_time = false;
  gtk_text_buffer_set_text(writer->buffer, "", 0);
  writer->loading_document = false;
  writer->known_mtime = -1;
  writer->last_backup_us = 0;
  update_word_count(writer);
}

void new_essay(Writer* writer) {
  if (writer->document_path.empty() && gtk_text_buffer_get_char_count(writer->buffer) == 0) return;
  switch_document(writer, "");
  GDateTime* now = g_date_time_new_now_local();
  char* started = g_date_time_format(now, "%Y-%m-%d %H%M");
  writer->essay_started = started;
  g_free(started);
  g_date_time_unref(now);
  show_notification(writer, "New essay");
}

void fill_switcher(Writer* writer) {
  const char* query = gtk_editable_get_text(GTK_EDITABLE(writer->switcher_entry));
  while (GtkWidget* old = gtk_widget_get_first_child(GTK_WIDGET(writer->switcher_list))) {
    gtk_list_box_remove(writer->switcher_list, old);
  }
  for (const Essay& essay : writer->essays) {
    if (!essay_matches(essay, query)) continue;
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget* title = gtk_label_new(essay.title.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_set_hexpand(title, true);
    GDateTime* modified = g_date_time_new_from_unix_local(essay.modified_us / G_USEC_PER_SEC);
    char* date = g_date_time_format(modified, "%e %b");
    GtkWidget* when = gtk_label_new(g_strstrip(date));
    g_free(date);
    g_date_time_unref(modified);
    gtk_widget_add_css_class(when, "essay-date");
    gtk_box_append(GTK_BOX(row), title);
    gtk_box_append(GTK_BOX(row), when);
    gtk_list_box_append(writer->switcher_list, row);
    g_object_set_data_full(G_OBJECT(gtk_widget_get_parent(row)), "essay-path", g_strdup(essay.path.c_str()), g_free);
  }
  gtk_list_box_select_row(writer->switcher_list, gtk_list_box_get_row_at_index(writer->switcher_list, 0));
}

void show_switcher(Writer* writer) {
  writer->essays = essays_list(writer->essays_dir);
  gtk_editable_set_text(GTK_EDITABLE(writer->switcher_entry), "");
  fill_switcher(writer);
  GtkWidget* parent = gtk_widget_get_parent(GTK_WIDGET(writer->switcher));
  const GdkRectangle top = {gtk_widget_get_width(parent) / 2, 64, 1, 1};
  gtk_popover_set_pointing_to(writer->switcher, &top);
  gtk_popover_popup(writer->switcher);
  gtk_widget_grab_focus(GTK_WIDGET(writer->switcher_entry));
}

void open_selected_essay(Writer* writer) {
  GtkListBoxRow* row = gtk_list_box_get_selected_row(writer->switcher_list);
  if (row == nullptr) return;
  const std::string path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "essay-path"));
  gtk_popover_popdown(writer->switcher);
  if (path != writer->document_path) switch_document(writer, path.c_str());
}

void on_switcher_changed(GtkEditable*, gpointer user_data) {
  fill_switcher(static_cast<Writer*>(user_data));
}

void on_switcher_activated(GtkEntry*, gpointer user_data) {
  open_selected_essay(static_cast<Writer*>(user_data));
}

void on_switcher_row_activated(GtkListBox*, GtkListBoxRow* row, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  gtk_list_box_select_row(writer->switcher_list, row);
  open_selected_essay(writer);
}

void on_switcher_stop(GtkSearchEntry*, gpointer user_data) {
  gtk_popover_popdown(static_cast<Writer*>(user_data)->switcher);
}

void on_switcher_closed(GtkPopover*, gpointer user_data) {
  gtk_widget_grab_focus(GTK_WIDGET(static_cast<Writer*>(user_data)->editor));
}

// Up and Down move through the list while the filter keeps the focus.
gboolean on_switcher_key(GtkEventControllerKey*, guint keyval, guint, GdkModifierType, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  if (keyval != GDK_KEY_Down && keyval != GDK_KEY_Up) return false;
  GtkListBoxRow* row = gtk_list_box_get_selected_row(writer->switcher_list);
  const int index = row != nullptr ? gtk_list_box_row_get_index(row) + (keyval == GDK_KEY_Down ? 1 : -1) : 0;
  if (GtkListBoxRow* next = gtk_list_box_get_row_at_index(writer->switcher_list, std::max(0, index))) {
    gtk_list_box_select_row(writer->switcher_list, next);
    GtkAdjustment* scroll = gtk_list_box_get_adjustment(writer->switcher_list);
    graphene_rect_t bounds;
    if (scroll != nullptr && gtk_widget_compute_bounds(GTK_WIDGET(next), GTK_WIDGET(writer->switcher_list), &bounds)) {
      gtk_adjustment_clamp_page(scroll, bounds.origin.y, bounds.origin.y + bounds.size.height);
    }
  }
  return true;
}

void reload_from_disk(Writer* writer) {
  if (!g_file_test(writer->document_path.c_str(), G_FILE_TEST_EXISTS)) return;
  if (writer->pending_save != 0) {
    g_source_remove(writer->pending_save);
    writer->pending_save = 0;
  }
  char* text = buffer_text(writer->buffer);
  take_backup(writer, text, strlen(text));
  g_free(text);
  const std::string path = writer->document_path;
  if (load_document(writer, path.c_str())) show_notification(writer, "Reloaded from disk");
}

void on_open_finished(GObject* source, GAsyncResult* result, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  GError* error = nullptr;
  GFile* selected_file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);

  if (selected_file != nullptr) {
    char* path = g_file_get_path(selected_file);
    if (path != nullptr) {
      switch_document(writer, path);
      g_free(path);
    } else {
      g_warning("Write can open only local files");
    }
    g_object_unref(selected_file);
  } else if (error != nullptr && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    g_warning("Write could not open the document: %s", error->message);
  }

  g_clear_error(&error);
}

void open_document(GtkButton*, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  GtkFileDialog* dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Open a text file");
  gtk_file_dialog_open(dialog, writer->window, nullptr, on_open_finished, writer);
  g_object_unref(dialog);
}

gboolean on_shortcut(GtkEventControllerKey*, guint keyval, guint, GdkModifierType state, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  const auto modifiers = state & gtk_accelerator_get_default_mod_mask();
  if (keyval == GDK_KEY_F11) {
    toggle_focus_mode(writer);
    return true;
  }
  if (keyval == GDK_KEY_Return && (modifiers & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK)) == 0) {
    insert_paragraph_break(writer);
    return true;
  }
  if ((modifiers & GDK_CONTROL_MASK) == 0) return false;

  if (keyval == GDK_KEY_f) {
    if (writer->focus_mode) gtk_widget_set_visible(writer->header, true);
    gtk_widget_set_visible(writer->search_container, true);
    gtk_widget_set_visible(GTK_WIDGET(writer->status), false);
    gtk_widget_grab_focus(GTK_WIDGET(writer->search_entry));
    return true;
  }

  if (keyval == GDK_KEY_n || keyval == GDK_KEY_N) {
    new_essay(writer);
    return true;
  }
  if (keyval == GDK_KEY_e || keyval == GDK_KEY_E) {
    show_switcher(writer);
    return true;
  }

  if (keyval == GDK_KEY_o || keyval == GDK_KEY_O) {
    open_document(nullptr, writer);
    return true;
  }

  if ((keyval == GDK_KEY_s || keyval == GDK_KEY_S) && (modifiers & GDK_SHIFT_MASK) != 0) {
    choose_save_location(nullptr, writer);
    return true;
  }
  if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
    save_now(writer);
    return true;
  }
  if ((keyval == GDK_KEY_d || keyval == GDK_KEY_D) && (modifiers & GDK_SHIFT_MASK) != 0) {
    writer->settings.paper = next_paper(writer->settings.paper);
    writer->settings.paper_saved = true;
    apply_theme(writer);
    save_settings(writer);
    show_notification(writer, paper_name(writer->settings.paper));
    return true;
  }
  if (keyval == GDK_KEY_r || keyval == GDK_KEY_R) {
    reload_from_disk(writer);
    return true;
  }
  if ((keyval == GDK_KEY_w || keyval == GDK_KEY_W) && (modifiers & GDK_SHIFT_MASK) != 0) {
    writer->settings.show_word_count = !writer->settings.show_word_count;
    update_word_count(writer);
    save_settings(writer);
    show_notification(writer, writer->settings.show_word_count ? "Word count on" : "Word count off");
    return true;
  }
  if ((keyval == GDK_KEY_i || keyval == GDK_KEY_I) && (modifiers & GDK_SHIFT_MASK) != 0) {
    show_notification(writer, writer->literata_available ? "Literata" : "Literata unavailable — fallback in use");
    return true;
  }
  if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal) {
    change_font_size(writer, writer->settings.font_size + 1, nullptr);
    return true;
  }
  if (keyval == GDK_KEY_minus) {
    change_font_size(writer, writer->settings.font_size - 1, nullptr);
    return true;
  }
  if (keyval == GDK_KEY_0) {
    change_font_size(writer, kDefaultFontSize, "Text size reset");
    return true;
  }

  return false;
}

void on_window_active(GObject* window, GParamSpec*, gpointer user_data) {
  if (gtk_window_is_active(GTK_WINDOW(window))) check_changed_on_disk(static_cast<Writer*>(user_data), false);
}

gboolean on_window_close(GtkWindow*, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  if (writer->pending_save != 0) {
    g_source_remove(writer->pending_save);
    writer->pending_save = 0;
    save(writer);
  } else {
    save_cursor_position(writer);
  }
  return false;
}

}  // namespace

void on_activate(GtkApplication* app, gpointer) {
  if (GtkWindow* existing = gtk_application_get_active_window(app)) {
    gtk_window_present(existing);
    return;
  }
  auto* writer = new Writer();
  writer->literata_available = register_bundled_literata();
  char* path = g_build_filename(g_get_user_data_dir(), "write", "document.txt", nullptr);
  writer->document_path = path;
  g_free(path);
  char* positions_path = g_build_filename(g_get_user_data_dir(), "write", "positions.ini", nullptr);
  writer->positions_path = positions_path;
  g_free(positions_path);
  writer->positions = positions_load(writer->positions_path);
  char* backups_root = g_build_filename(g_get_user_data_dir(), "write", "backups", nullptr);
  writer->backups_root = backups_root;
  g_free(backups_root);
  char* settings_path = g_build_filename(g_get_user_config_dir(), "write", "settings.ini", nullptr);
  writer->settings_path = settings_path;
  g_free(settings_path);
  writer->settings = settings_load(writer->settings_path);
  if (!writer->settings.paper_saved && system_prefers_dark()) writer->settings.paper = Paper::Dark;
  if (writer->settings.essays_dir.empty()) {
    writer->settings.essays_dir = default_essays_dir(g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS), g_get_home_dir());
  }
  writer->essays_dir = expand_home(writer->settings.essays_dir, g_get_home_dir());
  if (!writer->settings.last_document.empty() && g_file_test(writer->settings.last_document.c_str(), G_FILE_TEST_EXISTS)) {
    writer->document_path = writer->settings.last_document;
  }

  GtkWidget* window = gtk_application_window_new(app);
  writer->window = GTK_WINDOW(window);
  gtk_window_set_title(GTK_WINDOW(window), "Write");
  gtk_window_set_default_icon_name("com.silvags.Write");
  gtk_window_set_default_size(GTK_WINDOW(window), 920, 720);
  gtk_widget_add_css_class(window, "write-window");

  GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(window), root);

  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  writer->header = header;
  gtk_widget_add_css_class(header, "header");
  gtk_widget_set_margin_start(header, 32);
  gtk_widget_set_margin_end(header, 32);
  gtk_widget_set_margin_top(header, 22);
  gtk_widget_set_margin_bottom(header, 18);
  gtk_box_append(GTK_BOX(root), header);

  GtkWidget* wordmark = gtk_label_new("write");
  gtk_widget_add_css_class(wordmark, "wordmark");
  gtk_widget_set_halign(wordmark, GTK_ALIGN_START);
  gtk_widget_set_hexpand(wordmark, true);
  gtk_box_append(GTK_BOX(header), wordmark);

  writer->status = GTK_LABEL(gtk_label_new("0 words"));
  gtk_widget_add_css_class(GTK_WIDGET(writer->status), "status");

  writer->notification = GTK_LABEL(gtk_label_new(nullptr));
  gtk_widget_add_css_class(GTK_WIDGET(writer->notification), "notification");
  gtk_widget_set_margin_end(GTK_WIDGET(writer->notification), 16);
  gtk_widget_set_visible(GTK_WIDGET(writer->notification), false);
  gtk_box_append(GTK_BOX(header), GTK_WIDGET(writer->notification));
  gtk_box_append(GTK_BOX(header), GTK_WIDGET(writer->status));

  GtkWidget* search_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  writer->search_container = search_container;
  gtk_widget_add_css_class(search_container, "find-container");
  gtk_widget_set_visible(search_container, false);
  GtkWidget* search_entry = gtk_search_entry_new();
  writer->search_entry = GTK_SEARCH_ENTRY(search_entry);
  gtk_search_entry_set_placeholder_text(writer->search_entry, "Find");
  gtk_widget_add_css_class(search_entry, "find-entry");
  gtk_widget_set_size_request(search_entry, 210, -1);
  gtk_box_append(GTK_BOX(search_container), search_entry);
  gtk_box_append(GTK_BOX(header), search_container);

  GtkWidget* divider = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  writer->divider = divider;
  gtk_box_append(GTK_BOX(root), divider);

  GtkWidget* scroller = gtk_scrolled_window_new();
  writer->scroller = GTK_SCROLLED_WINDOW(scroller);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroller, true);
  gtk_box_append(GTK_BOX(root), scroller);

  GtkWidget* editor = gtk_text_view_new();
  gtk_widget_add_css_class(editor, "editor");
  gtk_widget_set_vexpand(editor, true);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(editor), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_justification(GTK_TEXT_VIEW(editor), GTK_JUSTIFY_LEFT);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(editor), 96);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(editor), 96);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(editor), 72);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(editor), kBottomPaperMargin);
  gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(editor), 0);
  gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(editor), 0);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), editor);

  writer->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor));
  writer->editor = GTK_TEXT_VIEW(editor);
  if (g_file_test(writer->document_path.c_str(), G_FILE_TEST_EXISTS)) {
    load_document(writer, writer->document_path.c_str());
  }
  update_word_count(writer);

  GtkWidget* switcher = gtk_popover_new();
  writer->switcher = GTK_POPOVER(switcher);
  gtk_widget_add_css_class(switcher, "essays");
  gtk_popover_set_has_arrow(writer->switcher, false);
  gtk_popover_set_position(writer->switcher, GTK_POS_BOTTOM);
  gtk_widget_set_parent(switcher, root);
  GtkWidget* switcher_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  GtkWidget* switcher_entry = gtk_search_entry_new();
  writer->switcher_entry = GTK_SEARCH_ENTRY(switcher_entry);
  gtk_search_entry_set_placeholder_text(writer->switcher_entry, "Essays");
  gtk_widget_add_css_class(switcher_entry, "find-entry");
  GtkWidget* switcher_scroller = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(switcher_scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(switcher_scroller), true);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(switcher_scroller), 320);
  gtk_widget_set_size_request(switcher_scroller, 420, -1);
  GtkWidget* switcher_list = gtk_list_box_new();
  writer->switcher_list = GTK_LIST_BOX(switcher_list);
  gtk_list_box_set_selection_mode(writer->switcher_list, GTK_SELECTION_BROWSE);
  gtk_list_box_set_adjustment(writer->switcher_list,
                              gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(switcher_scroller)));
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(switcher_scroller), switcher_list);
  gtk_box_append(GTK_BOX(switcher_box), switcher_entry);
  gtk_box_append(GTK_BOX(switcher_box), switcher_scroller);
  gtk_popover_set_child(writer->switcher, switcher_box);
  g_signal_connect(switcher_entry, "changed", G_CALLBACK(on_switcher_changed), writer);
  g_signal_connect(switcher_entry, "activate", G_CALLBACK(on_switcher_activated), writer);
  g_signal_connect(switcher_entry, "stop-search", G_CALLBACK(on_switcher_stop), writer);
  g_signal_connect(switcher_list, "row-activated", G_CALLBACK(on_switcher_row_activated), writer);
  g_signal_connect(switcher, "closed", G_CALLBACK(on_switcher_closed), writer);
  GtkEventController* switcher_keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(switcher_keys, GTK_PHASE_CAPTURE);
  g_signal_connect(switcher_keys, "key-pressed", G_CALLBACK(on_switcher_key), writer);
  gtk_widget_add_controller(switcher_entry, switcher_keys);

  g_signal_connect(writer->buffer, "changed", G_CALLBACK(on_buffer_changed), writer);
  g_signal_connect(writer->buffer, "mark-set", G_CALLBACK(on_cursor_moved), writer);
  g_signal_connect(search_entry, "changed", G_CALLBACK(on_search_changed), writer);
  g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activated), writer);
  g_signal_connect(editor, "notify::width", G_CALLBACK(center_writing_column), nullptr);
  gtk_widget_add_tick_callback(editor, maintain_writing_column, writer, nullptr);
  g_signal_connect(window, "notify::is-active", G_CALLBACK(on_window_active), writer);
  g_signal_connect(window, "close-request", G_CALLBACK(on_window_close), writer);
  g_object_set_data_full(G_OBJECT(window), "writer-state", writer, [](gpointer data) {
    delete static_cast<Writer*>(data);
  });

  GtkCssProvider* css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(css, R"(
    .wordmark { font-family: Literata, C059, serif; font-size: 24px; font-style: italic; letter-spacing: -0.8px; }
    .status, .notification { font-size: 12px; }
    .find-container { margin-right: 12px; }
    entry.find-entry { min-height: 28px; padding: 0 8px; border: 1px solid; border-radius: 5px; font-size: 13px; box-shadow: none; outline: none; }
    separator { min-height: 1px; }
    popover.essays > contents { padding: 10px; border-radius: 8px; box-shadow: none; }
    popover.essays row { padding: 7px 10px; border-radius: 5px; font-family: Literata, C059, serif; font-size: 15px; }
    popover.essays .essay-date { font-family: sans-serif; font-size: 12px; }
    textview:focus { outline: none; }
  )");
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(window), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);

  writer->theme_css = gtk_css_provider_new();
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(window), GTK_STYLE_PROVIDER(writer->theme_css), GTK_STYLE_PROVIDER_PRIORITY_USER);
  apply_theme(writer);

  writer->text_css = gtk_css_provider_new();
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(window), GTK_STYLE_PROVIDER(writer->text_css), GTK_STYLE_PROVIDER_PRIORITY_USER);
  apply_font_size(writer);

  GtkEventController* shortcuts = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(shortcuts, GTK_PHASE_CAPTURE);
  g_signal_connect(shortcuts, "key-pressed", G_CALLBACK(on_shortcut), writer);
  gtk_widget_add_controller(editor, shortcuts);

  GtkEventController* search_shortcuts = gtk_event_controller_key_new();
  g_signal_connect(search_shortcuts, "key-pressed", G_CALLBACK(on_search_shortcut), writer);
  gtk_widget_add_controller(search_entry, search_shortcuts);

  gtk_widget_grab_focus(editor);
  gtk_window_present(GTK_WINDOW(window));
  g_idle_add(center_writing_column_after_layout, editor);
}

void on_open(GApplication* app, GFile** files, int count, const char*, gpointer) {
  if (count < 1) return;
  on_activate(GTK_APPLICATION(app), nullptr);
  GtkWindow* window = gtk_application_get_active_window(GTK_APPLICATION(app));
  auto* writer = static_cast<Writer*>(g_object_get_data(G_OBJECT(window), "writer-state"));
  char* path = g_file_get_path(files[0]);
  if (path == nullptr) {
    g_warning("Write can open only local files");
    return;
  }
  switch_document(writer, path);
  g_free(path);
  gtk_window_present(window);
}
