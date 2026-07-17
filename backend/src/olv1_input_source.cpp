// olv1_input_source.cpp — see olv1_input_source.hpp. Thread 1: private
// io_context on an internal std::thread running the async receive loop.
// Formerly udp_receiver.cpp; bodies and log message formats are unchanged.

#include "olv1_input_source.hpp"

#include <string>

#include "logger.hpp"

namespace olv {

namespace asio = boost::asio;
using asio::ip::udp;

Olv1InputSource::Olv1InputSource(const std::string& bind_address, std::uint16_t port,
                                 StateStore& store, Logger& log)
    : socket_(ioc_, udp::endpoint(asio::ip::make_address(bind_address), port)),
      store_(store),
      log_(log) {}

Olv1InputSource::~Olv1InputSource() {
  stop();
}

void Olv1InputSource::start() {
  if (started_) return;
  started_ = true;
  armReceive();
  thread_ = std::thread([this] { ioc_.run(); });
}

void Olv1InputSource::stop() {
  if (!started_) return;
  asio::post(ioc_, [this] {
    boost::system::error_code ec;
    socket_.close(ec);
    ioc_.stop();
  });
  if (thread_.joinable()) thread_.join();
  started_ = false;
}

void Olv1InputSource::armReceive() {
  socket_.async_receive_from(asio::buffer(buffer_), sender_,
                             [this](const boost::system::error_code& ec, std::size_t len) {
                               if (ec == asio::error::operation_aborted) return;
                               if (!ec) handleDatagram(len);
                               armReceive();
                             });
}

void Olv1InputSource::handleDatagram(std::size_t len) {
  store_.countReceived(len);

  const std::string from = sender_.address().to_string() + ":" + std::to_string(sender_.port());

  proto::StatePacket pkt;
  const proto::DecodeError err = proto::decode(buffer_.data(), len, pkt);
  if (err != proto::DecodeError::kNone) {
    store_.countDropped(DropKind::kMalformed);
    log_.warn("udp", std::string("dropped ") + proto::toString(err) +
                         " len=" + std::to_string(len) + " from=" + from);
    return;
  }

  const ApplyResult res =
      store_.apply(pkt, std::chrono::system_clock::now(), std::chrono::steady_clock::now());
  if (res == ApplyResult::kStaleSequence) {
    log_.debug("udp", "dropped stale seq=" + std::to_string(pkt.sequence) + " from=" + from);
    return;
  }
  log_.debug("udp", "accepted seq=" + std::to_string(pkt.sequence) +
                        " objs=" + std::to_string(pkt.objects.size()) +
                        " total=" + std::to_string(pkt.object_total) + " from=" + from);
}

}  // namespace olv
