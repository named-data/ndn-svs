#pragma once

#include <iostream>

#include "svs_common.hpp"
#include "svs_helper.hpp"

namespace ndn {
namespace svs {

class SVS {
public:
  SVS(NodeID id, std::function<void(const std::string &)> onMsg_)
      : m_id(id), onMsg(onMsg_) {
    m_vv[id] = 0;
  }

  void run();

  void registerPrefix();

  void publishMsg(const std::string &msg);

private:
  void onSyncInterest(const Interest &interest);

  void onDataInterest(const Interest &interest);

  NodeID m_id;
  Face m_face;
  KeyChain m_keyChain;
  VersionVector m_vv;
  std::function<void(const std::string &)> onMsg;
};

} // namespace svs
} // namespace ndn