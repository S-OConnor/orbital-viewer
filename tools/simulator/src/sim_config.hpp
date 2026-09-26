// sim_config.hpp — simulator configuration: TOML config file + command line.
//
// Precedence (lowest to highest): built-in defaults < --config TOML file <
// explicit command-line flags. Uses the first-party TOML-subset parser
// (olv/toml.hpp). Pure std (no Boost) so it lives in olv_sim_lib and is
// unit-testable; only main.cpp touches the network.
//
// TOML schema (strict: unknown keys, wrong types, and out-of-range values
// are errors — see config/simulator.toml for a commented example):
//   [target]  host = "127.0.0.1"
//             port = 47000
//   [source]  mode = "csv"                 # "csv" | "generate"
//             csv_path = "…/mission.csv"   # csv mode
//             loop = false                 # csv mode: wrap and keep going
//             generate_count = 5000        # generate mode: [1, 5000]
//             duration_seconds = 120       # generate mode: 0 = forever
//             seed = 1                     # generate mode
//   [send]    rate_hz = 1.0                # (0, 50]
//             chunk = 128                  # objects/packet, [1, 128], olv1 only
//             protocol = "olv1"            # "olv1" | "dis" | "olv2"
//             dis_exercise_id = 1          # dis: PDU header exerciseID, [0, 255]
//             dis_site = 1                 # dis: EntityID site for objects
//             dis_satellite_entity_id = "1:1:1"  # dis: satellite EntityID
//             olv2_points = 10             # olv2: points per datagram, [1, 25]
//   [output]  quiet = false

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace olv::sim {

struct SimConfig {
  enum class Mode { kUnset, kCsv, kGenerate };
  enum class Protocol { kOlv1, kDis, kOlv2 };

  std::string config_file;  // --config PATH; empty = no file
  Mode mode = Mode::kUnset;
  std::string csv_path;
  bool loop = false;
  int generate_count = 0;
  int duration_seconds = 120;  // generate mode; 0 = run forever
  std::uint32_t seed = 1;
  std::string dest_host = "127.0.0.1";
  // Default 47000 (olv1). When protocol = "dis"/"olv2" and neither the file
  // nor --port set a port, parseSimArgs switches this to 47001/47002,
  // matching the backend's distinct defaults so both modes' defaults line up
  // end-to-end.
  std::uint16_t dest_port = 47000;
  bool dest_port_set = false;  // true once the file or --port set dest_port
  double rate_hz = 1.0;
  int chunk = 128;  // olv1 only; DIS is one entity per PDU/datagram
  Protocol protocol = Protocol::kOlv1;
  int dis_exercise_id = 1;                        // dis: [0, 255]
  int dis_site = 1;                               // dis: [0, 65535]
  std::string dis_satellite_entity_id = "1:1:1";  // dis: "site:application:entity"
  int olv2_points = 10;                           // olv2: points per datagram, [1, 25]
  bool quiet = false;
  bool show_help = false;
};

// Loads `path` and applies it over `cfg`. Returns false and fills `error`
// (with the offending key or line number) on unreadable file, TOML syntax
// error, unknown key, wrong type, or out-of-range value. Range rules are
// identical to the command-line flags.
bool applySimConfigFile(const std::string& path, SimConfig& cfg, std::string& error);

// Supported flags: --config PATH, --csv PATH, --generate N, --dest IP,
// --port N, --rate HZ, --loop, --chunk N, --duration S, --seed N,
// --protocol olv1|dis|olv2, --quiet, --help. There are no --dis-*/--olv2-*
// flags (matching the backend's --input-mode-only precedent): the dis_* and
// olv2_* settings come from the config file or their defaults. The config file (if given) is
// applied first, then the remaining flags on top, regardless of position. --csv and --generate each
// force the mode and are mutually exclusive ON THE COMMAND LINE; either overrides a mode set by the
// file. After merging, a source mode MUST be set (csv mode additionally requires a csv_path;
// generate mode requires generate_count in [1,5000]) or an error is returned. Returns nullopt +
// `error` on invalid input;
// --help returns a config with show_help=true.
std::optional<SimConfig> parseSimArgs(int argc, const char* const* argv, std::string& error);

void printSimUsage(const char* argv0);

}  // namespace olv::sim
