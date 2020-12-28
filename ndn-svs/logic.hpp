/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2020 University of California, Los Angeles
 *
 * This file is part of ndn-svs, synchronization library for distributed realtime
 * applications for NDN.
 *
 * ndn-svs is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * ndn-svs is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndn-svs, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NDN_SVS_LOGIC_HPP
#define NDN_SVS_LOGIC_HPP

#include "common.hpp"
#include "version-vector.hpp"

#include <random>

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
 * @brief Logic of ChronoSync
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
   * @param face The face used to communication, will be shutdown in destructor
   * @param syncPrefix The prefix of the sync group
   * @param defaultUserPrefix The prefix of the first user added to this session
   * @param onUpdate The callback function to handle state updates
   * @param defaultSigningId The signing Id of the default user
   * @param validator The validator for packet validation
   * @param ackFreshness Freshness of the sync ack
   * @param session Manually defined session ID
   */
  Logic(ndn::Face& face,
        const Name& syncPrefix,
        const Name& defaultUserPrefix,
        const UpdateCallback& onUpdate,
        const Name& signingId = DEFAULT_NAME,
        std::shared_ptr<Validator> validator = DEFAULT_VALIDATOR,
        const time::milliseconds& syncAckFreshness = DEFAULT_ACK_FRESHNESS,
        const NodeID session = EMPTY_NODE_ID);

  ~Logic();

  /**
   * @brief Reset the sync tree (and restart synchronization again)
   *
   * @param isOnInterest a flag that tells whether the reset is called by reset interest.
   */
  void
  reset(bool isOnInterest = false);

  /// @brief Get the name of default user.
  const Name&
  getUserPrefix() const
  {
    return m_userPrefix;
  }

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

  std::string
  getStateStr() const
  {
    return m_vv.toStr();
  }

private:
  void
  printState(std::ostream& os) const;

  /**
   * asyncSendPacket() - Send one pending packet with highest priority. Schedule
   * sending next packet with random delay.
   */
  void
  asyncSendPacket();

  void
  onSyncInterest(const Interest &interest);

  /**
   * onSyncAck() - Decode version vector from data body, and merge vector.
   */
  void
  onSyncAck(const Data &data);

  void
  onSyncNack(const Interest &interest, const lp::Nack &nack);

  void
  onSyncTimeout(const Interest &interest);

  /**
   * retxSyncInterest() - Cancel and schedule new retxSyncInterest event.
   */
  void
  retxSyncInterest();

  /**
   * sendSyncInterest() - Add one sync interest to queue. Called by
   *  Socket::retxSyncInterest(), or directly. Because this function is
   *  also called upon new msg via PublishMsg(), the shared data
   *  structures could cause race conditions.
   */
  void
  sendSyncInterest();

  /**
   * sendSyncAck() - Add an ACK into queue
   */
  void
  sendSyncAck(const Name &n);

  /**
   * mergeStateVector() - Merge state vector, return a pair of boolean
   *  representing: <my_vector_new, other_vector_new>.
   * Then, add missing data interests to data interest queue.
   */
  std::pair<bool, bool>
  mergeStateVector(const VersionVector &vv_other);

  ndn::Scheduler&
  getScheduler()
  {
    return m_scheduler;
  }

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
  Name m_syncReset;
  Name m_userPrefix;
  Name m_signingId;
  NodeID m_id;
  ndn::ScopedRegisteredPrefixHandle m_syncRegisteredPrefix;

  UpdateCallback m_onUpdate;

  // State
  VersionVector m_vv;

  std::deque<std::shared_ptr<Packet>> pending_ack;
  std::deque<std::shared_ptr<Packet>> pending_sync_interest;
  std::mutex pending_sync_interest_mutex;

  // Microseconds between sending two packets in the queues
  std::uniform_int_distribution<> packet_dist =
      std::uniform_int_distribution<>(10000, 15000);
  // Microseconds between sending two sync interests
  std::uniform_int_distribution<> retx_dist =
      std::uniform_int_distribution<>(1000000 * 0.9, 1000000 * 1.1);
  // Microseconds for sending ACK if local vector isn't newer
  std::uniform_int_distribution<> ack_dist =
      std::uniform_int_distribution<>(20000, 40000);

  // Random engine
  std::random_device rdevice_;
  std::mt19937 rengine_;

  /// @brief Freshness of sync ack
  time::milliseconds m_syncAckFreshness;

  // Security
  ndn::KeyChain m_keyChain;
  std::shared_ptr<security::Validator> m_validator;

  ndn::Scheduler m_scheduler;
  scheduler::ScopedEventId retx_event;
  scheduler::ScopedEventId packet_event;

  int m_instanceId;
  static int s_instanceCounter;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_LOGIC_HPP
