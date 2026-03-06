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
 *
 */

#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"

#include <fstream>
#include <memory>
#include <set>
#include <string>

// -------------------------
// D1-2 helper: dump candidate_sats.csv while simulation runs (keep original behavior)
// -------------------------
class TwCandidateSampler
{
public:
  TwCandidateSampler(std::string outCsv,
                     double tStart, double tEnd, double dt,
                     double latMin, double latMax,
                     double lonMin, double lonMax)
    : m_outCsv(std::move(outCsv)),
      m_tStart(tStart), m_tEnd(tEnd), m_dt(dt),
      m_latMin(latMin), m_latMax(latMax),
      m_lonMin(lonMin), m_lonMax(lonMax)
  {}

  void Init()
  {
    m_of.open(m_outCsv, std::ios::out | std::ios::trunc);
    m_of << "time,satId,in_tw\n";
  }

  void Start()
  {
    ns3::Simulator::Schedule(ns3::Seconds(m_tStart), &TwCandidateSampler::Tick, this);
  }

  void Close()
  {
    if (m_of.is_open())
    {
      m_of.close();
    }
  }

private:
  bool InTw(double lat, double lon) const
  {
    return (lat >= m_latMin && lat <= m_latMax && lon >= m_lonMin && lon <= m_lonMax);
  }

  void Tick()
  {
    double now = ns3::Simulator::Now().GetSeconds();
    if (now > m_tEnd + 1e-9)
    {
      return;
    }

    ns3::NodeContainer sats = ns3::Singleton<ns3::SatTopology>::Get()->GetOrbiterNodes();
    uint32_t n = sats.GetN();

    for (uint32_t satId = 0; satId < n; ++satId)
    {
      ns3::Ptr<ns3::Node> node = sats.Get(satId);
      ns3::Ptr<ns3::SatMobilityModel> mm = node->GetObject<ns3::SatMobilityModel>();

      int in_tw = 0;
      if (mm)
      {
        ns3::GeoCoordinate g = mm->GetGeoPosition();
        double lat = g.GetLatitude();
        double lon = g.GetLongitude();
        in_tw = InTw(lat, lon) ? 1 : 0;
      }

      m_of << now << "," << satId << "," << in_tw << "\n";
    }

    ns3::Simulator::Schedule(ns3::Seconds(m_dt), &TwCandidateSampler::Tick, this);
  }

private:
  std::string m_outCsv;
  double m_tStart, m_tEnd, m_dt;
  double m_latMin, m_latMax, m_lonMin, m_lonMax;
  std::ofstream m_of;
};

using namespace ns3;

/**
 * \file sat-constellation-example.cc
 * \ingroup satellite
 *
 * \brief This file gives an example of satellite constellations.
 *        User must choose which constellation to simulate from all available in
 *        satellite/data/scenarios.
 */

NS_LOG_COMPONENT_DEFINE("sat-constellation-example");

int
main(int argc, char* argv[])
{
  uint32_t packetSize = 512;
  std::string interval = "100ms";
  std::string scenarioFolder = "constellation-telesat-351-sats";

  Ptr<SimulationHelper> simulationHelper =
      CreateObject<SimulationHelper>("example-constellation");

  // --- D1-2 options (keep original behavior; only dumps CSV when mode=tw_candidates) ---
  std::string mode = "";    // "" means normal
  double tStart = 0.0;
  double tEnd = 300.0;
  double dt = 1.0;
  std::string outCsv = "";  // default will be OutputPath/candidate_sats.csv

  // Taiwan bbox (approx)
  double twLatMin = 21.5, twLatMax = 25.7;
  double twLonMin = 119.0, twLonMax = 122.5;

  // read command line parameters given by user
  CommandLine cmd;
  cmd.AddValue("packetSize", "Size of constant packet (bytes)", packetSize);
  cmd.AddValue("interval", "Interval to sent packets in seconds (e.g. (1s))", interval);
  cmd.AddValue("scenarioFolder",
               "Scenario folder (e.g. constellation-eutelsat-geo-2-sats-isls)",
               scenarioFolder);

  // D1-2 CLI
  cmd.AddValue("mode", "Optional mode: tw_candidates (dump candidate_sats.csv while keeping normal run)", mode);
  cmd.AddValue("tStart", "D1-2 sampling start time (seconds)", tStart);
  cmd.AddValue("tEnd",   "D1-2 sampling end time (seconds)", tEnd);
  cmd.AddValue("dt",     "D1-2 sampling period (seconds)", dt);
  cmd.AddValue("outCsv", "D1-2 output CSV path (default: OutputPath/candidate_sats.csv)", outCsv);
  cmd.AddValue("twLatMin", "TW bbox lat min", twLatMin);
  cmd.AddValue("twLatMax", "TW bbox lat max", twLatMax);
  cmd.AddValue("twLonMin", "TW bbox lon min", twLonMin);
  cmd.AddValue("twLonMax", "TW bbox lon max", twLonMax);

  simulationHelper->AddDefaultUiArguments(cmd);
  cmd.Parse(argc, argv);

  // After parsing, OutputPath is known via SatEnvVariables.
  std::string outputPath = Singleton<SatEnvVariables>::Get()->GetOutputPath();
  if (outCsv.empty())
  {
    outCsv = outputPath + "/candidate_sats.csv";
  }

  /// Set regeneration mode
  Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));
  Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));
  Config::SetDefault("ns3::SatOrbiterFeederPhy::QueueSize", UintegerValue(100000));
  Config::SetDefault("ns3::SatOrbiterUserPhy::QueueSize", UintegerValue(100000));

  /// Use constellationF
  Config::SetDefault("ns3::PointToPointIslHelper::IslDataRate",
                     DataRateValue(DataRate("100Mb/s")));
  Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionEachRequest", BooleanValue(false));
  Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionPeriod", TimeValue(Seconds(1)));
  Config::SetDefault("ns3::SatHelper::GwUsers", UintegerValue(3));
  Config::SetDefault("ns3::SatGwMac::SendNcrBroadcast", BooleanValue(false));

  /// When using 72 beams, we need a 72*nbSats network addresses for beams, so we take margin
  Config::SetDefault("ns3::SatHelper::BeamNetworkAddress", Ipv4AddressValue("20.1.0.0"));
  Config::SetDefault("ns3::SatHelper::GwNetworkAddress", Ipv4AddressValue("10.1.0.0"));
  Config::SetDefault("ns3::SatHelper::UtNetworkAddress", Ipv4AddressValue("250.1.0.0"));

  /// Enable ACM
  Config::SetDefault("ns3::SatBbFrameConf::AcmEnabled", BooleanValue(true));

  /// Set simulation output details
  Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));

  /// Enable packet trace
  Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(true));

  simulationHelper->LoadScenario(scenarioFolder);

  simulationHelper->SetSimulationTime(Seconds(5));

  std::set<uint32_t> beamSetAll = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                                   16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                   31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
                                   46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
                                   61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72};

  std::set<uint32_t> beamSet = {43, 30};

  std::set<uint32_t> beamSetTelesat = {43}; // assume beam 43 covers target UT in telesat scenario

  // Set beam ID
  if (scenarioFolder == "constellation-telesat-351-sats")
  {
    simulationHelper->SetBeamSet(beamSetTelesat);
  }
  else
  {
    simulationHelper->SetBeamSet(beamSet);
  }
  simulationHelper->SetUserCountPerUt(2);

  LogComponentEnable("sat-constellation-example", LOG_LEVEL_INFO);

  simulationHelper->CreateSatScenario();

  // --- D1-2: TW candidate sats dump (keep original behavior) ---
  std::unique_ptr<TwCandidateSampler> twSampler;
  if (mode == "tw_candidates")
  {
    // ensure SGP4 positions update with dt (does not disable other functionalities)
    Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionEachRequest", BooleanValue(false));
    Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionPeriod", TimeValue(Seconds(dt)));

    twSampler.reset(new TwCandidateSampler(outCsv, tStart, tEnd, dt, twLatMin, twLatMax, twLonMin, twLonMax));
    twSampler->Init();
    twSampler->Start();

    std::cout << "[D1-2] tw_candidates enabled\n"
              << "  OutputPath=" << outputPath << "\n"
              << "  outCsv=" << outCsv << "\n"
              << "  tStart=" << tStart << " tEnd=" << tEnd << " dt=" << dt << "\n"
              << "  bbox lat=[" << twLatMin << "," << twLatMax << "] lon=[" << twLonMin << "," << twLonMax << "]\n";
  }

  Singleton<SatTopology>::Get()->PrintTopology(std::cout);
  Singleton<SatIdMapper>::Get()->ShowIslMap();

  Config::SetDefault("ns3::CbrApplication::Interval", StringValue(interval));
  Config::SetDefault("ns3::CbrApplication::PacketSize", UintegerValue(packetSize));

  Time startTime = Seconds(1.0);
  Time stopTime = Seconds(29.0);
  Time startDelay = Seconds(0.0);

  NodeContainer gws = Singleton<SatTopology>::Get()->GetGwNodes();
  NodeContainer uts = Singleton<SatTopology>::Get()->GetUtNodes();
  NodeContainer gwUsers = Singleton<SatTopology>::Get()->GetGwUserNodes();
  NodeContainer utUsers = Singleton<SatTopology>::Get()->GetUtUserNodes(uts);

  // Total is 3*6 = 18 flows
  // Global App rate is pktSize*ptkPerSecond*nbFlows = 512*8*50*18 = 3686kb/s on both FWD and RTN
  Ptr<SatTrafficHelper> trafficHelper = simulationHelper->GetTrafficHelper();

  trafficHelper->AddCbrTraffic(SatTrafficHelper::FWD_LINK,
                               SatTrafficHelper::UDP,
                               Time(interval),
                               packetSize,
                               gwUsers,
                               utUsers,
                               startTime,
                               stopTime,
                               startDelay);

  trafficHelper->AddCbrTraffic(SatTrafficHelper::RTN_LINK,
                               SatTrafficHelper::UDP,
                               Time(interval),
                               packetSize,
                               gwUsers,
                               utUsers,
                               startTime,
                               stopTime,
                               startDelay);

  NS_LOG_INFO("--- sat-constellation-example ---");
  NS_LOG_INFO("  PacketSize: " << packetSize);
  NS_LOG_INFO("  Interval: " << interval);
  NS_LOG_INFO("  ");

  // To store attributes to file
  Config::SetDefault("ns3::ConfigStore::Filename", StringValue("output-attributes.xml"));
  Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("Xml"));
  Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
  ConfigStore outputConfig;
  outputConfig.ConfigureDefaults();

  Ptr<SatStatsHelperContainer> s = simulationHelper->GetStatisticsContainer();

  // Throughput statistics
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

  // Delay statistics
  s->AddPerUtFwdPhyDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdMacDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdDevDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnPhyDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnMacDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnDevDelay(SatStatsHelper::OUTPUT_SCATTER_FILE);

  // link delay statistics
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

  // Jitter statistics
  s->AddPerUtFwdPhyJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdMacJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdDevJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnPhyJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnMacJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnDevJitter(SatStatsHelper::OUTPUT_SCATTER_FILE);

  // Link jitter statistics
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

  // Phy RX statistics
  s->AddPerUtFwdFeederLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserLinkSinr(SatStatsHelper::OUTPUT_SCATTER_FILE);

  s->AddPerUtFwdFeederLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtFwdUserLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnFeederLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerUtRtnUserLinkRxPower(SatStatsHelper::OUTPUT_SCATTER_FILE);

  // Other statistics
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

  // ISL drop rate statistics
  s->AddGlobalPacketDropRate(SatStatsHelper::OUTPUT_SCATTER_FILE);
  s->AddPerIslPacketDropRate(SatStatsHelper::OUTPUT_SCATTER_FILE);

  simulationHelper->EnableProgressLogs();
  simulationHelper->RunSimulation();

  // D1-2 close file after run
  if (twSampler)
  {
    twSampler->Close();
  }

  simulationHelper->Dispose();
  return 0;
}
