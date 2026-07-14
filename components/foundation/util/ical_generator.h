#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ICalGenerator {

// Deployment/brand identity for the generated calendar, supplied by the caller
// so this generator carries no app-specific literals (componentization Phase
// 1.1 — this file is bound for the brand-free honuware_foundation component).
//   - `prodId`    : the RFC 5545 PRODID value, e.g. "-//Acme//Booking//EN".
//   - `uidDomain` : the right-hand side of every generated UID ("id@domain"),
//                   e.g. "acme.example"; also names the synthetic-UID fallback.
// Knotty Yoga's values are supplied app-side (see business_logic/app_ical_config.h).
struct ICalConfig {
    std::string prodId;
    std::string uidDomain;
};

// Classes Phase 4: additive RFC 5545 extensions. All new fields are optional
// and default to "off" (empty string / "0" sequence), so pre-Phase-4 call
// sites that set only the original fields emit substantially the same output
// (plus the now-mandatory UID/DTSTAMP/SEQUENCE lines).
struct ICalEvent {
    std::string title;
    int64_t startTimeUs = 0;   // Microseconds since epoch (UTC)
    int64_t endTimeUs = 0;     // Microseconds since epoch (UTC)
    std::string timezone;      // IANA timezone (e.g. "America/Los_Angeles")
    std::string location;
    std::string description;

    // RFC 5545 UID. Set via the Build*Uid helpers below; an empty uid falls
    // back to a synthetic value at generation time (never blocks an email).
    std::string uid;
    // "" (omit), "CONFIRMED", or "CANCELLED" (emits STATUS:CANCELLED, which
    // auto-removes the entry from the user's calendar).
    std::string status;
    // Verbatim RRULE value (without the "RRULE:" prefix), e.g.
    // "FREQ=WEEKLY;BYDAY=TU;UNTIL=20260101T000000Z". Build with the helpers.
    std::string rrule;
    std::string organizerEmail;
    std::string organizerName;
    std::string attendeeEmail;
    std::string attendeeName;
    // iCal SEQUENCE; bumped on each update so calendar apps accept the latest.
    std::string sequence = "0";
    // When true, DTSTART/DTEND are emitted as floating local times (no "Z" /
    // no TZID) — the calendar shows the wall-clock value in the viewer's own
    // zone. Used for class/series occurrences whose stored time is
    // wall-clock-as-UTC rather than a true instant. Default false → UTC ("Z").
    bool floatingLocal = false;
};

// Generate an RFC 5545 .ics for a single event. DTSTART/DTEND are emitted in
// UTC (Z suffix); a recurring event additionally carries an RRULE. (Full
// DST-aware VTIMEZONE/TZID emission is the documented §3.3 follow-up; the UTC
// form is the §3.3 fallback and is correct, just not DST-shifting.) `config`
// supplies the PRODID and the synthetic-UID domain.
std::string GenerateICalendar(const ICalEvent& event, const ICalConfig& config);

// Multi-event overload (Phase 6 weekly digest): one VCALENDAR wrapping a
// VEVENT per event. The digest uses per-occurrence VEVENTs (each a concrete
// UTC instant), so no VTIMEZONE is needed.
std::string GenerateICalendar(
    const std::vector<ICalEvent>& events, const ICalConfig& config);

// ---- UID / RRULE builders (§4) — centralize the conventions ----
//
// `uidDomain` is the RHS of the "id@domain" UID (no leading "@"); supplied by
// the caller so no brand literal lives here (componentization Phase 1.1).

std::string BuildBookingUid(int64_t bookingId, std::string_view uidDomain);
std::string BuildSessionUid(int64_t sessionId, std::string_view uidDomain);
std::string BuildTemplateUid(
    int64_t scheduleId, int64_t personId, std::string_view uidDomain);

// Per-occurrence UID for a booking-less templated occurrence (Phase 6 weekly
// digest / personal feed). Slot-keyed per the schedule redesign: each concrete
// occurrence is its own VEVENT, so the UID must be unique per (slot, person,
// occurrence-date). Distinct from BuildTemplateUid, which is schedule-keyed and
// names the single recurring-RRULE confirmation invite.
std::string BuildTemplateOccurrenceUid(
    int64_t classScheduleSlotId, int64_t personId, int64_t occurrenceDateUs,
    std::string_view uidDomain);

// daysOfWeek: 0..6 -> SU,MO,TU,WE,TH,FR,SA. untilUs is microseconds-since-epoch
// (UTC); emitted as UNTIL=<YYYYMMDDTHHMMSSZ>. untilUs <= 0 omits UNTIL.
std::string BuildWeeklyRRule(const std::vector<int>& daysOfWeek, int64_t untilUs);
std::string BuildBiweeklyRRule(const std::vector<int>& daysOfWeek, int64_t untilUs);
std::string BuildCustomRRule(int intervalDays, int64_t untilUs);

// Folds one logical line per RFC 5545 §3.1 (75-octet limit, CRLF + space
// continuation, UTF-8-safe). Exposed for testing.
std::string FoldLine(std::string_view line);

}  // namespace ICalGenerator
