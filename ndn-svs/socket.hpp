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

#ifndef NDN_SVS_SOCKET_HPP
#define NDN_SVS_SOCKET_HPP

#include "socket-base.hpp"

namespace ndn {
namespace svs {

/**
 * @brief Socket using arbitrary prefix for data delivery
 *
 * The data prefix acts as the node ID in the version vector
 * Sync logic runs under <sync-prefix>
 * Data is produced as <data-prefix>/<sync-prefix>/<seq>
 */
class Socket : public SocketBase
{
public:
  Socket(const Name& syncPrefix,
         const Name& nodePrefix,
         ndn::Face& face,
         const UpdateCallback& updateCallback,
         const SecurityOptions& securityOptions = SecurityOptions::DEFAULT,
         std::shared_ptr<DataStore> dataStore = DEFAULT_DATASTORE)
  : SocketBase(
      syncPrefix,
      Name(nodePrefix).append(syncPrefix),
      nodePrefix.toUri(),
      face, updateCallback, securityOptions, dataStore)
  {}

  Name
  getDataName(const NodeID& nid, const SeqNo& seqNo)
  {
    return Name(nid).append(m_syncPrefix).appendNumber(seqNo);
  }

  /**
   * @brief Retrive a data packet with a particular seqNo from a node
   *
   * @param nodePrefix Node prefix of the target node
   * @param seq The seqNo of the data packet.
   * @param onValidated The callback when the retrieved packet has been validated.
   * @param nRetries The number of retries.
   */
  void
  fetchData(const Name& nodePrefix, const SeqNo& seq,
            const DataValidatedCallback& onValidated,
            int nRetries = 0)
  {
    return fetchData(nodePrefix.toUri(), seq, onValidated, nRetries);
  }

  /**
   * @brief Retrive a data packet with a particular seqNo from a node
   *
   * @param nodePrefix Node prefix of the target node
   * @param seq The seqNo of the data packet.
   * @param onValidated The callback when the retrieved packet has been validated.
   * @param onValidationFailed The callback when the retrieved packet failed validation.
   * @param onTimeout The callback when data is not retrieved.
   * @param nRetries The number of retries.
   */
  void
  fetchData(const Name& nodePrefix, const SeqNo& seq,
            const DataValidatedCallback& onValidated,
            const DataValidationErrorCallback& onValidationFailed,
            const TimeoutCallback& onTimeout,
            int nRetries = 0)
  {
    return fetchData(nodePrefix.toUri(), seq, onValidated, onValidationFailed, onTimeout, nRetries);
  }
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_SOCKET_HPP
