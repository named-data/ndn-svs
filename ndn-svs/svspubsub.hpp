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

#ifndef NDN_SVS_SVSPS_HPP
#define NDN_SVS_SVSPS_HPP

#include "common.hpp"
#include "core.hpp"
#include "store.hpp"
#include "security-options.hpp"
#include "svsync.hpp"
#include "mapping-provider.hpp"

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

namespace ndn {
namespace svs {

/**
 * @brief A pub/sub interface for SVS
 *
 * This interface provides a high level API to use SVS for pub/sub applications.
 * Every node can produce data under a prefix which is served to subscribers
 * for that stream.
 *
 * @param syncPrefix The prefix of the sync group
 * @param nodePrefix Default prefix to publish data under
 * @param face The face used to communication
 * @param updateCallback The callback function to handle state updates
 * @param securityOptions Signing and validation for sync interests and data
 * @param dataStore Interface to store data packets
 */
class SVSPubSub : noncopyable
{
public:
  SVSPubSub(const Name& syncPrefix,
            const Name& nodePrefix,
            ndn::Face& face,
            const UpdateCallback& updateCallback,
            const SecurityOptions& securityOptions = SecurityOptions::DEFAULT,
            std::shared_ptr<DataStore> dataStore = SVSync::DEFAULT_DATASTORE);

  virtual ~SVSPubSub() = default;

  /** Subscription Data type */
  struct SubscriptionData
  {
    const Data& data;
    const Name& producerPrefix;
    const SeqNo seqNo;
  };

  /** Callback returning the received data, producer and sequence number and validated */
  //using SubscriptionCallback = function<void(const Data&, const Name&, const SeqNo, const bool)>;
  using SubscriptionCallback = function<void(const SubscriptionData& subData)>;

  /**
   * @brief Publish a encapsulated Data packet in the session and trigger
   * synchronization updates.
   *
   * The encapsulated packet MUST be signed
   *
   * @param data Data packet to publish
   * @param nodePrefix Name to publish the data under
   */
  SeqNo
  publishData(const Data& data, const Name nodePrefix = EMPTY_NAME);

  /**
   * @brief Subscribe to a data producer
   *
   * @param nodePrefix Prefix of the producer
   * @param callback Callback when new data is received from the producer
   * @param prefetch Mark as low latency stream(s)
   *
   * @returns Handle to the subscription
   */
  uint32_t
  subscribeToProducer(const Name nodePrefix, const SubscriptionCallback callback,
                      const bool prefetch = false);

  /**
   * @brief Subscribe to a data prefix
   *
   * @param prefix Prefix of the application data
   * @param callback Callback when new data is received
   *
   * @returns Handle to the subscription
   */
  uint32_t
  subscribeToPrefix(const Name prefix, const SubscriptionCallback callback);

  /**
   * @brief Unsubscribe from a stream using a handle
   *
   * @param handle Handle received during subscription
   */
  void
  unsubscribe(uint32_t handle);

  /*** @brief Get the underlying sync */
  SVSync&
  getSVSync()
  {
    return m_svsync;
  }

private:
  struct Subscription
  {
    uint32_t id;
    Name prefix;
    SubscriptionCallback callback;
    bool prefetch = false;
  };

  bool
  onSyncData(const Data& syncData, const Subscription& subscription,
             const Name& streamName, const SeqNo seqNo);

  void
  updateCallbackInternal(const std::vector<ndn::svs::MissingDataInfo>& info);

  Block
  onGetExtraData(const VersionVector& vv);

  void
  onRecvExtraData(const Block& block);

public:
  static const Name EMPTY_NAME;

private:
  const Name m_syncPrefix;
  const Name m_dataPrefix;
  const UpdateCallback m_onUpdate;
  const SecurityOptions m_securityOptions;
  SVSync m_svsync;

  // Provider for mapping interests
  MappingProvider m_mappingProvider;

  // MappingList to be sent in the next update with sync interest
  MappingList m_notificationMappingList;

  uint32_t m_subscriptionCount;
  std::vector<Subscription> m_producerSubscriptions;
  std::vector<Subscription> m_prefixSubscriptions;

  struct Sequence {};
  struct Hashtable {};
  boost::multi_index_container<
    size_t,
    boost::multi_index::indexed_by<
      boost::multi_index::sequenced<boost::multi_index::tag<Sequence>>,
      boost::multi_index::hashed_non_unique<boost::multi_index::tag<Hashtable>,
                                            boost::multi_index::identity<size_t>>
    >
  > m_receivedObjectIds;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_SVSPS_HPP
