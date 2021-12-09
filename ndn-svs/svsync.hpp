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

#ifndef NDN_SVS_SVSYNC_HPP
#define NDN_SVS_SVSYNC_HPP

#include "svsync-base.hpp"

namespace ndn {
namespace svs {

/**
 * @brief SVSync using arbitrary prefix for data delivery
 *
 * The data prefix acts as the node ID in the version vector
 * Sync core runs under <sync-prefix>
 * Data is produced as <data-prefix>/<sync-prefix>/<seq>
 */
class SVSync : public SVSyncBase
{
public:
  SVSync(const Name& syncPrefix,
         const Name& nodePrefix,
         ndn::Face& face,
         const UpdateCallback& updateCallback,
         const SecurityOptions& securityOptions = SecurityOptions::DEFAULT,
         std::shared_ptr<DataStore> dataStore = DEFAULT_DATASTORE)
  : SVSyncBase(
      syncPrefix,
      Name(nodePrefix).append(syncPrefix),
      nodePrefix,
      face, updateCallback, securityOptions, dataStore)
  {}

  Name
  getDataName(const NodeID& nid, const SeqNo& seqNo)
  {
    return Name(nid).append(m_syncPrefix).appendNumber(seqNo);
  }
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_SVSYNC_HPP
