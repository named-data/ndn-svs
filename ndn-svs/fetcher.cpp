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

#include "fetcher.hpp"

namespace ndn {
namespace svs {

Fetcher::Fetcher(Face& face)
  : m_face(face)
{}

void
Fetcher::expressInterest(const ndn::Interest &interest,
                         const ndn::DataCallback &afterSatisfied,
                         const ndn::NackCallback &afterNacked,
                         const ndn::TimeoutCallback &afterTimeout,
                         const int nRetries)
{
  uint64_t id = ++m_pendingInterestId;
  m_pendingInterests[id] =
    m_face.expressInterest(interest,
                           std::bind(&Fetcher::onData, this, _1, _2, afterSatisfied, id),
                           std::bind(&Fetcher::onNack, this, _1, _2, afterNacked, id),
                           std::bind(&Fetcher::onTimeout, this, _1, afterSatisfied, afterNacked, afterTimeout, id, nRetries));
}

void
Fetcher::onData(const Interest& interest, const Data& data,
                const ndn::DataCallback& afterSatisfied,
                const uint64_t interestId)
{
  m_pendingInterests.erase(interestId);
  afterSatisfied(interest, data);
}

void
Fetcher::onNack(const ndn::Interest& interest, const ndn::lp::Nack& nack,
                const ndn::NackCallback &afterNacked,
                const uint64_t interestId)
{
  m_pendingInterests.erase(interestId);
  afterNacked(interest, nack);
}

void
Fetcher::onTimeout(const Interest& interest,
                   const ndn::DataCallback &afterSatisfied,
                   const ndn::NackCallback &afterNacked,
                   const ndn::TimeoutCallback &afterTimeout,
                   const uint64_t interestId,
                   const int nRetries)
{
  m_pendingInterests.erase(interestId);

  if (nRetries == 0)
    return afterTimeout(interest);

  Interest newNonceInterest(interest);
  newNonceInterest.refreshNonce();

  expressInterest(newNonceInterest, afterSatisfied, afterNacked, afterTimeout, nRetries - 1);
}

}  // namespace svs
}  // namespace ndn