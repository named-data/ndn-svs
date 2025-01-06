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

#include <ctime>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <ndn-svs/svspubsub.hpp>

using namespace ndn::svs;

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
    SecurityOptions secOpts(m_keyChain);
    secOpts.interestSigner->signingInfo.setSigningHmacKey("dGhpcyBpcyBhIHNlY3JldCBtZXNzYWdl");

    // Sign data packets using SHA256 (for simplicity)
    secOpts.dataSigner->signingInfo.setSha256Signing();

    // Do not fetch publications older than 10 seconds
    SVSPubSubOptions opts;
    opts.useTimestamp = true;
    opts.maxPubAge = ndn::time::seconds(10);

    // Create the Pub/Sub instance
    m_svsps = std::make_shared<SVSPubSub>(ndn::Name(m_options.prefix),
                                          ndn::Name(m_options.m_id),
                                          face,
                                          std::bind(&Program::onMissingData, this, _1),
                                          opts,
                                          secOpts);

    std::cout << "SVS client starting: " << m_options.m_id << std::endl;

    // Subscribe to all data packets with prefix /chat (the "topic")
    m_svsps->subscribe(ndn::Name("/chat"), [](const auto& subData) {
      std::string content(reinterpret_cast<const char*>(subData.data.data()), subData.data.size());
      std::cout << subData.producerPrefix << " [" << subData.seqNo << "] : " << subData.name << " : ";
      if (content.length() > 200) {
        std::cout << "[LONG] " << content.length() << " bytes"
                  << " [" << std::hash<std::string>{}(content) << "]";
      } else {
        std::cout << content;
      }
      std::cout << std::endl;
    });
  }

  void run()
  {
    // Begin processing face events in a separate thread.
    std::thread svsThread([this] { face.processEvents(); });

    // Announce our presence.
    // Note that the SVS-PS instance is thread-safe.
    publishMsg("User " + m_options.m_id + " has joined the groupchat");

    // Read from stdin and publish messages.
    std::string userInput;
    while (true) {
      std::getline(std::cin, userInput);
      publishMsg(userInput);
    }

    // Wait for the SVS-PS thread to finish.
    svsThread.join();
  }

protected:
  /**
   * Callback on receving a new State Vector from another node.
   * This will be called regardless of whether the missing data contains any
   * topics or producers that we are subscribed to.
   */
  void onMissingData(const std::vector<MissingDataInfo>&)
  {
    // Ignore any other missing data for this example
  }

  /**
   * Publish a string message to the group
   */
  void publishMsg(const std::string& msg)
  {
    // Message to send
    std::string content = msg;

    // If the message starts with "SEND " generate a new message
    // with random content with length after send
    if (msg.length() > 5 && msg.substr(0, 5) == "SEND ") {
      auto len = std::stoi(msg.substr(5));

      content = std::string(len, 'a');
      std::srand(std::time(nullptr));
      for (auto& c : content)
        c = std::rand() % 26 + 'a';

      std::cout << "> Sending random message with hash [" << std::hash<std::string>{}(content) << "]"
                << std::endl;
    }

    // Note that unlike SVSync, names can be arbitrary,
    // and need not be prefixed with the producer prefix.
    ndn::Name name("chat");      // topic of publication
    name.append(m_options.m_id); // who sent this
    name.appendTimestamp();      // and when

    m_svsps->publish(name, ndn::make_span(reinterpret_cast<const uint8_t*>(content.data()), content.size()));
  }

private:
  const Options m_options;
  ndn::Face face;
  std::shared_ptr<SVSPubSub> m_svsps;
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
