// AUTHOR: Zhaoning Kong
// Email: jonnykong@cs.ucla.edu

#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <cstdint>
#include <iostream>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>
#include <string>
#include <thread>
#include <vector>

#include "svs.hpp"

class Options {
 public:
  Options() : prefix("/ndn/svs") {}

 public:
  ndn::Name prefix;
  std::string m_id;
};

namespace ndn {
namespace svs {

class Program {
 public:
  explicit Program(const Options &options)
      : m_options(options),
        m_svs(
          ndn::Name("/ndnnew/svs"),
          ndn::Name(m_options.m_id),
          face,
          std::bind(&Program::onMissingData, this, _1)) {
    printf("SVS client %s starts\n", m_options.m_id.c_str());

    // Suppress warning
    Interest::setDefaultCanBePrefix(true);
  }

  ndn::Face face;

  void run() {
    // Create other thread to run
    std::thread thread_svs([this] { face.processEvents(); });

    std::string init_msg = "User " +
                           boost::lexical_cast<std::string>(m_options.m_id) +
                           " has joined the groupchat";
    m_svs.publishMsg(init_msg);

    std::string userInput = "";

    while (true) {
      // send to Sync
      std::getline(std::cin, userInput);
      m_svs.publishMsg(userInput);
    }

    thread_svs.join();
  }

 private:
  void onMissingData(const std::vector<ndn::svs::MissingDataInfo>& v) {
    for (size_t i = 0; i < v.size(); i++) {
      for(SeqNo s = v[i].low; s <= v[i].high; ++s) {
        NodeID nid = v[i].nid;
        m_svs.fetchData(nid, s, [this, nid] (const Data& data) {
            size_t data_size = data.getContent().value_size();
            std::string content_str((char *)data.getContent().value(), data_size);
            content_str = nid + ":" + content_str;
            std::cout << content_str << std::endl;
          });
      }
    }
  }

  const Options m_options;
  Socket m_svs;
};

}  // namespace svs
}  // namespace ndn

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: TODO\n");
    exit(1);
  }

  Options opt;
  opt.m_id = argv[1];

  ndn::svs::Program program(opt);
  program.run();
  return 0;
}