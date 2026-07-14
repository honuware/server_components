#include "date_time_util.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <charconv>

#include "date/date.h"

namespace DateTimeUtil {

// This function is ChatGPT generated to parse ISO 8601 timestamps 
// like what PostgreSQL produces for timestamptz values. It's only
// used in tests, so I'm not worried about maintaining such
// elaborate code. 
// TODO(mason) - revisit this and replace with C++20's date time
// parsing when available.
int64_t StringToEpochMillis(std::string_view s) {
    // Expected base: "YYYY-MM-DD HH:MM:SS"
    // Index map:      0123456789012345678
    if (s.size() < 19) {
        throw std::runtime_error("timestamp too short: " + std::string(s));
    }
    auto expect = [&](std::size_t pos, char ch) {
        if (pos >= s.size() || s[pos] != ch) {
            throw std::runtime_error("bad format at pos " + std::to_string(pos) + ": " + std::string(s));
        }
        };

    expect(4, '-'); expect(7, '-'); expect(10, ' '); expect(13, ':'); expect(16, ':');

    auto to_int = [&](std::size_t pos, std::size_t len) -> int {
        int v = 0;
        auto res = std::from_chars(s.data() + pos, s.data() + pos + len, v);
        if (res.ec != std::errc()) {
            throw std::runtime_error("bad number at pos " + std::to_string(pos) + ": " + std::string(s));
        }
        return v;
        };

    const int year = to_int(0, 4);
    const int month = to_int(5, 2);
    const int day = to_int(8, 2);
    const int hour = to_int(11, 2);
    const int min = to_int(14, 2);
    const int sec = to_int(17, 2);

    // Fractional seconds (microseconds: 0..6 digits)
    std::size_t pos = 19;
    int micros = 0;
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        std::size_t start = pos;
        std::size_t digits = 0;
        int micro_val = 0;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])) && digits < 6) {
            micro_val = micro_val * 10 + (s[pos] - '0');
            ++pos; ++digits;
        }
        // If input had fewer than 6 digits, scale to microseconds
        for (; digits < 6; ++digits) micro_val *= 10;
        micros = micro_val;

        // Skip any extra fractional digits beyond 6 (truncation)
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }

    // Time zone: 'Z' or �HH[:MM] or �HHMM or �HH
    int tz_minutes = 0; // minutes to *subtract* to get UTC
    if (pos >= s.size()) {
        throw std::runtime_error("missing time zone offset: " + std::string(s));
    }

    if (s[pos] == 'Z') {
        ++pos; // UTC
    }
    else {
        if (s[pos] != '+' && s[pos] != '-') {
            throw std::runtime_error("expected 'Z' or sign at pos " + std::to_string(pos) + ": " + std::string(s));
        }
        const int sign = (s[pos] == '-') ? -1 : 1;
        ++pos;

        // Parse HH
        if (pos + 1 >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])) || !std::isdigit(static_cast<unsigned char>(s[pos + 1]))) {
            throw std::runtime_error("bad tz hour at pos " + std::to_string(pos) + ": " + std::string(s));
        }
        int off_h = (s[pos] - '0') * 10 + (s[pos + 1] - '0');
        pos += 2;

        int off_m = 0;
        if (pos < s.size() && s[pos] == ':') {
            // �HH:MM
            ++pos;
            if (pos + 1 >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos])) || !std::isdigit(static_cast<unsigned char>(s[pos + 1]))) {
                throw std::runtime_error("bad tz minute at pos " + std::to_string(pos) + ": " + std::string(s));
            }
            off_m = (s[pos] - '0') * 10 + (s[pos + 1] - '0');
            pos += 2;
        }
        else if (pos + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])) && std::isdigit(static_cast<unsigned char>(s[pos + 1]))) {
            // �HHMM
            off_m = (s[pos] - '0') * 10 + (s[pos + 1] - '0');
            pos += 2;
        }
        else {
            // �HH (no minutes)
            off_m = 0;
        }

        tz_minutes = sign * (off_h * 60 + off_m);
    }

    // There should be no junk after the offset (allow trailing whitespace)
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    if (pos != s.size()) {
        throw std::runtime_error("trailing characters after timestamp: " + std::string(s.substr(pos)));
    }

    // Build time_point in UTC (subtract offset to get UTC)
    date::year y{ year };
    date::month m{ static_cast<unsigned>(month) };
    date::day d{ static_cast<unsigned>(day) };
    if (!y.ok() || !m.ok() || !d.ok()) {
        throw std::runtime_error("invalid Y-M-D: " + std::string(s));
    }
    date::sys_days days = date::sys_days{ y / m / d };
    date::sys_time<std::chrono::microseconds> tp = days
        + std::chrono::hours{ hour }
        + std::chrono::minutes{ min }
        + std::chrono::seconds{ sec }
    + std::chrono::microseconds{ micros };

    // Adjust for numeric offset to land on UTC
    tp -= std::chrono::minutes{ tz_minutes };

    // Return milliseconds since epoch (truncate toward zero)
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

namespace {

constexpr const char* kMonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

}  // namespace

std::string FormatDateFromMicroseconds(int64_t microseconds) {
    auto tp = date::sys_time<std::chrono::microseconds>(
        std::chrono::microseconds{microseconds});
    auto days = date::floor<date::days>(tp);
    auto ymd = date::year_month_day{days};

    int month = static_cast<unsigned>(ymd.month());
    int day = static_cast<unsigned>(ymd.day());
    int year = static_cast<int>(ymd.year());

    std::ostringstream oss;
    oss << kMonthNames[month - 1] << " " << day << ", " << year;
    return oss.str();
}

std::string FormatTimeFromMicroseconds(int64_t microseconds) {
    auto tp = date::sys_time<std::chrono::microseconds>(
        std::chrono::microseconds{microseconds});
    auto days = date::floor<date::days>(tp);
    auto time_of_day = date::make_time(tp - days);

    int hours = time_of_day.hours().count();
    int minutes = time_of_day.minutes().count();

    std::string ampm = "AM";
    if (hours >= 12) {
        ampm = "PM";
        if (hours > 12) hours -= 12;
    }
    if (hours == 0) hours = 12;

    std::ostringstream oss;
    oss << hours << ":" << std::setfill('0') << std::setw(2) << minutes << " " << ampm;
    return oss.str();
}

// Mutex for thread-safe TZ environment variable manipulation
static std::mutex tzMutex;

// Map IANA timezone names to POSIX TZ strings (for _putenv_s on Windows)
static std::string IanaToPosixTz(std::string_view iana) {
    // Common US timezones
    if (iana == "America/Los_Angeles") return "PST8PDT,M3.2.0,M11.1.0";
    if (iana == "America/Denver") return "MST7MDT,M3.2.0,M11.1.0";
    if (iana == "America/Chicago") return "CST6CDT,M3.2.0,M11.1.0";
    if (iana == "America/New_York") return "EST5EDT,M3.2.0,M11.1.0";
    if (iana == "America/Phoenix") return "MST7";  // No DST
    if (iana == "America/Anchorage") return "AKST9AKDT,M3.2.0,M11.1.0";
    if (iana == "Pacific/Honolulu") return "HST10";  // No DST
    if (iana == "US/Pacific") return "PST8PDT,M3.2.0,M11.1.0";
    if (iana == "US/Mountain") return "MST7MDT,M3.2.0,M11.1.0";
    if (iana == "US/Central") return "CST6CDT,M3.2.0,M11.1.0";
    if (iana == "US/Eastern") return "EST5EDT,M3.2.0,M11.1.0";
    if (iana == "UTC" || iana == "Etc/UTC") return "UTC0";
    // Fallback: try the IANA name directly (works on Linux, may fail on Windows)
    return std::string(iana);
}

// Convert UTC microseconds to local struct tm using the TZ environment variable.
// Must be called under tzMutex lock.
static struct tm ToLocalTm(int64_t microseconds, std::string_view timezone) {
    time_t epochSeconds = static_cast<time_t>(microseconds / 1000000LL);
    struct tm localTm{};
    std::string posixTz = IanaToPosixTz(timezone);

#ifdef _WIN32
    _putenv_s("TZ", posixTz.c_str());
    _tzset();
    localtime_s(&localTm, &epochSeconds);
    _putenv_s("TZ", "");
    _tzset();
#else
    std::string oldTz;
    const char* prevTz = std::getenv("TZ");
    if (prevTz) oldTz = prevTz;
    setenv("TZ", posixTz.c_str(), 1);
    tzset();
    localtime_r(&epochSeconds, &localTm);
    if (prevTz) setenv("TZ", oldTz.c_str(), 1);
    else unsetenv("TZ");
    tzset();
#endif
    return localTm;
}

std::string FormatDateFromMicroseconds(int64_t microseconds, std::string_view timezone) {
    if (timezone.empty()) return FormatDateFromMicroseconds(microseconds);

    std::lock_guard<std::mutex> lock(tzMutex);
    struct tm localTm = ToLocalTm(microseconds, timezone);

    int month = localTm.tm_mon + 1;  // tm_mon is 0-based
    int day = localTm.tm_mday;
    int year = localTm.tm_year + 1900;

    std::ostringstream oss;
    oss << kMonthNames[month - 1] << " " << day << ", " << year;
    return oss.str();
}

std::string FormatTimeFromMicroseconds(int64_t microseconds, std::string_view timezone) {
    if (timezone.empty()) return FormatTimeFromMicroseconds(microseconds);

    std::lock_guard<std::mutex> lock(tzMutex);
    struct tm localTm = ToLocalTm(microseconds, timezone);

    int hours = localTm.tm_hour;
    int minutes = localTm.tm_min;

    std::string ampm = "AM";
    if (hours >= 12) {
        ampm = "PM";
        if (hours > 12) hours -= 12;
    }
    if (hours == 0) hours = 12;

    std::ostringstream oss;
    oss << hours << ":" << std::setfill('0') << std::setw(2) << minutes << " " << ampm;
    return oss.str();
}

int64_t StartOfMonthUs(int year, int month) {
    date::sys_days days = date::sys_days{
        date::year{year} / date::month{static_cast<unsigned>(month)} / date::day{1}};
    auto tp = date::sys_time<std::chrono::microseconds>(
        std::chrono::duration_cast<std::chrono::microseconds>(days.time_since_epoch()));
    return tp.time_since_epoch().count();
}

int64_t EndOfMonthUs(int year, int month) {
    // First moment of the next month
    if (month == 12) {
        return StartOfMonthUs(year + 1, 1);
    }
    return StartOfMonthUs(year, month + 1);
}

int64_t StartOfContainingMonthUs(int64_t timestampUs) {
    auto tp = date::sys_time<std::chrono::microseconds>(
        std::chrono::microseconds{timestampUs});
    auto days = date::floor<date::days>(tp);
    auto ymd = date::year_month_day{days};
    return StartOfMonthUs(static_cast<int>(ymd.year()),
                          static_cast<unsigned>(ymd.month()));
}

int64_t EndOfContainingMonthUs(int64_t timestampUs) {
    auto tp = date::sys_time<std::chrono::microseconds>(
        std::chrono::microseconds{timestampUs});
    auto days = date::floor<date::days>(tp);
    auto ymd = date::year_month_day{days};
    return EndOfMonthUs(static_cast<int>(ymd.year()),
                        static_cast<unsigned>(ymd.month()));
}

int GetDayOfWeek(int64_t timestampUs, std::string_view timezone) {
    std::lock_guard<std::mutex> lock(tzMutex);
    struct tm localTm = ToLocalTm(timestampUs, timezone);
    return localTm.tm_wday;  // 0=Sunday, 6=Saturday
}

int64_t GetMidnightUs(int64_t timestampUs, std::string_view timezone) {
    std::lock_guard<std::mutex> lock(tzMutex);
    struct tm localTm = ToLocalTm(timestampUs, timezone);
    // Zero out the time components
    localTm.tm_hour = 0;
    localTm.tm_min = 0;
    localTm.tm_sec = 0;

    // Convert back to epoch. We need to use the timezone again.
    std::string posixTz = IanaToPosixTz(timezone);
#ifdef _WIN32
    _putenv_s("TZ", posixTz.c_str());
    _tzset();
    time_t midnightEpoch = mktime(&localTm);
    _putenv_s("TZ", "");
    _tzset();
#else
    std::string oldTz;
    const char* prevTz = std::getenv("TZ");
    if (prevTz) oldTz = prevTz;
    setenv("TZ", posixTz.c_str(), 1);
    tzset();
    time_t midnightEpoch = mktime(&localTm);
    if (prevTz) setenv("TZ", oldTz.c_str(), 1);
    else unsetenv("TZ");
    tzset();
#endif

    return static_cast<int64_t>(midnightEpoch) * 1000000LL;
}

}  // namespace DateTimeUtil