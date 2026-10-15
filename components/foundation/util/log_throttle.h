#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>

namespace Util {

// Lets a repeating log line through at most once per interval, and counts the
// calls it held back. For errors that, once they start, recur on EVERY request
// (a database outage): one line a minute says it is still happening without
// burying everything else in the journal. Thread-safe.
class LogThrottle {
public:
    explicit LogThrottle(std::chrono::steady_clock::duration interval);
    LogThrottle(const LogThrottle&) = delete;
    LogThrottle& operator=(const LogThrottle&) = delete;

    // True if a line may be written at `now`: the first call, or the first
    // call a full interval after the last line written. When it returns true,
    // `suppressedOut` (if given) receives how many calls were held back since
    // that last line, and the count starts again.
    bool ShouldLog(std::chrono::steady_clock::time_point now,
                   int64_t* suppressedOut = nullptr);

    // The same, at steady_clock::now().
    bool ShouldLog(int64_t* suppressedOut = nullptr);

private:
    const std::chrono::steady_clock::duration interval_;
    std::mutex mutex_;
    std::optional<std::chrono::steady_clock::time_point> lastLogged_;
    int64_t suppressed_ = 0;
};

}  // namespace Util
