# Beam Hopping Controller
- selecting cells to serve in each BH window
- allocating dwell time to each selected cell
- scheduling beam switching events

## Model
- Active Beam Model : Single active beam per satellite
- BH Window Model : Σ dwell_time(cell) + switching overhead
- Maximum Cells per Window : 3
- Minimum Dwell Time : min_dwell = 1 ms
- Beam Switching Overhead : switch_time = 50 us
- Cell Selection Rule : Demand-priority selection + minimum service protection
- Dwell Allocation Rule :After cells are selected
  - 1. Each selected cell receives:min_dwell = 1 ms
  - 2. Remaining service time is distributed proportionally to demand score.
`dwell(cell)= min_dwell + proportional_remainder `

## Control Loop
```
1. 初始化 satellite / traffic / scheduler / cell mapping
2. 初始化 BH controller 狀態
3. 讀取各 cell demand
4. 選出本輪 window 的 cells
5. 分配 dwell time
6. 產生 switch-aware schedule
7. 執行 beam gating
8. 寫出 logs
9. window 結束後重新做下一輪
```
## Before BH window works
1. forward link traffic path
2. cell concept`flow / ue / queue → cell mapping`
3. init BH controller
 - current window id
 - per-cell not-served counter
 - current active cell
 - switch time
 - min dwell
 - max cells per window

## As BH window start
1. collect cell demand
2. decide cell in window
 - demand score:top 3
3. dwell time : dwell(cell) = 1 ms + proportional remainder
4. BuildWindowSchedule

## As BH window works
1.beam gating / active cell control
- ActivateCell(cellId)
- DeactivateAll()
- IsCellActive(cellId)
2. trig switch event

## After BH window works
1. update fairness (avoid cell always not served)
2. output
  - cell_demand_snapshot.csv
  - beam_schedule.csv
  - beam_hop_events.log
3. Read demand，build next window


```
Simulation start
    ↓
Initialize BH state
    ↓
RunNextWindow()
    ↓
ReadCellDemand()
    ↓
SelectCellsForWindow()
    ↓
AllocateDwellTimes()
    ↓
BuildWindowSchedule()
    ↓
Emit demand / schedule logs
    ↓
Execute dwell / switch events
    ↓
Update not-served counters
    ↓
Schedule next RunNextWindow()
```
|函數|說明|
|-|-|
|InitializeCellMapping()|建立 cell ID、userCount 對應表|
|InitializeBhController()|狀態機重置為 BH_INIT|
|ReadCellDemands()|讀取（或模擬）各 cell 的 Mbps 需求|
|SelectCellsForWindow()|依 demand 降序選出本輪 top-N cells|
|AllocateDwellTime()|按 demand 比例分配 dwell time，強制最小值|
|GenerateSwitchAwareSchedule()|插入 guard interval 產生完整 slot 序列|
|ExecuteBeamGating()|非同步 Simulator::Schedule 觸發每個 slot|
|WriteWindowLog()|每輪結束輸出表格到 .log 檔|
|RunNextWindow()|自動排程下一輪，形成週期迴圈|
