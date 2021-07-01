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

#ifndef NDN_SVS_SVSYNC_SHARED_HPP
#define NDN_SVS_SVSYNC_SHARED_HPP

#include "svsync-base.hpp"

namespace ndn {
namespace svs {

/**
 * @brief SVSync using shared prefix for data delivery
 *
 * Sync core runs under <grp-prefix>/s/
 * Data is produced as <grp-prefix>/d/<node-id>/<seq>
 * Both prefixes use multicast strategy, so all nodes receive
 * data interests for all other nodes.
 */
class SVSyncShared : public SVSyncBase
{
public:
  SVSyncShared(const Name& grpPrefix,
               const NodeID& id,
               ndn::Face& face,
               const UpdateCallback& updateCallback,
               const SecurityOptions& securityOptions = SecurityOptions::DEFAULT,
               std::shared_ptr<DataStore> dataStore = DEFAULT_DATASTORE)
  : SVSyncBase(
      Name(grpPrefix).append("s"),
      Name(grpPrefix).append("d"),
      id, face, updateCallback, securityOptions, dataStore)
  {}

  Name
  getDataName(const NodeID& nid, const SeqNo& seqNo)
  {
    return Name(m_dataPrefix).append(nid).appendNumber(seqNo);
  }

  /*** @brief Set whether data of other nodes is also cached and served */
  void
  setCacheAll(bool val)
  {
    m_cacheAll = val;
  }

private:
  bool
  shouldCache(const Data& data)
  {
    return m_cacheAll;
  }

private:
  bool m_cacheAll = false;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_SVSYNC_SHARED_HPP
