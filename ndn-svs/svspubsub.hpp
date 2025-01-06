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

#ifndef NDN_SVS_SVSPUBSUB_HPP
#define NDN_SVS_SVSPUBSUB_HPP

#include "core.hpp"
#include "mapping-provider.hpp"
#include "security-options.hpp"
#include "store.hpp"
#include "svsync.hpp"

#include <ndn-cxx/security/validator-null.hpp>

namespace ndn::svs {

/**
 * @brief Options for SVS pub/sub constructor
 */
struct SVSPubSubOptions
{
  /// @brief Interface to store data packets
  std::shared_ptr<DataStore> dataStore = SVSync::DEFAULT_DATASTORE;

  /**
   * @brief Send publication timestamp in mapping blocks.
   *
   * This option should be enabled in all instances for
   * correct usage of the maxPubAge option.
   */
  bool useTimestamp = true;

  /**
   * @brief Maximum age of publications to be fetched.
   *
   * The useTimestamp option should be enabled for this to work.
   */
  time::milliseconds maxPubAge = 0_ms;
};

/**
 * @brief A pub/sub interface for SVS.
 *
 * This interface provides a high level API to use SVS for pub/sub applications.
 * Every node can produce data under a prefix which is served to subscribers
 * for that stream.
 */
class SVSPubSub : noncopyable
{
public:
  /**
   * @brief Constructor.
   * @param syncPrefix The prefix of the sync group
   * @param nodePrefix Default prefix to publish data under
   * @param face The face used to communication
   * @param updateCallback The callback function to handle state updates
   * @param securityOptions Signing and validation for sync interests and data
   * @param dataStore Interface to store data packets
   */
  SVSPubSub(const Name& syncPrefix,
            const Name& nodePrefix,
            ndn::Face& face,
            UpdateCallback updateCallback,
            const SVSPubSubOptions& options = {},
            const SecurityOptions& securityOptions = SecurityOptions::DEFAULT);

  virtual ~SVSPubSub() = default;

  struct SubscriptionData
  {
    /** @brief Name of the received publication */
    const Name& name;

    /** @brief Payload of received data */
    const span<const uint8_t> data;

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
   * @param name name for the publication
   * @param value data buffer
   * @param nodePrefix Name to publish the data under
   * @param freshnessPeriod freshness period for the data
   * @param mappingBlocks Additional blocks to be published with the mapping
   * (use sparingly)
   */
  SeqNo publish(const Name& name,
                span<const uint8_t> value,
                const Name& nodePrefix = EMPTY_NAME,
                time::milliseconds freshnessPeriod = FRESH_FOREVER,
                std::vector<Block> mappingBlocks = {});

  /**
   * @brief Subscribe to a application name prefix.
   *
   * @param prefix Prefix of the application data
   * @param callback Callback when new data is received
   * @param packets Subscribe to the raw Data packets instead of BLOBs
   *
   * @returns Handle to the subscription
   */
  uint32_t subscribe(const Name& prefix, const SubscriptionCallback& callback, bool packets = false);

  /**
   * @brief Subscribe to a data producer
   *
   * @param nodePrefix Prefix of the producer
   * @param callback Callback when new data is received from the producer
   * @param prefetch Mark as low latency stream and prefetch data
   * @param packets Subscribe to the raw Data packets instead of BLOBs
   *
   * @returns Handle to the subscription
   */
  uint32_t subscribeToProducer(const Name& nodePrefix,
                               const SubscriptionCallback& callback,
                               bool prefetch = false,
                               bool packets = false);

  /**
   * @brief Unsubscribe from a stream using a handle
   *
   * @param handle Handle received during subscription
   */
  void unsubscribe(uint32_t handle);

  /**
   * @brief Publish a encapsulated Data packet in the session and trigger
   * synchronization updates. The encapsulated packet MUST be signed.
   *
   * This method provides a low level API to publish signed Data packets.
   * Using the publish method is recommended for most applications.
   *
   * @param data Data packet to publish
   * @param nodePrefix Name to publish the data under
   * @param mappingBlocks Additional blocks to be published with the mapping
   * (use sparingly)
   */
  SeqNo publishPacket(const Data& data,
                      const Name& nodePrefix = EMPTY_NAME,
                      std::vector<Block> mappingBlocks = {});

  /** @brief Get the underlying sync */
  SVSync& getSVSync()
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

  void onSyncData(const Data& syncData, const std::pair<Name, SeqNo>& publication);

  void updateCallbackInternal(const std::vector<MissingDataInfo>& info);

  Block onGetExtraData(const VersionVector& vv);

  void onRecvExtraData(const Block& block);

  /// @brief Insert a mapping entry into the store
  void insertMapping(const NodeID& nid, SeqNo seqNo, const Name& name, std::vector<Block> additional);

  /**
   * @brief Get and process mapping from store.
   * @returns true if new publications were queued for fetch
   * @throws std::exception error if mapping is not found
   */
  bool processMapping(const NodeID& nodeId, SeqNo seqNo);

  void fetchAll();

  void cleanUpFetch(const std::pair<Name, SeqNo>& publication);

public:
  static inline const Name EMPTY_NAME;
  static constexpr size_t MAX_DATA_SIZE = 8000;
  static constexpr time::milliseconds FRESH_FOREVER = time::years(10000); // well ...

private:
  Face& m_face;
  const Name m_syncPrefix;
  const Name m_dataPrefix;
  const UpdateCallback m_onUpdate;
  const SVSPubSubOptions m_opts;
  const SecurityOptions m_securityOptions;
  SVSync m_svsync;

  // Null validator for segment fetcher
  // TODO: use a real validator here
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
