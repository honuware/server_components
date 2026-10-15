#include "log_throttle.h"

#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

namespace Util {
namespace {

using std::chrono::minutes;
using std::chrono::seconds;
using TimePoint = std::chrono::steady_clock::time_point;

// A fixed starting point, so every test controls time exactly.
const TimePoint kStart = TimePoint{} + std::chrono::hours(1);

TEST(LogThrottleTest, TheFirstCallLogs) {
    LogThrottle throttle(minutes(1));
    int64_t suppressed = -1;
    EXPECT_TRUE(throttle.ShouldLog(kStart, &suppressed));
    EXPECT_EQ(suppressed, 0);
}

TEST(LogThrottleTest, CallsWithinTheIntervalAreHeldBack) {
    LogThrottle throttle(minutes(1));
    EXPECT_TRUE(throttle.ShouldLog(kStart));
    EXPECT_FALSE(throttle.ShouldLog(kStart + seconds(1)));
    EXPECT_FALSE(throttle.ShouldLog(kStart + seconds(59)));
}

TEST(LogThrottleTest, TheFirstCallAfterTheIntervalLogsAndCountsWhatWasHeldBack) {
    LogThrottle throttle(minutes(1));
    EXPECT_TRUE(throttle.ShouldLog(kStart));
    EXPECT_FALSE(throttle.ShouldLog(kStart + seconds(10)));
    EXPECT_FALSE(throttle.ShouldLog(kStart + seconds(20)));
    EXPECT_FALSE(throttle.ShouldLog(kStart + seconds(30)));

    int64_t suppressed = -1;
    EXPECT_TRUE(throttle.ShouldLog(kStart + minutes(1), &suppressed));
    EXPECT_EQ(suppressed, 3);

    // The count starts again, and so does the interval.
    EXPECT_FALSE(throttle.ShouldLog(kStart + minutes(1) + seconds(30)));
    EXPECT_TRUE(throttle.ShouldLog(kStart + minutes(2), &suppressed));
    EXPECT_EQ(suppressed, 1);
}

TEST(LogThrottleTest, TheNowOverloadLogsOnceThenHoldsBack) {
    LogThrottle throttle(minutes(10));
    EXPECT_TRUE(throttle.ShouldLog());
    EXPECT_FALSE(throttle.ShouldLog());
}

}  // namespace
}  // namespace Util
