# 系統分層
```
Routing abstraction      → Tier1:LEO 動態路由不穩定
Resource control         → Tier2:有限 beam / resource allocation； Tier2.5:control plane 是否真的影響 traffic
Data plane verification  → Tier3 :QoS policy 是否成立
```
# 資料流
```
LEO Scenario
      │
      ▼
Tier1-A1  Position Timeseries
      │
      ▼
Tier1-A2  Connectivity Graph
      │
      ▼
Tier1-A3  Stable Path Selection
      │
      ▼
Tier1-B   Golden Path / Pruned Set
      │
      ▼
Tier2     Beam/Cell Time Pattern
      │
      ▼
Tier2.5   Window / Gate / Throughput validation
      │
      ▼
Tier3     QoS traffic validation
```
# I/O
## tier 1
###  Node position timeseries
[sat-constellation-example]()

輸出:
- sat_positions_xyz_timeseries.csv
- gw_positions_xyz_timeseries.csv
- ut_positions_xyz_timeseries.csv

###  build connectivity graph
[build_connectivity.py]()

[meta.json]()

輸出:
- connectivity_timeseries.csv
 - head:time_sec, src, dst, edge_type

### SRC_NODE → DST_NODE:最穩路徑
`score = -hop_count + continuity_bonus`
[a3_stable_path.py]()

輸入:
connectivity_timeseries.csv

輸出:
- best_path_timeseries.csv
- golden_path_summary.json

### Routing abstraction finalization
[phase_b_extract_golden_path.py]()

輸出:
- golden_path.txt
- pruned_node_set.txt
- pruned_link_set.txt

# tier 2
時間: 服務 cell → gate enable / disable
pattern:
```
t_us, cell, enabled
0,A,1
5000,A,0
5000,B,1
```
驗證 gate:[w3_gate_smoke.cc]()
驗證 pattern reader:[w4_pattern_smoke.cc]()

## tier 2.5 Window Metrics
Control plane → Data plane bridge
[w25_tier2p5_window_metrics.cc]()
輸出:
- beam_hop_events.log
- throughput_per_cell.csv
- window_metrics.csv

## tier 3 QoS Validation
建立 traffic classes：Gold/Silver/Bronze
[w3_tier3_qos_singlecell.cc]()

輸出:
- qos_metrics.csv

# Flow chart
```mermaid
flowchart TB

subgraph T1["Tier 1 — Routing Abstraction"]
    A1["A1: run/run.sh
用途: 呼叫 sat-constellation-example 產生位置時序
輸出: SAT/GW/UT xyz traces"]

    A1E["env.sh
用途: 設定 scenario / T0 / T1 / DT / output path"]

    A1O["輸出檔
sat_positions_xyz_timeseries.csv
gw_positions_xyz_timeseries.csv
ut_positions_xyz_timeseries.csv"]

    A2["A2: build_connectivity.py
用途: 從位置 traces 建立 connectivity graph
方法: GSL elevation + hysteresis + static ISL"]

    A2M["meta.json
用途: 定義 A2 的輸入路徑與 connectivity policy"]

    A2O["輸出檔
connectivity_timeseries.csv"]

    A3["A3: a3_stable_path.py
用途: 從 connectivity graph 找穩定 best path
方法: BFS shortest paths + continuity bonus"]

    A3E["env.sh
用途: 設定 SRC/DST/continuity bonus"]

    A3O["輸出檔
best_path_timeseries.csv
golden_path_summary.json"]

    A4["Tier1 Finalization:
phase_b_extract_golden_path.py
用途: 從 best_path_timeseries 萃取 golden path 與 pruning sets"]

    A4O["輸出檔
golden_path.txt
pruned_node_set.txt
pruned_link_set.txt
golden_path_summary.txt"]
end

subgraph T2["Tier 2 — Beam / Cell Control"]
    B0["Tier2 Pattern Generator
用途: 產生最小化 beam/cell 時序 pattern
輸出: pattern.csv"]

    B1["pattern_reader.h
用途: 讀取 pattern.csv"]

    B2["beam_gate.h / flag_gate.h
用途: 根據 pattern 控制 cell enable/disable"]

    B3["app_gate_startstop.h
用途: 控制 application start/stop window"]

    B4["w3_gate_smoke.cc
用途: 驗證 gate 控制是否正確"]

    B5["w4_pattern_smoke.cc
用途: 驗證 pattern 是否能正確驅動 gate"]

    B6["輸出/驗證
Gate events / smoke logs"]
end

subgraph T25["Tier 2.5 — Control/Data Bridge"]
    C1["cell_udp_sender.h
用途: 依 cell 送出 UDP traffic"]

    C2["w5b_throughput_flagsender.cc
用途: sender 端 throughput 驗證"]

    C3["w5b_throughput_udpclient.cc
用途: UDP client throughput 驗證"]

    C4["w5_throughput_sanity
用途: 驗證 throughput 是否反映 gate / pattern"]

    C5["w25_tier2p5_window_metrics.cc
用途: 整合 beam-hopping event / window / throughput metrics"]

    C6["輸出檔
beam_hop_events.log
window metrics
throughput_per_cell.csv"]
end

subgraph T3["Tier 3 — QoS Data Plane Validation"]
    D1["w3_tier3_qos_singlecell.cc
用途: 建立 Gold/Silver/Bronze traffic classes
驗證 QoS differentiation"]

    D2["QoS Parameters
goldReservedRate
sharedRate
simTime
nSilver / nBronze"]

    D3["輸出檔
qos_metrics.csv
(flow + class metrics)"]
end

A1E --> A1
A1 --> A1O
A1O --> A2
A2M --> A2
A2 --> A2O
A2O --> A3
A3E --> A3
A3 --> A3O
A3O --> A4
A4 --> A4O

A4O --> B0
B0 --> B1
B1 --> B2
B2 --> B4
B1 --> B5
B3 --> B4
B4 --> B6
B5 --> B6

B2 --> C1
C1 --> C2
C2 --> C3
C2 --> C4
C3 --> C4
B1 --> C5
B2 --> C5
C4 --> C5
C5 --> C6

C6 --> D1
D2 --> D1
D1 --> D3
```
