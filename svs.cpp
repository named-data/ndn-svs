#include <chrono>
#include <iostream>
#include <ndn-cxx/interest-filter.hpp>
#include <random>

#include "svs.hpp"

namespace ndn {
namespace svs {

/**
 * run() - Start event loop. Called by the application.
 */
void SVS::run() { 
  // Start periodically send sync interest
  retxSyncInterest();
  
  // Start periodically send packets asynchronously
  asyncSendPacket();

  // Enter event loop
  m_face.processEvents(); 
}

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
  for (int i = 0; i < msg.length(); ++i) contentBuf.push_back((uint8_t)msg[i]);
  data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());
  m_keyChain.sign(
      *data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
  data->setFreshnessPeriod(time::milliseconds(4000));

  m_data_store[n] = data;
  // TODO: Send sync interest immediately
  sendSyncInterest();
}

/**
 * asyncSendPacket() - Send one pending packet with highest priority. Schedule
 *  sending next packet with random delay.
 */
void SVS::asyncSendPacket() {
  if (pending_ack.size() > 0 || pending_sync_interest.size() > 0 ||
      pending_data_reply.size() > 0 ||
      pending_data_interest_forwarded.size() > 0 ||
      pending_data_interest.size() > 0) {
    // Decouple packet selection and packet sending
    Name n;
    Packet packet;
    if (pending_ack.size() > 0) {
      packet = pending_ack.front();
      pending_ack.pop_front();
    } else if (pending_data_reply.size() > 0) {
      packet = pending_data_reply.front();
      pending_data_reply.pop_front();
    } else if (pending_sync_interest.size() > 0) {
      packet = pending_sync_interest.front();
      pending_sync_interest.pop_front();
    } else if (pending_data_interest_forwarded.size() > 0) {
      packet = pending_data_interest_forwarded.front();
      pending_data_interest_forwarded.pop_front();
    } else {
      packet = pending_data_interest.front();
      pending_data_interest.pop_front();
    }

    // Send packet
    switch (packet.packet_type) {
      case Packet::INTEREST_TYPE:
        n = (packet.interest)->getName();

        // Data Interest
        if (n.compare(0, 2, kSyncDataPrefix) == 0) {
          // Drop falsy data interest
          if (m_data_store.find(n) != m_data_store.end()) {
            return asyncSendPacket();
          }

          m_face.expressInterest(
              *packet.interest, std::bind(&SVS::onDataReply, this, _2),
              std::bind(&SVS::onNack, this,  _1, _2),
              std::bind(&SVS::onTimeout, this,  _1));
        }

        // Sync Interest
        else if (n.compare(0, 2, kSyncNotifyPrefix) == 0) {
          m_face.expressInterest(
              *packet.interest, std::bind(&SVS::onSyncAck, this, _2),
              std::bind(&SVS::onNack, this,  _1, _2),
              std::bind(&SVS::onTimeout, this,  _1));
        }
        break;

      case Packet::DATA_TYPE:
        n = (packet.data)->getName();

        // Data Reply
        if (n.compare(0, 2, kSyncDataPrefix) == 0) {
          m_face.put(*packet.data);
        }

        // Sync Ack
        else if (n.compare(0, 2, kSyncNotifyPrefix) == 0) {
          m_face.put(*packet.data);
        }
        break;
    }
  }

  int delay = packet_dist(rengine_);
  m_scheduler.cancelEvent(packet_event);
  packet_event = m_scheduler.scheduleEvent(time::microseconds(delay), [this] {
    asyncSendPacket();
  });
}

/**
 * onSyncInterest() - Merge vector, send ack and schedule to forward next sync
 *  interest.
 */
void SVS::onSyncInterest(const Interest &interest) {
  const auto &n = interest.getName();
  NodeID nid_other = ExtractNodeID(n);
  printf("Received sync interest from node %llu: %s\n", nid_other, ExtractEncodedVV(n).c_str());
  fflush(stdout);

  // Merge state vector
  bool my_vector_new, other_vector_new;
  VersionVector vv_other;
  std::set<NodeID> interested_nodes;
  std::tie(vv_other, interested_nodes) =
      DecodeVVFromNameWithInterest(ExtractEncodedVV(n));
  std::tie(my_vector_new, other_vector_new) = mergeStateVector(vv_other);

  // If my vector newer, send ACK immediately. Otherwise send with random delay
  if (my_vector_new) {
    sendSyncACK(n);
  } else {
    int delay = packet_dist(rengine_);
    m_scheduler.scheduleEvent(time::microseconds(delay),
                              [this, n] { sendSyncACK(n); });
  }

  // If incoming state identical to local vector, reset timer to delay sending
  //  next sync interest.
  // If incoming state newer than local vector, send sync interest immediately.
  // If local state newer than incoming state, do nothing.
  if (!my_vector_new && !other_vector_new) {
    m_scheduler.cancelEvent(retx_event);
    int delay = retx_dist(rengine_);
    retx_event = m_scheduler.scheduleEvent(time::milliseconds(delay),
                              [this] { retxSyncInterest(); });
  } else if (other_vector_new) {
    retxSyncInterest();
  } else {
    // Do nothing
  }
}

/**
 * onDataInterest() -
 */
void SVS::onDataInterest(const Interest &interest) {}


/**
 * onSyncAck() - Decode version vector from data body, and merge vector.
 */
void SVS::onSyncAck(const Data &data) {
  
  // Extract content
  VersionVector vv_other;
  std::set<NodeID> interested_nodes;
  size_t data_size = data.getContent().value_size();
  std::string content_str((char *)data.getContent().value(), data_size);
  std::tie(vv_other, interested_nodes) =
      DecodeVVFromNameWithInterest(ExtractEncodedVV(content_str));
  
  printf("Received ack with content: %s", content_str.c_str());

  // Merge state vector
  mergeStateVector(vv_other);
}

/**
 * onDataReply() -
 */
void SVS::onDataReply(const Data &data) {}

/**
 * onNack() - Print error msg from NFD.
 */
void SVS::onNack(const Interest& interest, const lp::Nack& nack){
  std::cout << "received Nack with reason " << nack.getReason()
            << " for interest " << interest << std::endl;
}

/**
 * onTimeout() - Print timeout msg.
 */
void SVS::onTimeout(const Interest& interest){
  std::cout << "Timeout " << interest << std::endl;
}


/**
 * retxSyncInterest() - Cancel and schedule new retxSyncInterest event.
 */
void SVS::retxSyncInterest() {
  sendSyncInterest();
  int delay = retx_dist(rengine_);
  m_scheduler.cancelEvent(retx_event);
  retx_event = m_scheduler.scheduleEvent(time::microseconds(delay),
                                         [this] { retxSyncInterest(); });
}

/**
 * sendSyncInterest() - Add one sync interest to queue. Called by
 *  SVS::retxSyncInterest(), or directly.
 */
void SVS::sendSyncInterest() {
  using namespace std::chrono;

  // Append a timestamp to make name unique
  std::string encoded_vv = EncodeVVToNameWithInterest(
      m_vv, [](uint64_t id) -> bool { return true; });
  milliseconds cur_time_ms =
      duration_cast<milliseconds>(system_clock::now().time_since_epoch());
  auto pending_sync_notify =
      MakeSyncNotifyName(m_id, encoded_vv, cur_time_ms.count());
  
  printf("Send sync interest: %s\n", encoded_vv.c_str());
  fflush(stdout);

  // Wrap into Packet
  Packet packet;
  packet.packet_type = Packet::INTEREST_TYPE;
  packet.interest =
      std::make_shared<Interest>(pending_sync_notify, time::milliseconds(500));
  pending_sync_interest.clear();  // Flush sync interest queue
  pending_sync_interest.push_back(packet);
}

/**
 * sendSyncACK() - Add an ACK into queue
 */
void SVS::sendSyncACK(const Name &n) {
  // Set data name
  std::shared_ptr<Data> data = std::make_shared<Data>(n);

  // Set data content
  std::string encoded_vv = EncodeVVToNameWithInterest(
      m_vv, [](uint64_t id) -> bool { return true; });
  Buffer contentBuf;
  for (int i = 0; i < encoded_vv.size(); ++i)
    contentBuf.push_back((uint8_t)encoded_vv[i]);
  data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());
  m_keyChain.sign(
      *data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
  data->setFreshnessPeriod(time::milliseconds(4000));

  // Wrap into Packet
  Packet packet;
  packet.packet_type = Packet::DATA_TYPE;
  packet.data = data;
  pending_ack.push_back(packet);
}

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

}  // namespace svs
}  // namespace ndn