#include "log_throttle.h"

namespace Util {

LogThrottle::LogThrottle(std::chrono::steady_clock::duration interval)
    : interval_(interval) {}

bool LogThrottle::ShouldLog(
    std::chrono::steady_clock::time_point now, int64_t* suppressedOut) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (lastLogged_.has_value() && now - *lastLogged_ < interval_) {
        ++suppressed_;
        return false;
    }
    lastLogged_ = now;
    if (suppressedOut != nullptr) {
        *suppressedOut = suppressed_;
    }
    suppressed_ = 0;
    return true;
}

bool LogThrottle::ShouldLog(int64_t* suppressedOut) {
    return ShouldLog(std::chrono::steady_clock::now(), suppressedOut);
}

}  // namespace Util
