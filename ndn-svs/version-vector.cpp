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

#include "version-vector.hpp"
#include "tlv.hpp"

#include <algorithm>

namespace ndn::svs {

namespace {

bool
isBootstrapTimeTooFarInFuture(BootstrapTime bootstrapTime)
{
  const auto now = static_cast<BootstrapTime>(
    time::toUnixTimestamp<time::seconds>(time::system_clock::now()).count());
  return bootstrapTime > now + 86400;
}

} // namespace

VersionVector::VersionVector(const ndn::Block& block)
{
  if (block.type() != tlv::StateVector)
    NDN_THROW(ndn::tlv::Error("StateVector", block.type()));

  block.parse();

  for (auto it = block.elements_begin(); it < block.elements_end(); it++) {
    if (it->type() != tlv::StateVectorEntry)
      NDN_THROW(ndn::tlv::Error("StateVectorEntry", it->type()));

    it->parse();
    const auto& elements = it->elements();
    if (elements.empty() || elements.front().type() != ndn::tlv::Name) {
      NDN_THROW(ndn::tlv::Error("Name", elements.empty() ? 0 : elements.front().type()));
    }

    NodeID nodeId(elements.front());
    for (auto seqIt = elements.begin() + 1; seqIt != elements.end(); ++seqIt) {
      if (seqIt->type() == tlv::SeqNoEntry) {
        seqIt->parse();
        if (seqIt->elements().size() != 2 ||
            seqIt->elements().at(0).type() != tlv::BootstrapTime ||
            seqIt->elements().at(1).type() != tlv::SeqNo) {
          NDN_THROW(ndn::tlv::Error("SeqNoEntry", seqIt->type()));
        }
        BootstrapTime bootstrapTime =
          ndn::encoding::readNonNegativeInteger(seqIt->elements().at(0));
        if (isBootstrapTimeTooFarInFuture(bootstrapTime)) {
          NDN_THROW(Error("State vector bootstrap time is too far in the future"));
        }
        SeqNo seqNo = ndn::encoding::readNonNegativeInteger(seqIt->elements().at(1));
        if (seqNo == 0) {
          NDN_THROW(Error("State vector sequence number must be positive"));
        }
        set(nodeId, bootstrapTime, seqNo);
        continue;
      }

      NDN_THROW(ndn::tlv::Error("SeqNoEntry", seqIt->type()));
    }
  }
}

ndn::Block
VersionVector::encode() const
{
  ndn::encoding::EncodingBuffer enc;
  size_t totalLength = 0;

  for (auto it = m_entries.rbegin(); it != m_entries.rend(); it++) {
    size_t entryLength = 0;

    for (auto seqIt = it->second.rbegin(); seqIt != it->second.rend(); ++seqIt) {
      size_t seqEntryLength = 0;
      seqEntryLength += ndn::encoding::prependNonNegativeIntegerBlock(enc, tlv::SeqNo, seqIt->second);
      seqEntryLength += ndn::encoding::prependNonNegativeIntegerBlock(enc, tlv::BootstrapTime,
                                                                      seqIt->first);
      entryLength += enc.prependVarNumber(seqEntryLength);
      entryLength += enc.prependVarNumber(tlv::SeqNoEntry);
      entryLength += seqEntryLength;
    }

    // NodeID (Name)
    entryLength += ndn::encoding::prependBlock(enc, it->first.wireEncode());

    totalLength += enc.prependVarNumber(entryLength);
    totalLength += enc.prependVarNumber(tlv::StateVectorEntry);
    totalLength += entryLength;
  }

  enc.prependVarNumber(totalLength);
  enc.prependVarNumber(tlv::StateVector);
  return enc.block();
}

std::string
VersionVector::toStr() const
{
  std::ostringstream stream;
  for (const auto& elem : m_entries) {
    stream << elem.first << ":";
    for (const auto& seqEntry : elem.second) {
      stream << "[" << seqEntry.first << "," << seqEntry.second << "]";
    }
    stream << " ";
  }
  return stream.str();
}

void
VersionVector::refreshLatest(const NodeID& nid)
{
  auto node = m_entries.find(nid);
  if (node == m_entries.end() || node->second.empty()) {
    m_latestMap.erase(nid);
    return;
  }

  SeqNo latestSeqNo = 0;
  for (const auto& [bootstrapTime, seqNo] : node->second) {
    latestSeqNo = std::max(latestSeqNo, seqNo);
  }
  m_latestMap[nid] = latestSeqNo;
}

} // namespace ndn::svs
