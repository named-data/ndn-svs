# ndn-svs: State Vector Sync library for distributed realtime applications for NDN

This library implements the State Vector Sync (SVS) protocol to synchronise state between multiple clients over NDN.

**This is NOT an official implementation**.

ndn-svs uses the [ndn-cxx](https://github.com/named-data/ndn-cxx) library.

## Installation

### Prerequisites

* [ndn-cxx and its dependencies](https://named-data.net/doc/ndn-cxx/current/INSTALL.html)

### Build

To build ndn-svs from the source:

    ./waf configure
    ./waf
    sudo ./waf install

To build on memory constrained platform, please use `./waf -j1` instead of `./waf`. The
command will disable parallel compilation.

### Examples

To try out the demo CLI chat application:

    ./waf configure --enable-static --disable-shared --with-examples
    ./waf
    ./build/examples/chat <prefix>

## Contributing

We greatly appreciate contributions to the ndn-svs code base, provided that they are
licensed under the LGPL 2.1+ or a compatible license (see below).
If you are new to the NDN software community, please read the
[Contributor's Guide](https://github.com/named-data/.github/blob/master/CONTRIBUTING.md)
to get started.

Since ndn-svs is an unofficial implementation, contributions are welcome through GitHub.

## License

ndn-svs is an open source project licensed under the LGPL version 2.1.
See [`COPYING.md`](COPYING.md) for more information.
