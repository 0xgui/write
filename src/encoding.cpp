#include "encoding.h"

#include <glib.h>

#include <cstring>

namespace {

bool convert(const char* bytes, std::size_t length, const char* charset, std::string* out) {
  gsize written = 0;
  char* utf8 = g_convert(bytes, static_cast<gssize>(length), "UTF-8", charset, nullptr, &written, nullptr);
  if (utf8 == nullptr) return false;
  // The editor cannot hold NUL characters, and a valid result is required.
  const bool usable = std::memchr(utf8, '\0', written) == nullptr && g_utf8_validate(utf8, static_cast<gssize>(written), nullptr);
  if (usable) out->assign(utf8, written);
  g_free(utf8);
  return usable;
}

}  // namespace

std::string text_to_utf8(const char* bytes, std::size_t length, const char* locale_charset, std::string* from) {
  from->clear();
  if (length >= 3 && std::memcmp(bytes, "\xEF\xBB\xBF", 3) == 0) {
    bytes += 3;
    length -= 3;
  }
  if (std::memchr(bytes, '\0', length) == nullptr && g_utf8_validate(bytes, static_cast<gssize>(length), nullptr)) {
    return std::string(bytes, length);
  }
  std::string text;
  if (length >= 2 && (std::memcmp(bytes, "\xFF\xFE", 2) == 0 || std::memcmp(bytes, "\xFE\xFF", 2) == 0)) {
    if (convert(bytes, length, "UTF-16", &text)) {
      // Drop the converted byte-order mark (U+FEFF) at the start.
      if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
      *from = "UTF-16";
      return text;
    }
  }
  const bool locale_is_utf8 = locale_charset == nullptr || g_ascii_strcasecmp(locale_charset, "UTF-8") == 0;
  if (!locale_is_utf8 && convert(bytes, length, locale_charset, &text)) {
    *from = locale_charset;
    return text;
  }
  for (const char* charset : {"Windows-1252", "ISO-8859-1"}) {
    if (convert(bytes, length, charset, &text)) {
      *from = charset;
      return text;
    }
  }
  // Only NUL bytes are left to explain: keep everything else, NULs as U+FFFD.
  std::string raw(bytes, length);
  for (char& c : raw) {
    if (c == '\0') c = '\x01';
  }
  convert(raw.data(), raw.size(), "ISO-8859-1", &text);
  std::string out;
  for (char c : text) {
    if (c == '\x01') {
      out += "\xEF\xBF\xBD";
    } else {
      out += c;
    }
  }
  *from = "ISO-8859-1";
  return out;
}
