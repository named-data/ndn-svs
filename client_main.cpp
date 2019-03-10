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
  uint64_t m_id;
};

namespace ndn {
namespace svs {

class Program {
 public:
  explicit Program(const Options &options)
      : m_options(options),
        m_svs(m_options.m_id,
              std::bind(&Program::onMsg, this, std::placeholders::_1)) {
    printf("SVS client %llu starts\n", m_options.m_id);

    // Suppress warning
    Interest::setDefaultCanBePrefix(true);
  }

  void run() {
    m_svs.registerPrefix();

    // Create other thread to run
    std::thread thread_svs([this] { m_svs.run(); });

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
  /**
   * onMsg() - Callback on receiving msg from sync layer.
   */
  void onMsg(const std::string &msg) {
    // Parse received msg
    std::vector<std::string> result;
    size_t cursor = msg.find(":");
    result.push_back(msg.substr(0, cursor));
    if (cursor < msg.length() - 1)
      result.push_back(msg.substr(cursor + 1));
    else
      result.push_back("");

    // Print to stdout
    printf("User %s>> %s\n\n", result[0].c_str(), result[1].c_str());
  }


  const Options m_options;
  SVS m_svs;
};

}  // namespace svs
}  // namespace ndn

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: TODO\n");
    exit(1);
  }

  Options opt;
  opt.m_id = std::stoll(argv[1]);

  ndn::svs::Program program(opt);
  program.run();
  return 0;
}