// logger.cpp — see logger.hpp.

#include "olv/logger.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iostream>

namespace olv {

const char* toString(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "UNKNOWN";
}

bool parseLogLevel(std::string_view text, LogLevel& out) {
  std::string lower;
  lower.reserve(text.size());
  for (char c : text)
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (lower == "debug") {
    out = LogLevel::kDebug;
  } else if (lower == "info") {
    out = LogLevel::kInfo;
  } else if (lower == "warn") {
    out = LogLevel::kWarn;
  } else if (lower == "error") {
    out = LogLevel::kError;
  } else {
    return false;
  }
  return true;
}

std::string iso8601Utc(std::chrono::system_clock::time_point tp) {
  using namespace std::chrono;
  const auto secs = time_point_cast<seconds>(tp);
  const auto ms = duration_cast<milliseconds>(tp - secs).count();
  const std::time_t t = system_clock::to_time_t(secs);
  std::tm tm{};
  gmtime_r(&t, &tm);
  // Sized for the compiler's worst-case width analysis of the %d directives.
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
  return std::string(buf);
}

std::string iso8601UtcNow() {
  return iso8601Utc(std::chrono::system_clock::now());
}

bool Logger::open(const std::string& path, LogLevel level, bool mirror_stderr) {
  std::lock_guard<std::mutex> lock(mutex_);
  file_.open(path, std::ios::out | std::ios::app);
  if (!file_.is_open()) return false;
  level_ = level;
  mirror_stderr_ = mirror_stderr;
  return true;
}

void Logger::log(LogLevel level, std::string_view component, std::string_view message) {
  if (level < level_) return;
  const std::string line = iso8601UtcNow();
  std::lock_guard<std::mutex> lock(mutex_);
  file_ << line << ' ' << toString(level) << " [" << component << "] " << message << '\n';
  file_.flush();
  if (mirror_stderr_ && level >= LogLevel::kInfo) {
    std::cerr << line << ' ' << toString(level) << " [" << component << "] " << message << '\n';
  }
}

}  // namespace olv
