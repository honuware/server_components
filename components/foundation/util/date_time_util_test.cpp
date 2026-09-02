#include "date_time_util.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "date/date.h"

namespace DateTimeUtil {
namespace {

// Helper to build expected epoch ms for a UTC moment, without chrono literals.
static std::int64_t ExpectedMsUtc(int y, unsigned m, unsigned d,
    int hh, int mm, int ss, int ms = 0) {
    date::sys_time<std::chrono::milliseconds> tp =
        date::sys_days{ date::year{y} / date::month{m} / date::day{d} }
        + std::chrono::hours{ hh }
        + std::chrono::minutes{ mm }
        + std::chrono::seconds{ ss }
    + std::chrono::milliseconds{ ms };

    return tp.time_since_epoch().count();
}

TEST(DateTimeUtilTest, ParsesSpaceSeparatedWithOffset) {
    // "YYYY-MM-DD HH:MM:SS.sss�HH:MM" -> "%F %T%Ez"
    const char* s = "2025-10-06 12:34:56.789+00:00";
    auto expected = ExpectedMsUtc(2025, 10, 6, 12, 34, 56, 789);
    ASSERT_EQ(StringToEpochMillis(s), expected);
}

TEST(DateTimeUtilTest, FormatDateFromMicroseconds) {
    // March 4, 2026 19:00:00 UTC in microseconds
    // 2026-03-04 = date::sys_days{date::year{2026}/3/4}
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{4}}
        + std::chrono::hours{19};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(FormatDateFromMicroseconds(us), "March 4, 2026");
}

TEST(DateTimeUtilTest, FormatDateFromMicrosecondsJanuary) {
    auto tp = date::sys_days{date::year{2025}/date::month{1}/date::day{15}}
        + std::chrono::hours{10};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(FormatDateFromMicroseconds(us), "January 15, 2025");
}

TEST(DateTimeUtilTest, FormatTimeFromMicrosecondsPM) {
    // 7:00 PM UTC
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{4}}
        + std::chrono::hours{19};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(FormatTimeFromMicroseconds(us), "7:00 PM");
}

TEST(DateTimeUtilTest, FormatTimeFromMicrosecondsAM) {
    // 10:30 AM UTC
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{4}}
        + std::chrono::hours{10} + std::chrono::minutes{30};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(FormatTimeFromMicroseconds(us), "10:30 AM");
}

TEST(DateTimeUtilTest, FormatTimeFromMicrosecondsMidnight) {
    // 12:00 AM (midnight)
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{4}};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(FormatTimeFromMicroseconds(us), "12:00 AM");
}

TEST(DateTimeUtilTest, FormatTimeFromMicrosecondsNoon) {
    // 12:00 PM (noon)
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{4}}
        + std::chrono::hours{12};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(FormatTimeFromMicroseconds(us), "12:00 PM");
}

// Helper to build expected epoch us for a UTC date at midnight.
static std::int64_t ExpectedUsUtcDate(int y, unsigned m, unsigned d) {
    date::sys_days days = date::sys_days{
        date::year{y} / date::month{m} / date::day{d}};
    return std::chrono::duration_cast<std::chrono::microseconds>(
        days.time_since_epoch()).count();
}

TEST(DateTimeUtilTest, StartOfMonthUs) {
    // January 2026
    int64_t result = StartOfMonthUs(2026, 1);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 1, 1));

    // March 2026
    result = StartOfMonthUs(2026, 3);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 3, 1));

    // December 2025
    result = StartOfMonthUs(2025, 12);
    EXPECT_EQ(result, ExpectedUsUtcDate(2025, 12, 1));
}

TEST(DateTimeUtilTest, EndOfMonthUs) {
    // End of January 2026 = start of February 2026
    int64_t result = EndOfMonthUs(2026, 1);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 2, 1));

    // End of December 2025 = start of January 2026
    result = EndOfMonthUs(2025, 12);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 1, 1));

    // End of February 2026 = start of March 2026
    result = EndOfMonthUs(2026, 2);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 3, 1));
}

TEST(DateTimeUtilTest, StartOfContainingMonthUs) {
    // March 15, 2026 10:30 UTC -> March 1, 2026 00:00 UTC
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{15}}
        + std::chrono::hours{10} + std::chrono::minutes{30};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    int64_t result = StartOfContainingMonthUs(us);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 3, 1));
}

TEST(DateTimeUtilTest, StartOfContainingMonthUsFirstDay) {
    // March 1, 2026 00:00 UTC -> March 1, 2026 00:00 UTC
    int64_t us = ExpectedUsUtcDate(2026, 3, 1);
    int64_t result = StartOfContainingMonthUs(us);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 3, 1));
}

TEST(DateTimeUtilTest, EndOfContainingMonthUs) {
    // March 15, 2026 -> April 1, 2026
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{15}}
        + std::chrono::hours{10};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    int64_t result = EndOfContainingMonthUs(us);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 4, 1));
}

TEST(DateTimeUtilTest, EndOfContainingMonthUsDecember) {
    // December 25, 2025 -> January 1, 2026
    auto tp = date::sys_days{date::year{2025}/date::month{12}/date::day{25}};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    int64_t result = EndOfContainingMonthUs(us);
    EXPECT_EQ(result, ExpectedUsUtcDate(2026, 1, 1));
}

// ── LocalWallClockToUs ──

namespace {

// Microseconds for a UTC instant, so a test can state the moment it means
// without depending on any of the functions under test.
int64_t UtcUs(int y, unsigned m, unsigned d, int hour, int minute = 0) {
    auto tp = date::sys_days{date::year{y}/date::month{m}/date::day{d}}
        + std::chrono::hours{hour} + std::chrono::minutes{minute};
    return std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
}

}  // namespace

TEST(DateTimeUtilTest, LocalWallClockToUsResolvesAWallClockTime) {
    // Any instant during June 10 2026 Pacific; ask for 10:00 local.
    // PDT is UTC-7 in June, so 10:00 PDT = 17:00 UTC.
    int64_t anyMomentThatDay = UtcUs(2026, 6, 10, 20);  // 1 PM PDT
    int64_t result = LocalWallClockToUs(
        anyMomentThatDay, 10 * 60, "America/Los_Angeles");
    EXPECT_EQ(result, UtcUs(2026, 6, 10, 17));
}

TEST(DateTimeUtilTest, LocalWallClockToUsIsIndependentOfTheTimeOfDayPassedIn) {
    // Only the local DAY of the input matters — the function must not carry
    // any of the input's time-of-day through.
    const int64_t morning = UtcUs(2026, 6, 10, 16);   // 9 AM PDT
    const int64_t evening = UtcUs(2026, 6, 10, 23);   // 4 PM PDT
    EXPECT_EQ(LocalWallClockToUs(morning, 18 * 60, "America/Los_Angeles"),
              LocalWallClockToUs(evening, 18 * 60, "America/Los_Angeles"));
}

TEST(DateTimeUtilTest, GetMidnightUsIsCorrectOnASpringForwardDay) {
    // Regression: GetMidnightUs used to inherit tm_isdst from the SOURCE
    // instant. Asked for midnight from an afternoon on a spring-forward day,
    // the flag said "DST" (true of the afternoon, false of midnight), so
    // mktime read 00:00 as PDT when it was PST and returned an instant an hour
    // early — 23:00 the previous day. Two days a year, silently.
    //
    // US DST begins Sunday 8 March 2026 at 02:00. Local midnight that day is
    // still PST (UTC-8) = 08:00 UTC.
    const int64_t afternoon = UtcUs(2026, 3, 8, 20);  // 1 PM PDT
    EXPECT_EQ(GetMidnightUs(afternoon, "America/Los_Angeles"),
              UtcUs(2026, 3, 8, 8));

    // The existing March-14 test passes either way because both the source and
    // midnight are PDT — which is exactly why the bug survived.
}

TEST(DateTimeUtilTest, LocalWallClockToUsIsCorrectOnASpringForwardDay) {
    // THE reason this function exists. US DST begins Sunday 8 March 2026: the
    // clock jumps 02:00 → 03:00, so that local day is 23 hours long and
    // wall-clock 10:00 is only NINE elapsed hours after local midnight.
    //
    // 10:00 PDT (UTC-7) = 17:00 UTC.
    int64_t during = UtcUs(2026, 3, 8, 20);
    int64_t result = LocalWallClockToUs(during, 10 * 60, "America/Los_Angeles");
    EXPECT_EQ(result, UtcUs(2026, 3, 8, 17));

    // And the naive form is wrong by exactly the skipped hour — pinned so the
    // implementation can never quietly regress to midnight + minutes.
    int64_t naive = GetMidnightUs(during, "America/Los_Angeles")
        + static_cast<int64_t>(10 * 60) * 60 * 1000000LL;
    EXPECT_NE(naive, result);
    EXPECT_EQ(naive - result, 3600LL * 1000000LL);
}

TEST(DateTimeUtilTest, LocalWallClockToUsIsCorrectOnAFallBackDay) {
    // US DST ends Sunday 1 November 2026 (25-hour local day). 10:00 PST
    // (UTC-8) = 18:00 UTC.
    int64_t during = UtcUs(2026, 11, 1, 20);
    EXPECT_EQ(LocalWallClockToUs(during, 10 * 60, "America/Los_Angeles"),
              UtcUs(2026, 11, 1, 18));
}

TEST(DateTimeUtilTest, LocalWallClockToUsAgreesWithMidnightAtZeroMinutes) {
    int64_t during = UtcUs(2026, 6, 10, 20);
    EXPECT_EQ(LocalWallClockToUs(during, 0, "America/Los_Angeles"),
              GetMidnightUs(during, "America/Los_Angeles"));
}

TEST(DateTimeUtilTest, LocalWallClockToUsHandlesAZoneWithoutDst) {
    // Phoenix never shifts, so 10:00 local is always 17:00 UTC.
    EXPECT_EQ(LocalWallClockToUs(UtcUs(2026, 6, 10, 20), 10 * 60, "America/Phoenix"),
              UtcUs(2026, 6, 10, 17));
    EXPECT_EQ(LocalWallClockToUs(UtcUs(2026, 12, 10, 20), 10 * 60, "America/Phoenix"),
              UtcUs(2026, 12, 10, 17));
}

// ── Calendar date tokens: CalendarDateWallClockToUs / LocalDateTokenUs ──
//
// A token is UTC midnight of a date, standing for the DATE, not an instant —
// the encoding event_sessions.occurrence_date_us uses. UtcUs(y, m, d, 0) is
// exactly such a token.

TEST(DateTimeUtilTest, CalendarDateWallClockToUsResolvesAWallClockTime) {
    // The token for June 10 2026; 10:00 PDT (UTC-7) = 17:00 UTC.
    EXPECT_EQ(CalendarDateWallClockToUs(
                  UtcUs(2026, 6, 10, 0), 10 * 60, "America/Los_Angeles"),
              UtcUs(2026, 6, 10, 17));
}

// THE reason this function exists, and the bug it was written to prevent.
TEST(DateTimeUtilTest, CalendarDateWallClockToUsDoesNotSlipToThePreviousDay) {
    const int64_t token = UtcUs(2026, 6, 10, 0);

    // LocalWallClockToUs derives the local date FROM the instant, and UTC
    // midnight Wednesday is Tuesday 17:00 in Los Angeles — so it answers with
    // 10:00 on June 9. Handing a token to the wrong helper is a silent
    // one-day error, which is why they are separate functions.
    EXPECT_EQ(LocalWallClockToUs(token, 10 * 60, "America/Los_Angeles"),
              UtcUs(2026, 6, 9, 17));
    EXPECT_EQ(CalendarDateWallClockToUs(token, 10 * 60, "America/Los_Angeles"),
              UtcUs(2026, 6, 10, 17));
    EXPECT_EQ(CalendarDateWallClockToUs(token, 10 * 60, "America/Los_Angeles")
                  - LocalWallClockToUs(token, 10 * 60, "America/Los_Angeles"),
              86400LL * 1000000LL);

    // In a positive-offset zone the naive helper slips the other way (UTC
    // midnight June 10 is already 09:00 June 10 in Tokyo, so both agree) —
    // the token form is the one that is right in every zone.
    EXPECT_EQ(CalendarDateWallClockToUs(token, 10 * 60, "UTC"),
              UtcUs(2026, 6, 10, 10));
}

TEST(DateTimeUtilTest, CalendarDateWallClockToUsIsCorrectAcrossDstBoundaries) {
    // Spring forward, Sunday 8 March 2026: 10:00 PDT (UTC-7) = 17:00 UTC.
    EXPECT_EQ(CalendarDateWallClockToUs(
                  UtcUs(2026, 3, 8, 0), 10 * 60, "America/Los_Angeles"),
              UtcUs(2026, 3, 8, 17));
    // Fall back, Sunday 1 November 2026: 10:00 PST (UTC-8) = 18:00 UTC.
    EXPECT_EQ(CalendarDateWallClockToUs(
                  UtcUs(2026, 11, 1, 0), 10 * 60, "America/Los_Angeles"),
              UtcUs(2026, 11, 1, 18));
    // Phoenix never shifts.
    EXPECT_EQ(CalendarDateWallClockToUs(
                  UtcUs(2026, 6, 10, 0), 10 * 60, "America/Phoenix"),
              UtcUs(2026, 6, 10, 17));
    EXPECT_EQ(CalendarDateWallClockToUs(
                  UtcUs(2026, 12, 10, 0), 10 * 60, "America/Phoenix"),
              UtcUs(2026, 12, 10, 17));
}

TEST(DateTimeUtilTest, CalendarDateWallClockToUsNormalisesPastMidnight) {
    // 25:30 on June 10 is 01:30 on June 11 — 08:30 UTC.
    EXPECT_EQ(CalendarDateWallClockToUs(
                  UtcUs(2026, 6, 10, 0), 25 * 60 + 30, "America/Los_Angeles"),
              UtcUs(2026, 6, 11, 8, 30));
}

TEST(DateTimeUtilTest, LocalDateTokenUsUsesTheLocalDateNotTheUtcDate) {
    // 03:00 UTC on June 11 is still 20:00 on June 10 in Los Angeles, so the
    // studio's calendar date — and the token — is June 10. Reading the UTC
    // date here is precisely the "starts a day late" bug in the day loop.
    EXPECT_EQ(LocalDateTokenUs(UtcUs(2026, 6, 11, 3), "America/Los_Angeles"),
              UtcUs(2026, 6, 10, 0));
    EXPECT_EQ(LocalDateTokenUs(UtcUs(2026, 6, 11, 3), "UTC"),
              UtcUs(2026, 6, 11, 0));
}

TEST(DateTimeUtilTest, LocalDateTokenUsIsATokenNotLocalMidnight) {
    // GetMidnightUs returns the INSTANT of local midnight (07:00 UTC in June);
    // the token is the timezone-free date. Confusing them reintroduces the
    // offset this whole encoding exists to keep out.
    const int64_t anyMoment = UtcUs(2026, 6, 10, 20);
    EXPECT_EQ(LocalDateTokenUs(anyMoment, "America/Los_Angeles"),
              UtcUs(2026, 6, 10, 0));
    EXPECT_EQ(GetMidnightUs(anyMoment, "America/Los_Angeles"),
              UtcUs(2026, 6, 10, 7));
}

TEST(DateTimeUtilTest, CalendarDateTokenRoundTrips) {
    // Any wall clock on a date resolves to an instant that reports that date.
    for (int minutes : { 0, 6 * 60, 10 * 60, 18 * 60, 23 * 60 + 59 }) {
        for (const char* zone :
             { "America/Los_Angeles", "America/New_York", "UTC" }) {
            const int64_t token = UtcUs(2026, 6, 10, 0);
            EXPECT_EQ(LocalDateTokenUs(
                          CalendarDateWallClockToUs(token, minutes, zone), zone),
                      token)
                << "minutes=" << minutes << " zone=" << zone;
        }
    }
}

// ── Timezone-aware formatting tests ──

TEST(DateTimeUtilTest, FormatTimeWithPacificTimezone) {
    // March 28, 2026, 18:10 UTC = 11:10 AM PDT (UTC-7)
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{28}}
        + std::chrono::hours{18} + std::chrono::minutes{10};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    std::string result = FormatTimeFromMicroseconds(us, "America/Los_Angeles");
    EXPECT_EQ(result, "11:10 AM");
}

TEST(DateTimeUtilTest, FormatTimeWithPacificTimezoneUtcCheck) {
    // Same timestamp in UTC should show 6:10 PM
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{28}}
        + std::chrono::hours{18} + std::chrono::minutes{10};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    std::string utcResult = FormatTimeFromMicroseconds(us);
    EXPECT_EQ(utcResult, "6:10 PM");
}

TEST(DateTimeUtilTest, FormatDateWithPacificTimezone) {
    // March 28, 2026, 6:00 UTC = March 27 at 11 PM PDT (still March 27 locally)
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{28}}
        + std::chrono::hours{6};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    std::string result = FormatDateFromMicroseconds(us, "America/Los_Angeles");
    EXPECT_NE(result.find("27"), std::string::npos);  // March 27 in Pacific
    EXPECT_NE(result.find("March"), std::string::npos);
}

TEST(DateTimeUtilTest, FormatTimeWithEmptyTimezoneFallsBackToUtc) {
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{28}}
        + std::chrono::hours{18} + std::chrono::minutes{10};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    std::string result = FormatTimeFromMicroseconds(us, "");
    EXPECT_EQ(result, "6:10 PM");  // Same as UTC
}

TEST(DateTimeUtilTest, FormatTimeWithEasternTimezone) {
    // March 28, 2026, 18:10 UTC = 2:10 PM EDT (UTC-4)
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{28}}
        + std::chrono::hours{18} + std::chrono::minutes{10};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();

    std::string result = FormatTimeFromMicroseconds(us, "America/New_York");
    EXPECT_EQ(result, "2:10 PM");
}

// ── GetDayOfWeek tests ──

TEST(DateTimeUtilTest, GetDayOfWeekMondayUtc) {
    // 2030-01-07 is a Monday
    auto tp = date::sys_days{date::year{2030}/date::month{1}/date::day{7}}
        + std::chrono::hours{12};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(GetDayOfWeek(us, "UTC"), 1);  // Monday
}

TEST(DateTimeUtilTest, GetDayOfWeekSundayUtc) {
    // 2030-01-06 is a Sunday
    auto tp = date::sys_days{date::year{2030}/date::month{1}/date::day{6}}
        + std::chrono::hours{12};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(GetDayOfWeek(us, "UTC"), 0);  // Sunday
}

TEST(DateTimeUtilTest, GetDayOfWeekSaturdayUtc) {
    // 2030-01-05 is a Saturday
    auto tp = date::sys_days{date::year{2030}/date::month{1}/date::day{5}}
        + std::chrono::hours{12};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(GetDayOfWeek(us, "UTC"), 6);  // Saturday
}

TEST(DateTimeUtilTest, GetDayOfWeekTimezoneShiftsDay) {
    // 2030-01-08 00:30 UTC = still Jan 7 (Monday) in Pacific (UTC-8)
    auto tp = date::sys_days{date::year{2030}/date::month{1}/date::day{8}}
        + std::chrono::minutes{30};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    EXPECT_EQ(GetDayOfWeek(us, "UTC"), 2);  // Tuesday in UTC
    EXPECT_EQ(GetDayOfWeek(us, "America/Los_Angeles"), 1);  // Monday in Pacific
}

// ── GetMidnightUs tests ──

TEST(DateTimeUtilTest, GetMidnightUsUtc) {
    // March 15, 2026 14:30 UTC → midnight March 15, 2026 UTC
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{15}}
        + std::chrono::hours{14} + std::chrono::minutes{30};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    int64_t midnight = GetMidnightUs(us, "UTC");

    int64_t expectedMidnight = std::chrono::duration_cast<std::chrono::microseconds>(
        date::sys_days{date::year{2026}/date::month{3}/date::day{15}}.time_since_epoch()).count();
    EXPECT_EQ(midnight, expectedMidnight);
}

TEST(DateTimeUtilTest, GetMidnightUsAlreadyMidnight) {
    // Exactly midnight → should return same value
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{15}};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    int64_t midnight = GetMidnightUs(us, "UTC");
    EXPECT_EQ(midnight, us);
}

TEST(DateTimeUtilTest, GetMidnightUsPacificTimezone) {
    // March 15, 2026 03:00 UTC = March 14 at 8pm PDT (UTC-7, DST active in March)
    // So midnight in Pacific = March 14 00:00 PDT = March 14 07:00 UTC
    auto tp = date::sys_days{date::year{2026}/date::month{3}/date::day{15}}
        + std::chrono::hours{3};
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        tp.time_since_epoch()).count();
    int64_t midnight = GetMidnightUs(us, "America/Los_Angeles");

    // March 14 midnight PDT = March 14 07:00 UTC
    int64_t expected = std::chrono::duration_cast<std::chrono::microseconds>(
        (date::sys_days{date::year{2026}/date::month{3}/date::day{14}}
         + std::chrono::hours{7}).time_since_epoch()).count();
    EXPECT_EQ(midnight, expected);
}

} // namespace {
}  // namespace DateTimeUtil
