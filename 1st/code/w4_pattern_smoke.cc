#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include "beam_gate.h"
#include "pattern_reader.h"

#include <fstream>
#include <map>

using namespace ns3;

static void WriteStateTick(std::ofstream* fout,
                           Ptr<t2::AppRateGate> gate,
                           const std::vector<std::string>& cells)
{
  uint64_t t = Simulator::Now().GetMicroSeconds();
  (*fout) << t;
  for (auto& c : cells)
  {
    (*fout) << "," << (gate->IsEnabled(c) ? 1 : 0);
  }
  (*fout) << "\n";
}

int main(int argc, char** argv)
{
  std::string patternPath = "inputs/pattern.csv";
  std::string outDir = "outputs";
  uint64_t simStopUs = 50000; // 50ms
  uint64_t tCycleUs = 10000;
  uint32_t numCycles = 1;
  cmd.AddValue("tCycleUs", "Cycle length in us", tCycleUs);
  cmd.AddValue("numCycles", "Number of cycles to replicate", numCycles);

  CommandLine cmd;
  cmd.AddValue("patternPath", "Path to pattern.csv", patternPath);
  cmd.AddValue("outDir", "Output directory", outDir);
  cmd.AddValue("simStopUs", "Simulation stop time in us", simStopUs);
  cmd.Parse(argc, argv);

  Time::SetResolution(Time::NS);

  // Minimal network (same as W3)
  NodeContainer n; n.Create(2);
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("1ms"));
  NetDeviceContainer d = p2p.Install(n);

  InternetStackHelper stack; stack.Install(n);
  Ipv4AddressHelper ip;
  ip.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifc = ip.Assign(d);

  uint16_t port = 9000;
  PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(n.Get(1));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(MicroSeconds(simStopUs));

  OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(ifc.GetAddress(1), port));
  onoff.SetAttribute("PacketSize", UintegerValue(1000));
  onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  onoff.SetAttribute("DataRate", StringValue("0bps")); // start OFF

  // one app per cell
  ApplicationContainer aApps = onoff.Install(n.Get(0));
  ApplicationContainer bApps = onoff.Install(n.Get(0));
  ApplicationContainer cApps = onoff.Install(n.Get(0));
  // Start traffic later to avoid dominating early schedule; W5 will bring it back
  aApps.Start(Seconds(0.02)); aApps.Stop(MicroSeconds(simStopUs));
  bApps.Start(Seconds(0.02)); bApps.Stop(MicroSeconds(simStopUs));
  cApps.Start(Seconds(0.02)); cApps.Stop(MicroSeconds(simStopUs));

  auto appA = DynamicCast<OnOffApplication>(aApps.Get(0));
  auto appB = DynamicCast<OnOffApplication>(bApps.Get(0));
  auto appC = DynamicCast<OnOffApplication>(cApps.Get(0));

  Ptr<t2::AppRateGate> gate = CreateObject<t2::AppRateGate>();
  gate->RegisterCell("A", {appA}, DataRate("10Mbps"));
  gate->RegisterCell("B", {appB}, DataRate("6Mbps"));
  gate->RegisterCell("C", {appC}, DataRate("4Mbps"));

  // Outputs
  std::ofstream ev((outDir + "/beam_hop_events.log").c_str(), std::ios::out);
  std::ofstream st((outDir + "/beam_state_timeseries.csv").c_str(), std::ios::out);
  st << "time_us,A,B,C\n";

  // Read pattern
  auto rows = t2::ReadPatternCsv(patternPath);

  // Schedule from pattern
  for (uint32_t k = 0; k < numCycles; ++k)
  {
    uint64_t base = k * tCycleUs;
    for (auto& r : rows)
    {
      uint64_t ts = base + r.tStartUs;
      uint64_t te = base + r.tEndUs;

      Simulator::Schedule(MicroSeconds(ts), [gate, &ev, r, ts]{
        gate->Enable(r.cellId);
        ev << ts << " ENABLE " << r.cellId << "\n";
      });
      Simulator::Schedule(MicroSeconds(te), [gate, &ev, r, te]{
        gate->Disable(r.cellId);
        ev << te << " DISABLE " << r.cellId << "\n";
      });
    }
  }

  // State sampling every 1ms (0..12ms is enough for DoD, but we sample until stop)
  for (uint64_t t = 0; t <= simStopUs; t += 1000)
  {
    Simulator::Schedule(MicroSeconds(t), &WriteStateTick, &st, gate,
                        std::vector<std::string>{"A","B","C"});
  }

  Simulator::Stop(MicroSeconds(simStopUs));
  Simulator::Run();
  ev.close();
  st.close();
  Simulator::Destroy();
  return 0;
}
