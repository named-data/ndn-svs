#ifndef NDN_SVS_TLV_HPP
#define NDN_SVS_TLV_HPP

namespace ndn::svs::tlv {

enum {
  StateVector = 201,
  StateVectorEntry = 202,
  SeqNo = 204,
  MappingData = 205,
  MappingEntry = 206,
};

} // namespace ndn::svs::tlv

#endif // NDN_SVS_TLV_HPP
