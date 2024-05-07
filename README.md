# ndn-svs: State Vector Sync library for distributed realtime applications for NDN

![Language](https://img.shields.io/badge/C%2B%2B-17-blue)
[![CI](https://github.com/named-data/ndn-svs/actions/workflows/ci.yml/badge.svg)](https://github.com/named-data/ndn-svs/actions/workflows/ci.yml)

This library provides an implementation of the [State Vector Sync (SVS)](https://named-data.github.io/StateVectorSync/)
protocol and the [Pub/Sub API](https://dl.acm.org/doi/abs/10.1145/3460417.3483376) for state
synchronization between multiple clients over NDN.

ndn-svs uses the [ndn-cxx](https://github.com/named-data/ndn-cxx) library.

## Installation

### Prerequisites

* [ndn-cxx and its dependencies](https://docs.named-data.net/ndn-cxx/current/INSTALL.html)

### Build

To build ndn-svs from source:

    ./waf configure
    ./waf
    sudo ./waf install

To build on memory constrained systems, please use `./waf -j1` instead of `./waf`. This
will disable parallel compilation.

### Examples

To try out the demo CLI chat application:

    ./waf configure --enable-static --disable-shared --with-examples
    ./waf
    ./build/examples/chat <prefix>

Configure NFD to be multicast:

    nfdc strategy set <sync-prefix> /localhost/nfd/strategy/multicast

Clear the content store of NFD if you restart the example:

    nfdc cs erase /

where `sync-prefix` is `/ndn/svs` for the example application.

## Contributing

Contributions are welcome through GitHub.
Use `clang-format` to format the code before submitting a pull request.
The VS Code extension for `clang-format` is recommended to format the code automatically.

## License

ndn-svs is free software distributed under the GNU Lesser General Public License version 2.1.
See [`COPYING.md`](COPYING.md) for details.
