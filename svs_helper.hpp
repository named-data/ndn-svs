#pragma once

#include <iostream>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>

#include "svs_common.hpp"

namespace ndn {
namespace svs {

/**
 * EncodeVVToNameWithInterest() - Encode version vector to name in format of :
 *  <NodeID>-<seq>-<interested>
 * Where interested is 0/1 indicating whether this node is interested in data
 *  produced by this NodeID.
 */
inline std::string EncodeVVToNameWithInterest(
    const VersionVector &v, std::function<bool(uint64_t)> is_important_data_) {
  std::string vv_encode = "";
  for (auto entry : v) {
    vv_encode += (to_string(entry.first) + "-" + to_string(entry.second) + "-");
    if (is_important_data_(entry.first))
      vv_encode += "1_";
    else
      vv_encode += "0_";
  }
  return vv_encode;
}

/**
 * DecodeVVFromNameWithInterest() - Given an encoded state vector encoded as:
 *  <NodeID>-<seq>-<interested>
 * Return the state vector, and a set of its interested nodes.
 */
inline std::pair<VersionVector, std::set<NodeID>> DecodeVVFromNameWithInterest(
    const std::string &vv_encode) {
  int start = 0;
  VersionVector vv;
  std::set<NodeID> interested_nodes;
  for (size_t i = 0; i < vv_encode.size(); ++i) {
    if (vv_encode[i] == '_') {
      std::string str = vv_encode.substr(start, i - start);
      size_t cursor_1 = str.find("-");
      size_t cursor_2 = str.find("-", cursor_1 + 1);
      NodeID nid = std::stoll(str.substr(0, cursor_1));
      uint64_t seq = std::stoll(str.substr(cursor_1 + 1, cursor_2));
      bool is_important = std::stoll(str.substr(cursor_2 + 1));
      if (is_important) interested_nodes.insert(nid);
      vv[nid] = seq;
      start = i + 1;
    }
  }
  return std::make_pair(vv, interested_nodes);
}

inline Name MakeSyncNotifyName(const NodeID &nid, std::string encoded_vv,
                               int64_t timestamp) {
  // name = /[syncNotify_prefix]/[nid]/[state-vector]/[heartbeat-vector]
  Name n(kSyncNotifyPrefix);
  n.appendNumber(nid).append(encoded_vv).appendNumber(timestamp);
  return n;
}

inline Name MakeDataName(const NodeID &nid, uint64_t seq) {
  // name = /[vsyncData_prefix]/[node_id]/[seq]/%0
  Name n(kSyncDataPrefix);
  n.appendNumber(nid).appendNumber(seq).appendNumber(0);
  return n;
}

inline uint64_t ExtractNodeID(const Name &n) { return n.get(-3).toNumber(); }

inline std::string ExtractEncodedVV(const Name &n) { return n.get(-2).toUri(); }

inline uint64_t ExtractSequence(const Name &n) { return n.get(-2).toNumber(); }

}  // namespace svs
}  // namespace ndn