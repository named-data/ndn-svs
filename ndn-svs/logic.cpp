/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2020 University of California, Los Angeles
 *
 * This file is part of ndn-svs, synchronization library for distributed realtime
 * applications for NDN.
 *
 * ndn-svs library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, in version 2.1 of the License.
 *
 * ndn-svs library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
 */

#include "logic.hpp"

#include <ndn-cxx/signature-info.hpp>
#include <ndn-cxx/util/random.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/security/signing-info.hpp>

namespace ndn {
namespace svs {

int Logic::s_instanceCounter = 0;

const ndn::Name Logic::DEFAULT_NAME;
const ndn::Name DEFAULT_NAME;
const std::shared_ptr<Validator> DEFAULT_VALIDATOR;
const NodeID Logic::EMPTY_NODE_ID;
const time::milliseconds Logic::DEFAULT_ACK_FRESHNESS = time::milliseconds(4000);

Logic::Logic(ndn::Face& face,
             const Name& syncPrefix,
             const UpdateCallback& onUpdate,
             const Name& signingId,
             std::shared_ptr<Validator> validator,
             const time::milliseconds& syncAckFreshness,
             const NodeID session)
  : m_face(face)
  , m_syncPrefix(syncPrefix)
  , m_signingId(signingId)
  , m_id(session)
  , m_onUpdate(onUpdate)
  , m_syncAckFreshness(syncAckFreshness)
  , m_validator(validator)
  , m_scheduler(m_face.getIoService())
  , m_instanceId(s_instanceCounter++)
{
  m_vv.set(m_id, 0);

  // Register sync interest filter
  m_syncRegisteredPrefix =
    m_face.setInterestFilter(syncPrefix,
                             [&] (const Name& prefix, const Interest& interest) {
                                if (!interest.isSigned()) return;

                                if (static_cast<bool>(m_validator))
                                  m_validator->validate(
                                    interest,
                                    bind(&Logic::onSyncInterest, this, _1),
                                    [] (const Interest& interest, const ValidationError& error) {});
                                else
                                  onSyncInterest(interest);
                             },
                             [] (const Name& prefix, const std::string& msg) {});

  // Start periodically send sync interest
  retxSyncInterest();

  // Start periodically send packets asynchronously
  asyncSendPacket();
}

Logic::~Logic()
{
}

void
Logic::asyncSendPacket()
{
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

  if (packet != nullptr) {
    Interest interest;
    security::SigningInfo signingInfo = signingByIdentity(m_signingId);
    std::vector<uint8_t> nonce(8);
    SignatureInfo signatureInfo;

    // Send packet
    switch (packet->packet_type) {
      case Packet::INTEREST_TYPE:
        interest = Interest(*packet->interest);
        interest.setCanBePrefix(true);
        interest.setMustBeFresh(true);

        random::generateSecureBytes(nonce.data(), nonce.size());
        signatureInfo.setTime();
        signatureInfo.setNonce(nonce);

        signingInfo.setSignedInterestFormat(security::SignedInterestFormat::V03);
        signingInfo.setSignatureInfo(signatureInfo);
        m_keyChain.sign(interest, signingInfo);

        // Sync Interest
        if (m_syncPrefix.isPrefixOf(interest.getName()))
          m_face.expressInterest(interest,
                                 std::bind(&Logic::onSyncAck, this, _2),
                                 std::bind(&Logic::onSyncNack, this, _1, _2),
                                 std::bind(&Logic::onSyncTimeout, this, _1));
        else
          NDN_THROW(Error("Invalid sync interest name"));

        break;

      case Packet::DATA_TYPE:
        // Data Reply
        if (m_syncPrefix.isPrefixOf(packet->data->getName()))
          m_face.put(*packet->data);
        else
          NDN_THROW(Error("Invalid sync data name"));

        break;

      default:
        NDN_THROW(Error("Invalid queued packet type"));
    }
  }

  int delay = packet_dist(rengine_);
  packet_event.cancel();
  packet_event = m_scheduler.schedule(time::microseconds(delay),
                                      [this] { asyncSendPacket(); });
}

void
Logic::onSyncInterest(const Interest &interest)
{
  const auto &n = interest.getName();
  NodeID nidOther = n.get(-4).toUri();

  if (nidOther == m_id) return;

  // Merge state vector
  bool myVectorNew, otherVectorNew;
  Name::Component encodedVV = n.get(-3);
  VersionVector vvOther(encodedVV.value(), encodedVV.value_size());
  std::tie(myVectorNew, otherVectorNew) = mergeStateVector(vvOther);

  // If my vector newer, send ACK immediately. Otherwise send with random delay
  if (myVectorNew) {
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
  if (!myVectorNew && !otherVectorNew)
  {
    retx_event.cancel();
    int delay = retx_dist(rengine_);
    retx_event = m_scheduler.schedule(time::microseconds(delay),
                                      [this] { retxSyncInterest(); });
  }
  else if (otherVectorNew)
  {
    retx_event.cancel();
    retxSyncInterest();
  }
}

void
Logic::onSyncAck(const Data &data)
{
  auto content = data.getContent();
  VersionVector vvOther(content.value(), content.value_size());
  mergeStateVector(vvOther);
}


void
Logic::onSyncNack(const Interest &interest, const lp::Nack &nack)
{
}

void
Logic::onSyncTimeout(const Interest &interest)
{
}

void
Logic::retxSyncInterest()
{
  sendSyncInterest();
  int delay = retx_dist(rengine_);
  retx_event = m_scheduler.schedule(time::microseconds(delay),
                                    [this] { retxSyncInterest(); });
}

void
Logic::sendSyncInterest()
{
  using namespace std::chrono;

  Name pending_sync_notify(m_syncPrefix);
  pending_sync_notify.append(m_id)
                     .append(Name::Component(m_vv.encode()))
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

void
Logic::sendSyncAck(const Name &n)
{
  std::shared_ptr<Data> data = std::make_shared<Data>(n);
  Buffer encodedVV = m_vv.encode();
  data->setContent(encodedVV.data(), encodedVV.size());

  if (m_signingId.empty())
    m_keyChain.sign(*data);
  else
    m_keyChain.sign(*data, signingByIdentity(m_signingId));

  // TODO : this should not be hard-coded
  data->setFreshnessPeriod(m_syncAckFreshness);

  Packet packet;
  packet.packet_type = Packet::DATA_TYPE;
  packet.data = data;
  pending_ack.push_back(std::make_shared<Packet>(packet));
}

std::pair<bool, bool>
Logic::mergeStateVector(const VersionVector &vv_other)
{
  bool my_vector_new = false, other_vector_new = false;

  // New data
  std::vector<MissingDataInfo> v;

  // Check if other vector has newer state
  for (auto entry : vv_other)
  {
    NodeID nidOther = entry.first;
    SeqNo seqOther = entry.second;
    SeqNo seqCurrent = m_vv.get(nidOther);

    if (seqCurrent < seqOther)
    {
      other_vector_new = true;

      SeqNo startSeq = m_vv.get(nidOther) + 1;
      v.push_back({nidOther, startSeq, seqOther});

      m_vv.set(nidOther, seqOther);
    }
  }

  // Callback if missing data found
  if (!v.empty())
  {
    m_onUpdate(v);
  }

  // Check if I have newer state
  for (auto entry : m_vv)
  {
    NodeID nid = entry.first;
    SeqNo seq = entry.second;
    SeqNo seqOther = vv_other.get(nid);

    if (seqOther < seq)
    {
      my_vector_new = true;
      break;
    }
  }

  return std::make_pair(my_vector_new, other_vector_new);
}

void
Logic::reset(bool isOnInterest)
{
}

SeqNo
Logic::getSeqNo(const NodeID& nid) const
{
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;
  return m_vv.get(t_nid);
}

void
Logic::updateSeqNo(const SeqNo& seq, const NodeID& nid)
{
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;

  SeqNo prev = m_vv.get(t_nid);
  m_vv.set(t_nid, seq);

  if (seq > prev)
    sendSyncInterest();
}

std::set<NodeID>
Logic::getSessionNames() const
{
  std::set<NodeID> sessionNames;
  for (const auto& nid : m_vv)
  {
    sessionNames.insert(nid.first);
  }
  return sessionNames;
}

}  // namespace svs
}  // namespace ndn
