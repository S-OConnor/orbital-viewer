// config.cpp — see config.hpp. Hand-rolled CLI parsing plus TOML config-file
// application; both paths share the same range-validation helpers below so
// the rules cannot diverge.

#include "olv/config.hpp"

#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>

#include "olv/toml.hpp"

namespace olv {

namespace {

// ---------------------------------------------------------------------------
// Shared range validation (used by both --flags and the TOML file path).
// ---------------------------------------------------------------------------

constexpr const char* kPortRangeDesc = "1-65535";
constexpr const char* kExpiryRangeDesc = "1-3600";
constexpr const char* kHzRangeDesc = ">0 and <=60";
constexpr const char* kExerciseRangeDesc = "0-255";

bool validPort(long long v) {
  return v >= 1 && v <= 65535;
}
bool validExpiry(long long v) {
  return v >= 1 && v <= 3600;
}
bool validHz(double v) {
  return v > 0.0 && v <= 60.0;
}
bool validExerciseId(long long v) {
  return v >= 0 && v <= 255;
}

std::string formatDouble(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  return buf;
}

bool parseLong(std::string_view s, long& out) {
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(begin, end, out);
  return ec == std::errc{} && ptr == end;
}

bool parseDouble(std::string_view s, double& out) {
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  auto [ptr, ec] = std::from_chars(begin, end, out);
  return ec == std::errc{} && ptr == end;
}

}  // namespace

// ---------------------------------------------------------------------------
// TOML config file.
// ---------------------------------------------------------------------------

bool applyConfigFile(const std::string& path, Config& cfg, std::string& error) {
  const toml::ParseResult res = toml::parseFile(path);
  if (!res.ok()) {
    if (res.error_line > 0) {
      error = path + ":" + std::to_string(res.error_line) + ": " + res.error;
    } else {
      error = res.error;
    }
    return false;
  }

  auto typeError = [&](const std::string& key, const char* expected, const toml::Value& v) {
    error = path + ": " + key + " expected " + expected + ", got " + v.typeName();
    return false;
  };

  for (const auto& [key, value] : res.values) {
    if (key == "network.udp_bind") {
      if (value.type != toml::Value::Type::kString) return typeError(key, "string", value);
      cfg.udp_bind = value.s;
    } else if (key == "network.udp_port") {
      if (value.type != toml::Value::Type::kInteger) return typeError(key, "integer", value);
      if (!validPort(value.i)) {
        error = path + ": " + key + " out of range (" + kPortRangeDesc +
                "): " + std::to_string(value.i);
        return false;
      }
      cfg.udp_port = static_cast<std::uint16_t>(value.i);
    } else if (key == "network.ws_bind") {
      if (value.type != toml::Value::Type::kString) return typeError(key, "string", value);
      cfg.ws_bind = value.s;
    } else if (key == "network.ws_port") {
      if (value.type != toml::Value::Type::kInteger) return typeError(key, "integer", value);
      if (!validPort(value.i)) {
        error = path + ": " + key + " out of range (" + kPortRangeDesc +
                "): " + std::to_string(value.i);
        return false;
      }
      cfg.ws_port = static_cast<std::uint16_t>(value.i);
    } else if (key == "broadcast.hz") {
      double hz = 0.0;
      if (value.type == toml::Value::Type::kInteger) {
        hz = static_cast<double>(value.i);
      } else if (value.type == toml::Value::Type::kFloat) {
        hz = value.f;
      } else {
        return typeError(key, "float or integer", value);
      }
      if (!validHz(hz)) {
        error = path + ": " + key + " out of range (" + kHzRangeDesc + "): " + formatDouble(hz);
        return false;
      }
      cfg.broadcast_hz = hz;
    } else if (key == "state.expiry_seconds") {
      if (value.type != toml::Value::Type::kInteger) return typeError(key, "integer", value);
      if (!validExpiry(value.i)) {
        error = path + ": " + key + " out of range (" + kExpiryRangeDesc +
                "): " + std::to_string(value.i);
        return false;
      }
      cfg.expiry_seconds = static_cast<int>(value.i);
    } else if (key == "logging.file") {
      if (value.type != toml::Value::Type::kString) return typeError(key, "string", value);
      if (value.s.empty()) {
        error = path + ": " + key + " must not be empty";
        return false;
      }
      cfg.log_file = value.s;
    } else if (key == "logging.level") {
      if (value.type != toml::Value::Type::kString) return typeError(key, "string", value);
      if (!parseLogLevel(value.s, cfg.log_level)) {
        error = path + ": " + key + " invalid log level (debug|info|warn|error): " + value.s;
        return false;
      }
    } else if (key == "logging.stderr") {
      if (value.type != toml::Value::Type::kBoolean) return typeError(key, "boolean", value);
      cfg.log_stderr = value.b;
    } else if (key == "input.mode") {
      if (value.type != toml::Value::Type::kString) return typeError(key, "string", value);
      if (!parseInputMode(value.s, cfg.input_mode)) {
        error = path + ": " + key + " invalid input mode (olv1|dis): " + value.s;
        return false;
      }
    } else if (key == "input.dis_bind") {
      if (value.type != toml::Value::Type::kString) return typeError(key, "string", value);
      cfg.dis.bind_address = value.s;
    } else if (key == "input.dis_port") {
      if (value.type != toml::Value::Type::kInteger) return typeError(key, "integer", value);
      if (!validPort(value.i)) {
        error = path + ": " + key + " out of range (" + kPortRangeDesc +
                "): " + std::to_string(value.i);
        return false;
      }
      cfg.dis.port = static_cast<std::uint16_t>(value.i);
    } else if (key == "input.dis_exercise_id") {
      if (value.type != toml::Value::Type::kInteger) return typeError(key, "integer", value);
      if (!validExerciseId(value.i)) {
        error = path + ": " + key + " out of range (" + kExerciseRangeDesc +
                "): " + std::to_string(value.i);
        return false;
      }
      cfg.dis.exercise_id = static_cast<std::uint8_t>(value.i);
    } else if (key == "input.dis_satellite_entity_id") {
      if (value.type != toml::Value::Type::kString) return typeError(key, "string", value);
      std::uint16_t s = 0, a = 0, e = 0;
      if (!parseDisEntityId(value.s, s, a, e)) {
        error = path + ": " + key +
                " invalid satellite entity id (expected site:application:entity, three decimal "
                "0-65535 values): " +
                value.s;
        return false;
      }
      cfg.dis.satellite_entity_id = value.s;
    } else {
      error = path + ": unknown key \"" + key + "\"";
      return false;
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// Command line.
// ---------------------------------------------------------------------------

std::optional<Config> parseArgs(int argc, const char* const* argv, std::string& error) {
  // Pass 1: locate --config (order-independent relative to other flags) and
  // apply it over the defaults before any other flag is considered.
  std::string config_path;
  bool has_config = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg != "--config") continue;
    if (has_config) {
      error = "--config given more than once";
      return std::nullopt;
    }
    if (i + 1 >= argc) {
      error = "missing value for --config";
      return std::nullopt;
    }
    config_path = argv[++i];
    has_config = true;
  }

  Config cfg;
  if (has_config) {
    cfg.config_file = config_path;
    if (!applyConfigFile(config_path, cfg, error)) return std::nullopt;
  }

  // Pass 2: apply every remaining flag on top of defaults/file, in argv
  // order, skipping --config (already handled above).
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];

    if (arg == "--config") {
      ++i;  // skip its value; already validated in pass 1
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      cfg.show_help = true;
      return cfg;
    }
    if (arg == "--quiet") {
      cfg.log_stderr = false;
      continue;
    }

    // All remaining flags take a value in the following argument.
    if (i + 1 >= argc) {
      error = "missing value for " + std::string(arg);
      return std::nullopt;
    }
    const std::string_view value = argv[++i];

    if (arg == "--udp-bind") {
      cfg.udp_bind = std::string(value);
    } else if (arg == "--ws-bind") {
      cfg.ws_bind = std::string(value);
    } else if (arg == "--udp-port" || arg == "--ws-port") {
      long v = 0;
      if (!parseLong(value, v)) {
        error = "non-numeric value for " + std::string(arg) + ": " + std::string(value);
        return std::nullopt;
      }
      if (!validPort(v)) {
        error = std::string(arg) + " out of range (" + kPortRangeDesc + "): " + std::string(value);
        return std::nullopt;
      }
      (arg == "--udp-port" ? cfg.udp_port : cfg.ws_port) = static_cast<std::uint16_t>(v);
    } else if (arg == "--log-file") {
      cfg.log_file = std::string(value);
    } else if (arg == "--log-level") {
      if (!parseLogLevel(value, cfg.log_level)) {
        error = "invalid log level (debug|info|warn|error): " + std::string(value);
        return std::nullopt;
      }
    } else if (arg == "--input-mode") {
      if (!parseInputMode(value, cfg.input_mode)) {
        error = "invalid input mode (olv1|dis): " + std::string(value);
        return std::nullopt;
      }
    } else if (arg == "--expiry-seconds") {
      long v = 0;
      if (!parseLong(value, v)) {
        error = "non-numeric value for --expiry-seconds: " + std::string(value);
        return std::nullopt;
      }
      if (!validExpiry(v)) {
        error = std::string("--expiry-seconds out of range (") + kExpiryRangeDesc +
                "): " + std::string(value);
        return std::nullopt;
      }
      cfg.expiry_seconds = static_cast<int>(v);
    } else if (arg == "--broadcast-hz") {
      double v = 0.0;
      if (!parseDouble(value, v)) {
        error = "non-numeric value for --broadcast-hz: " + std::string(value);
        return std::nullopt;
      }
      if (!validHz(v)) {
        error = std::string("--broadcast-hz out of range (") + kHzRangeDesc +
                "): " + std::string(value);
        return std::nullopt;
      }
      cfg.broadcast_hz = v;
    } else {
      error = "unknown flag: " + std::string(arg);
      return std::nullopt;
    }
  }

  // Cross-field invariant: DIS mode needs a satellite entity id (it has no
  // sensible default — §6). Checked on the fully merged config so it holds
  // regardless of whether mode and the id came from the file, flags, or a mix.
  if (cfg.input_mode == InputMode::kDis && cfg.dis.satellite_entity_id.empty()) {
    error = "input mode 'dis' requires [input] dis_satellite_entity_id (site:application:entity)";
    return std::nullopt;
  }

  return cfg;
}

void printUsage(const char* argv0) {
  std::printf(
      "Usage: %s [options]\n"
      "  --config PATH       TOML config file (see config/backend.toml)\n"
      "  --udp-bind ADDR     UDP listen address (default 0.0.0.0)\n"
      "  --udp-port N        UDP listen port (default 47000)\n"
      "  --ws-bind ADDR      WebSocket listen address (default 0.0.0.0)\n"
      "  --ws-port N         WebSocket listen port (default 8765)\n"
      "  --log-file PATH     log file path (default olv_backend.log)\n"
      "  --log-level LVL     debug|info|warn|error (default info)\n"
      "  --expiry-seconds N  object expiry window seconds, 1-3600 (default 15)\n"
      "  --broadcast-hz X    WebSocket broadcast rate, >0 and <=60 (default 1)\n"
      "  --input-mode MODE   intake strategy: olv1|dis (default olv1)\n"
      "  --quiet             do not mirror log lines to stderr\n"
      "  --help              show this help and exit\n"
      "Precedence: defaults < --config file < flags (regardless of where\n"
      "--config appears among the flags). See config/backend.toml.\n",
      argv0);
}

}  // namespace olv
