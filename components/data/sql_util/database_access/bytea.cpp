#include "bytea.h"

namespace SqlUtil {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string EncodeImpl(const unsigned char* data, std::size_t size) {
    std::string out;
    out.reserve(size * 2 + 2);
    out += "\\x";
    for (std::size_t i = 0; i < size; ++i) {
        out.push_back(kHexDigits[data[i] >> 4]);
        out.push_back(kHexDigits[data[i] & 0x0F]);
    }
    return out;
}

}  // namespace

std::string ByteaHexEncode(std::string_view bytes) {
    return EncodeImpl(
        reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

std::string ByteaHexEncode(const std::vector<char>& bytes) {
    return EncodeImpl(
        reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
}

std::string ByteaHexDecode(std::string_view hex) {
    // Not hex-form — hand it back untouched (see the header's note).
    if (hex.size() < 2 || hex[0] != '\\' || hex[1] != 'x') {
        return std::string(hex);
    }
    std::string out;
    out.reserve((hex.size() - 2) / 2);
    for (std::size_t i = 2; i + 1 < hex.size(); i += 2) {
        int high = HexValue(hex[i]);
        int low = HexValue(hex[i + 1]);
        // A stray non-hex character ends the run rather than throwing: this
        // decodes untrusted database content, and the old std::stoi version
        // would have thrown out of a read path.
        if (high < 0 || low < 0) {
            break;
        }
        out.push_back(static_cast<char>((high << 4) | low));
    }
    return out;
}

std::vector<char> ByteaHexDecodeToVector(std::string_view hex) {
    std::string decoded = ByteaHexDecode(hex);
    return std::vector<char>(decoded.begin(), decoded.end());
}

}  // namespace SqlUtil
