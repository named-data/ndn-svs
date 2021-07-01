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
  , m_mappingProvider(syncPrefix, nodePrefix.toUri(), face, securityOptions)
{}

SeqNo
SVSPubSub::publishData(const Data& data, const Name nodePrefix)
{
  NodeID nid = nodePrefix == EMPTY_NAME ? m_dataPrefix.toUri() : nodePrefix.toUri();
  SeqNo seqNo = m_svsync.publishData(data.wireEncode(), data.getFreshnessPeriod(), nid, ndn::tlv::Data);
  m_mappingProvider.insertMapping(nid, seqNo, data.getName());
  return seqNo;
}

uint32_t
SVSPubSub::subscribeToProducer(const Name nodePrefix, const SubscriptionCallback callback)
{
  uint32_t handle = ++m_subscriptionCount;
  Subscription sub = { handle, nodePrefix, callback };
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
    Name streamName(stream.session);

    // Producer subscriptions
    for (const auto sub : m_producerSubscriptions)
    {
      if (sub.prefix.isPrefixOf(streamName))
      {
        // Fetch the data, validate and call callback of sub
        for (SeqNo i = stream.low; i <= stream.high; i++)
        {
          m_svsync.fetchData(stream.session, i,
                             std::bind(&SVSPubSub::onSyncData, this, _1, sub, streamName, i), -1);
        }
      }
    }

    // Fetch all mappings if we have prefix subscription(s)
    if (m_prefixSubscriptions.size() > 0)
    {
      m_mappingProvider.fetchNameMapping(stream, [this, stream, streamName] (MappingProvider::MappingList list)
      {
        for (const auto sub : m_prefixSubscriptions)
        {
          for (const auto entry : list)
          {
            if (sub.prefix.isPrefixOf(entry.second))
            {
              m_svsync.fetchData(stream.session, entry.first,
                                 std::bind(&SVSPubSub::onSyncData, this, _1, sub, streamName, entry.first), -1);
            }
          }
        }
      }, -1);
    }
  }

  m_onUpdate(info);
}

bool
SVSPubSub::onSyncData(const Data& syncData, const Subscription& subscription,
                      const Name& streamName, const SeqNo seqNo)
{
  // Check if data in encapsulated
  if (syncData.getContentType() == ndn::tlv::Data)
  {
    Data encapsulatedData(syncData.getContent().blockFromValue());

    // Return data
    SubscriptionData subData = { encapsulatedData, streamName, seqNo, false };

    if (static_cast<bool>(m_securityOptions.validator))
      m_securityOptions.validator->validate(
        encapsulatedData,
        [&] (const Data& data)
        {
          subData.validated = true;
          subscription.callback(subData);
        },
        [&] (const Data& data, const security::ValidationError error)
        {
          subscription.callback(subData);
        }
      );
    else
      subscription.callback(subData);

    return true;
  }

  return false;
}

}  // namespace svs
}  // namespace ndn
