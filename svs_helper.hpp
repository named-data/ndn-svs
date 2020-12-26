#pragma once

#include <iostream>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/util/string-helper.hpp>

#include "svs_common.hpp"

namespace ndn {
namespace svs {

/**
 * EncodeVVToNameWithInterest() - Encode version vector to name in format of :
 *  <NodeID>-<seq>-<interested>
 * Where interested is 0/1 indicating whether this node is interested in data
 *  produced by this NodeID.
 */
inline std::string
EncodeVVToNameWithInterest(const VersionVector &v)
{
  std::string vv_encode = "";
  for (auto entry : v) {
    vv_encode += (escape(entry.first) + "-" + to_string(entry.second) + "_");
  }
  return vv_encode;
}

/**
 * DecodeVVFromNameWithInterest() - Given an encoded state vector encoded as:
 *  <NodeID>-<seq>-<interested>
 * Return the state vector, and a set of its interested nodes.
 */
inline VersionVector
DecodeVVFromNameWithInterest(const std::string &vv_encode)
{
  int start = 0;
  VersionVector vv;

  for (size_t i = 0; i < vv_encode.size(); ++i) {
    if (vv_encode[i] == '_') {
      std::string str = vv_encode.substr(start, i - start);
      size_t cursor_1 = str.find("-");
      NodeID nid = unescape(str.substr(0, cursor_1));
      uint64_t seq = std::stoll(str.substr(cursor_1 + 1, i));
      vv[nid] = seq;
      start = i + 1;
    }
  }

  return vv;
}

inline std::string
ExtractNodeID(const Name &n)
{
  return unescape(n.get(-3).toUri());
}

inline std::string ExtractEncodedVV(const Name &n) { return n.get(-2).toUri(); }

inline uint64_t ExtractSequence(const Name &n) { return n.get(-2).toNumber(); }

}  // namespace svs
}  // namespace ndn