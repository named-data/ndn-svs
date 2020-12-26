/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2020 University of California, Los Angeles
 *
 * This file is part of SVS, synchronization library for distributed realtime
 * applications for NDN.
 *
 * SVS is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * SVS is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * SVS, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SVS_VERSION_VECTOR_HPP
#define SVS_VERSION_VECTOR_HPP

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

  /** Decode a version vector from encoded string */
  VersionVector(std::string encoded);

  /** Encode the version vector to a string */
  std::string
  encode() const;

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

#endif // SVS_VERSION_VECTOR_HPP
