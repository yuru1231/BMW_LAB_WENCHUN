# Routing Planning Layer 

# 1.Role

## 1.1 Position in the Overall System Architecture

- **Tier-1：Routing Planning Layer**
- Tier-2：Beam Hopping / Resource Allocation Layer
- Tier-3：QoS / Traffic Scheduling Layer

1. 在給定的 LEO constellation scenario 下，哪些衛星在某個時間點能夠服務台北？
2. 這些衛星中，哪些能經由真實的 ISL 圖走到 gateway anchor satellite？
3. 在一個 planning window 內，應該選哪顆衛星作為 serving satellite？
4. 該 serving satellite 到 gateway 的衛星路徑為何？總 hop 數多少？

> 在真實星座幾何與真實 ISL topology 之上，建立一個可供上層 Beam Hopping 與 QoS 使用的 routing planning layer。

---

## 1.2 輸出的意義

- **candidate_sats.csv**：目前哪些衛星對台北可視、是否可達 gateway、候選條件是否成立
- **isl_connectivity.csv**：衛星與衛星之間的幾何距離與真實 ISL link 是否存在
- **routing_plan.csv**：每一個 planning window 應該選哪顆 satellite 作為 serving satellite，以及通往 gateway 的實際衛星路徑
- **summary.json**：本次 D1 執行的主要參數與輸出檔摘要

這些輸出後續可直接提供給：

- Tier-2：決定 Beam Hopping 期間應服務哪個區域 / 哪顆 serving satellite
- Tier-3：在已知 serving path 的前提下，規劃 QoS 與資源分配

---

# 2. 來源原型

## 2.1 模型

 `scratch/isl-leo-candidate.cc` 已將上述兩種原型的核心功能整合，並進一步升級為：

1. 使用 ** sat351 constellation** (`constellation-telesat-351-sats`)
2. 從 **SNS3 實際建立後的 topology** 中擷取 orbiter nodes、GW nodes、ISL adjacency
3. 使用 **SatMobilityModel** 取得衛星真實幾何位置
4. 計算 **台北 reference point 的真實 elevation**
5. 建立 **真實 ISL graph**
6. 搜尋 **satellite-to-gateway anchor 的最短路徑**
7. 在每個 planning window 中選擇 **serving satellite**

---

# 3. 目前實作檔案與事件結構

## 3.1 主程式

主程式名稱：[`scratch/isl-leo-candidate.cc`](https://github.com/yuru1231/BMW_LAB_WENCHUN/blob/22d2f295bf795348573ae688c396753bbc892d10/TriScale-LEO/Topology%20%26%20ISL%20Routing%20/isl-leo-candidate.cc)

負責：
- scenario 初始化
- constellation topology 建立
- orbiter / gateway nodes 擷取
- candidate 掃描
- ISL graph 建立
- shortest path search
- routing plan 輸出
- summary / logs 生成

---

## 3.2 事件目錄

事件目錄：`events/E20260305_ISL_BH/`


典型內容：

```text
events/
└── E20260305_ISL_BH/
    ├── env.sh
    ├── run_event.sh
    ├── README.md
    ├── logs/
    │   ├── run.out
    │   └── run.err
    ├── outputs/
    │   ├── candidate_sats.csv
    │   ├── isl_connectivity.csv
    │   ├── routing_plan.csv
    │   ├── route_dump.txt
    │   ├── forward_path.log
    │   └── summary.json
    ├── pcap/
    └── docs/
        └── notes.md
```

---

## 3.3 事件化執行方式
- `env.sh`：定義執行環境變數
```
#!/usr/bin/env bash

export EVENT_NAME="E20260305_ISL_BH"
export NS3_DIR="$HOME/workspace/ns-3.43"
export SCENARIO_DIR="constellation-telesat-351-sats"

export EVENT_DIR="$NS3_DIR/events/$EVENT_NAME"
export LOG_DIR="$EVENT_DIR/logs"
export OUT_DIR="$EVENT_DIR/outputs"
export PCAP_DIR="$EVENT_DIR/pcap"

mkdir -p "$LOG_DIR" "$OUT_DIR" "$PCAP_DIR"
```
- `run_event.sh`：固定執行命令與輸出位置
```
#!/usr/bin/env bash
set -e

source "$(dirname "$0")/env.sh"

cd "$NS3_DIR"

./ns3 run "isl-leo-candidate \
--mode=d1_final \
--scenarioFolder=$SCENARIO_DIR \
--simTime=30 \
--tStart=0 \
--tEnd=30 \
--dt=1 \
--planWindow=30 \
--refLat=25.0330 \
--refLon=121.5654 \
--elevDeg=20 \
--gwIndex=0 \
--statsLevel=min \
--outDir=$OUT_DIR" \
1>"$LOG_DIR/run.out" \
2>"$LOG_DIR/run.err"
```

---

# 4. Scenario 與建模基礎

## 4.1 使用之 scenario

真實 constellation scenario 為：`constellation-telesat-351-sats`

此 scenario 透過 SNS3 / satellite module 載入，並建立：

- 351 顆 orbiter nodes
- 2 個 gateway nodes
- constellation 內建之 ISL links

執行 log 之證據如下：

```text
orbiter nodes = 351
gw nodes = 2
ISL adjacency sats = 351
GW anchor sat count = 1
GW anchor sats: 64
```

---

## 4.2 建立 scenario 的必要設定

為使 constellation scenario 可正確建構，目前在 `main()` 中於建立 `SimulationHelper` 之前設定：

### regeneration mode

```cpp
Config::SetDefault("ns3::SatConf::ForwardLinkRegenerationMode",
                   EnumValue(SatEnums::REGENERATION_NETWORK));
Config::SetDefault("ns3::SatConf::ReturnLinkRegenerationMode",
                   EnumValue(SatEnums::REGENERATION_NETWORK));
```

這是 constellation mode 的必要設定，否則會出現：

```text
Forward regeneration must be network when using constellations
```

---

### network address defaults

```cpp
Config::SetDefault("ns3::SatHelper::BeamNetworkAddress", Ipv4AddressValue("20.1.0.0"));
Config::SetDefault("ns3::SatHelper::GwNetworkAddress", Ipv4AddressValue("10.1.0.0"));
Config::SetDefault("ns3::SatHelper::UtNetworkAddress", Ipv4AddressValue("250.1.0.0"));
```

其目的在於讓 scenario 建立時各節點之 network addressing 正常初始化。

---

### ACM / simulation overwrite / packet trace

```cpp
Config::SetDefault("ns3::SatBbFrameConf::AcmEnabled", BooleanValue(true));
Config::SetDefault("ns3::SatEnvVariables::EnableSimulationOutputOverwrite", BooleanValue(true));
Config::SetDefault("ns3::SatHelper::PacketTraceEnabled", BooleanValue(false));
```

`PacketTraceEnabled` 目前關閉，原因是：

- 尚未完成對應 trace callback 路徑之完整配置
- 開啟後曾發生 `SatPhy/PacketTrace` callback attach 失敗
- 目前核心任務為 geometry + topology planning，不依賴 packet trace 完成主要功能

---

## 4.3 beam set 與 user count

為使 `constellation-telesat-351-sats` 正常建立 gateway / beam 拓樸，使用：

```cpp
std::set<uint32_t> beamSetTelesat = {1, 43, 60, 64};
simulationHelper->SetBeamSet(beamSetTelesat);
simulationHelper->SetUserCountPerUt(2);
```


---

# 5. 動態與靜態條件說明

## 5.1 目前哪些是靜態的？

### (1) ISL adjacency graph

目前 ISL adjacency graph 是：

> **在 simulation initialization 後從實際 topology 擷取一次，視為靜態 graph。**

- `BuildRealIslAdjacency()` 會擷取 sat351 建立後的 orbiter 間連線關係
- graph 不隨 `t` 重新建構

此設計代表的模型假設為：

```text
satellite neighbors 固定
routing decision 動態
```

---

### (2) Gateway anchor satellite（目前版本）

目前 gateway anchor satellite 採用：

```text
GW position -> GetClosestSat(gwPos)
```

anchor satellite 亦固定判定，未隨時間更新。

本次執行之 evidence：

```text
GW anchor sats: 64
```

目前使用：

```text
GW0 -> SAT64
```

作為 gateway anchor。

---

## 5.2 目前哪些是動態的？

### (1) Satellite geometry / mobility

衛星的幾何位置並不是手寫固定，而是由：

```cpp
Ptr<SatMobilityModel> mob = satNode->GetObject<SatMobilityModel>();
GeoCoordinate geo = mob->GetGeoPosition();
```

取得。

因此，D1 已建立在真實 satellite mobility 之上。

---

### (2) Reference visibility evaluation

對 reference point（台北）的 elevation 是透過幾何計算而來，因此具有動態性。

在程式邏輯上，`RunCandidateScan()` 會依照：

```text
t = tStart ... tEnd, step = dt
```

進行掃描，於每個取樣時刻重新計算：

- `elev_ref_deg`
- `candidate`

因此 candidate evaluation 是時間相關的。

---

### (3) Routing plan

Routing plan 並非只在模擬開始算一次，而是依 `planWindow` 在每個 planning window 做一次決策。

因此：

- serving satellite selection 是 window-based dynamic
- 但 graph 本身仍為 static

---

## 5.3 目前 D1 的模型歸納

可將目前 D1 模型總結為：

```text
dynamic geometry
+ static ISL graph
+ static GW anchor (current version)
+ window-based routing planning
```

這代表目前 D1 的焦點在於：

- routing planning
- candidate determination
- serving satellite decision

而非 link feasibility physics 或 dynamic ISL handover。

---

# 6. 幾何計算與 Elevation 計算方法

## 6.1 為什麼要使用 ECEF

衛星與地面點之間的距離與仰角不能直接在 `(lat, lon, alt)` 空間中線性計算，因為：

- 經緯度不是直角座標
- 經度距離隨緯度而變
- 高度必須納入 3D 空間計算

因此程式中使用：

```cpp
Vector GeoToEcef(double latDeg, double lonDeg, double altM)
```

將地理座標轉換為 ECEF（Earth-Centered Earth-Fixed）座標。

---

## 6.2 `GeoToEcef()` 功能

此函式使用 WGS84 參數：

- semi-major axis `a = 6378137.0`
- eccentricity squared `e2 = 6.69437999014e-3`

並將：

```text
(lat, lon, alt)
```

轉換為：

```text
(x, y, z)
```

其目的在於後續可進行：

- sat-ground line-of-sight 向量
- sat-sat 真實距離
- elevation angle

之 3D 幾何計算。

---

## 6.3 `GetElevationDegToReference()`

### 功能

此函式用於計算某顆 satellite 相對 reference point（目前為台北）的 elevation angle。

### 流程

1. 由 `SatTopology` 取得指定 satId 的 orbiter node
2. 由該 node 取得 `SatMobilityModel`
3. 取出 satellite 的 `GeoCoordinate`
4. 將 satellite 與地面 reference point 轉為 ECEF
5. 計算 LOS（line-of-sight）向量
6. 建立 ground local up vector
7. 使用：

```text
elev = asin( dot(LOS, UP) / |LOS| )
```

求得仰角

### 重要特性

- 此版本已經是 **真實幾何值**，不是 placeholder
---

## 6.4 current reference point

目前 reference point 預設為台北：

```text
refLat = 25.0330
refLon = 121.5654
```

也就是臺北市中心附近。

此值可經由 CLI 參數改寫，因此具備參數化彈性。

---

# 7. 真實 ISL Graph 建立方法

## 7.1 `BuildNodeIdToSatIdMap()`

建立`nodeId -> satId`的反查表。後續在掃描 NetDevice / Channel peer 時，只能先拿到 peer node，而 path 計算需要以 satId 為 graph node index，因此必須建立這個 mapping。

---

## 7.3 `GetPeerDevices()`

此函式對任一 `NetDevice`：

1. 取出其 `Channel`
2. 掃描該 channel 上其他 devices
3. 回傳除自己之外的 peer devices

尋找實際 channel 相連的對端 device。

---

## 7.4 `BuildRealIslAdjacency()`

### 功能

由 instantiated topology 中擷取真實 satellite adjacency graph。

### 實作流程

對每一顆 satellite：

1. 取出 orbiter node
2. 掃描 node 上所有 NetDevices
3. 對每個 device 取 peer devices
4. 若 peer 所屬 node 也是 orbiter node，則視為 graph neighbor
5. 去除重複、排序
6. 補齊雙向邊

### 輸出形式

```cpp
using IslAdjMap = std::map<uint32_t, std::vector<uint32_t>>;
```

也就是：

```text
satellite_id -> neighbor satellites
```

### 執行 evidence

```text
[D1] ISL adjacency sats = 351
```

這表示 graph 已成功覆蓋整個 351-satellite constellation。

---

## 7.5 Graph 的時間特性

目前 `BuildRealIslAdjacency()` 是在每次需要時重建，邏輯上使用同一個 topology，因此 graph 本質仍視為靜態 graph。

後續若要優化，可改為：

- 初始化建一次 graph
- 後續重複使用 cache

這屬於效率優化，不影響目前模型正確性。

---

# 8. Gateway Anchor Satellite 的取得方式

## 8.1 為什麼需要 anchor satellite

D1 的 shortest path search 並不是直接找 `GW node` 作圖上的終點，而是先找：

```text
哪一顆 satellite 扮演 gateway 的 feeder / anchor satellite
```

然後在 ISL graph 上求：

```text
candidate satellite -> gateway anchor satellite
```

最後再連到 GW。

---

## 8.2 舊方法與問題

從 GW device 的 peer device 反查 satellite，結果：

```text
GW anchor sat count = 0
```

> GW 與 satellite 之間的 feeder relation 並不一定能以 generic peer-device 掃描法可靠取得。

---

## 8.3 現行方法

目前採用：

```cpp
GW node -> SatMobilityModel -> GeoCoordinate -> GetClosestSat(gwPos)
```

也就是以 GW 地理位置對應最近 satellite 作為 gateway anchor satellite。

### 執行結果

```text
GW anchor sat count = 1
GW anchor sats: 64
```

代表目前 gateway anchor satellite 為 `SAT64`。

---

## 8.4 模型假設

目前：

- gateway anchor satellite 於本次執行中固定
- 未設計為 per-window dynamic update

---

# 9. Path Search 與 Reachability

## 9.1 `FindShortestPath()`

此函式對 ISL graph 執行 BFS shortest path search。

### 輸入

- `adj`：ISL adjacency graph
- `srcSat`：來源 satellite
- `dstSats`：目的 satellite set（目前為 gateway anchor satellites）

### 輸出

- 是否找到 path
- `outPath`：satellite sequence

### 特性

- 使用 BFS
- 因 graph 為 unweighted，BFS 可得到最小 hop path

---

## 9.2 `SatPathToString()`

此函式負責將 satellite 路徑序列轉為文字路徑，例如：

```text
UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0
```

輸出格式的目的在於：

- 直接可讀
- 可寫入 `routing_plan.csv`
- 可作為 review / 論文圖表的依據

---

## 9.3 `PathExistsToGateway()`

### 功能

判斷某顆 satellite 是否能經由真實 ISL graph 到達 gateway anchor satellite，若可達則輸出：

- `hops`
- `path`

### 步驟

1. 建立或取得 `IslAdjMap`
2. 取得 `gwAnchorSats`
3. 執行 BFS shortest path
4. 若找到 path：
   - `ok = true`
   - `hops = path length - 1`
   - `path = SatPathToString(...)`
5. 若未找到：
   - `ok = false`
   - `hops = 0`
   - `path = empty`

### 目前語意

`isl_to_gw` 不再是假值，而是真正代表：

```text
this satellite can reach the gateway anchor through the actual ISL graph
```

---

# 10. Candidate 掃描邏輯

## 10.1 `RunCandidateScan()`

此函式會在：

```text
t = tStart ~ tEnd, step = dt
```

範圍內掃描所有 satellites。

---

## 10.2 掃描內容

對每個時間點、每顆 satellite，計算：

1. `elev_ref_deg`
2. `isl_to_gw`
3. `path_hops`
4. `candidate`

---

## 10.3 Candidate 條件

目前 D1 的 candidate 條件為：

```text
candidate = (elevation >= elevDeg) AND (path to gateway exists)
```

衛星要被視為真正 candidate，必須同時滿足：

1. 對台北可視
2. 在真實 ISL graph 上可到達 gateway anchor


---

## 10.4 輸出解讀

例如：

```text
time,satId,elev_ref_deg,isl_to_gw,path_hops,candidate
0.000,0,-64.508,1,5,0
0.000,1,-52.804,1,6,0
0.000,2,-39.701,1,7,0
```

表示：

- SAT0 / SAT1 / SAT2 雖然都可沿 graph 到達 gateway
- 但它們對台北的 elevation 為負值，不可視
- 因此 `candidate=0`

而真正可視且可達的衛星會是類似：

```text
satId=44, elev_ref_deg=65.296, isl_to_gw=1, candidate=1
```

---

# 11. ISL Connectivity 輸出邏輯

## 11.1 `EvaluateIslConnectivity()`

此函式對 satellite pairs 輸出：

- `distance_km`
- `active`

---

## 11.2 真實 sat-sat distance

對任意 satA / satB：

1. 取兩顆 satellite 的 `GeoCoordinate`
2. 轉為 ECEF
3. 用 3D Euclidean distance 求距離

因此 `distance_km` 已是真實幾何距離，而非 placeholder。

範例：

```text
0.000,0,1,3522.941,1
0.000,0,2,6851.804,0
```

這類數值已符合 LEO constellation 中不同 orbital relation 的合理距離範圍。

---

## 11.3 `active` 的定義

目前 `active` 已不再使用 distance threshold，而是直接由真實 ISL graph 判斷：

```cpp
row.active = HasEdge(adj, row.satA, row.satB);
```

因此 `active=1` 的意義是：

```text
satA 與 satB 在實際 instantiated topology 中有真實 ISL edge
```

這是 D1 從 geometry-based approximation 升級為 actual graph-based planning 的關鍵。

---

# 12. Routing Plan 建立邏輯

## 12.1 `BuildRoutingPlan()`

此函式以 `candidate_sats.csv` 的 candidate 集合為基礎，針對每個 planning window 決定：

- 該 window 的 serving satellite
- 通往 gateway 的 satellite path
- hop count
- selection reason

---

## 12.2 規劃時間窗

目前以：

```text
planWindow = 30s
```

進行 planning。

也就是：

- `0~30s` 一個 window
- 若 `simTime=300`、`planWindow=60`，則會有 5 個 planning windows

---

## 12.3 scoring / 選擇規則

目前選擇 serving satellite 的規則為：

1. **最大 elevation**
2. **最小 hop count**

在程式中以：

```text
best_elevation_then_min_hops
```

表示。

### 更精確的語意

對每個 satellite，先在 window 內統計：

- `maxElev`
- `minHops`

然後用 lexicographic rule 選擇：

```text
先比較 maxElev
若相同再比較 minHops
```

---

## 12.4 產生之 path

選出最佳 satellite 後，再實際呼叫 `PathExistsToGateway()` 取得該 satellite 到 gateway 的 shortest path。

因此 `routing_plan.csv` 中的 path 已是：

```text
UT -> serving satellite -> ISL graph path -> gateway anchor satellite -> GW
```

---

## 12.5 目前 evidence

本次執行結果：

```text
time_start,time_end,serving_sat,path,hop_count,status,gw_index,reason
0.000,30.000,44,UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0,7,OK,0,best_elevation_then_min_hops
```

解讀：

- `serving_sat = 44`
- gateway anchor sat = `64`
- path hop count = `7`
- status = `OK`
- selection reason = `best_elevation_then_min_hops`

這代表在 `0~30s` 這個 planning window 中，D1 認定：

- SAT44 是對台北最適合作為 serving satellite 的候選
- 它可沿實際 constellation ISL graph 通至 SAT64，再接到 GW0

---

# 13. CLI 參數與意義

目前 `ParseArgs()` 支援以下參數。

## 13.1 模式參數

### `--mode`
支援：

- `verify_path`
- `candidate_scan`
- `isl_connectivity`
- `plan`
- `d1_final`

其中 `d1_final` 為一次輸出完整 D1 交付物的模式。

---

## 13.2 scenario 與執行範圍

### `--scenarioFolder`
目前使用：

```text
constellation-telesat-351-sats
```

注意此參數需傳 **scenario 名稱**，不是絕對路徑。

---

### `--simTime`
整體模擬時間。

---

### `--tStart`, `--tEnd`, `--dt`
Candidate 掃描時間範圍與取樣步長。

例如：

```text
tStart = 0
tEnd = 30
dt = 1
```

代表每 1 秒掃描一次，掃描 0~30 秒。

---

### `--planWindow`
Routing planning time window 長度。

例如：

```text
planWindow = 30
```

表示每 30 秒規劃一次 serving satellite。

---

## 13.3 reference point

### `--refLat`, `--refLon`
Reference location 之經緯度，預設為台北。

---

### `--elevDeg`
Candidate elevation threshold。

目前使用：

```text
elevDeg = 20
```

也就是仰角需大於等於 20 度才視為可服務 reference point。

---

## 13.4 gateway

### `--gwIndex`
指定使用哪個 gateway。

目前 scenario 中 evidence 顯示：

```text
gw nodes = 2
```

因此理論上可擴充到使用 `gwIndex = 0` 或 `1`。

目前 example 使用 `gwIndex = 0`。

---

## 13.5 output

### `--outDir`
指定輸出目錄

### `--pcapDir`
指定 pcap 目錄

### `--enablePcap`
是否啟用 pcap evidence（目前 verify_path 中仍為 placeholder）

---

# 14. 模式說明

## 14.1 `verify_path`

用途：

- 產生基本 route dump / forwarding log placeholder
- 保留未來接真正 packet forwarding instrumentation 的位置

目前此模式尚未完成真實封包級 evidence，只是保留介面與輸出格式。

---

## 14.2 `candidate_scan`

用途：

- 只執行 candidate 掃描
- 輸出 `candidate_sats.csv`

---

## 14.3 `isl_connectivity`

用途：

- 只輸出 sat-sat connectivity
- 輸出 `isl_connectivity.csv`

---

## 14.4 `plan`

用途：

- 產生 routing plan
- 輸出 `routing_plan.csv`

---

## 14.5 `d1_final`

用途：

- 一次執行 D1 所需主要步驟
- 輸出 D1 全部交付檔案

---

# 15. 重要輸出欄位解讀

## 15.1 `candidate_sats.csv`

欄位：

```text
time,satId,elev_ref_deg,isl_to_gw,path_hops,candidate
```

### `time`
掃描時間點。

### `satId`
衛星 ID。

### `elev_ref_deg`
該衛星相對台北 reference point 的仰角。

### `isl_to_gw`
該 satellite 是否能透過真 ISL graph 走到 gateway anchor。

### `path_hops`
通往 gateway anchor 的最短 hop 數。

### `candidate`
是否同時滿足：

- `elev_ref_deg >= elevDeg`
- `isl_to_gw == 1`

---

## 15.2 `isl_connectivity.csv`

欄位：

```text
time,satA,satB,distance_km,active
```

### `distance_km`
真實 sat-sat 幾何距離。

### `active`
在真實 ISL graph 中是否存在 edge。

---

## 15.3 `routing_plan.csv`

欄位：

```text
time_start,time_end,serving_sat,path,hop_count,status,gw_index,reason
```

### `serving_sat`
該 planning window 中選出的服務衛星。

### `path`
從 UT 經由衛星 graph 至 gateway 的路徑。

### `hop_count`
最短路徑 hop 數。

### `status`
`OK` 或 `NO_PATH`。

### `reason`
目前主要使用：

```text
best_elevation_then_min_hops
```

---

# 16. 目前已完成的核心成果

1. 真實 sat351 constellation 載入
2. 真實 orbiter / GW nodes 擷取
3. 真實 satellite geometry 讀取
4. 真實台北 elevation 計算
5. 真實 ISL adjacency graph 建立
6. gateway anchor satellite detection
7. satellite-to-gateway shortest path search
8. candidate visibility + reachability integration
9. planning window serving satellite decision
10. eventized reproducible execution pipeline

---

# 17. 目前仍屬保留 / 尚未完成部分

為避免過度宣稱，以下項目應明確標註為目前尚未完成或仍為保留位置。

## 17.1 真正的 packet-level verify path / pcap evidence

`RunVerifyPath()` 目前仍是 placeholder。雖保留：

- `route_dump.txt`
- `forward_path.log`
- `pcap/`

但尚未真正綁定 ns-3 forwarding trace。

---

## 17.2 動態 ISL graph 更新

目前 graph 是 initialization-time extraction，不會依衛星距離或 LOS 改變而重新建構。

---

## 17.3 動態 gateway anchor update

目前 gateway anchor 於一次執行中固定，不是 per-window update。

---

## 17.4 Graph caching / 效能優化

目前 `BuildRealIslAdjacency()`、`GetGatewayAnchorSatIds()` 會在部分流程中被重複呼叫，未做 cache。這不影響正確性，但可優化速度與 log 簡潔度。

---

# 18. 與後續關係

## 18.1 對 Tier-2 Beam Hopping

提供：
- 哪顆 satellite 在某個 window 應作為 serving satellite
- 該 satellite 是否穩定可服務台北
- 通往 gateway 的 path 與 hop cost

Beam Hopping 層可在此基礎上決定：

- 某個 window 中 beam 該如何配置
- 哪些 cell 應優先被服務
- 是否需要考慮 handover 準備

---

## 18.2 對 Tier-3 QoS Scheduling

Tier-3 可利用 routing result：

- 了解 path 長度
- 推估 delay / congestion risk
- 在上層 scheduling 時把不同 path cost 考慮進去

---

# 19. 後續強化

1. 將 gateway anchor 改為 per-window dynamic update
2. 將 candidate 條件拆成：
   - visible_to_ref
   - reachable_to_gw
   - candidate
3. 將 graph / anchor 做 cache，避免重建
4. 將 `routing_plan.csv` 增加：
   - `gw_anchor_sat`
   - `path_stability`
5. 引入 dynamic graph update

---

# 20. 總結

具備：

- 真實 constellation topology
- 真實幾何可視性
- 真實 ISL adjacency
- 真 shortest path 到 gateway anchor
- 真 serving satellite 規劃

本次執行已產生明確 evidence：

```text
orbiter nodes = 351
gw nodes = 2
GW anchor sats: 64
serving_sat = 44
path = UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0
hop_count = 7
```
