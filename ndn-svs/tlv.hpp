#ifndef NDN_SVS_TLV_HPP
#define NDN_SVS_TLV_HPP

namespace ndn {
namespace svs {
namespace tlv {

enum {
  StateVector = 201,
  StateVectorEntry = 202,
  SeqNo = 204,
  MappingData = 205,
  MappingEntry = 206,
};

} // namespace tlv
} // namespace svs
} // namespace ndn

#endif // NDN_SVS_TLV_HPP
