/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2023 University of California, Los Angeles
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

#include "core.hpp"
#include "tlv.hpp"

#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/security/verification-helpers.hpp>
#include <ndn-cxx/encoding/buffer-stream.hpp>
#include <ndn-cxx/lp/tags.hpp>

#ifdef NDN_SVS_COMPRESSION
#include <boost/iostreams/filter/lzma.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#endif

namespace ndn::svs {

SVSyncCore::SVSyncCore(ndn::Face& face,
                       const Name& syncPrefix,
                       const UpdateCallback& onUpdate,
                       const SecurityOptions& securityOptions,
                       const NodeID& nid)
  : m_face(face)
  , m_syncPrefix(syncPrefix)
  , m_securityOptions(securityOptions)
  , m_id(nid)
  , m_onUpdate(onUpdate)
  , m_maxSuppressionTime(500_ms)
  , m_periodicSyncTime(30_s)
  , m_periodicSyncJitter(0.1)
  , m_rng(ndn::random::getRandomNumberEngine())
  , m_retxDist(m_periodicSyncTime.count() * (1.0 - m_periodicSyncJitter), m_periodicSyncTime.count() * (1.0 + m_periodicSyncJitter))
  , m_intrReplyDist(0, m_maxSuppressionTime.count())
  , m_keyChainMem("pib-memory:", "tpm-memory:")
  , m_scheduler(m_face.getIoContext())
{
  // Register sync interest filter
  m_syncRegisteredPrefix =
    m_face.setInterestFilter(syncPrefix,
                             std::bind(&SVSyncCore::onSyncInterest, this, _2),
                             std::bind(&SVSyncCore::sendInitialInterest, this),
                             [] (auto&&...) {
                                NDN_THROW(Error("Failed to register sync prefix"));
                             });
}

inline int
suppressionCurve(int constFactor, int value)
{
  /**
   * This curve increases the probability that only one or a few
   * nodes pick lower values for timers compared to other nodes.
   * This leads to better suppression results.
   * Increasing the curve factor makes the curve steeper =>
   * better for more nodes, but worse for fewer nodes.
   */

  double c = constFactor;
  double v = value;
  double f = 10.0; // curve factor

  return (int) (c * (1.0 - std::exp((v - c) / (c / f))));
}

void
SVSyncCore::sendInitialInterest()
{
  // Wait for 100ms before sending the first sync interest
  // This is necessary to give other things time to initialize
  m_scheduler.schedule(time::milliseconds(100), [this] {
    m_initialized = true;
    retxSyncInterest(true, 0);
  });
}

void
SVSyncCore::onSyncInterest(const Interest &interest)
{
  switch (m_securityOptions.interestSigner->signingInfo.getSignerType())
  {
    case security::SigningInfo::SIGNER_TYPE_NULL:
      onSyncInterestValidated(interest);
      return;

    case security::SigningInfo::SIGNER_TYPE_HMAC:
      if (security::verifySignature(interest, m_keyChainMem.getTpm(),
                                    m_securityOptions.interestSigner->signingInfo.getSignerName(),
                                    DigestAlgorithm::SHA256))
        onSyncInterestValidated(interest);
      return;

    default:
      if (static_cast<bool>(m_securityOptions.validator))
        m_securityOptions.validator->validate(interest,
                                              std::bind(&SVSyncCore::onSyncInterestValidated, this, _1),
                                              nullptr);
      else
        onSyncInterestValidated(interest);
      return;
  }
}

void
SVSyncCore::onSyncInterestValidated(const Interest &interest)
{
  const auto &n = interest.getName();

  // Get incoming face
  uint64_t incomingFace = 0;
  {
    auto tag = interest.getTag<ndn::lp::IncomingFaceIdTag>();
    if (tag) {
      incomingFace = tag->get();
    }
  }

  // Get state vector
  std::shared_ptr<VersionVector> vvOther;
  try
  {
    ndn::Block vvBlock = n.get(-2);

    // Decompress if necessary
    if (vvBlock.type() == 211) {
#ifdef NDN_SVS_COMPRESSION
      boost::iostreams::filtering_istreambuf in;
      in.push(boost::iostreams::lzma_decompressor());
      in.push(boost::iostreams::array_source(reinterpret_cast<const char*>(vvBlock.value()), vvBlock.value_size()));
      ndn::OBufferStream decompressed;
      boost::iostreams::copy(in, decompressed);

      auto inner = ndn::Block::fromBuffer(decompressed.buf());
      if (!std::get<0>(inner)) {
        throw ndn::tlv::Error("Failed to decode inner block");
      }

      vvBlock = std::get<1>(inner);
#else
      throw ndn::tlv::Error("SVS was compiled without compression support");
#endif
    }

    vvOther = std::make_shared<VersionVector>(vvBlock);
  }
  catch (ndn::tlv::Error&)
  {
    // TODO: log error
    return;
  }

  if (m_recvExtraBlock && interest.hasApplicationParameters())
  {
    try {
      m_recvExtraBlock(interest.getApplicationParameters().blockFromValue(), *vvOther);
    }
    catch (std::exception&) {}
  }

  // Merge state vector
  auto result = mergeStateVector(*vvOther);

  bool myVectorNew = std::get<0>(result);
  auto missingData = std::get<2>(result);

  // Callback if missing data found
  if (!missingData.empty())
  {
    for (auto &e : missingData)
      e.incomingFace = incomingFace;
    m_onUpdate(missingData);
  }

  // Try to record; the call will check if in suppression state
  if (recordVector(*vvOther))
    return;

  // If incoming state identical/newer to local vector, reset timer
  // If incoming state is older, send sync interest immediately
  if (!myVectorNew)
  {
    retxSyncInterest(false, 0);
  }
  else
  {
    enterSuppressionState(*vvOther);
    // Check how much time is left on the timer,
    // reset to ~m_intrReplyDist if more than that.
    int delay = m_intrReplyDist(m_rng);

    // Curve the delay for better suppression in large groups
    // TODO: efficient curve depends on number of active nodes
    delay = suppressionCurve(m_maxSuppressionTime.count(), delay);

    if (getCurrentTime() + delay * 1000 < m_nextSyncInterest)
    {
      retxSyncInterest(false, delay);
    }
  }
}

void
SVSyncCore::retxSyncInterest(bool send, unsigned int delay)
{
  if (send)
  {
    std::lock_guard<std::mutex> lock(m_recordedVvMutex);

    // Only send interest if in steady state or local vector has newer state
    // than recorded interests
    if (!m_recordedVv || std::get<0>(mergeStateVector(*m_recordedVv)))
      sendSyncInterest();
    m_recordedVv = nullptr;
  }

  if (delay == 0)
    delay = m_retxDist(m_rng);

  {
    std::lock_guard<std::mutex> lock(m_schedulerMutex);

    // Store the scheduled time
    m_nextSyncInterest = getCurrentTime() + 1000 * delay;

    m_retxEvent = m_scheduler.schedule(time::milliseconds(delay),
                                       [this] { retxSyncInterest(true, 0); });
  }
}

void
SVSyncCore::sendSyncInterest()
{
  if (!m_initialized)
    return;

  Interest interest;
  interest.setApplicationParameters(span<const uint8_t>{'0'});

  ndn::Block vvWire;
  {
    std::lock_guard<std::mutex> lock(m_vvMutex);
    vvWire = m_vv.encode();

    // Add parameters digest
    if (m_getExtraBlock)
    {
      interest.setApplicationParameters(m_getExtraBlock(m_vv));
    }
  }

  // Create sync interest name
  Name syncName(m_syncPrefix);

#ifdef NDN_SVS_COMPRESSION
  vvWire.encode();
  boost::iostreams::filtering_istreambuf in;
  in.push(boost::iostreams::lzma_compressor());
  in.push(boost::iostreams::array_source(reinterpret_cast<const char*>(vvWire.data()), vvWire.size()));
  ndn::OBufferStream compressed;
  boost::iostreams::copy(in, compressed);
  vvWire = ndn::Block(svs::tlv::StateVectorLzma, compressed.buf());
#endif

  syncName.append(Name::Component(vvWire));

  interest.setName(syncName);
  interest.setInterestLifetime(time::milliseconds(1));

  switch (m_securityOptions.interestSigner->signingInfo.getSignerType())
  {
    case security::SigningInfo::SIGNER_TYPE_NULL:
      break;

    case security::SigningInfo::SIGNER_TYPE_HMAC:
      m_keyChainMem.sign(interest, m_securityOptions.interestSigner->signingInfo);
      break;

    default:
      m_securityOptions.interestSigner->sign(interest);
      break;
  }

  m_face.expressInterest(interest, nullptr, nullptr, nullptr);
}

std::tuple<bool, bool, std::vector<MissingDataInfo>>
SVSyncCore::mergeStateVector(const VersionVector &vvOther)
{
  std::lock_guard<std::mutex> lock(m_vvMutex);

  bool myVectorNew = false,
       otherVectorNew = false;

  // New data found in vvOther
  std::vector<MissingDataInfo> missingData;

  // Check if other vector has newer state
  for (const auto& entry : vvOther)
  {
    NodeID nidOther = entry.first;
    SeqNo seqOther = entry.second;
    SeqNo seqCurrent = m_vv.get(nidOther);

    if (seqCurrent < seqOther)
    {
      otherVectorNew = true;

      SeqNo startSeq = m_vv.get(nidOther) + 1;
      missingData.push_back({nidOther, startSeq, seqOther, 0});

      m_vv.set(nidOther, seqOther);
    }
  }

  // Check if I have newer state
  for (const auto& entry : m_vv)
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

  return {myVectorNew, otherVectorNew, missingData};
}

void
SVSyncCore::reset(bool isOnInterest)
{
}

SeqNo
SVSyncCore::getSeqNo(const NodeID& nid) const
{
  std::lock_guard<std::mutex> lock(m_vvMutex);
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;
  return m_vv.get(t_nid);
}

void
SVSyncCore::updateSeqNo(const SeqNo& seq, const NodeID& nid)
{
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;

  SeqNo prev;
  {
    std::lock_guard<std::mutex> lock(m_vvMutex);
    prev = m_vv.get(t_nid);
    m_vv.set(t_nid, seq);
  }

  if (seq > prev)
    retxSyncInterest(false, 1);
}

std::set<NodeID>
SVSyncCore::getNodeIds() const
{
  std::lock_guard<std::mutex> lock(m_vvMutex);
  std::set<NodeID> sessionNames;
  for (const auto& nid : m_vv)
  {
    sessionNames.insert(nid.first);
  }
  return sessionNames;
}

long
SVSyncCore::getCurrentTime() const
{
  return std::chrono::duration_cast<std::chrono::microseconds>(
    m_steadyClock.now().time_since_epoch()).count();
}

bool
SVSyncCore::recordVector(const VersionVector &vvOther)
{
  std::lock_guard<std::mutex> lock(m_recordedVvMutex);

  if (!m_recordedVv) return false;

  std::lock_guard<std::mutex> lock1(m_vvMutex);

  for (const auto& entry : vvOther)
  {
    NodeID nidOther = entry.first;
    SeqNo seqOther = entry.second;
    SeqNo seqCurrent = m_recordedVv->get(nidOther);

    if (seqCurrent < seqOther)
    {
      m_recordedVv->set(nidOther, seqOther);
    }
  }

  return true;
}

void
SVSyncCore::enterSuppressionState(const VersionVector &vvOther)
{
  std::lock_guard<std::mutex> lock(m_recordedVvMutex);

  if (!m_recordedVv)
    m_recordedVv = std::make_unique<VersionVector>(vvOther);
}

} // namespace ndn::svs
