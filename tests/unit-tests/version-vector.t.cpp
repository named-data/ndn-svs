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

#include "version-vector.hpp"

#include "tests/boost-test.hpp"

namespace ndn {
namespace svs {
namespace test {

struct TestVersionVectorFixture
{
  TestVersionVectorFixture()
  {
    v.set("one", 1);
    v.set("two", 2);
  }

  VersionVector v;
};

BOOST_FIXTURE_TEST_SUITE(TestVersionVector, TestVersionVectorFixture)

BOOST_AUTO_TEST_CASE(Get)
{
  BOOST_CHECK_EQUAL(v.get("one"), 1);
  BOOST_CHECK_EQUAL(v.get("two"), 2);
  BOOST_CHECK_EQUAL(v.get("five"), 0);
}

BOOST_AUTO_TEST_CASE(Set)
{
  BOOST_CHECK_EQUAL(v.set("four", 44), 44);
  BOOST_CHECK_EQUAL(v.get("four"), 44);
}

BOOST_AUTO_TEST_CASE(Iterate)
{
  std::unordered_map<NodeID, SeqNo> umap;
  for (auto elem : v)
  {
    umap[elem.first] = elem.second;
  }

  BOOST_CHECK_EQUAL(umap["one"], 1);
  BOOST_CHECK_EQUAL(umap["two"], 2);
  BOOST_CHECK_EQUAL(umap.size(), 2);
}

BOOST_AUTO_TEST_CASE(EncodeDecode)
{
  ndn::Block block = v.encode();
  BOOST_CHECK_GT(block.value_size(), 0);

  // 100 bytes is too big
  BOOST_CHECK_LT(block.value_size(), 100);

  VersionVector dv(block);
  BOOST_CHECK_EQUAL(dv.get("one"), 1);
  BOOST_CHECK_EQUAL(dv.get("two"), 2);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn
} // namespace ndn
} // namespace ndn