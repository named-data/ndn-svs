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

#ifndef NDN_SVS_VERSION_VECTOR_HPP
#define NDN_SVS_VERSION_VECTOR_HPP

#include "common.hpp"

#include <unordered_map>

#include <ndn-cxx/util/string-helper.hpp>

namespace ndn {
namespace svs {

class VersionVector
{
public:
  using const_iterator = std::unordered_map<NodeID, SeqNo>::const_iterator;

  VersionVector() = default;

  VersionVector(const VersionVector&) = default;

  /** Decode a version vector from ndn::buffer */
  VersionVector(const ndn::Buffer encoded);

  /** Decode a version vector from raw buffer */
  VersionVector(const uint8_t* buf, const size_t size);

  /** Encode the version vector to a string */
  ndn::Buffer
  encode() const;

  /** Get a human-readable representation */
  std::string
  toStr() const;

  SeqNo
  set(NodeID nid, SeqNo seqNo)
  {
    m_umap[nid] = seqNo;
    return seqNo;
  }

  SeqNo
  get(NodeID nid) const
  {
    auto elem = m_umap.find(nid);
    return elem == m_umap.end() ? 0 : elem->second;
  }

  const_iterator
  begin() const
  {
    return m_umap.begin();
  }

  const_iterator
  end() const
  {
    return m_umap.end();
  }

  bool
  has(NodeID nid) const
  {
    return m_umap.find(nid) != end();
  }
private:
  std::unordered_map<NodeID, SeqNo> m_umap;
};

} // namespace ndn
} // namespace svs

#endif // NDN_SVS_VERSION_VECTOR_HPP
