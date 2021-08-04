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

#ifndef NDN_SVS_HASHED_SEQUENCE_HPP
#define NDN_SVS_HASHED_SEQUENCE_HPP

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>

namespace ndn {
namespace svs {

/**
 * Struct with a boost multi index container with
 * a sequence and a hash table
 */
template<typename T>
struct HashedSequence
{
  struct Sequence {};
  struct Hashtable {};
  using Container = boost::multi_index_container<
    T,
    boost::multi_index::indexed_by<
      boost::multi_index::sequenced<boost::multi_index::tag<Sequence>>,
      boost::multi_index::hashed_non_unique<boost::multi_index::tag<Hashtable>,
                                            boost::multi_index::identity<T>>
    >
  >;

  Container index;
  Container::index<Sequence>::type& seq = index.get<Sequence>();
  Container::index<Hashtable>::type& ht = index.get<Hashtable>();
};

} // namespace svs
} // namespace ndn

#endif // NDN_SVS_HASHED_SEQUENCE_HPP