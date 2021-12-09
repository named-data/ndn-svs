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
    NDN_THROW(ndn::tlv::Error("Expected VersionVector"));

  for (auto it = block.elements_begin(); it < block.elements_end(); it++) {
    if (it->type() != tlv::VersionVectorEntry)
      NDN_THROW(ndn::tlv::Error("Expected VersionVectorEntry"));
    it->parse();

    NodeID nodeId(it->elements().at(0));
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

    entryLength += enc.prependBlock(it->first.wireEncode());
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
