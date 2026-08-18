/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
#include "sync-protocol.hpp"

#include "tests/boost-test.hpp"

#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/util/string-helper.hpp>

#include <fstream>
#include <sstream>

namespace ndn::tests {

using namespace ndn::svs;

namespace {

Block
loadFixture(const std::string& relativePath)
{
  std::ifstream input("tests/fixtures/svs-v3/" + relativePath);
  BOOST_REQUIRE_MESSAGE(input.good(), "missing fixture " << relativePath);
  std::string hex;
  input >> hex;
  auto buffer = fromHex(hex);
  auto [ok, block] = Block::fromBuffer(buffer);
  BOOST_REQUIRE_MESSAGE(ok && block.size() == buffer->size(), "invalid fixture " << relativePath);
  return block;
}

Interest
makeFixtureInterest(const Block& params)
{
  Interest interest(SyncProtocolCodec::makeSyncName("/ndn/svs-v3-test",
                                                   SvsProtocolVersion::V3));
  interest.setApplicationParameters(params);
  return interest;
}

} // namespace

BOOST_AUTO_TEST_SUITE(TestV3Wire)

BOOST_AUTO_TEST_CASE(ProfileDefaultsAndOverrides)
{
  auto v3 = SyncProtocolOptions().resolve();
  BOOST_CHECK(v3.version == SvsProtocolVersion::V3);
  BOOST_CHECK_EQUAL(v3.syncInterestLifetime, 1_s);
  BOOST_CHECK_EQUAL(v3.suppressionPeriod, 200_ms);
  BOOST_CHECK_EQUAL(v3.periodicTimeout, 30_s);
  BOOST_CHECK_CLOSE(v3.periodicJitter, 0.1, 0.001);

}

BOOST_AUTO_TEST_CASE(EncodeMatchesIndependentOneNodeFixture)
{
  VersionVector vector;
  vector.set("/node/a", 1700000000, 1);
  auto options = SyncProtocolOptions().resolve();
  KeyChain keyChain("pib-memory:v3-wire", "tpm-memory:v3-wire");

  auto interest = SyncProtocolCodec::encode(
    "/ndn/svs-v3-test", vector, {}, options,
    [&] (Data& data) { keyChain.sign(data, security::signingWithSha256()); });

  BOOST_CHECK_EQUAL(interest.getName().at(-2).toVersion(), 3);
  BOOST_CHECK(interest.getName().at(-1).isParametersSha256Digest());
  BOOST_CHECK(interest.isParametersDigestValid());
  BOOST_CHECK_EQUAL(interest.getInterestLifetime(), 1_s);
  const auto expected = loadFixture("v3-one-node.hex");
  const auto& actual = interest.getApplicationParameters();
  BOOST_CHECK_EQUAL_COLLECTIONS(
    actual.data(), actual.data() + actual.size(),
    expected.data(), expected.data() + expected.size());
}

BOOST_AUTO_TEST_CASE(DecodeIndependentPositiveFixtures)
{
  for (const auto& fixture : {"v3-empty.hex", "v3-one-node.hex", "v3-multi-epoch.hex"}) {
    auto decoded = SyncProtocolCodec::decode(makeFixtureInterest(loadFixture(fixture)),
                                             "/ndn/svs-v3-test", SvsProtocolVersion::V3);
    BOOST_REQUIRE(decoded.stateVectorData.has_value());
    BOOST_CHECK_EQUAL(decoded.stateVectorData->getName(), "/ndn/svs-v3-test/v=3");
  }

  auto multi = SyncProtocolCodec::decode(makeFixtureInterest(loadFixture("v3-multi-epoch.hex")),
                                         "/ndn/svs-v3-test", SvsProtocolVersion::V3);
  BOOST_CHECK_EQUAL(multi.stateVector.get("/node/a", 1700000000), 7);
  BOOST_CHECK_EQUAL(multi.stateVector.get("/node/a", 1700000100), 2);
  BOOST_CHECK_EQUAL(multi.stateVector.get("/node/b", 1700000200), 3);

  auto extended = SyncProtocolCodec::decode(
    makeFixtureInterest(loadFixture("v3-unknown-extension.hex")),
    "/ndn/svs-v3-test", SvsProtocolVersion::V3);
  BOOST_REQUIRE_EQUAL(extended.extensions.size(), 1);
  BOOST_CHECK_EQUAL(extended.extensions.front().type(), 0xF001);
}

BOOST_AUTO_TEST_CASE(DecodeRejectsMalformedCoreFixtures)
{
  for (const auto& fixture : {
         "invalid/wrong-data-name.hex", "invalid/missing-signature.hex",
         "invalid/raw-state-vector.hex", "invalid/malformed-content.hex",
         "invalid/seq-zero.hex", "invalid/future-bootstrap.hex",
         "invalid/duplicate-state-vector.hex", "invalid/unknown-core-tlv.hex",
         "invalid/truncated-extension.hex", "invalid/unsigned-trailing-extension.hex"}) {
    BOOST_CHECK_EXCEPTION(
      SyncProtocolCodec::decode(makeFixtureInterest(loadFixture(fixture)),
                                "/ndn/svs-v3-test", SvsProtocolVersion::V3),
      std::exception,
      [&] (const std::exception&) { return true; });
  }
}

BOOST_AUTO_TEST_CASE(V3EnvelopeInspectionDefersSemanticVectorDecode)
{
  auto envelope = SyncProtocolCodec::decode(
    makeFixtureInterest(loadFixture("invalid/seq-zero.hex")),
    "/ndn/svs-v3-test", SvsProtocolVersion::V3, false);
  BOOST_REQUIRE(envelope.stateVectorData.has_value());
  BOOST_CHECK(!envelope.stateVectorDecoded);
  BOOST_CHECK_THROW(
    SyncProtocolCodec::decodeStateVector(envelope, SvsProtocolVersion::V3),
    std::exception);
}

BOOST_AUTO_TEST_CASE(SignedExtensionsAreBoundedAndCoreIndependent)
{
  VersionVector vector;
  vector.set("/node/a", 1700000000, 1);
  KeyChain keyChain("pib-memory:v3-extension", "tpm-memory:v3-extension");
  const auto signer = [&] (Data& data) {
    keyChain.sign(data, security::signingWithSha256());
  };

  const Block firstExtension(0xF001);
  const Block secondExtension(0xF002);
  auto interest = SyncProtocolCodec::encode("/ndn/svs-v3-test/extensions", vector,
                                             {firstExtension, secondExtension},
                                             SyncProtocolOptions().resolve(), signer);
  auto decoded = SyncProtocolCodec::decode(interest, "/ndn/svs-v3-test/extensions",
                                            SvsProtocolVersion::V3);
  BOOST_REQUIRE_EQUAL(decoded.extensions.size(), 2);
  BOOST_CHECK_EQUAL(decoded.extensions.at(0).type(), 0xF001);
  BOOST_CHECK_EQUAL(decoded.extensions.at(1).type(), 0xF002);
  BOOST_CHECK_EQUAL(decoded.stateVector.get("/node/a", 1700000000), 1);
  BOOST_REQUIRE(decoded.stateVectorData.has_value());
  auto signedContent = decoded.stateVectorData->getContent();
  signedContent.parse();
  BOOST_REQUIRE_EQUAL(signedContent.elements_size(), 3);
  BOOST_CHECK_EQUAL(signedContent.elements().at(0).type(), ndn::svs::tlv::StateVector);
  BOOST_CHECK_EQUAL(signedContent.elements().at(1).type(), 0xF001);
  BOOST_CHECK_EQUAL(signedContent.elements().at(2).type(), 0xF002);
  std::vector<Block> tooMany(SyncProtocolCodec::MAX_EXTENSION_BLOCKS + 1,
                             Block(0xF001));
  BOOST_CHECK_THROW(SyncProtocolCodec::encode("/ndn/svs-v3-test/extensions", vector,
                                               tooMany,
                                               SyncProtocolOptions().resolve(), signer),
                    SyncProtocolCodec::Error);
}

BOOST_AUTO_TEST_CASE(V3RejectsWholeParametersCompression)
{
  Interest interest("/ndn/svs-v3-test/v=3");
  interest.setApplicationParameters(makeStringBlock(ndn::svs::tlv::LzmaBlock, "compressed"));
  BOOST_CHECK_THROW(SyncProtocolCodec::decode(interest, "/ndn/svs-v3-test",
                                               SvsProtocolVersion::V3),
                    std::exception);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn::tests
