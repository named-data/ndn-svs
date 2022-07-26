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

#include "mapping-provider.hpp"
#include "tlv.hpp"

namespace ndn::svs {

MappingProvider::MappingProvider(const Name& syncPrefix,
                                 const NodeID& id,
                                 ndn::Face& face,
                                 const SecurityOptions& securityOptions)
  : m_syncPrefix(syncPrefix)
  , m_id(id)
  , m_face(face)
  , m_fetcher(face, securityOptions)
  , m_securityOptions(securityOptions)
{
  m_registeredPrefix =
    m_face.setInterestFilter(Name(m_id).append(m_syncPrefix).append("MAPPING"),
                             std::bind(&MappingProvider::onMappingQuery, this, _2),
                             [] (auto&&...) {});
}

void
MappingProvider::insertMapping(const NodeID& nodeId, const SeqNo& seqNo, const Name& appName)
{
  m_map[Name(nodeId).appendNumber(seqNo)] = appName;
}

Name
MappingProvider::getMapping(const NodeID& nodeId, const SeqNo& seqNo)
{
  return m_map.at(Name(nodeId).appendNumber(seqNo));
}

void
MappingProvider::onMappingQuery(const Interest& interest)
{
  MissingDataInfo query = parseMappingQueryDataName(interest.getName());
  MappingList queryResponse(query.nodeId);

  for (SeqNo i = query.low; i <= std::max(query.high, query.low); i++)
  {
    try {
      Name name = getMapping(query.nodeId, i);
      queryResponse.pairs.emplace_back(i, name);
    }
    catch (const std::exception&) {
      // TODO: don't give up if not everything is found
      // Instead return whatever we have and let the client request
      // the remaining mappings again
      return;
    }
  }

  // Don't reply if we have nothing
  if (queryResponse.pairs.empty())
    return;

  Data data(interest.getName());
  data.setContent(queryResponse.encode());
  data.setFreshnessPeriod(ndn::time::milliseconds(1000));
  m_securityOptions.dataSigner->sign(data);
  m_face.put(data);
}

void
MappingProvider::fetchNameMapping(const MissingDataInfo& info,
                                  const MappingListCallback& onValidated,
                                  int nRetries)
{
  TimeoutCallback onTimeout = [] (auto&&...) {};
  return fetchNameMapping(info, onValidated, onTimeout, nRetries);
}

void
MappingProvider::fetchNameMapping(const MissingDataInfo& info,
                                  const MappingListCallback& onValidated,
                                  const TimeoutCallback& onTimeout,
                                  int nRetries)
{
  Name queryName = getMappingQueryDataName(info);
  Interest interest(queryName);
  interest.setMustBeFresh(false);
  interest.setCanBePrefix(false);
  interest.setInterestLifetime(ndn::time::milliseconds(2000));

  auto onDataValidated = [this, onValidated, info] (const Data& data)
  {
    Block block = data.getContent().blockFromValue();
    MappingList list(block);

    // Add all mappings to self
    for (const auto& [seq, name] : list.pairs) {
      try {
        getMapping(info.nodeId, seq);
      }
      catch (const std::exception&) {
        insertMapping(info.nodeId, seq, name);
      }
    }

    onValidated(list);
  };

  m_fetcher.expressInterest(interest,
                            std::bind(onDataValidated, _2),
                            std::bind(onTimeout, _1), // Nack
                            onTimeout, nRetries,
                            [] (auto&&...) {});
}

Name
MappingProvider::getMappingQueryDataName(const MissingDataInfo& info)
{
  return Name(info.nodeId).append(m_syncPrefix).append("MAPPING").appendNumber(info.low).appendNumber(info.high);
}

MissingDataInfo
MappingProvider::parseMappingQueryDataName(const Name& name)
{
  MissingDataInfo info;
  info.low = name.get(-2).toNumber();
  info.high = name.get(-1).toNumber();
  info.nodeId = name.getPrefix(-3 - m_syncPrefix.size());
  return info;
}

Block
MappingList::encode() const
{
  ndn::encoding::EncodingBuffer enc;
  size_t totalLength = 0;

  for (const auto& [seq, name] : pairs)
  {
    // Name
    size_t entryLength = ndn::encoding::prependBlock(enc, name.wireEncode());

    // SeqNo
    entryLength += ndn::encoding::prependNonNegativeIntegerBlock(enc, tlv::SeqNo, seq);

    totalLength += enc.prependVarNumber(entryLength);
    totalLength += enc.prependVarNumber(tlv::MappingEntry);
    totalLength += entryLength;
  }

  totalLength += ndn::encoding::prependBlock(enc, nodeId.wireEncode());

  enc.prependVarNumber(totalLength);
  enc.prependVarNumber(tlv::MappingData);
  return enc.block();
}

MappingList::MappingList() = default;

MappingList::MappingList(const NodeID& nid)
  : nodeId(nid)
{}

MappingList::MappingList(const Block& block)
{
  block.parse();

  for (auto it = block.elements_begin(); it != block.elements_end(); it++) {
    if (it->type() == ndn::tlv::Name)
    {
      nodeId = NodeID(*it);
      continue;
    }

    if (it->type() == tlv::MappingEntry)
    {
      it->parse();

      SeqNo seqNo = ndn::encoding::readNonNegativeInteger(it->elements().at(0));
      Name name(it->elements().at(1));
      pairs.emplace_back(seqNo, name);
      continue;
    }
  }
}

} // namespace ndn::svs
