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
