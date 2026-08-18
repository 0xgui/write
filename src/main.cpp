#include <gtk/gtk.h>
#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <string>

namespace {

constexpr auto kAppId = "com.silvags.Write";
constexpr auto kSaveDelayMs = 350;
constexpr auto kReadingWidth = 560;
constexpr auto kBottomPaperMargin = 260;
constexpr auto kDefaultFontSize = 20;
constexpr auto kSmallestFontSize = 14;
constexpr auto kLargestFontSize = 32;
constexpr auto kLiterataFilename = "Literata-VariableFont_opsz,wght.ttf";

#ifndef WRITE_DATA_DIR
#define WRITE_DATA_DIR "/usr/local/share/write"
#endif

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
  GtkCssProvider* text_css = nullptr;
  GtkCssProvider* theme_css = nullptr;
  GKeyFile* positions = nullptr;
  std::string document_path;
  std::string positions_path;
  guint pending_save = 0;
  guint notification_timeout = 0;
  guint cursor_guard_source = 0;
  int editor_width = 0;
  int font_size = kDefaultFontSize;
  bool focus_mode = false;
  bool literata_available = false;
  bool dark_mode = false;
  bool loading_document = false;

  ~Writer() {
    if (notification_timeout != 0) g_source_remove(notification_timeout);
    if (cursor_guard_source != 0) g_source_remove(cursor_guard_source);
    g_clear_object(&text_css);
    g_clear_object(&theme_css);
    if (positions != nullptr) g_key_file_unref(positions);
  }
};

guint word_count(const char* text) {
  guint count = 0;
  bool in_word = false;

  for (const char* cursor = text; *cursor != '\0'; cursor = g_utf8_next_char(cursor)) {
    if (g_unichar_isspace(g_utf8_get_char(cursor))) {
      in_word = false;
    } else if (!in_word) {
      in_word = true;
      ++count;
    }
  }

  return count;
}

char* buffer_text(GtkTextBuffer* buffer) {
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  return gtk_text_buffer_get_text(buffer, &start, &end, false);
}

void update_word_count(Writer* writer) {
  char* text = buffer_text(writer->buffer);
  const auto count = word_count(text);
  char* label = g_strdup_printf("%u %s", count, count == 1 ? "word" : "words");
  gtk_label_set_text(writer->status, label);
  g_free(label);
  g_free(text);
}

std::string document_key(const std::string& path) {
  char* checksum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.c_str(), -1);
  std::string key = checksum;
  g_free(checksum);
  return key;
}

void save_cursor_position(Writer* writer) {
  if (writer->positions == nullptr || writer->positions_path.empty()) return;

  GtkTextIter cursor;
  gtk_text_buffer_get_iter_at_mark(
      writer->buffer, &cursor, gtk_text_buffer_get_insert(writer->buffer));
  const std::string key = document_key(writer->document_path);
  g_key_file_set_integer(writer->positions, "cursor-positions", key.c_str(), gtk_text_iter_get_offset(&cursor));

  char* directory = g_path_get_dirname(writer->positions_path.c_str());
  g_mkdir_with_parents(directory, 0700);
  g_free(directory);
  gsize length = 0;
  char* contents = g_key_file_to_data(writer->positions, &length, nullptr);
  g_file_set_contents(writer->positions_path.c_str(), contents, static_cast<gssize>(length), nullptr);
  g_free(contents);
}

void restore_cursor_position(Writer* writer) {
  if (writer->positions == nullptr) return;

  const std::string key = document_key(writer->document_path);
  GError* error = nullptr;
  int offset = g_key_file_get_integer(writer->positions, "cursor-positions", key.c_str(), &error);
  g_clear_error(&error);
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
  gtk_text_buffer_set_text(writer->buffer, text, static_cast<gint>(length));
  g_free(text);
  restore_cursor_position(writer);
  writer->loading_document = false;
  update_word_count(writer);
  return true;
}

void save(Writer* writer) {
  char* text = buffer_text(writer->buffer);
  char* directory = g_path_get_dirname(writer->document_path.c_str());
  GError* error = nullptr;

  if (g_mkdir_with_parents(directory, 0700) != 0) {
    g_warning("Write could not create the save folder");
  } else {
    // Keep exactly one recovery copy: the version that existed before this save.
    // It keeps writing safe without adding a document database or version UI.
    if (g_file_test(writer->document_path.c_str(), G_FILE_TEST_EXISTS)) {
      const std::string backup_path = writer->document_path + "~";
      GFile* source = g_file_new_for_path(writer->document_path.c_str());
      GFile* backup = g_file_new_for_path(backup_path.c_str());
      GError* backup_error = nullptr;
      if (!g_file_copy(source, backup, G_FILE_COPY_OVERWRITE, nullptr, nullptr, nullptr, &backup_error)) {
        g_warning("Write could not create a recovery copy: %s", backup_error->message);
        g_clear_error(&backup_error);
      }
      g_object_unref(source);
      g_object_unref(backup);
    }

    if (!g_file_set_contents(writer->document_path.c_str(), text, -1, &error)) {
      g_warning("Write could not save the document: %s", error != nullptr ? error->message : "unknown error");
      g_clear_error(&error);
    } else {
      save_cursor_position(writer);
    }
  }

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
      writer->font_size);
  gtk_css_provider_load_from_string(writer->text_css, css);
  g_free(css);
}

void apply_theme(Writer* writer) {
  const char* css = writer->dark_mode
      ? R"(
        window.write-window, .header, textview, textview text { background: #191714; color: #e9e1d4; }
        .status, .notification { color: #a69a89; }
        separator { background: #39342d; }
        entry.find-entry { background: #26221d; color: #e9e1d4; border-color: #4a4339; }
        entry.find-entry:focus { border-color: #aa9474; box-shadow: 0 0 0 2px rgba(170, 148, 116, 0.20); }
      )"
      : R"(
        window.write-window, .header, textview, textview text { background: #f7f3ea; color: #312c25; }
        .status { color: #91897c; }
        .notification { color: #857a6b; }
        separator { background: #ddd6ca; }
        entry.find-entry { background: #f0ebe1; color: #312c25; border-color: #d7cfc2; }
      )";
  gtk_css_provider_load_from_string(writer->theme_css, css);
}

gboolean hide_notification(gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  writer->notification_timeout = 0;
  gtk_widget_set_visible(GTK_WIDGET(writer->notification), false);
  return G_SOURCE_REMOVE;
}

void show_notification(Writer* writer, const char* message) {
  if (writer->notification_timeout != 0) {
    g_source_remove(writer->notification_timeout);
  }
  gtk_label_set_text(writer->notification, message);
  gtk_widget_set_visible(GTK_WIDGET(writer->notification), true);
  writer->notification_timeout = g_timeout_add(1600, hide_notification, writer);
}

void on_save_as_finished(GObject* source, GAsyncResult* result, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  GError* error = nullptr;
  GFile* selected_file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);

  if (selected_file != nullptr) {
    char* path = g_file_get_path(selected_file);
    if (path != nullptr) {
      writer->document_path = path;
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

void on_open_finished(GObject* source, GAsyncResult* result, gpointer user_data) {
  auto* writer = static_cast<Writer*>(user_data);
  GError* error = nullptr;
  GFile* selected_file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);

  if (selected_file != nullptr) {
    char* path = g_file_get_path(selected_file);
    if (path != nullptr) {
      if (writer->pending_save != 0) {
        save_now(writer);
      } else {
        save_cursor_position(writer);
      }
      load_document(writer, path);
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

  if (keyval == GDK_KEY_o || keyval == GDK_KEY_O) {
    open_document(nullptr, writer);
    return true;
  }

  if (keyval == GDK_KEY_s && (modifiers & GDK_SHIFT_MASK) != 0) {
    choose_save_location(nullptr, writer);
    return true;
  }
  if (keyval == GDK_KEY_s) {
    save_now(writer);
    return true;
  }
  if ((keyval == GDK_KEY_d || keyval == GDK_KEY_D) && (modifiers & GDK_SHIFT_MASK) != 0) {
    writer->dark_mode = !writer->dark_mode;
    apply_theme(writer);
    show_notification(writer, writer->dark_mode ? "Dark paper" : "Light paper");
    return true;
  }
  if ((keyval == GDK_KEY_i || keyval == GDK_KEY_I) && (modifiers & GDK_SHIFT_MASK) != 0) {
    show_notification(writer, writer->literata_available ? "Literata" : "Literata unavailable — fallback in use");
    return true;
  }
  if (keyval == GDK_KEY_plus || keyval == GDK_KEY_equal) {
    writer->font_size = std::min(kLargestFontSize, writer->font_size + 1);
    apply_font_size(writer);
    char* message = g_strdup_printf("Text size %d", writer->font_size);
    show_notification(writer, message);
    g_free(message);
    return true;
  }
  if (keyval == GDK_KEY_minus) {
    writer->font_size = std::max(kSmallestFontSize, writer->font_size - 1);
    apply_font_size(writer);
    char* message = g_strdup_printf("Text size %d", writer->font_size);
    show_notification(writer, message);
    g_free(message);
    return true;
  }
  if (keyval == GDK_KEY_0) {
    writer->font_size = kDefaultFontSize;
    apply_font_size(writer);
    show_notification(writer, "Text size reset");
    return true;
  }

  return false;
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

void on_activate(GtkApplication* app, gpointer) {
  auto* writer = new Writer();
  writer->literata_available = register_bundled_literata();
  char* path = g_build_filename(g_get_user_data_dir(), "write", "document.txt", nullptr);
  writer->document_path = path;
  g_free(path);
  char* positions_path = g_build_filename(g_get_user_data_dir(), "write", "positions.ini", nullptr);
  writer->positions_path = positions_path;
  g_free(positions_path);
  writer->positions = g_key_file_new();
  GError* positions_error = nullptr;
  g_key_file_load_from_file(writer->positions, writer->positions_path.c_str(), G_KEY_FILE_NONE, &positions_error);
  g_clear_error(&positions_error);

  GtkWidget* window = gtk_application_window_new(app);
  writer->window = GTK_WINDOW(window);
  gtk_window_set_title(GTK_WINDOW(window), "Write");
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
  char* saved_text = nullptr;
  gsize saved_length = 0;
  if (g_file_get_contents(writer->document_path.c_str(), &saved_text, &saved_length, nullptr)) {
    gtk_text_buffer_set_text(writer->buffer, saved_text, static_cast<gint>(saved_length));
    g_free(saved_text);
  }
  restore_cursor_position(writer);
  update_word_count(writer);

  g_signal_connect(writer->buffer, "changed", G_CALLBACK(on_buffer_changed), writer);
  g_signal_connect(writer->buffer, "mark-set", G_CALLBACK(on_cursor_moved), writer);
  g_signal_connect(search_entry, "changed", G_CALLBACK(on_search_changed), writer);
  g_signal_connect(search_entry, "activate", G_CALLBACK(on_search_activated), writer);
  g_signal_connect(editor, "notify::width", G_CALLBACK(center_writing_column), nullptr);
  gtk_widget_add_tick_callback(editor, maintain_writing_column, writer, nullptr);
  g_signal_connect(window, "close-request", G_CALLBACK(on_window_close), writer);
  g_object_set_data_full(G_OBJECT(window), "writer-state", writer, [](gpointer data) {
    delete static_cast<Writer*>(data);
  });

  GtkCssProvider* css = gtk_css_provider_new();
  gtk_css_provider_load_from_string(css, R"(
    window.write-window { background: #f7f3ea; color: #29251f; }
    .header { background: #f7f3ea; }
    .wordmark { font-family: Literata, C059, serif; font-size: 24px; font-style: italic; letter-spacing: -0.8px; }
    .status { color: #91897c; font-size: 12px; }
    .notification { color: #857a6b; font-size: 12px; }
    .find-container { margin-right: 12px; }
    entry.find-entry { min-height: 28px; padding: 0 8px; border: 1px solid #d7cfc2; border-radius: 5px; background: #f0ebe1; color: #312c25; font-size: 13px; box-shadow: none; }
    entry.find-entry:focus { border-color: #9b8971; box-shadow: 0 0 0 2px rgba(155, 137, 113, 0.18); }
    separator { background: #ddd6ca; min-height: 1px; }
    textview, textview text { background: #f7f3ea; color: #312c25; }
    textview:focus { outline: none; }
    scrollbar { background: transparent; }
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

}  // namespace

int main(int argc, char** argv) {
  GtkApplication* app = gtk_application_new(kAppId, G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
