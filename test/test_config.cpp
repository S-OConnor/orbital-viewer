// test_config.cpp — applyConfigFile schema/precedence + parseArgs --config
// handling. No OLV_TEST_MAIN(); it lives in test_protocol.cpp.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <unistd.h>
#include <vector>

#include "olv/config.hpp"
#include "olv_test.hpp"

using namespace olv;

// Streamable for the test framework's failure reporter (ADL in olv).
namespace olv {
inline std::ostream& operator<<(std::ostream& os, LogLevel l) {
  return os << toString(l);
}
inline std::ostream& operator<<(std::ostream& os, InputMode m) {
  return os << toString(m);
}
}  // namespace olv

namespace {

namespace fs = std::filesystem;

// Writes `content` to a fresh file under a per-process temp directory and
// removes the whole directory on destruction. Kept simple: one directory per
// TempConfig instance, so instances never collide even within one test.
struct TempConfig {
  fs::path dir;
  fs::path file;

  explicit TempConfig(const std::string& content, const std::string& name = "cfg.toml") {
    static int counter = 0;
    dir = fs::temp_directory_path() /
          ("olv_cfg_test_" + std::to_string(::getpid()) + "_" + std::to_string(++counter));
    fs::create_directories(dir);
    file = dir / name;
    std::ofstream out(file, std::ios::binary);
    out << content;
  }

  ~TempConfig() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  std::string pathStr() const { return file.string(); }
};

// Builds a char*[] argv from string literals for parseArgs.
std::vector<const char*> makeArgv(std::initializer_list<const char*> args) {
  std::vector<const char*> argv;
  argv.push_back("olv_backend");
  for (const char* a : args) argv.push_back(a);
  return argv;
}

}  // namespace

// ---------------------------------------------------------------------------
// applyConfigFile: schema coverage.
// ---------------------------------------------------------------------------

OLV_TEST(config_full_valid_file_sets_every_field) {
  TempConfig tc(
      "[network]\n"
      "udp_bind = \"127.0.0.1\"\n"
      "udp_port = 47100\n"
      "ws_bind = \"127.0.0.2\"\n"
      "ws_port = 8800\n"
      "[broadcast]\n"
      "hz = 5.5\n"
      "[state]\n"
      "expiry_seconds = 42\n"
      "[logging]\n"
      "file = \"custom.log\"\n"
      "level = \"debug\"\n"
      "stderr = false\n");

  Config cfg;
  std::string err;
  OLV_CHECK(applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK_EQ(err, std::string());
  OLV_CHECK_EQ(cfg.udp_bind, std::string("127.0.0.1"));
  OLV_CHECK_EQ(cfg.udp_port, 47100);
  OLV_CHECK_EQ(cfg.ws_bind, std::string("127.0.0.2"));
  OLV_CHECK_EQ(cfg.ws_port, 8800);
  OLV_CHECK_NEAR(cfg.broadcast_hz, 5.5, 1e-9);
  OLV_CHECK_EQ(cfg.expiry_seconds, 42);
  OLV_CHECK_EQ(cfg.log_file, std::string("custom.log"));
  OLV_CHECK(cfg.log_level == LogLevel::kDebug);
  OLV_CHECK_EQ(cfg.log_stderr, false);
}

OLV_TEST(config_empty_file_changes_nothing) {
  TempConfig tc("");
  Config cfg;
  const Config def;
  std::string err;
  OLV_CHECK(applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK_EQ(cfg.udp_bind, def.udp_bind);
  OLV_CHECK_EQ(cfg.udp_port, def.udp_port);
  OLV_CHECK_EQ(cfg.ws_bind, def.ws_bind);
  OLV_CHECK_EQ(cfg.ws_port, def.ws_port);
  OLV_CHECK_NEAR(cfg.broadcast_hz, def.broadcast_hz, 1e-9);
  OLV_CHECK_EQ(cfg.expiry_seconds, def.expiry_seconds);
  OLV_CHECK_EQ(cfg.log_file, def.log_file);
  OLV_CHECK(cfg.log_level == def.log_level);
  OLV_CHECK_EQ(cfg.log_stderr, def.log_stderr);
}

OLV_TEST(config_unknown_key_in_known_table) {
  TempConfig tc("[network]\nbogus = 1\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("network.bogus") != std::string::npos);
}

OLV_TEST(config_unknown_table) {
  TempConfig tc("[nope]\nx = 1\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("nope.x") != std::string::npos);
}

OLV_TEST(config_wrong_type_udp_port) {
  TempConfig tc("[network]\nudp_port = \"hi\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("network.udp_port") != std::string::npos);
  OLV_CHECK(err.find("integer") != std::string::npos);
  OLV_CHECK(err.find("string") != std::string::npos);
}

OLV_TEST(config_wrong_type_logging_stderr) {
  TempConfig tc("[logging]\nstderr = 1\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("logging.stderr") != std::string::npos);
  OLV_CHECK(err.find("boolean") != std::string::npos);
  OLV_CHECK(err.find("integer") != std::string::npos);
}

OLV_TEST(config_wrong_type_broadcast_hz) {
  TempConfig tc("[broadcast]\nhz = \"fast\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("broadcast.hz") != std::string::npos);
  OLV_CHECK(err.find("string") != std::string::npos);
}

OLV_TEST(config_range_udp_port) {
  {
    TempConfig tc("[network]\nudp_port = 0\n");
    Config cfg;
    std::string err;
    OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
    OLV_CHECK(err.find("network.udp_port") != std::string::npos);
    OLV_CHECK(err.find("1-65535") != std::string::npos);
  }
  {
    TempConfig tc("[network]\nudp_port = 70000\n");
    Config cfg;
    std::string err;
    OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
    OLV_CHECK(err.find("network.udp_port") != std::string::npos);
    OLV_CHECK(err.find("1-65535") != std::string::npos);
  }
}

OLV_TEST(config_range_broadcast_hz) {
  {
    TempConfig tc("[broadcast]\nhz = 0\n");
    Config cfg;
    std::string err;
    OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
    OLV_CHECK(err.find("broadcast.hz") != std::string::npos);
  }
  {
    TempConfig tc("[broadcast]\nhz = 61\n");
    Config cfg;
    std::string err;
    OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
    OLV_CHECK(err.find("broadcast.hz") != std::string::npos);
  }
}

OLV_TEST(config_range_expiry_seconds) {
  {
    TempConfig tc("[state]\nexpiry_seconds = 0\n");
    Config cfg;
    std::string err;
    OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
    OLV_CHECK(err.find("state.expiry_seconds") != std::string::npos);
    OLV_CHECK(err.find("1-3600") != std::string::npos);
  }
  {
    TempConfig tc("[state]\nexpiry_seconds = 3601\n");
    Config cfg;
    std::string err;
    OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
    OLV_CHECK(err.find("state.expiry_seconds") != std::string::npos);
    OLV_CHECK(err.find("1-3600") != std::string::npos);
  }
}

OLV_TEST(config_invalid_log_level) {
  TempConfig tc("[logging]\nlevel = \"verbose\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("logging.level") != std::string::npos);
  OLV_CHECK(err.find("verbose") != std::string::npos);
}

OLV_TEST(config_broadcast_hz_accepts_integer) {
  TempConfig tc("[broadcast]\nhz = 2\n");
  Config cfg;
  std::string err;
  OLV_CHECK(applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK_NEAR(cfg.broadcast_hz, 2.0, 1e-9);
}

OLV_TEST(config_missing_file_reports_path) {
  Config cfg;
  std::string err;
  const std::string path = "/nonexistent/olv_missing_dir/olv_missing.toml";
  OLV_CHECK(!applyConfigFile(path, cfg, err));
  OLV_CHECK(err.find(path) != std::string::npos);
}

// ---------------------------------------------------------------------------
// parseArgs: --config integration and precedence.
// ---------------------------------------------------------------------------

OLV_TEST(config_precedence_flag_after_config_wins) {
  TempConfig tc("[network]\nudp_port = 1111\n");
  const std::string path = tc.pathStr();
  auto argv = makeArgv({"--config", path.c_str(), "--udp-port", "2222"});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(cfg.has_value());
  OLV_CHECK_EQ(cfg->udp_port, 2222);
}

OLV_TEST(config_precedence_flag_before_config_still_wins) {
  TempConfig tc("[network]\nudp_port = 1111\n");
  const std::string path = tc.pathStr();
  auto argv = makeArgv({"--udp-port", "2222", "--config", path.c_str()});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(cfg.has_value());
  OLV_CHECK_EQ(cfg->udp_port, 2222);
}

OLV_TEST(config_parse_args_bad_file_returns_nullopt_with_file_error) {
  auto argv = makeArgv({"--config", "/nonexistent/olv_missing_dir/olv_missing.toml"});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(!cfg.has_value());
  OLV_CHECK(err.find("olv_missing.toml") != std::string::npos);
}

OLV_TEST(config_parse_args_config_missing_value) {
  auto argv = makeArgv({"--config"});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(!cfg.has_value());
  OLV_CHECK(err.find("--config") != std::string::npos);
}

OLV_TEST(config_parse_args_repeated_config) {
  auto argv = makeArgv({"--config", "a.toml", "--config", "b.toml"});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(!cfg.has_value());
  OLV_CHECK(err.find("--config") != std::string::npos);
}

// ---------------------------------------------------------------------------
// [input] mode / --input-mode (FEATURE_INPUT_SOURCES.md §6).
// ---------------------------------------------------------------------------

OLV_TEST(config_input_mode_defaults_to_olv1) {
  Config cfg;
  OLV_CHECK_EQ(cfg.input_mode, InputMode::kOlv1);
}

OLV_TEST(config_input_mode_from_file) {
  TempConfig tc("[input]\nmode = \"dis\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK_EQ(cfg.input_mode, InputMode::kDis);
}

OLV_TEST(config_input_mode_olv1_in_file_is_noop) {
  TempConfig tc("[input]\nmode = \"olv1\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK_EQ(cfg.input_mode, InputMode::kOlv1);
}

OLV_TEST(config_input_mode_invalid_string_in_file) {
  TempConfig tc("[input]\nmode = \"tcp\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("input.mode") != std::string::npos);
  OLV_CHECK(err.find("tcp") != std::string::npos);
}

OLV_TEST(config_input_mode_wrong_type_in_file) {
  TempConfig tc("[input]\nmode = 1\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("input.mode") != std::string::npos);
}

OLV_TEST(config_input_mode_flag) {
  // --input-mode dis requires a satellite entity id, and there is no CLI flag
  // for it (§6), so the flag is paired with a config file that supplies one.
  TempConfig tc("[input]\ndis_satellite_entity_id = \"1:1:1\"\n");
  const std::string path = tc.pathStr();
  auto argv = makeArgv({"--config", path.c_str(), "--input-mode", "dis"});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(cfg.has_value());
  OLV_CHECK_EQ(cfg->input_mode, InputMode::kDis);
}

OLV_TEST(config_input_mode_flag_invalid_is_hard_error) {
  auto argv = makeArgv({"--input-mode", "udp"});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(!cfg.has_value());
  OLV_CHECK(err.find("input mode") != std::string::npos);
  OLV_CHECK(err.find("udp") != std::string::npos);
}

OLV_TEST(config_input_mode_flag_overrides_file) {
  TempConfig tc("[input]\nmode = \"dis\"\n");
  const std::string path = tc.pathStr();
  auto argv = makeArgv({"--config", path.c_str(), "--input-mode", "olv1"});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(cfg.has_value());
  OLV_CHECK_EQ(cfg->input_mode, InputMode::kOlv1);
}

// ---------------------------------------------------------------------------
// [input] dis_* keys (FEATURE_INPUT_SOURCES.md §6). Flat keys under [input]
// because the first-party TOML subset has single-level tables only.
// ---------------------------------------------------------------------------

OLV_TEST(config_dis_full_group_sets_every_field) {
  TempConfig tc(
      "[input]\n"
      "mode = \"dis\"\n"
      "dis_bind = \"127.0.0.9\"\n"
      "dis_port = 47500\n"
      "dis_exercise_id = 42\n"
      "dis_satellite_entity_id = \"10:20:30\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK_EQ(err, std::string());
  OLV_CHECK_EQ(cfg.input_mode, InputMode::kDis);
  OLV_CHECK_EQ(cfg.dis.bind_address, std::string("127.0.0.9"));
  OLV_CHECK_EQ(cfg.dis.port, 47500);
  OLV_CHECK(cfg.dis.exercise_id.has_value());
  OLV_CHECK_EQ(static_cast<int>(*cfg.dis.exercise_id), 42);
  OLV_CHECK_EQ(cfg.dis.satellite_entity_id, std::string("10:20:30"));
}

OLV_TEST(config_dis_defaults_when_absent) {
  TempConfig tc("[input]\nmode = \"olv1\"\n");
  Config cfg;
  const Config def;
  std::string err;
  OLV_CHECK(applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK_EQ(cfg.dis.bind_address, def.dis.bind_address);
  OLV_CHECK_EQ(cfg.dis.port, def.dis.port);
  OLV_CHECK(!cfg.dis.exercise_id.has_value());
  OLV_CHECK_EQ(cfg.dis.satellite_entity_id, std::string());
}

OLV_TEST(config_dis_exercise_id_range) {
  TempConfig tc("[input]\ndis_exercise_id = 256\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("input.dis_exercise_id") != std::string::npos);
  OLV_CHECK(err.find("0-255") != std::string::npos);
}

OLV_TEST(config_dis_port_range) {
  TempConfig tc("[input]\ndis_port = 0\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("input.dis_port") != std::string::npos);
  OLV_CHECK(err.find("1-65535") != std::string::npos);
}

OLV_TEST(config_dis_satellite_entity_id_bad_format) {
  TempConfig tc("[input]\ndis_satellite_entity_id = \"1:2\"\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("input.dis_satellite_entity_id") != std::string::npos);
}

OLV_TEST(config_dis_satellite_entity_id_wrong_type) {
  TempConfig tc("[input]\ndis_satellite_entity_id = 5\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("input.dis_satellite_entity_id") != std::string::npos);
  OLV_CHECK(err.find("string") != std::string::npos);
}

OLV_TEST(config_dis_mode_requires_satellite_entity_id) {
  // mode=dis with no satellite id is a hard error at the parseArgs level.
  TempConfig tc("[input]\nmode = \"dis\"\n");
  const std::string path = tc.pathStr();
  auto argv = makeArgv({"--config", path.c_str()});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(!cfg.has_value());
  OLV_CHECK(err.find("dis_satellite_entity_id") != std::string::npos);
}

OLV_TEST(config_dis_mode_with_satellite_id_ok) {
  TempConfig tc("[input]\nmode = \"dis\"\ndis_satellite_entity_id = \"1:1:1\"\n");
  const std::string path = tc.pathStr();
  auto argv = makeArgv({"--config", path.c_str()});
  std::string err;
  const auto cfg = parseArgs(static_cast<int>(argv.size()), argv.data(), err);
  OLV_CHECK(cfg.has_value());
  OLV_CHECK_EQ(cfg->input_mode, InputMode::kDis);
  OLV_CHECK_EQ(cfg->dis.satellite_entity_id, std::string("1:1:1"));
}

OLV_TEST(config_dis_unknown_key_rejected) {
  TempConfig tc("[input]\ndis_bogus = 1\n");
  Config cfg;
  std::string err;
  OLV_CHECK(!applyConfigFile(tc.pathStr(), cfg, err));
  OLV_CHECK(err.find("input.dis_bogus") != std::string::npos);
}
