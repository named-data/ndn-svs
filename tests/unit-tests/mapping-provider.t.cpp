/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */

#include "mapping-provider.hpp"
#include "svsync.hpp"

#include "tests/boost-test.hpp"

#include <ndn-cxx/util/dummy-client-face.hpp>

namespace ndn::tests {

using namespace ndn::svs;

BOOST_AUTO_TEST_SUITE(TestV3Naming)

BOOST_AUTO_TEST_CASE(PublicationNameCarriesBootstrapBeforeSequence)
{
  DummyClientFace face;
  SyncProtocolOptions protocol;
  protocol.bootstrapTime = 1700000000;
  SVSync sync("/group", "/node", face, [] (const auto&) {},
              SecurityOptions::DEFAULT, SVSync::DEFAULT_DATASTORE, protocol);

  const auto name = sync.getDataName("/node", 1700000000, 7);
  BOOST_REQUIRE_EQUAL(name.size(), 4);
  BOOST_CHECK_EQUAL(name.getPrefix(2), "/node/group");
  BOOST_CHECK(name.at(2).isTimestamp());
  BOOST_CHECK_EQUAL(time::toUnixTimestamp<time::seconds>(name.at(2).toTimestamp()).count(),
                    1700000000);
  BOOST_CHECK(name.at(3).isSequenceNumber());
  BOOST_CHECK_EQUAL(name.at(3).toSequenceNumber(), 7);
}

BOOST_AUTO_TEST_CASE(MappingQueryUsesBootstrapBeforeMarker)
{
  DummyClientFace face;
  MappingProvider provider("/group", "/node", face, SecurityOptions::DEFAULT);
  MissingDataInfo info{"/node", 4, 9, 0, 1700000000};

  const auto name = provider.getMappingQueryDataName(info);
  BOOST_REQUIRE_EQUAL(name.size(), 6);
  BOOST_CHECK_EQUAL(name.getPrefix(2), "/node/group");
  BOOST_CHECK(name.at(2).isTimestamp());
  BOOST_CHECK_EQUAL(name.at(3), Name::Component("MAPPING"));
  BOOST_CHECK(name.at(4).isSequenceNumber());
  BOOST_CHECK(name.at(5).isSequenceNumber());

  const auto parsed = provider.parseMappingQueryDataName(name);
  BOOST_CHECK_EQUAL(parsed.nodeId, info.nodeId);
  BOOST_CHECK_EQUAL(parsed.bootstrapTime, info.bootstrapTime);
  BOOST_CHECK_EQUAL(parsed.low, info.low);
  BOOST_CHECK_EQUAL(parsed.high, info.high);
}

BOOST_AUTO_TEST_CASE(MappingStoreSeparatesBootstrapEpochs)
{
  DummyClientFace face;
  MappingProvider provider("/group", "/node", face, SecurityOptions::DEFAULT);
  provider.insertMapping("/node", 100, 1, {"/app/old", {}});
  provider.insertMapping("/node", 200, 1, {"/app/new", {}});

  BOOST_CHECK_EQUAL(provider.getMapping("/node", 100, 1).first, "/app/old");
  BOOST_CHECK_EQUAL(provider.getMapping("/node", 200, 1).first, "/app/new");
}

BOOST_AUTO_TEST_CASE(MappingDataUsesV3WireFormat)
{
  MappingList list("/node");
  list.pairs.push_back({1700000000, 7, {"/app/data", {}}});

  auto encoded = list.encode();
  encoded.parse();
  BOOST_REQUIRE_EQUAL(encoded.type(), ndn::svs::tlv::MappingData);
  BOOST_REQUIRE_EQUAL(encoded.elements_size(), 2);
  auto entry = encoded.elements().at(1);
  entry.parse();
  BOOST_REQUIRE_EQUAL(entry.type(), ndn::svs::tlv::MappingEntry);
  BOOST_REQUIRE_EQUAL(entry.elements_size(), 2);
  BOOST_CHECK_EQUAL(entry.elements().at(0).type(), ndn::svs::tlv::MappingSeqNo);
  BOOST_CHECK_EQUAL(ndn::encoding::readNonNegativeInteger(entry.elements().at(0)), 7);
  BOOST_CHECK_EQUAL(entry.elements().at(1).type(), ndn::tlv::Name);

  MappingList decoded(encoded, 1700000000);
  BOOST_REQUIRE_EQUAL(decoded.pairs.size(), 1);
  BOOST_CHECK_EQUAL(decoded.pairs.front().bootstrapTime, 1700000000);
  BOOST_CHECK_EQUAL(decoded.pairs.front().seqNo, 7);
  BOOST_CHECK_EQUAL(decoded.pairs.front().mapping.first, "/app/data");
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn::tests
