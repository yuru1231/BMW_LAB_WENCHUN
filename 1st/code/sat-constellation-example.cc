/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * sat-constellation-example.cc (Lucy/Wenj patched)
 *
 * Features:
 * - UT/GW/SAT position tracing (snapshot + timeseries), common schema:
 *     time_sec,role,nodeId,x_m,y_m,z_m
 * - IFMAP dump: role,idx,nodeId,ifIndex,localIp,peerNodeId
 * - Phase D: pruned static routing + UDP sanity flow
 *
 * Key routing behavior:
 * - routes_pruned.txt may specify a LOGICAL nextHop (e.g., SAT0) that is NOT L2-direct.
 * - Static routing requires a one-hop (same channel) neighbor.
 * - If nhTok is SAT* and src cannot directly reach that SAT node,
 *   we fallback to ANY direct L2 neighbor of src (the "service" node peer).
 *   This matches what IFMAP reveals in SNS3 scenarios (GWU/UTU attach to service nodes).
 *
 * Strictness:
 * - enableUdpTest=0 => route install failures: WARN + skip (do not abort dump/precheck)
 * - enableUdpTest=1 => route install failures: FATAL (sanity must be meaningful)
 */

#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/satellite-env-variables.h"
#include "ns3/traffic-module.h"
#include "ns3/ipv4-static-routing-helper.h"

#include "ns3/channel.h"
#include "ns3/net-device.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-address.h"
#include "ns3/uinteger.h"
#include "ns3/boolean.h"
#include "ns3/udp-client-server-helper.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <algorithm>
#include <set>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("sat-constellation-example");

/* ============================================================
 * UT/GW/SAT position tracing (snapshot + timeseries)
 * ============================================================ */

enum class PosTraceMode : uint32_t
{
  SNAPSHOT = 0,
  TIMESERIES = 1,
  BOTH = 2
};

static std::string
ModeToString(PosTraceMode m)
{
  switch (m)
  {
    case PosTraceMode::SNAPSHOT:   return "snapshot";
    case PosTraceMode::TIMESERIES: return "timeseries";
    case PosTraceMode::BOTH:       return "both";
  }
  return "unknown";
}

static PosTraceMode
ParseMode(uint32_t v)
{
  if (v == 0) return PosTraceMode::SNAPSHOT;
  if (v == 1) return PosTraceMode::TIMESERIES;
  return PosTraceMode::BOTH;
}

struct PosTraceConfig
{
  bool enabled = false;
  PosTraceMode mode = PosTraceMode::BOTH;

  // Simulation-time control
  double startTimeSec = 5.0;
  double stopTimeSec  = 30.0; // default; will be overridden by simTime
  double dtSec        = 1.0;
  double snapshotTimeSec = 0.0; // label only (snapshot writes immediately)

  // I/O
  uint32_t flushEvery = 10;
  uint32_t ioBufferKiB = 1024;

  // Limiters (0=no limit)
  uint32_t maxUt  = 0;
  uint32_t maxGw  = 0;
  uint32_t maxSat = 0;

  // Output path
  std::string outputPath;
};

class UtGwPosTracer
{
public:
  UtGwPosTracer(const PosTraceConfig& cfg,
                const NodeContainer& uts,
                const NodeContainer& gws,
                const NodeContainer& sats)
    : m_cfg(cfg), m_uts(uts), m_gws(gws), m_sats(sats)
  {}

  void InitFiles()
  {
    if (!m_cfg.enabled) return;

    std::string out = m_cfg.outputPath;
    if (!out.empty() && out.back() == '/') out.pop_back();

    // Snapshot files
    if (m_cfg.mode == PosTraceMode::SNAPSHOT || m_cfg.mode == PosTraceMode::BOTH)
    {
      m_utSnapshot.open((out + "/ut_positions_xyz_snapshot.csv").c_str(), std::ios::out);
      m_gwSnapshot.open((out + "/gw_positions_xyz_snapshot.csv").c_str(), std::ios::out);
      m_satSnapshot.open((out + "/sat_positions_xyz_snapshot.csv").c_str(), std::ios::out);

      SetupBuffer(m_utSnapshot);
      SetupBuffer(m_gwSnapshot);
      SetupBuffer(m_satSnapshot);

      WriteHeader(m_utSnapshot);
      WriteHeader(m_gwSnapshot);
      WriteHeader(m_satSnapshot);

      m_utSnapshot.flush();
      m_gwSnapshot.flush();
      m_satSnapshot.flush();
    }

    // Timeseries files
    if (m_cfg.mode == PosTraceMode::TIMESERIES || m_cfg.mode == PosTraceMode::BOTH)
    {
      m_utSeries.open((out + "/ut_positions_xyz_timeseries.csv").c_str(), std::ios::out);
      m_gwSeries.open((out + "/gw_positions_xyz_timeseries.csv").c_str(), std::ios::out);
      m_satSeries.open((out + "/sat_positions_xyz_timeseries.csv").c_str(), std::ios::out);

      SetupBuffer(m_utSeries);
      SetupBuffer(m_gwSeries);
      SetupBuffer(m_satSeries);

      WriteHeader(m_utSeries);
      WriteHeader(m_gwSeries);
      WriteHeader(m_satSeries);

      // ensure non-0B
      m_utSeries.flush();
      m_gwSeries.flush();
      m_satSeries.flush();
    }

    NS_LOG_UNCOND("[PosTrace] enabled mode=" << ModeToString(m_cfg.mode)
                  << " outputPath=" << out
                  << " start=" << m_cfg.startTimeSec
                  << " dt=" << m_cfg.dtSec
                  << " stop=" << m_cfg.stopTimeSec
                  << " flushEvery=" << m_cfg.flushEvery
                  << " maxUt=" << m_cfg.maxUt
                  << " maxGw=" << m_cfg.maxGw
                  << " maxSat=" << m_cfg.maxSat);
  }

  void WriteSnapshotNow()
  {
    if (!m_cfg.enabled) return;
    if (!(m_cfg.mode == PosTraceMode::SNAPSHOT || m_cfg.mode == PosTraceMode::BOTH)) return;

    DoSnapshot();
    CloseSnapshotFiles();
  }

  void ScheduleTimeseries()
  {
    if (!m_cfg.enabled) return;
    if (!(m_cfg.mode == PosTraceMode::TIMESERIES || m_cfg.mode == PosTraceMode::BOTH)) return;

    const double t0 = Clamp(m_cfg.startTimeSec, 0.0, m_cfg.stopTimeSec);
    Simulator::Schedule(Seconds(t0), &UtGwPosTracer::DoSample, this);
    Simulator::ScheduleDestroy(&UtGwPosTracer::CloseAll, this);

    NS_LOG_UNCOND("[PosTrace][DBG] ScheduleTimeseries() now=" << Simulator::Now().GetSeconds()
                  << " t0=" << t0
                  << " stop=" << m_cfg.stopTimeSec
                  << " dt=" << m_cfg.dtSec);
  }

private:
  static double Clamp(double v, double lo, double hi)
  {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
  }

  void SetupBuffer(std::ofstream& f)
  {
    if (!f.is_open()) return;
    const size_t bytes = static_cast<size_t>(m_cfg.ioBufferKiB) * 1024;
    m_ownedBuffers.emplace_back(std::make_unique<std::vector<char>>(bytes));
    f.rdbuf()->pubsetbuf(m_ownedBuffers.back()->data(),
                         static_cast<std::streamsize>(m_ownedBuffers.back()->size()));
  }

  void WriteHeader(std::ofstream& f)
  {
    if (!f.is_open()) return;
    f << "time_sec,role,nodeId,x_m,y_m,z_m\n";
  }

  void WriteOneContainer(std::ofstream& f,
                         const NodeContainer& nodes,
                         const char* role,
                         uint32_t maxN)
  {
    if (!f.is_open()) return;

    const double now = Simulator::Now().GetSeconds();

    std::ostringstream buf;
    buf.setf(std::ios::scientific);
    buf << std::setprecision(8);

    const uint32_t nmax = (maxN == 0) ? nodes.GetN() : std::min(nodes.GetN(), maxN);
    for (uint32_t i = 0; i < nmax; ++i)
    {
      Ptr<Node> n = nodes.Get(i);
      Ptr<MobilityModel> mob = n->GetObject<MobilityModel>();
      if (!mob) continue;

      Vector p = mob->GetPosition();
      buf << now << "," << role << "," << n->GetId()
          << "," << p.x << "," << p.y << "," << p.z << "\n";
    }

    f << buf.str();
  }

  void DoSnapshot()
  {
    if (m_utSnapshot.is_open())  WriteOneContainer(m_utSnapshot,  m_uts,  "UT",  m_cfg.maxUt);
    if (m_gwSnapshot.is_open())  WriteOneContainer(m_gwSnapshot,  m_gws,  "GW",  m_cfg.maxGw);
    if (m_satSnapshot.is_open()) WriteOneContainer(m_satSnapshot, m_sats, "SAT", m_cfg.maxSat);

    if (m_utSnapshot.is_open())  m_utSnapshot.flush();
    if (m_gwSnapshot.is_open())  m_gwSnapshot.flush();
    if (m_satSnapshot.is_open()) m_satSnapshot.flush();

    NS_LOG_UNCOND("[PosTrace] snapshot done at t=" << Simulator::Now().GetSeconds()
                  << " UTs=" << m_uts.GetN()
                  << " GWs=" << m_gws.GetN()
                  << " SATs=" << m_sats.GetN());
  }

  void DoSample()
  {
    const double now = Simulator::Now().GetSeconds();
    if (now > m_cfg.stopTimeSec + 1e-9) return;

    if (m_utSeries.is_open())  WriteOneContainer(m_utSeries,  m_uts,  "UT",  m_cfg.maxUt);
    if (m_gwSeries.is_open())  WriteOneContainer(m_gwSeries,  m_gws,  "GW",  m_cfg.maxGw);
    if (m_satSeries.is_open()) WriteOneContainer(m_satSeries, m_sats, "SAT", m_cfg.maxSat);

    m_sampleCount++;
    if (m_cfg.flushEvery > 0 && (m_sampleCount % m_cfg.flushEvery == 0))
    {
      if (m_utSeries.is_open())  m_utSeries.flush();
      if (m_gwSeries.is_open())  m_gwSeries.flush();
      if (m_satSeries.is_open()) m_satSeries.flush();
    }

    const double next = now + m_cfg.dtSec;
    if (next > m_cfg.stopTimeSec + 1e-9) return;

    Simulator::Schedule(Seconds(m_cfg.dtSec), &UtGwPosTracer::DoSample, this);
  }

  void CloseSnapshotFiles()
  {
    if (m_utSnapshot.is_open())  { m_utSnapshot.flush();  m_utSnapshot.close(); }
    if (m_gwSnapshot.is_open())  { m_gwSnapshot.flush();  m_gwSnapshot.close(); }
    if (m_satSnapshot.is_open()) { m_satSnapshot.flush(); m_satSnapshot.close(); }
  }

  void CloseAll()
  {
    CloseSnapshotFiles();
    if (m_utSeries.is_open())  { m_utSeries.flush();  m_utSeries.close(); }
    if (m_gwSeries.is_open())  { m_gwSeries.flush();  m_gwSeries.close(); }
    if (m_satSeries.is_open()) { m_satSeries.flush(); m_satSeries.close(); }
  }

private:
  PosTraceConfig m_cfg;
  NodeContainer m_uts, m_gws, m_sats;

  std::ofstream m_utSnapshot, m_gwSnapshot, m_satSnapshot;
  std::ofstream m_utSeries,   m_gwSeries,   m_satSeries;

  uint32_t m_sampleCount = 0;
  std::vector<std::unique_ptr<std::vector<char>>> m_ownedBuffers;
};

/* ============================================================
 * ProgressTick (simTime + wall)
 * ============================================================ */

static std::chrono::steady_clock::time_point gWallT0;
static bool gWallT0Set = false;
static double gProgressStopSimSec = -1.0;

static void
ProgressTick()
{
  using namespace std::chrono;

  const double sim  = Simulator::Now().GetSeconds();
  const double wall = gWallT0Set ? duration<double>(steady_clock::now() - gWallT0).count() : -1.0;

  std::cout.setf(std::ios::fixed);
  std::cout << std::setprecision(3);
  NS_LOG_UNCOND("[Progress] simTime=" << sim << "s  wall=+" << wall << "s");

  if (gProgressStopSimSec > 0.0 && (sim + 1.0) <= gProgressStopSimSec + 1e-9)
  {
    Simulator::Schedule(Seconds(1.0), &ProgressTick);
  }
}

/* ============================================================
 * Phase D: Pruned static routing + UDP sanity flow
 * ============================================================ */

static std::vector<std::tuple<std::string,std::string,std::string>>
LoadRoutesPruned(const std::string& file)
{
  std::vector<std::tuple<std::string,std::string,std::string>> rules;
  std::ifstream in(file);
  if (!in.is_open())
  {
    NS_FATAL_ERROR("Cannot open routesPrunedFile=" << file);
  }

  std::string line;
  while (std::getline(in, line))
  {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream iss(line);
    std::string src, dst, nh;
    if (!(iss >> src >> dst >> nh)) continue;
    rules.emplace_back(src, dst, nh);
  }
  return rules;
}

static uint32_t
ParseIndexOrFatal(const std::string& s, size_t off)
{
  if (s.size() <= off) NS_FATAL_ERROR("Bad token (no index): " << s);
  return static_cast<uint32_t>(std::stoul(s.substr(off)));
}

static Ptr<Node>
ResolveTokenToNode(const std::string& tok,
                   const NodeContainer& utUsers,
                   const NodeContainer& gwUsers,
                   const NodeContainer& uts,
                   const NodeContainer& gws,
                   const NodeContainer& sats)
{
  if (tok.rfind("UTU", 0) == 0)
  {
    uint32_t i = ParseIndexOrFatal(tok, 3);
    if (i >= utUsers.GetN()) NS_FATAL_ERROR("UTU index out of range: " << tok);
    return utUsers.Get(i);
  }
  if (tok.rfind("GWU", 0) == 0)
  {
    uint32_t i = ParseIndexOrFatal(tok, 3);
    if (i >= gwUsers.GetN()) NS_FATAL_ERROR("GWU index out of range: " << tok);
    return gwUsers.Get(i);
  }
  if (tok.rfind("UT", 0) == 0)
  {
    uint32_t i = ParseIndexOrFatal(tok, 2);
    if (i >= uts.GetN()) NS_FATAL_ERROR("UT index out of range: " << tok);
    return uts.Get(i);
  }
  if (tok.rfind("GW", 0) == 0)
  {
    uint32_t i = ParseIndexOrFatal(tok, 2);
    if (i >= gws.GetN()) NS_FATAL_ERROR("GW index out of range: " << tok);
    return gws.Get(i);
  }
  if (tok.rfind("SAT", 0) == 0)
  {
    uint32_t i = ParseIndexOrFatal(tok, 3);
    if (i >= sats.GetN()) NS_FATAL_ERROR("SAT index out of range: " << tok);
    return sats.Get(i);
  }

  NS_FATAL_ERROR("Unknown token prefix (expect UTU/GWU/UT/GW/SAT): " << tok);
  return nullptr;
}

static bool
StartsWith(const std::string& s, const char* pfx)
{
  return s.rfind(pfx, 0) == 0;
}

static Ipv4Address
GetAnyHostIp(Ptr<Node> n)
{
  Ptr<Ipv4> ip = n->GetObject<Ipv4>();
  if (!ip) return Ipv4Address::GetZero();

  for (uint32_t ifi = 0; ifi < ip->GetNInterfaces(); ++ifi)
  {
    for (uint32_t ai = 0; ai < ip->GetNAddresses(ifi); ++ai)
    {
      Ipv4Address addr = ip->GetAddress(ifi, ai).GetLocal();
      if (addr != Ipv4Address("127.0.0.1") && addr != Ipv4Address::GetZero())
        return addr;
    }
  }
  return Ipv4Address::GetZero();
}

// Check if src has a one-hop (same channel) link to peer; return src-side outIf.
static bool
FindOutIfToPeer(Ptr<Node> src, Ptr<Node> peer, uint32_t& outIf)
{
  Ptr<Ipv4> ipv4Src = src->GetObject<Ipv4>();
  if (!ipv4Src) return false;

  for (uint32_t ifIndex = 0; ifIndex < ipv4Src->GetNInterfaces(); ++ifIndex)
  {
    Ptr<NetDevice> dev = src->GetDevice(ifIndex);
    if (!dev) continue;
    Ptr<Channel> ch = dev->GetChannel();
    if (!ch) continue;

    for (uint32_t i = 0; i < ch->GetNDevices(); ++i)
    {
      Ptr<NetDevice> pdev = ch->GetDevice(i);
      if (pdev && pdev->GetNode() == peer)
      {
        outIf = ifIndex;
        return true;
      }
    }
  }
  return false;
}

// Find peer IPv4 address that is on same channel as src's outIf.
static bool
FindPeerIpv4OnSameChannel(Ptr<Node> src, Ptr<Node> peer, uint32_t outIf, Ipv4Address& peerIp)
{
  Ptr<Ipv4> ipv4Peer = peer->GetObject<Ipv4>();
  if (!ipv4Peer) return false;

  Ptr<NetDevice> srcDev = src->GetDevice(outIf);
  if (!srcDev) return false;
  Ptr<Channel> ch = srcDev->GetChannel();
  if (!ch) return false;

  for (uint32_t ifi = 0; ifi < ipv4Peer->GetNInterfaces(); ++ifi)
  {
    Ptr<NetDevice> dev = peer->GetDevice(ifi);
    if (!dev) continue;
    if (dev->GetChannel() != ch) continue;

    for (uint32_t ai = 0; ai < ipv4Peer->GetNAddresses(ifi); ++ai)
    {
      Ipv4Address a = ipv4Peer->GetAddress(ifi, ai).GetLocal();
      if (a != Ipv4Address("127.0.0.1") && a != Ipv4Address::GetZero())
      {
        peerIp = a;
        return true;
      }
    }
    return false;
  }

  return false;
}

// Pick ANY direct L2 neighbor (one hop) of src on an interface that has a non-zero local IPv4.
// Returns (peerNode, outIf).
static bool
PickAnyDirectNeighborWithIpv4(Ptr<Node> src, Ptr<Node>& outPeer, uint32_t& outIf)
{
  Ptr<Ipv4> ipv4Src = src->GetObject<Ipv4>();
  if (!ipv4Src) return false;

  for (uint32_t ifIndex = 0; ifIndex < ipv4Src->GetNInterfaces(); ++ifIndex)
  {
    // require src local IPv4 on this if
    Ipv4Address local = Ipv4Address::GetZero();
    for (uint32_t ai = 0; ai < ipv4Src->GetNAddresses(ifIndex); ++ai)
    {
      Ipv4Address a = ipv4Src->GetAddress(ifIndex, ai).GetLocal();
      if (a != Ipv4Address("127.0.0.1") && a != Ipv4Address::GetZero())
      {
        local = a;
        break;
      }
    }
    if (local == Ipv4Address::GetZero()) continue;

    Ptr<NetDevice> dev = src->GetDevice(ifIndex);
    if (!dev) continue;
    Ptr<Channel> ch = dev->GetChannel();
    if (!ch) continue;

    for (uint32_t i = 0; i < ch->GetNDevices(); ++i)
    {
      Ptr<NetDevice> peerDev = ch->GetDevice(i);
      if (!peerDev) continue;

      Ptr<Node> peerNode = peerDev->GetNode();
      if (peerNode && peerNode != src)
      {
        outPeer = peerNode;
        outIf   = ifIndex;
        return true;
      }
    }
  }
  return false;
}

static bool
ApplyOnePrunedRoute(const std::string& srcTok,
                    const std::string& dstTok,
                    const std::string& nhTok,
                    const NodeContainer& utUsers,
                    const NodeContainer& gwUsers,
                    const NodeContainer& uts,
                    const NodeContainer& gws,
                    const NodeContainer& sats,
                    bool strictFatal)
{
  Ipv4StaticRoutingHelper srh;

  Ptr<Node> src = ResolveTokenToNode(srcTok, utUsers, gwUsers, uts, gws, sats);
  Ptr<Node> dst = ResolveTokenToNode(dstTok, utUsers, gwUsers, uts, gws, sats);
  Ptr<Node> nh  = ResolveTokenToNode(nhTok,  utUsers, gwUsers, uts, gws, sats);

  Ptr<Ipv4> srcIpv4 = src->GetObject<Ipv4>();
  if (!srcIpv4)
  {
    NS_LOG_UNCOND("[ROUTE_PRUNED][SKIP] srcTok=" << srcTok << " has no Ipv4");
    return false;
  }

  Ipv4Address dstIp = GetAnyHostIp(dst);
  if (dstIp == Ipv4Address::GetZero())
  {
    std::ostringstream msg;
    msg << "Cannot find usable IPv4 on dst=" << dstTok;
    if (strictFatal) NS_FATAL_ERROR(msg.str());
    NS_LOG_UNCOND("[ROUTE_PRUNED][WARN] " << msg.str());
    return false;
  }

  Ptr<Ipv4StaticRouting> srt = srh.GetStaticRouting(srcIpv4);
  if (!srt)
  {
    std::ostringstream msg;
    msg << "No Ipv4StaticRouting installed on node " << srcTok;
    if (strictFatal) NS_FATAL_ERROR(msg.str());
    NS_LOG_UNCOND("[ROUTE_PRUNED][WARN] " << msg.str());
    return false;
  }

  // Try direct one-hop to logical next hop
  uint32_t outIf = 0;
  Ptr<Node> effectiveNh = nh;

  if (!FindOutIfToPeer(src, nh, outIf))
  {
    // Only apply fallback when logical next hop is SAT* (common case: logical hop != one-hop)
    if (!StartsWith(nhTok, "SAT"))
    {
      std::ostringstream msg;
      msg << "No direct link between " << srcTok << " and " << nhTok
          << " (nhTok is not SAT*, no fallback applies)";
      if (strictFatal) NS_FATAL_ERROR(msg.str());
      NS_LOG_UNCOND("[ROUTE_PRUNED][WARN] " << msg.str());
      return false;
    }

    Ptr<Node> peer;
    uint32_t fallbackIf = 0;
    bool ok = PickAnyDirectNeighborWithIpv4(src, peer, fallbackIf);

    if (!ok || !peer)
    {
      std::ostringstream msg;
      msg << "No direct link between " << srcTok << " and " << nhTok
          << " AND no direct neighbor found for fallback";
      if (strictFatal) NS_FATAL_ERROR(msg.str());
      NS_LOG_UNCOND("[ROUTE_PRUNED][WARN] " << msg.str());
      return false;
    }

    effectiveNh = peer;
    outIf = fallbackIf;

    NS_LOG_UNCOND("[ROUTE_PRUNED][FALLBACK] src=" << srcTok
                  << " logicalNh=" << nhTok
                  << " -> effectiveNh(nodeId=" << effectiveNh->GetId() << ")"
                  << " outIf=" << outIf);
  }

  Ipv4Address nhIp = Ipv4Address::GetZero();
  const bool hasNhIp = FindPeerIpv4OnSameChannel(src, effectiveNh, outIf, nhIp);

  if (hasNhIp)
  {
    srt->AddHostRouteTo(dstIp, nhIp, outIf);
    NS_LOG_UNCOND("[ROUTE_PRUNED] " << srcTok << " -> dst(" << dstTok << ":" << dstIp
                  << ") via effectiveNh(nodeId=" << effectiveNh->GetId()
                  << ",ip=" << nhIp << ") if=" << outIf);
  }
  else
  {
    // Direct route via interface (works even if next hop has no IPv4)
    srt->AddHostRouteTo(dstIp, outIf);
    NS_LOG_UNCOND("[ROUTE_PRUNED] " << srcTok << " -> dst(" << dstTok << ":" << dstIp
                  << ") via outIf=" << outIf << " (direct-no-nh-ip)");
  }

  return true;
}

static void
ApplyPrunedStaticRoutes(const NodeContainer& utUsers,
                        const NodeContainer& gwUsers,
                        const NodeContainer& uts,
                        const NodeContainer& gws,
                        const NodeContainer& sats,
                        const std::vector<std::tuple<std::string,std::string,std::string>>& rules,
                        bool strictFatal)
{
  for (const auto& r : rules)
  {
    ApplyOnePrunedRoute(std::get<0>(r), std::get<1>(r), std::get<2>(r),
                       utUsers, gwUsers, uts, gws, sats,
                       strictFatal);
  }
}

/* ============================================================
 * IF map dump (Node ifIndex/IP/peer)
 * ============================================================ */

static void
DumpIfMapCsv(const std::string& path,
             const NodeContainer& utUsers,
             const NodeContainer& gwUsers,
             const NodeContainer& uts,
             const NodeContainer& gws,
             const NodeContainer& sats)
{
  if (path.empty()) return;

  std::ofstream out(path, std::ios::out);
  if (!out.is_open())
  {
    NS_FATAL_ERROR("Cannot open dumpIfMapCsv=" << path);
  }

  out << "role,idx,nodeId,ifIndex,localIp,peerNodeId\n";

  auto DumpOne = [&](const char* role, uint32_t idx, Ptr<Node> n)
  {
    Ptr<Ipv4> ip = n->GetObject<Ipv4>();
    if (!ip) return;

    for (uint32_t ifi = 0; ifi < ip->GetNInterfaces(); ++ifi)
    {
      Ptr<NetDevice> dev = n->GetDevice(ifi);
      Ptr<Channel> ch = dev ? dev->GetChannel() : nullptr;

      Ipv4Address local = Ipv4Address::GetZero();
      for (uint32_t ai = 0; ai < ip->GetNAddresses(ifi); ++ai)
      {
        Ipv4Address a = ip->GetAddress(ifi, ai).GetLocal();
        if (a != Ipv4Address("127.0.0.1") && a != Ipv4Address::GetZero())
        {
          local = a;
          break;
        }
      }

      int peerId = -1;
      if (ch)
      {
        for (uint32_t di = 0; di < ch->GetNDevices(); ++di)
        {
          Ptr<NetDevice> peerDev = ch->GetDevice(di);
          if (peerDev && peerDev != dev)
          {
            peerId = static_cast<int>(peerDev->GetNode()->GetId());
            break;
          }
        }
      }

      out << role << "," << idx << "," << n->GetId()
          << "," << ifi << "," << local
          << "," << peerId << "\n";
    }
  };

  for (uint32_t i = 0; i < utUsers.GetN(); ++i) DumpOne("UTU", i, utUsers.Get(i));
  for (uint32_t i = 0; i < gwUsers.GetN(); ++i) DumpOne("GWU", i, gwUsers.Get(i));
  for (uint32_t i = 0; i < uts.GetN(); ++i)     DumpOne("UT",  i, uts.Get(i));
  for (uint32_t i = 0; i < gws.GetN(); ++i)     DumpOne("GW",  i, gws.Get(i));
  for (uint32_t i = 0; i < sats.GetN(); ++i)    DumpOne("SAT", i, sats.Get(i));

  out.close();

  NS_LOG_UNCOND("[IFMAP] wrote " << path
                << " UTU=" << utUsers.GetN()
                << " GWU=" << gwUsers.GetN()
                << " UT="  << uts.GetN()
                << " GW="  << gws.GetN()
                << " SAT=" << sats.GetN());
}

/* ============================================================
 * main
 * ============================================================ */

int
main(int argc, char* argv[])
{
  auto WallNow = [](){
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
  };

  uint32_t packetSize = 512;
  std::string interval = "20ms";
  std::string scenarioFolder = "constellation-eutelsat-geo-2-sats-isls";
  double simTime = 30.0;
  std::string outputPath = "";

  // Position trace CLI
  uint32_t enableUtGwPosTrace = 0;
  uint32_t posTraceMode = 2;
  double posTraceStartTime = 5.0;
  double posTraceStopTime  = -1.0;
  double posDt = 1.0;
  double snapshotTime = 0.0;
  uint32_t posTraceFlushEvery = 10;
  uint32_t posTraceIoBufferKiB = 1024;
  uint32_t posTraceExitAfterSnapshot = 0;
  uint32_t posTraceMaxUt  = 0;
  uint32_t posTraceMaxGw  = 0;
  uint32_t posTraceMaxSat = 0;

  // Phase D
  std::string routesPrunedFile = "";
  uint32_t enableUdpTest = 0;
  std::string rxSummaryCsv = "";

  // Debug dump
  std::string dumpIfMapCsv = "";

  Ptr<SimulationHelper> simulationHelper =
    CreateObject<SimulationHelper>("example-constellation");

  CommandLine cmd;
  cmd.AddValue("packetSize", "Size of constant packet (bytes)", packetSize);
  cmd.AddValue("interval", "CBR interval (e.g. 20ms)", interval);
  cmd.AddValue("scenarioFolder", "Scenario folder name", scenarioFolder);
  cmd.AddValue("simTime", "Simulation time (seconds)", simTime);
  cmd.AddValue("OutputPath", "Output path for storing the simulation statistics", outputPath);

  cmd.AddValue("enableUtGwPosTrace", "Enable UT/GW/SAT position trace (0/1)", enableUtGwPosTrace);
  cmd.AddValue("posTraceMode", "Position trace mode: 0=snapshot,1=timeseries,2=both", posTraceMode);
  cmd.AddValue("posTraceStartTime", "Time-series warm-up start time (seconds)", posTraceStartTime);
  cmd.AddValue("posTraceStopTime", "Time-series stop time (seconds, -1 means simTime)", posTraceStopTime);
  cmd.AddValue("posDt", "Time-series sampling period (seconds)", posDt);
  cmd.AddValue("snapshotTime", "Snapshot time label (seconds)", snapshotTime);
  cmd.AddValue("posTraceFlushEvery", "Flush every N samples (timeseries)", posTraceFlushEvery);
  cmd.AddValue("posTraceIoBufferKiB", "Stream buffer size (KiB) for trace files", posTraceIoBufferKiB);
  cmd.AddValue("posTraceExitAfterSnapshot",
               "Exit immediately after snapshot (0/1). Use with posTraceMode=0.",
               posTraceExitAfterSnapshot);
  cmd.AddValue("posTraceMaxUt",  "Limit number of UT nodes traced (0=no limit)",  posTraceMaxUt);
  cmd.AddValue("posTraceMaxGw",  "Limit number of GW nodes traced (0=no limit)",  posTraceMaxGw);
  cmd.AddValue("posTraceMaxSat", "Limit number of SAT nodes traced (0=no limit)", posTraceMaxSat);

  cmd.AddValue("routesPrunedFile", "Path to routes_pruned.txt (format: src dst nextHop)", routesPrunedFile);
  cmd.AddValue("enableUdpTest", "Enable UDP sanity flow (0/1)", enableUdpTest);
  cmd.AddValue("rxSummaryCsv", "Output CSV path for Rx summary", rxSummaryCsv);

  cmd.AddValue("dumpIfMapCsv", "Output CSV path to dump Node ifIndex/IP/peer mapping", dumpIfMapCsv);

  simulationHelper->AddDefaultUiArguments(cmd);
  cmd.Parse(argc, argv);

  gWallT0 = std::chrono::steady_clock::now();
  gWallT0Set = true;

  // Config
  Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));
  Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                     EnumValue(SatEnums::REGENERATION_NETWORK));

  Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionEachRequest",
                     BooleanValue(false));
  Config::SetDefault("ns3::SatSGP4MobilityModel::UpdatePositionPeriod",
                     TimeValue(Seconds(10)));

  Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(false));
  Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite",
                     BooleanValue(true));

  // Scenario
  simulationHelper->LoadScenario(scenarioFolder);
  simulationHelper->SetSimulationTime(Seconds(simTime));

  std::set<uint32_t> beamSet = {43, 30};
  std::set<uint32_t> beamSetTelesat = {1, 43, 60, 64};

  if (scenarioFolder == "constellation-telesat-351-sats")
    simulationHelper->SetBeamSet(beamSetTelesat);
  else
    simulationHelper->SetBeamSet(beamSet);

  simulationHelper->SetUserCountPerUt(2);
  LogComponentEnable("sat-constellation-example", LOG_LEVEL_INFO);

  const double w0 = WallNow();
  NS_LOG_UNCOND("[WALL] before CreateSatScenario t=" << w0);

  simulationHelper->CreateSatScenario();

  const double w1 = WallNow();
  NS_LOG_UNCOND("[WALL] after CreateSatScenario dt=" << (w1 - w0) << "s");

  // Stable containers
  NodeContainer satsTopo    = Singleton<SatTopology>::Get()->GetOrbiterNodes();
  NodeContainer utsTopo     = Singleton<SatTopology>::Get()->GetUtNodes();
  NodeContainer gwsTopo     = Singleton<SatTopology>::Get()->GetGwNodes();
  NodeContainer utUsersTopo = Singleton<SatTopology>::Get()->GetUtUserNodes(utsTopo);
  NodeContainer gwUsersTopo = Singleton<SatTopology>::Get()->GetGwUserNodes();

  NS_LOG_UNCOND("[TOPO] counts: UT=" << utsTopo.GetN()
                << " UTU=" << utUsersTopo.GetN()
                << " GW=" << gwsTopo.GetN()
                << " GWU=" << gwUsersTopo.GetN()
                << " SAT=" << satsTopo.GetN());

  DumpIfMapCsv(dumpIfMapCsv, utUsersTopo, gwUsersTopo, utsTopo, gwsTopo, satsTopo);

  // Phase D routes
  std::vector<std::tuple<std::string,std::string,std::string>> rules;
  if (!routesPrunedFile.empty())
  {
    NS_LOG_UNCOND("[PHASED] routesPrunedFile=" << routesPrunedFile);
    rules = LoadRoutesPruned(routesPrunedFile);
    NS_LOG_UNCOND("[PHASED] rules=" << rules.size());

    const bool strictFatal = (enableUdpTest != 0);
    ApplyPrunedStaticRoutes(utUsersTopo, gwUsersTopo, utsTopo, gwsTopo, satsTopo, rules, strictFatal);
  }

  // UDP sanity flow
  if (enableUdpTest != 0)
  {
    if (routesPrunedFile.empty())
      NS_FATAL_ERROR("enableUdpTest=1 but routesPrunedFile is empty");
    if (rules.empty())
      NS_FATAL_ERROR("routesPrunedFile has no rules; cannot pick UDP endpoints");

    auto isEnd = [](const std::string& t) -> bool {
      return (t.rfind("UTU", 0) == 0) || (t.rfind("GWU", 0) == 0) ||
             (t.rfind("UT",  0) == 0) || (t.rfind("GW",  0) == 0);
    };

    std::string clientTok, serverTok;
    for (const auto& r : rules)
    {
      const std::string& s = std::get<0>(r);
      const std::string& d = std::get<1>(r);
      if (isEnd(s) && isEnd(d))
      {
        clientTok = s;
        serverTok = d;
        break;
      }
    }
    if (clientTok.empty())
    {
      clientTok = std::get<0>(rules.front());
      serverTok = std::get<1>(rules.front());
    }

    Ptr<Node> udpClientNode = ResolveTokenToNode(clientTok, utUsersTopo, gwUsersTopo, utsTopo, gwsTopo, satsTopo);
    Ptr<Node> udpServerNode = ResolveTokenToNode(serverTok, utUsersTopo, gwUsersTopo, utsTopo, gwsTopo, satsTopo);

    Ipv4Address serverIp = GetAnyHostIp(udpServerNode);

    NS_LOG_UNCOND("[APP] UDP endpoints: clientTok=" << clientTok
                  << " nodeId=" << udpClientNode->GetId()
                  << " -> serverTok=" << serverTok
                  << " nodeId=" << udpServerNode->GetId()
                  << " serverIp=" << serverIp
                  << " interval=" << interval
                  << " pktSize=" << packetSize
                  << " simTime=" << simTime);

    if (serverIp == Ipv4Address::GetZero())
      NS_FATAL_ERROR("Server node " << serverTok << " has no usable IPv4 address");

    const uint16_t port = 9000;

    UdpServerHelper server(port);
    ApplicationContainer serverApps = server.Install(udpServerNode);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(std::max(1.0, simTime - 1.0)));

    Time udpInterval = Time(interval);

    UdpClientHelper client(serverIp, port);
    client.SetAttribute("MaxPackets", UintegerValue(1000000000));
    client.SetAttribute("Interval", TimeValue(udpInterval));
    client.SetAttribute("PacketSize", UintegerValue(packetSize));

    ApplicationContainer clientApps = client.Install(udpClientNode);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(std::max(2.0, simTime - 2.0)));

    Ptr<UdpServer> srv = DynamicCast<UdpServer>(serverApps.Get(0));
    if (!srv)
      NS_FATAL_ERROR("UdpServer DynamicCast failed on serverApps.Get(0)");

    if (!rxSummaryCsv.empty())
    {
      Simulator::Schedule(Seconds(std::max(0.0, simTime - 0.5)), [srv, rxSummaryCsv]() {
        std::ofstream out(rxSummaryCsv, std::ios::out);
        out << "rx_packets\n";
        out << srv->GetReceived() << "\n";
        out.close();
        NS_LOG_UNCOND("[RX_SUMMARY] wrote " << rxSummaryCsv
                      << " rx_packets=" << srv->GetReceived());
      });
    }
  }

  // Position tracing config
  PosTraceConfig pt;
  pt.enabled = (enableUtGwPosTrace != 0);
  pt.mode = ParseMode(posTraceMode);
  pt.startTimeSec = posTraceStartTime;
  pt.dtSec = posDt;
  pt.snapshotTimeSec = snapshotTime;
  pt.flushEvery = posTraceFlushEvery;
  pt.ioBufferKiB = posTraceIoBufferKiB;
  pt.maxUt  = posTraceMaxUt;
  pt.maxGw  = posTraceMaxGw;
  pt.maxSat = posTraceMaxSat;
  pt.stopTimeSec = (posTraceStopTime > 0.0) ? posTraceStopTime : simTime;

  pt.outputPath = outputPath;
  if (pt.outputPath.empty())
  {
    pt.outputPath = Singleton<SatEnvVariables>::Get()->GetOutputPath();
  }

  gProgressStopSimSec = simTime;

  static std::unique_ptr<UtGwPosTracer> tracer;
  if (pt.enabled)
  {
    tracer = std::make_unique<UtGwPosTracer>(pt, utsTopo, gwsTopo, satsTopo);
    tracer->InitFiles();

    if (pt.mode == PosTraceMode::SNAPSHOT && posTraceExitAfterSnapshot != 0)
    {
      tracer->WriteSnapshotNow();
      return 0;
    }
    if (pt.mode == PosTraceMode::BOTH)
      tracer->WriteSnapshotNow();

    tracer->ScheduleTimeseries();
  }

  // ConfigStore (optional, keep)
  Config::SetDefault("ns3::ConfigStore::Filename", StringValue("output-attributes.xml"));
  Config::SetDefault("ns3::ConfigStore::FileFormat", StringValue("Xml"));
  Config::SetDefault("ns3::ConfigStore::Mode", StringValue("Save"));
  ConfigStore outputConfig;
  outputConfig.ConfigureDefaults();

  // Run
  const double w2 = WallNow();
  NS_LOG_UNCOND("[WALL] before RunSimulation dt_since_create=" << (w2 - w1) << "s");

  Simulator::Schedule(Seconds(0.0), &ProgressTick);

  simulationHelper->RunSimulation();

  const double w3 = WallNow();
  NS_LOG_UNCOND("[WALL] after RunSimulation dt=" << (w3 - w2) << "s total=" << (w3 - w0) << "s");

  return 0;
}


