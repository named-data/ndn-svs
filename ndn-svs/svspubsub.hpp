/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2022 University of California, Los Angeles
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

#ifndef NDN_SVS_SVSPUBSUB_HPP
#define NDN_SVS_SVSPUBSUB_HPP

#include "core.hpp"
#include "mapping-provider.hpp"
#include "store.hpp"
#include "security-options.hpp"
#include "svsync.hpp"

#include <ndn-cxx/security/validator-null.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

namespace ndn::svs {

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

  virtual
  ~SVSPubSub() = default;

  struct SubscriptionData
  {
    /** @brief Name of the received publication */
    const Name& name;

    /** @brief Payload of received data */
    const span<const uint8_t>& data;

    /** @brief Producer of the publication */
    const Name& producerPrefix;

    /** @brief The sequence number of the publication */
    const SeqNo seqNo;

    /** @brief Received data packet, only for "packet" subscriptions */
    const std::optional<Data>& packet;
  };

  /** Callback returning the received data, producer and sequence number */
  using SubscriptionCallback = std::function<void(const SubscriptionData&)>;

  /**
   * @brief Sign and publish a binary BLOB on the pub/sub group.
   *
   * The blob must fit inside a single Data packet.
   *
   * @param name name for the publication
   * @param value raw buffer
   * @param length length of buffer
   * @param nodePrefix Name to publish the data under
   * @param freshnessPeriod freshness period for the data
   */
  SeqNo
  publish(const Name& name, const span<const uint8_t>& value,
          const Name& nodePrefix = EMPTY_NAME,
          const time::milliseconds freshnessPeriod = FRESH_FOREVER);

  /**
   * @brief Subscribe to a application name prefix.
   *
   * @param prefix Prefix of the application data
   * @param callback Callback when new data is received
   * @param packets Subscribe to the raw Data packets instead of BLOBs
   *
   * @returns Handle to the subscription
   */
  uint32_t
  subscribe(const Name& prefix, const SubscriptionCallback& callback, const bool packets = false);

  /**
   * @brief Subscribe to a data producer
   *
   * This method provides a low level API to receive Data packets.
   * Use subscribeToProducer instead if you want to receive binary BLOBs.
   *
   * @param nodePrefix Prefix of the producer
   * @param callback Callback when new data is received from the producer
   * @param prefetch Mark as low latency stream(s)
   * @param packets Subscribe to the raw Data packets instead of BLOBs
   *
   * @returns Handle to the subscription
   */
  uint32_t
  subscribeToProducer(const Name& nodePrefix, const SubscriptionCallback& callback,
                      const bool prefetch = false, const bool packets = false);

  /**
   * @brief Unsubscribe from a stream using a handle
   *
   * @param handle Handle received during subscription
   */
  void
  unsubscribe(uint32_t handle);

  /**
   * @brief Publish a encapsulated Data packet in the session and trigger
   * synchronization updates. The encapsulated packet MUST be signed.
   *
   * This method provides a low level API to publish signed Data packets.
   * Use publish to publish a binary BLOB using the signing options provided.
   *
   * @param data Data packet to publish
   * @param nodePrefix Name to publish the data under
   */
  SeqNo
  publishPacket(const Data& data, const Name& nodePrefix = EMPTY_NAME);

  /** @brief Get the underlying sync */
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
    bool isPacketSubscription;
    bool prefetch;
  };

  void
  onSyncData(const Data& syncData, const std::pair<Name, SeqNo>& publication);

  void
  updateCallbackInternal(const std::vector<ndn::svs::MissingDataInfo>& info);

  Block
  onGetExtraData(const VersionVector& vv);

  void
  onRecvExtraData(const Block& block);

  void
  insertMapping(const NodeID& nid, const SeqNo seqNo, const Name& name);

  void
  fetchAll();

  void
  cleanUpFetch(const std::pair<Name, SeqNo>& publication);

public:
  static inline const Name EMPTY_NAME;
  static inline const size_t MAX_DATA_SIZE = 8000;
  static inline const time::milliseconds FRESH_FOREVER = time::years(10000); // well ...

private:
  Face& m_face;
  const Name m_syncPrefix;
  const Name m_dataPrefix;
  const UpdateCallback m_onUpdate;
  const SecurityOptions m_securityOptions;
  SVSync m_svsync;

  // Null validator for segment fetcher
  ndn::security::ValidatorNull m_nullValidator;

  // Provider for mapping interests
  MappingProvider m_mappingProvider;

  // MappingList to be sent in the next update with sync interest
  MappingList m_notificationMappingList;

  uint32_t m_subscriptionCount;
  std::vector<Subscription> m_producerSubscriptions;
  std::vector<Subscription> m_prefixSubscriptions;

  // Queue of publications to fetch
  std::map<std::pair<Name, SeqNo>, std::vector<Subscription>> m_fetchMap;
  std::map<std::pair<Name, SeqNo>, bool> m_fetchingMap;
};

} // namespace ndn::svs

#endif // NDN_SVS_SVSPUBSUB_HPP
