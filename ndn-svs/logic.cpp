/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2021 University of California, Los Angeles
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

#include <ndn-cxx/security/signing-helpers.hpp>

namespace ndn {
namespace svs {

int Logic::s_instanceCounter = 0;

const ndn::Name Logic::DEFAULT_NAME;
const std::shared_ptr<Validator> Logic::DEFAULT_VALIDATOR;
const NodeID Logic::EMPTY_NODE_ID;
const time::milliseconds Logic::DEFAULT_ACK_FRESHNESS = time::milliseconds(4000);

Logic::Logic(ndn::Face& face,
             ndn::KeyChain& keyChain,
             const Name& syncPrefix,
             const UpdateCallback& onUpdate,
             const Name& signingId,
             std::shared_ptr<Validator> validator,
             const time::milliseconds& syncAckFreshness,
             const NodeID nid)
  : m_face(face)
  , m_syncPrefix(syncPrefix)
  , m_signingId(signingId)
  , m_id(nid)
  , m_onUpdate(onUpdate)
  , m_rng(ndn::random::getRandomNumberEngine())
  , m_packetDist(10, 15)
  , m_retxDist(5000 * 0.9, 5000 * 1.1)
  , m_syncAckFreshness(syncAckFreshness)
  , m_keyChain(keyChain)
  , m_validator(validator)
  , m_scheduler(m_face.getIoService())
  , m_instanceId(s_instanceCounter++)
{
  m_vv.set(m_id, 0);

  // Use default identity if not specified
  if (m_signingId == Logic::DEFAULT_NAME)
    m_signingId = m_keyChain.getPib().getDefaultIdentity().getName();

  // Register sync interest filter
  m_syncRegisteredPrefix =
    m_face.setInterestFilter(syncPrefix,
                             [&] (const Name& prefix, const Interest& interest) {
                                // TODO: verify the sync interest (pseudo-)signature
                                onSyncInterest(interest);
                             },
                             [] (const Name& prefix, const std::string& msg) {});

  // Start periodically send sync interest
  retxSyncInterest();
}

Logic::~Logic()
{
}

void
Logic::onSyncInterest(const Interest &interest)
{
  const auto &n = interest.getName();

  // Merge state vector
  bool myVectorNew, otherVectorNew;
  VersionVector vvOther(n.get(-1));
  std::tie(myVectorNew, otherVectorNew) = mergeStateVector(vvOther);

  // If my vector newer, send ACK
  if (myVectorNew)
    sendSyncAck(n);

  // If incoming state identical to local vector, reset timer to delay sending
  //  next sync interest.
  // If incoming state newer than local vector, send sync interest immediately.
  // If local state newer than incoming state, do nothing.
  if (!myVectorNew && !otherVectorNew)
  {
    int delay = m_retxDist(m_rng);
    m_retxEvent = m_scheduler.schedule(time::milliseconds(delay),
                                       [this] { retxSyncInterest(); });
  }
  else if (otherVectorNew)
  {
    retxSyncInterest();
  }
}

void
Logic::onSyncAck(const Data &data)
{
  VersionVector vvOther(data.getContent().blockFromValue());
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
  int delay = m_retxDist(m_rng);
  m_retxEvent = m_scheduler.schedule(time::milliseconds(delay),
                                     [this] { retxSyncInterest(); });
}

void
Logic::sendSyncInterest()
{
  Name syncName(m_syncPrefix);

  {
    std::lock_guard<std::mutex> lock(m_vvMutex);
    syncName.append(Name::Component(m_vv.encode()));
  }

  Interest interest(syncName, time::milliseconds(1000));
  interest.setCanBePrefix(true);
  interest.setMustBeFresh(true);

  m_face.expressInterest(interest,
                         std::bind(&Logic::onSyncAck, this, _2),
                         std::bind(&Logic::onSyncNack, this, _1, _2),
                         std::bind(&Logic::onSyncTimeout, this, _1));
}

void
Logic::sendSyncAck(const Name &n)
{
  int delay = m_packetDist(m_rng);
  m_scheduler.schedule(time::milliseconds(delay), [this, n]
  {
    std::shared_ptr<Data> data = std::make_shared<Data>(n);
    {
      std::lock_guard<std::mutex> lock(m_vvMutex);
      data->setContent(m_vv.encode());
    }

    if (m_signingId.empty())
      m_keyChain.sign(*data);
    else
      m_keyChain.sign(*data, signingByIdentity(m_signingId));

    data->setFreshnessPeriod(m_syncAckFreshness);

    m_face.put(*data);
  });
}

std::pair<bool, bool>
Logic::mergeStateVector(const VersionVector &vvOther)
{
  std::lock_guard<std::mutex> lock(m_vvMutex);

  bool myVectorNew = false,
       otherVectorNew = false;

  // New data found in vvOther
  std::vector<MissingDataInfo> v;

  // Check if other vector has newer state
  for (auto entry : vvOther)
  {
    NodeID nidOther = entry.first;
    SeqNo seqOther = entry.second;
    SeqNo seqCurrent = m_vv.get(nidOther);

    if (seqCurrent < seqOther)
    {
      otherVectorNew = true;

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
    SeqNo seqOther = vvOther.get(nid);

    if (seqOther < seq)
    {
      myVectorNew = true;
      break;
    }
  }

  return std::make_pair(myVectorNew, otherVectorNew);
}

void
Logic::reset(bool isOnInterest)
{
}

SeqNo
Logic::getSeqNo(const NodeID& nid) const
{
  std::lock_guard<std::mutex> lock(m_vvMutex);
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;
  return m_vv.get(t_nid);
}

void
Logic::updateSeqNo(const SeqNo& seq, const NodeID& nid)
{
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;

  SeqNo prev;
  {
    std::lock_guard<std::mutex> lock(m_vvMutex);
    prev = m_vv.get(t_nid);
    m_vv.set(t_nid, seq);
  }

  if (seq > prev)
    sendSyncInterest();
}

std::set<NodeID>
Logic::getSessionNames() const
{
  std::lock_guard<std::mutex> lock(m_vvMutex);
  std::set<NodeID> sessionNames;
  for (const auto& nid : m_vv)
  {
    sessionNames.insert(nid.first);
  }
  return sessionNames;
}

}  // namespace svs
}  // namespace ndn
