#pragma once

#include <iostream>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/util/string-helper.hpp>

#include "svs_common.hpp"
#include "version-vector.hpp"

namespace ndn {
namespace svs {

inline std::string
ExtractNodeID(const Name &n)
{
  return unescape(n.get(-3).toUri());
}

inline std::string ExtractEncodedVV(const Name &n) { return n.get(-2).toUri(); }

inline uint64_t ExtractSequence(const Name &n) { return n.get(-2).toNumber(); }

}  // namespace svs
}  // namespace ndn