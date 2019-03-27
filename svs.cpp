#include <boost/lexical_cast.hpp>
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
  printf(">> %s\n\n", msg.c_str());
  fflush(stdout);

  m_vv[m_id]++;

  // Set data name
  auto n = MakeDataName(m_id, m_vv[m_id]);
  std::shared_ptr<Data> data = std::make_shared<Data>(n);

  // Set data content
  Buffer contentBuf;
  for (size_t i = 0; i < msg.length(); ++i)
    contentBuf.push_back((uint8_t)msg[i]);
  data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());
  m_keyChain.sign(
      *data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
  data->setFreshnessPeriod(time::milliseconds(1000));

  m_data_store[n] = data;

  sendSyncInterest();
}

/**
 * asyncSendPacket() - Send one pending packet with highest priority. Schedule
 *  sending next packet with random delay.
 */
void SVS::asyncSendPacket() {
  // Decouple packet selection and packet sending
  Name n;
  std::shared_ptr<Packet> packet;
  pending_sync_interest_mutex.lock();
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
  } else if (pending_data_interest.size() > 0) {
    packet = pending_data_interest.front();
    pending_data_interest.pop_front();
  }
  pending_sync_interest_mutex.unlock();

  if (packet != nullptr) {
    // Send packet
    switch (packet->packet_type) {
      case Packet::INTEREST_TYPE:
        n = packet->interest->getName();

        // Data Interest
        if (n.compare(0, 3, kSyncDataPrefix) == 0) {
          // Drop falsy data interest
          if (m_data_store.find(n) != m_data_store.end()) {
            return asyncSendPacket();
          }

          m_face.expressInterest(*packet->interest,
                                 std::bind(&SVS::onDataReply, this, _2),
                                 std::bind(&SVS::onNack, this, _1, _2),
                                 std::bind(&SVS::onTimeout, this, _1));
          pending_data_interest.push_back(packet);
          // printf("Send data interest\n");
        }

        // Sync Interest
        else if (n.compare(0, 3, kSyncNotifyPrefix) == 0) {
          m_face.expressInterest(*packet->interest,
                                 std::bind(&SVS::onSyncAck, this, _2),
                                 std::bind(&SVS::onNack, this, _1, _2),
                                 std::bind(&SVS::onTimeout, this, _1));
          fflush(stdout);
        }

        else {
          std::cout << "Invalid name: " << n << std::endl;
          // assert(0);
        }

        break;

      case Packet::DATA_TYPE:
        n = packet->data->getName();

        // Data Reply
        if (n.compare(0, 3, kSyncDataPrefix) == 0) {
          m_face.put(*packet->data);
        }

        // Sync Ack
        else if (n.compare(0, 3, kSyncNotifyPrefix) == 0) {
          m_face.put(*packet->data);
        }

        else
          assert(0);

        break;

      default:
        assert(0);
    }
  }

  int delay = packet_dist(rengine_);
  m_scheduler.cancelEvent(packet_event);
  packet_event = m_scheduler.scheduleEvent(time::microseconds(delay),
                                           [this] { asyncSendPacket(); });
}

/**
 * onSyncInterest() - Merge vector, send ack and schedule to forward next sync
 *  interest.
 */
void SVS::onSyncInterest(const Interest &interest) {
  const auto &n = interest.getName();
  NodeID nid_other = ExtractNodeID(n);

  if (nid_other == m_id) return;

  // printf("Received sync interest from node %llu: %s\n", nid_other,
  //        ExtractEncodedVV(n).c_str());
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
    // printf("Delay next sync interest\n");
    fflush(stdout);
    m_scheduler.cancelEvent(retx_event);
    int delay = retx_dist(rengine_);
    retx_event = m_scheduler.scheduleEvent(time::microseconds(delay),
                                           [this] { retxSyncInterest(); });
  } else if (other_vector_new) {
    // printf("Send next sync interest immediately\n");
    fflush(stdout);
    m_scheduler.cancelEvent(retx_event);
    retxSyncInterest();
  } else {
    // Do nothing
  }
}

/**
 * onDataInterest() -
 */
void SVS::onDataInterest(const Interest &interest) {
  const auto &n = interest.getName();
  auto iter = m_data_store.find(n);

  // If have data, reply. Otherwise forward with probability
  if (iter != m_data_store.end()) {
    Packet packet;
    packet.packet_type = Packet::DATA_TYPE;
    packet.data = iter->second;
    pending_data_reply.push_back(std::make_shared<Packet>(packet));
  }

  else {
    // TODO
  }
}

/**
 * onSyncAck() - Decode version vector from data body, and merge vector.
 */
void SVS::onSyncAck(const Data &data) {
  // Extract content
  VersionVector vv_other;
  std::set<NodeID> interested_nodes;
  size_t data_size = data.getContent().value_size();
  std::string content_str((char *)data.getContent().value(), data_size);
  // printf("Receive ACK: %s\n", content_str.c_str());
  fflush(stdout);
  std::tie(vv_other, interested_nodes) =
      DecodeVVFromNameWithInterest(content_str);

  // Merge state vector
  mergeStateVector(vv_other);
}

/**
 * onDataReply() - Save data to data store, and call application callback to
 *  pass the data northbound.
 */
void SVS::onDataReply(const Data &data) {
  const auto &n = data.getName();
  NodeID nid_other = ExtractNodeID(n);

  // Drop duplicate data
  if (m_data_store.find(n) != m_data_store.end()) return;

  // printf("Received data: %s\n", n.toUri().c_str());
  m_data_store[n] = data.shared_from_this();

  // Pass msg to application in format: <sender_id>:<content>
  size_t data_size = data.getContent().value_size();
  std::string content_str((char *)data.getContent().value(), data_size);
  content_str = boost::lexical_cast<std::string>(nid_other) + ":" + content_str;

  onMsg(content_str);
}

/**
 * onNack() - Print error msg from NFD.
 */
void SVS::onNack(const Interest &interest, const lp::Nack &nack) {
  // std::cout << "received Nack with reason "
  //           << " for interest " << interest << std::endl;
}

/**
 * onTimeout() - Print timeout msg.
 */
void SVS::onTimeout(const Interest &interest) {
  // std::cout << "Timeout " << interest << std::endl;
}

/**
 * retxSyncInterest() - Cancel and schedule new retxSyncInterest event.
 */
void SVS::retxSyncInterest() {
  sendSyncInterest();
  int delay = retx_dist(rengine_);
  retx_event = m_scheduler.scheduleEvent(time::microseconds(delay),
                                         [this] { retxSyncInterest(); });
}

/**
 * sendSyncInterest() - Add one sync interest to queue. Called by
 *  SVS::retxSyncInterest(), or directly. Because this function is
 *  also called upon new msg via PublishMsg(), the shared data 
 *  structures could cause race conditions.
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

  // printf("Send sync interest: %s\n", encoded_vv.c_str());
  fflush(stdout);

  // Wrap into Packet
  Packet packet;
  packet.packet_type = Packet::INTEREST_TYPE;
  packet.interest =
      std::make_shared<Interest>(pending_sync_notify, time::milliseconds(1000));

  pending_sync_interest_mutex.lock();
  pending_sync_interest.clear();  // Flush sync interest queue
  pending_sync_interest.push_back(std::make_shared<Packet>(packet));
  pending_sync_interest_mutex.unlock();
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
  for (size_t i = 0; i < encoded_vv.size(); ++i)
    contentBuf.push_back((uint8_t)encoded_vv[i]);
  data->setContent(contentBuf.get<uint8_t>(), contentBuf.size());
  m_keyChain.sign(
      *data, security::SigningInfo(security::SigningInfo::SIGNER_TYPE_SHA256));
  data->setFreshnessPeriod(time::milliseconds(4000));

  // Wrap into Packet
  Packet packet;
  packet.packet_type = Packet::DATA_TYPE;
  packet.data = data;
  pending_ack.push_back(std::make_shared<Packet>(packet));
}

/**
 * mergeStateVector() - Merge state vector, return a pair of boolean
 *  representing: <my_vector_new, other_vector_new>.
 * Then, add missing data interests to data interest queue.
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
      for (auto seq = start_seq; seq <= seq_other; ++seq) {
        auto n = MakeDataName(nid_other, seq);
        Packet packet;
        packet.packet_type = Packet::INTEREST_TYPE;
        packet.interest =
            std::make_shared<Interest>(n, time::milliseconds(1000));
        pending_data_interest.push_back(std::make_shared<Packet>(packet));
      }

      // Merge local vector
      m_vv[nid_other] = seq_other;
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
