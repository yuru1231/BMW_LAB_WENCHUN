/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * \file sat-qos-flowid-fwd-min.cc
 *
 * Minimal SNS3 Satellite QoS experiment (clean architecture):
 * - Build scenario via SimulationHelper
 * - Pick 1 GW-user node and 1 UT-user node
 * - Install UDP PacketSink on UT
 * - Install 3 custom CBR apps on GW, each tags packets with AppQosTag(qosClass)
 *
 * Notes:
 * - Do NOT use SatFlowIdTag (reserved for SNS3 internal flow id).
 * - Uses Time(string) constructor for interval parsing (ns-3.43).
 * - Resolves UT IP robustly by scanning non-loopback interfaces for first non-0.0.0.0 address.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/satellite-module.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/app-qos-tag.h"
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("sat-qos-flowid-fwd-min");

//
// ---------- Clean QoS class tag (application-owned) ----------
// qosClass: 1=Gold, 2=Silver, 3=Bronze (you define the semantics)
//


static void
OnSinkRx(Ptr<const Packet> p, const Address& /*from*/)
{
  AppQosTag t;
  if (p->PeekPacketTag(t))
  {
    NS_LOG_UNCOND("[RX] AppQosTag class=" << unsigned(t.GetClass())
                                         << " size=" << p->GetSize());
  }
  else
  {
    NS_LOG_UNCOND("[RX] NO AppQosTag size=" << p->GetSize());
  }
}

static Time
ParseTimeOrDie(const std::string& s)
{
  // ns-3.43 supports Time("20ms"), Time("1s"), etc.
  return Time(s);
}

static Ipv4Address
PickFirstNonZeroIpv4OrDie(Ptr<Node> n, uint32_t* outIfIndex = nullptr)
{
  Ptr<Ipv4> ipv4 = n->GetObject<Ipv4>();
  NS_ABORT_MSG_IF(!ipv4, "Node " << n->GetId() << " has no Ipv4 object");

  for (uint32_t ifi = 0; ifi < ipv4->GetNInterfaces(); ++ifi)
  {
    Ptr<NetDevice> dev = ipv4->GetNetDevice(ifi);
    if (!dev)
      continue;

    const std::string tname = dev->GetInstanceTypeId().GetName();
    if (tname.find("Loopback") != std::string::npos)
      continue;

    for (uint32_t ai = 0; ai < ipv4->GetNAddresses(ifi); ++ai)
    {
      Ipv4InterfaceAddress ifaddr = ipv4->GetAddress(ifi, ai);
      Ipv4Address a = ifaddr.GetLocal();
      if (a != Ipv4Address("0.0.0.0") && a != Ipv4Address("127.0.0.1"))
      {
        if (outIfIndex)
          *outIfIndex = ifi;
        return a;
      }
    }
  }

  NS_LOG_UNCOND("[IPDUMP] NodeId=" << n->GetId() << " interfaces:");
  for (uint32_t ifi = 0; ifi < ipv4->GetNInterfaces(); ++ifi)
  {
    Ptr<NetDevice> dev = ipv4->GetNetDevice(ifi);
    std::string tname = dev ? dev->GetInstanceTypeId().GetName() : "NULLDEV";
    for (uint32_t ai = 0; ai < ipv4->GetNAddresses(ifi); ++ai)
    {
      Ipv4InterfaceAddress ifaddr = ipv4->GetAddress(ifi, ai);
      NS_LOG_UNCOND("  if=" << ifi << " dev=" << tname << " addr=" << ifaddr.GetLocal());
    }
  }

  NS_ABORT_MSG("No usable IPv4 address found on node " << n->GetId());
  return Ipv4Address("0.0.0.0"); // unreachable
}

//
// ---------- Custom CBR app (adds AppQosTag) ----------
//
class FlowIdCbrApp : public Application
{
public:
  FlowIdCbrApp() = default;
  ~FlowIdCbrApp() override = default;

  void Setup(Ptr<Socket> socket,
             Address peer,
             Time interval,
             uint32_t pktSize,
             uint8_t qosClass)
  {
    m_socket = socket;
    m_peer = peer;
    m_interval = interval;
    m_pktSize = pktSize;
    m_qosClass = qosClass;
  }

private:
  void StartApplication() override
  {
    NS_ABORT_MSG_IF(!m_socket, "FlowIdCbrApp StartApplication: m_socket is null");

    m_running = true;
    m_socket->Bind();
    m_socket->Connect(m_peer);

    NS_LOG_INFO("[APP] start qosClass=" << unsigned(m_qosClass)
                                        << " interval=" << m_interval.GetSeconds()
                                        << " pktSize=" << m_pktSize);
    SendPacket();
  }

  void StopApplication() override
  {
    m_running = false;
    if (m_sendEvent.IsRunning())
      Simulator::Cancel(m_sendEvent);
    if (m_socket)
      m_socket->Close();

    NS_LOG_INFO("[APP] stop qosClass=" << unsigned(m_qosClass));
  }

  void SendPacket()
  {
    Ptr<Packet> packet = Create<Packet>(m_pktSize);

    AppQosTag tag;
    tag.SetClass(m_qosClass);      // 1/2/3 (Gold/Silver/Bronze)
    packet->AddPacketTag(tag);

    m_socket->Send(packet);

    if (m_running)
    {
      m_sendEvent = Simulator::Schedule(m_interval,
                                        &FlowIdCbrApp::SendPacket,
                                        this);
    }
  }

  Ptr<Socket> m_socket;
  Address m_peer;
  EventId m_sendEvent;
  bool m_running{false};
  Time m_interval{Seconds(0)};
  uint32_t m_pktSize{0};
  uint8_t m_qosClass{0};
};

//
// ----------------------- main -----------------------
//
int
main(int argc, char* argv[])
{
  std::string scenarioFolder = "constellation-leo-2-satellites";
  uint32_t simTime = 10;

  uint32_t gwUserIndex = 0;
  uint32_t utUserIndex = 0;

  uint16_t port = 9000;
  uint32_t pktSize = 512;

  uint32_t beamId = 1;
  uint32_t userCountPerUt = 2;

  std::string intvGold = "20ms";
  std::string intvSilver = "10ms";
  std::string intvBronze = "5ms";

  bool enableLogs = true;

  CommandLine cmd;
  cmd.AddValue("scenarioFolder", "Scenario folder", scenarioFolder);
  cmd.AddValue("simTime", "Simulation time (s)", simTime);
  cmd.AddValue("gwUserIndex", "Index of GW-user node (GetGwUserNodes())", gwUserIndex);
  cmd.AddValue("utUserIndex", "Index of UT-user node (GetUtUserNodes())", utUserIndex);
  cmd.AddValue("port", "UDP port for sink", port);
  cmd.AddValue("pktSize", "UDP packet size (bytes)", pktSize);
  cmd.AddValue("beamId", "Beam ID to include in BeamSet", beamId);
  cmd.AddValue("userCountPerUt", "Users per UT (required by SimulationHelper)", userCountPerUt);

  cmd.AddValue("gold", "Gold interval (e.g. 20ms)", intvGold);
  cmd.AddValue("silver", "Silver interval (e.g. 10ms)", intvSilver);
  cmd.AddValue("bronze", "Bronze interval (e.g. 5ms)", intvBronze);

  cmd.AddValue("enableLogs", "Enable component logs (0/1)", enableLogs);
  cmd.Parse(argc, argv);
  
  Config::SetDefault("ns3::SatFwdLinkSchedulerDefault::DebugEnable", BooleanValue(true));
  Config::SetDefault("ns3::SatFwdLinkSchedulerDefault::DebugEvery", UintegerValue(1));
  if (enableLogs)
  {
    LogComponentEnable("sat-qos-flowid-fwd-min", LOG_LEVEL_INFO);
  }

  // Must be before CreateSatScenario()
  Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));
  Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));
  Config::SetDefault("ns3::SatFwdLinkSchedulerDefault::DebugEnable", BooleanValue(true));
  Config::SetDefault("ns3::SatFwdLinkSchedulerDefault::DebugEnable", BooleanValue(true));
  Config::SetDefault("ns3::SatFwdLinkSchedulerDefault::DebugEvery", UintegerValue(2000)); // 每 2000 次 round 印一次
  Config::SetDefault("ns3::SatFwdLinkSchedulerDefault::DebugEnable", BooleanValue(true));
Config::SetDefault("ns3::SatFwdLinkSchedulerDefault::DebugEvery", UintegerValue(1));
  Ptr<SimulationHelper> sim = CreateObject<SimulationHelper>("sat-qos-flowid-fwd-min");

  sim->LoadScenario(scenarioFolder);
  sim->SetSimulationTime(Seconds(simTime));

  // Required by helper
  sim->SetBeamSet(std::set<uint32_t>{beamId});
  sim->SetUserCountPerUt(userCountPerUt);

  sim->CreateSatScenario();

  // Topology
  NodeContainer utNodes = Singleton<SatTopology>::Get()->GetUtNodes();
  NodeContainer gwUsers = Singleton<SatTopology>::Get()->GetGwUserNodes();
  NodeContainer utUsers = Singleton<SatTopology>::Get()->GetUtUserNodes(utNodes);

  NS_ABORT_MSG_IF(gwUsers.GetN() == 0, "No GW user nodes in scenario");
  NS_ABORT_MSG_IF(utUsers.GetN() == 0, "No UT user nodes in scenario");
  NS_ABORT_MSG_IF(gwUserIndex >= gwUsers.GetN(),
                  "gwUserIndex out of range: " << gwUserIndex << " >= " << gwUsers.GetN());
  NS_ABORT_MSG_IF(utUserIndex >= utUsers.GetN(),
                  "utUserIndex out of range: " << utUserIndex << " >= " << utUsers.GetN());

  Ptr<Node> gw = gwUsers.Get(gwUserIndex);
  Ptr<Node> ut = utUsers.Get(utUserIndex);

  NS_LOG_UNCOND("[TOPO] gwUser nodeId=" << gw->GetId()
                                       << " utUser nodeId=" << ut->GetId()
                                       << " simTime=" << simTime << "s");

  // UT sink
  PacketSinkHelper sink("ns3::UdpSocketFactory",
                        InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(ut);
  sinkApp.Start(Seconds(0.5));
  sinkApp.Stop(Seconds(simTime - 0.1));

  Ptr<PacketSink> ps = DynamicCast<PacketSink>(sinkApp.Get(0));
  NS_ABORT_MSG_IF(!ps, "PacketSink cast failed");
  ps->TraceConnectWithoutContext("Rx", MakeCallback(&OnSinkRx));
  Simulator::Schedule(Seconds(0.9), []() {
  NS_LOG_UNCOND("[DBG] t=" << Simulator::Now().GetSeconds() << " schedule alive");
});

Simulator::Schedule(Seconds(1.5), [ps]() {
  NS_LOG_UNCOND("[SINK] t=" << Simulator::Now().GetSeconds()
                           << " TotalRxBytes=" << ps->GetTotalRx());
});
  
  Simulator::Schedule(Seconds(simTime - 0.05), [ps]() {
  NS_LOG_UNCOND("[SINK] TotalRxBytes=" << ps->GetTotalRx());
});
  // Resolve UT IP robustly
  uint32_t chosenIf = 0;
  Ipv4Address utIp = PickFirstNonZeroIpv4OrDie(ut, &chosenIf);
  NS_LOG_UNCOND("[ADDR] utIp=" << utIp << " (ifIndex=" << chosenIf << ") port=" << port);

  InetSocketAddress utAddr(utIp, port);

  // Install flows on GW
  auto installFlow = [&](uint8_t qosClass, const std::string& intervalStr)
  {
    Ptr<Socket> sock = Socket::CreateSocket(gw, UdpSocketFactory::GetTypeId());

    Ptr<FlowIdCbrApp> app = CreateObject<FlowIdCbrApp>();
    app->Setup(sock,
               utAddr,
               ParseTimeOrDie(intervalStr),
               pktSize,
               qosClass);

    gw->AddApplication(app);
    app->SetStartTime(Seconds(1.0));
    app->SetStopTime(Seconds(simTime - 0.1));
  };

  installFlow(1, intvGold);   // Gold
  installFlow(2, intvSilver); // Silver
  installFlow(3, intvBronze); // Bronze

  sim->EnableProgressLogs();
  sim->RunSimulation();
  return 0;
}
