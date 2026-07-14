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

}