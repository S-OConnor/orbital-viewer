// logger.hpp — thread-safe file logger + ISO-8601 time utilities.
//
// One line per event: "<ISO-8601 UTC ms> <LEVEL> [component] message".
// Used from both the UDP thread and the WebSocket thread; all public
// methods are safe to call concurrently.

#pragma once

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace olv {

enum class LogLevel { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3 };

const char* toString(LogLevel level);

// Parses "debug"/"info"/"warn"/"error" (case-insensitive). Returns false and
// leaves `out` untouched on unknown input.
bool parseLogLevel(std::string_view text, LogLevel& out);

// "2026-07-01T12:34:56.789Z" (UTC, millisecond precision).
std::string iso8601Utc(std::chrono::system_clock::time_point tp);
std::string iso8601UtcNow();

class Logger {
 public:
  Logger() = default;
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // Opens (appends to) the log file. mirror_stderr additionally writes every
  // line at >= kInfo to stderr. Returns false if the file cannot be opened.
  bool open(const std::string& path, LogLevel level, bool mirror_stderr);

  void log(LogLevel level, std::string_view component, std::string_view message);

  void debug(std::string_view component, std::string_view message) {
    log(LogLevel::kDebug, component, message);
  }
  void info(std::string_view component, std::string_view message) {
    log(LogLevel::kInfo, component, message);
  }
  void warn(std::string_view component, std::string_view message) {
    log(LogLevel::kWarn, component, message);
  }
  void error(std::string_view component, std::string_view message) {
    log(LogLevel::kError, component, message);
  }

  LogLevel level() const { return level_; }

 private:
  std::mutex mutex_;
  std::ofstream file_;
  LogLevel level_ = LogLevel::kInfo;
  bool mirror_stderr_ = false;
};

}  // namespace olv
