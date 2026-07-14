#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <ostream>
#include <string>

// Where the LogXxx() streams write. Resolved from HONUWARE_LOG_DEST (legacy
// fallback: KNOTTYYOGA_LOG_DEST) at
// startup by InitializeLogging(); defaults to Stdout so log output is
// captured by the systemd journal (and from there by CloudWatch Logs)
// without any extra configuration on the EC2.
enum class LogDestination {
    Stdout,
    Stderr,
    File,
};

struct LogDestinationConfig {
    LogDestination dest = LogDestination::Stdout;
    std::string filePath;  // populated only when dest == File
};

// Pure helper. Parses a HONUWARE_LOG_DEST value into a config.
//   nullptr / empty / "stdout" -> Stdout
//   "stderr"                   -> Stderr
//   anything else              -> File with that string as filePath
LogDestinationConfig ResolveLogDestination(const char* envValue);

// Reads HONUWARE_LOG_DEST (legacy fallback: KNOTTYYOGA_LOG_DEST) and
// configures the LogXxx() streams. Forces
// the chosen stream to line-buffered so each LogInfo() "...\n" is
// immediately visible to the systemd journal / CloudWatch Logs agent
// (default block-buffered behavior on a piped stdout would otherwise hide
// log lines until the buffer flushed). Idempotent; call once at the very
// start of `main()` before any LogXxx() output. Returns the resolved config.
//
// Also installs a custom Crow log handler so CROW_LOG_INFO / ERROR / etc.
// emit to the same destination — without this, Crow's default
// CerrLogHandler keeps writing to std::cerr regardless of what we do here,
// and operators end up with two log streams to chase.
LogDestinationConfig InitializeLogging();

// Returns the fixed-width label used in Crow log output (e.g. "INFO    ",
// "ERROR   "). Exposed for unit testing of the bridge formatting.
std::string CrowLogLevelLabel(int crowLogLevel);

std::ostream& LogError();
std::ostream& LogInfo();
std::ostream& LogWarning();

#endif  // __LOGGING_H__
