#ifndef NDN_SVS_TLV_HPP
#define NDN_SVS_TLV_HPP

#include <cstdint>

namespace ndn::svs::tlv {

enum : uint32_t {
  StateVector         = 201,
  StateVectorEntry    = 202,
  SeqNo               = 204,
  MappingData         = 205,
  MappingEntry        = 206,
  StateVectorLzma     = 211,
};

} // namespace ndn::svs::tlv

#endif // NDN_SVS_TLV_HPP
