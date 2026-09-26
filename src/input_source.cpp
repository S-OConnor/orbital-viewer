// input_source.cpp — see input_source.hpp. InputMode parsing/naming and the
// boot-time factory that main.cpp constructs its InputSource through.

#include "olv/input_source.hpp"

#include <stdexcept>

#include "olv/config.hpp"
#include "olv/dis_input_source.hpp"
#include "olv/olv1_input_source.hpp"
#include "olv/olv2_input_source.hpp"

namespace olv {

bool parseInputMode(std::string_view s, InputMode& out) {
  if (s == "olv1") {
    out = InputMode::kOlv1;
    return true;
  }
  if (s == "dis") {
    out = InputMode::kDis;
    return true;
  }
  if (s == "olv2") {
    out = InputMode::kOlv2;
    return true;
  }
  return false;
}

const char* toString(InputMode m) {
  switch (m) {
    case InputMode::kOlv1:
      return "olv1";
    case InputMode::kDis:
      return "dis";
    case InputMode::kOlv2:
      return "olv2";
  }
  return "unknown";
}

std::unique_ptr<InputSource> makeInputSource(const Config& cfg, StateStore& store, Logger& log) {
  switch (cfg.input_mode) {
    case InputMode::kOlv1:
      return std::make_unique<Olv1InputSource>(cfg.udp_bind, cfg.udp_port, store, log);
    case InputMode::kDis:
      return std::make_unique<DisInputSource>(cfg.dis, store, log);
    case InputMode::kOlv2:
      return std::make_unique<Olv2InputSource>(cfg.olv2, store, log);
  }
  throw std::runtime_error("unhandled input mode");
}

}  // namespace olv
