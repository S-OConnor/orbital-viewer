// test_csv_reader.cpp — unit tests for csv_reader.hpp (see that header for
// the CSV format contract). Also hosts OLV_TEST_MAIN() for the whole
// olv_sim_tests binary (test_frame_builder.cpp links into the same binary).

#include "csv_reader.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "frame_builder.hpp"
#include "olv/protocol.hpp"
#include "olv_test.hpp"

namespace {

constexpr const char* kHeader =
    "time_s,kind,id,type,px_m,py_m,pz_m,vx_mps,vy_mps,vz_mps,confidence,intensity,flags";

std::string joinRow(const std::vector<std::string>& fields) {
  std::string out;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i != 0) out += ',';
    out += fields[i];
  }
  return out;
}

std::string makeCsv(const std::vector<std::string>& data_rows) {
  std::string out = kHeader;
  out += '\n';
  for (const std::string& r : data_rows) {
    out += r;
    out += '\n';
  }
  return out;
}

// Parses a single malformed data row (after the header) and checks the
// error is reported at the given 1-based line number.
void expectErrorOnLine(const std::string& row, int expected_line) {
  std::istringstream in(makeCsv({row}));
  olv::sim::ParseResult r = olv::sim::parseCsv(in);
  OLV_CHECK(!static_cast<bool>(r));
  if (r.error) {
    OLV_CHECK_EQ(r.error->line, expected_line);
  }
}

}  // namespace

OLV_TEST(csv_happy_path) {
  const std::vector<std::string> rows = {
      joinRow({"0", "sat", "1", "unused", "6921000", "0", "0", "0", "0", "7500", "", "", ""}),
      joinRow({"1", "obj", "10", "debris", "100000", "200000", "300000", "10", "20", "30", "90",
               "0", "0"}),
      joinRow({"1", "obj", "11", "2", "1000000000000", "0", "0", "", "", "", "", "", ""}),
      joinRow({"1", "obj", "12", "ground_hot", "6371000", "0", "0", "", "", "", "80", "900", "2"}),
  };
  std::istringstream in(makeCsv(rows));
  olv::sim::ParseResult r = olv::sim::parseCsv(in);
  OLV_CHECK(static_cast<bool>(r));
  OLV_CHECK_EQ(r.rows.size(), std::size_t{4});
  if (r.rows.size() != 4) return;

  const olv::sim::CsvRow& sat = r.rows[0];
  OLV_CHECK(sat.is_sat);
  OLV_CHECK_EQ(sat.rec.id, 1u);
  OLV_CHECK_NEAR(sat.rec.px, 6921000.0, 1e-6);
  OLV_CHECK(sat.rec.hasVelocity());
  OLV_CHECK_NEAR(sat.rec.vz, 7500.0, 1e-3);

  const olv::sim::CsvRow& debris = r.rows[1];
  OLV_CHECK(!debris.is_sat);
  OLV_CHECK_EQ(debris.rec.type, static_cast<std::uint8_t>(olv::proto::ObjectType::kDebris));
  OLV_CHECK(debris.rec.hasVelocity());
  OLV_CHECK_EQ(debris.rec.confidence, static_cast<std::uint8_t>(90));

  const olv::sim::CsvRow& star = r.rows[2];  // numeric type "2" == kStar
  OLV_CHECK(!star.is_sat);
  OLV_CHECK_EQ(star.rec.type, static_cast<std::uint8_t>(olv::proto::ObjectType::kStar));
  OLV_CHECK(!star.rec.hasVelocity());                                 // empty vx/vy/vz
  OLV_CHECK_EQ(star.rec.confidence, static_cast<std::uint8_t>(100));  // empty => default 100
  OLV_CHECK_NEAR(star.rec.intensity, 0.0, 1e-6);                      // empty => default 0

  const olv::sim::CsvRow& gh = r.rows[3];
  OLV_CHECK_EQ(gh.rec.type, static_cast<std::uint8_t>(olv::proto::ObjectType::kGroundHot));
  OLV_CHECK_EQ(gh.rec.flags, olv::proto::kFlagHighlight);  // bit1 set, no velocity => bit0 unset
}

OLV_TEST(csv_missing_column) {
  // Only 12 fields (flags column dropped).
  expectErrorOnLine(
      joinRow({"0", "sat", "1", "unused", "6921000", "0", "0", "0", "0", "7500", "", ""}), 2);
}

OLV_TEST(csv_non_numeric_field) {
  expectErrorOnLine(joinRow({"1", "obj", "10", "debris", "abc", "0", "0", "", "", "", "", "", ""}),
                    2);
}

OLV_TEST(csv_bad_kind) {
  expectErrorOnLine(joinRow({"1", "wat", "10", "debris", "0", "0", "0", "", "", "", "", "", ""}),
                    2);
}

OLV_TEST(csv_bad_type_name) {
  expectErrorOnLine(joinRow({"1", "obj", "10", "spaceship", "0", "0", "0", "", "", "", "", "", ""}),
                    2);
}

OLV_TEST(csv_type_out_of_range) {
  expectErrorOnLine(joinRow({"1", "obj", "10", "6", "0", "0", "0", "", "", "", "", "", ""}), 2);
}

OLV_TEST(csv_confidence_out_of_range) {
  expectErrorOnLine(joinRow({"1", "obj", "10", "debris", "0", "0", "0", "", "", "", "101", "", ""}),
                    2);
}

OLV_TEST(csv_partial_velocity) {
  // vx set, vy/vz empty.
  expectErrorOnLine(joinRow({"1", "obj", "10", "debris", "0", "0", "0", "5", "", "", "", "", ""}),
                    2);
}

OLV_TEST(csv_bad_flags_has_velocity_bit) {
  // bit0 (has-velocity) must never be set directly in the flags column.
  expectErrorOnLine(joinRow({"1", "obj", "10", "debris", "0", "0", "0", "", "", "", "", "", "1"}),
                    2);
}

OLV_TEST(csv_error_reports_correct_line_among_many) {
  const std::vector<std::string> rows = {
      joinRow({"0", "sat", "1", "unused", "0", "0", "0", "0", "0", "0", "", "", ""}),
      joinRow({"1", "obj", "10", "debris", "0", "0", "0", "", "", "", "", "", ""}),
      joinRow({"2", "obj", "11", "debris", "0", "0", "0", "", "", "", "999", "", ""}),  // bad
  };
  // header=line1, row0=line2, row1=line3, bad row=line4.
  std::istringstream in(makeCsv(rows));
  olv::sim::ParseResult r = olv::sim::parseCsv(in);
  OLV_CHECK(!static_cast<bool>(r));
  if (r.error) OLV_CHECK_EQ(r.error->line, 4);
}

OLV_TEST(csv_comments_and_blank_lines_skipped) {
  std::string csv = std::string(kHeader) + "\n" + "# a leading comment\n" + "\n" + "   \n" +
                    "0,sat,1,unused,6921000,0,0,0,0,7500,,,\n" + "# a trailing comment\n";
  std::istringstream in(csv);
  olv::sim::ParseResult r = olv::sim::parseCsv(in);
  OLV_CHECK(static_cast<bool>(r));
  OLV_CHECK_EQ(r.rows.size(), std::size_t{1});
}

OLV_TEST(csv_missing_header_is_an_error) {
  std::istringstream in("0,sat,1,unused,0,0,0,0,0,0,,,\n");
  olv::sim::ParseResult r = olv::sim::parseCsv(in);
  OLV_CHECK(!static_cast<bool>(r));
}

OLV_TEST(csv_example_mission_file_parses) {
  const std::filesystem::path csv_path =
      std::filesystem::path(__FILE__).parent_path() / ".." / "data" / "example_mission.csv";
  std::ifstream in(csv_path);
  OLV_CHECK(static_cast<bool>(in));
  if (!in) return;

  olv::sim::ParseResult r = olv::sim::parseCsv(in);
  OLV_CHECK(static_cast<bool>(r));
  if (r.error) {
    std::fprintf(stderr, "example_mission.csv:%d: %s\n", r.error->line, r.error->message.c_str());
  }
  OLV_CHECK(r.rows.size() > 1000);

  olv::sim::GroupResult grouped = olv::sim::buildFrames(r.rows);
  OLV_CHECK(static_cast<bool>(grouped));
  OLV_CHECK(!grouped.frames.empty());
  if (!grouped.frames.empty()) {
    OLV_CHECK(grouped.frames.front().sat.id != 0);
  }
}

OLV_TEST_MAIN()
