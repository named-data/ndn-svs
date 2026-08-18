/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2025 University of California, Los Angeles
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

#include "core.hpp"
#include "tlv.hpp"

#include "tests/boost-test.hpp"

#include <ndn-cxx/mgmt/nfd/control-parameters.hpp>
#include <ndn-cxx/mgmt/nfd/control-response.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>
#include <ndn-cxx/util/dummy-client-face.hpp>
#include <ndn-cxx/util/string-helper.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <algorithm>
#include <fstream>
#include <thread>

namespace ndn::tests {

using namespace ndn::svs;
using namespace std::chrono_literals;

static Interest
makeV3SyncInterest(const Name& syncPrefix, const VersionVector& vv)
{
  KeyChain keyChain("pib-memory:core-v3-test", "tpm-memory:core-v3-test");
  return SyncProtocolCodec::encode(
    syncPrefix, vv, {}, SyncProtocolOptions().resolve(),
    [&] (Data& data) { keyChain.sign(data, security::signingWithSha256()); });
}

static Interest
makeFixtureV3Interest(const Name& syncPrefix, const std::string& relativePath)
{
  std::ifstream input("tests/fixtures/svs-v3/" + relativePath);
  BOOST_REQUIRE_MESSAGE(input.good(), "missing fixture " << relativePath);
  std::string hex;
  input >> hex;
  auto buffer = fromHex(hex);
  auto [ok, params] = Block::fromBuffer(buffer);
  BOOST_REQUIRE_MESSAGE(ok && params.size() == buffer->size(),
                        "invalid fixture " << relativePath);
  Interest interest(Name(syncPrefix).appendVersion(3));
  interest.setApplicationParameters(params);
  return interest;
}

static void
runIoUntil(Face& face, const std::function<bool()>& done)
{
  auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    face.getIoContext().restart();
    face.getIoContext().run_for(10ms);
    std::this_thread::sleep_for(1ms);
  }
}

class CoreFixture
{
protected:
  CoreFixture()
    : m_syncPrefix("/ndn/test")
    , m_core(m_face, m_syncPrefix, [](auto&&...) {}, SecurityOptions::DEFAULT,
             SVSyncCore::EMPTY_NODE_ID)
  {
  }

protected:
  DummyClientFace m_face;
  Name m_syncPrefix;
  SVSyncCore m_core;
};

class RejectingDataValidator final : public BaseValidator
{
public:
  void validate(const Data& data,
                const security::DataValidationSuccessCallback&,
                const security::DataValidationFailureCallback& failure) override
  {
    failure(data, security::ValidationError(security::ValidationError::INVALID_SIGNATURE,
                                            "injected rejection"));
  }
};

class DeferredDataValidator final : public BaseValidator
{
public:
  void validate(const Data& data,
                const security::DataValidationSuccessCallback& success,
                const security::DataValidationFailureCallback&) override
  {
    m_data = data;
    m_success = success;
  }

  void accept()
  {
    if (m_success && m_data) {
      m_success(*m_data);
    }
  }

private:
  std::optional<Data> m_data;
  security::DataValidationSuccessCallback m_success;
};

BOOST_FIXTURE_TEST_SUITE(TestCore, CoreFixture)

BOOST_AUTO_TEST_CASE(DefaultProfileEmitsCompleteV3Envelope)
{
  DummyClientFace face;
  SVSyncCore core(face, m_syncPrefix, [](auto&&...) {});
  core.sendInitialInterest();
  runIoUntil(face, [&] {
    return std::any_of(face.sentInterests.begin(), face.sentInterests.end(),
                       [this] (const Interest& interest) {
                         return m_syncPrefix.isPrefixOf(interest.getName());
                       });
  });

  const auto found = std::find_if(face.sentInterests.begin(), face.sentInterests.end(),
                                  [this] (const Interest& interest) {
                                    return m_syncPrefix.isPrefixOf(interest.getName());
                                  });
  BOOST_REQUIRE(found != face.sentInterests.end());
  BOOST_REQUIRE_GE(found->getName().size(), m_syncPrefix.size() + 2);
  BOOST_CHECK(found->getName().at(m_syncPrefix.size()).isVersion());
  BOOST_CHECK_EQUAL(found->getName().at(m_syncPrefix.size()).toVersion(), 3);
  BOOST_CHECK_EQUAL(found->getInterestLifetime(), 1_s);

  auto params = found->getApplicationParameters();
  params.parse();
  BOOST_REQUIRE(!params.elements().empty());
  BOOST_CHECK_EQUAL(params.elements().front().type(), ndn::tlv::Data);
}

BOOST_AUTO_TEST_CASE(DefaultProfileRegistersV3AnnouncementPrefix)
{
  DummyClientFace::Options options;
  options.enableRegistrationReply = true;
  DummyClientFace face(options);
  SVSyncCore core(face, m_syncPrefix, [](auto&&...) {});

  static const Name ribRegisterPrefix("/localhost/nfd/rib/register");
  runIoUntil(face, [&] {
    return std::any_of(face.sentInterests.begin(), face.sentInterests.end(),
                       [] (const Interest& interest) {
                         return ribRegisterPrefix.isPrefixOf(interest.getName());
                       });
  });

  const auto found = std::find_if(face.sentInterests.begin(), face.sentInterests.end(),
                                  [] (const Interest& interest) {
                                    return ribRegisterPrefix.isPrefixOf(interest.getName());
                                  });
  BOOST_REQUIRE(found != face.sentInterests.end());
  BOOST_REQUIRE_GT(found->getName().size(), 4);
  nfd::ControlParameters parameters(found->getName().at(4).blockFromValue());
  BOOST_REQUIRE(parameters.hasName());
  BOOST_CHECK_EQUAL(parameters.getName(), m_syncPrefix.appendVersion(3));
}

BOOST_AUTO_TEST_CASE(V3AnnouncementPrefixRegistrationIsReleasedOnDestruction)
{
  DummyClientFace::Options options;
  options.enableRegistrationReply = true;
  DummyClientFace face(options);
  {
    SVSyncCore core(face, m_syncPrefix, [](auto&&...) {});
    static const Name ribRegisterPrefix("/localhost/nfd/rib/register");
    runIoUntil(face, [&] {
      return std::any_of(face.sentInterests.begin(), face.sentInterests.end(),
                         [] (const Interest& interest) {
                           return ribRegisterPrefix.isPrefixOf(interest.getName());
                         });
    });
  }

  static const Name ribUnregisterPrefix("/localhost/nfd/rib/unregister");
  runIoUntil(face, [&] {
    return std::any_of(face.sentInterests.begin(), face.sentInterests.end(),
                       [] (const Interest& interest) {
                         return ribUnregisterPrefix.isPrefixOf(interest.getName());
                       });
  });
  const auto found = std::find_if(face.sentInterests.begin(), face.sentInterests.end(),
                                  [] (const Interest& interest) {
                                    return ribUnregisterPrefix.isPrefixOf(interest.getName());
                                  });
  BOOST_REQUIRE(found != face.sentInterests.end());
  BOOST_REQUIRE_GT(found->getName().size(), 4);
  nfd::ControlParameters parameters(found->getName().at(4).blockFromValue());
  BOOST_REQUIRE(parameters.hasName());
  BOOST_CHECK_EQUAL(parameters.getName(), m_syncPrefix.appendVersion(3));
}

BOOST_AUTO_TEST_CASE(V3AnnouncementPrefixRegistrationFailureIsReported)
{
  DummyClientFace face;
  KeyChain keyChain("pib-memory:core-registration-failure",
                    "tpm-memory:core-registration-failure");
  static const Name ribRegisterPrefix("/localhost/nfd/rib/register");
  face.onSendInterest.connect([&] (const Interest& interest) {
    if (!ribRegisterPrefix.isPrefixOf(interest.getName())) {
      return;
    }
    nfd::ControlResponse response(500, "injected registration failure");
    auto data = std::make_shared<Data>(interest.getName());
    data->setContent(response.wireEncode());
    keyChain.sign(*data, security::signingWithSha256());
    boost::asio::post(face.getIoContext(), [&face, data] { face.receive(*data); });
  });

  SVSyncCore core(face, m_syncPrefix, [](auto&&...) {});
  face.getIoContext().restart();
  BOOST_CHECK_NO_THROW(face.getIoContext().run_for(100ms));
  BOOST_CHECK(std::none_of(face.sentInterests.begin(), face.sentInterests.end(),
                           [this] (const Interest& interest) {
                             return m_syncPrefix.isPrefixOf(interest.getName());
                           }));
}

BOOST_AUTO_TEST_CASE(MergeStateVector)
{
  std::vector<MissingDataInfo> missingInfo;

  VersionVector v = m_core.getState();
  BOOST_CHECK_EQUAL(v.get("one"), 0);
  BOOST_CHECK_EQUAL(v.get("two"), 0);
  BOOST_CHECK_EQUAL(v.get("three"), 0);
  BOOST_CHECK_EQUAL(missingInfo.size(), 0);

  VersionVector v1;
  v1.set("one", 100, 1);
  v1.set("two", 200, 2);
  missingInfo = m_core.mergeStateVector(v1).missingInfo;

  v = m_core.getState();
  BOOST_CHECK_EQUAL(v.get("one"), 1);
  BOOST_CHECK_EQUAL(v.get("two"), 2);
  BOOST_CHECK_EQUAL(v.get("three"), 0);
  BOOST_CHECK_EQUAL(missingInfo.size(), 2);

  VersionVector v2;
  v2.set("one", 100, 1);
  v2.set("two", 200, 1);
  v2.set("three", 300, 3);
  missingInfo = m_core.mergeStateVector(v2).missingInfo;

  v = m_core.getState();
  BOOST_CHECK_EQUAL(v.get("one"), 1);
  BOOST_CHECK_EQUAL(v.get("two"), 2);
  BOOST_CHECK_EQUAL(v.get("three"), 3);

  BOOST_REQUIRE_EQUAL(missingInfo.size(), 1);
  BOOST_CHECK_EQUAL(missingInfo[0].nodeId, "three");
  BOOST_CHECK_EQUAL(missingInfo[0].bootstrapTime, 300);
  BOOST_CHECK_EQUAL(missingInfo[0].low, 1);
  BOOST_CHECK_EQUAL(missingInfo[0].high, 3);
}

BOOST_AUTO_TEST_CASE(ComputeMergeStateVector)
{
  VersionVector local;
  local.set("one", 100, 2);
  local.set("local-only", 700, 7);

  VersionVector remote;
  remote.set("one", 100, 5);
  remote.set("two", 200, 1);

  auto result = SVSyncCore::computeMergeStateVector(local, remote);

  BOOST_CHECK(result.myVectorNew);
  BOOST_CHECK(result.otherVectorNew);
  BOOST_CHECK_EQUAL(result.mergedVector.get("one"), 5);
  BOOST_CHECK_EQUAL(result.mergedVector.get("two"), 1);
  BOOST_CHECK_EQUAL(result.mergedVector.get("local-only"), 7);

  BOOST_REQUIRE_EQUAL(result.missingData.size(), 2);
  BOOST_CHECK_EQUAL(result.missingData[0].nodeId, "one");
  BOOST_CHECK_EQUAL(result.missingData[0].bootstrapTime, 100);
  BOOST_CHECK_EQUAL(result.missingData[0].low, 3);
  BOOST_CHECK_EQUAL(result.missingData[0].high, 5);
  BOOST_CHECK_EQUAL(result.missingData[1].nodeId, "two");
  BOOST_CHECK_EQUAL(result.missingData[1].bootstrapTime, 200);
  BOOST_CHECK_EQUAL(result.missingData[1].low, 1);
  BOOST_CHECK_EQUAL(result.missingData[1].high, 1);
}

BOOST_AUTO_TEST_CASE(ComputeMergeStateVectorKeepsRestartEpochsSeparate)
{
  VersionVector local;
  local.set("node", 100, 10);

  VersionVector remote;
  remote.set("node", 200, 1);

  auto result = SVSyncCore::computeMergeStateVector(local, remote);
  BOOST_CHECK(result.myVectorNew);
  BOOST_CHECK(result.otherVectorNew);
  BOOST_CHECK_EQUAL(result.mergedVector.get("node", 100), 10);
  BOOST_CHECK_EQUAL(result.mergedVector.get("node", 200), 1);
  BOOST_REQUIRE_EQUAL(result.missingData.size(), 1);
  BOOST_CHECK_EQUAL(result.missingData[0].nodeId, "node");
  BOOST_CHECK_EQUAL(result.missingData[0].bootstrapTime, 200);
  BOOST_CHECK_EQUAL(result.missingData[0].low, 1);
  BOOST_CHECK_EQUAL(result.missingData[0].high, 1);
}

BOOST_AUTO_TEST_CASE(SyncInterestMissingDataCarriesBootstrapTimeInCallback)
{
  DummyClientFace localFace;
  std::vector<MissingDataInfo> callbacks;

  SVSyncCore core(localFace, "/ndn/test", [&] (const std::vector<MissingDataInfo>& updates) {
    callbacks = updates;
  });

  VersionVector remote;
  remote.set("peer", 1700000000, 3);
  core.onSyncInterest(makeV3SyncInterest("/ndn/test", remote));

  runIoUntil(localFace, [&] { return !callbacks.empty(); });

  BOOST_REQUIRE_EQUAL(callbacks.size(), 1);
  BOOST_CHECK_EQUAL(callbacks[0].nodeId, "peer");
  BOOST_CHECK_EQUAL(callbacks[0].bootstrapTime, 1700000000);
  BOOST_CHECK_EQUAL(callbacks[0].low, 1);
  BOOST_CHECK_EQUAL(callbacks[0].high, 3);
}

BOOST_AUTO_TEST_CASE(V3ReceiveAdvancesExactlyOneRemoteRange)
{
  DummyClientFace localFace;
  std::vector<MissingDataInfo> callbacks;
  SVSyncCore core(localFace, "/ndn/test/v3-receive",
                  [&] (const std::vector<MissingDataInfo>& updates) { callbacks = updates; });

  VersionVector remote;
  remote.set("/peer", 1700000000, 3);
  core.onSyncInterest(makeV3SyncInterest("/ndn/test/v3-receive", remote));
  runIoUntil(localFace, [&] { return !callbacks.empty(); });

  BOOST_REQUIRE_EQUAL(callbacks.size(), 1);
  BOOST_CHECK_EQUAL(callbacks.front().nodeId, "/peer");
  BOOST_CHECK_EQUAL(callbacks.front().low, 1);
  BOOST_CHECK_EQUAL(callbacks.front().high, 3);
  BOOST_CHECK_EQUAL(core.getState().get("/peer", 1700000000), 3);
}

BOOST_AUTO_TEST_CASE(V3ValidationFailureIsAtomicAndObservable)
{
  DummyClientFace localFace;
  KeyChain keyChain("pib-memory:core-v3-reject", "tpm-memory:core-v3-reject");
  SecurityOptions security(keyChain);
  security.validator = std::make_shared<RejectingDataValidator>();
  size_t callbackCount = 0;
  SVSyncCore core(localFace, "/ndn/test/v3-reject",
                  [&] (const auto&) { ++callbackCount; }, security);
  VersionVector remote;
  remote.set("/peer", 1700000000, 1);
  core.onSyncInterest(makeV3SyncInterest("/ndn/test/v3-reject", remote));

  BOOST_CHECK_EQUAL(callbackCount, 0);
  BOOST_CHECK_EQUAL(core.getState().get("/peer"), 0);
  BOOST_CHECK(core.getLastValidationStatus() == SVSyncCore::ValidationStatus::Rejected);
}

BOOST_AUTO_TEST_CASE(V3ValidationPrecedesSemanticVectorDecode)
{
  DummyClientFace localFace;
  KeyChain keyChain("pib-memory:core-v3-order", "tpm-memory:core-v3-order");
  SecurityOptions security(keyChain);
  security.validator = std::make_shared<RejectingDataValidator>();
  SVSyncCore core(localFace, "/ndn/svs-v3-test", [] (const auto&) {}, security);
  core.onSyncInterest(makeFixtureV3Interest("/ndn/svs-v3-test", "invalid/seq-zero.hex"));

  BOOST_CHECK(core.getLastValidationStatus() == SVSyncCore::ValidationStatus::Rejected);
  BOOST_CHECK_EQUAL(core.getState().get("/peer"), 0);
}

BOOST_AUTO_TEST_CASE(DeferredValidatorCallbackAfterShutdownIsFenced)
{
  DummyClientFace localFace;
  KeyChain keyChain("pib-memory:core-v3-deferred", "tpm-memory:core-v3-deferred");
  SecurityOptions security(keyChain);
  auto validator = std::make_shared<DeferredDataValidator>();
  security.validator = validator;
  VersionVector remote;
  remote.set("/peer", 1700000000, 1);
  {
    SVSyncCore core(localFace, "/ndn/test/v3-deferred", [] (const auto&) {}, security);
    core.onSyncInterest(makeV3SyncInterest("/ndn/test/v3-deferred", remote));
  }
  BOOST_CHECK_NO_THROW(validator->accept());
}

BOOST_AUTO_TEST_CASE(MalformedV3DoesNotPoisonSerialProgress)
{
  DummyClientFace localFace;
  const Name group("/ndn/test/v3-negative-serial");
  SVSyncCore core(localFace, group, [] (const auto&) {});
  VersionVector malformedVector;
  malformedVector.set("/bad", 0, 1);
  Interest malformed(Name(group).appendVersion(3));
  ndn::encoding::EncodingBuffer enc;
  auto raw = malformedVector.encode();
  ndn::encoding::prependBlock(enc, raw);
  enc.prependVarNumber(raw.size());
  enc.prependVarNumber(ndn::tlv::ApplicationParameters);
  malformed.setApplicationParameters(enc.block());
  core.onSyncInterestValidated(malformed);
  BOOST_CHECK_EQUAL(core.getState().get("/bad"), 0);

  VersionVector valid;
  valid.set("/good", 1700000000, 2);
  core.onSyncInterest(makeV3SyncInterest(group, valid));
  runIoUntil(localFace, [&] { return core.getState().get("/good") == 2; });
  BOOST_CHECK_EQUAL(core.getState().get("/good", 1700000000), 2);
  BOOST_CHECK_EQUAL(core.getState().get("/bad"), 0);
}

BOOST_AUTO_TEST_CASE(BootstrapOverrideIsValidatedBeforeRegistration)
{
  DummyClientFace localFace;
  SyncProtocolOptions valid;
  valid.bootstrapTime = 1700000000;
  SVSyncCore restored(localFace, "/ndn/test/restored", [] (const auto&) {},
                      SecurityOptions::DEFAULT, "/restored", valid);
  BOOST_CHECK_EQUAL(restored.getBootstrapTime(), 1700000000);

  const auto now = static_cast<BootstrapTime>(
    std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
  SyncProtocolOptions invalid;
  invalid.bootstrapTime = now + 86401;
  BOOST_CHECK_THROW(SVSyncCore(localFace, "/ndn/test/future", [] (const auto&) {},
                               SecurityOptions::DEFAULT, "/future", invalid),
                    std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace ndn::tests
