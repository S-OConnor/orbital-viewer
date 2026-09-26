// sim_config.cpp — see sim_config.hpp for the schema/precedence contract.

#include "sim_config.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <string>

#include "olv/dis_entity_id.hpp"
#include "olv/protocol.hpp"
#include "olv/protocol_olv2.hpp"
#include "olv/toml.hpp"

namespace olv::sim {

namespace {

// ---- Validation limits shared by the TOML schema walk and the CLI --------

bool validPort(std::int64_t v) {
  return v >= 1 && v <= 65535;
}
bool validRateHz(double v) {
  return v > 0.0 && v <= 50.0;
}
bool validChunk(std::int64_t v) {
  return v >= 1 && v <= olv::proto::kMaxObjectsPerPacket;
}
bool validGenerateCount(std::int64_t v) {
  return v >= 1 && v <= 5000;
}
bool validDurationSeconds(std::int64_t v) {
  return v >= 0;
}
bool validSeed(std::int64_t v) {
  return v >= 0 && v <= 4294967295LL;
}
bool validExerciseId(std::int64_t v) {
  return v >= 0 && v <= 255;
}
bool validSite(std::int64_t v) {
  return v >= 0 && v <= 65535;
}
bool validSatelliteEntityId(const std::string& s) {
  std::uint16_t site = 0, app = 0, entity = 0;
  return olv::parseDisEntityId(s, site, app, entity);
}
bool validOlv2Points(std::int64_t v) {
  return v >= 1 && v <= static_cast<std::int64_t>(olv::proto::olv2::kMaxPoints);
}

bool parseInt64(const std::string& s, std::int64_t& out) {
  auto res = std::from_chars(s.data(), s.data() + s.size(), out);
  return res.ec == std::errc() && res.ptr == s.data() + s.size();
}

bool parseDouble(const std::string& s, double& out) {
  auto res = std::from_chars(s.data(), s.data() + s.size(), out);
  return res.ec == std::errc() && res.ptr == s.data() + s.size();
}

}  // namespace

bool applySimConfigFile(const std::string& path, SimConfig& cfg, std::string& error) {
  olv::toml::ParseResult parsed = olv::toml::parseFile(path);
  if (!parsed.ok()) {
    error = parsed.error_line > 0
                ? (path + ":" + std::to_string(parsed.error_line) + ": " + parsed.error)
                : parsed.error;
    return false;
  }

  // Strict schema walk: every recognized key is removed from `remaining`;
  // anything left over afterwards is an unknown key.
  std::map<std::string, olv::toml::Value> remaining(parsed.values);
  using Value = olv::toml::Value;

  auto fail = [&](const std::string& key, const std::string& detail) {
    error = path + ": \"" + key + "\" " + detail;
    return false;
  };

  auto take = [&](const std::string& key) -> std::optional<Value> {
    auto it = remaining.find(key);
    if (it == remaining.end()) return std::nullopt;
    Value v = std::move(it->second);
    remaining.erase(it);
    return v;
  };

  if (auto v = take("target.host")) {
    if (v->type != Value::Type::kString) return fail("target.host", "must be a string");
    if (v->s.empty()) return fail("target.host", "must not be empty");
    cfg.dest_host = v->s;
  }
  if (auto v = take("target.port")) {
    if (v->type != Value::Type::kInteger) return fail("target.port", "must be an integer");
    if (!validPort(v->i)) return fail("target.port", "must be in [1,65535]");
    cfg.dest_port = static_cast<std::uint16_t>(v->i);
    cfg.dest_port_set = true;
  }
  if (auto v = take("source.mode")) {
    if (v->type != Value::Type::kString) return fail("source.mode", "must be a string");
    if (v->s == "csv") {
      cfg.mode = SimConfig::Mode::kCsv;
    } else if (v->s == "generate") {
      cfg.mode = SimConfig::Mode::kGenerate;
    } else {
      return fail("source.mode", "must be \"csv\" or \"generate\" (got \"" + v->s + "\")");
    }
  }
  if (auto v = take("source.csv_path")) {
    if (v->type != Value::Type::kString) return fail("source.csv_path", "must be a string");
    cfg.csv_path = v->s;
  }
  if (auto v = take("source.loop")) {
    if (v->type != Value::Type::kBoolean) return fail("source.loop", "must be a boolean");
    cfg.loop = v->b;
  }
  if (auto v = take("source.generate_count")) {
    if (v->type != Value::Type::kInteger)
      return fail("source.generate_count", "must be an integer");
    if (!validGenerateCount(v->i)) return fail("source.generate_count", "must be in [1,5000]");
    cfg.generate_count = static_cast<int>(v->i);
  }
  if (auto v = take("source.duration_seconds")) {
    if (v->type != Value::Type::kInteger)
      return fail("source.duration_seconds", "must be an integer");
    if (!validDurationSeconds(v->i)) return fail("source.duration_seconds", "must be >= 0");
    cfg.duration_seconds = static_cast<int>(v->i);
  }
  if (auto v = take("source.seed")) {
    if (v->type != Value::Type::kInteger) return fail("source.seed", "must be an integer");
    if (!validSeed(v->i)) return fail("source.seed", "must be in [0,4294967295]");
    cfg.seed = static_cast<std::uint32_t>(v->i);
  }
  if (auto v = take("send.rate_hz")) {
    double r = 0.0;
    if (v->type == Value::Type::kInteger) {
      r = static_cast<double>(v->i);
    } else if (v->type == Value::Type::kFloat) {
      r = v->f;
    } else {
      return fail("send.rate_hz", "must be a float or integer");
    }
    if (!validRateHz(r)) return fail("send.rate_hz", "must be > 0 and <= 50");
    cfg.rate_hz = r;
  }
  if (auto v = take("send.chunk")) {
    if (v->type != Value::Type::kInteger) return fail("send.chunk", "must be an integer");
    if (!validChunk(v->i)) return fail("send.chunk", "must be in [1,128]");
    cfg.chunk = static_cast<int>(v->i);
  }
  if (auto v = take("send.protocol")) {
    if (v->type != Value::Type::kString) return fail("send.protocol", "must be a string");
    if (v->s == "olv1") {
      cfg.protocol = SimConfig::Protocol::kOlv1;
    } else if (v->s == "dis") {
      cfg.protocol = SimConfig::Protocol::kDis;
    } else if (v->s == "olv2") {
      cfg.protocol = SimConfig::Protocol::kOlv2;
    } else {
      return fail("send.protocol", "must be \"olv1\", \"dis\", or \"olv2\" (got \"" + v->s + "\")");
    }
  }
  if (auto v = take("send.dis_exercise_id")) {
    if (v->type != Value::Type::kInteger) return fail("send.dis_exercise_id", "must be an integer");
    if (!validExerciseId(v->i)) return fail("send.dis_exercise_id", "must be in [0,255]");
    cfg.dis_exercise_id = static_cast<int>(v->i);
  }
  if (auto v = take("send.dis_site")) {
    if (v->type != Value::Type::kInteger) return fail("send.dis_site", "must be an integer");
    if (!validSite(v->i)) return fail("send.dis_site", "must be in [0,65535]");
    cfg.dis_site = static_cast<int>(v->i);
  }
  if (auto v = take("send.dis_satellite_entity_id")) {
    if (v->type != Value::Type::kString)
      return fail("send.dis_satellite_entity_id", "must be a string");
    if (!validSatelliteEntityId(v->s))
      return fail("send.dis_satellite_entity_id",
                  "must be \"site:application:entity\" (three decimal uint16s)");
    cfg.dis_satellite_entity_id = v->s;
  }
  if (auto v = take("send.olv2_points")) {
    if (v->type != Value::Type::kInteger) return fail("send.olv2_points", "must be an integer");
    if (!validOlv2Points(v->i)) return fail("send.olv2_points", "must be in [1,25]");
    cfg.olv2_points = static_cast<int>(v->i);
  }
  if (auto v = take("output.quiet")) {
    if (v->type != Value::Type::kBoolean) return fail("output.quiet", "must be a boolean");
    cfg.quiet = v->b;
  }

  if (!remaining.empty()) {
    return fail(remaining.begin()->first, "unknown key");
  }

  return true;
}

std::optional<SimConfig> parseSimArgs(int argc, const char* const* argv, std::string& error) {
  SimConfig cfg;

  // Pass 1: locate --config anywhere in argv and apply the file first, so
  // that flags encountered in pass 2 (in their original argv order,
  // regardless of position relative to --config) always win.
  std::optional<std::string> config_path;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--config") {
      if (config_path.has_value()) {
        error = "--config: specified more than once";
        return std::nullopt;
      }
      if (i + 1 >= argc) {
        error = "--config requires a value";
        return std::nullopt;
      }
      config_path = argv[++i];
    }
  }

  if (config_path) {
    cfg.config_file = *config_path;
    if (!applySimConfigFile(*config_path, cfg, error)) return std::nullopt;
  }

  // Pass 2: apply the remaining flags on top, in argv order.
  bool have_csv_flag = false;
  bool have_generate_flag = false;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];

    auto value = [&]() -> std::optional<std::string> {
      if (i + 1 >= argc) return std::nullopt;
      return std::string(argv[++i]);
    };

    if (a == "--config") {
      ++i;  // skip the value; already applied in pass 1
      continue;
    } else if (a == "--help") {
      cfg.show_help = true;
      return cfg;
    } else if (a == "--quiet") {
      cfg.quiet = true;
    } else if (a == "--loop") {
      cfg.loop = true;
    } else if (a == "--csv") {
      auto v = value();
      if (!v) {
        error = "--csv requires a value";
        return std::nullopt;
      }
      cfg.mode = SimConfig::Mode::kCsv;
      cfg.csv_path = *v;
      have_csv_flag = true;
    } else if (a == "--generate") {
      auto v = value();
      std::int64_t n = 0;
      if (!v || !parseInt64(*v, n) || !validGenerateCount(n)) {
        error = "--generate: expected an integer in [1,5000]";
        return std::nullopt;
      }
      cfg.mode = SimConfig::Mode::kGenerate;
      cfg.generate_count = static_cast<int>(n);
      have_generate_flag = true;
    } else if (a == "--dest") {
      auto v = value();
      if (!v) {
        error = "--dest requires a value";
        return std::nullopt;
      }
      cfg.dest_host = *v;
    } else if (a == "--port") {
      auto v = value();
      std::int64_t n = 0;
      if (!v || !parseInt64(*v, n) || !validPort(n)) {
        error = "--port: expected an integer in [1,65535]";
        return std::nullopt;
      }
      cfg.dest_port = static_cast<std::uint16_t>(n);
      cfg.dest_port_set = true;
    } else if (a == "--protocol") {
      auto v = value();
      if (!v || (*v != "olv1" && *v != "dis" && *v != "olv2")) {
        error = "--protocol: expected \"olv1\", \"dis\", or \"olv2\"";
        return std::nullopt;
      }
      cfg.protocol = (*v == "dis")    ? SimConfig::Protocol::kDis
                     : (*v == "olv2") ? SimConfig::Protocol::kOlv2
                                      : SimConfig::Protocol::kOlv1;
    } else if (a == "--rate") {
      auto v = value();
      double r = 0.0;
      if (!v || !parseDouble(*v, r) || !validRateHz(r)) {
        error = "--rate: expected 0 < HZ <= 50";
        return std::nullopt;
      }
      cfg.rate_hz = r;
    } else if (a == "--chunk") {
      auto v = value();
      std::int64_t n = 0;
      if (!v || !parseInt64(*v, n) || !validChunk(n)) {
        error = "--chunk: expected an integer in [1,128]";
        return std::nullopt;
      }
      cfg.chunk = static_cast<int>(n);
    } else if (a == "--duration") {
      auto v = value();
      std::int64_t n = 0;
      if (!v || !parseInt64(*v, n) || !validDurationSeconds(n)) {
        error = "--duration: expected an integer >= 0";
        return std::nullopt;
      }
      cfg.duration_seconds = static_cast<int>(n);
    } else if (a == "--seed") {
      auto v = value();
      std::int64_t n = 0;
      if (!v || !parseInt64(*v, n) || !validSeed(n)) {
        error = "--seed: expected an integer in [0,4294967295]";
        return std::nullopt;
      }
      cfg.seed = static_cast<std::uint32_t>(n);
    } else {
      error = "unknown flag: " + a;
      return std::nullopt;
    }
  }

  if (have_csv_flag && have_generate_flag) {
    error = "--csv and --generate cannot both be given";
    return std::nullopt;
  }

  if (cfg.mode == SimConfig::Mode::kUnset) {
    error = "no data source: use --csv/--generate or set source.mode in the config file";
    return std::nullopt;
  }
  if (cfg.mode == SimConfig::Mode::kCsv && cfg.csv_path.empty()) {
    error = "csv mode requires a csv_path (--csv PATH or source.csv_path in the config file)";
    return std::nullopt;
  }
  if (cfg.mode == SimConfig::Mode::kGenerate && !validGenerateCount(cfg.generate_count)) {
    error =
        "generate mode requires generate_count in [1,5000] (--generate N or "
        "source.generate_count in the config file)";
    return std::nullopt;
  }

  // DIS/OLV2 default port: when nothing set a port explicitly, follow the
  // backend's distinct defaults (47001 DIS, 47002 OLV2, vs OLV1's 47000) so
  // `olv_sim --protocol dis|olv2` reaches the matching `olv_backend
  // --input-mode` with both sides on defaults.
  if (cfg.protocol == SimConfig::Protocol::kDis && !cfg.dest_port_set) {
    cfg.dest_port = 47001;
  } else if (cfg.protocol == SimConfig::Protocol::kOlv2 && !cfg.dest_port_set) {
    cfg.dest_port = 47002;
  }

  return cfg;
}

void printSimUsage(const char* argv0) {
  std::cout
      << "Usage: " << argv0 << " (--csv PATH | --generate N) [options]\n"
      << "       " << argv0 << " --config PATH [options]\n"
      << "\n"
      << "Data source (exactly one required, from flags and/or config file):\n"
      << "  --csv PATH        Replay a mission CSV file.\n"
      << "  --generate N      Synthesize N tracked objects (deterministic), N in [1,5000].\n"
      << "\n"
      << "Options:\n"
      << "  --config PATH     Load a TOML config file (see config/simulator.toml).\n"
      << "  --dest IP         Destination address (default 127.0.0.1).\n"
      << "  --port N          Destination UDP port (default 47000; 47001 with --protocol dis;\n"
      << "                    47002 with --protocol olv2).\n"
      << "  --rate HZ         Cycles/second, 0 < HZ <= 50 (default 1.0).\n"
      << "  --loop            CSV mode only: wrap to the first frame and keep going.\n"
      << "  --chunk N         Max objects per packet, [1,128] (default 128; olv1 only).\n"
      << "  --protocol P      Wire protocol: \"olv1\" (default), \"dis\" (IEEE 1278.1 Entity\n"
      << "                    State PDUs), or \"olv2\" (per-track batched updates,\n"
      << "                    olv2_points per datagram); dis_*/olv2_* settings come from\n"
      << "                    the config file.\n"
      << "  --duration S      Generate mode only: run S seconds; 0 = forever (default 120).\n"
      << "  --seed N          Generate mode only: RNG seed (default 1).\n"
      << "  --quiet           Suppress per-cycle status lines.\n"
      << "  --help            Print this message and exit.\n"
      << "\n"
      << "Precedence (lowest to highest): built-in defaults < --config TOML file <\n"
      << "explicit command-line flags (applied regardless of argument order). See\n"
      << "config/simulator.toml for a commented example config file.\n";
}

}  // namespace olv::sim
