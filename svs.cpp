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

#include "svs.hpp"

#include <ndn-cxx/util/string-helper.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>

namespace ndn {
namespace svs {

const ndn::Name Socket::DEFAULT_NAME;
const ndn::Name Socket::DEFAULT_PREFIX;
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
  , m_onUpdate(updateCallback)
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

void
Socket::publishData(const uint8_t* buf, size_t len, const ndn::time::milliseconds& freshness,
                    const Name& prefix)
{
  publishData(ndn::encoding::makeBinaryBlock(ndn::tlv::Content, buf, len), freshness, prefix);
}

void
Socket::publishData(const uint8_t* buf, size_t len, const ndn::time::milliseconds& freshness,
                    const uint64_t& seqNo, const Name& prefix)
{
  publishData(ndn::encoding::makeBinaryBlock(ndn::tlv::Content, buf, len), freshness, seqNo, prefix);
}

void
Socket::publishData(const Block& content, const ndn::time::milliseconds& freshness,
                    const Name& prefix)
{
  shared_ptr<Data> data = make_shared<Data>();
  data->setContent(content);
  data->setFreshnessPeriod(freshness);

  SeqNo newSeq = ++m_vv[m_id];
  Name dataName;
  dataName.append(m_id).appendNumber(newSeq);
  data->setName(dataName);

  if (m_signingId.empty())
    m_keyChain.sign(*data);
  else
    m_keyChain.sign(*data, security::signingByIdentity(m_signingId));

  m_ims.insert(*data);
  sendSyncInterest();
}

void
Socket::publishData(const Block& content, const ndn::time::milliseconds& freshness,
                    const uint64_t& seqNo, const Name& prefix)
{
  shared_ptr<Data> data = make_shared<Data>();
  data->setContent(content);
  data->setFreshnessPeriod(freshness);

  SeqNo newSeq = seqNo;
  Name dataName;
  dataName.append(m_id).appendNumber(newSeq);
  data->setName(dataName);

  if (m_signingId.empty())
    m_keyChain.sign(*data);
  else
    m_keyChain.sign(*data, security::signingByIdentity(m_signingId));

  m_ims.insert(*data);
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
  } else if (pending_sync_interest.size() > 0) {
    packet = pending_sync_interest.front();
    pending_sync_interest.pop_front();
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
                                 std::bind(&Socket::onSyncNack, this, _1, _2),
                                 std::bind(&Socket::onSyncTimeout, this, _1));
        } else {
          NDN_THROW(Error("Invalid interest name: " + n.toUri()));
        }

        break;

      case Packet::DATA_TYPE:
        n = packet->data->getName();

        // Data Reply
        if (m_syncPrefix.isPrefixOf(n)) {
          m_face.put(*packet->data);
        }

        else {
          NDN_THROW(Error("Invalid data name: " + n.toUri()));
        }

        break;

      default:
        NDN_THROW(Error("Invalid packet type"));
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
    sendSyncAck(n);
  } else {
    int delay = packet_dist(rengine_);
    m_scheduler.schedule(time::microseconds(delay),
                         [this, n] { sendSyncAck(n); });
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
  // If have data, reply. Otherwise forward with probability (?)
  shared_ptr<const Data> data = m_ims.find(interest);
  if (data != nullptr) {
    m_face.put(*data);
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

void
Socket::fetchData(const NodeID& nid, const SeqNo& seqNo,
                  const DataValidatedCallback& dataCallback,
                  int nRetries)
{
  Name interestName;
  interestName.append(nid).appendNumber(seqNo);

  Interest interest(interestName);
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  DataValidationErrorCallback failureCallback =
    bind(&Socket::onDataValidationFailed, this, _1, _2);

  m_face.expressInterest(interest,
                         bind(&Socket::onData, this, _1, _2, dataCallback, failureCallback),
                         bind(&Socket::onDataTimeout, this, _1, nRetries,
                              dataCallback, failureCallback), // Nack
                         bind(&Socket::onDataTimeout, this, _1, nRetries,
                              dataCallback, failureCallback));
}

void
Socket::fetchData(const NodeID& nid, const SeqNo& seqNo,
                  const DataValidatedCallback& dataCallback,
                  const DataValidationErrorCallback& failureCallback,
                  const ndn::TimeoutCallback& onTimeout,
                  int nRetries)
{
  Name interestName;
  interestName.append(nid).appendNumber(seqNo);

  Interest interest(interestName);
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  m_face.expressInterest(interest,
                         bind(&Socket::onData, this, _1, _2, dataCallback, failureCallback),
                         bind(onTimeout, _1), // Nack
                         onTimeout);
}

void
Socket::onData(const Interest& interest, const Data& data,
               const DataValidatedCallback& onValidated,
               const DataValidationErrorCallback& onFailed)
{
  if (static_cast<bool>(m_validator))
    m_validator->validate(data, onValidated, onFailed);
  else
    onValidated(data);
}

void
Socket::onDataTimeout(const Interest& interest, int nRetries,
                      const DataValidatedCallback& onValidated,
                      const DataValidationErrorCallback& onFailed)
{
  if (nRetries <= 0)
    return;

  Interest newNonceInterest(interest);
  newNonceInterest.refreshNonce();

  m_face.expressInterest(newNonceInterest,
                         bind(&Socket::onData, this, _1, _2, onValidated, onFailed),
                         bind(&Socket::onDataTimeout, this, _1, nRetries - 1,
                              onValidated, onFailed), // Nack
                         bind(&Socket::onDataTimeout, this, _1, nRetries - 1,
                              onValidated, onFailed));
}

void
Socket::onDataValidationFailed(const Data& data,
                               const ValidationError& error)
{
}

void
Socket::onSyncNack(const Interest &interest, const lp::Nack &nack) {
}

void
Socket::onSyncTimeout(const Interest &interest) {
}

/**
 * retxSyncInterest() - Cancel and schedule new retxSyncInterest event.
 */
void
Socket::retxSyncInterest() {
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
void
Socket::sendSyncInterest() {
  using namespace std::chrono;

  Name pending_sync_notify(m_syncPrefix);
  pending_sync_notify.append(escape(m_id))
                     .append(EncodeVVToNameWithInterest(m_vv))
                     .appendTimestamp();

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
 * sendSyncAck() - Add an ACK into queue
 */
void
Socket::sendSyncAck(const Name &n) {
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

  // New data
  std::vector<MissingDataInfo> v;

  // Check if other vector has newer state
  for (auto entry : vv_other) {
    auto nidOther = entry.first;
    auto seqOther = entry.second;
    auto it = m_vv.find(nidOther);

    if (it == m_vv.end() || it->second < seqOther) {
      other_vector_new = true;
      // Detect new data
      auto startSeq =
        m_vv.find(nidOther) == m_vv.end() ? 1 : (m_vv[nidOther] + 1);

      // Add missing data
      v.push_back({nidOther, startSeq, seqOther});

      // Merge local vector
      m_vv[nidOther] = seqOther;
    }
  }

  // Callback if missing data found
  if (!v.empty()) {
    m_onUpdate(v);
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
