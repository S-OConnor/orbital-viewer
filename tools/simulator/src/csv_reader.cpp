// csv_reader.cpp — see csv_reader.hpp for the CSV format contract.

#include "csv_reader.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace olv::sim {

namespace {

constexpr std::size_t kNumFields = 13;
constexpr const char* kHeaderLine =
    "time_s,kind,id,type,px_m,py_m,pz_m,vx_mps,vy_mps,vz_mps,confidence,intensity,flags";

std::string trim(const std::string& s) {
  auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
  auto begin = std::find_if(s.begin(), s.end(), notSpace);
  auto end = std::find_if(s.rbegin(), s.rend(), notSpace).base();
  if (begin >= end) return "";
  return std::string(begin, end);
}

std::vector<std::string> splitFields(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    std::size_t comma = line.find(',', start);
    if (comma == std::string::npos) {
      fields.push_back(trim(line.substr(start)));
      break;
    }
    fields.push_back(trim(line.substr(start, comma - start)));
    start = comma + 1;
  }
  return fields;
}

// Parses an integral value; rejects empty strings, partial parses, and
// trailing garbage (e.g. "3x").
template <typename T>
std::optional<T> parseIntegral(const std::string& s) {
  if (s.empty()) return std::nullopt;
  T v{};
  auto res = std::from_chars(s.data(), s.data() + s.size(), v);
  if (res.ec != std::errc() || res.ptr != s.data() + s.size()) return std::nullopt;
  return v;
}

// Parses a floating-point value; rejects empty strings, partial parses, and
// non-finite results (CSV mission data has no legitimate use for NaN/Inf,
// and the wire protocol would reject them anyway — better to fail early with
// a line number).
template <typename T>
std::optional<T> parseFloating(const std::string& s) {
  if (s.empty()) return std::nullopt;
  T v{};
  auto res = std::from_chars(s.data(), s.data() + s.size(), v);
  if (res.ec != std::errc() || res.ptr != s.data() + s.size() || !std::isfinite(v)) {
    return std::nullopt;
  }
  return v;
}

std::optional<std::uint8_t> typeFromName(const std::string& s) {
  static const std::unordered_map<std::string, std::uint8_t> kNames{
      {"debris", 1}, {"star", 2}, {"comet", 3}, {"satellite", 4}, {"ground_hot", 5}};
  auto it = kNames.find(s);
  if (it == kNames.end()) return std::nullopt;
  return it->second;
}

// Accepts either a numeric ObjectType value (1-5) or a known name.
std::optional<std::uint8_t> parseType(const std::string& s) {
  if (auto n = parseIntegral<int>(s)) {
    if (*n >= 1 && *n <= 5) return static_cast<std::uint8_t>(*n);
    return std::nullopt;  // numeric but out of range, e.g. "6" or "0"
  }
  return typeFromName(s);
}

}  // namespace

ParseResult parseCsv(std::istream& in) {
  ParseResult result;
  std::string line;
  int line_no = 0;
  bool have_header = false;

  auto fail = [&](int ln, std::string msg) {
    result.error = CsvError{ln, std::move(msg)};
    result.rows.clear();
  };

  while (std::getline(in, line)) {
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') continue;

    if (!have_header) {
      if (trimmed != kHeaderLine) {
        fail(line_no, "header row must be exactly: " + std::string(kHeaderLine));
        return result;
      }
      have_header = true;
      continue;
    }

    const std::vector<std::string> f = splitFields(line);
    if (f.size() != kNumFields) {
      fail(line_no, "expected " + std::to_string(kNumFields) + " columns, found " +
                        std::to_string(f.size()));
      return result;
    }

    CsvRow row;

    auto t = parseFloating<double>(f[0]);
    if (!t) {
      fail(line_no, "time_s: not a number: '" + f[0] + "'");
      return result;
    }
    row.time_s = *t;

    if (f[1] == "sat") {
      row.is_sat = true;
    } else if (f[1] == "obj") {
      row.is_sat = false;
    } else {
      fail(line_no, "kind must be 'sat' or 'obj', got '" + f[1] + "'");
      return result;
    }

    auto id = parseIntegral<std::uint32_t>(f[2]);
    if (!id) {
      fail(line_no, "id: not a valid non-negative integer: '" + f[2] + "'");
      return result;
    }
    row.rec.id = *id;

    if (row.is_sat) {
      row.rec.type = 0;  // ignored for kind=sat
    } else {
      auto type_val = parseType(f[3]);
      if (!type_val) {
        fail(line_no, "type: unrecognized value '" + f[3] +
                          "' (expected 1-5 or debris|star|comet|satellite|ground_hot)");
        return result;
      }
      row.rec.type = *type_val;
    }

    auto px = parseFloating<double>(f[4]);
    auto py = parseFloating<double>(f[5]);
    auto pz = parseFloating<double>(f[6]);
    if (!px) {
      fail(line_no, "px_m: not a number: '" + f[4] + "'");
      return result;
    }
    if (!py) {
      fail(line_no, "py_m: not a number: '" + f[5] + "'");
      return result;
    }
    if (!pz) {
      fail(line_no, "pz_m: not a number: '" + f[6] + "'");
      return result;
    }
    row.rec.px = *px;
    row.rec.py = *py;
    row.rec.pz = *pz;

    const bool vx_empty = f[7].empty();
    const bool vy_empty = f[8].empty();
    const bool vz_empty = f[9].empty();
    const int empty_count = (vx_empty ? 1 : 0) + (vy_empty ? 1 : 0) + (vz_empty ? 1 : 0);
    bool has_velocity = false;
    if (empty_count == 3) {
      has_velocity = false;
      row.rec.vx = row.rec.vy = row.rec.vz = 0.0f;
    } else if (empty_count == 0) {
      auto vx = parseFloating<float>(f[7]);
      auto vy = parseFloating<float>(f[8]);
      auto vz = parseFloating<float>(f[9]);
      if (!vx) {
        fail(line_no, "vx_mps: not a number: '" + f[7] + "'");
        return result;
      }
      if (!vy) {
        fail(line_no, "vy_mps: not a number: '" + f[8] + "'");
        return result;
      }
      if (!vz) {
        fail(line_no, "vz_mps: not a number: '" + f[9] + "'");
        return result;
      }
      has_velocity = true;
      row.rec.vx = *vx;
      row.rec.vy = *vy;
      row.rec.vz = *vz;
    } else {
      fail(line_no, "partial velocity: vx_mps/vy_mps/vz_mps must be all empty or all present");
      return result;
    }

    if (f[10].empty()) {
      row.rec.confidence = 100;
    } else {
      auto c = parseIntegral<int>(f[10]);
      if (!c || *c < 0 || *c > 100) {
        fail(line_no, "confidence: must be an integer 0-100, got '" + f[10] + "'");
        return result;
      }
      row.rec.confidence = static_cast<std::uint8_t>(*c);
    }

    if (f[11].empty()) {
      row.rec.intensity = 0.0f;
    } else {
      auto iv = parseFloating<float>(f[11]);
      if (!iv) {
        fail(line_no, "intensity: not a number: '" + f[11] + "'");
        return result;
      }
      row.rec.intensity = *iv;
    }

    std::uint8_t user_flags = 0;
    if (!f[12].empty()) {
      auto fl = parseIntegral<int>(f[12]);
      if (!fl || (*fl != 0 && *fl != static_cast<int>(proto::kFlagHighlight))) {
        fail(line_no, "flags: only 0 or " +
                          std::to_string(static_cast<int>(proto::kFlagHighlight)) +
                          " (highlight) may be set in CSV, got '" + f[12] + "'");
        return result;
      }
      user_flags = static_cast<std::uint8_t>(*fl);
    }
    row.rec.flags =
        static_cast<std::uint8_t>(user_flags | (has_velocity ? proto::kFlagHasVelocity : 0));

    result.rows.push_back(std::move(row));
  }

  if (!have_header) {
    fail(line_no == 0 ? 1 : line_no, "missing required header row");
    return result;
  }

  return result;
}

}  // namespace olv::sim
