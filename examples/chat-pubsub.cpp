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

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <ndn-svs/svspubsub.hpp>

using namespace ndn::svs;

class Options
{
public:
  Options() {}

public:
  std::string prefix;
  std::string m_id;
};

class Program
{
public:
  Program(const Options &options)
    : m_options(options)
  {
    // Use HMAC signing
    SecurityOptions securityOptions(m_keyChain);
    securityOptions.interestSigner->signingInfo.setSigningHmacKey("dGhpcyBpcyBhIHNlY3JldCBtZXNzYWdl");

    m_svspubsub = std::make_shared<SVSPubSub>(
      ndn::Name(m_options.prefix),
      ndn::Name(m_options.m_id),
      face,
      std::bind(&Program::onMissingData, this, _1),
      securityOptions);

    std::cout << "SVS client starting:" << m_options.m_id << std::endl;
    m_signingInfo.setSha256Signing();

    m_svspubsub->subscribeToProducer(ndn::Name("/ndn"), [&] (SVSPubSub::SubscriptionData subData)
    {
      const size_t data_size = subData.data.getContent().value_size();
      const std::string content_str((char *) subData.data.getContent().value(), data_size);

      std::cout << subData.producerPrefix << "[" << subData.seqNo << "] : " <<
                   subData.data.getName() << " : " << content_str << std::endl;
    }, true);
  }

  void
  run()
  {
    std::thread thread_svs([this] { face.processEvents(); });

    std::string init_msg = "User " + m_options.m_id + " has joined the groupchat";
    publishMsg(init_msg);

    std::string userInput = "";

    while (true) {
      std::getline(std::cin, userInput);
      publishMsg(userInput);
    }

    thread_svs.join();
  }

protected:
  void
  onMissingData(const std::vector<MissingDataInfo>& v)
  {
  }

  void
  publishMsg(std::string msg)
  {
    // Content block
    ndn::Block block = ndn::encoding::makeBinaryBlock(
      ndn::tlv::Content, reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size());

    // Data packet
    ndn::Name name(m_options.m_id);
    name.appendTimestamp();

    ndn::Data data(name);
    data.setContent(block);
    data.setFreshnessPeriod(ndn::time::milliseconds(1000));
    m_keyChain.sign(data, m_signingInfo);

    m_svspubsub->publishData(data);
  }

public:
  const Options m_options;
  ndn::Face face;
  std::shared_ptr<SVSPubSub> m_svspubsub;
  ndn::KeyChain m_keyChain;
  ndn::security::SigningInfo m_signingInfo;
};

int main(int argc, char **argv)
{
  if (argc != 2) {
    std::cout << "Usage: client <prefix>" << std::endl;
    exit(1);
  }

  Options opt;
  opt.prefix = "/ndn/svs";
  opt.m_id = argv[1];

  Program program(opt);
  program.run();
  return 0;
}
