#include "logging.h"

#include <crow/logging.h>

#include <gtest/gtest.h>

namespace {

TEST(LoggingResolveDestinationTest, NullDefaultsToStdout) {
    LogDestinationConfig cfg = ResolveLogDestination(nullptr);
    EXPECT_EQ(cfg.dest, LogDestination::Stdout);
    EXPECT_TRUE(cfg.filePath.empty());
}

TEST(LoggingResolveDestinationTest, EmptyStringDefaultsToStdout) {
    LogDestinationConfig cfg = ResolveLogDestination("");
    EXPECT_EQ(cfg.dest, LogDestination::Stdout);
    EXPECT_TRUE(cfg.filePath.empty());
}

TEST(LoggingResolveDestinationTest, StdoutLiteral) {
    LogDestinationConfig cfg = ResolveLogDestination("stdout");
    EXPECT_EQ(cfg.dest, LogDestination::Stdout);
    EXPECT_TRUE(cfg.filePath.empty());
}

TEST(LoggingResolveDestinationTest, StderrLiteral) {
    LogDestinationConfig cfg = ResolveLogDestination("stderr");
    EXPECT_EQ(cfg.dest, LogDestination::Stderr);
    EXPECT_TRUE(cfg.filePath.empty());
}

TEST(LoggingResolveDestinationTest, AbsoluteFilePath) {
    LogDestinationConfig cfg = ResolveLogDestination(
        "/var/log/knottyyoga/server.log");
    EXPECT_EQ(cfg.dest, LogDestination::File);
    EXPECT_EQ(cfg.filePath, "/var/log/knottyyoga/server.log");
}

TEST(LoggingResolveDestinationTest, RelativeFilePath) {
    LogDestinationConfig cfg = ResolveLogDestination("server.log");
    EXPECT_EQ(cfg.dest, LogDestination::File);
    EXPECT_EQ(cfg.filePath, "server.log");
}

TEST(LoggingResolveDestinationTest, WindowsStyleFilePath) {
    LogDestinationConfig cfg = ResolveLogDestination(
        "C:\\ProgramData\\knottyyoga\\server.log");
    EXPECT_EQ(cfg.dest, LogDestination::File);
    EXPECT_EQ(cfg.filePath, "C:\\ProgramData\\knottyyoga\\server.log");
}

// "stdout"/"stderr" are case-sensitive — anything else is treated as a path.
// Documented here so a future change that adds case-insensitive matching is
// a deliberate decision, not an accidental one.
TEST(LoggingResolveDestinationTest, MixedCaseTreatedAsPath) {
    LogDestinationConfig cfg = ResolveLogDestination("Stdout");
    EXPECT_EQ(cfg.dest, LogDestination::File);
    EXPECT_EQ(cfg.filePath, "Stdout");
}

// --- CrowLogLevelLabel ---
//
// The bridge handler reuses Crow's exact 8-char level prefixes so log lines
// are indistinguishable from the upstream CerrLogHandler format.

TEST(CrowLogLevelLabelTest, AllLevelsHaveFixedWidthLabel) {
    EXPECT_EQ(CrowLogLevelLabel(static_cast<int>(crow::LogLevel::Debug)),
              "DEBUG   ");
    EXPECT_EQ(CrowLogLevelLabel(static_cast<int>(crow::LogLevel::Info)),
              "INFO    ");
    EXPECT_EQ(CrowLogLevelLabel(static_cast<int>(crow::LogLevel::Warning)),
              "WARNING ");
    EXPECT_EQ(CrowLogLevelLabel(static_cast<int>(crow::LogLevel::Error)),
              "ERROR   ");
    EXPECT_EQ(CrowLogLevelLabel(static_cast<int>(crow::LogLevel::Critical)),
              "CRITICAL");
}

TEST(CrowLogLevelLabelTest, OutOfRangeValueProducesUnknown) {
    EXPECT_EQ(CrowLogLevelLabel(99), "UNKNOWN ");
}

TEST(CrowLogLevelLabelTest, AllLabelsAreEightCharsForAlignment) {
    // Operators grep / table-align on the prefix, so guarantee 8-char width.
    for (int lvl = 0; lvl < 5; ++lvl) {
        EXPECT_EQ(CrowLogLevelLabel(lvl).size(), 8u)
            << "Level " << lvl << " label is not 8 chars";
    }
}

}  // namespace
