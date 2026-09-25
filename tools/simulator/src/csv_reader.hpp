// csv_reader.hpp — CSV mission file parser for olv_sim.
//
// CSV FORMAT (contract; the committed example at tools/simulator/data/example_
// mission.csv follows this exactly):
//
//   Header row required, exactly:
//     time_s,kind,id,type,px_m,py_m,pz_m,vx_mps,vy_mps,vz_mps,confidence,
//     intensity,flags
//
//   - `kind`: "sat" (the primary satellite; `type` is ignored/unvalidated for
//     this row) or "obj" (a tracked object).
//   - `type`: a name (debris|star|comet|satellite|ground_hot) or the numeric
//     ObjectType value (1-5). Required and validated for kind=obj; ignored
//     for kind=sat.
//   - `px_m,py_m,pz_m`: required ECEF meters.
//   - `vx_mps,vy_mps,vz_mps`: either all three empty (record has no
//     velocity; HAS_VELOCITY flag unset, wire velocity sent as 0) or all
//     three present and numeric (HAS_VELOCITY set). Any other combination
//     ("partial velocity") is a parse error.
//   - `confidence`: integer 0-100; empty cell defaults to 100.
//   - `intensity`: float; empty cell defaults to 0.
//   - `flags`: numeric; empty cell defaults to 0. Only bit1 (value 2,
//     HIGHLIGHT) may be set in this column — do NOT set bit0 here, the
//     has-velocity bit is derived automatically from the velocity cells;
//     any other value is a parse error.
//   - Lines that are blank (whitespace-only) or whose first non-whitespace
//     character is '#' are skipped entirely (not counted as data rows, but
//     still counted toward line numbers for error reporting).
//   - Any malformed row rejects the WHOLE file: the parser returns a
//     CsvError carrying the 1-based source line number and a human-readable
//     reason; no partial row list is returned.
//
// This header/impl is pure std (no Boost) so it links into olv_sim_lib and
// is directly testable via std::istringstream.

#pragma once

#include <istream>
#include <optional>
#include <string>
#include <vector>

#include "olv/protocol.hpp"

namespace olv::sim {

// One parsed CSV data row. For kind=sat, `rec.id/px/py/pz/vx/vy/vz` carry the
// satellite state (type/flags/confidence/intensity are unused); for kind=obj
// all of `rec` is populated.
struct CsvRow {
  double time_s = 0.0;
  bool is_sat = false;
  proto::ObjectRecord rec;
};

struct CsvError {
  int line = 0;
  std::string message;
};

// parseCsv() result: either a full row list (error == nullopt) or a single
// error describing the first malformed line (rows is then empty).
struct ParseResult {
  std::vector<CsvRow> rows;
  std::optional<CsvError> error;

  explicit operator bool() const { return !error.has_value(); }
};

// Parses a mission CSV from `in` per the format documented above. Takes a
// stream (rather than a path) so unit tests can feed an in-memory
// std::istringstream directly.
ParseResult parseCsv(std::istream& in);

}  // namespace olv::sim
