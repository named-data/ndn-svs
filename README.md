# Sync-MANET
### Overview

This is a vector sync library over NDN with a chatroom application as demo. 

### Prerequisites

[ndn-cxx](https://github.com/named-data/ndn-cxx)

[NFD - NDN Forwarding Daemon](https://github.com/named-data/NFD)

### Build & Run

After installing ndn-cxx and NFD, build and run the chatroom client:
```
make
./client <my_id>
```

You may have to explicitly configure NFD to be multicast:
```bash
nfdc strategy set / /localhost/nfd/strategy/multicast/%FD%03
```

MacOS supports setting up ad-hoc Wi-Fi between computers (en0) interface, so you can configure routes throught this face. On other platforms, configure route with UDP4 face:

```
nfdc face create udp4://169.254.234.255::6363
```

Add outgoing face:

```
nfdc route add /ndn/svs/syncNotify <FaceID>
nfdc route add /ndn/svs/vsyncData <FaceID>
```

Restarting clients of same id numbers may cause inconsistency due to NFD Content Store. To disable Content Store, run:

```
nfdc cs config serve off
```