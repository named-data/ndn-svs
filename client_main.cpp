// AUTHOR: Zhaoning Kong
// Email: jonnykong@cs.ucla.edu

#include <boost/asio.hpp>
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

    // This is where I fuck up the program: TX

    std::string userInput = " ";

    // while (true) {
    //   std::cout << "Enter some fucking content" << std::endl;
    //   // send to Sync
    //   std::getline(std::cin, userInput);
    //   m_svs.publishMsg(userInput);
    // }
    // while
    // parse msg, std string --> call publish msg-->sync takes care of it

    thread_svs.join();
  }

 private:
  void onMsg(const std::string &msg) {
    printf("App received msg: %s\n", msg.c_str());
    fflush(stdout);
    // TODO: Print received msg to stdout
    // receive msg
    // display result
    // format

    // std::string s = msg;
    // std::string delimiter = ":";

    // size_t pos = 0;
    // std::string token;
    // while ((pos = s.find(delimiter)) != std::string::npos) {
    //   token = s.substr(0, pos);
    //   std::cout << token << std::endl;
    //   s.erase(0, pos + delimiter.length());
    //   }
    //   std::cout << s << std::endl;

    std::string str = msg;
    char delimiter = ':';
    std::vector<std::string> v = split(str, delimiter);

    std::cout << "sender id:" << v.at(0) << "data name:" << v.at(1)
              << "content:" << v.at(2) << std::endl;
  }
  // define split function
  std::vector<std::string> split(std::string str, char delimiter) {
    std::vector<std::string> internal;
    std::stringstream ss(str);
    std::string tok;

    while (std::getline(ss, tok, delimiter)) {
      internal.push_back(tok);
    }
    return internal;
  }

  const Options m_options;
  SVS m_svs;
};

}  // namespace svs
}  // namespace ndn

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage:\n");
    exit(1);
  }

  Options opt;
  opt.m_id = std::stoll(argv[1]);

  ndn::svs::Program program(opt);
  program.run();
  return 0;
}