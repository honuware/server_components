#include "ical_generator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "date/date.h"

#include "util/logging.h"

namespace ICalGenerator {

namespace {

// Format microseconds since epoch as iCal UTC datetime: "YYYYMMDDTHHMMSSZ"
std::string FormatUtcDateTime(int64_t microseconds) {
    auto tp = date::sys_time<std::chrono::microseconds>(
        std::chrono::microseconds{microseconds});
    auto days = date::floor<date::days>(tp);
    auto ymd = date::year_month_day{days};
    auto time_of_day = date::make_time(tp - days);

    int year = static_cast<int>(ymd.year());
    unsigned month = static_cast<unsigned>(ymd.month());
    unsigned day = static_cast<unsigned>(ymd.day());
    int hours = static_cast<int>(time_of_day.hours().count());
    int minutes = static_cast<int>(time_of_day.minutes().count());
    int seconds = static_cast<int>(time_of_day.seconds().count());

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << year
        << std::setw(2) << month
        << std::setw(2) << day
        << "T"
        << std::setw(2) << hours
        << std::setw(2) << minutes
        << std::setw(2) << seconds
        << "Z";
    return oss.str();
}

// Floating local datetime ("YYYYMMDDTHHMMSS", no "Z"): the same wall-clock
// components as the UTC form but without a zone, so calendars interpret it in
// the viewer's local zone. Used for wall-clock-as-UTC class occurrences.
std::string FormatFloatingDateTime(int64_t microseconds) {
    std::string utc = FormatUtcDateTime(microseconds);
    if (!utc.empty() && utc.back() == 'Z') {
        utc.pop_back();
    }
    return utc;
}

// Current UTC time as an iCal datetime — used for the mandatory DTSTAMP.
std::string NowUtcDateTime() {
    auto now = std::chrono::system_clock::now();
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    return FormatUtcDateTime(us);
}

// Escape text for iCal: backslash-escape commas, semicolons, and newlines.
std::string EscapeICalText(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '\\': result += "\\\\"; break;
            case ',': result += "\\,"; break;
            case ';': result += "\\;"; break;
            case '\n': result += "\\n"; break;
            case '\r': break;  // Drop CR
            default: result += c; break;
        }
    }
    return result;
}

// OQ-P4-3: a unique-enough synthetic UID for the empty-uid fallback. Never
// blocks an email; logs so unset UIDs are caught in dev. (Time + an atomic
// per-process counter is sufficient uniqueness for a rarely-hit safety net.)
// `uidDomain` is supplied by the caller so no brand literal lives here.
std::string SyntheticUid(std::string_view uidDomain) {
    static std::atomic<uint64_t> counter{0};
    auto now = std::chrono::system_clock::now();
    int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    std::ostringstream oss;
    oss << "synthetic-" << us << "-" << counter.fetch_add(1)
        << "@" << uidDomain;
    return oss.str();
}

std::string ResolveUid(const std::string& uid, std::string_view uidDomain) {
    if (!uid.empty()) {
        return uid;
    }
    std::string synthetic = SyntheticUid(uidDomain);
    LogWarning() << "ICalGenerator: event has no UID; using synthetic "
                 << synthetic << std::endl;
    return synthetic;
}

// Emit one BEGIN:VEVENT ... END:VEVENT block (shared by the single- and
// multi-event paths). Lines are emitted unfolded; FoldAll() folds the whole
// document afterward. `uidDomain` names the synthetic-UID fallback.
void EmitVEvent(const ICalEvent& event, std::ostream& oss,
                std::string_view uidDomain) {
    oss << "BEGIN:VEVENT\r\n"
        << "UID:" << ResolveUid(event.uid, uidDomain) << "\r\n"
        << "DTSTAMP:" << NowUtcDateTime() << "\r\n"
        << "SEQUENCE:" << (event.sequence.empty() ? "0" : event.sequence) << "\r\n";

    if (!event.status.empty()) {
        oss << "STATUS:" << event.status << "\r\n";
    }

    if (event.floatingLocal) {
        oss << "DTSTART:" << FormatFloatingDateTime(event.startTimeUs) << "\r\n"
            << "DTEND:" << FormatFloatingDateTime(event.endTimeUs) << "\r\n";
    } else {
        oss << "DTSTART:" << FormatUtcDateTime(event.startTimeUs) << "\r\n"
            << "DTEND:" << FormatUtcDateTime(event.endTimeUs) << "\r\n";
    }
    oss << "SUMMARY:" << EscapeICalText(event.title) << "\r\n";

    if (!event.location.empty()) {
        oss << "LOCATION:" << EscapeICalText(event.location) << "\r\n";
    }
    if (!event.description.empty()) {
        oss << "DESCRIPTION:" << EscapeICalText(event.description) << "\r\n";
    }
    if (!event.rrule.empty()) {
        oss << "RRULE:" << event.rrule << "\r\n";
    }
    if (!event.organizerEmail.empty()) {
        oss << "ORGANIZER;CN=" << EscapeICalText(event.organizerName)
            << ":mailto:" << event.organizerEmail << "\r\n";
    }
    if (!event.attendeeEmail.empty()) {
        oss << "ATTENDEE;CN=" << EscapeICalText(event.attendeeName)
            << ";RSVP=FALSE:mailto:" << event.attendeeEmail << "\r\n";
    }

    oss << "END:VEVENT\r\n";
}

// Wrap the VEVENT blocks in a VCALENDAR, then fold long lines (§3.4).
std::string GenerateImpl(
    const std::vector<ICalEvent>& events, const ICalConfig& config) {
    std::ostringstream oss;
    oss << "BEGIN:VCALENDAR\r\n"
        << "VERSION:2.0\r\n"
        << "PRODID:" << config.prodId << "\r\n"
        << "CALSCALE:GREGORIAN\r\n"
        << "METHOD:PUBLISH\r\n";
    for (const auto& event : events) {
        EmitVEvent(event, oss, config.uidDomain);
    }
    oss << "END:VCALENDAR\r\n";

    // Fold each CRLF-terminated logical line to <= 75 octets (RFC 5545 §3.1).
    std::string raw = oss.str();
    std::string folded;
    folded.reserve(raw.size() + raw.size() / 60);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t eol = raw.find("\r\n", pos);
        std::string_view line = (eol == std::string::npos)
            ? std::string_view(raw).substr(pos)
            : std::string_view(raw).substr(pos, eol - pos);
        folded += FoldLine(line);
        folded += "\r\n";
        if (eol == std::string::npos) break;
        pos = eol + 2;
    }
    return folded;
}

}  // namespace

std::string GenerateICalendar(const ICalEvent& event, const ICalConfig& config) {
    return GenerateImpl({event}, config);
}

std::string GenerateICalendar(
    const std::vector<ICalEvent>& events, const ICalConfig& config) {
    return GenerateImpl(events, config);
}

// ---- RFC 5545 §3.1 line folding (UTF-8 safe) ----

std::string FoldLine(std::string_view line) {
    constexpr size_t kMaxOctets = 75;
    if (line.size() <= kMaxOctets) {
        return std::string(line);
    }
    std::string result;
    result.reserve(line.size() + line.size() / kMaxOctets + 1);

    size_t pos = 0;
    bool firstSegment = true;
    while (pos < line.size()) {
        // Continuation lines start with a single space, which counts toward
        // the 75-octet budget.
        size_t budget = firstSegment ? kMaxOctets : (kMaxOctets - 1);
        size_t remaining = line.size() - pos;
        size_t take = std::min(budget, remaining);

        // Don't split inside a UTF-8 multi-byte sequence: if the byte at the
        // split point is a continuation byte (10xxxxxx), back off to the start
        // of that codepoint.
        if (take < remaining) {
            while (take > 0 &&
                   (static_cast<unsigned char>(line[pos + take]) & 0xC0) == 0x80) {
                --take;
            }
            if (take == 0) {
                take = std::min(budget, remaining);  // pathological; avoid stall
            }
        }

        if (!firstSegment) {
            result += "\r\n ";
        }
        result.append(line.substr(pos, take));
        pos += take;
        firstSegment = false;
    }
    return result;
}

// ---- UID / RRULE builders (§4) ----

std::string BuildBookingUid(int64_t bookingId, std::string_view uidDomain) {
    return "booking-" + std::to_string(bookingId) + "@" + std::string(uidDomain);
}

std::string BuildSessionUid(int64_t sessionId, std::string_view uidDomain) {
    return "session-" + std::to_string(sessionId) + "@" + std::string(uidDomain);
}

std::string BuildTemplateUid(
    int64_t scheduleId, int64_t personId, std::string_view uidDomain) {
    return "schedule-" + std::to_string(scheduleId) + "-person-"
        + std::to_string(personId) + "@" + std::string(uidDomain);
}

std::string BuildTemplateOccurrenceUid(
    int64_t classScheduleSlotId, int64_t personId, int64_t occurrenceDateUs,
    std::string_view uidDomain) {
    return "slot-" + std::to_string(classScheduleSlotId) + "-person-"
        + std::to_string(personId) + "-occ-" + std::to_string(occurrenceDateUs)
        + "@" + std::string(uidDomain);
}

namespace {

// "MO,TU,..." from day indices 0..6 (SU..SA), de-duped in input order.
std::string JoinByDay(const std::vector<int>& daysOfWeek) {
    static const char* kDays[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    std::string out;
    for (int d : daysOfWeek) {
        if (d < 0 || d > 6) {
            continue;
        }
        if (!out.empty()) {
            out += ",";
        }
        out += kDays[d];
    }
    return out;
}

std::string UntilClause(int64_t untilUs) {
    if (untilUs <= 0) {
        return "";
    }
    return ";UNTIL=" + FormatUtcDateTime(untilUs);
}

}  // namespace

std::string BuildWeeklyRRule(
    const std::vector<int>& daysOfWeek, int64_t untilUs) {
    return "FREQ=WEEKLY;BYDAY=" + JoinByDay(daysOfWeek) + UntilClause(untilUs);
}

std::string BuildBiweeklyRRule(
    const std::vector<int>& daysOfWeek, int64_t untilUs) {
    return "FREQ=WEEKLY;INTERVAL=2;BYDAY=" + JoinByDay(daysOfWeek)
        + UntilClause(untilUs);
}

std::string BuildCustomRRule(int intervalDays, int64_t untilUs) {
    return "FREQ=DAILY;INTERVAL=" + std::to_string(intervalDays)
        + UntilClause(untilUs);
}

}  // namespace ICalGenerator
