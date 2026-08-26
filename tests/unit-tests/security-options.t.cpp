/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2025 University of California, Los Angeles
 *
 * This file is part of ndn-svs, synchronization library for distributed realtime
 * applications for NDN.
 */

#include "security-options.hpp"

#include "tests/boost-test.hpp"

#include <ndn-cxx/security/signing-helpers.hpp>

namespace ndn::tests {

using namespace ndn::svs;

BOOST_AUTO_TEST_SUITE(TestSecurityOptions)

BOOST_AUTO_TEST_CASE(KeyChainSignerSignsV3StateVectorData)
{
  KeyChain keyChain("pib-memory:security-options-data", "tpm-memory:security-options-data");
  auto identity = keyChain.createIdentity("/security-options/data");
  SecurityOptions options(keyChain);
  options.dataSigner->signingInfo = security::signingByIdentity(identity);

  Data data("/sync/v=3");
  data.setContent("state-vector");
  options.dataSigner->sign(data);

  BOOST_CHECK(data.getSignatureValue().isValid());
  BOOST_CHECK(data.getSignatureInfo().hasKeyLocator());
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn::tests
