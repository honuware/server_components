#include "bytea.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace SqlUtil {
namespace {

TEST(ByteaTest, EncodeProducesPostgresHexForm) {
    EXPECT_EQ(ByteaHexEncode(std::string_view("\x00\x01\xFF", 3)), "\\x0001ff");
    EXPECT_EQ(ByteaHexEncode(std::string_view("A")), "\\x41");
    EXPECT_EQ(ByteaHexEncode(std::string_view("")), "\\x");
}

TEST(ByteaTest, EncodeIsLowercaseAndZeroPadded) {
    // Postgres emits lowercase; matching it keeps stored values comparable.
    EXPECT_EQ(ByteaHexEncode(std::string_view("\x0A\xB0", 2)), "\\x0ab0");
}

TEST(ByteaTest, DecodeReversesEncodeForArbitraryBytes) {
    std::string original;
    for (int i = 0; i < 256; ++i) {
        original.push_back(static_cast<char>(i));
    }
    EXPECT_EQ(ByteaHexDecode(ByteaHexEncode(original)), original);
}

TEST(ByteaTest, DecodeAcceptsUppercaseHex) {
    EXPECT_EQ(ByteaHexDecode("\\x0AB0"), std::string("\x0A\xB0", 2));
}

TEST(ByteaTest, DecodePassesThroughAValueThatIsNotHexForm) {
    // Rows written before a column became bytea, and test doubles that hand
    // back raw bytes, both rely on this passthrough.
    EXPECT_EQ(ByteaHexDecode("plain text"), "plain text");
    EXPECT_EQ(ByteaHexDecode(""), "");
    EXPECT_EQ(ByteaHexDecode("\\"), "\\");
}

TEST(ByteaTest, DecodeStopsAtJunkRatherThanThrowing) {
    // This decodes untrusted database content on a read path; the previous
    // std::stoi implementation would have thrown out of it.
    EXPECT_NO_THROW(ByteaHexDecode("\\x41ZZ41"));
    EXPECT_EQ(ByteaHexDecode("\\x41ZZ41"), "A");
    // An odd trailing nibble is dropped, not guessed at.
    EXPECT_EQ(ByteaHexDecode("\\x414"), "A");
}

TEST(ByteaTest, VectorOverloadsRoundTrip) {
    std::vector<char> bytes{'\x00', 'w', 'O', 'F', '2', '\xFF'};
    std::string encoded = ByteaHexEncode(bytes);
    EXPECT_EQ(encoded, "\\x00774f4632ff");
    EXPECT_EQ(ByteaHexDecodeToVector(encoded), bytes);
}

TEST(ByteaTest, HandlesEmbeddedNulsSoBinaryIsNotTruncated) {
    // The reason this is a codec and not a cast: font and image bytes contain
    // NULs, and a C-string round trip would stop at the first one.
    std::string withNuls("\x01\x00\x02\x00\x03", 5);
    EXPECT_EQ(ByteaHexDecode(ByteaHexEncode(withNuls)).size(), 5u);
    EXPECT_EQ(ByteaHexDecode(ByteaHexEncode(withNuls)), withNuls);
}

}  // namespace
}  // namespace SqlUtil
