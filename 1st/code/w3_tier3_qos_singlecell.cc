// scratch/w3_tier3_qos_singlecell.cc
//
// Tier3-A+ (Pragmatic QoS Micro-scheduling demo in ns-3.43 with available QueueDiscs)
//
// Why this version?
// - In ns-3.43, PrioQueueDisc only exposes "Priomap" and can ASSERT if bands/classes are not explicitly built.
// - Your environment has these QueueDiscs: CoDel, FqCoDel, FqCobalt, FqPie, MqQueueDisc.
// - There is no DRR/WFQ QueueDisc available from your TypeId list, so "70/30" cannot be enforced directly
//   by a single built-in weighted scheduler on one queue.
//
// What we implement (mechanism-valid, explainable):
// 1) Gold protection via *reserved link* (logical service isolation):
//    - Create TWO parallel point-to-point links between GW and UT:
//        (A) Gold link:   goldReservedRate (default 5Mbps)
//        (B) Shared link: sharedRate        (default 15Mbps)
//    - Gold traffic goes to UT IP on Gold-link subnet.
//    - Silver/Bronze traffic goes to UT IP on Shared-link subnet.
//
// 2) Silver vs Bronze "70/30" via FQ-based approximation (flow-count weighting):
//    - On the shared link, install FqCoDelQueueDisc (per-flow fairness).
//    - Create 7 Silver subflows + 3 Bronze subflows with saturated offered load.
//      Under FQ, the remaining capacity is roughly shared proportional to number of flows => ~70/30.
//
// Outputs:
//   <outDir>/marker.txt
//   <outDir>/qos_metrics.csv          (per-flow lines + per-class aggregate lines)
//
// Build:
//   ./ns3 build
//
// Run example:
//   export P3="$HOME/beam_hopping/results/p3_tier3_qos_$(date +%Y%m%d_%H%M%S)"
//   export TR="$P3/traces"; export LG="$P3/logs"; mkdir -p "$TR" "$LG"
//   ./ns3 run "w3_tier3_qos_singlecell --outDir=$TR --goldReservedRate=5Mbps --sharedRate=15Mbps --bottleneckDelay=20ms --simTime=10s" 
//     2>&1 | tee "$LG/run.log"

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"

#include <fstream>
#include <iomanip>
#include <string>
#include <map>

using namespace ns3;

static inline uint8_t
TosFromDscp(uint8_t dscp)
{
  return static_cast<uint8_t>(dscp << 2); // DSCP in upper 6 bits
}

static Time
RateToInterval(const std::string& rateStr, uint32_t pktSize)
{
  DataRate r(rateStr);
  double pps = (r.GetBitRate() / 8.0) / static_cast<double>(pktSize);
  if (pps <= 0.0)
  {
    pps = 1.0;
  }
  return Seconds(1.0 / pps);
}

int
main(int argc, char** argv)
{
  // ---- CLI ----
  uint32_t seed = 1;
  std::string outDir = "/tmp/tier3";

  // Link rates (two parallel links)
  std::string goldReservedRate = "5Mbps";
  std::string sharedRate = "15Mbps";
  std::string bottleneckDelay = "20ms";
  std::string simTime = "10s";

  // Offered loads per subflow (make them high to saturate)
  // With FQ, shares tend towards proportional to number of flows.
  std::string perSilverSubflowRate = "10Mbps";
  std::string perBronzeSubflowRate = "10Mbps";
  std::string goldRate = "5Mbps"; // Gold offered; should fit inside goldReservedRate

  // Number of subflows to approximate 70/30 (7:3)
  uint32_t nSilver = 7;
  uint32_t nBronze = 3;

  CommandLine cmd;
  cmd.AddValue("outDir", "Output dir", outDir);
  cmd.AddValue("seed", "RNG seed", seed);
  cmd.AddValue("goldReservedRate", "Gold reserved link rate", goldReservedRate);
  cmd.AddValue("sharedRate", "Shared link rate (Silver+Bronze)", sharedRate);
  cmd.AddValue("bottleneckDelay", "Link delay", bottleneckDelay);
  cmd.AddValue("simTime", "Simulation time", simTime);

  cmd.AddValue("goldRate", "Gold offered rate", goldRate);
  cmd.AddValue("perSilverSubflowRate", "Offered rate per Silver subflow", perSilverSubflowRate);
  cmd.AddValue("perBronzeSubflowRate", "Offered rate per Bronze subflow", perBronzeSubflowRate);
  cmd.AddValue("nSilver", "Number of Silver subflows (default 7)", nSilver);
  cmd.AddValue("nBronze", "Number of Bronze subflows (default 3)", nBronze);
  cmd.Parse(argc, argv);

  // Marker
  {
    std::ofstream mk(outDir + "/marker.txt", std::ios::out);
    mk << "tier3 marker ok\n";
  }
  NS_LOG_UNCOND("[Tier3] outDir=" << outDir);

  RngSeedManager::SetSeed(seed);

  // ---- Topology: GW (0) <== two parallel p2p links ==> UT (1) ----
  NodeContainer n;
  n.Create(2);

  InternetStackHelper internet;
  internet.Install(n);

  // Gold link
  PointToPointHelper p2pGold;
  p2pGold.SetDeviceAttribute("DataRate", StringValue(goldReservedRate));
  p2pGold.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
  NetDeviceContainer devGold = p2pGold.Install(n);

  // Shared link
  PointToPointHelper p2pShared;
  p2pShared.SetDeviceAttribute("DataRate", StringValue(sharedRate));
  p2pShared.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
  NetDeviceContainer devShared = p2pShared.Install(n);

  // IP addressing: two subnets
  Ipv4AddressHelper ip1;
  ip1.SetBase("10.0.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifGold = ip1.Assign(devGold);

  Ipv4AddressHelper ip2;
  ip2.SetBase("10.0.2.0", "255.255.255.0");
  Ipv4InterfaceContainer ifShared = ip2.Assign(devShared);

  // Routing: destination IP selects the corresponding interface/subnet
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // ---- Traffic Control ----
  // Gold link: keep it simple (no special qdisc needed).
  // Shared link: install FqCoDel on GW egress to enforce per-flow fairness.
  {
    Ptr<TrafficControlLayer> tcl0 = n.Get(0)->GetObject<TrafficControlLayer>();
    if (tcl0)
    {
      // Defensive: delete any existing root qdisc on shared device
      Ptr<NetDevice> nd = devShared.Get(0);
      Ptr<QueueDisc> existing = tcl0->GetRootQueueDiscOnDevice(nd);
      if (existing)
      {
        NS_LOG_UNCOND("[Tier3] deleting existing root qdisc on shared dev: "
                      << existing->GetInstanceTypeId().GetName());
        tcl0->DeleteRootQueueDiscOnDevice(nd);
      }
    }
  }

  TrafficControlHelper tchShared;
  tchShared.SetRootQueueDisc("ns3::FqCoDelQueueDisc");
  tchShared.Install(devShared.Get(0));

  // ---- Applications ----
  const uint32_t pktSize = 1200;

  // Ports
  const uint16_t portGold = 5001;
  const uint16_t portSilverBase = 5100; // 5100..5100+nSilver-1
  const uint16_t portBronzeBase = 5200; // 5200..5200+nBronze-1

  // Servers on UT (Gold on Gold subnet IP; Silver/Bronze on Shared subnet IP)
  ApplicationContainer servers;

  UdpServerHelper srvGold(portGold);
  servers.Add(srvGold.Install(n.Get(1)));

  for (uint32_t i = 0; i < nSilver; ++i)
  {
    UdpServerHelper s(portSilverBase + i);
    servers.Add(s.Install(n.Get(1)));
  }
  for (uint32_t i = 0; i < nBronze; ++i)
  {
    UdpServerHelper s(portBronzeBase + i);
    servers.Add(s.Install(n.Get(1)));
  }

  servers.Start(Seconds(0.0));
  servers.Stop(Time(simTime));

  // Clients from GW
  ApplicationContainer clients;

  auto addClient = [&](Ipv4Address dstIp, uint16_t port, const std::string& rateStr, uint8_t tos) {
    UdpClientHelper cli(dstIp, port);
    cli.SetAttribute("PacketSize", UintegerValue(pktSize));
    cli.SetAttribute("Interval", TimeValue(RateToInterval(rateStr, pktSize)));
    cli.SetAttribute("MaxPackets", UintegerValue(0));
    cli.SetAttribute("Tos", UintegerValue(tos));
    clients.Add(cli.Install(n.Get(0)));
  };

  // Gold: goes to UT IP on Gold-link subnet
  addClient(ifGold.GetAddress(1), portGold, goldRate, TosFromDscp(46)); // EF

  // Silver/Bronze: go to UT IP on Shared-link subnet, saturated subflows
  for (uint32_t i = 0; i < nSilver; ++i)
  {
    addClient(ifShared.GetAddress(1), portSilverBase + i, perSilverSubflowRate, TosFromDscp(26)); // AF31
  }
  for (uint32_t i = 0; i < nBronze; ++i)
  {
    addClient(ifShared.GetAddress(1), portBronzeBase + i, perBronzeSubflowRate, TosFromDscp(0)); // Default
  }

  clients.Start(Seconds(0.1));
  clients.Stop(Time(simTime));

  // ---- FlowMonitor ----
  FlowMonitorHelper fmHelper;
  Ptr<FlowMonitor> monitor = fmHelper.InstallAll();

  Simulator::Stop(Time(simTime));
  Simulator::Run();

  monitor->CheckForLostPackets();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(fmHelper.GetClassifier());
  auto stats = monitor->GetFlowStats();

  double simSeconds = Simulator::Now().GetSeconds();
  if (simSeconds <= 0.0)
  {
    simSeconds = 1e-9;
  }

  // ---- Write CSV: per-flow + per-class aggregate ----
  std::ofstream csv(outDir + "/qos_metrics.csv", std::ios::out);
  csv << "level,class,flowId,dstIp,dstPort,rxBytes,rxPackets,lostPackets,throughput_Mbps,meanDelay_ms,meanJitter_ms,dropRate\n";
  csv << std::fixed << std::setprecision(6);

  struct Agg
  {
    uint64_t rxBytes = 0;
    uint64_t rxPackets = 0;
    uint64_t lostPackets = 0;
    double delaySumSec = 0.0;
    double jitterSumSec = 0.0;
  };
  std::map<std::string, Agg> agg;

  auto classify = [&](uint16_t dport) -> std::string {
    if (dport == portGold) return "Gold";
    if (dport >= portSilverBase && dport < portSilverBase + nSilver) return "Silver";
    if (dport >= portBronzeBase && dport < portBronzeBase + nBronze) return "Bronze";
    return "Unknown";
  };

  for (const auto& kv : stats)
  {
    FlowId fid = kv.first;
    const FlowMonitor::FlowStats& st = kv.second;
    Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(fid);

    std::string cls = classify(t.destinationPort);

    double thrMbps = (st.rxBytes * 8.0) / simSeconds / 1e6;
    double meanDelayMs = (st.rxPackets > 0) ? (st.delaySum.GetSeconds() / st.rxPackets) * 1e3 : 0.0;
    double meanJitterMs = (st.rxPackets > 0) ? (st.jitterSum.GetSeconds() / st.rxPackets) * 1e3 : 0.0;

    uint64_t sent = st.rxPackets + st.lostPackets;
    double dropRate = (sent > 0) ? static_cast<double>(st.lostPackets) / static_cast<double>(sent) : 0.0;

    csv << "flow," << cls << "," << fid << ","
        << t.destinationAddress << "," << t.destinationPort << ","
        << st.rxBytes << "," << st.rxPackets << "," << st.lostPackets << ","
        << thrMbps << "," << meanDelayMs << "," << meanJitterMs << "," << dropRate << "\n";

    // aggregate
    Agg& a = agg[cls];
    a.rxBytes += st.rxBytes;
    a.rxPackets += st.rxPackets;
    a.lostPackets += st.lostPackets;
    a.delaySumSec += st.delaySum.GetSeconds();
    a.jitterSumSec += st.jitterSum.GetSeconds();
  }

  // aggregate lines
  for (const auto& kv : agg)
  {
    const std::string& cls = kv.first;
    const Agg& a = kv.second;
    if (cls == "Unknown") continue;

    double thrMbps = (a.rxBytes * 8.0) / simSeconds / 1e6;
    double meanDelayMs = (a.rxPackets > 0) ? (a.delaySumSec / a.rxPackets) * 1e3 : 0.0;
    double meanJitterMs = (a.rxPackets > 0) ? (a.jitterSumSec / a.rxPackets) * 1e3 : 0.0;
    uint64_t sent = a.rxPackets + a.lostPackets;
    double dropRate = (sent > 0) ? static_cast<double>(a.lostPackets) / static_cast<double>(sent) : 0.0;

    csv << "class," << cls << ",0,*,*,"
        << a.rxBytes << "," << a.rxPackets << "," << a.lostPackets << ","
        << thrMbps << "," << meanDelayMs << "," << meanJitterMs << "," << dropRate << "\n";
  }

  csv.close();
  NS_LOG_UNCOND("[Tier3] wrote: " << outDir + "/qos_metrics.csv");

  Simulator::Destroy();
  return 0;
}
