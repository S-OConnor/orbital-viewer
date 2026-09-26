// input_source.hpp — common interface for backend intake strategies.
//
// Exactly one InputSource is constructed and started per process, chosen at
// boot by Config::input_mode (docs/FEATURE_INPUT_SOURCES.md §4). Both
// implementations own their own io_context and receive thread (matching the
// pre-existing UdpReceiver threading model) and call
// StateStore::countReceived/countDropped/apply identically, so Stats mean
// the same thing regardless of mode.

#pragma once

#include <memory>
#include <string_view>

namespace olv {

class Logger;
class StateStore;
struct Config;

// Boot-time intake strategy selector (config [input] mode / --input-mode).
enum class InputMode {
  kOlv1,  // first-party OLV1 UDP binary protocol (default; docs/PROTOCOL_UDP.md)
  kDis,   // IEEE 1278.1 DIS Entity State PDUs (docs/PROTOCOL_DIS.md)
  kOlv2,  // OLV2 per-track batched UDP binary protocol (docs/PROTOCOL_OLV2.md)
};

// "olv1"/"dis"/"olv2" -> enum; returns false on any other string (strict, matching
// parseLogLevel's contract).
bool parseInputMode(std::string_view s, InputMode& out);

const char* toString(InputMode m);

class InputSource {
 public:
  virtual ~InputSource() = default;
  virtual void start() = 0;  // spawns the receive thread; no-op if already started
  virtual void stop() = 0;   // stops and joins; idempotent
};

// Constructs the concrete InputSource selected by cfg.input_mode. Throws
// std::runtime_error for modes that are configured but not built (kOlv2 until
// FEATURE_OLV2.md Phase 2 ships), and propagates the concrete type's constructor exceptions
// (e.g. bind failure) so startup errors surface before threads exist.
std::unique_ptr<InputSource> makeInputSource(const Config& cfg, StateStore& store, Logger& log);

}  // namespace olv
