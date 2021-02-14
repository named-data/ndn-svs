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

#ifndef NDN_SVS_STORE_HPP
#define NDN_SVS_STORE_HPP

#include "common.hpp"

namespace ndn {
namespace svs {

class DataStore : noncopyable {
public:
    virtual std::shared_ptr<const Data>
    find(const Interest& interest) = 0;

    virtual void
    insert(const Data& data) = 0;

    virtual ~DataStore() = default;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_STORE_HPP
