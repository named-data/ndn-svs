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

#include "svsync-base.hpp"
#include "store-memory.hpp"
#include "tlv.hpp"

#include <ndn-cxx/security/signing-helpers.hpp>

namespace ndn {
namespace svs {

const NodeID SVSyncBase::EMPTY_NODE_ID;
const std::shared_ptr<DataStore> SVSyncBase::DEFAULT_DATASTORE;

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
  , m_dataStore(dataStore)
  , m_core(m_face, m_syncPrefix, m_onUpdate, securityOptions, m_id)
{
  // Register new data store
  if (m_dataStore == DEFAULT_DATASTORE)
    m_dataStore = make_shared<MemoryDataStore>();

  // Register data prefix
  m_registeredDataPrefix =
    m_face.setInterestFilter(m_dataPrefix,
                             bind(&SVSyncBase::onDataInterest, this, _2),
                             [] (const Name& prefix, const std::string& msg) {});
}

SeqNo
SVSyncBase::publishData(const uint8_t* buf, size_t len, const ndn::time::milliseconds& freshness,
                        const NodeID id)
{
  return publishData(ndn::encoding::makeBinaryBlock(ndn::tlv::Content, buf, len), freshness, id);
}

SeqNo
SVSyncBase::publishData(const Block& content, const ndn::time::milliseconds& freshness,
                        const NodeID id, const uint32_t contentType)
{
  NodeID pubId = id != EMPTY_NODE_ID ? id : m_id;
  SeqNo newSeq = m_core.getSeqNo(pubId) + 1;

  Name dataName = getDataName(pubId, newSeq);
  shared_ptr<Data> data = make_shared<Data>(dataName);
  data->setContent(content);
  data->setFreshnessPeriod(freshness);

  if (contentType != ndn::tlv::Invalid)
    data->setContentType(contentType);

  m_securityOptions.dataSigner->sign(*data);

  m_dataStore->insert(*data);
  m_core.updateSeqNo(newSeq, pubId);
  m_face.put(*data);

  return newSeq;
}

void
SVSyncBase::onDataInterest(const Interest &interest) {
  auto data = m_dataStore->find(interest);
  if (data != nullptr)
    m_face.put(*data);
}

void
SVSyncBase::fetchData(const NodeID& nid, const SeqNo& seqNo,
                      const DataValidatedCallback& onValidated,
                      const int nRetries)
{
  DataValidationErrorCallback onValidationFailed =
    bind(&SVSyncBase::onDataValidationFailed, this, _1, _2);
  TimeoutCallback onTimeout =
    [] (const Interest& interest) {};
  fetchData(nid, seqNo, onValidated, onValidationFailed, onTimeout, nRetries);
}

void
SVSyncBase::fetchData(const NodeID& nid, const SeqNo& seqNo,
                      const DataValidatedCallback& onValidated,
                      const DataValidationErrorCallback& onValidationFailed,
                      const TimeoutCallback& onTimeout,
                      const int nRetries)
{
  Name interestName = getDataName(nid, seqNo);
  Interest interest(interestName);
  interest.setMustBeFresh(false);
  interest.setCanBePrefix(false);
  interest.setInterestLifetime(ndn::time::milliseconds(2000));

  m_fetcher.expressInterest(interest,
                            bind(&SVSyncBase::onDataValidated, this, _2, onValidated),
                            bind(onTimeout, _1), // Nack
                            onTimeout, nRetries, onValidationFailed);
}

void
SVSyncBase::onDataValidated(const Data& data,
                            const DataValidatedCallback& dataCallback)
{
  if (shouldCache(data))
    m_dataStore->insert(data);

  dataCallback(data);
}

void
SVSyncBase::onDataValidationFailed(const Data& data,
                                   const ValidationError& error)
{
}

}  // namespace svs
}  // namespace ndn
