# ISL_lock
## run.out
```
[TRACE] nodeId=0 Ipv4L3 Tx=1 Rx=1
[TRACE] nodeId=1 Ipv4L3 Tx=1 Rx=1
[TRACE] nodeId=2 Ipv4L3 Tx=1 Rx=1
[TRACE] nodeId=3 Ipv4L3 Tx=1 Rx=1
[TRACE] UdpClient Tx connect=1
[APP] UdpClient Tx bytes=188 t=0.2
[APP] UdpClient Tx bytes=188 t=0.3
[APP] UdpClient Tx bytes=188 t=0.4
[APP] UdpClient Tx bytes=188 t=0.5
[APP] UdpClient Tx bytes=188 t=0.6
[APP] UdpClient Tx bytes=188 t=0.7
[APP] UdpClient Tx bytes=188 t=0.8
[APP] UdpClient Tx bytes=188 t=0.9
[APP] UdpClient Tx bytes=188 t=1
[APP] UdpClient Tx bytes=188 t=1.1
[APP] UdpClient Tx bytes=188 t=1.2
[APP] UdpClient Tx bytes=188 t=1.3
[APP] UdpClient Tx bytes=188 t=1.4
[APP] UdpClient Tx bytes=188 t=1.5
[APP] UdpClient Tx bytes=188 t=1.6
[APP] UdpClient Tx bytes=188 t=1.7
[APP] UdpClient Tx bytes=188 t=1.8
[APP] UdpClient Tx bytes=188 t=1.9
[D1-1] FT(UdpServer) RxPackets=18  // 代表 封包成功送到 FT。
```
## Forwarding path
UT → Sat-A → Sat-B → FT
```
nodeId=0 TX
nodeId=1 RX
nodeId=1 TX
nodeId=2 RX
nodeId=2 TX
nodeId=3 RX
```
## route_dump.txt
routing path 被鎖定
```
=== UT routes (nodeId=0) ===
Node: 0, Time: +0s, Local time: +0s, Ipv4StaticRouting table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
127.0.0.0       0.0.0.0         255.0.0.0       U     0      -      -   0
10.0.0.0        0.0.0.0         255.255.255.0   U     0      -      -   1
10.0.2.2        10.0.0.2        255.255.255.255 UHS   0      -      -   1


=== Sat-A routes (nodeId=1) ===
Node: 1, Time: +0s, Local time: +0s, Ipv4StaticRouting table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
127.0.0.0       0.0.0.0         255.0.0.0       U     0      -      -   0
10.0.0.0        0.0.0.0         255.255.255.0   U     0      -      -   1
10.0.1.0        0.0.0.0         255.255.255.0   U     0      -      -   2
10.0.2.2        10.0.1.2        255.255.255.255 UHS   0      -      -   2


=== Sat-B routes (nodeId=2) ===
Node: 2, Time: +0s, Local time: +0s, Ipv4StaticRouting table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
127.0.0.0       0.0.0.0         255.0.0.0       U     0      -      -   0
10.0.1.0        0.0.0.0         255.255.255.0   U     0      -      -   1
10.0.2.0        0.0.0.0         255.255.255.0   U     0      -      -   2
10.0.2.2        10.0.2.2        255.255.255.255 UHS   0      -      -   2


=== FT routes (nodeId=3) ===
Node: 3, Time: +0s, Local time: +0s, Ipv4StaticRouting table
Destination     Gateway         Genmask         Flags Metric Ref    Use Iface
127.0.0.0       0.0.0.0         255.0.0.0       U     0      -      -   0
10.0.2.0        0.0.0.0         255.255.255.0   U     0      -      -   1
```

# TW_Candidate
