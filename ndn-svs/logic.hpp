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

#ifndef NDN_SVS_LOGIC_HPP
#define NDN_SVS_LOGIC_HPP

#include "common.hpp"
#include "version-vector.hpp"

#include <ndn-cxx/util/random.hpp>

#include <mutex>

namespace ndn {
namespace svs {

class MissingDataInfo
{
public:
  /// @brief session name
  NodeID session;
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
 * @brief Logic of SVS
 */
class Logic : noncopyable
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
  static const time::milliseconds DEFAULT_ACK_FRESHNESS;

  /**
   * @brief Constructor
   *
   * @param face The face used to communication
   * @param syncPrefix The prefix of the sync group
   * @param onUpdate The callback function to handle state updates
   * @param signingId The signing Id of the default user
   * @param validator The validator for packet validation
   * @param ackFreshness Freshness of the sync ack
   * @param nid ID for the node
   */
  Logic(ndn::Face& face,
        ndn::KeyChain& keyChain,
        const Name& syncPrefix,
        const UpdateCallback& onUpdate,
        const Name& signingId = DEFAULT_NAME,
        std::shared_ptr<Validator> validator = DEFAULT_VALIDATOR,
        const time::milliseconds& syncAckFreshness = DEFAULT_ACK_FRESHNESS,
        const NodeID nid = EMPTY_NODE_ID);

  ~Logic();

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
  getSessionName()
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

  /// @brief Get the name of all sessions
  std::set<NodeID>
  getSessionNames() const;

  /// @brief Get human-readable representation of version vector
  std::string
  getStateStr() const
  {
    return m_vv.toStr();
  }

NDN_SVS_PUBLIC_WITH_TESTS_ELSE_PRIVATE:
  void
  onSyncInterest(const Interest &interest);

  /// @brief Decode version vector from data body, and merge vector.
  void
  onSyncAck(const Data &data);

  void
  onSyncNack(const Interest &interest, const lp::Nack &nack);

  void
  onSyncTimeout(const Interest &interest);

  /// @brief sendSyncInterest and schedule a new retxSyncInterest event.
  void
  retxSyncInterest();

  /**
   * @brief Add one sync interest to queue.
   *
   * Called by retxSyncInterest(), or after increasing a sequence
   * number with updateSeqNo()
   */
  void
  sendSyncInterest();

  /// @brief Add an ACK into queue
  void
  sendSyncAck(const Name &n);

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

  /// @brief Reference to scheduler
  ndn::Scheduler&
  getScheduler()
  {
    return m_scheduler;
  }

  /// @brief Get current version vector
  VersionVector&
  getState()
  {
    return m_vv;
  }

private:

public:
  static const ndn::Name DEFAULT_NAME;
  static const std::shared_ptr<Validator> DEFAULT_VALIDATOR;
  static const NodeID EMPTY_NODE_ID;

private:
  static const ConstBufferPtr EMPTY_DIGEST;
  static const ndn::name::Component RESET_COMPONENT;
  static const ndn::name::Component RECOVERY_COMPONENT;

  // Communication
  ndn::Face& m_face;
  Name m_syncPrefix;
  Name m_signingId;
  NodeID m_id;
  ndn::ScopedRegisteredPrefixHandle m_syncRegisteredPrefix;

  UpdateCallback m_onUpdate;

  // State
  VersionVector m_vv;
  mutable std::mutex m_vvMutex;

  // Random Engine
  ndn::random::RandomNumberEngine& m_rng;
  // Microseconds between sending two packets in the queues
  std::uniform_int_distribution<> m_packetDist;
  // Microseconds between sending two sync interests
  std::uniform_int_distribution<> m_retxDist;

  // Freshness of sync ack
  time::milliseconds m_syncAckFreshness;

  // Security
  ndn::KeyChain& m_keyChain;
  std::shared_ptr<security::Validator> m_validator;

  ndn::Scheduler m_scheduler;
  scheduler::ScopedEventId m_retxEvent;
  scheduler::ScopedEventId m_packetEvent;

  int m_instanceId;
  static int s_instanceCounter;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_LOGIC_HPP
