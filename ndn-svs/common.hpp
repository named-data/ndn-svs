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

#ifndef NDN_SVS_COMMON_HPP
#define NDN_SVS_COMMON_HPP

#include <ndn-cxx/util/scheduler.hpp>
#include <ndn-cxx/security/validator.hpp>
#include <ndn-cxx/face.hpp>

namespace ndn {
namespace svs {

// Type and constant declarations for State Vector Sync (SVS)
using NodeID = std::string;
using SeqNo = uint64_t;

using ndn::security::ValidationError;
using ndn::security::Validator;

typedef struct Packet_
{
  std::shared_ptr<const Interest> interest;
  std::shared_ptr<const Data> data;

  enum PacketType { INTEREST_TYPE, DATA_TYPE } packet_type;
} Packet;

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_COMMON_HPP
