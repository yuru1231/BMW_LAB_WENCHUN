#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "beam_gate.h"

using namespace ns3;

int main(int argc, char** argv)
{
  Time::SetResolution(Time::NS);

  NodeContainer n;
  n.Create(2);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("1ms"));
  NetDeviceContainer d = p2p.Install(n);

  InternetStackHelper stack;
  stack.Install(n);

  Ipv4AddressHelper ip;
  ip.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifc = ip.Assign(d);

  uint16_t port = 9000;
  PacketSinkHelper sink("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), port));
  ApplicationContainer sinkApp = sink.Install(n.Get(1));
  sinkApp.Start(Seconds(0.0));
  sinkApp.Stop(Seconds(0.05));

  OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(ifc.GetAddress(1), port));
  onoff.SetAttribute("PacketSize", UintegerValue(1000));
  onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1]"));
  onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
  onoff.SetAttribute("DataRate", StringValue("0bps")); // start OFF

  auto appA = DynamicCast<OnOffApplication>(onoff.Install(n.Get(0)).Get(0));
  auto appB = DynamicCast<OnOffApplication>(onoff.Install(n.Get(0)).Get(0));
  auto appC = DynamicCast<OnOffApplication>(onoff.Install(n.Get(0)).Get(0));

  appA->Start(Seconds(0.0)); appA->Stop(Seconds(0.05));
  appB->Start(Seconds(0.0)); appB->Stop(Seconds(0.05));
  appC->Start(Seconds(0.0)); appC->Stop(Seconds(0.05));

  Ptr<t2::AppRateGate> gate = CreateObject<t2::AppRateGate>();
  gate->RegisterCell("A", {appA}, DataRate("10Mbps"));
  gate->RegisterCell("B", {appB}, DataRate("6Mbps"));
  gate->RegisterCell("C", {appC}, DataRate("4Mbps"));

  // W3 smoke: call Enable/Disable at fixed times
  Simulator::Schedule(MicroSeconds(0),    &t2::AppRateGate::Enable, gate, std::string("A"));
  Simulator::Schedule(MicroSeconds(5000), &t2::AppRateGate::Disable, gate, std::string("A"));
  Simulator::Schedule(MicroSeconds(5000), &t2::AppRateGate::Enable, gate, std::string("B"));
  Simulator::Schedule(MicroSeconds(8000), &t2::AppRateGate::Disable, gate, std::string("B"));
  Simulator::Schedule(MicroSeconds(8000), &t2::AppRateGate::Enable, gate, std::string("C"));
  Simulator::Schedule(MicroSeconds(10000),&t2::AppRateGate::Disable, gate, std::string("C"));

  Simulator::Stop(Seconds(0.05));
  Simulator::Run();
  Simulator::Destroy();
  return 0;
}
