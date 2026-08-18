/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
#include "sync-protocol.hpp"

#include <cmath>
#include <chrono>
#include <iterator>

namespace ndn::svs {

ResolvedSyncProtocolOptions
SyncProtocolOptions::resolve() const
{
  if (periodicJitter < 0.0 || periodicJitter > 1.0) {
    NDN_THROW(std::invalid_argument("SVS periodic jitter must be in [0,1]"));
  }

  ResolvedSyncProtocolOptions resolved;
  resolved.version = version;
  resolved.bootstrapTime = bootstrapTime;
  resolved.periodicJitter = periodicJitter;
  if (version != SvsProtocolVersion::V3) {
    NDN_THROW(std::invalid_argument("unsupported SVS protocol version"));
  }
  resolved.syncInterestLifetime = syncInterestLifetime.value_or(1_s);
  resolved.suppressionPeriod = suppressionPeriod.value_or(200_ms);
  resolved.periodicTimeout = periodicTimeout.value_or(30_s);

  if (resolved.syncInterestLifetime <= 0_ms || resolved.suppressionPeriod < 0_ms ||
      resolved.periodicTimeout <= 0_ms) {
    NDN_THROW(std::invalid_argument("SVS protocol timers are out of range"));
  }
  if (resolved.bootstrapTime) {
    const auto now = static_cast<BootstrapTime>(
      time::toUnixTimestamp<time::seconds>(time::system_clock::now()).count());
    if (*resolved.bootstrapTime > now + 86400) {
      NDN_THROW(std::invalid_argument("SVS bootstrap time is too far in the future"));
    }
  }
  return resolved;
}

Name
SyncProtocolCodec::makeSyncName(const Name& groupPrefix, SvsProtocolVersion version)
{
  return Name(groupPrefix).appendVersion(static_cast<uint64_t>(version));
}

Interest
SyncProtocolCodec::encode(const Name& groupPrefix,
                          const VersionVector& stateVector,
                          const std::vector<Block>& extensions,
                          const ResolvedSyncProtocolOptions& options,
                          const DataSigner& signData)
{
  const auto syncName = makeSyncName(groupPrefix, options.version);
  if (extensions.size() > MAX_EXTENSION_BLOCKS) {
    NDN_THROW(Error("too many SVS extension blocks"));
  }
  for (const auto& extension : extensions) {
    if (extension.type() == tlv::StateVector || extension.type() == ndn::tlv::Data) {
      NDN_THROW(Error("extension collides with SVS core envelope"));
    }
  }
  ndn::encoding::EncodingBuffer encoder;
  size_t length = 0;

  Data stateData(syncName);
  Block content(ndn::tlv::Content);
  content.push_back(stateVector.encode());
  for (const auto& extension : extensions) {
    content.push_back(extension);
  }
  content.encode();
  stateData.setContent(content);
  if (!signData) {
    NDN_THROW(std::invalid_argument("SVS V3 requires a Data signer"));
  }
  signData(stateData);
  length += ndn::encoding::prependBlock(encoder, stateData.wireEncode());

  encoder.prependVarNumber(length);
  encoder.prependVarNumber(ndn::tlv::ApplicationParameters);

  Interest interest(syncName);
  auto parameters = encoder.block();
  interest.setApplicationParameters(parameters);
  interest.setInterestLifetime(options.syncInterestLifetime);
  return interest;
}

DecodedSyncEnvelope
SyncProtocolCodec::decode(const Interest& interest,
                          const Name& groupPrefix,
                          SvsProtocolVersion version,
                          bool decodeSemanticState)
{
  const auto expectedPrefix = makeSyncName(groupPrefix, version);
  if (interest.getName().size() != expectedPrefix.size() + 1 ||
      interest.getName().getPrefix(expectedPrefix.size()) != expectedPrefix ||
      !interest.getName().at(-1).isParametersSha256Digest() ||
      !interest.isParametersDigestValid()) {
    NDN_THROW(ndn::tlv::Error("SVS Sync Interest name or parameters digest"));
  }
  if (!interest.hasApplicationParameters()) {
    NDN_THROW(ndn::tlv::Error("missing SVS ApplicationParameters"));
  }

  auto params = interest.getApplicationParameters();
  params.parse();
  if (params.elements().empty()) {
    NDN_THROW(ndn::tlv::Error("empty SVS ApplicationParameters"));
  }

  DecodedSyncEnvelope decoded;
  auto first = params.elements_begin();
  if (version != SvsProtocolVersion::V3 || first->type() != ndn::tlv::Data) {
    NDN_THROW(ndn::tlv::Error("SVS V3 State Vector Data", first->type()));
  }
  if (std::next(first) != params.elements_end()) {
    NDN_THROW(Error("SVS V3 ApplicationParameters must contain one Data"));
  }
  Data stateData(*first);
  if (stateData.getName() != expectedPrefix || !stateData.getSignatureValue().isValid()) {
    NDN_THROW(ndn::tlv::Error("invalid SVS V3 State Vector Data"));
  }
  auto content = stateData.getContent();
  content.parse();
  if (content.elements().empty() || content.elements().front().type() != tlv::StateVector) {
    NDN_THROW(ndn::tlv::Error("SVS V3 StateVector Content"));
  }
  for (auto it = std::next(content.elements_begin()); it != content.elements_end(); ++it) {
    if (decoded.extensions.size() >= MAX_EXTENSION_BLOCKS) {
      NDN_THROW(Error("too many SVS extension blocks"));
    }
    if (it->type() == tlv::StateVector || it->type() == ndn::tlv::Data) {
      NDN_THROW(Error("duplicate SVS core envelope"));
    }
    decoded.extensions.push_back(*it);
  }
  decoded.stateVectorData = std::move(stateData);
  if (decodeSemanticState) {
    decoded.stateVector = decodeStateVector(decoded, version);
    decoded.stateVectorDecoded = true;
  }
  return decoded;
}

VersionVector
SyncProtocolCodec::decodeStateVector(const DecodedSyncEnvelope& envelope,
                                     SvsProtocolVersion version)
{
  if (version != SvsProtocolVersion::V3 || !envelope.stateVectorData) {
    NDN_THROW(Error("missing SVS V3 State Vector Data"));
  }
  auto content = envelope.stateVectorData->getContent();
  content.parse();
  if (content.elements().empty()) {
    NDN_THROW(Error("empty SVS V3 State Vector Data content"));
  }
  const auto& stateBlock = content.elements().front();
  if (stateBlock.type() != tlv::StateVector) {
    NDN_THROW(ndn::tlv::Error("SVS V3 StateVector Content", stateBlock.type()));
  }
  return VersionVector(stateBlock);
}

} // namespace ndn::svs
