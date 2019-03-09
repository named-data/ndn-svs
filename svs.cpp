#include <iostream>
#include <ndn-cxx/interest-filter.hpp>

#include "svs.hpp"

namespace ndn {
namespace svs {

void SVS::run() { m_face.processEvents(); }

void SVS::registerPrefix() {
  m_face.setInterestFilter(InterestFilter(kSyncNotifyPrefix),
                           bind(&SVS::onSyncInterest, this, _2), nullptr);
  m_face.setInterestFilter(InterestFilter(kSyncDataPrefix),
                           bind(&SVS::onDataInterest, this, _2), nullptr);
}

void SVS::publishMsg(const std::string &msg) {

  printf("Publishing data: %s\n", msg.c_str());
  fflush(stdout);

  m_vv[m_id]++;

  // Set data name
  auto n = MakeDataName(m_id, m_vv[m_id]);
  std::shared_ptr<Data> data = std::make_shared<Data>(n);

  // Set data content
  Buffer contentBuf;
  for (int i = 0; i < msg.length(); ++i)
    contentBuf.push_back((uint8_t)msg[i]);
  data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());
  m_keyChain.sign(
      *data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
  data->setFreshnessPeriod(time::milliseconds(4000));
  
  m_face.put(*data);
}

void SVS::onMsg(const std::string &msg) {}

void SVS::onSyncInterest(const Interest &interest) {}

void SVS::onDataInterest(const Interest &interest) {}

} // namespace svs
} // namespace ndn