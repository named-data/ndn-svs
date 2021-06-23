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
  , m_onUpdate(updateCallback)
  , m_dataStore(dataStore)
  , m_core(m_face, m_keyChain, m_syncPrefix, m_onUpdate, securityOptions, m_id)
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

void
SVSyncBase::publishData(const uint8_t* buf, size_t len, const ndn::time::milliseconds& freshness,
                        const NodeID id)
{
  publishData(ndn::encoding::makeBinaryBlock(ndn::tlv::Content, buf, len), freshness, id);
}

void
SVSyncBase::publishData(const Data& data, const NodeID id)
{
  publishData(data.wireEncode(), data.getFreshnessPeriod(), id, ndn::tlv::Data);
}

void
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

  m_keyChain.sign(*data, m_securityOptions.dataSigningInfo);

  m_dataStore->insert(*data);
  m_core.updateSeqNo(newSeq, pubId);
}

void
SVSyncBase::onDataInterest(const Interest &interest) {
  auto data = m_dataStore->find(interest);
  if (data != nullptr) {
    m_face.put(*data);
    return;
  }

  if (isMappingQueryDataName(interest.getName()))
    onMappingQuery(interest);
}

void
SVSyncBase::fetchData(const NodeID& nid, const SeqNo& seqNo,
                      const DataValidatedCallback& onValidated,
                      const int nRetries)
{
  Name interestName = getDataName(nid, seqNo);
  Interest interest(interestName);
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  DataValidationErrorCallback onValidationFailed =
    bind(&SVSyncBase::onDataValidationFailed, this, _1, _2);
  TimeoutCallback onTimeout =
    [] (const Interest& interest) {};

  m_face.expressInterest(interest,
                         bind(&SVSyncBase::onData, this, _1, _2, onValidated, onValidationFailed),
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout), // Nack
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout));
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
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  m_face.expressInterest(interest,
                         bind(&SVSyncBase::onData, this, _1, _2, onValidated, onValidationFailed),
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout), // Nack
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout));
}

void
SVSyncBase::onData(const Interest& interest, const Data& data,
                   const DataValidatedCallback& onValidated,
                   const DataValidationErrorCallback& onFailed)
{
  if (static_cast<bool>(m_securityOptions.validator))
    m_securityOptions.validator->validate(data,
                                          bind(&SVSyncBase::onDataValidated, this, _1, onValidated, onFailed),
                                          onFailed);
  else
    onDataValidated(data, onValidated, onFailed);
}

void
SVSyncBase::onDataTimeout(const Interest& interest, int nRetries,
                          const DataValidatedCallback& dataCallback,
                          const DataValidationErrorCallback& failCallback,
                          const TimeoutCallback& timeoutCallback)
{
  if (nRetries <= 0)
    return timeoutCallback(interest);

  Interest newNonceInterest(interest);
  newNonceInterest.refreshNonce();

  m_face.expressInterest(newNonceInterest,
                         bind(&SVSyncBase::onData, this, _1, _2, dataCallback, failCallback),
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries - 1,
                              dataCallback, failCallback, timeoutCallback), // Nack
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries - 1,
                              dataCallback, failCallback, timeoutCallback));
}

void
SVSyncBase::onDataValidated(const Data& data,
                            const DataValidatedCallback& dataCallback,
                            const DataValidationErrorCallback& onFailed)
{
  if (shouldCache(data))
    m_dataStore->insert(data);

  if (data.getContentType() == ndn::tlv::Data)
  {
    Data encapsulatedData(data.getContent().blockFromValue());
    if (static_cast<bool>(m_securityOptions.validator))
      m_securityOptions.validator->validate(encapsulatedData, dataCallback, onFailed);
    else
      dataCallback(encapsulatedData);
  }
  else
  {
    dataCallback(data);
  }
}

void
SVSyncBase::onDataValidationFailed(const Data& data,
                                   const ValidationError& error)
{
}

void
SVSyncBase::onMappingQuery(const Interest& interest)
{
  MissingDataInfo query = parseMappingQueryDataName(interest.getName());
  std::cout << "Received mapping query " << query.session << " = " << query.low << " = " << query.high << std::endl;
}

void
SVSyncBase::fetchNameMapping(const MissingDataInfo info,
                             const DataValidatedCallback& onValidated,
                             const int nRetries)
{
  TimeoutCallback onTimeout =
    [] (const Interest& interest) {};
  return fetchNameMapping(info, onValidated, onTimeout, nRetries);
}

void
SVSyncBase::fetchNameMapping(const MissingDataInfo info,
                             const DataValidatedCallback& onValidated,
                             const TimeoutCallback& onTimeout,
                             const int nRetries)
{
  Name queryName = getMappingQueryDataName(info);
  Interest interest(queryName);
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  DataValidationErrorCallback onValidationFailed =
    bind(&SVSyncBase::onDataValidationFailed, this, _1, _2);

  m_face.expressInterest(interest,
                         bind(&SVSyncBase::onData, this, _1, _2, onValidated, onValidationFailed),
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout), // Nack
                         bind(&SVSyncBase::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout));
}

}  // namespace svs
}  // namespace ndn
