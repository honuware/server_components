#include "ical_generator.h"

#include <gtest/gtest.h>

namespace ICalGenerator {
namespace {

// Helper: Jan 1, 2026 at 17:00 UTC in microseconds
constexpr int64_t kJan1_2026_17_00_UTC =
    1767286800LL * 1000000LL;  // 2026-01-01 17:00:00 UTC

// Helper: Jan 1, 2026 at 18:30 UTC in microseconds
constexpr int64_t kJan1_2026_18_30_UTC =
    (1767286800LL + 5400LL) * 1000000LL;  // 90 minutes later

// Deliberately non-Knotty test identity: proves the generator emits whatever
// PRODID / UID domain the caller supplies (componentization Phase 1.1) and
// carries no brand literal of its own.
constexpr std::string_view kTestProdId = "-//Test Studio//Booking//EN";
constexpr std::string_view kTestUidDomain = "test.example";
const ICalConfig kTestConfig{
    std::string(kTestProdId), std::string(kTestUidDomain)};

// Thin wrappers so the bulk of the tests read unchanged.
std::string Gen(const ICalEvent& event) {
    return GenerateICalendar(event, kTestConfig);
}
std::string Gen(const std::vector<ICalEvent>& events) {
    return GenerateICalendar(events, kTestConfig);
}

TEST(ICalGeneratorTest, GeneratesValidStructure) {
    ICalEvent event;
    event.title = "Yoga Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.timezone = "America/Los_Angeles";
    event.location = "Knotty Yoga Studio";
    event.description = "A relaxing yoga session";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("BEGIN:VCALENDAR"), std::string::npos);
    EXPECT_NE(ical.find("END:VCALENDAR"), std::string::npos);
    EXPECT_NE(ical.find("BEGIN:VEVENT"), std::string::npos);
    EXPECT_NE(ical.find("END:VEVENT"), std::string::npos);
    EXPECT_NE(ical.find("VERSION:2.0"), std::string::npos);
    EXPECT_NE(ical.find("PRODID:-//Test Studio//Booking//EN"), std::string::npos);
    EXPECT_NE(ical.find("CALSCALE:GREGORIAN"), std::string::npos);
    EXPECT_NE(ical.find("METHOD:PUBLISH"), std::string::npos);
}

TEST(ICalGeneratorTest, ContainsUtcDateTimes) {
    ICalEvent event;
    event.title = "Morning Session";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("DTSTART:20260101T170000Z"), std::string::npos)
        << "iCal: " << ical;
    EXPECT_NE(ical.find("DTEND:20260101T183000Z"), std::string::npos)
        << "iCal: " << ical;
}

// floatingLocal emits wall-clock times with no "Z" so the calendar shows the
// authored time in the viewer's own zone (class/series occurrences).
TEST(ICalGeneratorTest, FloatingLocalOmitsZSuffix) {
    ICalEvent event;
    event.title = "Evening Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.floatingLocal = true;

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("DTSTART:20260101T170000\r\n"), std::string::npos)
        << "iCal: " << ical;
    EXPECT_NE(ical.find("DTEND:20260101T183000\r\n"), std::string::npos)
        << "iCal: " << ical;
    // No UTC "Z" form when floating.
    EXPECT_EQ(ical.find("DTSTART:20260101T170000Z"), std::string::npos);
}

TEST(ICalGeneratorTest, ContainsSummary) {
    ICalEvent event;
    event.title = "Peak Spa - 60 Min Session";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("SUMMARY:Peak Spa - 60 Min Session"), std::string::npos);
}

TEST(ICalGeneratorTest, ContainsLocation) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.location = "Portland Studio";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("LOCATION:Portland Studio"), std::string::npos);
}

TEST(ICalGeneratorTest, ContainsDescription) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.description = "Your booking at Knotty Yoga";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("DESCRIPTION:Your booking at Knotty Yoga"), std::string::npos);
}

TEST(ICalGeneratorTest, OmitsLocationWhenEmpty) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.location = "";

    std::string ical = Gen(event);

    EXPECT_EQ(ical.find("LOCATION:"), std::string::npos);
}

TEST(ICalGeneratorTest, OmitsDescriptionWhenEmpty) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.description = "";

    std::string ical = Gen(event);

    EXPECT_EQ(ical.find("DESCRIPTION:"), std::string::npos);
}

TEST(ICalGeneratorTest, EscapesCommasInTitle) {
    ICalEvent event;
    event.title = "Yoga, Pilates, and More";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("SUMMARY:Yoga\\, Pilates\\, and More"), std::string::npos);
}

TEST(ICalGeneratorTest, EscapesSemicolonsInDescription) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.description = "Room: A; Level: Beginner";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("DESCRIPTION:Room: A\\; Level: Beginner"), std::string::npos);
}

TEST(ICalGeneratorTest, EscapesNewlinesInDescription) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.description = "Line 1\nLine 2";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("DESCRIPTION:Line 1\\nLine 2"), std::string::npos);
}

TEST(ICalGeneratorTest, UsesCrLfLineEndings) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;

    std::string ical = Gen(event);

    // Every line should end with \r\n
    EXPECT_NE(ical.find("BEGIN:VCALENDAR\r\n"), std::string::npos);
    EXPECT_NE(ical.find("END:VCALENDAR\r\n"), std::string::npos);
    EXPECT_NE(ical.find("BEGIN:VEVENT\r\n"), std::string::npos);
    EXPECT_NE(ical.find("END:VEVENT\r\n"), std::string::npos);
}

TEST(ICalGeneratorTest, DifferentTimestamps) {
    // March 15, 2026, 14:30 UTC = epoch 1773585000
    int64_t march15Us = 1773585000LL * 1000000LL;
    // March 15, 2026, 15:30 UTC (60 minutes later)
    int64_t march15EndUs = march15Us + 3600LL * 1000000LL;

    ICalEvent event;
    event.title = "Afternoon Session";
    event.startTimeUs = march15Us;
    event.endTimeUs = march15EndUs;

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("DTSTART:20260315T143000Z"), std::string::npos)
        << "iCal: " << ical;
    EXPECT_NE(ical.find("DTEND:20260315T153000Z"), std::string::npos)
        << "iCal: " << ical;
}

TEST(ICalGeneratorTest, EscapesBackslashInLocation) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.location = "Suite A\\B";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("LOCATION:Suite A\\\\B"), std::string::npos);
}

// ---- Classes Phase 4 extensions ----

TEST(ICalGeneratorTest, EmitsUidDtstampAndSequence) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.uid = "booking-42@knottyyoga.com";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("UID:booking-42@knottyyoga.com\r\n"), std::string::npos)
        << ical;
    EXPECT_NE(ical.find("DTSTAMP:"), std::string::npos);
    EXPECT_NE(ical.find("SEQUENCE:0\r\n"), std::string::npos);
}

TEST(ICalGeneratorTest, EmptyUidFallsBackToSynthetic) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    // uid left empty.

    std::string ical = Gen(event);

    // OQ-P4-3: a synthetic UID is substituted; generation never fails. The
    // synthetic domain comes from the caller's config, not a baked-in literal.
    EXPECT_NE(ical.find("UID:synthetic-"), std::string::npos) << ical;
    EXPECT_NE(ical.find("@test.example"), std::string::npos) << ical;
    EXPECT_EQ(ical.find("@knottyyoga.com"), std::string::npos) << ical;
}

TEST(ICalGeneratorTest, EmitsSequenceFromField) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.uid = "booking-1@knottyyoga.com";
    event.sequence = "2";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("SEQUENCE:2\r\n"), std::string::npos) << ical;
}

TEST(ICalGeneratorTest, EmitsStatusCancelled) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.uid = "booking-7@knottyyoga.com";
    event.status = "CANCELLED";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("STATUS:CANCELLED\r\n"), std::string::npos) << ical;
}

TEST(ICalGeneratorTest, OmitsStatusWhenEmpty) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;

    std::string ical = Gen(event);

    EXPECT_EQ(ical.find("STATUS:"), std::string::npos);
}

TEST(ICalGeneratorTest, EmitsRruleWhenSet) {
    ICalEvent event;
    event.title = "Recurring Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.uid = "schedule-3-person-9@knottyyoga.com";
    event.rrule = "FREQ=WEEKLY;BYDAY=TU";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("RRULE:FREQ=WEEKLY;BYDAY=TU\r\n"), std::string::npos)
        << ical;
    // Recurring events use the UTC DTSTART form for now (§3.3 fallback) — no
    // TZID/VTIMEZONE yet.
    EXPECT_EQ(ical.find("VTIMEZONE"), std::string::npos);
    EXPECT_NE(ical.find("DTSTART:20260101T170000Z"), std::string::npos);
}

TEST(ICalGeneratorTest, EmitsOrganizerAndAttendee) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.uid = "booking-5@knottyyoga.com";
    event.organizerName = "Knotty Yoga";
    event.organizerEmail = "studio@knottyyoga.com";
    event.attendeeName = "Ada Lovelace";
    event.attendeeEmail = "ada@example.com";

    std::string ical = Gen(event);

    EXPECT_NE(ical.find("ORGANIZER;CN=Knotty Yoga:mailto:studio@knottyyoga.com\r\n"),
              std::string::npos) << ical;
    EXPECT_NE(ical.find("ATTENDEE;CN=Ada Lovelace;RSVP=FALSE:mailto:ada@example.com\r\n"),
              std::string::npos) << ical;
}

TEST(ICalGeneratorTest, MultiEventOverloadWrapsThreeVevents) {
    auto make = [](const std::string& uid, const std::string& title) {
        ICalEvent e;
        e.title = title;
        e.startTimeUs = kJan1_2026_17_00_UTC;
        e.endTimeUs = kJan1_2026_18_30_UTC;
        e.uid = uid;
        return e;
    };
    std::vector<ICalEvent> events = {
        make("session-1@knottyyoga.com", "A"),
        make("session-2@knottyyoga.com", "B"),
        make("session-3@knottyyoga.com", "C"),
    };

    std::string ical = Gen(events);

    // Exactly one VCALENDAR wrapping three VEVENTs.
    EXPECT_EQ(ical.find("BEGIN:VCALENDAR"), ical.rfind("BEGIN:VCALENDAR"));
    auto countOccurrences = [](const std::string& hay, const std::string& needle) {
        size_t n = 0, pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos) { ++n; pos += needle.size(); }
        return n;
    };
    EXPECT_EQ(countOccurrences(ical, "BEGIN:VEVENT"), 3u);
    EXPECT_EQ(countOccurrences(ical, "END:VEVENT"), 3u);
    EXPECT_NE(ical.find("UID:session-2@knottyyoga.com"), std::string::npos);
}

TEST(ICalGeneratorTest, FoldsLongDescriptionLine) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    event.uid = "booking-1@knottyyoga.com";
    event.description = std::string(200, 'x');  // well over 75 octets

    std::string ical = Gen(event);

    // The folded continuation marker (CRLF + single space) must appear.
    EXPECT_NE(ical.find("\r\n "), std::string::npos) << ical;
    // No raw logical line should exceed 75 octets between CRLFs.
    size_t pos = 0;
    while (pos < ical.size()) {
        size_t eol = ical.find("\r\n", pos);
        size_t len = (eol == std::string::npos ? ical.size() : eol) - pos;
        EXPECT_LE(len, 75u) << "Unfolded line: " << ical.substr(pos, len);
        if (eol == std::string::npos) break;
        pos = eol + 2;
    }
}

TEST(FoldLineTest, ShortLineUnchanged) {
    std::string line = "SUMMARY:Short";
    EXPECT_EQ(FoldLine(line), line);
}

TEST(FoldLineTest, SplitsAtOctetBoundary) {
    std::string line = "DESCRIPTION:" + std::string(100, 'a');
    std::string folded = FoldLine(line);
    // First segment is exactly 75 octets, then CRLF + space.
    EXPECT_EQ(folded.substr(0, 75), line.substr(0, 75));
    EXPECT_EQ(folded.substr(75, 3), "\r\n ");
}

TEST(FoldLineTest, Utf8SafeDoesNotSplitMultiByte) {
    // Build a line whose 75th octet lands inside a 3-byte UTF-8 codepoint.
    // 'â‚¬' (euro sign, U+20AC) is 3 bytes: E2 82 AC.
    std::string line = "DESCRIPTION:";
    while (line.size() < 74) line += 'a';   // pad to octet 74
    line += "\xE2\x82\xAC";                  // euro at octets 75..77
    line += "tail";

    std::string folded = FoldLine(line);

    // The euro's 3 bytes must stay contiguous (not split across the fold).
    EXPECT_NE(folded.find("\xE2\x82\xAC"), std::string::npos);
    // And every physical line must be valid: the first segment backed off to
    // before the euro, so it is <= 75 octets.
    size_t firstEol = folded.find("\r\n");
    ASSERT_NE(firstEol, std::string::npos);
    EXPECT_LE(firstEol, 75u);
}

TEST(BuildUidTest, BookingSessionTemplateFormats) {
    EXPECT_EQ(BuildBookingUid(42, "test.example"), "booking-42@test.example");
    EXPECT_EQ(BuildSessionUid(7, "test.example"), "session-7@test.example");
    EXPECT_EQ(BuildTemplateUid(3, 9, "test.example"),
              "schedule-3-person-9@test.example");
}

TEST(BuildUidTest, TemplateOccurrenceUidIsSlotKeyedAndPerOccurrence) {
    EXPECT_EQ(BuildTemplateOccurrenceUid(5, 9, 1700000000000000LL, "test.example"),
              "slot-5-person-9-occ-1700000000000000@test.example");
    // Different occurrence dates → different UIDs (one VEVENT per occurrence).
    EXPECT_NE(BuildTemplateOccurrenceUid(5, 9, 1700000000000000LL, "test.example"),
              BuildTemplateOccurrenceUid(5, 9, 1700086400000000LL, "test.example"));
    // Different slots → different UIDs.
    EXPECT_NE(BuildTemplateOccurrenceUid(5, 9, 1700000000000000LL, "test.example"),
              BuildTemplateOccurrenceUid(6, 9, 1700000000000000LL, "test.example"));
}

// Phase 1.1: the PRODID and UID domain are the caller's, not baked in — two
// different configs produce two different documents.
TEST(ICalGeneratorTest, ProdIdAndUidDomainComeFromConfig) {
    ICalEvent event;
    event.title = "Class";
    event.startTimeUs = kJan1_2026_17_00_UTC;
    event.endTimeUs = kJan1_2026_18_30_UTC;
    // uid empty → the synthetic fallback exercises the config domain.

    ICalConfig acme{"-//Acme Fitness//Cal//EN", "acme.example"};
    std::string ical = GenerateICalendar(event, acme);

    EXPECT_NE(ical.find("PRODID:-//Acme Fitness//Cal//EN"), std::string::npos)
        << ical;
    EXPECT_NE(ical.find("@acme.example"), std::string::npos) << ical;
    // No trace of the other test config's identity.
    EXPECT_EQ(ical.find("Test Studio"), std::string::npos) << ical;
    EXPECT_EQ(ical.find("test.example"), std::string::npos) << ical;
}

TEST(BuildUidTest, UidDomainIsCallerSupplied) {
    EXPECT_EQ(BuildBookingUid(1, "acme.example"), "booking-1@acme.example");
    EXPECT_EQ(BuildSessionUid(1, "studio.test"), "session-1@studio.test");
}

TEST(BuildRRuleTest, WeeklyBiweeklyCustom) {
    // Day index 2 = Tuesday.
    int64_t until = 1767225600LL * 1000000LL;  // 2026-01-01 00:00:00 UTC
    EXPECT_EQ(BuildWeeklyRRule({2}, until),
              "FREQ=WEEKLY;BYDAY=TU;UNTIL=20260101T000000Z");
    EXPECT_EQ(BuildWeeklyRRule({1, 3, 5}, 0),
              "FREQ=WEEKLY;BYDAY=MO,WE,FR");
    EXPECT_EQ(BuildBiweeklyRRule({2}, 0),
              "FREQ=WEEKLY;INTERVAL=2;BYDAY=TU");
    EXPECT_EQ(BuildCustomRRule(3, 0), "FREQ=DAILY;INTERVAL=3");
}

}  // namespace
}  // namespace ICalGenerator
