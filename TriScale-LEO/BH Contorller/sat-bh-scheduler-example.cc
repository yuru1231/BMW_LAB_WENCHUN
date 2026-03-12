/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * sat-bh-scheduler-example.cc
 *
 * Beam-Hopping Scheduler with Backhaul Controller
 *
 * 實作流程：
 *   1. 初始化 satellite / traffic / scheduler / cell mapping
 *   2. 初始化 BH controller 狀態
 *   3. 讀取各 cell demand
 *   4. 選出本輪 window 的 cells
 *   5. 分配 dwell time
 *   6. 產生 switch-aware schedule
 *   7. 執行 beam gating
 *   8. 寫出 logs
 *   9. window 結束後重新做下一輪
 *
 * 使用方式:
 *   ./ns3 run "sat-bh-scheduler-example
 *               --numBeams=7
 *               --windowDuration=0.1
 *               --simDuration=10.0
 *               --minDwellTime=0.005
 *               --switchGuardTime=0.001"
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/satellite-module.h"
#include "ns3/traffic-module.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SatBhSchedulerExample");

// ============================================================
//  資料結構定義
// ============================================================

/**
 * @brief 單一 Cell 的狀態與需求資訊
 */
struct CellInfo
{
    uint32_t cellId;        ///< Cell / Beam ID
    double demandMbps;      ///< 當前需求 (Mbps)
    double allocatedMbps;   ///< 本輪已分配容量
    double dwellTimeSec;    ///< 本輪分配到的 dwell time (秒)
    bool scheduledThisWindow; ///< 本輪是否被排程
    uint32_t userCount;     ///< 該 cell 下 UT 數量

    CellInfo()
        : cellId(0),
          demandMbps(0.0),
          allocatedMbps(0.0),
          dwellTimeSec(0.0),
          scheduledThisWindow(false),
          userCount(0)
    {
    }
};

/**
 * @brief 單一排程 slot（beam gating 的基本單元）
 */
struct ScheduleSlot
{
    uint32_t cellId;        ///< 這個 slot 服務的 Cell
    double startTimeSec;    ///< slot 開始時間（相對於 window 起點）
    double durationSec;     ///< slot 持續時間
    bool isGuard;           ///< 是否為 switch guard interval

    ScheduleSlot(uint32_t id, double start, double dur, bool guard = false)
        : cellId(id), startTimeSec(start), durationSec(dur), isGuard(guard)
    {
    }
};

/**
 * @brief BH Controller 整體狀態機
 */
enum BhControllerState
{
    BH_INIT,        ///< 初始化
    BH_DEMAND_READ, ///< 讀取 demand
    BH_SCHEDULING,  ///< 排程計算中
    BH_EXECUTING,   ///< 執行 gating
    BH_LOGGING,     ///< 寫出 log
    BH_WINDOW_END   ///< 本輪結束，準備下一輪
};

// ============================================================
//  BeamHoppingController 類別
// ============================================================

class BeamHoppingController
{
  public:
    BeamHoppingController(uint32_t numBeams,
                          double windowDurationSec,
                          double minDwellSec,
                          double switchGuardSec,
                          double totalCapacityMbps);

    // --- 主要流程函數 ---

    /** Step 1: 初始化 cell mapping 與內部狀態 */
    void InitializeCellMapping();

    /** Step 2: 重置 BH controller 至初始狀態 */
    void InitializeBhController();

    /** Step 3: 讀取（或模擬）各 cell demand */
    void ReadCellDemands(double simTimeSec);

    /** Step 4: 根據 demand 選出本輪要服務的 cells */
    void SelectCellsForWindow();

    /** Step 5: 對選出的 cells 分配 dwell time */
    void AllocateDwellTime();

    /** Step 6: 產生 switch-aware 的時序 schedule */
    void GenerateSwitchAwareSchedule();

    /** Step 7: 執行 beam gating（觸發 ns3 事件） */
    void ExecuteBeamGating(uint32_t slotIndex);

    /** Step 8: 寫出本輪 log */
    void WriteWindowLog(uint32_t windowIndex, double windowStartSec);

    /** Step 9: 進入下一輪（由 Simulator::Schedule 觸發） */
    void RunNextWindow();

    // --- 工具函數 ---
    void SetLogFile(const std::string& filename);
    void PrintScheduleSummary() const;
    uint32_t GetWindowCount() const { return m_windowCount; }
    BhControllerState GetState() const { return m_state; }

  private:
    // ---- 設定參數 ----
    uint32_t m_numBeams;
    double m_windowDurationSec;
    double m_minDwellSec;
    double m_switchGuardSec;
    double m_totalCapacityMbps;

    // ---- 狀態 ----
    BhControllerState m_state;
    uint32_t m_windowCount;
    double m_currentWindowStart;

    // ---- Cell 資料 ----
    std::vector<CellInfo> m_cells;
    std::vector<uint32_t> m_selectedCells; ///< 本輪選出的 cell IDs

    // ---- Schedule ----
    std::vector<ScheduleSlot> m_schedule; ///< 本輪 slot 序列

    // ---- Log ----
    std::string m_logFilename;
    std::ofstream m_logFile;

    // ---- 私有輔助 ----
    double SimulateDemand(uint32_t cellId, double timeSec) const;
    std::string StateToString(BhControllerState s) const;
    void LogLine(const std::string& line);
};

// ============================================================
//  BeamHoppingController 實作
// ============================================================

BeamHoppingController::BeamHoppingController(uint32_t numBeams,
                                             double windowDurationSec,
                                             double minDwellSec,
                                             double switchGuardSec,
                                             double totalCapacityMbps)
    : m_numBeams(numBeams),
      m_windowDurationSec(windowDurationSec),
      m_minDwellSec(minDwellSec),
      m_switchGuardSec(switchGuardSec),
      m_totalCapacityMbps(totalCapacityMbps),
      m_state(BH_INIT),
      m_windowCount(0),
      m_currentWindowStart(0.0)
{
}

// ------ Step 1 -----------------------------------------------
void
BeamHoppingController::InitializeCellMapping()
{
    NS_LOG_FUNCTION(this);
    m_cells.clear();
    m_cells.resize(m_numBeams);

    for (uint32_t i = 0; i < m_numBeams; ++i)
    {
        m_cells[i].cellId = i + 1; // Cell ID 從 1 開始
        m_cells[i].demandMbps = 0.0;
        m_cells[i].allocatedMbps = 0.0;
        m_cells[i].dwellTimeSec = 0.0;
        m_cells[i].scheduledThisWindow = false;
        // 模擬每個 cell 有 1~5 個 UT
        m_cells[i].userCount = 1 + (i % 5);
    }

    NS_LOG_INFO("[Init] Cell mapping 完成，共 " << m_numBeams << " 個 beams/cells");
}

// ------ Step 2 -----------------------------------------------
void
BeamHoppingController::InitializeBhController()
{
    NS_LOG_FUNCTION(this);

    m_state = BH_INIT;
    m_windowCount = 0;
    m_currentWindowStart = 0.0;
    m_selectedCells.clear();
    m_schedule.clear();

    NS_LOG_INFO("[BH Init] Controller 狀態重置完成，State=" << StateToString(m_state));
}

// ------ Step 3 -----------------------------------------------
void
BeamHoppingController::ReadCellDemands(double simTimeSec)
{
    NS_LOG_FUNCTION(this << simTimeSec);
    m_state = BH_DEMAND_READ;

    for (auto& cell : m_cells)
    {
        cell.demandMbps = SimulateDemand(cell.cellId, simTimeSec);
    }

    // 輸出 demand 摘要
    double totalDemand = 0.0;
    for (const auto& cell : m_cells)
    {
        totalDemand += cell.demandMbps;
        NS_LOG_DEBUG("[Demand] Cell " << cell.cellId
                                      << " demand=" << std::fixed << std::setprecision(2)
                                      << cell.demandMbps << " Mbps");
    }
    NS_LOG_INFO("[Demand] 總需求=" << std::fixed << std::setprecision(2)
                                    << totalDemand << " Mbps"
                                    << "，容量=" << m_totalCapacityMbps << " Mbps");
}

// ------ Step 4 -----------------------------------------------
void
BeamHoppingController::SelectCellsForWindow()
{
    NS_LOG_FUNCTION(this);
    m_state = BH_SCHEDULING;
    m_selectedCells.clear();

    // 計算本 window 可用的有效時間（扣掉 guard time overhead）
    // 最多可排 floor(windowDuration / (minDwell + guard)) 個 cells
    uint32_t maxSlots =
        static_cast<uint32_t>(m_windowDurationSec / (m_minDwellSec + m_switchGuardSec));
    maxSlots = std::min(maxSlots, m_numBeams);

    // 策略：依 demand 降序排列，選 top-N
    std::vector<uint32_t> indices(m_numBeams);
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [this](uint32_t a, uint32_t b) {
        return m_cells[a].demandMbps > m_cells[b].demandMbps;
    });

    for (uint32_t rank = 0; rank < maxSlots; ++rank)
    {
        uint32_t idx = indices[rank];
        if (m_cells[idx].demandMbps > 0.0)
        {
            m_selectedCells.push_back(m_cells[idx].cellId);
        }
    }

    NS_LOG_INFO("[Select] Window #" << m_windowCount
                                     << " 選出 " << m_selectedCells.size()
                                     << " 個 cells（maxSlots=" << maxSlots << "）");
}

// ------ Step 5 -----------------------------------------------
void
BeamHoppingController::AllocateDwellTime()
{
    NS_LOG_FUNCTION(this);

    if (m_selectedCells.empty())
    {
        NS_LOG_WARN("[Alloc] 無選出 cell，跳過 dwell time 分配");
        return;
    }

    // 可用總時間 = windowDuration - (N cells * switchGuard)
    uint32_t N = static_cast<uint32_t>(m_selectedCells.size());
    double usableTime = m_windowDurationSec - N * m_switchGuardSec;
    if (usableTime <= 0)
    {
        NS_LOG_WARN("[Alloc] 可用時間不足，請減少 cells 數或加大 window duration");
        return;
    }

    // 計算各 cell 的 demand 比例，依比例分配 dwell time
    double totalSelectedDemand = 0.0;
    for (uint32_t cid : m_selectedCells)
    {
        totalSelectedDemand += m_cells[cid - 1].demandMbps;
    }

    for (uint32_t cid : m_selectedCells)
    {
        CellInfo& cell = m_cells[cid - 1];
        double ratio = (totalSelectedDemand > 0) ? cell.demandMbps / totalSelectedDemand : 1.0 / N;
        double dwell = usableTime * ratio;

        // 強制下限
        dwell = std::max(dwell, m_minDwellSec);
        cell.dwellTimeSec = dwell;
        cell.scheduledThisWindow = true;

        // 估算分配到的容量（假設固定 bps/Hz * dwell 比例）
        cell.allocatedMbps = (dwell / m_windowDurationSec) * m_totalCapacityMbps;

        NS_LOG_DEBUG("[Alloc] Cell " << cid
                                      << " dwell=" << std::fixed << std::setprecision(4)
                                      << dwell << "s"
                                      << " allocated=" << std::setprecision(2)
                                      << cell.allocatedMbps << " Mbps");
    }
}

// ------ Step 6 -----------------------------------------------
void
BeamHoppingController::GenerateSwitchAwareSchedule()
{
    NS_LOG_FUNCTION(this);
    m_schedule.clear();

    double cursor = 0.0; // 相對於 window 起點的時間指針

    for (uint32_t cid : m_selectedCells)
    {
        const CellInfo& cell = m_cells[cid - 1];

        // 加入實際服務 slot
        m_schedule.emplace_back(cid, cursor, cell.dwellTimeSec, false);
        cursor += cell.dwellTimeSec;

        // 加入 switch guard interval（最後一個 slot 後也加，避免跨 window 干擾）
        m_schedule.emplace_back(0, cursor, m_switchGuardSec, true); // cellId=0 表示 guard
        cursor += m_switchGuardSec;
    }

    NS_LOG_INFO("[Schedule] Window #" << m_windowCount
                                       << " 共 " << m_schedule.size()
                                       << " slots (含 guard)，總時間="
                                       << std::fixed << std::setprecision(4)
                                       << cursor << "s / " << m_windowDurationSec << "s");
}

// ------ Step 7 -----------------------------------------------
void
BeamHoppingController::ExecuteBeamGating(uint32_t slotIndex)
{
    NS_LOG_FUNCTION(this << slotIndex);

    if (slotIndex >= m_schedule.size())
    {
        NS_LOG_DEBUG("[Gating] 所有 slots 執行完畢");
        return;
    }

    const ScheduleSlot& slot = m_schedule[slotIndex];
    double absoluteStart = m_currentWindowStart + slot.startTimeSec;

    if (slot.isGuard)
    {
        NS_LOG_DEBUG("[Gating] [Guard] t=" << std::fixed << std::setprecision(4)
                                            << absoluteStart
                                            << " dur=" << slot.durationSec << "s");
    }
    else
    {
        NS_LOG_INFO("[Gating] [ON ] Cell " << slot.cellId
                                            << " t=" << std::fixed << std::setprecision(4)
                                            << absoluteStart
                                            << " dur=" << slot.durationSec << "s");

        // --------------------------------------------------------
        //  ★ 在這裡呼叫實際的 SNS3 beam gating API，例如：
        //      Ptr<SatBeamScheduler> beamSched = ...;
        //      beamSched->SetBeamEnabled(slot.cellId, true);
        //
        //  或透過 SatHelper / SatBeamHelper 取得對應的 GW PHY
        //  並呼叫 EnableBeam() / DisableBeam()
        // --------------------------------------------------------
    }

    // 排程下一個 slot 的 gating 事件
    if (slotIndex + 1 < m_schedule.size())
    {
        const ScheduleSlot& nextSlot = m_schedule[slotIndex + 1];
        double delay = nextSlot.startTimeSec - slot.startTimeSec;
        Simulator::Schedule(Seconds(delay),
                            &BeamHoppingController::ExecuteBeamGating,
                            this,
                            slotIndex + 1);
    }
}

// ------ Step 8 -----------------------------------------------
void
BeamHoppingController::WriteWindowLog(uint32_t windowIndex, double windowStartSec)
{
    NS_LOG_FUNCTION(this << windowIndex);

    std::ostringstream oss;
    oss << "=== Window #" << windowIndex
        << " | t=" << std::fixed << std::setprecision(3) << windowStartSec << "s ===\n";

    oss << std::left
        << std::setw(8) << "CellID"
        << std::setw(14) << "Demand(Mbps)"
        << std::setw(16) << "Allocated(Mbps)"
        << std::setw(14) << "Dwell(ms)"
        << std::setw(10) << "Sched?"
        << "\n";
    oss << std::string(62, '-') << "\n";

    for (const auto& cell : m_cells)
    {
        oss << std::left
            << std::setw(8)  << cell.cellId
            << std::setw(14) << std::fixed << std::setprecision(2) << cell.demandMbps
            << std::setw(16) << cell.allocatedMbps
            << std::setw(14) << std::setprecision(1) << (cell.dwellTimeSec * 1000.0)
            << std::setw(10) << (cell.scheduledThisWindow ? "YES" : "no")
            << "\n";
    }

    // 輸出 slot 序列
    oss << "\nSlot sequence:\n";
    for (size_t i = 0; i < m_schedule.size(); ++i)
    {
        const auto& s = m_schedule[i];
        if (s.isGuard)
        {
            oss << "  [" << i << "] GUARD  dur=" << std::setprecision(3) << s.durationSec << "s\n";
        }
        else
        {
            oss << "  [" << i << "] Cell " << s.cellId
                << "  start=" << s.startTimeSec
                << "s  dur=" << s.durationSec << "s\n";
        }
    }
    oss << "\n";

    LogLine(oss.str());
    NS_LOG_INFO("[Log] Window #" << windowIndex << " log 已寫出");
}

// ------ Step 9 -----------------------------------------------
void
BeamHoppingController::RunNextWindow()
{
    NS_LOG_FUNCTION(this);

    m_state = BH_WINDOW_END;
    m_currentWindowStart = Simulator::Now().GetSeconds();

    NS_LOG_INFO("========================================");
    NS_LOG_INFO("[Window] 開始 Window #" << m_windowCount
                                          << " @ t=" << std::fixed << std::setprecision(3)
                                          << m_currentWindowStart << "s");

    // 重置本輪 cell 狀態
    for (auto& cell : m_cells)
    {
        cell.allocatedMbps = 0.0;
        cell.dwellTimeSec = 0.0;
        cell.scheduledThisWindow = false;
    }

    // 執行各步驟
    ReadCellDemands(m_currentWindowStart);   // Step 3
    SelectCellsForWindow();                   // Step 4
    AllocateDwellTime();                      // Step 5
    GenerateSwitchAwareSchedule();            // Step 6
    ExecuteBeamGating(0);                     // Step 7（非同步觸發）
    WriteWindowLog(m_windowCount,             // Step 8
                   m_currentWindowStart);

    m_state = BH_EXECUTING;
    m_windowCount++;

    // Step 9: 排程下一輪
    Simulator::Schedule(Seconds(m_windowDurationSec),
                        &BeamHoppingController::RunNextWindow,
                        this);
}

// ------ 工具函數 ---------------------------------------------

double
BeamHoppingController::SimulateDemand(uint32_t cellId, double timeSec) const
{
    // 用正弦波模擬時變需求，各 cell 相位不同
    // 實際部署時換成從 SatStatsHelper / DAMA 讀取
    double phase = (cellId - 1) * 0.7;
    double base = 10.0 + (cellId % 4) * 5.0; // 10~25 Mbps 基礎
    double variation = 8.0 * std::sin(2.0 * M_PI * timeSec / 5.0 + phase);
    return std::max(0.0, base + variation);
}

std::string
BeamHoppingController::StateToString(BhControllerState s) const
{
    switch (s)
    {
    case BH_INIT:        return "INIT";
    case BH_DEMAND_READ: return "DEMAND_READ";
    case BH_SCHEDULING:  return "SCHEDULING";
    case BH_EXECUTING:   return "EXECUTING";
    case BH_LOGGING:     return "LOGGING";
    case BH_WINDOW_END:  return "WINDOW_END";
    default:             return "UNKNOWN";
    }
}

void
BeamHoppingController::SetLogFile(const std::string& filename)
{
    m_logFilename = filename;
    m_logFile.open(filename, std::ios::out | std::ios::trunc);
    if (!m_logFile.is_open())
    {
        NS_LOG_ERROR("[Log] 無法開啟 log 檔案：" << filename);
    }
    else
    {
        NS_LOG_INFO("[Log] Log 檔案：" << filename);
        m_logFile << "# BH Scheduler Log\n"
                  << "# Generated by sat-bh-scheduler-example\n\n";
    }
}

void
BeamHoppingController::LogLine(const std::string& line)
{
    if (m_logFile.is_open())
    {
        m_logFile << line;
        m_logFile.flush();
    }
    // 同時印到 ns3 log
    NS_LOG_INFO(line);
}

void
BeamHoppingController::PrintScheduleSummary() const
{
    std::cout << "\n[Summary] 總共執行 " << m_windowCount << " 個 windows\n";
    std::cout << "[Summary] 最後一輪排程 slots：\n";
    for (const auto& s : m_schedule)
    {
        if (!s.isGuard)
        {
            std::cout << "  Cell " << s.cellId
                      << "  dwell=" << std::fixed << std::setprecision(4)
                      << s.durationSec << "s\n";
        }
    }
}

// ============================================================
//  main()
// ============================================================

int
main(int argc, char* argv[])
{
    // ---- 命令列參數 ----
    uint32_t numBeams = 7;
    double windowDuration = 0.1;   // 100ms per window
    double simDuration = 10.0;     // 總模擬時長 (秒)
    double minDwellTime = 0.005;   // 最小 dwell time = 5ms
    double switchGuardTime = 0.001;// switch guard = 1ms
    double totalCapacity = 200.0;  // 總衛星容量 (Mbps)
    std::string logFile = "bh-scheduler.log";
    bool verbose = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("numBeams",       "Beam / Cell 數量",          numBeams);
    cmd.AddValue("windowDuration", "BH window 長度 (秒)",        windowDuration);
    cmd.AddValue("simDuration",    "模擬總時長 (秒)",             simDuration);
    cmd.AddValue("minDwellTime",   "最小 dwell time (秒)",        minDwellTime);
    cmd.AddValue("switchGuardTime","Beam switch guard time (秒)", switchGuardTime);
    cmd.AddValue("totalCapacity",  "衛星總容量 (Mbps)",           totalCapacity);
    cmd.AddValue("logFile",        "輸出 log 檔案名稱",           logFile);
    cmd.AddValue("verbose",        "開啟詳細 NS_LOG 輸出",        verbose);
    cmd.Parse(argc, argv);

    // ---- Log 設定 ----
    if (verbose)
    {
        LogComponentEnable("SatBhSchedulerExample", LOG_LEVEL_DEBUG);
    }
    else
    {
        LogComponentEnable("SatBhSchedulerExample", LOG_LEVEL_INFO);
    }

    // ---- 參數驗證 ----
    NS_ASSERT_MSG(numBeams > 0, "numBeams 必須 > 0");
    NS_ASSERT_MSG(windowDuration > 0, "windowDuration 必須 > 0");
    NS_ASSERT_MSG(minDwellTime < windowDuration, "minDwellTime 必須小於 windowDuration");
    NS_ASSERT_MSG(switchGuardTime < minDwellTime, "switchGuardTime 必須小於 minDwellTime");

    NS_LOG_INFO("=========================================");
    NS_LOG_INFO(" BH Scheduler Example");
    NS_LOG_INFO(" numBeams       = " << numBeams);
    NS_LOG_INFO(" windowDuration = " << windowDuration << " s");
    NS_LOG_INFO(" simDuration    = " << simDuration << " s");
    NS_LOG_INFO(" minDwellTime   = " << minDwellTime << " s");
    NS_LOG_INFO(" switchGuardTime= " << switchGuardTime << " s");
    NS_LOG_INFO(" totalCapacity  = " << totalCapacity << " Mbps");
    NS_LOG_INFO("=========================================");

    // ============================================================
    //  Step 1: 初始化 BH Controller + Cell Mapping
    // ============================================================
    BeamHoppingController bhController(numBeams,
                                       windowDuration,
                                       minDwellTime,
                                       switchGuardTime,
                                       totalCapacity);

    bhController.SetLogFile(logFile);
    bhController.InitializeCellMapping();  // Step 1

    // ============================================================
    //  Step 2: 初始化 BH controller 狀態
    // ============================================================
    bhController.InitializeBhController(); // Step 2

    // ============================================================
    //  ★ SNS3 衛星拓樸初始化區塊
    //
    //  如果你有完整的 SatHelper，在這裡加入：
    //
    //  Ptr<SatHelper> helper = CreateObject<SatHelper>();
    //  helper->CreatePredefinedScenario(SatHelper::FULL);
    //
    //  並將 helper 的 beam scheduler 指標傳入 bhController
    //  供 Step 7 的 ExecuteBeamGating 呼叫實際 API
    // ============================================================

    // ============================================================
    //  排程第一個 window（Steps 3~9 會週期性自動執行）
    // ============================================================
    Simulator::Schedule(Seconds(0.0),
                        &BeamHoppingController::RunNextWindow,
                        &bhController);

    // ---- 執行模擬 ----
    Simulator::Stop(Seconds(simDuration));
    NS_LOG_INFO("[Sim] 開始模擬，總時長=" << simDuration << "s");
    Simulator::Run();

    // ---- 輸出最終摘要 ----
    bhController.PrintScheduleSummary();
    NS_LOG_INFO("[Sim] 模擬結束，共完成 " << bhController.GetWindowCount() << " 個 windows");

    Simulator::Destroy();
    return 0;
}
