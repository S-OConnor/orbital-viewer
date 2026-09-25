// config.hpp — backend configuration: TOML config file + command line.
//
// Precedence (lowest to highest): built-in defaults < --config TOML file <
// explicit command-line flags. Hand-rolled parsing plus the first-party
// TOML-subset parser (olv/toml.hpp); see docs/PLAN.md §10.
//
// TOML schema (strict: unknown keys, wrong types, and out-of-range values
// are errors — see config/backend.toml for a commented example):
//   [network]  udp_bind = "0.0.0.0"   # UDP listen address
//              udp_port = 47000
//              ws_bind  = "0.0.0.0"   # WebSocket listen address
//              ws_port  = 8765
//   [broadcast] hz = 1.0              # (0, 60]
//   [state]    expiry_seconds = 15    # [1, 3600]
//   [logging]  file = "olv_backend.log"
//              level = "info"         # debug|info|warn|error
//              stderr = true          # mirror >= info to stderr
//   [input]    mode = "olv1"          # olv1|dis (intake strategy at boot)
//              dis_bind = "0.0.0.0"   # consulted only when mode = "dis":
//              dis_port = 47001       #   DIS Entity State PDU listen socket
//              dis_exercise_id = 1    #   optional 0-255 filter; omit = accept all
//              dis_satellite_entity_id = "1:1:1"  # site:app:entity; required when dis
// (The first-party TOML subset has single-level tables only, so the DIS group
// is flat dis_* keys under [input] rather than a nested [input.dis] table.)

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "olv/dis_input_source.hpp"
#include "olv/input_source.hpp"
#include "olv/logger.hpp"

namespace olv {

struct Config {
  std::string config_file;  // --config PATH; empty = no file
  std::string udp_bind = "0.0.0.0";
  std::uint16_t udp_port = 47000;
  std::string ws_bind = "0.0.0.0";
  std::uint16_t ws_port = 8765;
  std::string log_file = "olv_backend.log";
  LogLevel log_level = LogLevel::kInfo;
  int expiry_seconds = 15;    // object table expiry window
  double broadcast_hz = 1.0;  // WebSocket broadcast rate
  bool log_stderr = true;     // mirror >= info to stderr
  InputMode input_mode = InputMode::kOlv1;  // intake strategy selected at boot
  DisInputConfig dis;         // consulted only when input_mode == kDis
  bool show_help = false;
};

// Loads `path` and applies it over `cfg`. Returns false and fills `error`
// (including the offending key or line number) on: unreadable file, TOML
// syntax error, unknown key, wrong value type, or out-of-range value. Range
// rules are identical to the command-line flags.
bool applyConfigFile(const std::string& path, Config& cfg, std::string& error);

// Supported flags (all optional):
//   --config PATH  --udp-bind ADDR --udp-port N --ws-bind ADDR --ws-port N
//   --log-file PATH --log-level LVL --expiry-seconds N --broadcast-hz X
//   --input-mode olv1|dis --quiet (no stderr mirror) --help
// The config file (if given) is applied first, then the remaining flags on
// top, regardless of their position relative to --config. Returns nullopt
// and fills `error` on invalid input. When --help was given, returns a
// Config with show_help=true and the caller prints usage.
std::optional<Config> parseArgs(int argc, const char* const* argv, std::string& error);

void printUsage(const char* argv0);

}  // namespace olv
