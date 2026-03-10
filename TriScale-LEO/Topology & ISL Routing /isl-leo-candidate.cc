#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"

#include "ns3/satellite-module.h"
#include "ns3/simulation-helper.h"
#include "ns3/satellite-topology.h"
#include "ns3/satellite-mobility-model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ns3;

namespace
{

struct AppConfig
{
  std::string mode = "d1_final";
  std::string scenarioFolder = "constellation-telesat-351-sats";
  std::string statsLevel = "min";
  std::string outDir = "./outputs";
  std::string pcapDir = "./pcap";
  double simTime = 300.0;
  double tStart = 0.0;
  double tEnd = 300.0;
  double dt = 1.0;
  double planWindow = 60.0;
  double refLat = 25.0330;
  double refLon = 121.5654;
  double elevDeg = 20.0;
  uint32_t gwIndex = 0;
  bool enablePcap = true;
  uint32_t satA = 0;
  uint32_t satB = 1;
};

struct CandidateRow
{
  double time = 0.0;
  uint32_t satId = 0;
  double elevRefDeg = 0.0;
  bool islToGw = false;
  uint32_t pathHops = 0;
  bool candidate = false;
};

struct IslRow
{
  double time = 0.0;
  uint32_t satA = 0;
  uint32_t satB = 0;
  double distanceKm = 0.0;
  bool active = false;
};

struct PlanRow
{
  double timeStart = 0.0;
  double timeEnd = 0.0;
  int32_t servingSat = -1;
  std::string path;
  int32_t hopCount = -1;
  std::string status = "NO_PATH";
  uint32_t gwIndex = 0;
  std::string reason;
};

using IslAdjMap = std::map<uint32_t, std::vector<uint32_t>>;

struct GraphCache
{
  IslAdjMap adj;
  std::set<uint32_t> gwAnchorSats;
};

std::string
Bool01(bool v)
{
  return v ? "1" : "0";
}

void
EnsureDir(const std::string& path)
{
  std::string cmd = "mkdir -p '" + path + "'";
  std::ignore = std::system(cmd.c_str());
}

std::string
JoinPath(const std::string& a, const std::string& b)
{
  if (a.empty())
    {
      return b;
    }
  if (a.back() == '/')
    {
      return a + b;
    }
  return a + "/" + b;
}

void
WriteCandidateCsv(const std::string& file, const std::vector<CandidateRow>& rows)
{
  std::ofstream os(file);
  os << "time,satId,elev_ref_deg,isl_to_gw,path_hops,candidate\n";
  os << std::fixed << std::setprecision(3);
  for (const auto& r : rows)
    {
      os << r.time << ','
         << r.satId << ','
         << r.elevRefDeg << ','
         << Bool01(r.islToGw) << ','
         << r.pathHops << ','
         << Bool01(r.candidate) << '\n';
    }
}

void
WriteIslCsv(const std::string& file, const std::vector<IslRow>& rows)
{
  std::ofstream os(file);
  os << "time,satA,satB,distance_km,active\n";
  os << std::fixed << std::setprecision(3);
  for (const auto& r : rows)
    {
      os << r.time << ','
         << r.satA << ','
         << r.satB << ','
         << r.distanceKm << ','
         << Bool01(r.active) << '\n';
    }
}

void
WritePlanCsv(const std::string& file, const std::vector<PlanRow>& rows)
{
  std::ofstream os(file);
  os << "time_start,time_end,serving_sat,path,hop_count,status,gw_index,reason\n";
  os << std::fixed << std::setprecision(3);
  for (const auto& r : rows)
    {
      os << r.timeStart << ','
         << r.timeEnd << ','
         << r.servingSat << ','
         << r.path << ','
         << r.hopCount << ','
         << r.status << ','
         << r.gwIndex << ','
         << r.reason << '\n';
    }
}

void
WriteText(const std::string& file, const std::string& content)
{
  std::ofstream os(file);
  os << content;
}

void
WriteSummaryJson(const std::string& file,
                 const AppConfig& cfg,
                 const std::vector<std::string>& generatedFiles,
                 const std::string& gwInfo)
{
  std::ofstream os(file);
  os << "{\n";
  os << "  \"event_name\": \"E20260305_ISL_BH\",\n";
  os << "  \"mode\": \"" << cfg.mode << "\",\n";
  os << "  \"scenario_folder\": \"" << cfg.scenarioFolder << "\",\n";
  os << "  \"simTime\": " << cfg.simTime << ",\n";
  os << "  \"tStart\": " << cfg.tStart << ",\n";
  os << "  \"tEnd\": " << cfg.tEnd << ",\n";
  os << "  \"dt\": " << cfg.dt << ",\n";
  os << "  \"planWindow\": " << cfg.planWindow << ",\n";
  os << "  \"refLat\": " << cfg.refLat << ",\n";
  os << "  \"refLon\": " << cfg.refLon << ",\n";
  os << "  \"elevDeg\": " << cfg.elevDeg << ",\n";
  os << "  \"gwIndex\": " << cfg.gwIndex << ",\n";
  os << "  \"gwInfo\": \"" << gwInfo << "\",\n";
  os << "  \"statsLevel\": \"" << cfg.statsLevel << "\",\n";
  os << "  \"generated_files\": [\n";
  for (size_t i = 0; i < generatedFiles.size(); ++i)
    {
      os << "    \"" << generatedFiles[i] << "\"";
      os << (i + 1 < generatedFiles.size() ? ",\n" : "\n");
    }
  os << "  ]\n";
  os << "}\n";
}

std::vector<uint32_t>
GetScenarioSatelliteIds(const AppConfig&)
{
  std::vector<uint32_t> satIds;

  uint32_t n = Singleton<SatTopology>::Get()->GetNOrbiterNodes();
  satIds.reserve(n);

  for (uint32_t satId = 0; satId < n; ++satId)
    {
      Ptr<Node> satNode = Singleton<SatTopology>::Get()->GetOrbiterNode(satId);
      if (satNode != nullptr)
        {
          satIds.push_back(satId);
        }
    }

  std::cout << "[D1] real orbiter count = " << satIds.size() << std::endl;
  return satIds;
}

std::vector<uint32_t>
GetScenarioGatewayIds(const AppConfig&)
{
  std::vector<uint32_t> gwIds;
  NodeContainer gws = Singleton<SatTopology>::Get()->GetGwNodes();

  for (uint32_t i = 0; i < gws.GetN(); ++i)
    {
      if (gws.Get(i) != nullptr)
        {
          gwIds.push_back(i);
        }
    }

  std::cout << "[D1] real gw count = " << gwIds.size() << std::endl;
  return gwIds;
}

double
DegToRad(double deg)
{
  return deg * M_PI / 180.0;
}

double
RadToDeg(double rad)
{
  return rad * 180.0 / M_PI;
}

Vector
GeoToEcef(double latDeg, double lonDeg, double altM)
{
  const double a = 6378137.0;
  const double e2 = 6.69437999014e-3;

  double lat = DegToRad(latDeg);
  double lon = DegToRad(lonDeg);

  double sinLat = std::sin(lat);
  double cosLat = std::cos(lat);
  double sinLon = std::sin(lon);
  double cosLon = std::cos(lon);

  double N = a / std::sqrt(1.0 - e2 * sinLat * sinLat);

  double x = (N + altM) * cosLat * cosLon;
  double y = (N + altM) * cosLat * sinLon;
  double z = (N * (1.0 - e2) + altM) * sinLat;

  return Vector(x, y, z);
}

double
DistanceMeters(const Vector& a, const Vector& b)
{
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  const double dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::map<uint32_t, uint32_t>
BuildNodeIdToSatIdMap()
{
  std::map<uint32_t, uint32_t> m;
  uint32_t n = Singleton<SatTopology>::Get()->GetNOrbiterNodes();

  for (uint32_t satId = 0; satId < n; ++satId)
    {
      Ptr<Node> satNode = Singleton<SatTopology>::Get()->GetOrbiterNode(satId);
      if (satNode != nullptr)
        {
          m[satNode->GetId()] = satId;
        }
    }
  return m;
}

bool
HasEdge(const IslAdjMap& adj, uint32_t a, uint32_t b)
{
  auto it = adj.find(a);
  if (it == adj.end())
    {
      return false;
    }
  return std::find(it->second.begin(), it->second.end(), b) != it->second.end();
}

std::vector<Ptr<NetDevice>>
GetPeerDevices(Ptr<NetDevice> dev)
{
  std::vector<Ptr<NetDevice>> peers;
  if (dev == nullptr)
    {
      return peers;
    }

  Ptr<Channel> ch = dev->GetChannel();
  if (ch == nullptr)
    {
      return peers;
    }

  for (uint32_t i = 0; i < ch->GetNDevices(); ++i)
    {
      Ptr<NetDevice> other = ch->GetDevice(i);
      if (other != nullptr && other != dev)
        {
          peers.push_back(other);
        }
    }
  return peers;
}

IslAdjMap
BuildRealIslAdjacency()
{
  IslAdjMap adj;
  auto nodeIdToSatId = BuildNodeIdToSatIdMap();

  uint32_t n = Singleton<SatTopology>::Get()->GetNOrbiterNodes();
  for (uint32_t satId = 0; satId < n; ++satId)
    {
      Ptr<Node> satNode = Singleton<SatTopology>::Get()->GetOrbiterNode(satId);
      if (satNode == nullptr)
        {
          continue;
        }

      for (uint32_t d = 0; d < satNode->GetNDevices(); ++d)
        {
          Ptr<NetDevice> dev = satNode->GetDevice(d);
          if (dev == nullptr)
            {
              continue;
            }

          auto peers = GetPeerDevices(dev);
          for (const auto& peerDev : peers)
            {
              Ptr<Node> peerNode = peerDev ? peerDev->GetNode() : nullptr;
              if (peerNode == nullptr)
                {
                  continue;
                }

              auto it = nodeIdToSatId.find(peerNode->GetId());
              if (it == nodeIdToSatId.end())
                {
                  continue;
                }

              uint32_t peerSatId = it->second;
              if (peerSatId == satId)
                {
                  continue;
                }

              adj[satId].push_back(peerSatId);
            }
        }
    }

  for (auto& kv : adj)
    {
      auto& nbrs = kv.second;
      std::sort(nbrs.begin(), nbrs.end());
      nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
    }

  for (const auto& kv : adj)
    {
      uint32_t u = kv.first;
      for (uint32_t v : kv.second)
        {
          if (!HasEdge(adj, v, u))
            {
              adj[v].push_back(u);
            }
        }
    }

  for (auto& kv : adj)
    {
      auto& nbrs = kv.second;
      std::sort(nbrs.begin(), nbrs.end());
      nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
    }

  
  return adj;
}

std::set<uint32_t>
GetGatewayAnchorSatIds(const AppConfig& cfg)
{
  std::set<uint32_t> anchors;
  NodeContainer gws = Singleton<SatTopology>::Get()->GetGwNodes();

  if (gws.GetN() == 0)
    {
      
      return anchors;
    }

  if (cfg.gwIndex >= gws.GetN())
    {
      
      return anchors;
    }

  Ptr<Node> gw = gws.Get(cfg.gwIndex);
  if (gw == nullptr)
    {
      
      return anchors;
    }

  Ptr<SatMobilityModel> gwMob = gw->GetObject<SatMobilityModel>();
  if (gwMob == nullptr)
    {
      
      return anchors;
    }

  GeoCoordinate gwPos = gwMob->GetGeoPosition();
  uint32_t satId = Singleton<SatTopology>::Get()->GetClosestSat(gwPos);
  anchors.insert(satId);

   return anchors;
}

GraphCache
BuildGraphCache(const AppConfig& cfg)
{
  GraphCache cache;
  cache.adj = BuildRealIslAdjacency();
  cache.gwAnchorSats = GetGatewayAnchorSatIds(cfg);
  return cache;
}

std::string
SatPathToString(uint32_t srcSat, const std::vector<uint32_t>& satPath, uint32_t gwIndex)
{
  std::ostringstream oss;
  oss << "UT->SAT" << srcSat;

  for (size_t i = 1; i < satPath.size(); ++i)
    {
      oss << "->SAT" << satPath[i];
    }

  oss << "->GW" << gwIndex;
  return oss.str();
}

bool
FindShortestPath(const IslAdjMap& adj,
                 uint32_t srcSat,
                 const std::set<uint32_t>& dstSats,
                 std::vector<uint32_t>& outPath)
{
  std::queue<uint32_t> q;
  std::map<uint32_t, int32_t> parent;
  std::set<uint32_t> visited;

  q.push(srcSat);
  visited.insert(srcSat);
  parent[srcSat] = -1;

  int32_t found = -1;

  while (!q.empty())
    {
      uint32_t u = q.front();
      q.pop();

      if (dstSats.count(u))
        {
          found = static_cast<int32_t>(u);
          break;
        }

      auto it = adj.find(u);
      if (it == adj.end())
        {
          continue;
        }

      for (uint32_t v : it->second)
        {
          if (!visited.count(v))
            {
              visited.insert(v);
              parent[v] = static_cast<int32_t>(u);
              q.push(v);
            }
        }
    }

  if (found < 0)
    {
      return false;
    }

  outPath.clear();
  for (int32_t cur = found; cur != -1; cur = parent[cur])
    {
      outPath.push_back(static_cast<uint32_t>(cur));
    }
  std::reverse(outPath.begin(), outPath.end());
  return true;
}

double
GetElevationDegToReference(const AppConfig& cfg, uint32_t satId, double /*tSec*/)
{
  Ptr<Node> satNode = Singleton<SatTopology>::Get()->GetOrbiterNode(satId);
  if (satNode == nullptr)
    {
      return -90.0;
    }

  Ptr<SatMobilityModel> mob = satNode->GetObject<SatMobilityModel>();
  if (mob == nullptr)
    {
      return -90.0;
    }

  GeoCoordinate satGeo = mob->GetGeoPosition();

  const double gsLatDeg = cfg.refLat;
  const double gsLonDeg = cfg.refLon;
  const double gsAltM = 0.0;

  Vector gsEcef = GeoToEcef(gsLatDeg, gsLonDeg, gsAltM);
  Vector satEcef = GeoToEcef(satGeo.GetLatitude(),
                             satGeo.GetLongitude(),
                             satGeo.GetAltitude());

  Vector los(satEcef.x - gsEcef.x,
             satEcef.y - gsEcef.y,
             satEcef.z - gsEcef.z);

  double lat = DegToRad(gsLatDeg);
  double lon = DegToRad(gsLonDeg);

  Vector up(std::cos(lat) * std::cos(lon),
            std::cos(lat) * std::sin(lon),
            std::sin(lat));

  double losNorm = std::sqrt(los.x * los.x + los.y * los.y + los.z * los.z);
  if (losNorm <= 0.0)
    {
      return -90.0;
    }

  double dotUp = los.x * up.x + los.y * up.y + los.z * up.z;
  double elevRad = std::asin(dotUp / losNorm);

  return RadToDeg(elevRad);
}

bool
PathExistsToGateway(const AppConfig& cfg,
                    const GraphCache& cache,
                    uint32_t satId,
                    uint32_t /*gwId*/,
                    double /*tSec*/,
                    uint32_t& hops,
                    std::string& path)
{
  if (cache.gwAnchorSats.empty())
    {
      hops = 0;
      path.clear();
      return false;
    }

  std::vector<uint32_t> satPath;
  bool ok = FindShortestPath(cache.adj, satId, cache.gwAnchorSats, satPath);
  if (!ok)
    {
      hops = 0;
      path.clear();
      return false;
    }

  hops = (satPath.size() >= 2) ? static_cast<uint32_t>(satPath.size() - 1) : 0;
  path = SatPathToString(satId, satPath, cfg.gwIndex);
  return true;
}

std::vector<IslRow>
EvaluateIslConnectivity(const AppConfig& cfg,
                        const GraphCache& cache,
                        double tSec,
                        const std::vector<uint32_t>& satIds)
{
  std::vector<IslRow> rows;
  

  for (size_t i = 0; i < satIds.size(); ++i)
    {
      Ptr<Node> satNodeA = Singleton<SatTopology>::Get()->GetOrbiterNode(satIds[i]);
      if (satNodeA == nullptr)
        {
          continue;
        }

      Ptr<SatMobilityModel> mobA = satNodeA->GetObject<SatMobilityModel>();
      if (mobA == nullptr)
        {
          continue;
        }

      GeoCoordinate geoA = mobA->GetGeoPosition();
      Vector ecefA = GeoToEcef(geoA.GetLatitude(),
                               geoA.GetLongitude(),
                               geoA.GetAltitude());

      for (size_t j = i + 1; j < satIds.size(); ++j)
        {
          Ptr<Node> satNodeB = Singleton<SatTopology>::Get()->GetOrbiterNode(satIds[j]);
          if (satNodeB == nullptr)
            {
              continue;
            }

          Ptr<SatMobilityModel> mobB = satNodeB->GetObject<SatMobilityModel>();
          if (mobB == nullptr)
            {
              continue;
            }

          GeoCoordinate geoB = mobB->GetGeoPosition();
          Vector ecefB = GeoToEcef(geoB.GetLatitude(),
                                   geoB.GetLongitude(),
                                   geoB.GetAltitude());

          IslRow row;
          row.time = tSec;
          row.satA = satIds[i];
          row.satB = satIds[j];
          row.distanceKm = DistanceMeters(ecefA, ecefB) / 1000.0;
          row.active = HasEdge(cache.adj, row.satA, row.satB);
          rows.push_back(row);
        }
    }

  if (cfg.statsLevel == "min" && rows.size() > 256)
    {
      rows.resize(256);
    }

  return rows;
}

std::string
ResolveGwInfo(const AppConfig& cfg)
{
  auto gwIds = GetScenarioGatewayIds(cfg);
  if (gwIds.empty() || cfg.gwIndex >= gwIds.size())
    {
      return "GW_UNAVAILABLE";
    }

  std::ostringstream oss;
  oss << "GW" << gwIds[cfg.gwIndex] << "(index=" << cfg.gwIndex << ")";
  return oss.str();
}

std::vector<CandidateRow>
RunCandidateScan(const AppConfig& cfg, const GraphCache& cache)
{
  const auto satIds = GetScenarioSatelliteIds(cfg);
  const auto gwIds = GetScenarioGatewayIds(cfg);
  std::vector<CandidateRow> rows;

  if (gwIds.empty() || cfg.gwIndex >= gwIds.size())
    {
      NS_FATAL_ERROR("No valid GW available for gwIndex=" << cfg.gwIndex);
    }

  const uint32_t gwId = gwIds[cfg.gwIndex];

  for (double t = cfg.tStart; t <= cfg.tEnd + 1e-9; t += cfg.dt)
    {
      for (uint32_t satId : satIds)
        {
          CandidateRow row;
          row.time = t;
          row.satId = satId;
          row.elevRefDeg = GetElevationDegToReference(cfg, satId, t);

          std::string path;
          row.islToGw = PathExistsToGateway(cfg, cache, satId, gwId, t, row.pathHops, path);
          row.candidate = (row.elevRefDeg >= cfg.elevDeg) && row.islToGw;
          rows.push_back(row);
        }
    }

  return rows;
}

std::vector<PlanRow>
BuildRoutingPlan(const AppConfig& cfg,
                 const GraphCache& cache,
                 const std::vector<CandidateRow>& candidates)
{
  std::map<double, std::vector<CandidateRow>> byTime;
  for (const auto& row : candidates)
    {
      byTime[row.time].push_back(row);
    }

  const auto gwIds = GetScenarioGatewayIds(cfg);
  if (gwIds.empty() || cfg.gwIndex >= gwIds.size())
    {
      NS_FATAL_ERROR("No valid GW available for gwIndex=" << cfg.gwIndex);
    }
  const uint32_t gwId = gwIds[cfg.gwIndex];

  struct SatScore
  {
    double maxElev = -1e9;
    uint32_t minHops = std::numeric_limits<uint32_t>::max();
  };

  std::vector<PlanRow> plan;
  for (double ws = cfg.tStart; ws < cfg.tEnd; ws += cfg.planWindow)
    {
      const double we = std::min(ws + cfg.planWindow, cfg.tEnd);
      std::map<uint32_t, SatScore> score;

      for (const auto& kv : byTime)
        {
          if (kv.first < ws || kv.first >= we)
            {
              continue;
            }

          for (const auto& row : kv.second)
            {
              if (!row.candidate)
                {
                  continue;
                }

              auto& s = score[row.satId];
              s.maxElev = std::max(s.maxElev, row.elevRefDeg);
              s.minHops = std::min(s.minHops, row.pathHops);
            }
        }

      PlanRow pr;
      pr.timeStart = ws;
      pr.timeEnd = we;
      pr.gwIndex = cfg.gwIndex;

      if (score.empty())
        {
          pr.servingSat = -1;
          pr.path = "";
          pr.hopCount = -1;
          pr.status = "NO_PATH";
          pr.reason = "no_candidate_in_window";
          plan.push_back(pr);
          continue;
        }

      auto best = std::min_element(
          score.begin(),
          score.end(),
          [](const auto& a, const auto& b) {
            if (a.second.maxElev != b.second.maxElev)
              {
                return a.second.maxElev > b.second.maxElev;
              }
            return a.second.minHops < b.second.minHops;
          });

      uint32_t bestSat = best->first;
      uint32_t hops = 0;
      std::string path;
      bool ok = PathExistsToGateway(cfg, cache, bestSat, gwId, ws, hops, path);

      if (!ok)
        {
          pr.servingSat = -1;
          pr.path = "";
          pr.hopCount = -1;
          pr.status = "NO_PATH";
          pr.reason = "best_sat_lost_path";
          plan.push_back(pr);
          continue;
        }

      pr.servingSat = static_cast<int32_t>(bestSat);
      pr.path = path;
      pr.hopCount = static_cast<int32_t>(hops);
      pr.status = "OK";
      pr.reason = "best_elevation_then_min_hops";
      plan.push_back(pr);
    }

  return plan;
}

void
RunVerifyPath(const AppConfig& cfg)
{
  EnsureDir(cfg.outDir);
  EnsureDir(cfg.pcapDir);

  std::ostringstream routeDump;
  routeDump << "verify_path\n";
  routeDump << "satA=" << cfg.satA << " satB=" << cfg.satB << '\n';
  routeDump << "expected_path=UT->SAT" << cfg.satA << "->SAT" << cfg.satB << "->GW" << cfg.gwIndex
            << '\n';

  std::ostringstream fwdLog;
  fwdLog << "[VERIFY] forwarding placeholder evidence\n";
  fwdLog << "[VERIFY] TODO bind real ns-3 routing / pcap instrumentation\n";

  WriteText(JoinPath(cfg.outDir, "route_dump.txt"), routeDump.str());
  WriteText(JoinPath(cfg.outDir, "forward_path.log"), fwdLog.str());

  if (cfg.enablePcap)
    {
      WriteText(JoinPath(cfg.pcapDir, "README.txt"),
                "TODO: attach real point-to-point/net-device pcap traces in verify_path mode.\n");
    }
}

void
ParseArgs(int argc, char** argv, AppConfig& cfg)
{
  CommandLine cmd(__FILE__);
  cmd.AddValue("mode", "verify_path | candidate_scan | isl_connectivity | plan | d1_final", cfg.mode);
  cmd.AddValue("scenarioFolder", "Scenario folder", cfg.scenarioFolder);
  cmd.AddValue("simTime", "Simulation time", cfg.simTime);
  cmd.AddValue("tStart", "Candidate scan start", cfg.tStart);
  cmd.AddValue("tEnd", "Candidate scan end", cfg.tEnd);
  cmd.AddValue("dt", "Sampling step", cfg.dt);
  cmd.AddValue("planWindow", "Planning window", cfg.planWindow);
  cmd.AddValue("refLat", "Reference latitude", cfg.refLat);
  cmd.AddValue("refLon", "Reference longitude", cfg.refLon);
  cmd.AddValue("elevDeg", "Elevation threshold", cfg.elevDeg);
  cmd.AddValue("gwIndex", "Gateway index", cfg.gwIndex);
  cmd.AddValue("statsLevel", "min | full", cfg.statsLevel);
  cmd.AddValue("outDir", "Output directory", cfg.outDir);
  cmd.AddValue("pcapDir", "PCAP directory", cfg.pcapDir);
  cmd.AddValue("enablePcap", "Enable pcap evidence", cfg.enablePcap);
  cmd.AddValue("satA", "verify_path satA", cfg.satA);
  cmd.AddValue("satB", "verify_path satB", cfg.satB);
  cmd.Parse(argc, argv);
}

} // namespace

int
main(int argc, char** argv)
{
  AppConfig cfg;
  ParseArgs(argc, argv, cfg);

  Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));
  Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));

  Config::SetDefault("ns3::SatHelper::BeamNetworkAddress", Ipv4AddressValue("20.1.0.0"));
  Config::SetDefault("ns3::SatHelper::GwNetworkAddress", Ipv4AddressValue("10.1.0.0"));
  Config::SetDefault("ns3::SatHelper::UtNetworkAddress", Ipv4AddressValue("250.1.0.0"));

  Config::SetDefault("ns3::SatBbFrameConf::AcmEnabled", BooleanValue(true));
  Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));
  Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(false));

  Ptr<SimulationHelper> simulationHelper = CreateObject<SimulationHelper>("isl-leo-candidate");
  simulationHelper->LoadScenario(cfg.scenarioFolder);
  simulationHelper->SetSimulationTime(Seconds(cfg.simTime));

  if (cfg.scenarioFolder == "constellation-telesat-351-sats")
    {
      std::set<uint32_t> beamSetTelesat = {1, 43, 60, 64};
      simulationHelper->SetBeamSet(beamSetTelesat);
    }
  else
    {
      std::set<uint32_t> beamSetDefault = {43, 30};
      simulationHelper->SetBeamSet(beamSetDefault);
    }

  simulationHelper->SetUserCountPerUt(2);
  simulationHelper->CreateSatScenario();

  NodeContainer gwsDbg = Singleton<SatTopology>::Get()->GetGwNodes();
  NodeContainer orbDbg = Singleton<SatTopology>::Get()->GetOrbiterNodes();
  
  std::cout << "[D1] orbiter nodes = " << orbDbg.GetN() << std::endl;
  std::cout << "[D1] gw nodes = " << gwsDbg.GetN() << std::endl;
  
  GraphCache cache = BuildGraphCache(cfg);

std::cout << "[D1] ISL adjacency sats = " << cache.adj.size() << std::endl;
std::cout << "[D1] GW anchor sat count = " << cache.gwAnchorSats.size() << std::endl;

if (!cache.gwAnchorSats.empty())
  {
    std::cout << "[D1] GW anchor sats:";
    for (uint32_t s : cache.gwAnchorSats)
      {
        std::cout << " " << s;
      }
    std::cout << std::endl;
  }
  
  uint32_t nOrb = Singleton<SatTopology>::Get()->GetNOrbiterNodes();
  std::cout << "[D1] GetNOrbiterNodes() = " << nOrb << std::endl;

  if (nOrb > 0)
    {
      Ptr<Node> sat0 = Singleton<SatTopology>::Get()->GetOrbiterNode(0);
      Ptr<SatMobilityModel> mob = sat0 ? sat0->GetObject<SatMobilityModel>() : nullptr;
      if (mob)
        {
          GeoCoordinate geo = mob->GetGeoPosition();
          std::cout << "[D1] SAT0 geo = lat=" << geo.GetLatitude()
                    << " lon=" << geo.GetLongitude()
                    << " alt=" << geo.GetAltitude() << std::endl;
        }
      else
        {
          std::cout << "[D1] SAT0 mobility not found" << std::endl;
        }
    }

  std::cout << "[D1] =====================================" << std::endl;
  std::cout << "[D1] isl-leo-candidate start" << std::endl;
  std::cout << "[D1] mode=" << cfg.mode << std::endl;
  std::cout << "[D1] scenarioFolder=" << cfg.scenarioFolder << std::endl;
  std::cout << "[D1] outDir=" << cfg.outDir << std::endl;
  std::cout << "[D1] simTime=" << cfg.simTime << std::endl;
  std::cout << "[D1] tStart=" << cfg.tStart << " tEnd=" << cfg.tEnd << std::endl;
  std::cout << "[D1] ref=(" << cfg.refLat << "," << cfg.refLon << ")" << std::endl;
  std::cout << "[D1] elevDeg=" << cfg.elevDeg << std::endl;
  std::cout << "[D1] =====================================" << std::endl;

  EnsureDir(cfg.outDir);
  EnsureDir(cfg.pcapDir);

  std::vector<std::string> generatedFiles;

  std::cout << "[D1] mode=" << cfg.mode << " scenario=" << cfg.scenarioFolder << std::endl;
  std::cout << "[D1] ref=(" << cfg.refLat << ", " << cfg.refLon << ") elev=" << cfg.elevDeg << std::endl;
  std::cout << "[D1] t=[" << cfg.tStart << ", " << cfg.tEnd << "] dt=" << cfg.dt
            << " planWindow=" << cfg.planWindow << std::endl;

  std::vector<CandidateRow> candidates;
  std::vector<IslRow> islRows;
  std::vector<PlanRow> planRows;

  if (cfg.mode == "verify_path")
    {
      RunVerifyPath(cfg);
      generatedFiles.push_back("route_dump.txt");
      generatedFiles.push_back("forward_path.log");
    }
  else if (cfg.mode == "candidate_scan")
    {
      candidates = RunCandidateScan(cfg, cache);
      WriteCandidateCsv(JoinPath(cfg.outDir, "candidate_sats.csv"), candidates);
      generatedFiles.push_back("candidate_sats.csv");
    }
  else if (cfg.mode == "isl_connectivity")
    {
      const auto satIds = GetScenarioSatelliteIds(cfg);
      for (double t = cfg.tStart; t <= cfg.tEnd + 1e-9; t += cfg.dt)
        {
          auto rows = EvaluateIslConnectivity(cfg, cache, t, satIds);
          islRows.insert(islRows.end(), rows.begin(), rows.end());
        }
      WriteIslCsv(JoinPath(cfg.outDir, "isl_connectivity.csv"), islRows);
      generatedFiles.push_back("isl_connectivity.csv");
    }
  else if (cfg.mode == "plan")
    {
      candidates = RunCandidateScan(cfg, cache);
      planRows = BuildRoutingPlan(cfg, cache, candidates);
      WritePlanCsv(JoinPath(cfg.outDir, "routing_plan.csv"), planRows);
      generatedFiles.push_back("routing_plan.csv");
    }
  else if (cfg.mode == "d1_final")
    {
      RunVerifyPath(cfg);
      generatedFiles.push_back("route_dump.txt");
      generatedFiles.push_back("forward_path.log");

      candidates = RunCandidateScan(cfg, cache);
      WriteCandidateCsv(JoinPath(cfg.outDir, "candidate_sats.csv"), candidates);
      generatedFiles.push_back("candidate_sats.csv");

      const auto satIds = GetScenarioSatelliteIds(cfg);
      for (double t = cfg.tStart; t <= cfg.tEnd + 1e-9; t += cfg.dt)
        {
          auto rows = EvaluateIslConnectivity(cfg, cache, t, satIds);
          islRows.insert(islRows.end(), rows.begin(), rows.end());
        }
      WriteIslCsv(JoinPath(cfg.outDir, "isl_connectivity.csv"), islRows);
      generatedFiles.push_back("isl_connectivity.csv");

      planRows = BuildRoutingPlan(cfg, cache, candidates);
      WritePlanCsv(JoinPath(cfg.outDir, "routing_plan.csv"), planRows);
      generatedFiles.push_back("routing_plan.csv");
    }
  else
    {
      NS_FATAL_ERROR("Unsupported mode: " << cfg.mode);
    }

  const auto gwInfo = ResolveGwInfo(cfg);
  WriteSummaryJson(JoinPath(cfg.outDir, "summary.json"), cfg, generatedFiles, gwInfo);
  generatedFiles.push_back("summary.json");

  std::cout << "[D1] done. outDir=" << cfg.outDir << std::endl;
  return 0;
}
