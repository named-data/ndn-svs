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

#ifndef NDN_SVS_SOCKET_HPP
#define NDN_SVS_SOCKET_HPP

#include "common.hpp"
#include "version-vector.hpp"

#include <deque>
#include <random>
#include <mutex>

#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/security/validator.hpp>
#include <ndn-cxx/ims/in-memory-storage-persistent.hpp>

using ndn::security::ValidationError;
using ndn::security::Validator;

namespace ndn {
namespace svs {

/**
 * @brief A simple interface to interact with client code
 *
 * Though it is called Socket, it is not a real socket. It just trying to provide
 * a simplified interface for data publishing and fetching.
 *
 * This interface simplify data publishing.  Client can simply dump raw data
 * into this interface without handling the SVS specific details, such
 * as sequence number and session id.
 *
 * This interface also simplifies data fetching.  Client only needs to provide a
 * data fetching strategy (through a updateCallback).
 */
class Socket : noncopyable
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
  Socket(const Name& syncPrefix,
         const Name& userPrefix,
         ndn::Face& face,
         const UpdateCallback& updateCallback,
         const Name& signingId = DEFAULT_NAME,
         std::shared_ptr<Validator> validator = DEFAULT_VALIDATOR);

  ~Socket();

  using DataValidatedCallback = function<void(const Data&)>;

  using DataValidationErrorCallback = function<void(const Data&, const ValidationError& error)> ;

  /**
   * @brief Publish a data packet in the session and trigger synchronization updates
   *
   * This method will create a data packet with the supplied content.
   * The packet name is the local session + seqNo.
   * The seqNo is automatically maintained by internal Logic.
   *
   * @throws It will throw error, if the prefix does not exist
   *
   * @param buf Pointer to the bytes in content
   * @param len size of the bytes in content
   * @param freshness FreshnessPeriod of the data packet.
   * @param prefix The user prefix that will be used to publish the data.
   */
  void
  publishData(const uint8_t* buf, size_t len, const ndn::time::milliseconds& freshness,
              const Name& prefix = DEFAULT_PREFIX);

  /**
   * @brief Publish a data packet in the session and trigger synchronization updates
   *
   * This method will create a data packet with the supplied content.
   * The packet name is the local session + seqNo.
   * The seqNo is set by the application.
   *
   * @throws It will throw error, if the prefix does not exist in m_logic
   *
   * @param buf Pointer to the bytes in content
   * @param len size of the bytes in content
   * @param freshness FreshnessPeriod of the data packet.
   * @param seqNo Sequence number of the data
   * @param prefix The user prefix that will be used to publish the data.
   */
  void
  publishData(const uint8_t* buf, size_t len, const ndn::time::milliseconds& freshness,
              const uint64_t& seqNo, const Name& prefix = DEFAULT_PREFIX);

  /**
   * @brief Publish a data packet in the session and trigger synchronization updates
   *
   * This method will create a data packet with the supplied content.
   * The packet name is the local session + seqNo.
   * The seqNo is automatically maintained by internal Logic.
   *
   * @throws It will throw error, if the prefix does not exist in m_logic
   *
   * @param content Block that will be set as the content of the data packet.
   * @param freshness FreshnessPeriod of the data packet.
   * @param prefix The user prefix that will be used to publish the data.
   */
  void
  publishData(const Block& content, const ndn::time::milliseconds& freshness,
              const Name& prefix = DEFAULT_PREFIX);

  /**
   * @brief Publish a data packet in the session and trigger synchronization updates
   *
   * This method will create a data packet with the supplied content.
   * The packet name is the local session + seqNo.
   * The seqNo is set by the application.
   *
   * @throws It will throw error, if the prefix does not exist in m_logic
   *
   * @param content Block that will be set as the content of the data packet.
   * @param freshness FreshnessPeriod of the data packet.
   * @param seqNo Sequence number of the data
   * @param prefix The user prefix that will be used to publish the data.
   */
  void
  publishData(const Block& content, const ndn::time::milliseconds& freshness,
              const uint64_t& seqNo, const Name& prefix = DEFAULT_PREFIX);

  /**
   * @brief Retrive a data packet with a particular seqNo from a session
   *
   * @param sessionName The name of the target session.
   * @param seq The seqNo of the data packet.
   * @param onValidated The callback when the retrieved packet has been validated.
   * @param nRetries The number of retries.
   */
  void
  fetchData(const NodeID& nid, const SeqNo& seq,
            const DataValidatedCallback& onValidated,
            int nRetries = 0);

  /**
   * @brief Retrive a data packet with a particular seqNo from a session
   *
   * @param sessionName The name of the target session.
   * @param seq The seqNo of the data packet.
   * @param onValidated The callback when the retrieved packet has been validated.
   * @param onValidationFailed The callback when the retrieved packet failed validation.
   * @param onTimeout The callback when data is not retrieved.
   * @param nRetries The number of retries.
   */
  void
  fetchData(const NodeID& nid, const SeqNo& seq,
            const DataValidatedCallback& onValidated,
            const DataValidationErrorCallback& onValidationFailed,
            const ndn::TimeoutCallback& onTimeout,
            int nRetries = 0);

public:
  static const ndn::Name DEFAULT_NAME;
  static const ndn::Name DEFAULT_PREFIX;
  static const std::shared_ptr<Validator> DEFAULT_VALIDATOR;

private:
  using RegisteredPrefixList = std::unordered_map<ndn::Name, ndn::RegisteredPrefixHandle>;

  void
  asyncSendPacket();

  void
  onSyncInterest(const Interest &interest);

  void
  onDataInterest(const Interest &interest);

  void
  onSyncAck(const Data &data);

  void
  onSyncNack(const Interest &interest, const lp::Nack &nack);

  void
  onSyncTimeout(const Interest &interest);

  void
  retxSyncInterest();

  void
  sendSyncInterest();

  void
  sendSyncAck(const Name &n);

  void
  onData(const Interest& interest, const Data& data,
         const DataValidatedCallback& dataCallback,
         const DataValidationErrorCallback& failCallback);

  void
  onDataTimeout(const Interest& interest, int nRetries,
                const DataValidatedCallback& dataCallback,
                const DataValidationErrorCallback& failCallback);

  void
  onDataValidationFailed(const Data& data,
                         const ValidationError& error);

  std::pair<bool, bool>
  mergeStateVector(const VersionVector &vv_other);

  std::function<void(const std::string &)> onMsg;

  // State
  NodeID m_id;
  Name m_syncPrefix;
  Name m_userPrefix;
  Name m_signingId;
  Face& m_face;
  KeyChain m_keyChain;
  VersionVector m_vv;

  // Validator
  std::shared_ptr<Validator> m_validator;

  // Callback
  UpdateCallback m_onUpdate;

  // Mult-level queues
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

  ndn::Scheduler m_scheduler;
  RegisteredPrefixList m_registeredPrefixList;
  ndn::InMemoryStoragePersistent m_ims;

  scheduler::EventId retx_event;    // will send retx next sync intrest
  scheduler::EventId packet_event;  // Will send next packet async
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_SOCKET_HPP
