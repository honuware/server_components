#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace SqlUtil {

// PostgreSQL `bytea` hex wire format (`\xABCD…`).
//
// Binary column values travel through this codebase's KeyValueTable layer as
// STRINGS, so every table helper that stores bytes has to encode on the way in
// and decode on the way out. This lived as two private functions inside
// business_logic/images/image_helper.cpp until the font inventory (Tenant
// Theming Phase 4B) became the second store of binary data — a binary wire
// format implemented twice is one that eventually disagrees with itself.
//
// Decode is deliberately TOLERANT of a value that is not in hex form: it
// returns the input unchanged. Rows written before a column became bytea, and
// test doubles that hand back raw bytes, both rely on that passthrough.

std::string ByteaHexEncode(std::string_view bytes);
std::string ByteaHexEncode(const std::vector<char>& bytes);

std::string ByteaHexDecode(std::string_view hex);

// vector<char> overload for callers holding image/font bytes in that shape.
std::vector<char> ByteaHexDecodeToVector(std::string_view hex);

}  // namespace SqlUtil
