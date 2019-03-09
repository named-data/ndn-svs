#pragma once

#include <deque>
#include <iostream>
#include <ndn-cxx/util/scheduler.hpp>
#include <random>

#include "svs_common.hpp"
#include "svs_helper.hpp"

namespace ndn {
namespace svs {

class SVS {
 public:
  SVS(NodeID id, std::function<void(const std::string &)> onMsg_)
      : onMsg(onMsg_),
        m_id(id),
        m_scheduler(m_face.getIoService()),
        rengine_(rdevice_()) {
    // Bootstrap with knowledge of itself only
    m_vv[id] = 0;
  }

  void run();

  void registerPrefix();

  void publishMsg(const std::string &msg);

 private:
  void asyncSendPacket();

  void onSyncInterest(const Interest &interest);

  void onDataInterest(const Interest &interest);

  void onSyncAck(const Data &data);

  void onDataReply(const Data &data);

  void onNack(const Interest& interest, const lp::Nack& nack);

  void onTimeout(const Interest& interest);

  void retxSyncInterest();

  void sendSyncInterest();

  void sendSyncACK(const Name &n);

  std::pair<bool, bool> mergeStateVector(const VersionVector &vv_other);

  std::function<void(const std::string &)> onMsg;

  // Members
  NodeID m_id;
  Face m_face;
  KeyChain m_keyChain;
  VersionVector m_vv;
  Scheduler m_scheduler;  // Use io_service from face
  std::unordered_map<Name, std::shared_ptr<const Data>> m_data_store;

  // Mult-level queues
  std::deque<Packet> pending_ack;
  std::deque<Packet> pending_sync_interest;
  std::deque<Packet> pending_data_reply;
  std::deque<Packet> pending_data_interest_forwarded;
  std::deque<Packet> pending_data_interest;

  // Microseconds between sending two packets in the queues
  std::uniform_int_distribution<> packet_dist =
      std::uniform_int_distribution<>(10000, 15000);
  // Microseconds between sending two sync interests
  std::uniform_int_distribution<> retx_dist =
      std::uniform_int_distribution<>(1000000 * 0.9, 1000000 * 1.1);
  // Microseconds for sending ACK if local vector isn't newer
  std::uniform_int_distribution<> ack_dist =
      std::uniform_int_distribution<>(20000, 40000);
  // Random engine
  std::random_device rdevice_;
  std::mt19937 rengine_;

  // Events
  EventId retx_event;    // will send retx next sync intrest
  EventId packet_event;  // Will send next packet async
};

}  // namespace svs
}  // namespace ndn