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
  , m_fetcher(face)
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
  m_map[nodeId + "/" + std::to_string(seqNo)] = appName;
}

Name
MappingProvider::getMapping(const NodeID& nodeId, const SeqNo& seqNo)
{
  return m_map.at(nodeId + "/" + std::to_string(seqNo));
}

void
MappingProvider::onMappingQuery(const Interest& interest)
{
  MissingDataInfo query = parseMappingQueryDataName(interest.getName());
  std::vector<std::pair<SeqNo, Name>> queryResponse;

  for (SeqNo i = query.low; i <= std::max(query.high, query.low); i++)
  {
    try {
      Name name = getMapping(query.session, i);
      queryResponse.push_back(std::make_pair(i, name));
    } catch (const std::exception& ex) {}
  }

  ndn::encoding::Encoder enc;
  size_t totalLength = 0;

  for (const auto p : queryResponse)
  {
    size_t entryLength = enc.prependBlock(p.second.wireEncode());
    size_t valLength = enc.prependNonNegativeInteger(p.first);
    entryLength += enc.prependVarNumber(valLength);
    entryLength += enc.prependVarNumber(tlv::VersionVectorValue);
    entryLength += valLength;

    totalLength += enc.prependVarNumber(entryLength);
    totalLength += enc.prependVarNumber(tlv::MappingEntry);
    totalLength += entryLength;
  }

  totalLength += enc.prependVarNumber(totalLength);
  totalLength += enc.prependVarNumber(tlv::MappingData);

  Data data(interest.getName());
  data.setContent(enc.block());
  data.setFreshnessPeriod(ndn::time::milliseconds(1000));
  m_keyChain.sign(data, m_securityOptions.dataSigningInfo);
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
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  auto onDataValidated = [onValidated] (const Data& data)
  {
    MappingList list;

    Block block = data.getContent().blockFromValue();
    block.parse();

    for (auto it = block.elements_begin(); it < block.elements_end(); it++) {
      if (it->type() != tlv::MappingEntry) continue;
      it->parse();

      SeqNo seqNo = ndn::encoding::readNonNegativeInteger(it->elements().at(0));
      Name name(it->elements().at(1));
      list.push_back(std::make_pair(seqNo, name));
    }

    onValidated(list);
  };

  auto onValidationFailed = [] (const Data& data, const ValidationError& error) {};

  m_fetcher.expressInterest(interest,
                            bind(&MappingProvider::onData, this, _1, _2, onDataValidated, onValidationFailed),
                            bind(onTimeout, _1), // Nack
                            onTimeout, nRetries);
}

Name
MappingProvider::getMappingQueryDataName(const MissingDataInfo& info)
{
  return Name(info.session).append(m_syncPrefix).append("MAPPING").appendNumber(info.low).appendNumber(info.high);
}

MissingDataInfo
MappingProvider::parseMappingQueryDataName(const Name& name)
{
  MissingDataInfo info;
  info.low = name.get(-2).toNumber();
  info.high = name.get(-1).toNumber();
  info.session = name.getPrefix(-3 - m_syncPrefix.size()).toUri();
  return info;
}

void
MappingProvider::onData(const Interest& interest, const Data& data,
                        const DataValidatedCallback& onValidated,
                        const DataValidationErrorCallback& onFailed)
{
  if (static_cast<bool>(m_securityOptions.validator))
    m_securityOptions.validator->validate(data, onValidated, onFailed);
  else
    onValidated(data);
}

}  // namespace svs
}  // namespace ndn
