/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2020 University of California, Los Angeles
 *
 * This file is part of ndn-svs, synchronization library for distributed realtime
 * applications for NDN.
 *
 * ndn-svs library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-svs library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU Lesser General Public License for more details.
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
