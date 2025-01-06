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

#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <ndn-svs/svsync.hpp>

struct Options
{
  std::string prefix;
  std::string m_id;
};

class Program
{
public:
  Program(const Options& options)
    : m_options(options)
  {
    // This is a usage example of the low level SvSyncCore API.

    // Create the SVSyncCore instance
    m_svs = std::make_shared<ndn::svs::SVSyncCore>(
      face,                                    // Shared NDN face
      ndn::Name(m_options.prefix),             // Sync prefix, common for all nodes in the group
      std::bind(&Program::onUpdate, this, _1), // Callback on learning new sequence numbers from SVS
      ndn::svs::SecurityOptions::DEFAULT,      // Security configuration
      ndn::Name(m_options.m_id)                // Unique prefix for this node
    );

    std::cout << "SVS client starting: " << m_options.m_id << std::endl;
  }

  void run()
  {
    // Begin processing face events in a separate thread.
    std::thread svsThread([this] { face.processEvents(); });

    // Increment our sequence number every 3 seconds
    while (true) {
      auto seq = m_svs->getSeqNo() + 1;
      m_svs->updateSeqNo(seq);
      std::cout << "Published sequence number: " << m_options.m_id << "=" << seq << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    // Wait for the SVSync thread to finish on exit.
    svsThread.join();
  }

protected:
  /**
   * Callback on receving a new State Vector from another node
   */
  void onUpdate(const std::vector<ndn::svs::MissingDataInfo>& v)
  {
    for (size_t i = 0; i < v.size(); i++) {
      for (ndn::svs::SeqNo s = v[i].low; s <= v[i].high; ++s) {
        std::cout << "Received update: " << v[i].nodeId << "=" << s << std::endl;
      }
    }
  }

public:
  const Options m_options;
  ndn::Face face;
  std::shared_ptr<ndn::svs::SVSyncCore> m_svs;
  ndn::KeyChain m_keyChain;
};

int
main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <prefix>" << std::endl;
    return 1;
  }

  Options opt;
  opt.prefix = "/ndn/svs";
  opt.m_id = argv[1];

  Program program(opt);
  program.run();

  return 0;
}
