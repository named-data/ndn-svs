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

#ifndef NDN_SVS_MAPPING_PROVIDER_HPP
#define NDN_SVS_MAPPING_PROVIDER_HPP

#include "core.hpp"
#include "fetcher.hpp"

#include <map>

namespace ndn::svs {

using MappingEntryPair = std::pair<Name, std::vector<Block>>;

/**
 * @brief TLV type for mapping list
 */
class MappingList
{
public:
  MappingList();

  explicit MappingList(const NodeID& nid);

  /// @brief Decode from Block
  explicit MappingList(const Block& block);

  /// @brief Encode to Block
  Block encode() const;

public:
  NodeID nodeId;
  std::vector<std::pair<SeqNo, MappingEntryPair>> pairs;
};

/**
 * @brief Provider for application name mapping
 */
class MappingProvider : noncopyable
{
public:
  MappingProvider(const Name& syncPrefix,
                  const NodeID& id,
                  ndn::Face& face,
                  const SecurityOptions& securityOptions);

  virtual ~MappingProvider() = default;

  using MappingListCallback = std::function<void(const MappingList&)>;

  /**
   * @brief Insert a mapping entry into the store
   */
  void insertMapping(const NodeID& nodeId, const SeqNo& seqNo, const MappingEntryPair& entry);

  /**
   * @brief Get a mapping and throw if not found
   *
   * @returns Corresponding application name
   */
  MappingEntryPair getMapping(const NodeID& nodeId, const SeqNo& seqNo);

  /**
   * @brief Retrieve the data mappings for encapsulated data packets
   *
   * @param info Query info
   * @param onValidated Callback when mapping is fetched and validated
   */
  void fetchNameMapping(const MissingDataInfo& info,
                        const MappingListCallback& onValidated,
                        int nRetries = 0);

  /**
   * @brief Retrieve the data mappings for encapsulated data packets
   *
   * @param info Query info
   * @param onValidated Callback when mapping is fetched and validated
   * @param onTimeout Callback when mapping is not retrieved
   */
  void fetchNameMapping(const MissingDataInfo& info,
                        const MappingListCallback& onValidated,
                        const TimeoutCallback& onTimeout,
                        int nRetries = 0);

private:
  /**
   * @brief Return data name for mapping query
   */
  Name getMappingQueryDataName(const MissingDataInfo& info);

  /**
   * @brief Return the query from mapping data name
   */
  MissingDataInfo parseMappingQueryDataName(const Name& name);

  void onMappingQuery(const Interest& interest);

private:
  const Name m_syncPrefix;
  const NodeID m_id;
  Face& m_face;
  Fetcher m_fetcher;
  const SecurityOptions m_securityOptions;

  ndn::ScopedRegisteredPrefixHandle m_registeredPrefix;

  std::map<Name, MappingEntryPair> m_map;
};

} // namespace ndn::svs

#endif // NDN_SVS_MAPPING_PROVIDER_HPP
