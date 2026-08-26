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

#include "mapping-provider.hpp"
#include "tlv.hpp"

#include <ndn-cxx/util/time.hpp>

namespace ndn::svs {

namespace {

Name
makeMappingKey(const NodeID& nodeId, BootstrapTime bootstrapTime, SeqNo seqNo)
{
  return Name(nodeId).append(Name::Component::fromTimestamp(
                       time::fromUnixTimestamp(time::seconds(bootstrapTime))))
                     .append(Name::Component::fromSequenceNumber(seqNo));
}

} // namespace

MappingList::MappingList() = default;

MappingList::MappingList(const NodeID& nid)
  : nodeId(nid)
{
}

MappingList::MappingList(const Block& block, BootstrapTime bootstrapTime)
{
  block.parse();

  for (auto it = block.elements_begin(); it != block.elements_end(); it++) {
    if (it->type() == ndn::tlv::Name) {
      nodeId = NodeID(*it);
      continue;
    }

    if (it->type() == tlv::MappingEntry) {
      it->parse();

      if (it->elements().size() < 2 || it->elements().at(0).type() != tlv::MappingSeqNo ||
          it->elements().at(1).type() != ndn::tlv::Name) {
        NDN_THROW(ndn::tlv::Error("MappingEntry SeqNo/Name", it->elements().at(0).type()));
      }
      SeqNo seqNo = ndn::encoding::readNonNegativeInteger(it->elements().at(0));
      Name name(it->elements().at(1));

      // Additional blocks
      std::vector<Block> blocks;
      for (auto it2 = it->elements().begin() + 2; it2 != it->elements().end(); it2++)
        blocks.push_back(*it2);

      pairs.push_back({ bootstrapTime, seqNo, std::make_pair(name, blocks) });
      continue;
    }
  }
}

Block
MappingList::encode() const
{
  ndn::encoding::EncodingBuffer enc;
  size_t totalLength = 0;

  for (const auto& entry : pairs) {
    size_t entryLength = 0;

    // Additional blocks
    for (const auto& block : entry.mapping.second)
      entryLength += ndn::encoding::prependBlock(enc, block);

    // Name
    entryLength += ndn::encoding::prependBlock(enc, entry.mapping.first.wireEncode());

    entryLength += ndn::encoding::prependNonNegativeIntegerBlock(enc, tlv::MappingSeqNo,
                                                                 entry.seqNo);

    totalLength += enc.prependVarNumber(entryLength);
    totalLength += enc.prependVarNumber(tlv::MappingEntry);
    totalLength += entryLength;
  }

  totalLength += ndn::encoding::prependBlock(enc, nodeId.wireEncode());

  enc.prependVarNumber(totalLength);
  enc.prependVarNumber(tlv::MappingData);
  return enc.block();
}

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
  m_interestFilter = m_face.setInterestFilter(
    InterestFilter(Name(m_id).append(m_syncPrefix)),
    std::bind(&MappingProvider::onMappingQuery, this, _2));
}

void
MappingProvider::insertMapping(const NodeID& nodeId, BootstrapTime bootstrapTime,
                               const SeqNo& seqNo, const MappingEntryPair& entry)
{
  m_map[makeMappingKey(nodeId, bootstrapTime, seqNo)] = entry;
}

MappingEntryPair
MappingProvider::getMapping(const NodeID& nodeId, BootstrapTime bootstrapTime,
                            const SeqNo& seqNo)
{
  return m_map.at(makeMappingKey(nodeId, bootstrapTime, seqNo));
}

MappingEntryPair
MappingProvider::getLatestMapping(const NodeID& nodeId, SeqNo seqNo,
                                  BootstrapTime& bootstrapTime)
{
  const Name nodeName(nodeId);
  bool found = false;
  MappingEntryPair result;
  BootstrapTime latest = 0;
  for (const auto& [key, mapping] : m_map) {
    if (key.size() < 2 || key.getPrefix(key.size() - 2) != nodeName ||
        !key.get(-2).isTimestamp() || !key.get(-1).isSequenceNumber() ||
        key.get(-1).toSequenceNumber() != seqNo) {
      continue;
    }
    const auto epoch = static_cast<BootstrapTime>(
      time::toUnixTimestamp<time::seconds>(key.get(-2).toTimestamp()).count());
    if (!found || epoch > latest) {
      found = true;
      latest = epoch;
      result = mapping;
    }
  }
  if (!found)
    throw std::out_of_range("mapping not found");
  bootstrapTime = latest;
  return result;
}

void
MappingProvider::onMappingQuery(const Interest& interest)
{
  MissingDataInfo query;
  try {
    query = parseMappingQueryDataName(interest.getName());
  }
  catch (const std::exception&) {
    return;
  }

  MappingList queryResponse(query.nodeId);

  for (SeqNo i = query.low; i <= std::max(query.high, query.low); i++) {
    try {
      BootstrapTime bootstrapTime = 0;
      auto mapping = getLatestMapping(query.nodeId, i, bootstrapTime);
      queryResponse.pairs.push_back({bootstrapTime, i, mapping});
    } catch (const std::exception&) {
      continue;
    }
  }

  // Don't reply if we have nothing
  if (queryResponse.pairs.empty())
    return;

  Data data(interest.getName());
  data.setContent(queryResponse.encode());
  data.setFreshnessPeriod(1_s);
  m_securityOptions.dataSigner->sign(data);
  m_face.put(data);
}

void
MappingProvider::fetchNameMapping(const MissingDataInfo& info,
                                  const MappingListCallback& onValidated,
                                  int nRetries)
{
  TimeoutCallback onTimeout = [](auto&&...) {};
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
  interest.setCanBePrefix(false);
  interest.setMustBeFresh(false);
  interest.setInterestLifetime(2_s);

  auto onDataValidated = [this, onValidated, info](const Data& data) {
    Block block = data.getContent().blockFromValue();
    MappingList list(block, info.bootstrapTime);

    // Add all mappings to self
    for (const auto& entry : list.pairs) {
      try {
        getMapping(info.nodeId, entry.bootstrapTime, entry.seqNo);
      } catch (const std::exception&) {
        insertMapping(info.nodeId, entry.bootstrapTime, entry.seqNo, entry.mapping);
      }
    }

    onValidated(list);
  };

  m_fetcher.expressInterest(interest,
                            std::bind(onDataValidated, _2),
                            std::bind(onTimeout, _1), // Nack
                            onTimeout,
                            nRetries,
                            [](auto&&...) {});
}

Name
MappingProvider::getMappingQueryDataName(const MissingDataInfo& info)
{
  Name name = Name(info.nodeId).append(m_syncPrefix);
  return name.append("MAPPING").appendNumber(info.low).appendNumber(info.high);
}

MissingDataInfo
MappingProvider::parseMappingQueryDataName(const Name& name)
{
  MissingDataInfo info;
  const Name expectedPrefix = Name(m_id).append(m_syncPrefix);
  if (name.size() != expectedPrefix.size() + 3 ||
      name.getPrefix(expectedPrefix.size()) != expectedPrefix ||
      name.get(-3) != Name::Component("MAPPING") ||
      !name.get(-2).isGeneric() || !name.get(-1).isGeneric()) {
    NDN_THROW(std::invalid_argument("invalid SVS-PS Mapping query name"));
  }
  info.low = name.get(-2).toNumber();
  info.high = name.get(-1).toNumber();
  if (info.low == 0 || info.high < info.low)
    NDN_THROW(std::invalid_argument("invalid SVS-PS Mapping query range"));
  info.nodeId = name.getPrefix(-3 - m_syncPrefix.size());
  return info;
}

} // namespace ndn::svs
