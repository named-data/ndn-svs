/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2022 University of California, Los Angeles
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

#include <ndn-svs/svsync-base.hpp>

struct Options
{
  std::string prefix;
  std::string m_id;
};

class Program
{
public:
  Program(const Options &options)
    : m_options(options)
  {
    std::cout << "SVS client starting:" << m_options.m_id << std::endl;
    m_signingInfo.setSha256Signing();
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
  onMissingData(const std::vector<ndn::svs::MissingDataInfo>& v)
  {
    for (size_t i = 0; i < v.size(); i++)
    {
      for (ndn::svs::SeqNo s = v[i].low; s <= v[i].high; ++s)
      {
        ndn::svs::NodeID nid = v[i].nodeId;
        m_svs->fetchData(nid, s, [nid] (const ndn::Data& data)
          {
            const std::string content(reinterpret_cast<const char*>(data.getContent().value()),
                                      data.getContent().value_size());
            std::cout << data.getName() << " : " << content << std::endl;
          });
      }
    }
  }

  void
  publishMsg(const std::string& msg)
  {
    // Content block
    auto block = ndn::encoding::makeBinaryBlock(ndn::tlv::Content, msg.data(), msg.size());
    m_svs->publishData(block, ndn::time::milliseconds(1000));
  }

public:
  const Options m_options;
  ndn::Face face;
  std::shared_ptr<ndn::svs::SVSyncBase> m_svs;
  ndn::KeyChain m_keyChain;
  ndn::security::SigningInfo m_signingInfo;
};

template <typename T>
int
callMain(int argc, char **argv)
{
  if (argc != 2) {
    std::cout << "Usage: client <prefix>" << std::endl;
    exit(1);
  }

  Options opt;
  opt.prefix = "/ndn/svs";
  opt.m_id = argv[1];

  T program(opt);
  program.run();
  return 0;
}
