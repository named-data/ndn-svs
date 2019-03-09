// AUTHOR: Zhaoning Kong
// Email: jonnykong@cs.ucla.edu

#include <boost/asio.hpp>
#include <cstdint>
#include <iostream>
#include <ndn-cxx/face.hpp>
#include <ndn-cxx/name.hpp>
#include <thread>

#include "svs.hpp"

class Options {
public:
  Options() : prefix("/ndn/svs") {}

public:
  ndn::Name prefix;
};

namespace ndn {
namespace svs {

class Program {
public:
  explicit Program(const Options &options)
      : m_options(options),
        m_svs(0, std::bind(&Program::onMsg, this, std::placeholders::_1)) {
    printf("SVS client starts\n");
  }

  void run() {
    printf("SVS client runs\n");
    m_svs.registerPrefix();

    // Create other thread to run
    std::thread thread_svs([this] { m_svs.run(); });

    // Accept user input data
    printf("Accepting user input:\n");

    // TODO: Read user input from stdout
    m_svs.publishMsg("Hello World");

    thread_svs.join();
  }

private:
  void onMsg(const std::string &msg) {
    printf("App received msg\n");
    fflush(stdout);
    // TODO: Print received msg to stdout
  }

  const Options m_options;
  SVS m_svs;
};

} // namespace svs
} // namespace ndn

int main(int argc, char **argv) {
  Options opt;
  ndn::svs::Program program(opt);
  program.run();
  return 0;
}