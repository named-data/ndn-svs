/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
#ifndef NDN_SVS_SYNC_PROTOCOL_HPP
#define NDN_SVS_SYNC_PROTOCOL_HPP

#include "security-options.hpp"
#include "tlv.hpp"
#include "version-vector.hpp"

#include <optional>
#include <vector>

namespace ndn::svs {

enum class SvsProtocolVersion : uint8_t
{
  V3 = 3,
};

struct ResolvedSyncProtocolOptions
{
  SvsProtocolVersion version = SvsProtocolVersion::V3;
  std::optional<BootstrapTime> bootstrapTime;
  time::milliseconds syncInterestLifetime = 1_s;
  time::milliseconds suppressionPeriod = 200_ms;
  time::milliseconds periodicTimeout = 30_s;
  double periodicJitter = 0.1;
};

struct SyncProtocolOptions
{
  SvsProtocolVersion version = SvsProtocolVersion::V3;
  std::optional<BootstrapTime> bootstrapTime;
  std::optional<time::milliseconds> syncInterestLifetime;
  std::optional<time::milliseconds> suppressionPeriod;
  std::optional<time::milliseconds> periodicTimeout;
  double periodicJitter = 0.1;

  ResolvedSyncProtocolOptions resolve() const;
};

struct DecodedSyncEnvelope
{
  VersionVector stateVector;
  bool stateVectorDecoded = false;
  std::vector<Block> extensions;
  std::optional<Data> stateVectorData;
};

class SyncProtocolCodec
{
public:
  class Error : public std::runtime_error
  {
  public:
    using std::runtime_error::runtime_error;
  };

  using DataSigner = std::function<void(Data&)>;

  static constexpr size_t MAX_EXTENSION_BLOCKS = 16;

  static Name makeSyncName(const Name& groupPrefix, SvsProtocolVersion version);

  static Interest encode(const Name& groupPrefix,
                         const VersionVector& stateVector,
                         const std::vector<Block>& extensions,
                         const ResolvedSyncProtocolOptions& options,
                         const DataSigner& signData);

  static DecodedSyncEnvelope decode(const Interest& interest,
                                    const Name& groupPrefix,
                                    SvsProtocolVersion version,
                                    bool decodeSemanticState = true);

  static VersionVector decodeStateVector(const DecodedSyncEnvelope& envelope,
                                         SvsProtocolVersion version);
};

} // namespace ndn::svs

#endif // NDN_SVS_SYNC_PROTOCOL_HPP
