#pragma once

#include <string>
#include <string_view>

namespace DateTimeUtil {

int64_t StringToEpochMillis(std::string_view dateString);

// Formats microseconds since epoch as "March 4, 2026" (UTC)
std::string FormatDateFromMicroseconds(int64_t microseconds);

// Formats microseconds since epoch as "March 4, 2026" in the given timezone
std::string FormatDateFromMicroseconds(int64_t microseconds, std::string_view timezone);

// Formats microseconds since epoch as "7:00 PM" (UTC)
std::string FormatTimeFromMicroseconds(int64_t microseconds);

// Formats microseconds since epoch as "7:00 PM" in the given timezone
std::string FormatTimeFromMicroseconds(int64_t microseconds, std::string_view timezone);

// Returns microsecond timestamp for the first moment of a given month in UTC.
int64_t StartOfMonthUs(int year, int month);

// Returns microsecond timestamp for the first moment of the NEXT month in UTC.
int64_t EndOfMonthUs(int year, int month);

// Given a timestamp, returns the start of its containing month.
int64_t StartOfContainingMonthUs(int64_t timestampUs);

// Given a timestamp, returns the start of the next month.
int64_t EndOfContainingMonthUs(int64_t timestampUs);

// Returns the day of week (0=Sunday, 6=Saturday) for a given microsecond timestamp
// in the specified timezone.
int GetDayOfWeek(int64_t timestampUs, std::string_view timezone);

// Returns the microsecond timestamp of midnight (start of day) for the given
// timestamp in the specified timezone.
int64_t GetMidnightUs(int64_t timestampUs, std::string_view timezone);

// The absolute timestamp of a WALL-CLOCK time on the local day containing
// `dayTimestampUs`. `minutesAfterMidnight` is local wall clock (600 = 10:00),
// matching how class_schedule_slots.start_time_minutes is stored.
//
// This is NOT `GetMidnightUs(...) + minutes` — that is wrong across a DST
// transition. On a spring-forward day the clock jumps 02:00 → 03:00, so
// wall-clock 10:00 is only NINE elapsed hours after midnight and the naive sum
// lands an hour late. mktime with tm_isdst = -1 resolves the offset from the
// zone's rules instead of assuming every day is 24 hours.
int64_t LocalWallClockToUs(
    int64_t dayTimestampUs, int minutesAfterMidnight, std::string_view timezone);

// A CALENDAR DATE TOKEN is UTC midnight of a date, standing for the date
// itself rather than an instant — the encoding `event_sessions
// .occurrence_date_us` uses. These two convert between a token and a real
// instant; together they are what lets a recurring occurrence be stored as
// (date, wall-clock minutes) and still be served as a true UTC instant.

// The absolute instant of a wall-clock time on the calendar date `dateTokenUs`
// stands for, resolved in `timezone`.
//
// NOT interchangeable with LocalWallClockToUs, which derives the local date
// FROM the instant it is given: hand it a UTC-midnight token and any
// negative-offset zone resolves it to the PREVIOUS day (UTC midnight Wednesday
// is Tuesday 17:00 in Los Angeles). This one reads the token's UTC Y/M/D and
// treats those numbers as the local date, which is what a token means.
//
// `minutesAfterMidnight` may exceed 1440; it normalises into the next day.
int64_t CalendarDateWallClockToUs(
    int64_t dateTokenUs, int minutesAfterMidnight, std::string_view timezone);

// The inverse: the calendar date token for the date on which `timestampUs`
// falls in `timezone`. Note this is NOT GetMidnightUs — that returns the
// INSTANT of local midnight, which is offset from UTC midnight; this returns
// the timezone-free token for the same date.
int64_t LocalDateTokenUs(int64_t timestampUs, std::string_view timezone);

}