// Tests for reading non-UTF-8 text files: `make test`.
#include <cstdio>
#include <string>

#include "../src/encoding.h"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string decode(const std::string& bytes, std::string* from, const char* locale = "UTF-8") {
  return text_to_utf8(bytes.data(), bytes.size(), locale, from);
}

}  // namespace

int main() {
  std::string from;
  check(decode("caf\xC3\xA9", &from) == "caf\xC3\xA9" && from.empty(), "UTF-8 is left alone");
  check(decode("\xEF\xBB\xBFhi", &from) == "hi" && from.empty(), "a UTF-8 byte-order mark is dropped");
  check(decode("", &from).empty() && from.empty(), "an empty file stays empty");
  check(decode("caf\xE9 cr\xE8me", &from) == "caf\xC3\xA9 cr\xC3\xA8me" && from == "Windows-1252",
        "Latin-1 accents become UTF-8");
  check(decode("\x93quoted\x94 \x80", &from) == "\xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x82\xAC" && from == "Windows-1252",
        "Windows-1252 quotes and the euro sign convert");
  check(decode("a\x81z", &from) == "a\xC2\x81z" && from == "ISO-8859-1",
        "bytes Windows-1252 lacks fall back to ISO-8859-1");
  check(decode("caf\xE9", &from, "ISO-8859-15") == "caf\xC3\xA9" && from == "ISO-8859-15",
        "the locale charset is tried first");
  check(decode(std::string("\xFF\xFEh\0i\0", 6), &from) == "hi" && from == "UTF-16", "UTF-16 with a byte-order mark");
  check(decode(std::string("a\0b", 3), &from) == "a\xEF\xBF\xBD" "b", "a NUL byte is kept visible, not dropped");
  if (failures == 0) std::puts("encoding tests passed");
  return failures == 0 ? 0 : 1;
}
