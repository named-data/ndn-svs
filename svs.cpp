/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2020 University of California, Los Angeles
 *
 * This file is part of SVS, synchronization library for distributed realtime
 * applications for NDN.
 *
 * SVS is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * SVS is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * SVS, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <chrono>
#include <boost/lexical_cast.hpp>

#include <ndn-cxx/interest-filter.hpp>
#include <ndn-cxx/util/string-helper.hpp>

#include "svs.hpp"

namespace ndn {
namespace svs {

const ndn::Name Socket::DEFAULT_NAME;
const std::shared_ptr<Validator> Socket::DEFAULT_VALIDATOR;

Socket::Socket(const Name& syncPrefix,
               const Name& userPrefix,
               ndn::Face& face,
               const UpdateCallback& updateCallback,
               const Name& signingId,
               std::shared_ptr<Validator> validator)
  : m_syncPrefix(syncPrefix)
  , m_userPrefix(userPrefix)
  , m_signingId(signingId)
  , m_face(face)
  , m_validator(validator)
  , m_scheduler(m_face.getIoService())
{
  if (m_userPrefix != DEFAULT_NAME)
    m_registeredPrefixList[m_userPrefix] =
      m_face.setInterestFilter(m_userPrefix,
                               bind(&Socket::onDataInterest, this, _2),
                               [] (const Name& prefix, const std::string& msg) {});

  // Register sync interest filter
  m_registeredPrefixList[syncPrefix] =
    m_face.setInterestFilter(syncPrefix,
                             bind(&Socket::onSyncInterest, this, _2),
                             [] (const Name& prefix, const std::string& msg) {});

  // Start with self only
  m_id = m_userPrefix.toUri();
  m_vv[m_id] = 0;

  // Start periodically send sync interest
  retxSyncInterest();

  // Start periodically send packets asynchronously
  asyncSendPacket();
}

Socket::~Socket()
{
  for (auto& itr : m_registeredPrefixList) {
    itr.second.unregister();
  }
}

void Socket::publishMsg(const std::string &msg) {
  printf(">> %s\n\n", msg.c_str());
  fflush(stdout);

  m_vv[m_id]++;

  // Set data name
  auto n = MakeDataName(m_id, m_vv[m_id]);
  std::shared_ptr<Data> data = std::make_shared<Data>(n);
  // std::cout << "Sending data packet with name: " << n << std::endl;

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
 * sending next packet with random delay.
 */
void Socket::asyncSendPacket() {
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

  Interest interest;

  if (packet != nullptr) {
    // Send packet
    switch (packet->packet_type) {
      case Packet::INTEREST_TYPE:
        interest = Interest(*packet->interest);
        n = interest.getName();
        interest.setCanBePrefix(true);
        interest.setMustBeFresh(true);

        // Sync Interest
        if (m_syncPrefix.isPrefixOf(n)) {
          m_face.expressInterest(interest,
                                 std::bind(&Socket::onSyncAck, this, _2),
                                 std::bind(&Socket::onNack, this, _1, _2),
                                 std::bind(&Socket::onTimeout, this, _1));
          // std::cout << "Send sync interest: " << n << std::endl;
        }
        else
        {
          // Drop falsy data interest
          if (m_data_store.find(n) != m_data_store.end()) {
            return asyncSendPacket();
          }

          m_face.expressInterest(interest,
                                 std::bind(&Socket::onDataReply, this, _2),
                                 std::bind(&Socket::onNack, this, _1, _2),
                                 std::bind(&Socket::onTimeout, this, _1));
          // std::cout << "Send data interest: " << n << std::endl;
        }

        break;

      case Packet::DATA_TYPE:
        n = packet->data->getName();

        // Data Reply
        if (m_userPrefix.isPrefixOf(n)) {
          m_face.put(*packet->data);
        }

        // Sync Ack
        else if (m_syncPrefix.isPrefixOf(n)) {
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
  packet_event.cancel();
  packet_event = m_scheduler.schedule(time::microseconds(delay),
                                      [this] { asyncSendPacket(); });
}

void Socket::onSyncInterest(const Interest &interest) {
  const auto &n = interest.getName();
  NodeID nid_other = ExtractNodeID(n);
  // std::cout << "Receive sync interest: " << n << " from " << nid_other << std::endl;

  if (nid_other == m_id) return;

  // printf("Received sync interest from node %llu: %s\n", nid_other,
  //        ExtractEncodedVV(n).c_str());
  fflush(stdout);

  // Merge state vector
  bool my_vector_new, other_vector_new;
  VersionVector vv_other =
      DecodeVVFromNameWithInterest(ExtractEncodedVV(n));
  std::tie(my_vector_new, other_vector_new) = mergeStateVector(vv_other);

  // If my vector newer, send ACK immediately. Otherwise send with random delay
  if (my_vector_new) {
    sendSyncACK(n);
  } else {
    int delay = packet_dist(rengine_);
    m_scheduler.schedule(time::microseconds(delay),
                         [this, n] { sendSyncACK(n); });
  }

  // If incoming state identical to local vector, reset timer to delay sending
  //  next sync interest.
  // If incoming state newer than local vector, send sync interest immediately.
  // If local state newer than incoming state, do nothing.
  if (!my_vector_new && !other_vector_new) {
    // printf("Delay next sync interest\n");
    fflush(stdout);
    retx_event.cancel();
    int delay = retx_dist(rengine_);
    retx_event = m_scheduler.schedule(time::microseconds(delay),
                                      [this] { retxSyncInterest(); });
  } else if (other_vector_new) {
    // printf("Send next sync interest immediately\n");
    fflush(stdout);
    retx_event.cancel();
    retxSyncInterest();
  } else {
    // Do nothing
  }
}

void Socket::onDataInterest(const Interest &interest) {
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
void Socket::onSyncAck(const Data &data) {
  size_t data_size = data.getContent().value_size();
  std::string content_str((char *)data.getContent().value(), data_size);

  VersionVector vv_other = DecodeVVFromNameWithInterest(content_str);

  // Merge state vector
  mergeStateVector(vv_other);
}

/**
 * onDataReply() - Save data to data store, and call application callback to
 *  pass the data northbound.
 */
void Socket::onDataReply(const Data &data) {
  const auto &n = data.getName();
  NodeID nid_other = n.getPrefix(-1).toUri();
  // std::cout << "Receive data reply: " << n << std::endl;

  // Drop duplicate data
  if (m_data_store.find(n) != m_data_store.end()) return;

  // printf("Received data: %s\n", n.toUri().c_str());
  m_data_store[n] = data.shared_from_this();

  // Pass msg to application in format: <sender_id>:<content>
  size_t data_size = data.getContent().value_size();
  std::string content_str((char *)data.getContent().value(), data_size);
  content_str = boost::lexical_cast<std::string>(nid_other) + ":" + content_str;

  std::cout << content_str << std::endl;
  // onMsg(content_str);
}

/**
 * onNack() - Print error msg from NFD.
 */
void Socket::onNack(const Interest &interest, const lp::Nack &nack) {
}

/**
 * onTimeout() - Print timeout msg.
 */
void Socket::onTimeout(const Interest &interest) {
}

/**
 * retxSyncInterest() - Cancel and schedule new retxSyncInterest event.
 */
void Socket::retxSyncInterest() {
  sendSyncInterest();
  int delay = retx_dist(rengine_);
  retx_event = m_scheduler.schedule(time::microseconds(delay),
                                    [this] { retxSyncInterest(); });
}

/**
 * sendSyncInterest() - Add one sync interest to queue. Called by
 *  Socket::retxSyncInterest(), or directly. Because this function is
 *  also called upon new msg via PublishMsg(), the shared data
 *  structures could cause race conditions.
 */
void Socket::sendSyncInterest() {
  using namespace std::chrono;

  // Append a timestamp to make name unique
  std::string encoded_vv = EncodeVVToNameWithInterest(m_vv);
  milliseconds cur_time_ms =
      duration_cast<milliseconds>(system_clock::now().time_since_epoch());

  Name pending_sync_notify(m_syncPrefix);
  pending_sync_notify.append(escape(m_id)).append(encoded_vv).appendTimestamp();

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
void Socket::sendSyncACK(const Name &n) {
  // Set data name
  std::shared_ptr<Data> data = std::make_shared<Data>(n);

  // Set data content
  std::string encoded_vv = EncodeVVToNameWithInterest(m_vv);
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
std::pair<bool, bool>
Socket::mergeStateVector(const VersionVector &vv_other) {
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

Name
Socket::MakeDataName(const NodeID &nid, uint64_t seq) {
  Name n(nid);
  n.appendNumber(seq);
  return n;
}

}  // namespace svs
}  // namespace ndn
