/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ptr.h"
#include <fstream>
#include <string>
#include <sstream>
#include <map>
#include <unistd.h>
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("isl_lock_min");

static std::ofstream gFwd;

static std::string
NowStr()
{
  std::ostringstream oss;
  oss << Simulator::Now().GetSeconds();
  return oss.str();
}

struct HopCtx
{
  uint32_t nodeId;
  std::string dir;   // "TX" or "RX"
  uint32_t iface;    // ipv4 interface index
  Ipv4Address dst;   // expected dst (FT IP)
  Ipv4Address nextHop;
};

// key = raw pointer of NetDevice
static std::map<const void*, HopCtx> gCtx;

/**
 * Callback signature must match what your trace source expects.
 * From your earlier compiler output, it expects:
 *   void(Ptr<const Packet>, uint32_t, std::string, uint32_t, Ipv4Address, Ipv4Address)
 *
 * We bind the NetDevice as the FIRST argument. MakeBoundCallback binds from the front.
 */
static void
TraceAdapter(Ptr<const NetDevice> dev,
             Ptr<const Packet> p,
             uint32_t a1,
             std::string a2,
             uint32_t a3,
             Ipv4Address a4,
             Ipv4Address a5)
{
  (void)a1;
  (void)a2;
  (void)a3;
  (void)a4;
  (void)a5;

 const void* key = PeekPointer(dev);
  auto it = gCtx.find(key);
  if (it == gCtx.end())
    return;

  const HopCtx& c = it->second;

  gFwd << NowStr()
       << " nodeId=" << c.nodeId
       << " dir=" << c.dir
       << " iface=" << c.iface
       << " dst=" << c.dst
       << " nextHop=" << c.nextHop
       << " bytes=" << p->GetSize()
       << "\n";
}
static void
Ipv4TxTrace(Ptr<const Packet> p, Ptr<Ipv4> ipv4, uint32_t iface)
{
  // 這裡不用解析 header，也可以先只印 nodeId/iface 來證明路徑有走
  Ptr<Node> n = ipv4->GetObject<Node>();
  gFwd << NowStr()
       << " nodeId=" << n->GetId()
       << " dir=TX"
       << " iface=" << iface
       << " bytes=" << p->GetSize()
       << "\n";
}

static void
Ipv4RxTrace(Ptr<const Packet> p, Ptr<Ipv4> ipv4, uint32_t iface)
{
  Ptr<Node> n = ipv4->GetObject<Node>();
  gFwd << NowStr()
       << " nodeId=" << n->GetId()
       << " dir=RX"
       << " iface=" << iface
       << " bytes=" << p->GetSize()
       << "\n";
}
static void
UdpClientTxTrace(Ptr<const Packet> p)
{
  std::cout << "[APP] UdpClient Tx bytes=" << p->GetSize()
            << " t=" << Simulator::Now().GetSeconds()
            << std::endl;
}

int
main(int argc, char* argv[])
{
  std::string outDir = "/tmp/isl_lock_min";
  double simTime = 2.0;
  uint32_t packetSize = 200;
  std::string interval = "100ms";

  CommandLine cmd;
  cmd.AddValue("outDir", "Output directory (must exist)", outDir);
  cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
  cmd.AddValue("packetSize", "UDP payload size (bytes)", packetSize);
  cmd.AddValue("interval", "Inter-packet interval (e.g., 100ms)", interval);
  cmd.Parse(argc, argv);
  
  if (chdir(outDir.c_str()) != 0)
{
  std::cerr << "Cannot chdir to outDir=" << outDir << std::endl;
  return 1;
}
  // 4 nodes: UT -> Sat-A -> Sat-B -> FT
  Ptr<Node> ut = CreateObject<Node>();
  Ptr<Node> satA = CreateObject<Node>();
  Ptr<Node> satB = CreateObject<Node>();
  Ptr<Node> ft = CreateObject<Node>();

  NodeContainer all(ut, satA, satB, ft);
  InternetStackHelper internet;
  internet.Install(all);

  for (Ptr<Node> node : {ut, satA, satB, ft})
{
  Ptr<Ipv4L3Protocol> l3 = node->GetObject<Ipv4L3Protocol>();

  bool okTx = l3->TraceConnectWithoutContext("Tx", MakeCallback(&Ipv4TxTrace));
  bool okRx = l3->TraceConnectWithoutContext("Rx", MakeCallback(&Ipv4RxTrace));

  std::cout << "[TRACE] nodeId=" << node->GetId()
            << " Ipv4L3 Tx=" << okTx
            << " Rx=" << okRx
            << std::endl;
}
  // Links
  PointToPointHelper p2p_ut_a;
  p2p_ut_a.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  p2p_ut_a.SetChannelAttribute("Delay", StringValue("10ms"));

  PointToPointHelper p2p_a_b;
  p2p_a_b.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  p2p_a_b.SetChannelAttribute("Delay", StringValue("10ms"));

  PointToPointHelper p2p_b_ft;
  p2p_b_ft.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
  p2p_b_ft.SetChannelAttribute("Delay", StringValue("10ms"));

  NetDeviceContainer dev_ut_a = p2p_ut_a.Install(ut, satA);
  NetDeviceContainer dev_a_b  = p2p_a_b.Install(satA, satB);
  NetDeviceContainer dev_b_ft = p2p_b_ft.Install(satB, ft);

  // IP addressing
  Ipv4AddressHelper ip;
  ip.SetBase("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer if_ut_a = ip.Assign(dev_ut_a);

  ip.SetBase("10.0.1.0", "255.255.255.0");
  Ipv4InterfaceContainer if_a_b = ip.Assign(dev_a_b);

  ip.SetBase("10.0.2.0", "255.255.255.0");
  Ipv4InterfaceContainer if_b_ft = ip.Assign(dev_b_ft);

  // Next-hops
  Ipv4Address nh_ut_to_a = if_ut_a.GetAddress(1); // Sat-A on UT<->A
  Ipv4Address nh_a_to_b  = if_a_b.GetAddress(1);  // Sat-B on A<->B
  Ipv4Address nh_b_to_ft = if_b_ft.GetAddress(1); // FT on B<->FT

  Ipv4Address ftAddr = if_b_ft.GetAddress(1);     // FT host address

  // Static routes
  Ipv4StaticRoutingHelper srh;

  Ptr<Ipv4> ipv4_ut = ut->GetObject<Ipv4>();
  Ptr<Ipv4> ipv4_a  = satA->GetObject<Ipv4>();
  Ptr<Ipv4> ipv4_b  = satB->GetObject<Ipv4>();
  Ptr<Ipv4> ipv4_ft = ft->GetObject<Ipv4>();

  Ptr<Ipv4StaticRouting> r_ut = srh.GetStaticRouting(ipv4_ut);
  Ptr<Ipv4StaticRouting> r_a  = srh.GetStaticRouting(ipv4_a);
  Ptr<Ipv4StaticRouting> r_b  = srh.GetStaticRouting(ipv4_b);
  Ptr<Ipv4StaticRouting> r_ft = srh.GetStaticRouting(ipv4_ft);

  uint32_t utIf = ipv4_ut->GetInterfaceForDevice(dev_ut_a.Get(0));
  uint32_t aIf0 = ipv4_a->GetInterfaceForDevice(dev_ut_a.Get(1));
  uint32_t aIf1 = ipv4_a->GetInterfaceForDevice(dev_a_b.Get(0));
  uint32_t bIf0 = ipv4_b->GetInterfaceForDevice(dev_a_b.Get(1));
  uint32_t bIf1 = ipv4_b->GetInterfaceForDevice(dev_b_ft.Get(0));
  uint32_t ftIf = ipv4_ft->GetInterfaceForDevice(dev_b_ft.Get(1));

  r_ut->AddHostRouteTo(ftAddr, nh_ut_to_a, utIf);
  r_a->AddHostRouteTo(ftAddr, nh_a_to_b, aIf1);
  r_b->AddHostRouteTo(ftAddr, nh_b_to_ft, bIf1);

  // forward_path.log
  std::string fwdPathFile = outDir + "/forward_path.log";
  gFwd.open(fwdPathFile, std::ios::out | std::ios::trunc);

  // Context per NetDevice (key must be dev.Get())
 gCtx[PeekPointer(dev_ut_a.Get(0))] = {ut->GetId(),   "TX", utIf, ftAddr, nh_ut_to_a};
gCtx[PeekPointer(dev_ut_a.Get(1))] = {satA->GetId(), "RX", aIf0, ftAddr, Ipv4Address("0.0.0.0")};

gCtx[PeekPointer(dev_a_b.Get(0))]  = {satA->GetId(), "TX", aIf1, ftAddr, nh_a_to_b};
gCtx[PeekPointer(dev_a_b.Get(1))]  = {satB->GetId(), "RX", bIf0, ftAddr, Ipv4Address("0.0.0.0")};

gCtx[PeekPointer(dev_b_ft.Get(0))] = {satB->GetId(), "TX", bIf1, ftAddr, nh_b_to_ft};
gCtx[PeekPointer(dev_b_ft.Get(1))] = {ft->GetId(),   "RX", ftIf, ftAddr, Ipv4Address("0.0.0.0")};

  // Attach traces (bind the same dev pointer used in gCtx key)
// ===== Attach trace with verification =====

/*bool ok1 = dev_ut_a.Get(0)->TraceConnectWithoutContext(
    "MacTx",
    MakeBoundCallback(&TraceAdapter, dev_ut_a.Get(0))
);
std::cout << "[TRACE] UT->SatA MacTx connect=" << ok1 << std::endl;

bool ok2 = dev_ut_a.Get(1)->TraceConnectWithoutContext(
    "MacRx",
    MakeBoundCallback(&TraceAdapter, dev_ut_a.Get(1))
);
std::cout << "[TRACE] SatA<-UT MacRx connect=" << ok2 << std::endl;


bool ok3 = dev_a_b.Get(0)->TraceConnectWithoutContext(
    "MacTx",
    MakeBoundCallback(&TraceAdapter, dev_a_b.Get(0))
);
std::cout << "[TRACE] SatA->SatB MacTx connect=" << ok3 << std::endl;

bool ok4 = dev_a_b.Get(1)->TraceConnectWithoutContext(
    "MacRx",
    MakeBoundCallback(&TraceAdapter, dev_a_b.Get(1))
);
std::cout << "[TRACE] SatB<-SatA MacRx connect=" << ok4 << std::endl;


bool ok5 = dev_b_ft.Get(0)->TraceConnectWithoutContext(
    "MacTx",
    MakeBoundCallback(&TraceAdapter, dev_b_ft.Get(0))
);
std::cout << "[TRACE] SatB->FT MacTx connect=" << ok5 << std::endl;

bool ok6 = dev_b_ft.Get(1)->TraceConnectWithoutContext(
    "MacRx",
    MakeBoundCallback(&TraceAdapter, dev_b_ft.Get(1))
);
std::cout << "[TRACE] FT<-SatB MacRx connect=" << ok6 << std::endl;
*/
  // PCAP
  p2p_ut_a.EnablePcap("pcap_ut_link", dev_ut_a.Get(0), true);
  p2p_a_b.EnablePcap("pcap_isl_link", dev_a_b.Get(0), true);
  // route_dump.txt
  std::string routeDumpFile = outDir + "/route_dump.txt";
  Ptr<OutputStreamWrapper> rd = Create<OutputStreamWrapper>(routeDumpFile, std::ios::out);

  *rd->GetStream() << "=== UT routes (nodeId=" << ut->GetId() << ") ===\n";
  r_ut->PrintRoutingTable(rd);

  *rd->GetStream() << "\n=== Sat-A routes (nodeId=" << satA->GetId() << ") ===\n";
  r_a->PrintRoutingTable(rd);

  *rd->GetStream() << "\n=== Sat-B routes (nodeId=" << satB->GetId() << ") ===\n";
  r_b->PrintRoutingTable(rd);

  *rd->GetStream() << "\n=== FT routes (nodeId=" << ft->GetId() << ") ===\n";
  r_ft->PrintRoutingTable(rd);

  // UDP apps
  uint16_t port = 9000;

  UdpServerHelper server(port);
  ApplicationContainer servApp = server.Install(ft);
  servApp.Start(Seconds(0.1));
  servApp.Stop(Seconds(simTime));

  UdpClientHelper client(ftAddr, port);
  client.SetAttribute("MaxPackets", UintegerValue(1000000));
  client.SetAttribute("Interval", TimeValue(Time(interval)));
  client.SetAttribute("PacketSize", UintegerValue(packetSize));
  ApplicationContainer cliApp = client.Install(ut);\
  
  Ptr<Application> app = cliApp.Get(0);
Ptr<UdpClient> uc = DynamicCast<UdpClient>(app);
if (uc)
{
  bool ok = uc->TraceConnectWithoutContext("Tx", MakeCallback(&UdpClientTxTrace));
  std::cout << "[TRACE] UdpClient Tx connect=" << ok << std::endl;
}
else
{
  std::cout << "[WARN] UdpClient cast failed\n";
}
  
  cliApp.Start(Seconds(0.2));
  cliApp.Stop(Seconds(simTime));

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();

  Ptr<UdpServer> s = DynamicCast<UdpServer>(servApp.Get(0));
  if (s)
  {
    std::cout << "[D1-1] FT(UdpServer) RxPackets=" << s->GetReceived() << std::endl;
  }

  Simulator::Destroy();
  gFwd.close();
  return 0;
}
