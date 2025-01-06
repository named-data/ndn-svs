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

#include "svsync-base.hpp"
#include "store-memory.hpp"

#include <ndn-cxx/security/signing-helpers.hpp>

namespace ndn::svs {

SVSyncBase::SVSyncBase(const Name& syncPrefix,
                       const Name& dataPrefix,
                       const NodeID& id,
                       ndn::Face& face,
                       const UpdateCallback& updateCallback,
                       const SecurityOptions& securityOptions,
                       std::shared_ptr<DataStore> dataStore)
  : m_syncPrefix(syncPrefix)
  , m_dataPrefix(dataPrefix)
  , m_securityOptions(securityOptions)
  , m_id(id)
  , m_face(face)
  , m_fetcher(face, securityOptions)
  , m_onUpdate(updateCallback)
  , m_dataStore(std::move(dataStore))
  , m_core(m_face, m_syncPrefix, m_onUpdate, securityOptions, m_id)
{
  // Register new data store
  if (m_dataStore == DEFAULT_DATASTORE)
    m_dataStore = std::make_shared<MemoryDataStore>();

  // Register data prefix
  m_registeredDataPrefix = m_face.setInterestFilter(
    m_dataPrefix, std::bind(&SVSyncBase::onDataInterest, this, _2), [](auto&&...) {});
}

SeqNo
SVSyncBase::publishData(const uint8_t* buf,
                        size_t len,
                        const ndn::time::milliseconds& freshness,
                        const NodeID& nid)
{
  return publishData(ndn::encoding::makeBinaryBlock(ndn::tlv::Content, { buf, len }), freshness, nid);
}

SeqNo
SVSyncBase::publishData(const Block& content,
                        const ndn::time::milliseconds& freshness,
                        const NodeID& id,
                        uint32_t contentType)
{
  NodeID pubId = id != EMPTY_NODE_ID ? id : m_id;
  SeqNo newSeq = m_core.getSeqNo(pubId) + 1;

  Name dataName = getDataName(pubId, newSeq);
  auto data = std::make_shared<Data>(dataName);
  data->setContent(content);
  data->setFreshnessPeriod(freshness);
  data->setContentType(contentType);

  m_securityOptions.dataSigner->sign(*data);

  m_dataStore->insert(*data);
  m_core.updateSeqNo(newSeq, pubId);
  m_face.put(*data);

  return newSeq;
}

void
SVSyncBase::insertDataSegment(const Block& content,
                              const ndn::time::milliseconds& freshness,
                              const NodeID& nid,
                              const SeqNo seq,
                              const size_t segNo,
                              const Name::Component& finalBlock,
                              uint32_t contentType)
{
  Name dataName = getDataName(nid, seq).appendVersion(0).appendSegment(segNo);
  auto data = std::make_shared<Data>(dataName);
  data->setContent(content);
  data->setFreshnessPeriod(freshness);
  data->setContentType(contentType);
  data->setFinalBlock(finalBlock);
  m_securityOptions.dataSigner->sign(*data);
  m_dataStore->insert(*data);
}

void
SVSyncBase::onDataInterest(const Interest& interest)
{
  auto data = m_dataStore->find(interest);
  if (data != nullptr)
    m_face.put(*data);
}

void
SVSyncBase::fetchData(const NodeID& nid,
                      const SeqNo& seqNo,
                      const DataValidatedCallback& onValidated,
                      int nRetries)
{
  DataValidationErrorCallback onValidationFailed =
    std::bind(&SVSyncBase::onDataValidationFailed, this, _1, _2);
  TimeoutCallback onTimeout = [](auto&&...) {};
  fetchData(nid, seqNo, onValidated, onValidationFailed, onTimeout, nRetries);
}

void
SVSyncBase::fetchData(const NodeID& nid,
                      const SeqNo& seqNo,
                      const DataValidatedCallback& onValidated,
                      const DataValidationErrorCallback& onValidationFailed,
                      const TimeoutCallback& onTimeout,
                      int nRetries)
{
  Name interestName = getDataName(nid, seqNo);
  Interest interest(interestName);
  interest.setCanBePrefix(true);
  interest.setInterestLifetime(2_s);

  m_fetcher.expressInterest(interest,
                            std::bind(&SVSyncBase::onDataValidated, this, _2, onValidated),
                            std::bind(onTimeout, _1), // Nack
                            onTimeout,
                            nRetries,
                            onValidationFailed);
}

void
SVSyncBase::onDataValidated(const Data& data, const DataValidatedCallback& dataCallback)
{
  if (shouldCache(data))
    m_dataStore->insert(data);

  dataCallback(data);
}

void
SVSyncBase::onDataValidationFailed(const Data& data, const ValidationError& error)
{
}

} // namespace ndn::svs
