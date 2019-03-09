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

} // namespace svs
} // namespace ndn