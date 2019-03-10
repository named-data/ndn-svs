#pragma once

#include <cstdint>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>
#include <set>
#include <unordered_map>

namespace ndn {
namespace svs {

// Type and constant declarations for VectorSync
using NodeID = uint64_t;
using VersionVector = std::unordered_map<NodeID, uint64_t>;

static const Name kSyncNotifyPrefix = Name("/ndn/svs/syncNotify");
static const Name kSyncDataPrefix = Name("/ndn/svs/vsyncData");

typedef struct Packet_ {
  std::shared_ptr<const Interest> interest;
  std::shared_ptr<const Data> data;

  enum PacketType { INTEREST_TYPE, DATA_TYPE } packet_type;

  // // Define copy constructor to safely copy shared ptr
  // Packet_() : interest(nullptr), data(nullptr){};
  // Packet_(const Packet_ &c)
  //     : interest(c.interest), data(c.data), packet_type(c.packet_type) {}
} Packet;

}  // namespace svs
}  // namespace ndn