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

#include "svspubsub.hpp"
#include "store-memory.hpp"
#include "tlv.hpp"

#include <functional>

namespace ndn {
namespace svs {

const Name SVSPubSub::EMPTY_NAME;

SVSPubSub::SVSPubSub(const Name& syncPrefix,
                     const Name& nodePrefix,
                     ndn::Face& face,
                     const UpdateCallback& updateCallback,
                     const SecurityOptions& securityOptions,
                     std::shared_ptr<DataStore> dataStore)
  : m_syncPrefix(syncPrefix)
  , m_dataPrefix(nodePrefix)
  , m_onUpdate(updateCallback)
  , m_securityOptions(securityOptions)
  , m_svsync(syncPrefix, nodePrefix, face,
             std::bind(&SVSPubSub::updateCallbackInternal, this, _1),
             securityOptions, dataStore)
  , m_mappingProvider(syncPrefix, nodePrefix, face, securityOptions)
{
  m_svsync.getCore().setGetExtraBlockCallback(std::bind(&SVSPubSub::onGetExtraData, this, _1));
  m_svsync.getCore().setRecvExtraBlockCallback(std::bind(&SVSPubSub::onRecvExtraData, this, _1));
}

SeqNo
SVSPubSub::publishData(const Data& data, const Name nodePrefix)
{
  NodeID nid = nodePrefix == EMPTY_NAME ? m_dataPrefix : nodePrefix;
  SeqNo seqNo = m_svsync.publishData(data.wireEncode(), data.getFreshnessPeriod(), nid, ndn::tlv::Data);

  if (m_notificationMappingList.nodeId == EMPTY_NAME || m_notificationMappingList.nodeId == nid)
  {
    m_notificationMappingList.nodeId = nid;
    m_notificationMappingList.pairs.push_back(std::make_pair(seqNo, data.getName()));
  }

  m_mappingProvider.insertMapping(nid, seqNo, data.getName());
  return seqNo;
}

uint32_t
SVSPubSub::subscribeToProducer(const Name nodePrefix, const SubscriptionCallback callback,
                               const bool prefetch)
{
  uint32_t handle = ++m_subscriptionCount;
  Subscription sub = { handle, nodePrefix, callback, prefetch };
  m_producerSubscriptions.push_back(sub);
  return handle;
}

uint32_t
SVSPubSub::subscribeToPrefix(const Name prefix, const SubscriptionCallback callback)
{
  uint32_t handle = ++m_subscriptionCount;
  Subscription sub = { handle, prefix, callback };
  m_prefixSubscriptions.push_back(sub);
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
  for (const auto stream : info)
  {
    Name streamName(stream.nodeId);

    // Producer subscriptions
    for (const auto sub : m_producerSubscriptions)
    {
      if (sub.prefix.isPrefixOf(streamName))
      {
        // Fetch the data, validate and call callback of sub
        for (SeqNo i = stream.low; i <= stream.high; i++)
        {
          m_svsync.fetchData(stream.nodeId, i,
                             std::bind(&SVSPubSub::onSyncData, this, _1, sub, streamName, i), -1);
        }

        // Prefetch next available data
        if (sub.prefetch)
        {
          const SeqNo s = stream.high + 1;
          m_svsync.fetchData(stream.nodeId, s,
                             std::bind(&SVSPubSub::onSyncData, this, _1, sub, streamName, s), -1);
        }
      }
    }

    // Fetch all mappings if we have prefix subscription(s)
    if (m_prefixSubscriptions.size() > 0)
    {
      MissingDataInfo remainingInfo = stream;

      // Attemt to find what we already know about mapping
      for (SeqNo i = remainingInfo.low; i <= remainingInfo.high; i++)
      {
        try
        {
          Name mapping = m_mappingProvider.getMapping(stream.nodeId, i);
          for (const auto sub : m_prefixSubscriptions)
          {
           if (sub.prefix.isPrefixOf(mapping))
            {
              m_svsync.fetchData(stream.nodeId, i,
                                 std::bind(&SVSPubSub::onSyncData, this, _1, sub, streamName, i), -1);
            }
          }
          remainingInfo.low++;
        }
        catch(const std::exception& e)
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

        m_mappingProvider.fetchNameMapping(truncatedRemainingInfo, [this, remainingInfo, streamName] (MappingList list)
        {
          for (const auto sub : m_prefixSubscriptions)
          {
            for (const auto entry : list.pairs)
            {
              if (sub.prefix.isPrefixOf(entry.second))
              {
                m_svsync.fetchData(remainingInfo.nodeId, entry.first,
                                   std::bind(&SVSPubSub::onSyncData, this, _1, sub, streamName, entry.first), -1);
              }
            }
          }
        }, -1);

        remainingInfo.low += 11;
      }
    }
  }

  m_onUpdate(info);
}

bool
SVSPubSub::onSyncData(const Data& syncData, const Subscription& subscription,
                      const Name& streamName, const SeqNo seqNo)
{
  // Check for duplicate calls and push into queue
  // TODO: save memory by popping out from the queue after some time?
  {
    const size_t hash = std::hash<std::string>{}(Name(streamName).appendNumber(seqNo).toUri());
    const auto& ht = m_receivedObjectIds.get<Hashtable>();
    if (ht.find(hash) != ht.end())
      return false;
    m_receivedObjectIds.get<Sequence>().push_back(hash);
  }

  // Check if data in encapsulated
  if (syncData.getContentType() == ndn::tlv::Data)
  {
    Data encapsulatedData(syncData.getContent().blockFromValue());

    try {
      m_mappingProvider.getMapping(streamName, seqNo);
    } catch (const std::exception& ex) {
      m_mappingProvider.insertMapping(streamName, seqNo, encapsulatedData.getName());
    }

    // Return data
    SubscriptionData subData = { encapsulatedData, streamName, seqNo };

    if (static_cast<bool>(m_securityOptions.encapsulatedDataValidator))
      m_securityOptions.encapsulatedDataValidator->validate(
        encapsulatedData,
        [&] (const Data& data) { subscription.callback(subData); },
        [&] (const Data& data, const security::ValidationError error) { }
      );
    else
      subscription.callback(subData);

    return true;
  }

  return false;
}

Block
SVSPubSub::onGetExtraData(const VersionVector& vv)
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
    for (const auto p : list.pairs)
    {
      m_mappingProvider.insertMapping(list.nodeId, p.first, p.second);
    }
  }
  catch(const std::exception& e) {}
}

}  // namespace svs
}  // namespace ndn
