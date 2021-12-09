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

#ifndef NDN_SVS_CORE_HPP
#define NDN_SVS_CORE_HPP

#include "common.hpp"
#include "version-vector.hpp"
#include "security-options.hpp"

#include <ndn-cxx/util/random.hpp>

#include <atomic>
#include <chrono>
#include <mutex>

namespace ndn {
namespace svs {

class MissingDataInfo
{
public:
  /// @brief session name
  NodeID nodeId;
  /// @brief the lowest one of missing sequence numbers
  SeqNo low;
  /// @brief the highest one of missing sequence numbers
  SeqNo high;
};

/**
 * @brief The callback function to handle state updates
 *
 * The parameter is a set of MissingDataInfo, of which each corresponds to
 * a session that has changed its state.
 */
using UpdateCallback = function<void(const std::vector<MissingDataInfo>&)>;

/**
 * @brief Pure SVS
 */
class SVSyncCore : noncopyable
{
public:
  class Error : public std::runtime_error
  {
  public:
    explicit
    Error(const std::string& what)
      : std::runtime_error(what)
    {
    }
  };

public:
  /**
   * @brief Constructor
   *
   * @param face The face used to communication
   * @param syncPrefix The prefix of the sync group
   * @param onUpdate The callback function to handle state updates
   * @param syncKey Base64 encoded key to sign sync interests
   * @param nid ID for the node
   */
  SVSyncCore(ndn::Face& face,
             const Name& syncPrefix,
             const UpdateCallback& onUpdate,
             const SecurityOptions& securityOptions = SecurityOptions::DEFAULT,
             const NodeID nid = EMPTY_NODE_ID);

  ~SVSyncCore();

  /**
   * @brief Reset the sync tree (and restart synchronization again)
   *
   * @param isOnInterest a flag that tells whether the reset is called by reset interest.
   */
  void
  reset(bool isOnInterest = false);

  /**
   * @brief Get the node ID of the local session.
   *
   * @param prefix prefix of the node
   */
  const NodeID&
  getNodeId()
  {
    return m_id;
  }

  /**
   * @brief Get current seqNo of the local session.
   *
   * This method gets the seqNo according to prefix, if prefix is not specified,
   * it returns the seqNo of default user.
   *
   * @param prefix prefix of the node
   */
  SeqNo
  getSeqNo(const NodeID& nid = EMPTY_NODE_ID) const;

  /**
   * @brief Update the seqNo of the local session
   *
   * The method updates the existing seqNo with the supplied seqNo and NodeID.
   *
   * @param seq The new seqNo.
   * @param nid The NodeID of node to update.
   */
  void
  updateSeqNo(const SeqNo& seq, const NodeID& nid = EMPTY_NODE_ID);

  /// @brief Get all the nodeIDs
  std::set<NodeID>
  getNodeIds() const;

  using GetExtraBlockCallback = function<const ndn::Block(const VersionVector&)>;
  using RecvExtraBlockCallback = function<void(const ndn::Block&, const VersionVector&)>;

  /**
  * @brief Callback to get extra data block for sync interest
  * The version vector will be locked during the duration of this callback,
  * so it must return FAST
  */
  void setGetExtraBlockCallback(const GetExtraBlockCallback& callback)
  {
    m_getExtraBlock = callback;
  }

  /**
   * @brief Callback on receiving extra data in a sync interest.
   * Will be called BEFORE the interest is processed.
   */
  void setRecvExtraBlockCallback(const RecvExtraBlockCallback& callback)
  {
    m_recvExtraBlock = callback;
  }

  /// @brief Get current version vector
  VersionVector&
  getState()
  {
    return m_vv;
  }

  /// @brief Get human-readable representation of version vector
  std::string
  getStateStr() const
  {
    return m_vv.toStr();
  }

NDN_SVS_PUBLIC_WITH_TESTS_ELSE_PRIVATE:
  void
  onSyncInterest(const Interest &interest);

  void
  onSyncInterestValidated(const Interest &interest);

  /**
   * @brief Mark the instance as initialized and send the first interest
   */
  void
  sendInitialInterest();

  /**
   * @brief sendSyncInterest and schedule a new retxSyncInterest event.
   *
   * @param send Send a sync interest immediately
   * @param delay Delay in milliseconds to schedule next interest (0 for default).
   */
  void
  retxSyncInterest(const bool send, unsigned int delay);

  /**
   * @brief Add one sync interest to queue.
   *
   * Called by retxSyncInterest(), or after increasing a sequence
   * number with updateSeqNo()
   */
  void
  sendSyncInterest();

  /**
   * @brief Merge state vector into the current
   *
   * Also adds missing data interests to data interest queue.
   *
   * @param vvOther state vector to merge in
   *
   * @returns a pair of boolean representing:
   *    <my vector new, other vector new>.
   */
  std::pair<bool, bool>
  mergeStateVector(const VersionVector &vvOther);

  /**
   * @brief Record vector by merging it into m_recordedVv
   *
   * @param vvOther state vector to merge in
   * @returns if recorded successfully
   */
  bool
  recordVector(const VersionVector &vvOther);

  /**
   * @brief Enter suppression state by setting
   * m_recording to True and initializing m_recordedVv to vvOther
   *
   * Does nothing if already in suppression state
   *
   * @param vvOther first vector to record
   */
  void
  enterSuppressionState(const VersionVector &vvOther);

  /// @brief Reference to scheduler
  ndn::Scheduler&
  getScheduler()
  {
    return m_scheduler;
  }

  /// @brief Get the current time in microseconds with arbitrary reference
  long
  getCurrentTime() const;

public:
  static const NodeID EMPTY_NODE_ID;

private:
  static const ConstBufferPtr EMPTY_DIGEST;
  static const ndn::name::Component RESET_COMPONENT;
  static const ndn::name::Component RECOVERY_COMPONENT;

  // Communication
  ndn::Face& m_face;
  const Name m_syncPrefix;
  const SecurityOptions m_securityOptions;
  const NodeID m_id;
  ndn::ScopedRegisteredPrefixHandle m_syncRegisteredPrefix;

  const UpdateCallback m_onUpdate;

  // State
  VersionVector m_vv;
  mutable std::mutex m_vvMutex;
  // Aggregates incoming vectors while in suppression state
  std::unique_ptr<VersionVector> m_recordedVv = nullptr;
  mutable std::mutex m_recordedVvMutex;

  // Extra block
  GetExtraBlockCallback m_getExtraBlock;
  RecvExtraBlockCallback m_recvExtraBlock;

  // Random Engine
  ndn::random::RandomNumberEngine& m_rng;
  // Milliseconds between sending two packets in the queues
  std::uniform_int_distribution<> m_packetDist;
  // Milliseconds between sending two sync interests
  std::uniform_int_distribution<> m_retxDist;
  // Milliseconds to send sync interest reply after
  std::uniform_int_distribution<> m_intrReplyDist;

  // Security
  ndn::KeyChain m_keyChainMem;

  ndn::Scheduler m_scheduler;
  mutable std::mutex m_schedulerMutex;
  scheduler::ScopedEventId m_retxEvent;
  scheduler::ScopedEventId m_packetEvent;

  std::chrono::steady_clock m_steadyClock;

  // Time at which the next sync interest will be sent
  std::atomic_long m_nextSyncInterest;

  int m_instanceId;
  static int s_instanceCounter;

  // Prevent sending interests before initialization
  bool m_initialized = false;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_CORE_HPP
