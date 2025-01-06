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

#ifndef NDN_SVS_STORE_MEMORY_HPP
#define NDN_SVS_STORE_MEMORY_HPP

#include "store.hpp"

#include <ndn-cxx/ims/in-memory-storage-persistent.hpp>

namespace ndn::svs {

class MemoryDataStore : public DataStore
{
public:
  std::shared_ptr<const Data> find(const Interest& interest) override
  {
    return m_ims.find(interest);
  }

  void insert(const Data& data) override
  {
    return m_ims.insert(data);
  }

private:
  InMemoryStoragePersistent m_ims;
};

} // namespace ndn::svs

#endif // NDN_SVS_STORE_MEMORY_HPP
