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

#include "svspubsub.hpp"

#include <ndn-cxx/util/segment-fetcher.hpp>

#include <chrono>

namespace ndn::svs {

SVSPubSub::SVSPubSub(const Name& syncPrefix,
                     const Name& nodePrefix,
                     ndn::Face& face,
                     UpdateCallback updateCallback,
                     const SVSPubSubOptions& options,
                     const SecurityOptions& securityOptions)
  : m_face(face)
  , m_syncPrefix(syncPrefix)
  , m_dataPrefix(nodePrefix)
  , m_onUpdate(std::move(updateCallback))
  , m_opts(options)
  , m_securityOptions(securityOptions)
  , m_svsync(syncPrefix,
             nodePrefix,
             face,
             std::bind(&SVSPubSub::updateCallbackInternal, this, _1),
             securityOptions,
             options.dataStore)
  , m_mappingProvider(syncPrefix, nodePrefix, face, securityOptions)
{
  m_svsync.getCore().setGetExtraBlockCallback(std::bind(&SVSPubSub::onGetExtraData, this, _1));
  m_svsync.getCore().setRecvExtraBlockCallback(std::bind(&SVSPubSub::onRecvExtraData, this, _1));
}

SeqNo
SVSPubSub::publish(const Name& name,
                   span<const uint8_t> value,
                   const Name& nodePrefix,
                   time::milliseconds freshnessPeriod,
                   std::vector<Block> mappingBlocks)
{
  // Segment the data if larger than MAX_DATA_SIZE
  if (value.size() > MAX_DATA_SIZE) {
    size_t nSegments = (value.size() / MAX_DATA_SIZE) + 1;
    auto finalBlock = name::Component::fromSegment(nSegments - 1);

    NodeID nid = nodePrefix == EMPTY_NAME ? m_dataPrefix : nodePrefix;
    SeqNo seqNo = m_svsync.getCore().getSeqNo(nid) + 1;

    for (size_t i = 0; i < nSegments; i++) {
      // Create encapsulated segment
      auto segmentName = Name(name).appendVersion(0).appendSegment(i);
      auto segment = Data(segmentName);
      segment.setFreshnessPeriod(freshnessPeriod);

      const uint8_t* segVal = value.data() + i * MAX_DATA_SIZE;
      const size_t segValSize = std::min(value.size() - i * MAX_DATA_SIZE, MAX_DATA_SIZE);
      segment.setContent(ndn::make_span(segVal, segValSize));

      segment.setFinalBlock(finalBlock);
      m_securityOptions.dataSigner->sign(segment);

      // Insert outer segment
      m_svsync.insertDataSegment(
        segment.wireEncode(), freshnessPeriod, nid, seqNo, i, finalBlock, ndn::tlv::Data);
    }

    // Insert mapping and manually update the sequence number
    insertMapping(nid, seqNo, name, mappingBlocks);
    m_svsync.getCore().updateSeqNo(seqNo, nid);
    return seqNo;
  } else {
    ndn::Data data(name);
    data.setContent(value);
    data.setFreshnessPeriod(freshnessPeriod);
    m_securityOptions.dataSigner->sign(data);
    return publishPacket(data, nodePrefix);
  }
}

SeqNo
SVSPubSub::publishPacket(const Data& data, const Name& nodePrefix, std::vector<Block> mappingBlocks)
{
  NodeID nid = nodePrefix == EMPTY_NAME ? m_dataPrefix : nodePrefix;
  SeqNo seqNo = m_svsync.publishData(data.wireEncode(), data.getFreshnessPeriod(), nid, ndn::tlv::Data);
  insertMapping(nid, seqNo, data.getName(), mappingBlocks);
  return seqNo;
}

void
SVSPubSub::insertMapping(const NodeID& nid, SeqNo seqNo, const Name& name, std::vector<Block> additional)
{
  // additional is a copy deliberately
  // this way we can add well-known mappings to the list

  // add timestamp block
  if (m_opts.useTimestamp) {
    unsigned long now = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    auto timestamp = Name::Component::fromNumber(now, tlv::TimestampNameComponent);
    additional.push_back(timestamp);
  }

  // create mapping entry
  MappingEntryPair entry = { name, additional };

  // notify subscribers in next sync interest
  if (m_notificationMappingList.nodeId == EMPTY_NAME || m_notificationMappingList.nodeId == nid) {
    m_notificationMappingList.nodeId = nid;
    m_notificationMappingList.pairs.push_back({ seqNo, entry });
  }

  // send mapping to provider
  m_mappingProvider.insertMapping(nid, seqNo, entry);
}

uint32_t
SVSPubSub::subscribe(const Name& prefix, const SubscriptionCallback& callback, bool packets)
{
  uint32_t handle = ++m_subscriptionCount;
  Subscription sub = { handle, prefix, callback, packets, false };
  m_prefixSubscriptions.push_back(sub);
  return handle;
}

uint32_t
SVSPubSub::subscribeToProducer(const Name& nodePrefix,
                               const SubscriptionCallback& callback,
                               bool prefetch,
                               bool packets)
{
  uint32_t handle = ++m_subscriptionCount;
  Subscription sub = { handle, nodePrefix, callback, packets, prefetch };
  m_producerSubscriptions.push_back(sub);
  return handle;
}

void
SVSPubSub::unsubscribe(uint32_t handle)
{
  auto unsub = [handle](std::vector<Subscription>& subs) {
    for (auto it = subs.begin(); it != subs.end(); ++it) {
      if (it->id == handle) {
        subs.erase(it);
        return;
      }
    }
  };

  unsub(m_producerSubscriptions);
  unsub(m_prefixSubscriptions);
}

void
SVSPubSub::updateCallbackInternal(const std::vector<MissingDataInfo>& info)
{
  for (const auto& stream : info) {
    Name streamName(stream.nodeId);

    // Producer subscriptions
    for (const auto& sub : m_producerSubscriptions) {
      if (sub.prefix.isPrefixOf(streamName)) {
        // Add to fetching queue
        for (SeqNo i = stream.low; i <= stream.high; i++)
          m_fetchMap[std::pair(stream.nodeId, i)].push_back(sub);

        // Prefetch next available data
        if (sub.prefetch)
          m_svsync.fetchData(stream.nodeId, stream.high + 1, [](auto&&...) {}); // do nothing with prefetch
      }
    }

    // Fetch all mappings if we have prefix subscription(s)
    if (!m_prefixSubscriptions.empty()) {
      MissingDataInfo remainingInfo = stream;

      // Attemt to find what we already know about mapping
      // This typically refers to the Sync Interest mapping optimization,
      // where the Sync Interest contains the notification mapping list
      for (SeqNo i = remainingInfo.low; i <= remainingInfo.high; i++) {
        try {
          // throws if mapping not found
          this->processMapping(stream.nodeId, i);
          remainingInfo.low++;
        } catch (const std::exception&) {
          break;
        }
      }

      // Find from network what we don't yet know
      while (remainingInfo.high >= remainingInfo.low) {
        // Fetch a max of 10 entries per request
        // This is to ensure the mapping response does not overflow
        // TODO: implement a better solution to this issue
        MissingDataInfo truncatedRemainingInfo = remainingInfo;
        if (truncatedRemainingInfo.high - truncatedRemainingInfo.low > 10) {
          truncatedRemainingInfo.high = truncatedRemainingInfo.low + 10;
        }

        m_mappingProvider.fetchNameMapping(
          truncatedRemainingInfo,
          [this, remainingInfo, streamName](const MappingList& list) {
            bool queued = false;
            for (const auto& [seq, mapping] : list.pairs)
              queued |= this->processMapping(streamName, seq);

            if (queued)
              this->fetchAll();
          },
          -1);

        remainingInfo.low += 11;
      }
    }
  }

  fetchAll();
  m_onUpdate(info);
}

bool
SVSPubSub::processMapping(const NodeID& nodeId, SeqNo seqNo)
{
  // this will throw if mapping not found
  auto mapping = m_mappingProvider.getMapping(nodeId, seqNo);

  // check if timestamp is too old
  if (m_opts.maxPubAge > 0_ms) {
    // look for the additional timestamp block
    // if no timestamp block is present, we just skip this step
    for (const auto& block : mapping.second) {
      if (block.type() != tlv::TimestampNameComponent)
        continue;

      unsigned long now = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();

      unsigned long pubTime = Name::Component(block).toNumber();
      unsigned long maxAge = time::microseconds(m_opts.maxPubAge).count();

      if (now - pubTime > maxAge)
        return false;
    }
  }

  // check if known mapping matches subscription
  bool queued = false;
  for (const auto& sub : m_prefixSubscriptions) {
    if (sub.prefix.isPrefixOf(mapping.first)) {
      m_fetchMap[std::pair(nodeId, seqNo)].push_back(sub);
      queued = true;
    }
  }

  return queued;
}

void
SVSPubSub::fetchAll()
{
  for (const auto& pair : m_fetchMap) {
    // Check if already fetching this publication
    auto key = pair.first;
    if (m_fetchingMap.find(key) != m_fetchingMap.end())
      continue;
    m_fetchingMap[key] = true;

    // Fetch first data packet
    const auto& [nodeId, seqNo] = key;
    m_svsync.fetchData(nodeId, seqNo, std::bind(&SVSPubSub::onSyncData, this, _1, key), 12);
  }
}

void
SVSPubSub::onSyncData(const Data& firstData, const std::pair<Name, SeqNo>& publication)
{
  // Make sure the data is encapsulated
  if (firstData.getContentType() != ndn::tlv::Data) {
    m_fetchingMap[publication] = false;
    return;
  }

  // Unwrap
  Data innerData(firstData.getContent().blockFromValue());
  auto innerContent = innerData.getContent();

  // Return data to packet subscriptions
  SubscriptionData subData = {
    innerData.getName(), innerContent.value_bytes(), publication.first, publication.second, innerData,
  };

  // Function to return data to subscriptions
  auto returnData = [this, firstData, subData, publication]() {
    bool hasFinalBlock = subData.packet.value().getFinalBlock().has_value();
    bool hasBlobSubcriptions = false;

    for (const auto& sub : this->m_fetchMap[publication]) {
      if (sub.isPacketSubscription || !hasFinalBlock)
        sub.callback(subData);

      hasBlobSubcriptions |= !sub.isPacketSubscription;
    }

    // If there are blob subscriptions and a final block, we need to fetch
    // remaining segments
    if (hasBlobSubcriptions && hasFinalBlock && firstData.getName().size() > 2) {
      // Fetch remaining segments
      auto pubName = firstData.getName().getPrefix(-2);
      Interest interest(pubName); // strip off version and segment number
      ndn::SegmentFetcher::Options opts;
      auto fetcher = ndn::SegmentFetcher::start(m_face, interest, m_nullValidator, opts);

      fetcher->onComplete.connectSingleShot([this, publication](const ndn::ConstBufferPtr& data) {
        try {
          // Binary BLOB to return to app
          auto finalBuffer = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>(data->size()));
          auto bufSize = std::make_shared<size_t>(0);
          bool hasValidator = !!m_securityOptions.encapsulatedDataValidator;

          // Read all TLVs as Data packets till the end of data buffer
          ndn::Block block(6, data);
          block.parse();

          // Number of elements validated / failed to validate
          auto numValidated = std::make_shared<size_t>(0);
          auto numFailed = std::make_shared<size_t>(0);
          auto numElem = block.elements_size();

          if (numElem == 0)
            return this->cleanUpFetch(publication);

          // Get name of inner data
          auto innerName = Data(block.elements()[0]).getName().getPrefix(-2);

          // Function to send final buffer to subscriptions if possible
          auto sendFinalBuffer =
            [this, innerName, publication, finalBuffer, bufSize, numFailed, numValidated, numElem] {
              if (*numValidated + *numFailed != numElem)
                return;

              if (*numFailed > 0) // abort
                return this->cleanUpFetch(publication);

              // Resize buffer to actual size
              finalBuffer->resize(*bufSize);

              // Return data to packet subscriptions
              SubscriptionData subData = {
                innerName, *finalBuffer, publication.first, publication.second, std::nullopt,
              };

              for (const auto& sub : this->m_fetchMap[publication])
                if (!sub.isPacketSubscription)
                  sub.callback(subData);

              this->cleanUpFetch(publication);
            };

          for (size_t i = 0; i < numElem; i++) {
            Data innerData(block.elements()[i]);

            // Copy actual binary data to final buffer
            auto size = innerData.getContent().value_size();
            std::memcpy(finalBuffer->data() + *bufSize, innerData.getContent().value(), size);
            *bufSize += size;

            // Validate inner data
            if (hasValidator) {
              this->m_securityOptions.encapsulatedDataValidator->validate(
                innerData,
                [sendFinalBuffer, numValidated](auto&&...) {
                  *numValidated += 1;
                  sendFinalBuffer();
                },
                [sendFinalBuffer, numFailed](auto&&...) {
                  *numFailed += 1;
                  sendFinalBuffer();
                });
            } else {
              *numValidated += 1;
            }
          }

          sendFinalBuffer();
        } catch (const std::exception&) {
          cleanUpFetch(publication);
        }
      });
      fetcher->onError.connectSingleShot(std::bind(&SVSPubSub::cleanUpFetch, this, publication));
    } else {
      cleanUpFetch(publication);
    }
  };

  // Validate encapsulated packet
  if (m_securityOptions.encapsulatedDataValidator) {
    m_securityOptions.encapsulatedDataValidator->validate(
      innerData, [&](auto&&...) { returnData(); }, [](auto&&...) {});
  } else {
    returnData();
  }
}

void
SVSPubSub::cleanUpFetch(const std::pair<Name, SeqNo>& publication)
{
  m_fetchMap.erase(publication);
  m_fetchingMap.erase(publication);
}

Block
SVSPubSub::onGetExtraData(const VersionVector&)
{
  MappingList copy = m_notificationMappingList;
  m_notificationMappingList = MappingList();
  return copy.encode();
}

void
SVSPubSub::onRecvExtraData(const Block& block)
{
  try {
    MappingList list(block);
    for (const auto& p : list.pairs) {
      m_mappingProvider.insertMapping(list.nodeId, p.first, p.second);
    }
  } catch (const std::exception&) {
  }
}

} // namespace ndn::svs
