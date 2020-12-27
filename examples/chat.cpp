/* -*- Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2012-2020 University of California, Los Angeles
 *
 * This file is part of ndn-svs, synchronization library for distributed realtime
 * applications for NDN.
 *
 * ndn-svs is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * ndn-svs is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndn-svs, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <ndn-svs/socket.hpp>

class Options
{
public:
  Options() : prefix("/ndn/svs") {}

public:
  std::string prefix;
  std::string m_id;
};

class Program
{
public:
  Program(const Options &options)
    : m_options(options),
      m_svs(
        ndn::Name(m_options.prefix),
        ndn::Name(m_options.m_id),
        face,
        std::bind(&Program::onMissingData, this, _1))
  {
    std::cout << "SVS client stared:" << m_options.m_id << std::endl;
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

private:
  void
  onMissingData(const std::vector<ndn::svs::MissingDataInfo>& v)
  {
    for (size_t i = 0; i < v.size(); i++)
    {
      for (ndn::svs::SeqNo s = v[i].low; s <= v[i].high; ++s)
      {
        ndn::svs::NodeID nid = v[i].nid;
        m_svs.fetchData(nid, s, [nid] (const ndn::Data& data)
          {
            size_t data_size = data.getContent().value_size();
            std::string content_str((char *)data.getContent().value(), data_size);
            content_str = nid + " : " + content_str;
            std::cout << content_str << std::endl;
          });
      }
    }
  }

  void
  publishMsg(std::string msg)
  {
    m_svs.publishData(reinterpret_cast<const uint8_t*>(msg.c_str()),
                      msg.size(),
                      ndn::time::milliseconds(1000));
  }

public:
  const Options m_options;
  ndn::Face face;
  ndn::svs::Socket m_svs;
};

int
main(int argc, char **argv)
{
  if (argc != 2) {
    std::cout << "Usage: client <prefix>" << std::endl;
    exit(1);
  }

  Options opt;
  opt.m_id = argv[1];

  Program program(opt);
  program.run();
  return 0;
}