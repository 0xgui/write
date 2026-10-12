#include "storage.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

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

std::string document_key(const std::string& path) {
  char* checksum = g_compute_checksum_for_string(G_CHECKSUM_SHA256, path.c_str(), -1);
  std::string key = checksum;
  g_free(checksum);
  return key;
}

bool atomic_write(const std::string& requested_path, const char* data, gsize length, std::string* error) {
  // Write through symlinks so the link target is updated, not replaced.
  char* resolved = realpath(requested_path.c_str(), nullptr);
  const std::string path = resolved != nullptr ? resolved : requested_path;
  free(resolved);
  char* directory = g_path_get_dirname(path.c_str());
  char* base = g_path_get_basename(path.c_str());
  char* temp_path = g_strdup_printf("%s/.%s.XXXXXX", directory, base);
  bool ok = false;
  const int fd = g_mkstemp_full(temp_path, O_WRONLY, 0644);

  if (fd < 0) {
    if (error != nullptr) *error = g_strerror(errno);
  } else {
    struct stat existing;
    if (stat(path.c_str(), &existing) == 0) fchmod(fd, existing.st_mode & 07777);

    gsize written = 0;
    while (written < length) {
      const ssize_t n = write(fd, data + written, length - written);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      written += static_cast<gsize>(n);
    }
    ok = written == length && fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(temp_path, path.c_str()) == 0;
    if (!ok) {
      if (error != nullptr) *error = g_strerror(errno);
      unlink(temp_path);
    } else {
      // Make the rename itself durable.
      const int dir_fd = open(directory, O_RDONLY | O_DIRECTORY);
      if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
      }
    }
  }

  g_free(temp_path);
  g_free(base);
  g_free(directory);
  return ok;
}

gint64 file_mtime(const std::string& path) {
  struct stat info;
  if (stat(path.c_str(), &info) != 0) return -1;
  return static_cast<gint64>(info.st_mtim.tv_sec) * G_USEC_PER_SEC + info.st_mtim.tv_nsec / 1000;
}

std::string backup_dir(const std::string& backups_root, const std::string& document_path) {
  return backups_root + "/" + document_key(document_path);
}

bool backup_due(gint64 last_backup_us, gint64 now_us) {
  return last_backup_us <= 0 || now_us - last_backup_us >= kBackupIntervalUs;
}

namespace {

std::vector<std::string> list_snapshots(const std::string& directory) {
  std::vector<std::string> snapshots;
  if (GDir* dir = g_dir_open(directory.c_str(), 0, nullptr)) {
    while (const char* entry = g_dir_read_name(dir)) {
      if (g_str_has_suffix(entry, ".txt")) snapshots.emplace_back(entry);
    }
    g_dir_close(dir);
  }
  std::sort(snapshots.begin(), snapshots.end());
  return snapshots;
}

}  // namespace

bool write_backup(const std::string& backups_root, const std::string& document_path,
                  const char* data, gsize length, gint64 now_us, int keep, std::string* error) {
  const std::string directory = backup_dir(backups_root, document_path);
  if (g_mkdir_with_parents(directory.c_str(), 0700) != 0) {
    if (error != nullptr) *error = g_strerror(errno);
    return false;
  }

  // Zero-padded UTC timestamps sort chronologically by name.
  // Skip a snapshot identical to the newest one, so reopening a file
  // repeatedly does not push real history out.
  std::vector<std::string> snapshots = list_snapshots(directory);
  if (!snapshots.empty()) {
    char* newest = nullptr;
    gsize newest_length = 0;
    const bool same = g_file_get_contents((directory + "/" + snapshots.back()).c_str(), &newest, &newest_length, nullptr)
        && newest_length == length && memcmp(newest, data, length) == 0;
    g_free(newest);
    if (same) return true;
  }

  GDateTime* time = g_date_time_new_from_unix_utc(now_us / G_USEC_PER_SEC);
  char* stamp = g_date_time_format(time, "%Y%m%d-%H%M%S");
  char* name = g_strdup_printf("%s-%06d.txt", stamp, static_cast<int>(now_us % G_USEC_PER_SEC));
  const std::string path = directory + "/" + name;
  g_free(name);
  g_free(stamp);
  g_date_time_unref(time);
  if (!atomic_write(path, data, length, error)) return false;

  snapshots = list_snapshots(directory);
  for (std::size_t i = 0; i + keep < snapshots.size(); ++i) {
    g_remove((directory + "/" + snapshots[i]).c_str());
  }
  return true;
}

std::string home_relative(const std::string& path, const std::string& home) {
  if (home.empty() || path.compare(0, home.size(), home) != 0) return path;
  if (path.size() == home.size()) return "~";
  if (path[home.size()] != '/') return path;
  return "~" + path.substr(home.size());
}
