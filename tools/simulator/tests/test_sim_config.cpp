// test_sim_config.cpp — unit tests for sim_config.hpp (TOML config file +
// CLI parsing/precedence). OLV_TEST_MAIN() lives in test_csv_reader.cpp;
// this file links into the same olv_sim_tests binary.

#include "sim_config.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "olv_test.hpp"

namespace {

namespace fs = std::filesystem;
using olv::sim::SimConfig;

// Writes `content` to a fresh, uniquely-named temp file and returns its
// path; removed automatically when the wrapper goes out of scope.
struct TempConfig {
  fs::path path;

  explicit TempConfig(const std::string& content) {
    static int counter = 0;
    path =
        fs::temp_directory_path() / ("olv_sim_config_test_" + std::to_string(counter++) + ".toml");
    std::ofstream out(path, std::ios::trunc);
    out << content;
  }

  ~TempConfig() {
    std::error_code ec;
    fs::remove(path, ec);
  }

  std::string str() const { return path.string(); }
};

// Owns the argv storage for a parseSimArgs() call so the pointers stay valid.
struct Args {
  std::vector<std::string> storage;
  std::vector<const char*> argv;

  explicit Args(std::vector<std::string> args) : storage(std::move(args)) {
    argv.push_back("olv_sim");
    for (const std::string& a : storage) argv.push_back(a.c_str());
  }

  int argc() const { return static_cast<int>(argv.size()); }
  const char* const* data() const { return argv.data(); }
};

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

// ---- applySimConfigFile: valid files --------------------------------------

OLV_TEST(config_file_csv_mode_sets_every_field) {
  TempConfig cfg_file(R"(
[target]
host = "10.0.0.5"
port = 9000

[source]
mode = "csv"
csv_path = "mission.csv"
loop = true
generate_count = 42
duration_seconds = 30
seed = 7

[send]
rate_hz = 2.5
chunk = 64

[output]
quiet = true
)");

  SimConfig cfg;
  std::string error;
  OLV_CHECK(olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK_EQ(cfg.dest_host, std::string("10.0.0.5"));
  OLV_CHECK_EQ(cfg.dest_port, static_cast<std::uint16_t>(9000));
  OLV_CHECK(cfg.mode == SimConfig::Mode::kCsv);
  OLV_CHECK_EQ(cfg.csv_path, std::string("mission.csv"));
  OLV_CHECK(cfg.loop);
  OLV_CHECK_EQ(cfg.generate_count, 42);
  OLV_CHECK_EQ(cfg.duration_seconds, 30);
  OLV_CHECK_EQ(cfg.seed, static_cast<std::uint32_t>(7));
  OLV_CHECK_NEAR(cfg.rate_hz, 2.5, 1e-9);
  OLV_CHECK_EQ(cfg.chunk, 64);
  OLV_CHECK(cfg.quiet);
}

OLV_TEST(config_file_generate_mode_sets_every_field) {
  TempConfig cfg_file(R"(
[target]
host = "192.168.1.1"
port = 5000

[source]
mode = "generate"
generate_count = 250
duration_seconds = 0
seed = 99

[send]
rate_hz = 10
chunk = 1

[output]
quiet = false
)");

  SimConfig cfg;
  std::string error;
  OLV_CHECK(olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK_EQ(cfg.dest_host, std::string("192.168.1.1"));
  OLV_CHECK_EQ(cfg.dest_port, static_cast<std::uint16_t>(5000));
  OLV_CHECK(cfg.mode == SimConfig::Mode::kGenerate);
  OLV_CHECK_EQ(cfg.generate_count, 250);
  OLV_CHECK_EQ(cfg.duration_seconds, 0);
  OLV_CHECK_EQ(cfg.seed, static_cast<std::uint32_t>(99));
  OLV_CHECK_NEAR(cfg.rate_hz, 10.0, 1e-9);  // integer literal accepted for a float field
  OLV_CHECK_EQ(cfg.chunk, 1);
  OLV_CHECK(!cfg.quiet);
}

OLV_TEST(config_file_empty_leaves_defaults) {
  TempConfig cfg_file("# nothing here\n\n");

  SimConfig cfg;
  std::string error;
  OLV_CHECK(olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK_EQ(cfg.dest_host, std::string("127.0.0.1"));
  OLV_CHECK_EQ(cfg.dest_port, static_cast<std::uint16_t>(47000));
  OLV_CHECK(cfg.mode == SimConfig::Mode::kUnset);
  OLV_CHECK_EQ(cfg.csv_path, std::string(""));
  OLV_CHECK(!cfg.loop);
  OLV_CHECK_EQ(cfg.generate_count, 0);
  OLV_CHECK_EQ(cfg.duration_seconds, 120);
  OLV_CHECK_EQ(cfg.seed, static_cast<std::uint32_t>(1));
  OLV_CHECK_NEAR(cfg.rate_hz, 1.0, 1e-9);
  OLV_CHECK_EQ(cfg.chunk, 128);
  OLV_CHECK(!cfg.quiet);

  // With no mode from the file and no --csv/--generate, parseSimArgs must
  // still reject: a source mode is mandatory after merging.
  Args args({"--config", cfg_file.str()});
  std::string parse_error;
  auto result = olv::sim::parseSimArgs(args.argc(), args.data(), parse_error);
  OLV_CHECK(!result.has_value());
  OLV_CHECK(contains(parse_error, "no data source"));
}

// ---- parseSimArgs: precedence ---------------------------------------------

OLV_TEST(config_file_precedence_cli_flag_wins) {
  TempConfig cfg_file(R"(
[source]
mode = "csv"
csv_path = "x.csv"

[send]
rate_hz = 5.0
)");

  {
    Args args({"--config", cfg_file.str(), "--rate", "2"});
    std::string error;
    auto result = olv::sim::parseSimArgs(args.argc(), args.data(), error);
    OLV_CHECK(result.has_value());
    if (result) {
      OLV_CHECK_NEAR(result->rate_hz, 2.0, 1e-9);
      OLV_CHECK(result->mode == SimConfig::Mode::kCsv);
      OLV_CHECK_EQ(result->csv_path, std::string("x.csv"));
    }
  }

  // Flags positioned before --config on the command line still win.
  {
    Args args({"--rate", "2", "--config", cfg_file.str()});
    std::string error;
    auto result = olv::sim::parseSimArgs(args.argc(), args.data(), error);
    OLV_CHECK(result.has_value());
    if (result) OLV_CHECK_NEAR(result->rate_hz, 2.0, 1e-9);
  }
}

OLV_TEST(config_file_mode_overridden_by_cli_csv) {
  TempConfig cfg_file(R"(
[source]
mode = "generate"
generate_count = 10
)");

  Args args({"--config", cfg_file.str(), "--csv", "other.csv"});
  std::string error;
  auto result = olv::sim::parseSimArgs(args.argc(), args.data(), error);
  OLV_CHECK(result.has_value());
  if (result) {
    OLV_CHECK(result->mode == SimConfig::Mode::kCsv);
    OLV_CHECK_EQ(result->csv_path, std::string("other.csv"));
  }
}

OLV_TEST(config_file_mode_overridden_by_cli_generate) {
  TempConfig cfg_file(R"(
[source]
mode = "csv"
csv_path = "file.csv"
)");

  Args args({"--config", cfg_file.str(), "--generate", "50"});
  std::string error;
  auto result = olv::sim::parseSimArgs(args.argc(), args.data(), error);
  OLV_CHECK(result.has_value());
  if (result) {
    OLV_CHECK(result->mode == SimConfig::Mode::kGenerate);
    OLV_CHECK_EQ(result->generate_count, 50);
  }
}

OLV_TEST(cli_csv_and_generate_together_is_an_error) {
  Args args({"--csv", "a.csv", "--generate", "5"});
  std::string error;
  auto result = olv::sim::parseSimArgs(args.argc(), args.data(), error);
  OLV_CHECK(!result.has_value());
  OLV_CHECK(!error.empty());
}

// ---- applySimConfigFile: schema errors -------------------------------------

OLV_TEST(config_file_unknown_key_is_rejected) {
  TempConfig cfg_file("[target]\nhost = \"127.0.0.1\"\nbogus = 1\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "bogus"));
}

OLV_TEST(config_file_wrong_type_port_string) {
  TempConfig cfg_file("[target]\nport = \"x\"\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "target.port"));
}

OLV_TEST(config_file_wrong_type_loop_integer) {
  TempConfig cfg_file("[source]\nmode = \"csv\"\ncsv_path = \"a\"\nloop = 1\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "source.loop"));
}

OLV_TEST(config_file_wrong_type_mode_integer) {
  TempConfig cfg_file("[source]\nmode = 7\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "source.mode"));
}

OLV_TEST(config_file_port_out_of_range) {
  {
    TempConfig cfg_file("[target]\nport = 0\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "target.port"));
  }
  {
    TempConfig cfg_file("[target]\nport = 70000\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "target.port"));
  }
}

OLV_TEST(config_file_rate_hz_out_of_range) {
  {
    TempConfig cfg_file("[send]\nrate_hz = 0\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "send.rate_hz"));
  }
  {
    TempConfig cfg_file("[send]\nrate_hz = 51\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "send.rate_hz"));
  }
}

OLV_TEST(config_file_chunk_out_of_range) {
  {
    TempConfig cfg_file("[send]\nchunk = 0\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "send.chunk"));
  }
  {
    TempConfig cfg_file("[send]\nchunk = 129\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "send.chunk"));
  }
}

OLV_TEST(config_file_generate_count_out_of_range) {
  {
    TempConfig cfg_file("[source]\ngenerate_count = 0\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "source.generate_count"));
  }
  {
    TempConfig cfg_file("[source]\ngenerate_count = 5001\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "source.generate_count"));
  }
}

OLV_TEST(config_file_duration_negative_is_rejected) {
  TempConfig cfg_file("[source]\nduration_seconds = -1\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "source.duration_seconds"));
}

OLV_TEST(config_file_mode_banana_is_rejected) {
  TempConfig cfg_file("[source]\nmode = \"banana\"\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "source.mode"));
}

OLV_TEST(config_file_csv_mode_without_csv_path_is_rejected_after_merge) {
  TempConfig cfg_file("[source]\nmode = \"csv\"\n");

  // The file itself is schema-valid (csv_path is just an ordinary optional
  // string key), so applySimConfigFile succeeds...
  SimConfig cfg;
  std::string error;
  OLV_CHECK(olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(cfg.mode == SimConfig::Mode::kCsv);
  OLV_CHECK(cfg.csv_path.empty());

  // ...but parseSimArgs must reject the merged result: csv mode requires a
  // non-empty csv_path from somewhere (file or --csv).
  Args args({"--config", cfg_file.str()});
  std::string parse_error;
  auto result = olv::sim::parseSimArgs(args.argc(), args.data(), parse_error);
  OLV_CHECK(!result.has_value());
  OLV_CHECK(contains(parse_error, "csv_path"));
}

OLV_TEST(config_file_missing_file_error_mentions_path) {
  const std::string path = "/nonexistent/olv_sim_config_test_missing.toml";
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(path, cfg, error));
  OLV_CHECK(contains(error, path));
}

// ---- parseSimArgs: --config flag handling ----------------------------------

OLV_TEST(cli_repeated_config_flag_is_an_error) {
  Args args({"--config", "a.toml", "--config", "b.toml"});
  std::string error;
  auto result = olv::sim::parseSimArgs(args.argc(), args.data(), error);
  OLV_CHECK(!result.has_value());
  OLV_CHECK(contains(error, "--config"));
}

OLV_TEST(cli_config_flag_missing_value_is_an_error) {
  Args args({"--config"});
  std::string error;
  auto result = olv::sim::parseSimArgs(args.argc(), args.data(), error);
  OLV_CHECK(!result.has_value());
  OLV_CHECK(contains(error, "--config"));
}

// ---- [send] protocol / dis_* (docs/FEATURE_INPUT_SOURCES.md Phase 3) -------

OLV_TEST(config_file_send_protocol_and_dis_keys) {
  TempConfig cfg_file(
      "[source]\nmode = \"generate\"\ngenerate_count = 10\n"
      "[send]\nprotocol = \"dis\"\ndis_exercise_id = 7\ndis_site = 12\n"
      "dis_satellite_entity_id = \"3:5:9\"\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(cfg.protocol == SimConfig::Protocol::kDis);
  OLV_CHECK_EQ(cfg.dis_exercise_id, 7);
  OLV_CHECK_EQ(cfg.dis_site, 12);
  OLV_CHECK_EQ(cfg.dis_satellite_entity_id, std::string("3:5:9"));
}

OLV_TEST(config_file_send_protocol_banana_is_rejected) {
  TempConfig cfg_file("[send]\nprotocol = \"banana\"\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "send.protocol"));
}

OLV_TEST(config_file_dis_exercise_id_out_of_range) {
  TempConfig cfg_file("[send]\ndis_exercise_id = 256\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "send.dis_exercise_id"));
}

OLV_TEST(config_file_dis_site_out_of_range) {
  TempConfig cfg_file("[send]\ndis_site = 65536\n");
  SimConfig cfg;
  std::string error;
  OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
  OLV_CHECK(contains(error, "send.dis_site"));
}

OLV_TEST(config_file_dis_satellite_entity_id_bad_format) {
  for (const char* bad : {"\"1:2\"", "\"1:2:3:4\"", "\"a:b:c\"", "\"1::3\"", "\"1:2:99999\""}) {
    TempConfig cfg_file(std::string("[send]\ndis_satellite_entity_id = ") + bad + "\n");
    SimConfig cfg;
    std::string error;
    OLV_CHECK(!olv::sim::applySimConfigFile(cfg_file.str(), cfg, error));
    OLV_CHECK(contains(error, "send.dis_satellite_entity_id"));
  }
}

OLV_TEST(cli_protocol_flag_and_dis_default_port) {
  std::string error;
  {
    Args args({"--generate", "10", "--protocol", "dis"});
    auto cfg = olv::sim::parseSimArgs(args.argc(), args.data(), error);
    OLV_CHECK(cfg.has_value());
    OLV_CHECK(cfg->protocol == SimConfig::Protocol::kDis);
    OLV_CHECK_EQ(cfg->dest_port, 47001);  // DIS default when no port given
  }
  {
    Args args({"--generate", "10", "--protocol", "dis", "--port", "47123"});
    auto cfg = olv::sim::parseSimArgs(args.argc(), args.data(), error);
    OLV_CHECK(cfg.has_value());
    OLV_CHECK_EQ(cfg->dest_port, 47123);  // explicit port always wins
  }
  {
    Args args({"--generate", "10"});
    auto cfg = olv::sim::parseSimArgs(args.argc(), args.data(), error);
    OLV_CHECK(cfg.has_value());
    OLV_CHECK(cfg->protocol == SimConfig::Protocol::kOlv1);
    OLV_CHECK_EQ(cfg->dest_port, 47000);  // olv1 default unchanged
  }
}

OLV_TEST(cli_protocol_dis_respects_config_file_port) {
  TempConfig cfg_file(
      "[target]\nport = 48000\n[source]\nmode = \"generate\"\ngenerate_count = 5\n");
  std::string error;
  Args args({"--config", cfg_file.str(), "--protocol", "dis"});
  auto cfg = olv::sim::parseSimArgs(args.argc(), args.data(), error);
  OLV_CHECK(cfg.has_value());
  OLV_CHECK_EQ(cfg->dest_port, 48000);  // file-set port counts as explicit
}

OLV_TEST(cli_protocol_bad_value_is_rejected) {
  std::string error;
  Args args({"--generate", "10", "--protocol", "udp"});
  auto cfg = olv::sim::parseSimArgs(args.argc(), args.data(), error);
  OLV_CHECK(!cfg.has_value());
  OLV_CHECK(contains(error, "--protocol"));
}
