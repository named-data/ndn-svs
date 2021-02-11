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

#include "socket.hpp"

#include <ndn-cxx/util/string-helper.hpp>
#include <ndn-cxx/security/signing-helpers.hpp>

namespace ndn {
namespace svs {

const ndn::Name Socket::DEFAULT_NAME;
const std::shared_ptr<Validator> Socket::DEFAULT_VALIDATOR;
const NodeID Socket::EMPTY_NODE_ID;

Socket::Socket(const Name& syncPrefix,
               const NodeID& id,
               ndn::Face& face,
               const UpdateCallback& updateCallback,
               const std::string& syncKey,
               const Name& signingId,
               std::shared_ptr<Validator> validator)
  : m_syncPrefix(Name(syncPrefix).append("s"))
  , m_dataPrefix(Name(syncPrefix).append("d"))
  , m_signingId(signingId)
  , m_id(escape(id))
  , m_face(face)
  , m_validator(validator)
  , m_onUpdate(updateCallback)
  , m_logic(face, m_keyChain, m_syncPrefix, updateCallback, syncKey,
            m_signingId, m_validator, Logic::DEFAULT_ACK_FRESHNESS, m_id)
{
  m_registeredDataPrefix =
    m_face.setInterestFilter(Name(m_dataPrefix).append(m_id),
                             bind(&Socket::onDataInterest, this, _2),
                             [] (const Name& prefix, const std::string& msg) {});
}

Socket::~Socket()
{
}

void
Socket::publishData(const uint8_t* buf, size_t len, const ndn::time::milliseconds& freshness,
                    const uint64_t& seqNo, const NodeID id)
{
  publishData(ndn::encoding::makeBinaryBlock(ndn::tlv::Content, buf, len), freshness, seqNo, id);
}

void
Socket::publishData(const Block& content, const ndn::time::milliseconds& freshness,
                    const uint64_t& seqNo, const NodeID id)
{
  shared_ptr<Data> data = make_shared<Data>();
  data->setContent(content);
  data->setFreshnessPeriod(freshness);

  NodeID pubId = id != EMPTY_NODE_ID ? id : m_id;
  SeqNo newSeq = seqNo > 0 ? seqNo : m_logic.getSeqNo(pubId) + 1;

  Name dataName(m_dataPrefix);
  dataName.append(pubId).appendNumber(newSeq);
  data->setName(dataName);

  if (m_signingId.empty())
    m_keyChain.sign(*data);
  else
    m_keyChain.sign(*data, signingByIdentity(m_signingId));

  m_ims.insert(*data);
  m_logic.updateSeqNo(newSeq, pubId);
}

void Socket::onDataInterest(const Interest &interest) {
  // If have data, reply. Otherwise forward with probability (?)
  shared_ptr<const Data> data = m_ims.find(interest);
  if (data != nullptr)
  {
    m_face.put(*data);
  }
  else
  {
    // TODO
  }
}

void
Socket::fetchData(const NodeID& nid, const SeqNo& seqNo,
                  const DataValidatedCallback& onValidated,
                  int nRetries)
{
  Name interestName(m_dataPrefix);
  interestName.append(nid).appendNumber(seqNo);

  Interest interest(interestName);
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  DataValidationErrorCallback onValidationFailed =
    bind(&Socket::onDataValidationFailed, this, _1, _2);
  TimeoutCallback onTimeout =
    [] (const Interest& interest) {};

  m_face.expressInterest(interest,
                         bind(&Socket::onData, this, _1, _2, onValidated, onValidationFailed),
                         bind(&Socket::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout), // Nack
                         bind(&Socket::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout));
}

void
Socket::fetchData(const NodeID& nid, const SeqNo& seqNo,
                  const DataValidatedCallback& onValidated,
                  const DataValidationErrorCallback& onValidationFailed,
                  const TimeoutCallback& onTimeout,
                  int nRetries)
{
  Name interestName(m_dataPrefix);
  interestName.append(nid).appendNumber(seqNo);

  Interest interest(interestName);
  interest.setMustBeFresh(true);
  interest.setCanBePrefix(false);

  m_face.expressInterest(interest,
                         bind(&Socket::onData, this, _1, _2, onValidated, onValidationFailed),
                         bind(&Socket::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout), // Nack
                         bind(&Socket::onDataTimeout, this, _1, nRetries,
                              onValidated, onValidationFailed, onTimeout));
}

void
Socket::onData(const Interest& interest, const Data& data,
               const DataValidatedCallback& onValidated,
               const DataValidationErrorCallback& onFailed)
{
  if (static_cast<bool>(m_validator))
    m_validator->validate(data, onValidated, onFailed);
  else
    onValidated(data);
}

void
Socket::onDataTimeout(const Interest& interest, int nRetries,
                      const DataValidatedCallback& dataCallback,
                      const DataValidationErrorCallback& failCallback,
                      const TimeoutCallback& timeoutCallback)
{
  if (nRetries <= 0)
    return timeoutCallback(interest);

  Interest newNonceInterest(interest);
  newNonceInterest.refreshNonce();

  m_face.expressInterest(newNonceInterest,
                         bind(&Socket::onData, this, _1, _2, dataCallback, failCallback),
                         bind(&Socket::onDataTimeout, this, _1, nRetries - 1,
                              dataCallback, failCallback, timeoutCallback), // Nack
                         bind(&Socket::onDataTimeout, this, _1, nRetries - 1,
                              dataCallback, failCallback, timeoutCallback));
}

void
Socket::onDataValidationFailed(const Data& data,
                               const ValidationError& error)
{
}

}  // namespace svs
}  // namespace ndn
