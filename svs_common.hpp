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

typedef struct {
  std::shared_ptr<const Interest> interest;
  std::shared_ptr<const Data> data;

  enum PacketType { INTEREST_TYPE, DATA_TYPE } packet_type;
  enum SourceType {
    ORIGINAL,
    FORWARDED,
    SUPPRESSED
  } packet_origin; /* Used in data only */

  int64_t last_sent;
} Packet;

}  // namespace svs
}  // namespace ndn