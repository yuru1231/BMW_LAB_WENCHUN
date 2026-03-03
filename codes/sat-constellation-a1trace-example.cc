/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 Magister Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Bastien Tauran <bastien.tauran@viveris.fr>
 */

#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/mobility-module.h"

#include <fstream>
#include <memory>
#include <algorithm>
#include <set>
#include <string>

using namespace ns3;

/**
 * \file sat-constellation-a1trace-example.cc
 * \ingroup satellite
 *
 * \brief Satellite constellation example + A1 position trace (SAT/GW/UT) + QueueDisc inspection
 */

NS_LOG_COMPONENT_DEFINE("sat-constellation-a1trace-example");

static bool
IsLoopbackDev(Ptr<NetDevice> dev)
{
  if (!dev)
  {
    return true;
  }
  const std::string tname = dev->GetInstanceTypeId().GetName();
  return (tname.find("Loopback") != std::string::npos);
}

static Time
ParseTimeOrDie(const std::string& s, const std::string& what)
{
  // Works across ns-3 versions: parse time using TimeValue + StringValue
  TimeValue tv;

  // Some ns-3 versions have GetValue(TimeValue&) on StringValue,
  // others use tv.DeserializeFromString(). We'll use DeserializeFromString
  // which is widely available via AttributeValue.
  bool ok = tv.DeserializeFromString(s, MakeTimeChecker());
  NS_ABORT_MSG_IF(!ok, "Bad time string for " << what << ": '" << s << "'");

  return tv.Get();
}

int
main(int argc, char* argv[])
{
  // ===============================
  // User parameters (CommandLine)
  // ===============================
  uint32_t packetSize = 512;
  std::string interval = "20ms";
  std::string scenarioFolder = "constellation-eutelsat-geo-2-sats-isls";
  std::string outputPath = "";
  uint32_t simTime = 30;

  // ---- Layer0 trace parameters ----
  bool enableUtGwPosTrace = false;
  uint32_t posTraceMode = 1; // reserved; this example outputs xyz trace
  double posTraceStartTime = 0.0;
  double posTraceStopTime = 30.0;
  double posDt = 10.0;
  uint32_t posTraceFlushEvery = 1;
  uint32_t posTraceMaxSat = 0;
  uint32_t posTraceMaxGw = 0;
  uint32_t posTraceMaxUt = 0;

  // ===============================
  // Parse CLI
  // ===============================
  CommandLine cmd;
  cmd.AddValue("packetSize", "Size of constant packet (bytes)", packetSize);
  cmd.AddValue("interval", "Interval to sent packets in seconds (e.g. (1s))", interval);
  cmd.AddValue("scenarioFolder",
               "Scenario folder (e.g. constellation-eutelsat-geo-2-sats-isls)",
               scenarioFolder);
  cmd.AddValue("simTime", "Simulation length in seconds", simTime);

  cmd.AddValue("enableUtGwPosTrace", "Enable SAT/GW/UT positions trace (0/1)", enableUtGwPosTrace);
  cmd.AddValue("posTraceMode", "Position trace mode", posTraceMode);
  cmd.AddValue("posTraceStartTime", "Position trace start time (sec)", posTraceStartTime);
  cmd.AddValue("posTraceStopTime", "Position trace stop time (sec)", posTraceStopTime);
  cmd.AddValue("posDt", "Position sampling period (sec)", posDt);
  cmd.AddValue("posTraceFlushEvery", "Flush to disk every N samples", posTraceFlushEvery);
  cmd.AddValue("posTraceMaxSat", "Max SAT nodes traced (0=no limit)", posTraceMaxSat);
  cmd.AddValue("posTraceMaxGw", "Max GW nodes traced (0=no limit)", posTraceMaxGw);
  cmd.AddValue("posTraceMaxUt", "Max UT nodes traced (0=no limit)", posTraceMaxUt);

  cmd.AddValue("OutputPath", "Output path for storing the simulation statistics", outputPath);

  // Create helper BEFORE parse if you want default UI args
  Ptr<SimulationHelper> simulationHelper =
      CreateObject<SimulationHelper>("sat-constellation-a1trace-example");
  simulationHelper->AddDefaultUiArguments(cmd);

  cmd.Parse(argc, argv);

  // ===============================
  // Default configs (original example)
  // ===============================
  Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));
  Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));

  Config::SetDefault("ns3::SatOrbiterFeederPhy::QueueSize", UintegerValue(100000));
  Config::SetDefault("ns3::SatOrbiterUserPhy::QueueSize", UintegerValue(100000));

  Config::SetDefault("ns3::PointToPointIslHelper::IslDataRate",
                     DataRateValue(DataRate("100Mb/s")));
  Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionEachRequest", BooleanValue(false));
  Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionPeriod", TimeValue(Seconds(1)));
  Config::SetDefault("ns3::SatHelper::GwUsers", UintegerValue(3));
  Config::SetDefault("ns3::SatGwMac::SendNcrBroadcast", BooleanValue(false));

  Config::SetDefault("ns3::SatHelper::BeamNetworkAddress", Ipv4AddressValue("20.1.0.0"));
  Config::SetDefault("ns3::SatHelper::GwNetworkAddress", Ipv4AddressValue("10.1.0.0"));
  Config::SetDefault("ns3::SatHelper::UtNetworkAddress", Ipv4AddressValue("250.1.0.0"));

  Config::SetDefault("ns3::SatBbFrameConf::AcmEnabled", BooleanValue(true));
  Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));
  Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(true));

  // ===============================
  // Scenario (DO ONCE)
  // ===============================
  simulationHelper->LoadScenario(scenarioFolder);
  simulationHelper->SetSimulationTime(Seconds(simTime));

  std::set<uint32_t> beamSet = {43, 30};
  std::set<uint32_t> beamSetTelesat = {1, 43, 60, 64};

  if (scenarioFolder == "constellation-telesat-351-sats")
  {
    simulationHelper->SetBeamSet(beamSetTelesat);
  }
  else
  {
    simulationHelper->SetBeamSet(beamSet);
  }

  simulationHelper->SetUserCountPerUt(2);

  LogComponentEnable("sat-constellation-a1trace-example", LOG_LEVEL_INFO);

  // Create satellites + ground + UT + stacks
  simulationHelper->CreateSatScenario();

  // =============================================
  // Layer0 — A1 Position Trace (Stable Version)
  // =============================================
  if (enableUtGwPosTrace)
  {
    // If OutputPath not set, default to current directory
    std::string outPath = outputPath.empty() ? std::string(".") : outputPath;

    // Use shared_ptr so scheduled events can safely access streams
    auto satCsv = std::make_shared<std::ofstream>(outPath + "/sat_positions_xyz_timeseries.csv");
    auto gwCsv  = std::make_shared<std::ofstream>(outPath + "/gw_positions_xyz_timeseries.csv");
    auto utCsv  = std::make_shared<std::ofstream>(outPath + "/ut_positions_xyz_timeseries.csv");

    NS_ABORT_MSG_IF(!(*satCsv), "Failed to open: " << outPath << "/sat_positions_xyz_timeseries.csv");
    NS_ABORT_MSG_IF(!(*gwCsv),  "Failed to open: " << outPath << "/gw_positions_xyz_timeseries.csv");
    NS_ABORT_MSG_IF(!(*utCsv),  "Failed to open: " << outPath << "/ut_positions_xyz_timeseries.csv");

    (*satCsv) << "time_sec,role,nodeId,x_m,y_m,z_m\n";
    (*gwCsv)  << "time_sec,role,nodeId,x_m,y_m,z_m\n";
    (*utCsv)  << "time_sec,role,nodeId,x_m,y_m,z_m\n";

    // SNS3 API: satellites are "Orbiters"
    NodeContainer satsTopo = Singleton<SatTopology>::Get()->GetOrbiterNodes();
    NodeContainer gwsTopo  = Singleton<SatTopology>::Get()->GetGwNodes();
    NodeContainer utsTopo  = Singleton<SatTopology>::Get()->GetUtNodes();

    // Prevent scheduling beyond sim end
    const double tStop  = std::min(posTraceStopTime, static_cast<double>(simTime));
    const double tStart = std::max(0.0, posTraceStartTime);

    auto LimitContainer = [](const NodeContainer& in, uint32_t maxN) -> NodeContainer {
      if (maxN == 0 || in.GetN() <= maxN)
      {
        return in;
      }
      NodeContainer out;
      for (uint32_t i = 0; i < maxN; ++i)
      {
        out.Add(in.Get(i));
      }
      return out;
    };

    NodeContainer satsL = LimitContainer(satsTopo, posTraceMaxSat);
    NodeContainer gwsL  = LimitContainer(gwsTopo,  posTraceMaxGw);
    NodeContainer utsL  = LimitContainer(utsTopo,  posTraceMaxUt);

    for (double t = tStart; t <= tStop + 1e-12; t += posDt)
    {
      Simulator::Schedule(
          Seconds(t),
          [satsL, gwsL, utsL, satCsv, gwCsv, utCsv, t, posTraceFlushEvery]() {
            for (uint32_t i = 0; i < satsL.GetN(); ++i)
            {
              Ptr<Node> n = satsL.Get(i);
              Ptr<MobilityModel> mob = n->GetObject<MobilityModel>();
              if (!mob)
                continue;
              Vector p = mob->GetPosition();
              (*satCsv) << t << ",SAT," << n->GetId() << "," << p.x << "," << p.y << "," << p.z << "\n";
            }

            for (uint32_t i = 0; i < gwsL.GetN(); ++i)
            {
              Ptr<Node> n = gwsL.Get(i);
              Ptr<MobilityModel> mob = n->GetObject<MobilityModel>();
              if (!mob)
                continue;
              Vector p = mob->GetPosition();
              (*gwCsv) << t << ",GW," << n->GetId() << "," << p.x << "," << p.y << "," << p.z << "\n";
            }

            for (uint32_t i = 0; i < utsL.GetN(); ++i)
            {
              Ptr<Node> n = utsL.Get(i);
              Ptr<MobilityModel> mob = n->GetObject<MobilityModel>();
              if (!mob)
                continue;
              Vector p = mob->GetPosition();
              (*utCsv) << t << ",UT," << n->GetId() << "," << p.x << "," << p.y << "," << p.z << "\n";
            }

            if (posTraceFlushEvery > 0)
            {
              satCsv->flush();
              gwCsv->flush();
              utCsv->flush();
            }
          });
    }
  }

  // =============================================
  // Print topology (optional)
  // =============================================
  Singleton<SatTopology>::Get()->PrintTopology(std::cout);
  Singleton<SatIdMapper>::Get()->ShowIslMap();

  // =============================================
  // Traffic configuration (original example)
  // =============================================
  Config::SetDefault("ns3::CbrApplication::Interval", StringValue(interval));
  Config::SetDefault("ns3::CbrApplication::PacketSize", UintegerValue(packetSize));

  // Use robust parsing for interval string
  Time intervalT = ParseTimeOrDie(interval, "interval");

  Time startTime = Seconds(1.0);
  Time stopTime = Seconds(std::max<int>(1, static_cast<int>(simTime) - 1)); // safe default
  Time startDelay = Seconds(0.0);

  NodeContainer gwNodes = Singleton<SatTopology>::Get()->GetGwNodes();
  NodeContainer utNodes = Singleton<SatTopology>::Get()->GetUtNodes();
  NodeContainer gwUsers = Singleton<SatTopology>::Get()->GetGwUserNodes();
  NodeContainer utUsers = Singleton<SatTopology>::Get()->GetUtUserNodes(utNodes);

  // =============================================
  // Layer3 — QoS Baseline CHECK (no install)
  // 목적：確認既有 root QueueDisc 在哪些 device 上
  // =============================================
  for (uint32_t i = 0; i < utUsers.GetN(); ++i)
  {
    Ptr<Node> node = utUsers.Get(i);
    Ptr<TrafficControlLayer> tcl = node->GetObject<TrafficControlLayer>();

    if (!tcl)
    {
      NS_LOG_UNCOND("[L3][QDISC] utUser nodeId=" << node->GetId() << " has NO TrafficControlLayer");
      continue;
    }

    for (uint32_t d = 0; d < node->GetNDevices(); ++d)
    {
      Ptr<NetDevice> dev = node->GetDevice(d);
      if (!dev || IsLoopbackDev(dev))
      {
        continue;
      }

      Ptr<QueueDisc> root = tcl->GetRootQueueDiscOnDevice(dev);
      if (root)
      {
        NS_LOG_UNCOND("[L3][QDISC] utUser nodeId=" << node->GetId()
                                                 << " dev=" << dev->GetInstanceTypeId().GetName()
                                                 << " root=" << root->GetInstanceTypeId().GetName());
      }
      else
      {
        NS_LOG_UNCOND("[L3][QDISC] utUser nodeId=" << node->GetId()
                                                 << " dev=" << dev->GetInstanceTypeId().GetName()
                                                 << " root=NULL");
      }
    }
  }

  // Total is 3*6 = 18 flows (depends on scenario settings)
  Ptr<SatTrafficHelper> trafficHelper = simulationHelper->GetTrafficHelper();

  trafficHelper->AddCbrTraffic(SatTrafficHelper::FWD_LINK,
                               SatTrafficHelper::UDP,
                               intervalT,
                               packetSize,
                               gwUsers,
                               utUsers,
                               startTime,
                               stopTime,
                               startDelay);

  trafficHelper->AddCbrTraffic(SatTrafficHelper::RTN_LINK,
                               SatTrafficHelper::UDP,
                               intervalT,
                               packetSize,
                               gwUsers,
                               utUsers,
                               startTime,
                               stopTime,
                               startDelay);

  NS_LOG_INFO("--- sat-constellation-a1trace-example ---");
  NS_LOG_INFO("  PacketSize: " << packetSize);
  NS_LOG_INFO("  Interval: " << interval);
  NS_LOG_INFO("  Scenario: " << scenarioFolder);
  NS_LOG_INFO("  SimTime: " << simTime);

  // =============================================
  // Store attributes
  // =============================================
  Config::SetDefault("ns3::ConfigStore::Filename", StringValue("output-attributes.xml"));
  Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("Xml"));
  Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
  ConfigStore outputConfig;
  outputConfig.ConfigureDefaults();

  // =============================================
  // Statistics (original example)
  // =============================================
  Ptr<SatStatsHelperContainer> s = simulationHelper->GetStatisticsContainer();

  s->AddPerUtFwdFeederPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserPhyThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserMacThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserDevThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddGlobalFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddGlobalRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerGwFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerGwRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerSatFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerSatRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerBeamFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerBeamRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnAppThroughput(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdPhyDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdMacDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdDevDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnPhyDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnMacDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnDevDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserPhyLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserMacLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserDevLinkDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdPhyJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdMacJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdDevJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnPhyJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnMacJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnDevJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserPhyLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserMacLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserDevLinkJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserLinkModcod(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerGwRtnFeederQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerSatRtnFeederQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerSatRtnFeederQueuePackets(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerGwFwdUserQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerSatFwdUserQueueBytes(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerSatFwdUserQueuePackets(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddGlobalPacketDropRate(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerIslPacketDropRate(SatStatsHelper::OUTPUT_SCATTER_FILE);

  // =============================================
  // Run
  // =============================================
  simulationHelper->EnableProgressLogs();
  simulationHelper->RunSimulation();

  return 0;
}
