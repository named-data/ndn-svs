#pragma once

#include <iostream>

#include "svs_common.hpp"
#include "svs_helper.hpp"

namespace ndn {
namespace svs {

class SVS {
public:
  SVS(NodeID id) : m_id(id) { m_vv[id] = 0; }

  void run();

  void registerPrefix();

  void publishMsg(const std::string &msg);

  void onMsg(const std::string &msg);

private:
  void onSyncInterest(const Interest &interest);

  void onDataInterest(const Interest &interest);

  NodeID m_id;
  Face m_face;
  KeyChain m_keyChain;
  VersionVector m_vv;
};

} // namespace svs
} // namespace ndn