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

  for (auto it = block.elements_begin(); it < block.elements_end(); it += 2) {
    auto key = it, val = it + 1;

    if (key->type() != tlv::VersionVectorKey)
      NDN_THROW(Error("Expected VersionVectorKey"));
    if (val->type() != tlv::VersionVectorValue)
      NDN_THROW(Error("Expected VersionVectorValue"));

    m_map[NodeID(reinterpret_cast<const char*>(it->value()), it->value_size())] =
      SeqNo(ndn::encoding::readNonNegativeInteger(*val));
  }
}

ndn::Block
VersionVector::encode() const
{
  ndn::encoding::Encoder enc;

  size_t totalLength = 0;

  for (auto it = m_map.rbegin(); it != m_map.rend(); it++)
  {
    size_t valLength = enc.prependNonNegativeInteger(it->second);
    totalLength += enc.prependVarNumber(valLength);
    totalLength += enc.prependVarNumber(tlv::VersionVectorValue);
    totalLength += valLength;

    totalLength += enc.prependByteArrayBlock(tlv::VersionVectorKey,
                                             reinterpret_cast<const uint8_t*>(it->first.c_str()), it->first.size());
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
