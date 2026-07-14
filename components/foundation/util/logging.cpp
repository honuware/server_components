#include "logging.h"

#include <crow/logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "util/env.h"

namespace {

// All three LogXxx() helpers write here. Default points at std::cout so any
// code that calls a Log helper before InitializeLogging() runs (tests,
// early-startup paths) still produces output.
std::ostream* g_logStream = &std::cout;

// Owns the file stream when HONUWARE_LOG_DEST resolves to a path. Held
// in a unique_ptr so it stays alive for the process lifetime.
std::unique_ptr<std::ofstream> g_fileStream;

// Force the given C FILE* into line-buffered mode. The systemd journal and
// the CloudWatch Logs agent both tail piped stdout/stderr line-by-line; the
// default behavior when stdout is redirected to a pipe is *block* buffered,
// which can hide log lines for minutes. POSIX honors _IOLBF; on MSVC the
// runtime treats it as full-buffered, which is fine for Windows dev where
// console output is line-buffered by default anyway.
void EnsureLineBuffered(FILE* file) {
    std::setvbuf(file, nullptr, _IOLBF, /*size=*/4096);
}

// Format a UTC timestamp matching Crow's default CerrLogHandler so the
// bridged output is indistinguishable from the upstream format.
std::string CrowTimestamp() {
    char date[32];
    std::time_t t = std::time(nullptr);
    std::tm my_tm{};
#if defined(_MSC_VER) || defined(__MINGW32__)
    gmtime_s(&my_tm, &t);
#else
    gmtime_r(&t, &my_tm);
#endif
    size_t sz = std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &my_tm);
    return std::string(date, date + sz);
}

// Crow log handler that bridges into our destination stream. Mirrors the
// timestamp + level-prefix format that crow::CerrLogHandler uses, but
// writes to *g_logStream (so the destination follows HONUWARE_LOG_DEST
// just like our own LogXxx() output).
//
// Crow 1.3+ takes `message` by `const std::string&`; Crow 1.2 took it
// by value. The signature here matches 1.3+, which is what we ship.
class CrowLogHandler : public crow::ILogHandler {
public:
    void log(const std::string& message, crow::LogLevel level) override {
        std::string label = CrowLogLevelLabel(static_cast<int>(level));
        (*g_logStream) << "(" << CrowTimestamp() << ") ["
                       << label << "] " << message << "\n";
        // Defensive: explicit flush so a Crow-emitted ERROR on a request
        // path lands in the journal even when the surrounding app is idle.
        g_logStream->flush();
    }
};

// Single process-lifetime instance. Crow holds this as a raw pointer.
CrowLogHandler g_crowLogHandler;

}  // namespace

std::string CrowLogLevelLabel(int crowLogLevel) {
    switch (static_cast<crow::LogLevel>(crowLogLevel)) {
        case crow::LogLevel::Debug:    return "DEBUG   ";
        case crow::LogLevel::Info:     return "INFO    ";
        case crow::LogLevel::Warning:  return "WARNING ";
        case crow::LogLevel::Error:    return "ERROR   ";
        case crow::LogLevel::Critical: return "CRITICAL";
    }
    return "UNKNOWN ";
}

LogDestinationConfig ResolveLogDestination(const char* envValue) {
    LogDestinationConfig cfg;
    if (envValue == nullptr || envValue[0] == '\0') {
        cfg.dest = LogDestination::Stdout;
        return cfg;
    }
    if (std::strcmp(envValue, "stdout") == 0) {
        cfg.dest = LogDestination::Stdout;
        return cfg;
    }
    if (std::strcmp(envValue, "stderr") == 0) {
        cfg.dest = LogDestination::Stderr;
        return cfg;
    }
    cfg.dest = LogDestination::File;
    cfg.filePath = envValue;
    return cfg;
}

LogDestinationConfig InitializeLogging() {
    // Read the canonical HONUWARE_* name first, falling back to the legacy
    // KNOTTYYOGA_* name during the componentization rename (Phase 1.1).
    const char* envValue = Util::GetEnvWithFallback(
        "HONUWARE_LOG_DEST", "KNOTTYYOGA_LOG_DEST");
    LogDestinationConfig cfg = ResolveLogDestination(envValue);

    switch (cfg.dest) {
        case LogDestination::Stdout:
            EnsureLineBuffered(stdout);
            g_fileStream.reset();
            g_logStream = &std::cout;
            break;
        case LogDestination::Stderr:
            EnsureLineBuffered(stderr);
            g_fileStream.reset();
            g_logStream = &std::cerr;
            break;
        case LogDestination::File:
            g_fileStream = std::make_unique<std::ofstream>(
                cfg.filePath, std::ios::out | std::ios::app);
            if (g_fileStream && g_fileStream->is_open()) {
                // Flush on every operator<<; cheap insurance for the file
                // path (operator typically writes 1 line at a time anyway).
                g_fileStream->setf(std::ios::unitbuf);
                g_logStream = g_fileStream.get();
            } else {
                // Don't go silent: fall back to stdout and surface the
                // failure on stderr so an operator notices.
                EnsureLineBuffered(stdout);
                g_fileStream.reset();
                g_logStream = &std::cout;
                std::cerr << "Failed to open log file '" << cfg.filePath
                          << "'; falling back to stdout\n";
            }
            break;
    }

    // Bridge Crow's CROW_LOG_* macros into the same destination. Without
    // this, request-handling logs from Crow keep going to std::cerr via
    // its default CerrLogHandler, and operators have two log streams to
    // correlate. The handler instance is process-lifetime static.
    crow::logger::setHandler(&g_crowLogHandler);

    return cfg;
}

std::ostream& LogError()   { return *g_logStream; }
std::ostream& LogInfo()    { return *g_logStream; }
std::ostream& LogWarning() { return *g_logStream; }
