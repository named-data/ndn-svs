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

#include "security-options.hpp"

namespace ndn {
namespace svs {

void
KeyChainSigner::sign(Interest& interest) const
{
  m_keyChain.sign(interest, signingInfo);
}

void
KeyChainSigner::sign(Data& data) const
{
  m_keyChain.sign(data, signingInfo);
}

SecurityOptions::SecurityOptions(KeyChain& keyChain)
  : m_keyChain(keyChain)
  , interestSigner(make_shared<KeyChainSigner>(keyChain))
  , dataSigner(make_shared<KeyChainSigner>(keyChain))
  , pubSigner(make_shared<KeyChainSigner>(keyChain))
{
  interestSigner->signingInfo.setSignedInterestFormat(security::SignedInterestFormat::V03);
}

KeyChain SecurityOptions::DEFAULT_KEYCHAIN;
const SecurityOptions SecurityOptions::DEFAULT(SecurityOptions::DEFAULT_KEYCHAIN);

}  // namespace svs
}  // namespace ndn
