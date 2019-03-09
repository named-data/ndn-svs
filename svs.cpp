#include <iostream>
#include <ndn-cxx/interest-filter.hpp>
#include <random>

#include "svs.hpp"

namespace ndn {
namespace svs {

/**
 * run() - Start event loop. Called by the application.
 */
void SVS::run() { m_face.processEvents(); }

/**
 * registerPrefix() - Called by the constructor.
 */
void SVS::registerPrefix() {
  m_face.setInterestFilter(InterestFilter(kSyncNotifyPrefix),
                           bind(&SVS::onSyncInterest, this, _2), nullptr);
  m_face.setInterestFilter(InterestFilter(kSyncDataPrefix),
                           bind(&SVS::onDataInterest, this, _2), nullptr);
}

/**
 * publishMsg() - Public method called by application to send new msg to the 
 *  sync layer. The sync layer will keep a copy.
 */
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

  m_data_store[n] = data;
  // TODO: Send sync interest
}

void SVS::onSyncInterest(const Interest &interest) {
  const auto &n = interest.getName();
  NodeID nid_other = ExtractNodeID(n);
  printf("Received sync interest from node %llu\n", nid_other);
  fflush(stdout);

  // Merge state vector
  bool my_vector_new, other_vector_new;
  VersionVector vv_other;
  std::set<NodeID> interested_nodes;
  std::tie(vv_other, interested_nodes) =
      DecodeVVFromNameWithInterest(ExtractEncodedVV(n));
  std::tie(my_vector_new, other_vector_new) = mergeStateVector(vv_other);

  // If my vector newer, send ACK immediately
  if (my_vector_new) {

  }

  // If incoming state not newer, reset timer to delay sending next sync
  //  interest
  if (other_vector_new) {
  }
}

void SVS::onDataInterest(const Interest &interest) {}

/**
 * mergeStateVector() - Merge state vector, return a pair of boolean
 *  representing: <my_vector_new, other_vector_new>.
 * TODO: Add missing data interests
 */
std::pair<bool, bool> SVS::mergeStateVector(const VersionVector &vv_other) {

  bool my_vector_new = false, other_vector_new = false;

  // Check if other vector has newer state
  for (auto entry : vv_other) {
    auto nid_other = entry.first;
    auto seq_other = entry.second;
    auto it = m_vv.find(nid_other);

    if (it == m_vv.end() || it->second < seq_other) {
      other_vector_new = true;
      // Detect new data
      auto start_seq =
          m_vv.find(nid_other) == m_vv.end() ? 1 : m_vv[nid_other] + 1;
      for (auto seq = start_seq; seq <= seq_other; ++seq)
        printf("Detect missing data: %llu-%llu", nid_other, seq);

      // Merge local vector
      m_vv[seq_other] = seq_other;
    }
  }

  // Check if I have newer state
  for (auto entry : m_vv) {
    auto nid = entry.first;
    auto seq = entry.second;
    auto it = vv_other.find(nid);

    if (it == vv_other.end() || it->second < seq) {
      my_vector_new = true;
      break;
    }
  }

  return std::make_pair(my_vector_new, other_vector_new);
}

} // namespace svs
} // namespace ndn