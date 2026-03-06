// scratch/w25_tier2p5_window_metrics.cc
//
// Tier2.5 (robust, propagation-safe):
// - Deterministic pattern drives ENABLE/DISABLE windows per cell (A/B/C).
// - GW sends CBR UDP only when cell is ENABLE.
// - Each packet carries WinTag(cellId, winId) so RX bytes are attributed back to the window
//   where the packet was SENT (not where it arrived).
//
// PatternCsv: time_us,cell,action   cell in {A,B,C}, action in {ENABLE,DISABLE}
// Outputs (under --outDir):
//   - beam_hop_events.log              (control-plane evidence)
//   - tier23_window_metrics.csv        (window-attributed data-plane evidence)
//   - throughput_per_cell.csv          (optional bin time-series, based on total RX)
//
// Build & run:
//   ./ns3 run "w25_tier2p5_window_metrics --PrintHelp"
//   export OUT=/tmp/tier2p5
//   ./ns3 run "w25_tier2p5_window_metrics --outDir=$OUT --PatternCsv=/tmp/pattern.csv --onRate=50Mbps --binUs=1000 --stopMarginUs=60000"
//
// Notes:
// - If you want windows (0~30ms) to get non-zero rx_bytes, you MUST run long enough for
//   packets to arrive: stopMarginUs >= one-way path delay + some slack.
//
// ns-3.43 compatible.

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <iomanip>
#include <cctype>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("W25Tier2p5WindowMetrics");

// ------------------------ Pattern ------------------------

struct PatternEvent
{
  uint64_t timeUs{0};
  uint32_t cellId{0}; // 0=A,1=B,2=C
  bool enable{false};
};

static bool ParseCell(const std::string& s, uint32_t& cellId)
{
  if (s == "A" || s == "a") { cellId = 0; return true; }
  if (s == "B" || s == "b") { cellId = 1; return true; }
  if (s == "C" || s == "c") { cellId = 2; return true; }
  return false;
}

static std::string CellName(uint32_t cellId)
{
  if (cellId == 0) return "A";
  if (cellId == 1) return "B";
  return "C";
}

static bool ParseAction(const std::string& s, bool& enable)
{
  if (s == "ENABLE" || s == "enable" || s == "ON" || s == "on") { enable = true; return true; }
  if (s == "DISABLE" || s == "disable" || s == "OFF" || s == "off") { enable = false; return true; }
  return false;
}

static std::vector<PatternEvent> ReadPatternCsv(const std::string& path)
{
  std::ifstream fin(path.c_str(), std::ios::in);
  if (!fin.is_open())
  {
    NS_FATAL_ERROR("Cannot open PatternCsv: " << path);
  }

  std::vector<PatternEvent> evs;
  std::string line;
  bool first = true;

  while (std::getline(fin, line))
  {
    if (line.empty()) continue;

    if (first)
    {
      first = false;
      if (!std::isdigit(static_cast<unsigned char>(line[0])))
      {
        // header
        continue;
      }
    }

    std::stringstream ss(line);
    std::string tokTime, tokCell, tokAction;

    if (!std::getline(ss, tokTime, ',')) continue;
    if (!std::getline(ss, tokCell, ',')) continue;
    if (!std::getline(ss, tokAction, ',')) continue;

    PatternEvent e;
    e.timeUs = static_cast<uint64_t>(std::stoull(tokTime));

    if (!ParseCell(tokCell, e.cellId))
    {
      NS_FATAL_ERROR("PatternCsv bad cell: '" << tokCell << "' in line: " << line);
    }

    if (!ParseAction(tokAction, e.enable))
    {
      NS_FATAL_ERROR("PatternCsv bad action: '" << tokAction << "' in line: " << line);
    }

    evs.push_back(e);
  }

  std::sort(evs.begin(), evs.end(), [](const PatternEvent& a, const PatternEvent& b){
    if (a.timeUs != b.timeUs) return a.timeUs < b.timeUs;
    // Same time: DISABLE before ENABLE
    if (a.enable != b.enable) return a.enable < b.enable;
    return a.cellId < b.cellId;
  });

  return evs;
}

// ------------------------ WinTag (packet attribution) ------------------------

class WinTag : public Tag
{
public:
  WinTag() = default;
  WinTag(uint8_t cell, uint32_t win) : m_cell(cell), m_win(win) {}

  static TypeId GetTypeId()
  {
    static TypeId tid = TypeId("WinTag")
      .SetParent<Tag>()
      .AddConstructor<WinTag>();
    return tid;
  }

  TypeId GetInstanceTypeId() const override { return GetTypeId(); }

  uint32_t GetSerializedSize() const override { return 1 + 4; }

  void Serialize(TagBuffer i) const override
  {
    i.WriteU8(m_cell);
    i.WriteU32(m_win);
  }

  void Deserialize(TagBuffer i) override
  {
    m_cell = i.ReadU8();
    m_win  = i.ReadU32();
  }

  void Print(std::ostream& os) const override
  {
    os << "cell=" << unsigned(m_cell) << " win=" << m_win;
  }

  uint8_t GetCell() const { return m_cell; }
  uint32_t GetWin() const { return m_win; }

private:
  uint8_t  m_cell{0};
  uint32_t m_win{0};
};

// ------------------------ Window bookkeeping ------------------------

struct WinState
{
  bool open{false};
  uint32_t winId{0};
  uint64_t tStartUs{0};
};

struct WindowInfo
{
  uint32_t epoch{0};
  uint32_t winId{0};
  uint64_t tStartUs{0};
  uint64_t tEndUs{0};
  uint64_t durUs{0};
};

// ------------------------ Runtime CBR Sender (with tagging) ------------------------

class CellCbrSender : public Object
{
public:
  void Setup(uint8_t cellId,
             Ptr<Node> node,
             Ipv4Address dstIp,
             uint16_t dstPort,
             DataRate rate,
             uint32_t pktSize)
  {
    m_cellId = cellId;
    m_node = node;
    m_dstIp = dstIp;
    m_dstPort = dstPort;
    m_rate = rate;
    m_pktSize = pktSize;

    m_sock = Socket::CreateSocket(m_node, UdpSocketFactory::GetTypeId());
    m_sock->Connect(InetSocketAddress(m_dstIp, m_dstPort));

    const double pktBits = m_pktSize * 8.0;
    const double sec = pktBits / static_cast<double>(m_rate.GetBitRate());
    m_interval = Seconds(sec);
  }

  void Enable(uint32_t winId)
  {
    m_curWinId = winId;
    if (m_enabled) return;
    m_enabled = true;
    SendOnce();
  }

  void Disable(uint64_t windowEndUs)
  {
      m_enabled = false;

  if (m_ev.IsRunning())
  {
    Simulator::Cancel(m_ev);
  }
  }

private:
  void SendOnce()
  {
    if (!m_enabled) return;

    Ptr<Packet> p = Create<Packet>(m_pktSize);
    p->AddPacketTag(WinTag(m_cellId, m_curWinId));
    m_sock->Send(p);

    m_ev = Simulator::Schedule(m_interval, &CellCbrSender::SendOnce, this);
  }

  Ptr<Node>   m_node;
  Ptr<Socket> m_sock;
  Ipv4Address m_dstIp;
  uint16_t    m_dstPort{0};
  DataRate    m_rate{0};
  uint32_t    m_pktSize{1200};
  Time        m_interval{MilliSeconds(1)};

  uint8_t     m_cellId{0};
  uint32_t    m_curWinId{0};
  bool        m_enabled{false};
  EventId     m_ev;
};

// ------------------------ RX Attribution + Throughput sampling ------------------------

struct RxAgg
{
  // rxBytesByWin[cell][winId] = bytes
  std::vector<uint64_t> rxBytesByWin[3];

  // total RX bytes per cell for time-series
  uint64_t totalRx[3]{0,0,0};
};

static void EnsureSize(std::vector<uint64_t>& v, uint32_t idx)
{
  if (v.size() <= idx) v.resize(idx + 1, 0);
}

class RxAttributor : public Object
{
public:
  void SetAgg(RxAgg* a) { m_agg = a; }

  void HandleRead(Ptr<Socket> sock)
  {
    Address from;
    Ptr<Packet> p;

    while ((p = sock->RecvFrom(from)))
    {
      WinTag tag;
      if (p->PeekPacketTag(tag))
      {
        const uint32_t cell = tag.GetCell();
        const uint32_t win  = tag.GetWin();

        if (cell < 3 && m_agg)
        {
          EnsureSize(m_agg->rxBytesByWin[cell], win);
          m_agg->rxBytesByWin[cell][win] += p->GetSize();
          m_agg->totalRx[cell] += p->GetSize();
        }
      }
    }
  }

private:
  RxAgg* m_agg{nullptr};
};

struct TsState
{
  uint64_t lastTotalRx[3]{0,0,0};
};

static void SampleThroughput(std::ofstream* fout,
                             RxAgg* agg,
                             TsState* st,
                             uint64_t binUs)
{
  const uint64_t nowUs = Simulator::Now().GetMicroSeconds();

  for (uint32_t c = 0; c < 3; ++c)
  {
    uint64_t cur = agg->totalRx[c];
    uint64_t delta = (cur >= st->lastTotalRx[c]) ? (cur - st->lastTotalRx[c]) : 0;
    st->lastTotalRx[c] = cur;

    double thrBps = (delta * 8.0) / (binUs * 1e-6);
    (*fout) << nowUs << "," << CellName(c) << "," << delta << "," << thrBps << "\n";
  }

  Simulator::Schedule(MicroSeconds(binUs), &SampleThroughput, fout, agg, st, binUs);
}

// ------------------------ main ------------------------

int main(int argc, char** argv)
{
  std::string outDir = "/tmp/tier2p5";
  std::string patternCsv;
  std::string onRateStr = "50Mbps";
  uint32_t pktSize = 1200;
  uint64_t binUs = 1000;
  uint64_t stopMarginUs = 2000;
  uint32_t seed = 1;

  // Link delays (keep explicit so you can sanity-check propagation effects)
  std::string bottleneckDelay = "20ms";
  std::string accessDelay = "2ms";

  CommandLine cmd;
  cmd.AddValue("outDir", "Output directory", outDir);
  cmd.AddValue("PatternCsv", "Pattern CSV path (time_us,cell,action)", patternCsv);
  cmd.AddValue("onRate", "CBR data rate when a cell is ENABLE (e.g., 50Mbps)", onRateStr);
  cmd.AddValue("pktSize", "UDP packet size in bytes", pktSize);
  cmd.AddValue("binUs", "Throughput sampling bin in microseconds (0 disables)", binUs);
  cmd.AddValue("stopMarginUs", "Stop margin after last pattern event (us)", stopMarginUs);
  cmd.AddValue("seed", "RNG seed", seed);
  cmd.AddValue("bottleneckDelay", "GW<->SAT delay (e.g., 20ms)", bottleneckDelay);
  cmd.AddValue("accessDelay", "SAT<->UT delay (e.g., 2ms)", accessDelay);
  cmd.Parse(argc, argv);

  if (patternCsv.empty())
  {
    NS_FATAL_ERROR("You must provide --PatternCsv=/path/to/pattern.csv");
  }

  auto events = ReadPatternCsv(patternCsv);
  if (events.empty())
  {
    NS_FATAL_ERROR("PatternCsv has no events.");
  }

  const uint64_t lastUs = events.back().timeUs;
  const uint64_t simStopUs = lastUs + stopMarginUs;

  // mkdir -p outDir
  {
    std::string mk = "mkdir -p " + outDir;
    (void)std::system(mk.c_str());
  }

  RngSeedManager::SetSeed(seed);
  RngSeedManager::SetRun(1);

  // Topology: GW -- bottleneck -- SAT -- access -- UT_A/UT_B/UT_C
  NodeContainer gw, sat, uts;
  gw.Create(1);
  sat.Create(1);
  uts.Create(3);

  InternetStackHelper internet;
  internet.Install(gw);
  internet.Install(sat);
  internet.Install(uts);

  PointToPointHelper p2pBottleneck;
  p2pBottleneck.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
  p2pBottleneck.SetChannelAttribute("Delay", StringValue(bottleneckDelay));

  PointToPointHelper p2pAccess;
  p2pAccess.SetDeviceAttribute("DataRate", StringValue("1Gbps"));
  p2pAccess.SetChannelAttribute("Delay", StringValue(accessDelay));

  NetDeviceContainer devGwSat = p2pBottleneck.Install(gw.Get(0), sat.Get(0));

  NetDeviceContainer devSatUt[3];
  for (uint32_t i = 0; i < 3; ++i)
  {
    devSatUt[i] = p2pAccess.Install(sat.Get(0), uts.Get(i));
  }

  Ipv4AddressHelper ip;

  ip.SetBase("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer ifGwSat = ip.Assign(devGwSat);
  (void)ifGwSat;

  Ipv4InterfaceContainer ifSatUt[3];
  for (uint32_t i = 0; i < 3; ++i)
  {
    std::ostringstream base;
    base << "10.0." << (i + 1) << ".0";
    ip.SetBase(base.str().c_str(), "255.255.255.0");
    ifSatUt[i] = ip.Assign(devSatUt[i]);
  }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Ports per cell
  const uint16_t ports[3] = {5001, 5002, 5003};

  // RX aggregation (tag attribution)
  RxAgg agg;

  // Receiver sockets on UTs
  Ptr<RxAttributor> rxAttr = CreateObject<RxAttributor>();
rxAttr->SetAgg(&agg);

for (uint32_t c = 0; c < 3; ++c)
{
  Ptr<Socket> rx = Socket::CreateSocket(uts.Get(c), UdpSocketFactory::GetTypeId());
  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), ports[c]);
  rx->Bind(local);

  rx->SetRecvCallback(MakeCallback(&RxAttributor::HandleRead, rxAttr));
}

  // Runtime-controlled senders on GW
  DataRate onRate(onRateStr);

  Ptr<CellCbrSender> sender[3];
  for (uint32_t c = 0; c < 3; ++c)
  {
    sender[c] = CreateObject<CellCbrSender>();
    const Ipv4Address utIp = ifSatUt[c].GetAddress(1);
    sender[c]->Setup(static_cast<uint8_t>(c), gw.Get(0), utIp, ports[c], onRate, pktSize);
  }

  // Output files
  std::ofstream evLog((outDir + "/beam_hop_events.log").c_str(), std::ios::out);
  std::ofstream winCsv((outDir + "/tier23_window_metrics.csv").c_str(), std::ios::out);
  std::ofstream thrCsv;

  if (!evLog.is_open()) NS_FATAL_ERROR("Cannot open " << outDir << "/beam_hop_events.log");
  if (!winCsv.is_open()) NS_FATAL_ERROR("Cannot open " << outDir << "/tier23_window_metrics.csv");

  evLog << "# time_us,cell,action,epoch,win_id,reason\n";
  winCsv << "epoch,cell,win_id,t_start_us,t_end_us,dur_us,rx_bytes,thr_mbps\n";
  winCsv << std::fixed << std::setprecision(6);

  TsState ts;
  if (binUs > 0)
  {
    thrCsv.open((outDir + "/throughput_per_cell.csv").c_str(), std::ios::out);
    if (!thrCsv.is_open()) NS_FATAL_ERROR("Cannot open " << outDir << "/throughput_per_cell.csv");
    thrCsv << "time_us,cell,rx_bytes,thr_bps\n";
    Simulator::Schedule(MicroSeconds(binUs), &SampleThroughput, &thrCsv, &agg, &ts, binUs);
  }

  // Window state + finalized windows list
  std::map<uint32_t, WinState> ws;
  std::vector<WindowInfo> windows[3];

  uint32_t epoch = 0;

  auto OnEnable = [&](uint32_t cellId, const std::string& reason) {
    const uint64_t nowUs = Simulator::Now().GetMicroSeconds();

    ws[cellId].open = true;
    ws[cellId].tStartUs = nowUs;

    // Ensure rx vector has slot for this winId (so tag attribution won't overflow)
    EnsureSize(agg.rxBytesByWin[cellId], ws[cellId].winId);

    // Enable sender with current winId
    sender[cellId]->Enable(ws[cellId].winId);

    evLog << nowUs << "," << CellName(cellId) << ",ENABLE," 
      << epoch << "," << ws[cellId].winId << "," 
      << reason << "\n";
  };

  auto OnDisable = [&](uint32_t cellId, const std::string& reason) {
    const uint64_t nowUs = Simulator::Now().GetMicroSeconds();

    sender[cellId]->Disable(nowUs);

    uint32_t endedWinId = ws[cellId].winId;  // default

    if (ws[cellId].open)
    {
     endedWinId = ws[cellId].winId;         // win that is ending

     WindowInfo wi;
     wi.epoch = epoch;
     wi.winId = ws[cellId].winId;
     wi.tStartUs = ws[cellId].tStartUs;
     wi.tEndUs = nowUs;
     wi.durUs = (nowUs > ws[cellId].tStartUs) ? (nowUs - ws[cellId].tStartUs) : 0;

     windows[cellId].push_back(wi);

     ws[cellId].winId++;                    // advance for next window
     ws[cellId].open = false;
}

evLog << nowUs << "," << CellName(cellId) << ",DISABLE,"
      << epoch << "," << endedWinId << "," << reason << "\n";
epoch++;
  };

  // Schedule pattern events
  for (const auto& e : events)
  {
    if (e.enable)
    {
      Simulator::Schedule(MicroSeconds(e.timeUs), [&, e](){ OnEnable(e.cellId, "pattern"); });
    }
    else
    {
      Simulator::Schedule(MicroSeconds(e.timeUs), [&, e](){ OnDisable(e.cellId, "pattern"); });
    }
  }

  Simulator::Stop(MicroSeconds(simStopUs));
  Simulator::Run();
  Simulator::Destroy();

  // Write window-attributed metrics (bytes attributed by WinTag)
  for (uint32_t c = 0; c < 3; ++c)
  {
    for (const auto& wi : windows[c])
    {
      uint64_t rxBytes = 0;
      if (agg.rxBytesByWin[c].size() > wi.winId)
      {
        rxBytes = agg.rxBytesByWin[c][wi.winId];
      }
      const double thrMbps = (wi.durUs > 0)
        ? (rxBytes * 8.0 / (wi.durUs * 1e-6) / 1e6)
        : 0.0;

      winCsv << wi.epoch << "," << CellName(c) << "," << wi.winId << ","
             << wi.tStartUs << "," << wi.tEndUs << "," << wi.durUs << ","
             << rxBytes << "," << thrMbps << "\n";
    }
  }

  if (binUs > 0) thrCsv.close();
  winCsv.close();
  evLog.close();

  NS_LOG_UNCOND("[W25] outDir=" << outDir);
  NS_LOG_UNCOND("[W25] wrote: " << outDir << "/beam_hop_events.log");
  NS_LOG_UNCOND("[W25] wrote: " << outDir << "/tier23_window_metrics.csv");
  if (binUs > 0) NS_LOG_UNCOND("[W25] wrote: " << outDir << "/throughput_per_cell.csv");

  return 0;
}
