/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2025 University of California, Los Angeles
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

#include <ndn-cxx/encoding/buffer-stream.hpp>
#include <ndn-cxx/lp/tags.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/security/verification-helpers.hpp>
#include <ndn-cxx/util/logger.hpp>

#include <chrono>

namespace ndn::svs {

NDN_LOG_INIT(ndn_svs.Core);

namespace {

BootstrapTime
getCurrentBootstrapTime()
{
  return static_cast<BootstrapTime>(
    time::toUnixTimestamp<time::seconds>(time::system_clock::now()).count());
}

} // namespace

SVSyncCore::SVSyncCore(ndn::Face& face,
                       const Name& syncPrefix,
                       const UpdateCallback& onUpdate,
                       const SecurityOptions& securityOptions,
                       const NodeID& nid,
                       const SyncProtocolOptions& protocolOptions)
  : m_face(face)
  , m_syncPrefix(syncPrefix)
  , m_syncInterestPrefix(SyncProtocolCodec::makeSyncName(syncPrefix, protocolOptions.version))
  , m_securityOptions(securityOptions)
  , m_protocolOptions(protocolOptions.resolve())
  , m_id(nid)
  , m_bootstrapTime(m_protocolOptions.bootstrapTime.value_or(getCurrentBootstrapTime()))
  , m_onUpdate(onUpdate)
  , m_maxSuppressionTime(m_protocolOptions.suppressionPeriod)
  , m_periodicSyncTime(m_protocolOptions.periodicTimeout)
  , m_periodicSyncJitter(m_protocolOptions.periodicJitter)
  , m_rng(ndn::random::getRandomNumberEngine())
  , m_retxDist(m_periodicSyncTime.count() * (1.0 - m_periodicSyncJitter),
               m_periodicSyncTime.count() * (1.0 + m_periodicSyncJitter))
  , m_intrReplyDist(0, m_maxSuppressionTime.count())
  , m_keyChainMem("pib-memory:", "tpm-memory:")
  , m_scheduler(m_face.getIoContext())
{
  m_validationGate->owner = this;
  // Dispatch only the selected wire version locally, but advertise the group
  // prefix so peers that follow the established SVS registration convention
  // can reach this participant through the forwarder.
  m_syncInterestFilter =
    m_face.setInterestFilter(m_syncInterestPrefix,
                             std::bind(&SVSyncCore::onSyncInterest, this, _2));
  m_syncRegisteredPrefix =
    m_face.registerPrefix(m_syncPrefix,
                          std::bind(&SVSyncCore::sendInitialInterest, this),
                          [this] (auto&&...) {
                            NDN_LOG_ERROR("Failed to register sync prefix " << m_syncPrefix);
                          });
}

SVSyncCore::~SVSyncCore()
{
  std::lock_guard<std::mutex> lock(m_validationGate->mutex);
  m_validationGate->owner = nullptr;
}

static inline int
suppressionCurve(int constFactor, int value)
{
  // This curve increases the probability that only one or a few
  // nodes pick lower values for timers compared to other nodes.
  // This leads to better suppression results.
  // Increasing the curve factor makes the curve steeper =>
  // better for more nodes, but worse for fewer nodes.

  float c = constFactor;
  float v = value;
  float f = 10.0; // curve factor

  return static_cast<int>(c * (1.0 - std::exp((v - c) / (c / f))));
}

void
SVSyncCore::sendInitialInterest()
{
  // Wait for 100ms before sending the first sync interest
  // This is necessary to give other things time to initialize
  m_scheduler.schedule(100_ms, [this] {
    m_initialized = true;
    retxSyncInterest(true, 0);
  });
}

void
SVSyncCore::onSyncInterest(const Interest& interest)
{
  DecodedSyncEnvelope envelope;
  try {
    envelope = SyncProtocolCodec::decode(interest, m_syncPrefix,
                                         m_protocolOptions.version, false);
  }
  catch (const std::exception& e) {
    m_lastValidationStatus.store(ValidationStatus::Rejected, std::memory_order_relaxed);
    NDN_LOG_DEBUG("Reject malformed SVS V3 Sync Interest: " << e.what());
    return;
  }

  if (!envelope.stateVectorData) {
    m_lastValidationStatus.store(ValidationStatus::Rejected, std::memory_order_relaxed);
    return;
  }

  if (m_securityOptions.validator) {
    auto gate = m_validationGate;
    m_securityOptions.validator->validate(
      *envelope.stateVectorData,
      [gate, interest] (const Data&) {
        std::lock_guard<std::mutex> lock(gate->mutex);
        if (gate->owner != nullptr) {
          gate->owner->m_lastValidationStatus.store(ValidationStatus::Verified,
                                                    std::memory_order_relaxed);
          gate->owner->onSyncInterestValidated(interest);
        }
      },
      [gate] (const Data&, const auto&) {
        std::lock_guard<std::mutex> lock(gate->mutex);
        if (gate->owner != nullptr) {
          gate->owner->m_lastValidationStatus.store(ValidationStatus::Rejected,
                                                    std::memory_order_relaxed);
        }
      });
  }
  else {
    m_lastValidationStatus.store(ValidationStatus::StructuralUnverified,
                                 std::memory_order_relaxed);
    onSyncInterestValidated(interest);
  }
}

void
SVSyncCore::onSyncInterestValidated(const Interest& interest)
{
  // Get incoming face (this is needed by NLSR)
  uint64_t incomingFace = 0;
  {
    auto tag = interest.getTag<ndn::lp::IncomingFaceIdTag>();
    if (tag) {
      incomingFace = tag->get();
    }
  }

  DecodedSyncEnvelope envelope;
  try {
    envelope = SyncProtocolCodec::decode(interest, m_syncPrefix, m_protocolOptions.version);
  }
  catch (const std::exception& e) {
    m_lastValidationStatus.store(ValidationStatus::Rejected, std::memory_order_relaxed);
    NDN_LOG_DEBUG("Reject invalid SVS Sync Interest: " << e.what());
    return;
  }

  // Extension callbacks run only after the complete envelope and state vector
  // have decoded. In V3 these blocks are covered by the embedded Data signature.
  if (m_recvExtraBlock) {
    for (const auto& extension : envelope.extensions) {
      try {
        m_recvExtraBlock(extension, envelope.stateVector);
      }
      catch (const std::exception& e) {
        NDN_LOG_DEBUG("Reject SVS extension type=" << extension.type() << ": " << e.what());
      }
    }
  }

  // Merge state vector
  auto result = mergeStateVector(envelope.stateVector);

  // Callback if missing data found
  if (!result.missingInfo.empty()) {
    for (auto& e : result.missingInfo)
      e.incomingFace = incomingFace;
    m_onUpdate(result.missingInfo);
  }

  // Try to record; the call will check if in suppression state
  if (recordVector(envelope.stateVector))
    return;

  // If incoming state identical/newer to local vector, reset timer
  // If incoming state is older, send sync interest immediately
  if (!result.myVectorNew) {
    retxSyncInterest(false, 0);
  } else {
    enterSuppressionState(envelope.stateVector);
    // Check how much time is left on the timer,
    // reset to ~m_intrReplyDist if more than that.
    int delay = m_intrReplyDist(m_rng);

    // Curve the delay for better suppression in large groups
    // TODO: efficient curve depends on number of active nodes
    delay = suppressionCurve(m_maxSuppressionTime.count(), delay);

    if (getCurrentTime() + delay * 1000 < m_nextSyncInterest) {
      retxSyncInterest(false, delay);
    }
  }
}

void
SVSyncCore::retxSyncInterest(bool send, unsigned int delay)
{
  if (send) {
    std::lock_guard<std::mutex> lock(m_recordedVvMutex);

    // Only send interest if in steady state or local vector has newer state
    // than recorded interests
    if (!m_recordedVv || mergeStateVector(*m_recordedVv).myVectorNew)
      sendSyncInterest();
    m_recordedVv = nullptr;
  }

  if (delay == 0)
    delay = m_retxDist(m_rng);

  {
    std::lock_guard<std::mutex> lock(m_schedulerMutex);

    // Store the scheduled time
    m_nextSyncInterest = getCurrentTime() + 1000 * delay;

    m_retxEvent = m_scheduler.schedule(time::milliseconds(delay), [this] { retxSyncInterest(true, 0); });
  }
}

void
SVSyncCore::sendSyncInterest()
{
  if (!m_initialized)
    return;

  VersionVector stateVector;
  std::vector<Block> extensions;
  {
    std::lock_guard<std::mutex> lock(m_vvMutex);
    stateVector = m_vv;
    if (m_getExtraBlocks) {
      extensions = m_getExtraBlocks(m_vv);
    }
    else if (m_getExtraBlock) {
      extensions.push_back(m_getExtraBlock(m_vv));
    }
  }

  Interest interest = SyncProtocolCodec::encode(
    m_syncPrefix, stateVector, extensions, m_protocolOptions,
    [this] (Data& data) {
      if (m_securityOptions.dataSigner->signingInfo.getSignerType() ==
          security::SigningInfo::SIGNER_TYPE_NULL) {
        m_keyChainMem.sign(data, security::signingWithSha256());
      }
      else {
        m_securityOptions.dataSigner->sign(data);
      }
    });

  m_face.expressInterest(interest, nullptr, nullptr, nullptr);
}

SVSyncCore::MergeResult
SVSyncCore::mergeStateVector(const VersionVector& vvOther)
{
  std::lock_guard<std::mutex> lock(m_vvMutex);
  auto result = computeMergeStateVector(m_vv, vvOther);
  m_vv = result.mergedVector;
  return {result.myVectorNew, result.otherVectorNew, result.missingData};
}

SVSyncCore::MergeComputationResult
SVSyncCore::computeMergeStateVector(const VersionVector& localVector,
                                    const VersionVector& remoteVector)
{
  MergeComputationResult result;
  result.mergedVector = localVector;

  // Check if other vector has newer state
  for (const auto& [nidOther, seqEntries] : remoteVector.getAllEntries()) {
    for (const auto& [bootstrapTime, seqOther] : seqEntries) {
      const SeqNo seqCurrent = result.mergedVector.get(nidOther, bootstrapTime);
      if (seqCurrent < seqOther) {
        result.otherVectorNew = true;
        result.missingData.push_back({nidOther, seqCurrent + 1, seqOther, 0, bootstrapTime});
        result.mergedVector.set(nidOther, bootstrapTime, seqOther);
      }
    }
  }

  // Check if I have newer state
  for (const auto& [nid, seqEntries] : result.mergedVector.getAllEntries()) {
    for (const auto& [bootstrapTime, seq] : seqEntries) {
      const SeqNo seqOther = remoteVector.get(nid, bootstrapTime);
      if (seqOther < seq) {
        result.myVectorNew = true;
        break;
      }
    }
    if (result.myVectorNew) {
      break;
    }
  }

  return result;
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
  if (t_nid == m_id) {
    return m_vv.get(t_nid, m_bootstrapTime);
  }
  return m_vv.get(t_nid);
}

void
SVSyncCore::updateSeqNo(const SeqNo& seq, const NodeID& nid)
{
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;
  updateSeqNo(seq, m_bootstrapTime, t_nid);
}

void
SVSyncCore::updateSeqNo(const SeqNo& seq, BootstrapTime bootstrapTime, const NodeID& nid)
{
  NodeID t_nid = (nid == EMPTY_NODE_ID) ? m_id : nid;

  SeqNo prev;
  {
    std::lock_guard<std::mutex> lock(m_vvMutex);
    prev = m_vv.get(t_nid, bootstrapTime);
    m_vv.set(t_nid, bootstrapTime, seq);
  }

  if (seq > prev)
    retxSyncInterest(false, 1);
}

std::set<NodeID>
SVSyncCore::getNodeIds() const
{
  std::lock_guard<std::mutex> lock(m_vvMutex);
  std::set<NodeID> sessionNames;
  for (const auto& nid : m_vv) {
    sessionNames.insert(nid.first);
  }
  return sessionNames;
}

long
SVSyncCore::getCurrentTime() const
{
  return std::chrono::duration_cast<std::chrono::microseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}

bool
SVSyncCore::recordVector(const VersionVector& vvOther)
{
  std::lock_guard<std::mutex> lock(m_recordedVvMutex);

  if (!m_recordedVv)
    return false;

  std::lock_guard<std::mutex> lock1(m_vvMutex);

  for (const auto& [nidOther, seqEntries] : vvOther.getAllEntries()) {
    for (const auto& [bootstrapTime, seqOther] : seqEntries) {
      SeqNo seqCurrent = m_recordedVv->get(nidOther, bootstrapTime);

      if (seqCurrent < seqOther) {
        m_recordedVv->set(nidOther, bootstrapTime, seqOther);
      }
    }
  }

  return true;
}

void
SVSyncCore::enterSuppressionState(const VersionVector& vvOther)
{
  std::lock_guard<std::mutex> lock(m_recordedVvMutex);

  if (!m_recordedVv)
    m_recordedVv = std::make_unique<VersionVector>(vvOther);
}

} // namespace ndn::svs
