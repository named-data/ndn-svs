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

#include "mapping-provider.hpp"
#include "tlv.hpp"

namespace ndn {
namespace svs {

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
                             bind(&MappingProvider::onMappingQuery, this, _2),
                             [] (const Name& prefix, const std::string& msg) {});
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
      queryResponse.pairs.push_back(std::make_pair(i, name));
    } catch (const std::exception& ex) {
      // TODO: don't give up if not everything is found
      // Instead return whatever we have and let the client request
      // the remaining mappings again
      return;
    }
  }

  // Don't reply if we have nothing
  if (queryResponse.pairs.size() == 0)
    return;

  Data data(interest.getName());
  data.setContent(queryResponse.encode());
  data.setFreshnessPeriod(ndn::time::milliseconds(1000));
  m_securityOptions.dataSigner->sign(data);
  m_face.put(data);
}

void
MappingProvider::fetchNameMapping(const MissingDataInfo info,
                                  const MappingListCallback& onValidated,
                                  const int nRetries)
{
  TimeoutCallback onTimeout =
    [] (const Interest& interest) {};
  return fetchNameMapping(info, onValidated, onTimeout, nRetries);
}

void
MappingProvider::fetchNameMapping(const MissingDataInfo info,
                                  const MappingListCallback& onValidated,
                                  const TimeoutCallback& onTimeout,
                                  const int nRetries)
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
    for (const auto entry : list.pairs) {
      try {
        getMapping(info.nodeId, entry.first);
      } catch (const std::exception& ex) {
        insertMapping(info.nodeId, entry.first, entry.second);
      }
    }

    onValidated(list);
  };

  auto onValidationFailed = [] (const Data& data, const ValidationError& error) {};

  m_fetcher.expressInterest(interest,
                            bind(onDataValidated, _2),
                            bind(onTimeout, _1), // Nack
                            onTimeout, nRetries,
                            onValidationFailed);
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
MappingList::encode()
{
  ndn::encoding::Encoder enc;
  size_t totalLength = 0;

  for (const auto p : pairs)
  {
    size_t entryLength = enc.prependBlock(p.second.wireEncode());
    size_t valLength = enc.prependNonNegativeInteger(p.first);
    entryLength += enc.prependVarNumber(valLength);
    entryLength += enc.prependVarNumber(tlv::SeqNo);
    entryLength += valLength;

    totalLength += enc.prependVarNumber(entryLength);
    totalLength += enc.prependVarNumber(tlv::MappingEntry);
    totalLength += entryLength;
  }

  totalLength += enc.prependBlock(nodeId.wireEncode());
  totalLength += enc.prependVarNumber(totalLength);
  totalLength += enc.prependVarNumber(tlv::MappingData);

  return enc.block();
}

MappingList::MappingList(const Block& block)
{
  block.parse();

  for (auto it = block.elements_begin(); it < block.elements_end(); it++) {
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
      pairs.push_back(std::make_pair(seqNo, name));
      continue;
    }
  }
}

MappingList::MappingList()
{}

MappingList::MappingList(const NodeID& nid)
  : nodeId(nid)
{}

}  // namespace svs
}  // namespace ndn
