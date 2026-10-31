#pragma once

#include <string>

// Turns the bytes of a text file into UTF-8 for the editor. Valid UTF-8 is
// returned unchanged (minus a byte-order mark). Anything else is converted from
// UTF-16 (with a byte-order mark), the locale's charset, Windows-1252, or
// ISO-8859-1, which accepts every byte, so no text is ever dropped.
// `from` is set to the charset that was converted from, or left empty.
std::string text_to_utf8(const char* bytes, std::size_t length, const char* locale_charset, std::string* from);
