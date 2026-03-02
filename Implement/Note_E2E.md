# LEO NTN E2E 架構筆記

**主軸：ISL Routing（大尺度）→ Beam Hopping（中尺度）→ QoS Scheduling（小尺度）**
目標：把衛星/FT/UT 的巨量組合先限縮，再做可驗證、可重放的E2E pipeline。

---

## 0. Core
資源調度拆成三個時間尺度（由大到小）的控制層：

1. **Routing/Pruning（秒～十秒）**：先把「可用路徑集合」與「成本可接受的 FT pair」縮到最小，輸出時間索引的路徑契約。
2. **Beam Hopping Schedule（毫秒～百毫秒）**：在固定衛星與可視時間內，依需求決定 cell 服務窗口順序（pattern），輸出時間窗（window）契約。
3. **QoS Scheduling（TTI/packet）**：在每個 window 內，對該 cell 的 UT flows 做優先權與公平分配（priority + WFQ），輸出 per-class/per-flow 指標。

**E2E可驗證的關鍵**：控制面（pattern/window）與資料面（packet/throughput）對齊， **window-attributed metrics**。

---

## 1. Layer vs Tier

### 1.1 Layer = System

* **Layer 1：ISL Routing & Pruning**
* **Layer 2：Beam Hopping Scheduling**
* **Layer 3：QoS Scheduling / Resource Allocation**

### 1.2 Tier = Implement

* **Tier 1**：routing lock / golden path / pruning scenario（讓網路拓樸可重現且可跑快）
* **Tier 2**：beam hopping pattern（由需求→時間窗）
* **Tier 2.5**：window attribution + window metrics（控制面/資料面一致性驗證）
* **Tier 3**：QoS（priority + WFQ，產出 per-class 指標）

> 結論：**Layer 是論文架構，Tier 是 Layer 落地的路線圖**。

---

## 2. End-to-End Pipeline（E2E流程圖）

### 2.1 概念流程（由資料→控制→模擬→輸出）

```
(Scenario inputs: TLE/ISL/GS/UT positions)
        |
        v
[Layer1/Tier1] Routing & Pruning
  - 選 FT pair
  - 算 time-indexed shortest path / feasibility
  - 只保留黃金路徑上節點與鏈路
        |
        |  routing_contract.csv  (path/capacity/time)
        v
[Layer2/Tier2] Beam Hopping Schedule
  - demand model (UT count, traffic prediction, static matrix)
  - 轉成 windows/pattern
        |
        |  service_windows.csv / tier1_pattern.csv  (window_id, cell, t_start, t_end)
        v
[Tier2.5] Control/Data Plane Alignment
  - gate events log
  - packet/window attribution
  - window metrics
        |
        v
[Layer3/Tier3] QoS Scheduling
  - per-window cell queue scheduling
  - priority + WFQ within class
        |
        v
Outputs:
  - packet_trace_with_window.csv
  - window_metrics.csv
  - e2e_summary.json (assumptions/seed/commit/params)
```

---

## 3. 三層控制機制的「時間尺度」與「控制輸入」


### 3.1 Layer 1：Routing / Pruning（秒～十秒）

**目的**：降低路由複雜度，縮小「可用衛星/地面站/鏈路」集合，避免 ns-3 動態路由成本。
**控制輸入**：

* 時間區間 `[t0, t1]` 與採樣步長 `Δt`
* FT pair set：`(FT_i, FT_j)`（成本/租用/策略限制後的集合）
* Link feasibility（仰角/距離/是否視線/ISL限制等）
* 可能的 capacity 模型（先簡化常數也行，但要寫進假設）

**輸出（必須契約化）**：`routing_contract.csv`（時間索引路徑契約）

* `time_range`：例如 0–10s, 10–20s...
* `path_id`
* `hop_sequence`（GW→SAT…→UT）
* `bottleneck_bw` / `bottleneck_delay`（可先 baseline 常數）
* `status`（OK/NO_PATH）

> **架構要點**：Layer1 輸出「下一層能使用的時間化路由契約」。

---

### 3.2 Layer 2：Beam Hopping Schedule（毫秒～百毫秒）

**目的**：在指定衛星可視時間內，決定服務 cell 的時間切片（windows），將需求映射到服務順序與比例。
**控制輸入**：

* 已鎖定的衛星（通常是 routing 選中的衛星集合中的某一顆/少數幾顆）
* 衛星可視時間（routing_contract 內能支撐 E2E 的那段時間）
* cell/beam 定義（A/B/C 或 beam IDs）
* demand model：

  * baseline：固定需求矩陣（例如 50/30/20）
  * trace-driven：由 UT count / offered load proxy 推得需求

**輸出**：`service_windows.csv` 或 `tier1_pattern.csv`

* `window_id`（非常重要：後面所有歸因都靠它）
* `sat_id`
* `cell_id` / `beam_id`
* `t_start`, `t_end`
* `guard_us`（你之前 stopMarginUs 類似概念）

> **架構要點**：Layer2 的產品不是“beam hopping”，而是一份**可執行的時間窗契約**。

---

### 3.3 Layer 3：QoS Scheduling（TTI / packet）

**目的**：在每個 window 內，決定「該 cell 的哪些 UT/flows」能送多少、什麼 QoS 先送。
**控制輸入**：

* window 內可用資源：`C_window`（來自 Layer1 bottleneck + Layer2 window長度）
* QoS class 定義（你可以先用 qualitative：High/Med/Low）
* scheduling policy：

  * **Priority across classes**：高 QoS 先服務
  * **WFQ within class**：同 class 的 flows 做加權公平（weights 可先等權）

**輸出（可觀測指標）**：

* per-class throughput/delay/drop
* per-flow throughput/fairness
* queue length / backlog（若有實作）

> **架構要點**：Layer3 必須指定 **queueing point 在哪裡**（SAT 或 GW）

---

## 4. 控制面 vs 資料面：Tier2.5 的定位（非常關鍵）

控制面與資料面對齊

### 4.1 控制面（Control Plane）

* pattern/window 是控制面
* 輸出：
  * `beam_hop_events.log`：每次 ENABLE/DISABLE 的時間、cell、window_id


### 4.2 資料面（Data Plane）

* 真正的 packet tx/rx、吞吐、delay 是資料面
* 你必須做到：

  * 每個 packet / throughput sample 都能映射到一個 `window_id`
  * 這叫 **window attribution**

### 4.3 一致性保證

* 規格 1：資料面送包只允許在 ENABLE window 內發生
* 規格 2：每筆 metrics 都能對應到 window_id（可追溯）
* 規格 3：若發生違反（例如 window 外有 tx），直接判定 run invalid

---

## 5. E2E（E2E）完成的「最小完整輸出集合」

同一個 run 能產出以下檔案，就叫 E2E baseline done**。

1. `routing_contract.csv`（Layer1 output）
2. `service_windows.csv` / `tier1_pattern.csv`（Layer2 output）
3. `beam_hop_events.log`（控制面事件）
4. `packet_trace_with_window.csv`（資料面歸因：packet↔window）
5. `window_metrics.csv`（window 粒度吞吐/丟包/延遲）
6. `e2e_summary.json`（本次 run 的參數、seed、commit、假設、輸入檔 checksum）

---

## 6. E2E的「控制點」清單


### 6.1 控制的變數（Control knobs）

* Layer1：FT pair set、路徑選擇準則、可用性判定、容量模型
* Layer2：window size、cycle length、cell ordering、guard time、demand→schedule mapping
* Layer3：QoS class mapping、priority strictness、WFQ weights、queue size、drop policy

### 6.2 固定的觀測點（Measurement points）

* E2E throughput：建議固定用 UT sink goodput（或 GW app goodput，但要一致）
* Per-window throughput：window_metrics.csv
* Delay：per-class/per-flow delay（CDF 或均值）
* Drop：per-window / per-class drop

---

## 7. Baseline 假設

baseline ：

* Bottleneck 先固定在某一段 link（或固定 feeder/access 容量），避免“每層各自假設”
* routing_contract 的 capacity 可以先常數（research version 會換成 trace-derived capacity）
* demand model 先用靜態矩陣（50/30/20）或 UT count proxy

---

checklist：

* [ ] routing_contract.csv 產出，且在測試區間內 status 多數為 OK
* [ ] service_windows.csv 有連續 window_id，且總窗口時間合理（含 guard）
* [ ] beam_hop_events.log 的 ENABLE/DISABLE 與 windows 一致
* [ ] packet_trace_with_window.csv：每個 packet 都有 window_id（或明確標註 OUT_OF_WINDOW）
* [ ] window_metrics.csv：每個 window 都有 goodput/drop/delay 欄位
* [ ] e2e_summary.json：含 seed、commit、參數、輸入檔 hash（可重放）

---
