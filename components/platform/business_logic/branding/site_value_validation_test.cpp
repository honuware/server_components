#include "business_logic/branding/site_value_validation.h"

#include <string>

#include <gtest/gtest.h>

namespace Branding {
namespace {

// --- IsValidHexColor: #RRGGBB and nothing else (D10) ---

TEST(SiteValueValidationTest, HexColorAcceptsSixDigitFormInEitherCase) {
    EXPECT_TRUE(IsValidHexColor("#ED1C26"));
    EXPECT_TRUE(IsValidHexColor("#ed1c26"));
    EXPECT_TRUE(IsValidHexColor("#000000"));
    EXPECT_TRUE(IsValidHexColor("#FFFFFF"));
}

TEST(SiteValueValidationTest, HexColorRejectsEverythingElse) {
    EXPECT_FALSE(IsValidHexColor(""));
    EXPECT_FALSE(IsValidHexColor("ED1C26"));      // missing '#'
    EXPECT_FALSE(IsValidHexColor("#ED1C2"));      // too short
    EXPECT_FALSE(IsValidHexColor("#ED1C266"));    // too long
    EXPECT_FALSE(IsValidHexColor("#F00"));        // shorthand
    EXPECT_FALSE(IsValidHexColor("#ED1C26FF"));   // alpha
    EXPECT_FALSE(IsValidHexColor("#ZZZZZZ"));     // not hex
    EXPECT_FALSE(IsValidHexColor("red"));
    EXPECT_FALSE(IsValidHexColor("rgb(1,2,3)"));
    // The reason the form is locked down: the value is written straight into a
    // CSS custom property, so anything that can close the declaration is out.
    EXPECT_FALSE(IsValidHexColor("#fff; background:url(x)"));
}

// --- IsValidSiteUrl: absolute http(s) or root-relative ---

TEST(SiteValueValidationTest, SiteUrlAcceptsHttpHttpsAndRootRelative) {
    EXPECT_TRUE(IsValidSiteUrl("https://cdn.example/logo.svg"));
    EXPECT_TRUE(IsValidSiteUrl("http://example.test/x.png"));
    EXPECT_TRUE(IsValidSiteUrl("/assets/svg/logo.svg"));
    EXPECT_TRUE(IsValidSiteUrl("/"));
    EXPECT_TRUE(IsValidSiteUrl("/api/get_scaled_photo/home_page_photos/3?w=800"));
}

TEST(SiteValueValidationTest, SiteUrlRejectsOtherSchemesAndRelativePaths) {
    EXPECT_FALSE(IsValidSiteUrl("javascript:alert(1)"));
    EXPECT_FALSE(IsValidSiteUrl("data:image/svg+xml;base64,AAAA"));
    EXPECT_FALSE(IsValidSiteUrl("ftp://example.test/logo.png"));
    EXPECT_FALSE(IsValidSiteUrl("assets/logo.svg"));  // relative, no leading /
    EXPECT_FALSE(IsValidSiteUrl("http://"));          // scheme with no host
    EXPECT_FALSE(IsValidSiteUrl("https://"));
}

TEST(SiteValueValidationTest, SiteUrlRejectsProtocolRelativeAndBackslashForms) {
    // `//evil.test/x` looks root-relative but fetches from another origin.
    EXPECT_FALSE(IsValidSiteUrl("//evil.test/logo.svg"));
    EXPECT_FALSE(IsValidSiteUrl("/\\evil.test/logo.svg"));
}

TEST(SiteValueValidationTest, SiteUrlRejectsWhitespaceControlCharsAndOverlongValues) {
    EXPECT_FALSE(IsValidSiteUrl("https://example.test/a b.png"));
    EXPECT_FALSE(IsValidSiteUrl("https://example.test/a\nb.png"));
    EXPECT_FALSE(IsValidSiteUrl("https://example.test/a\tb.png"));
    std::string overlong = "https://example.test/" + std::string(kMaxUrlBytes, 'a');
    EXPECT_FALSE(IsValidSiteUrl(overlong));
}

// --- ValidateSlotValue: the write path's reasons ---

TEST(SiteValueValidationTest, EmptyValueIsValidForEverySlotType) {
    // Empty means "unset — use the bundled default", so clearing a field is how
    // a tenant reverts one. It must never be an error.
    EXPECT_EQ(ValidateSlotValue(SlotType::Line, ""), "");
    EXPECT_EQ(ValidateSlotValue(SlotType::Lines, ""), "");
    EXPECT_EQ(ValidateSlotValue(SlotType::Markdown, ""), "");
    EXPECT_EQ(ValidateSlotValue(SlotType::Url, ""), "");
    EXPECT_EQ(ValidateSlotValue(SlotType::Color, ""), "");
}

TEST(SiteValueValidationTest, LineAcceptsOrdinaryCopyIncludingMultiByteCharacters) {
    EXPECT_EQ(ValidateSlotValue(SlotType::Line, "Knotty Yoga Fitness Studio"), "");
    // The shipped tagline and blurbs carry em dashes and emoji; UTF-8
    // continuation bytes must not read as control characters.
    EXPECT_EQ(
        ValidateSlotValue(
            SlotType::Line,
            "Unlimited classes — pick the tier that fits how you train. \xF0\x9F\x92\xAA"),
        "");
}

TEST(SiteValueValidationTest, LineRejectsNewlinesAndControlCharacters) {
    EXPECT_FALSE(ValidateSlotValue(SlotType::Line, "one\ntwo").empty());
    EXPECT_FALSE(ValidateSlotValue(SlotType::Line, "one\r\ntwo").empty());
    EXPECT_FALSE(ValidateSlotValue(SlotType::Line, std::string("a\0b", 3)).empty());
    EXPECT_FALSE(ValidateSlotValue(SlotType::Line, "tab\there").empty());
}

TEST(SiteValueValidationTest, LinesAcceptsNewlinesButNotOtherControlCharacters) {
    EXPECT_EQ(
        ValidateSlotValue(
            SlotType::Lines,
            "That which doesn't kill you\nmakes you hotter."),
        "");
    EXPECT_FALSE(ValidateSlotValue(SlotType::Lines, "a\tb").empty());
    EXPECT_FALSE(ValidateSlotValue(SlotType::Lines, std::string("a\0b", 3)).empty());
}

TEST(SiteValueValidationTest, MarkdownAcceptsNewlinesAndTabs) {
    EXPECT_EQ(
        ValidateSlotValue(SlotType::Markdown, "# Heading\n\n- item\n\t- nested"),
        "");
}

TEST(SiteValueValidationTest, SizeCapsRejectOversizeValuesPerType) {
    EXPECT_FALSE(
        ValidateSlotValue(SlotType::Line, std::string(kMaxLineBytes + 1, 'a')).empty());
    EXPECT_EQ(
        ValidateSlotValue(SlotType::Line, std::string(kMaxLineBytes, 'a')), "");

    EXPECT_FALSE(
        ValidateSlotValue(SlotType::Lines, std::string(kMaxLinesBytes + 1, 'a')).empty());
    EXPECT_EQ(
        ValidateSlotValue(SlotType::Lines, std::string(kMaxLinesBytes, 'a')), "");

    EXPECT_FALSE(
        ValidateSlotValue(SlotType::Markdown, std::string(kMaxMarkdownBytes + 1, 'a'))
            .empty());
    EXPECT_EQ(
        ValidateSlotValue(SlotType::Markdown, std::string(kMaxMarkdownBytes, 'a')), "");
}

TEST(SiteValueValidationTest, RejectionReasonNamesTheLimitSoTheAdminCanActOnIt) {
    std::string reason =
        ValidateSlotValue(SlotType::Markdown, std::string(kMaxMarkdownBytes + 1, 'a'));
    EXPECT_NE(reason.find("65536"), std::string::npos) << reason;
}

TEST(SiteValueValidationTest, UrlAndColorSlotsDelegateToTheirPredicates) {
    EXPECT_EQ(ValidateSlotValue(SlotType::Url, "https://example.test/x.svg"), "");
    EXPECT_FALSE(ValidateSlotValue(SlotType::Url, "javascript:alert(1)").empty());
    EXPECT_EQ(ValidateSlotValue(SlotType::Color, "#ED1C26"), "");
    EXPECT_FALSE(ValidateSlotValue(SlotType::Color, "#F00").empty());
}

// --- NormalizeLineEndings ---

TEST(SiteValueValidationTest, NormalizeLineEndingsCollapsesCrlfAndLoneCr) {
    EXPECT_EQ(NormalizeLineEndings("a\r\nb"), "a\nb");
    EXPECT_EQ(NormalizeLineEndings("a\rb"), "a\nb");
    EXPECT_EQ(NormalizeLineEndings("a\nb"), "a\nb");
    EXPECT_EQ(NormalizeLineEndings("a\r\n\r\nb"), "a\n\nb");
    EXPECT_EQ(NormalizeLineEndings(""), "");
    EXPECT_EQ(NormalizeLineEndings("no line breaks"), "no line breaks");
}

// --- NormalizeSlotValue: the read path's repair-or-blank (D10) ---

TEST(SiteValueValidationTest, NormalizeSlotValueRepairsWindowsLineEndings) {
    // An admin pasting into a textarea is the normal case, not an error.
    EXPECT_EQ(
        NormalizeSlotValue(SlotType::Lines, "line one\r\nline two"),
        "line one\nline two");
}

TEST(SiteValueValidationTest, NormalizeSlotValueBlanksJunkRatherThanPropagatingIt) {
    // D10: a junk value falls back to the SPA's default rather than breaking
    // every page of a tenant's site.
    EXPECT_EQ(NormalizeSlotValue(SlotType::Url, "javascript:alert(1)"), "");
    EXPECT_EQ(NormalizeSlotValue(SlotType::Color, "not-a-color"), "");
    EXPECT_EQ(NormalizeSlotValue(SlotType::Line, "headline\nwith a newline"), "");
    EXPECT_EQ(
        NormalizeSlotValue(SlotType::Markdown, std::string(kMaxMarkdownBytes + 1, 'a')),
        "");
}

TEST(SiteValueValidationTest, NormalizeSlotValuePassesGoodValuesThroughUnchanged) {
    EXPECT_EQ(
        NormalizeSlotValue(SlotType::Line, "Knotty Yoga Fitness Studio"),
        "Knotty Yoga Fitness Studio");
    EXPECT_EQ(
        NormalizeSlotValue(SlotType::Url, "/assets/svg/logo.svg"),
        "/assets/svg/logo.svg");
    EXPECT_EQ(NormalizeSlotValue(SlotType::Color, "#ED1C26"), "#ED1C26");
    EXPECT_EQ(NormalizeSlotValue(SlotType::Markdown, "# About\n\nWe are here."),
              "# About\n\nWe are here.");
}

}  // namespace
}  // namespace Branding
