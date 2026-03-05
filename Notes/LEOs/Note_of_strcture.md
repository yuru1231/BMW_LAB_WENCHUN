# 1.Technical Specification
## 1.1 Space & Mobility
- Altitude : 550km
- Orbit Model: SGP4 Mobility Model
- Main course : Taiwan 
- ISL :

## 1.2 Time & Management
- Routing 決策週期 (Large Scale): 30 sec
  - 處理衛星網路的拓樸與路由決策，因此更新頻率較低
- Beam Hopping 週期 (Medium Scale): 10 ms
  - 決定衛星在不同 Cell 之間的服務順序，因此需要較快的控制週期
- Scheduling 週期 (Small Scale): 1 ms
  - 在最短時間尺度內，將實際傳輸資源分配給個別 UT

## 1.3 Traffic & QoS
- 波束頻寬: $250\text{ MHz}$ (Ka-band)
- QoS Class 定義:
 - Class A (Control/Voice): 優先權 7, 使用 CRA (Constant Rate Allocation)。
 - Class B (Video/Real-time): 優先權 5, 使用 RBDC (Rate-Based)。
 - Class C (Best Effort): 優先權 1, 使用 WFQ 分配剩餘頻寬。
依照 QoS class 的優先順序進行服務，高優先權的封包會優先被傳送。在同一 QoS class 內，如果有多個 UT 同時請求資源，則使用 Weighted Fair Queueing（WFQ）進行公平分配

# 2.Flow
## 2.1 ISL
### 2.1.1 Goal
1.  找到一條可行的封包傳輸路徑，使 UT 可以透過衛星網路連接到地面站（FT）
2.  流程:
  - 讀取衛星的軌道參數（例如 TLE），透過軌道推進模型，可以預測在指定時間區間內衛星的位置
  - 選擇一組節點形成基本轉送拓樸 : UT *1 、 SAT *2（Sat-A 與 Sat-B）、 FT * 1
    - Sat-A 作為 serving satellite，負責與 UT 建立 user link
    - Sat-B 作為 relay satellite，負責透過 ISL 將封包轉送至 gateway。
 `UT --USERLINK--→ Sat-A --ISL-→ Sat-B --GtewayLink-→ FT`
3. `Ipv4StaticRoutingHelper `為每個節點建立靜態路由表，以強制封包沿著指定路徑轉送
### 2.1.2 I/O
- Input:衛星軌道參數、地面站位置、時間區間
- Output:`NodeContainer `中各節點的靜態路由表
  
### 2.1.3 file
`sat-constellation-example.cc` : 如何定義多顆衛星軌道、高度（LEO）以及衛星間的相對位置
`sat-fwd-link-beam-hopping-example.cc` : 如何動態切換波束資源
`sat-dynamic-frequency-plan-example.cc` :　在模擬運行中更換頻率
`sat-link-budget-example.cc` : 計算 ISL 或 UT 鏈路的 SINR 時，需要參考這個檔案來確保物理層參數正確。
`sat-mobility-example.cc` : 驗證 LEO 衛星是否真的在動，以及動態拓撲下的路由切換


## 2.2 Beam Hopping
### 2.2.1 Goal
決定衛星在不同覆蓋 Cell 之間的服務順序，系統會建立一個週期性事件，當事件被觸發時，系統會查詢 MAC 層佇列，以了解各 Cell 的待傳輸資料量，根據各 Cell 的流量比例，系統會決定在下一個 hopping 週期內，各 Cell 所分配到的服務時間

### 2.2.2  I/O
- Input： MAC 層隊列長度、流量預測值。
- Output：Beam activation schedule(CellId 時間)

## 2.3 QoS Scheduling
### 2.3.1 Goal
對封包進行 QoS 分類，封包會依照其 QoS class 被放入不同優先級的佇列，接著根據 QoS class 的優先順序進行服務
### 2.3.2  I/O
- Input： UT 的容量請求 (Capacity Requests, 如 RBDC)、封包 QoS 標籤。
- Output： BurstTimePlan (BTP)，告訴 UT 何時可以發射訊號。

# 3.Execution Life Cycle
## 3.1 Configuration Phase (0s):
- `CreateNodes()`:建立衛星、UT、FT
- `InstallMobility()`: 設定 LEO 軌道
- `nstallSatStack()`: 安裝 DVB 協議棧
What I do :**定義靜態路由，限縮路徑。**
## 3.2 Event Loop (0s ~ End):
- T=10ms, 20ms...: BeamHoppingController 觸發，決定下一個週期的波束圖譜。
- T=Every Frame: SatScheduler 觸發，根據 WFQ 分配當前波束內的資源。
- T=30s: RoutingManager 檢查衛星是否飛出範圍，必要時切換下一組 FT pair。
## 3.3 Analysis Phase (End):
- 產出 .pcap 檔案與 flowmon 統計

# 4.DoD
-[ ] End-to-End Latency (QoS 分佈)： 驗證 Class A 的延遲是否維持在最低（跳過 WFQ 競爭）。
-[ ] Throughput per Beam： 驗證 Beam Hopping 是否有正確將資源引導至高流量 Cell。
-[ ] ISL Utilization： 觀察 ISL 鏈路在多跳轉發下的頻寬佔用。


# 5.Implement
## 5.1 ISL 路由基礎建設
複製官方星座範例到 scratch
`sat-constellation-example.cc`->`leo-bh-sim.cc`
### 5.1.1 確認場景資料夾與 LEO 屬性
`std::string scenarioFolder = "constellation-eutelsat-geo-2-sats-isls";` -> `std::string scenarioFolder = "constellation-telesat-351-sats-TW_near";`
- 
### 5.1.2. 強制鎖定 ISL 速率
`Config::SetDefault("ns3::PointToPointIslHelper::IslDataRate", DataRateValue(DataRate("100Mb/s")));`
### 5.1.3 BeamSet 排除無用節點
留下你想實驗的特定 UT 所在的波束
`std::set<uint32_t> beamSetTelesat = {1, 43, 60, 64};` ->`std::set<uint32_t> beamSetTelesat = {43};`
### 5.1.4 方便觀察單一跳數的延遲，流量調稀疏
`std::string interval = "100ms";` (20ms->100ms)
### 5.1.5 
```
SAT: ID = 92, at 40.8401,-14.4919,1.01063e+06
    Devices to ground stations 
      02-06-00:00:00:00:00:5d
        Feeder at 02-06-00:00:00:00:02:7c, beam 43
      Feeder connected to
        User at 02-06-00:00:00:00:02:7d, beam 43
      User connected to
    ISLs 
      02-06-00:00:00:00:06:ce to SAT 79
      02-06-00:00:00:00:06:fc to SAT 91
      02-06-00:00:00:00:06:ff to SAT 93
      02-06-00:00:00:00:07:01 to SAT 105
```
- LEO Confirmed :1.01063e+06 (1,010.63 km)
- ISL : Sat Id successed connect with SAT 79.91.93.105
- Feeder beam 43 and User beam 43 under SAT 92

```
--- Baseline (Global) Stats ---

Avg Delay: 72.111 ms (Samples: 197)

--- TW-near (Taipei) Stats ---

Avg Delay: 7.684 ms (Samples: 587)
```

- 延遲從 72ms 掉到 7.6ms :在 TW-near 中，UT 與 GW 都在台北，封包路徑變成極短的 GW → Sat → UT

## 5.2 Frequency Plan
定義衛星系統的可用頻譜資源，包括 Forward Link & Return Link的 Carriers分配
