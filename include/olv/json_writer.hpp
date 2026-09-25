// json_writer.hpp — Snapshot -> WebSocket JSON text.
//
// Produces exactly the messages specified in docs/PROTOCOL_WS.md. All values
// are numeric, ISO-8601 strings generated internally, or fixed literals, so
// no JSON string escaping is required (documented simplification; revisit if
// string fields are ever added to the protocol).
//
// Formatting rules: positions %.1f (0.1 m), velocities %.2f, intensity %.1f,
// udpRateHz %.2f. Objects are 11-column row arrays; velocity columns are
// `null` when the record has no HAS_VELOCITY flag.

#pragma once

#include <chrono>
#include <string>

#include "olv/state_store.hpp"

namespace olv {

// The 1 Hz broadcast payload. `ws_clients` is provided by the caller
// (WsServer) since StateStore does not know about sessions.
std::string buildStateMessage(const Snapshot& snap,
                              std::chrono::system_clock::time_point server_time, int ws_clients);

// Sent once per client right after the WebSocket handshake.
std::string buildHelloMessage(std::chrono::system_clock::time_point server_time,
                              double broadcast_hz);

}  // namespace olv
