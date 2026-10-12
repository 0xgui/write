#pragma once

// GTK-free document helpers (GLib and POSIX only), so they can be unit tested.

#include <glib.h>

#include <string>

guint word_count(const char* text);
std::string document_key(const std::string& path);

// Writes to a temporary file beside `path`, fsyncs it, then renames it into
// place, so a crash never leaves a half-written document.
bool atomic_write(const std::string& path, const char* data, gsize length, std::string* error);

// Modification time in microseconds, or -1 when the file does not exist.
gint64 file_mtime(const std::string& path);

constexpr gint64 kBackupIntervalUs = G_GINT64_CONSTANT(5) * 60 * G_USEC_PER_SEC;
constexpr int kBackupsKept = 20;

// Snapshots live in <root>/<hash of document path>/<timestamp>.txt.
std::string backup_dir(const std::string& backups_root, const std::string& document_path);
bool backup_due(gint64 last_backup_us, gint64 now_us);
// Writes one snapshot and keeps only the newest `keep` for that document.
bool write_backup(const std::string& backups_root, const std::string& document_path,
                  const char* data, gsize length, gint64 now_us, int keep, std::string* error);
