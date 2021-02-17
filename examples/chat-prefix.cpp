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

#include "chat.hpp"

#include <ndn-svs/socket.hpp>

class ProgramPrefix : public Program
{
public:
  ProgramPrefix(const Options &options) : Program(options)
  {
    m_svs = std::make_shared<ndn::svs::Socket>(
      ndn::Name(m_options.prefix),
      ndn::Name(m_options.m_id),
      face,
      std::bind(&ProgramPrefix::onMissingData, this, _1),
      "dGhpcyBpcyBhIHNlY3JldCBtZXNzYWdl",
      ndn::Name(m_options.m_id));
  }
};

int
main(int argc, char **argv)
{
  return callMain<ProgramPrefix>(argc, argv);
}
