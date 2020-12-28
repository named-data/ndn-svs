/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2020 University of California, Los Angeles
 *
 * This file is part of ndn-svs, synchronization library for distributed realtime
 * applications for NDN.
 *
 * ndn-svs is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * ndn-svs is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndn-svs, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "version-vector.hpp"

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/unordered_map.hpp>

namespace ndn {
namespace svs {

VersionVector::VersionVector(const ndn::Buffer buf)
  : VersionVector::VersionVector(buf.data(), buf.size()) {}

VersionVector::VersionVector(const uint8_t* buf, const std::size_t size) {
  std::stringstream stream;
  stream.write(reinterpret_cast<const char*>(buf), size);
  boost::archive::binary_iarchive ar(stream, boost::archive::no_header);
  ar >> m_umap;
}

ndn::Buffer
VersionVector::encode() const
{
  std::ostringstream stream;
  boost::archive::binary_oarchive oa(stream, boost::archive::no_header);
  oa << m_umap;
  std::string serialized = stream.str();
  return Buffer(serialized.data(), serialized.size());
}

std::string
VersionVector::toStr() const
{
  std::ostringstream stream;
  for (auto &elem : m_umap) {
    stream << elem.first << ":" << elem.second << " ";
  }
  return stream.str();
}

} // namespace ndn
} // namespace svs
