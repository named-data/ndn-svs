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

#include "svspubsub.hpp"
#include "store-memory.hpp"
#include "tlv.hpp"
#include <ndn-cxx/util/segment-fetcher.hpp>

namespace ndn::svs {

SVSPubSub::SVSPubSub(const Name& syncPrefix,
                     const Name& nodePrefix,
                     ndn::Face& face,
                     const UpdateCallback& updateCallback,
                     const SecurityOptions& securityOptions,
                     std::shared_ptr<DataStore> dataStore)
  : m_face(face)
  , m_syncPrefix(syncPrefix)
  , m_dataPrefix(nodePrefix)
  , m_onUpdate(updateCallback)
  , m_securityOptions(securityOptions)
  , m_svsync(syncPrefix, nodePrefix, face,
             std::bind(&SVSPubSub::updateCallbackInternal, this, _1),
             securityOptions, std::move(dataStore))
  , m_mappingProvider(syncPrefix, nodePrefix, face, securityOptions)
{
  m_svsync.getCore().setGetExtraBlockCallback(std::bind(&SVSPubSub::onGetExtraData, this, _1));
  m_svsync.getCore().setRecvExtraBlockCallback(std::bind(&SVSPubSub::onRecvExtraData, this, _1));
}

SeqNo
SVSPubSub::publish(const Name& name, const span<const uint8_t>& value,
                   const Name& nodePrefix,
                   const time::milliseconds freshnessPeriod)
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
      m_svsync.insertDataSegment(segment.wireEncode(), freshnessPeriod,
                                 nid, seqNo, i, finalBlock, ndn::tlv::Data);
    }

    // Insert mapping and manually update the sequence number
    insertMapping(nid, seqNo, name);
    m_svsync.getCore().updateSeqNo(seqNo, nid);
    return seqNo;
  }
  else {
    ndn::Data data(name);
    data.setContent(value);
    data.setFreshnessPeriod(freshnessPeriod);
    m_securityOptions.dataSigner->sign(data);
    return publishPacket(data, nodePrefix);
  }
}

SeqNo
SVSPubSub::publishPacket(const Data& data, const Name& nodePrefix)
{
  NodeID nid = nodePrefix == EMPTY_NAME ? m_dataPrefix : nodePrefix;
  SeqNo seqNo = m_svsync.publishData(data.wireEncode(), data.getFreshnessPeriod(), nid, ndn::tlv::Data);
  insertMapping(nid, seqNo, data.getName());
  return seqNo;
}

void
SVSPubSub::insertMapping(const NodeID& nid, const SeqNo seqNo, const Name& name)
{
  if (m_notificationMappingList.nodeId == EMPTY_NAME || m_notificationMappingList.nodeId == nid)
  {
    m_notificationMappingList.nodeId = nid;
    m_notificationMappingList.pairs.emplace_back(seqNo, name);
  }

  m_mappingProvider.insertMapping(nid, seqNo, name);
}

uint32_t
SVSPubSub::subscribe(const Name& prefix, const SubscriptionCallback& callback, const bool packets)
{
  uint32_t handle = ++m_subscriptionCount;
  Subscription sub = Subscription{handle, prefix, callback, packets, false};
  m_prefixSubscriptions.push_back(sub);
  return handle;
}

uint32_t
SVSPubSub::subscribeToProducer(const Name& nodePrefix, const SubscriptionCallback& callback,
                               const bool prefetch, const bool packets)
{
  uint32_t handle = ++m_subscriptionCount;
  Subscription sub = { handle, nodePrefix, callback, packets, prefetch };
  m_producerSubscriptions.push_back(sub);
  return handle;
}

void
SVSPubSub::unsubscribe(uint32_t handle)
{
  auto unsub = [](uint32_t handle, std::vector<Subscription> subs)
  {
    for (size_t i = 0; i < subs.size(); i++)
    {
      if (subs[i].id == handle)
      {
        subs.erase(subs.begin() + i);
        return;
      }
    }
  };

  unsub(handle, m_producerSubscriptions);
  unsub(handle, m_prefixSubscriptions);
}

void
SVSPubSub::updateCallbackInternal(const std::vector<ndn::svs::MissingDataInfo>& info)
{
  for (const auto& stream : info)
  {
    Name streamName(stream.nodeId);

    // Producer subscriptions
    for (const auto& sub : m_producerSubscriptions)
    {
      if (sub.prefix.isPrefixOf(streamName))
      {
        // Add to fetching queue
        for (SeqNo i = stream.low; i <= stream.high; i++)
          m_fetchMap[std::pair(stream.nodeId, i)].push_back(sub);

        // Prefetch next available data
        if (sub.prefetch)
          m_svsync.fetchData(stream.nodeId, stream.high + 1, [](const ndn::Data&) {}); // do nothing with prefetch
      }
    }

    // Fetch all mappings if we have prefix subscription(s)
    if (!m_prefixSubscriptions.empty())
    {
      MissingDataInfo remainingInfo = stream;

      // Attemt to find what we already know about mapping
      for (SeqNo i = remainingInfo.low; i <= remainingInfo.high; i++)
      {
        try
        {
          Name mapping = m_mappingProvider.getMapping(stream.nodeId, i);
          for (const auto& sub : m_prefixSubscriptions)
          {
           if (sub.prefix.isPrefixOf(mapping))
            {
              m_fetchMap[std::pair(stream.nodeId, i)].push_back(sub);
            }
          }
          remainingInfo.low++;
        }
        catch (const std::exception&)
        {
          break;
        }
      }

      // Find from network what we don't yet know
      while (remainingInfo.high >= remainingInfo.low)
      {
        // Fetch a max of 10 entries per request
        // This is to ensure the mapping response does not overflow
        // TODO: implement a better solution to this issue
        MissingDataInfo truncatedRemainingInfo = remainingInfo;
        if (truncatedRemainingInfo.high - truncatedRemainingInfo.low > 10)
        {
          truncatedRemainingInfo.high = truncatedRemainingInfo.low + 10;
        }

        m_mappingProvider.fetchNameMapping(truncatedRemainingInfo,
          [this, remainingInfo, streamName] (const MappingList& list) {
            for (const auto& sub : m_prefixSubscriptions)
            {
              for (const auto& [seq, name] : list.pairs)
              {
                if (sub.prefix.isPrefixOf(name))
                {
                  m_fetchMap[std::pair(streamName, seq)].push_back(sub);
                  fetchAll();
                }
              }
            }
          }, -1);

        remainingInfo.low += 11;
      }
    }
  }

  fetchAll();
  m_onUpdate(info);
}

void
SVSPubSub::fetchAll()
{
  for (const auto& pair : m_fetchMap)
  {
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

  // Return data to packet subscriptions
  SubscriptionData subData = {
    innerData.getName(),
    innerData.getContent().value_bytes(),
    publication.first,
    publication.second,
    innerData,
  };

  // Function to return data to subscriptions
  auto returnData = [this, firstData, subData, publication] ()
  {
    bool hasFinalBlock = subData.packet.value().getFinalBlock().has_value();
    bool hasBlobSubcriptions = false;

    for (const auto& sub : this->m_fetchMap[publication])
    {
      if (sub.isPacketSubscription || !hasFinalBlock)
        sub.callback(subData);

      hasBlobSubcriptions |= !sub.isPacketSubscription;
    }

    // If there are blob subscriptions and a final block, we need to fetch remaining segments
    if (hasBlobSubcriptions && hasFinalBlock && firstData.getName().size() > 2)
    {
      // Fetch remaining segments
      auto pubName = firstData.getName().getPrefix(-2);
      Interest interest(pubName); // strip off version and segment number
      ndn::util::SegmentFetcher::Options opts;
      auto fetcher = ndn::util::SegmentFetcher::start(m_face, interest, m_nullValidator, opts);

      fetcher->onComplete.connectSingleShot([this, publication] (const ndn::ConstBufferPtr& data) {
        try {
          // Binary BLOB to return to app
          auto finalBuffer = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>(data->size()));
          auto bufSize = std::make_shared<size_t>(0);
          bool hasValidator = static_cast<bool>(m_securityOptions.encapsulatedDataValidator);

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
          auto sendFinalBuffer = [this, innerName, publication, finalBuffer, bufSize, numFailed, numValidated, numElem] ()
          {
            if (*numValidated + *numFailed != numElem)
              return;

            if (*numFailed > 0) // abort
              return this->cleanUpFetch(publication);

            // Resize buffer to actual size
            finalBuffer->resize(*bufSize);

            // Return data to packet subscriptions
            SubscriptionData subData = {
              innerName,
              *finalBuffer,
              publication.first,
              publication.second,
              std::nullopt,
            };

            for (const auto& sub : this->m_fetchMap[publication])
              if (!sub.isPacketSubscription)
                sub.callback(subData);

            this->cleanUpFetch(publication);
          };

          for (size_t i = 0; i < numElem; i++)
          {
            Data innerData(block.elements()[i]);

            // Copy actual binary data to final buffer
            auto size = innerData.getContent().value_size();
            std::memcpy(finalBuffer->data() + *bufSize, innerData.getContent().value(), size);
            *bufSize += size;

            // Validate inner data
            if (hasValidator) {
              this->m_securityOptions.encapsulatedDataValidator->validate(innerData,
                [sendFinalBuffer, numValidated] (auto&&...) {
                  *numValidated += 1;
                  sendFinalBuffer();
                }, [sendFinalBuffer, numFailed] (auto&&...) {
                  *numFailed += 1;
                  sendFinalBuffer();
                });
            } else {
              *numValidated += 1;
            }
          }

          sendFinalBuffer();
        }
        catch (const std::exception&) {
          cleanUpFetch(publication);
        }
      });
      fetcher->onError.connectSingleShot(std::bind(&SVSPubSub::cleanUpFetch, this, publication));
    }
    else
    {
      cleanUpFetch(publication);
    }
  };

  // Validate encapsulated packet
  if (static_cast<bool>(m_securityOptions.encapsulatedDataValidator)) {
    m_securityOptions.encapsulatedDataValidator->validate(
      innerData,
      [&] (const Data&) { returnData(); },
      [] (auto&&...) {});
  }
  else {
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
  try
  {
    MappingList list(block);
    for (const auto& p : list.pairs)
    {
      m_mappingProvider.insertMapping(list.nodeId, p.first, p.second);
    }
  }
  catch (const std::exception&) {}
}

} // namespace ndn::svs
