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

#ifndef NDN_SVS_FETCHER_HPP
#define NDN_SVS_FETCHER_HPP

#include "common.hpp"

namespace ndn {
namespace svs {

class Fetcher
{
public:
  Fetcher(Face& face);

  void
  expressInterest(const ndn::Interest &interest,
                  const ndn::DataCallback &afterSatisfied,
                  const ndn::NackCallback &afterNacked,
                  const ndn::TimeoutCallback &afterTimeout,
                  const int nRetries = 0);

private:
  void
  onData(const Interest& interest, const Data& data,
         const ndn::DataCallback& afterSatisfied,
         const uint64_t interestId);

  void
  onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack,
         const ndn::NackCallback &afterNacked,
         const uint64_t interestId);

  void
  onTimeout(const Interest& interest,
            const ndn::DataCallback &afterSatisfied,
            const ndn::NackCallback &afterNacked,
            const ndn::TimeoutCallback &afterTimeout,
            const uint64_t interestId,
            const int nRetries);

private:
  Face& m_face;

  uint64_t m_pendingInterestId = 0;

  // Keep a scoped map of all pending interests.
  // This ensures all interests are cancelled when
  // the fetcher is destroyed.
  std::map<uint64_t, ScopedPendingInterestHandle> m_pendingInterests;
};

}  // namespace svs
}  // namespace ndn

#endif // NDN_SVS_FETCHER_HPP
