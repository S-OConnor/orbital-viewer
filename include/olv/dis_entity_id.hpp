// dis_entity_id.hpp — the DIS "site:application:entity" id string format.
//
// Single source of truth for parsing the three-part DIS Entity ID notation
// used by backend config validation and DisInputSource (dis_satellite_entity_id,
// docs/FEATURE_INPUT_SOURCES.md §3.3/§6) and by the simulator's DIS emitter
// ([send] dis_satellite_entity_id). Lives in the shared olv_proto include tree
// so the accepted format cannot diverge between the two ends of the wire.

#pragma once

#include <cstdint>
#include <string_view>

namespace olv {

// Parses a "site:application:entity" string (three colon-separated decimal
// uint16s) into its parts. Returns false on any format violation: wrong field
// count, an empty field, a non-decimal character, or a value above 65535.
inline bool parseDisEntityId(std::string_view s, std::uint16_t& site, std::uint16_t& application,
                             std::uint16_t& entity) {
  std::uint16_t* out[3] = {&site, &application, &entity};
  std::size_t field = 0;
  std::size_t start = 0;
  while (true) {
    const std::size_t colon = s.find(':', start);
    const std::string_view tok =
        s.substr(start, colon == std::string_view::npos ? std::string_view::npos : colon - start);
    if (field >= 3) return false;  // more than three fields
    if (tok.empty()) return false;
    unsigned long v = 0;
    for (const char c : tok) {
      if (c < '0' || c > '9') return false;  // non-decimal
      v = v * 10 + static_cast<unsigned long>(c - '0');
      if (v > 65535) return false;  // out of uint16 range
    }
    *out[field++] = static_cast<std::uint16_t>(v);
    if (colon == std::string_view::npos) break;
    start = colon + 1;
  }
  return field == 3;  // exactly three fields
}

}  // namespace olv
