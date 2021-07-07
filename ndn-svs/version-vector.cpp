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

#include "version-vector.hpp"
#include "tlv.hpp"

namespace ndn {
namespace svs {

VersionVector::VersionVector(const ndn::Block& block) {
  block.parse();

  if (block.type() != tlv::VersionVector)
    NDN_THROW(Error("Expected VersionVector"));

  for (auto it = block.elements_begin(); it < block.elements_end(); it++) {
    if (it->type() != tlv::VersionVectorEntry)
      NDN_THROW(Error("Expected VersionVectorEntry"));
    it->parse();

    auto nodeIdElem = it->elements().at(0);
    NodeID nodeId(reinterpret_cast<const char*>(nodeIdElem.value()), nodeIdElem.value_size());
    SeqNo seqNo = ndn::encoding::readNonNegativeInteger(it->elements().at(1));

    m_map[nodeId] = seqNo;
  }
}

ndn::Block
VersionVector::encode() const
{
  ndn::encoding::Encoder enc;

  size_t totalLength = 0;

  for (auto it = m_map.rbegin(); it != m_map.rend(); it++)
  {
    size_t entryLength = 0;
    size_t valLength = enc.prependNonNegativeInteger(it->second);
    entryLength += enc.prependVarNumber(valLength);
    entryLength += enc.prependVarNumber(tlv::SeqNo);
    entryLength += valLength;

    entryLength += enc.prependByteArrayBlock(tlv::ProducerPrefix,
                                             reinterpret_cast<const uint8_t*>(it->first.c_str()), it->first.size());

    totalLength += enc.prependVarNumber(entryLength);
    entryLength += enc.prependVarNumber(tlv::VersionVectorEntry);
    totalLength += entryLength;
  }

  totalLength += enc.prependVarNumber(totalLength);
  totalLength += enc.prependVarNumber(tlv::VersionVector);

  return enc.block();
}

std::string
VersionVector::toStr() const
{
  std::ostringstream stream;
  for (auto &elem : m_map)
  {
    stream << elem.first << ":" << elem.second << " ";
  }
  return stream.str();
}

} // namespace ndn
} // namespace svs
