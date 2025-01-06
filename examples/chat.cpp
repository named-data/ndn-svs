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
    // Use HMAC signing for Sync Interests
    // Note: this is not generally recommended, but is used here for simplicity
    ndn::svs::SecurityOptions securityOptions(m_keyChain);
    securityOptions.interestSigner->signingInfo.setSigningHmacKey("dGhpcyBpcyBhIHNlY3JldCBtZXNzYWdl");

    // Create the SVSync instance
    m_svs = std::make_shared<ndn::svs::SVSync>(
      ndn::Name(m_options.prefix), // Sync prefix, common for all nodes in the group
      ndn::Name(m_options.m_id),   // Unique data prefix for this node
      face,                        // Shared NDN face
      std::bind(&Program::onMissingData,
                this,
                _1),    // Callback on learning new sequence numbers from SVS
      securityOptions); // Security configuration

    std::cout << "SVS client starting: " << m_options.m_id << std::endl;
  }

  void run()
  {
    // Begin processing face events in a separate thread.
    std::thread svsThread([this] { face.processEvents(); });

    // Announce our presence.
    // Note that the SVSync instance is thread-safe.
    publishMsg("User " + m_options.m_id + " has joined the groupchat");

    // Read from stdin and publish messages.
    std::string userInput;
    while (true) {
      std::getline(std::cin, userInput);
      publishMsg(userInput);
    }

    // Wait for the SVSync thread to finish on exit.
    svsThread.join();
  }

protected:
  /**
   * Callback on receving a new State Vector from another node
   */
  void onMissingData(const std::vector<ndn::svs::MissingDataInfo>& v)
  {
    // Iterate over the entire difference set
    for (size_t i = 0; i < v.size(); i++) {
      // Iterate over each new sequence number that we learned
      for (ndn::svs::SeqNo s = v[i].low; s <= v[i].high; ++s) {
        // Request a single data packet using the SVSync API
        ndn::svs::NodeID nid = v[i].nodeId;
        m_svs->fetchData(nid, s, [nid](const auto& data) {
          std::string content(reinterpret_cast<const char*>(data.getContent().value()),
                              data.getContent().value_size());
          std::cout << data.getName() << " : " << content << std::endl;
        });
      }
    }
  }

  /**
   * Publish a string message to the SVSync group
   */
  void publishMsg(std::string_view msg)
  {
    // Encode the message into a Content TLV block, which is what the SVSync API
    // expects
    auto block = ndn::encoding::makeStringBlock(ndn::tlv::Content, msg);
    m_svs->publishData(block, ndn::time::seconds(1));
  }

public:
  const Options m_options;
  ndn::Face face;
  std::shared_ptr<ndn::svs::SVSyncBase> m_svs;
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
