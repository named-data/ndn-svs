# Sync-MANET
This is a vector sync library over NDN with a chatroom application as demo.

To build the chatroom client, simply run:
```
make
./client <my_id>
```

To configure NFD, run:
```
nfd-start
nfdc strategy set / /localhost/nfd/strategy/multicast/%FD%03
nfdc face create udp4://169.254.234.255::6363
nfdc route add /ndn/svs/syncNotify <FaceID>
nfdc route add /ndn/svs/vsyncData <FaceID>
```

To disable NFD Content Store, run:
```
nfdc cs config serve off
```