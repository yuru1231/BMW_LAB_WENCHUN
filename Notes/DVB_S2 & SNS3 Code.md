# DVB-S2X 規格書與 SNS3（Satellite Network Simulator 3）筆記整理

> **Reference**
> - SNS3: https://www.sns3.org/
> - arXiv: https://arxiv.org/pdf/0904.3701
> - NotebookLM: https://notebooklm.google.com/notebook/b66e9dfc-738d-4b16-b9da-771d1ab370aa
> - DVB-RCS2 Guidelines (Aug 2025 PDF): https://dvb.org/wp-content/uploads/2014/04/A155-4r1_DVB-RCS2_Guidelines-for-the-Implementation-and-Use-of-EN-301-545-2-LLS_Draft-TR-101-545-4-v121_August-2025.pdf
> - DVB_Ray_0827

---

## 0. 架構（Architecture Overview）

### 0.1 規格體系層級（Specification Layers）

DVB 衛星系統規格可分為三層：高層級（HLS）、低層級（LLS）、以及實施指南/擴充（Guidelines & Extensions）。

#### 0.1.1 高層級衛星規格（DVB-HLS, Higher Layer Satellite）
- **對應標準**：ETSI TS 101 545-3  
  https://www.etsi.org/deliver/etsi_ts/101500_101599/10154503/01.03.01_60/ts_10154503v010301p.pdf
- **涵蓋範圍**：系統架構、Network Layer、管理/控制、流量管理與攔截
- **定位**：偏端到端系統與 IP 整合（與核心網/服務銜接）

#### 0.1.2 低層級衛星規格（DVB-LLS, Lower Layer Satellite）
- **對應標準**：ETSI EN 301 545-2  
  https://www.etsi.org/deliver/etsi_en/301500_301599/30154502/01.04.01_60/en_30154502v010401p.pdf
- **涵蓋範圍**：Physical Layer + Access Layer
- **定位**：決定頻譜效率、延遲、可靠度的核心層

#### 0.1.3 指南與擴充（Guidelines and Extensions）
- **Implementation Guidelines**：TR 101 545-4  
  https://www.etsi.org/deliver/etsi_tr/101500_101599/10154504/01.01.01_60/tr_10154504v010101p.pdf
- **擴充情境**：Mobile / Mesh / Security
- **定位**：補足實作細節與進階情境行為（標準未強制但工程必需）

---

### 0.2 協定堆疊層級（Protocol Stack）

依資料流方向分為：**前向鏈路（FWD）** 與 **回傳鏈路（RTN）**。

#### 0.2.1 前向鏈路（Forward Link）
- **Network Layer**：IP PDU 傳遞與端到端一致性
- **GSE（Generic Stream Encapsulation）**
  - PDU 定界、分段、標籤/位址、完整性檢查
  - 追求高效率與低開銷（適合單向高吞吐廣播）
- **Physical Layer**
  - GSE 資料 → BBFRAME → FEC → PLFRAME → RF
  - DVB-S2X：PLFRAME 可再組織成 Superframe（強化同步與資源管理）

#### 0.2.2 回傳鏈路（Return Link）
- **RLE（Return Link Encapsulation）**
  - 類似 GSE，但針對 RTN 特性優化（低 overhead、映射至 burst）
- **MF-TDMA（Physical / Access）**
  - 資源切分：Timeslot（由 BTU 組成）
  - 支援多終端共享回傳頻寬，適合突發型流量

---

### 0.3 低層控制訊令（L2S, Lower Layer Signaling）

LLS 層包含廣播/單播控制表單，支撐同步與資源管理。

#### 0.3.1 控制與對時（Control and Synchronization）
- **NCR（Network Clock Reference）**
  - 系統時間基準（終端與控制中心對時）
  - 可視為整體系統「心跳訊號」

#### 0.3.2 資源管理表單（Resource Management Tables）
- **SCT**：Superframe Composition Table（定義超影格結構）
- **FCT2**：Frame Composition Table 2（定義 timeslot 結構）
- **BCT**：Broadcast Configuration Table（MODCOD 等參數）
- **TBTP2**：Terminal Burst Time Plan 2（動態分配上行時隙給終端）

---

## 0.4 概念對照表：DVB ↔ ns-3 ↔ SNS3（追 code 的索引）

| 層/名詞 | 真實系統意義 | ns-3（核心）對應 | SNS3 / satellite module 對應 | 與 DVB-S2X 的關係 |
|---|---|---|---|---|
| HLS / Network Layer | IP、管理、上層整合 | Internet stack、UDP/TCP/App | SNS3 承載 IP 流量（衛星作為 access/link model） | S2X 非此層重點（此層看端到端流量） |
| LLS / PHY+Access | 無線傳輸、接入、資源控制 | Channel/NetDevice 抽象、MAC/PHY 接口 | SNS3 satellite module 核心範圍 | S2X 差異在 PHY framing / 效率擴充 |
| FWD：GSE | 封裝、分段、重組、標籤 | ns-3 Packet | SNS3 提供 GSE 測試（encap/frag/reassembly） | 與 S2X 不衝突（L2/L3 適配層） |
| FWD：BBFRAME | 基頻影格容器 | 無直接對應（PHY 容器語意） | SatSignalParameters：FWD container 為 BBFrame | S2X 更強 framing；SNS3 至少保留 BBFrame 語意 |
| S2X Superframe | 多 PLFRAME 組織、同步/導頻 | 無直接對應 | 範例可見 SuperFrameConf 組態入口 | 追 code 切入點：SuperFrameConf 影響傳輸/排程 |
| RTN：RLE | 回傳封裝（低 overhead） | ns-3 Packet | SNS3 提供 RLE 測試（encap/frag/reassembly） | 與 S2X 不衝突（回傳封裝層） |
| RTN：MF-TDMA（Timeslot/BTU） | 多頻分時資源切分 | 事件排程/資源抽象 | 以 Tx opportunities 抽象資源分配 | 將 Timeslot/BTU 視為 scheduler 粒度 |
| RTN：FPDU / Burst | 回傳突發承載單位 | 無直接對應（PHY 容器語意） | SatSignalParameters：RTN container 為 FPDU | 與 burst 概念一致：FPDU = RTN 容器抽象 |
| L2S：控制訊息容器 | 控制表單體系 | 自訂 header/message | SNS3 有 control msg container 測試程式 | 重點是控制訊息如何影響 scheduler/配置 |
| NCR（對時） | 系統時間基準 | Simulator time + 同步邏輯 | 以鏈路時序/控制面機制對應 | 重點是「哪些模組用 NCR 對齊 frame/superframe」 |
| SCT/FCT2/BCT | 結構/配置表 | Attribute/Config 機制 | 以設定檔/組態物件映射 | 讀法：視為 configuration object 追參數流 |
| TBTP2 | 動態分配上行資源 | Scheduler/事件排程 | Tx opportunities + 控制面通知 | 追「誰算出 UT 上行機會、何時下發、如何影響傳輸」 |

> SNS3 相關文件（你原本引用的索引）  
> - Design Doc: https://www.sns3.org/doc/satellite-design.html  
> - Testing Doc: https://www.sns3.org/doc/satellite-testing.html  
> - API（示例）：SatSignalParameters / GSE test / RLE test / control msg container test  
>   - https://www.sns3.org/api/classns3_1_1_sat_signal_parameters.html  
>   - https://www.sns3.org/api/class_sat_gse_test_case.html  
>   - https://www.sns3.org/api/class_sat_rle_test_case.html  
>   - https://www.sns3.org/api/satellite-control-msg-container-test_8cc_source.html  
>   - https://www.sns3.org/api/sat-tutorial-example_8cc_source.html  

---

## 1. 實體層影格結構（Physical Layer Framing）

### 1.1 實體層影格與超影格（PLFRAME / Superframe）

#### 1.1.1 PLFRAME 組成
- **PLHEADER** + **XFECFRAME**
- **PLHEADER**
  - SOF：影格同步
  - PLSCODE：指示 MODCOD
- **XFECFRAME**
  - BBFRAME 經 FEC（LDPC / BCH）後形成

#### 1.1.2 Superframe（DVB-S2X）
- 長度通常：**612,540 symbols**
- 開頭：**SOSF + SFFI = 720 symbols**
- 內部分割：**CU（Capacity Unit）= 90 symbols / CU**

#### 1.1.3 NCRv2 同步（概念位置）
- SOF（或 SOSF）觸發時 latch 時鐘值，寫入 42-bit 欄位
- 支撐 RCST 與 NCC 對時，為 MF-TDMA 基礎

#### 1.1.4 VL-SNR
- 針對極低 SNR：VL-SNR header + pilots
- 強化同步與解碼成功率

---

### 1.2 SNS3 中的相關實作與模擬（對照表）

| 層級 / 模組 | SNS3 實作對象 | 對應 DVB-S2X / DVB-RCS2 機制 | 說明 |
|---|---|---|---|
| 裝置架構 | SatNetDevice | 協定堆疊（LLC/MAC/PHY） | 衛星端與終端的核心堆疊實作 |
| 系統拓樸 | GEO 多波束系統 | Spot beams + Gateways | 72 beams、5 gateways 的常見設定 |
| PHY 計算 | Packet-level SNIR | 接收條件與干擾 | 逐封包 SNIR + co-channel / intra-beam |
| 錯誤模型 | BER-based model | 解碼成功/失敗 | 依 BER 曲線決定是否解碼成功 |
| 前向調度 | ACM Scheduler | MODCOD 自適應 | 依鏈路狀況選擇 MODCOD |
| 回傳結構 | MF-TDMA Scheduler | Superframe/Frame/Timeslot | 資源切分與配置 |
| 存取控制 | DAMA | CRA/RBDC/VBDC | 需求導向資源配置 |
| 隨機存取 | Random Access | Slotted ALOHA/CRDSA | 突發/非即時流量 |
| 仿真介面 | FdNetDevice | 真實 IP 注入 | Tap/Emu 將真實流量帶入模擬 |

---

### 1.3 模擬環境與效能評估（表格）

| 模擬元件 | 功能角色 | 評估對象 | 可分析指標 |
|---|---|---|---|
| Packet Source | 封包產生 | 流量特性 | size distribution、traffic pattern |
| RLE Encapsulator | RTN 封裝 | 封裝效率 | overhead、聚合效率 |
| Scheduler | 資源配置 | 資源使用 | timeslot 利用率、delay |
| Burst Mapping | PHY 映射 | PHY 使用 | burst 大小、對齊效率 |
| Channel Model | 通道 | 可靠度 | SNR/BER/PER |
| FSL Model | 路徑損耗 | coverage | 接收功率、可用 MODCOD |
| Weather Traces | 天氣衰減 | Ka/Ku 影響 | throughput drop、error rise |
| LMS Model | 行動衛星通道 | 時變衰落 | Doppler、穩定度 |
| Stats/Tracing | 效能量測 | 系統結果 | throughput、delay、loss |

---

## 2. GSE 封裝與分段（GSE Encapsulation & Fragmentation）

> GSE 是連結網路層與實體層的橋樑，也是 SNS3 處理 IP 流量的重要機制。

### 2.1 GSE 封裝流程與核心功能

| 功能 | 說明 |
|---|---|
| PDU 定界與封裝 | 界定 PDU 起訖，加入控制資訊 |
| Header（Type/Label） | 支援 Unlabelled / 3B Label / 6B Label |
| Integrity Check | 建議於承載 GSE 的 BBFRAME 尾端插入 CRC32（提升錯誤偵測，特別在 ACM / mobile） |

### 2.2 分段機制（Fragmentation）

| 需求 | GSE 行為 |
|---|---|
| PDU 太大 | 切成多個 GSE Fragments 跨 BBFRAME 傳輸 |
| ACM 支援 | fragments 可落在不同 MODCOD 的 BBFRAME（提升 ACM gain） |
| 容量 | 支援至 64 kB PDU；支援 9 kB Jumbo frames（RCS2 新修訂導入 RTN） |

### 2.3 SNS3 模擬器中的實作對照

#### 2.3.1 實作位置
| 層級 | SNS3 實作角色 | 說明 |
|---|---|---|
| LLC | GSE Encap/Decap | queue、encap、frag、reassembly |
| NetDevice | SatNetDevice | 協定堆疊承載與資料流銜接 |
| PHY Interface | BBFRAME 輸入 | 封裝後資料送往 PHY 容器 |

#### 2.3.2 FWD / RTN 支援
| 鏈路 | 使用方式 | 模擬重點 |
|---|---|---|
| FWD | 主要封裝機制 | TDM 流量封裝進影格 |
| RTN | 可作為 RLE 替代 | 大封包/高吞吐場景 |

#### 2.3.3 可量測指標
| 指標 | 說明 |
|---|---|
| Throughput | 有效吞吐 |
| Delay | 分段/重組帶來的延遲 |
| Overhead | header/fragmentation 開銷 |
| Loss Rate | 惡劣通道下遺失率 |

---

## 3. 時鐘同步機制：NCR（Network Clock Reference）

> NCR 是互動衛星網路「心跳」，確保 RCST 與 NCC 對時，支撐 MF-TDMA。

### 3.1 NCR 機制（規格觀點）

#### 3.1.1 核心功能與原理
| 項目 | 說明 |
|---|---|
| 系統心跳 | 同步 RCST 內部時鐘的基礎時效資訊 |
| 42-bit counter | NCR 欄位 42 位元，反映 NCC 主時鐘 |
| NCRv2 | SOF / SOSF 觸發 latch 寫入 |
| 插入頻率 | 至少 10 次/秒；移動環境可至 32 次/秒（抗多普勒頻偏） |

#### 3.1.2 傳輸健壯性（Robustness）
| 項目 | 說明 |
|---|---|
| 最低 MODCOD | NCR 必須以最穩健 MODCOD 傳輸（波束邊緣也能收） |
| 同步流程 | RCST 需先接收 NCR 才能進入 Ready-for-Logon 與 TDMA 同步狀態 |

### 3.2 SNS3 對照

#### 3.2.1 時鐘模型與同步
| 面向 | DVB 規格 | SNS3 模擬 |
|---|---|---|
| 時鐘來源 | 硬體主時鐘 | ns-3 全域時鐘 |
| NCR 表示 | 42-bit | 映射為模擬時間 |
| 鎖定條件 | SOF/SOSF 符號邊界 | 事件觸發 |
| 精度重點 | 抖動/檢測精度 | propagation delay 計算與時序事件一致性 |

#### 3.2.2 MF-TDMA 時隙映射
| 面向 | DVB-RCS2 | SNS3 |
|---|---|---|
| 時間定義 | SCT 以 NCR ticks 表示起始 | 模擬時間對應 NCR 刻度概念 |
| 資源配置 | TBTP2 配置突發時間與頻率 | RTN scheduler 產生 Tx opportunities |
| 同步影響 | 誤差導致碰撞 | 可觀察 SNIR/干擾變化 |

#### 3.2.3 移動性與多普勒
| 面向 | DVB 指南 | SNS3 |
|---|---|---|
| 關注 | NGSO / 行動終端 | LMS + weather traces |
| 影響 | Doppler / 時變 | 衰減/漂移壓力測試 |
| NCR 角色 | 追蹤與補償基準 | 作為 TA/排程修正的時間基準 |

### 3.3 差異總結
| 特性 | DVB 規格 | SNS3 模擬 |
|---|---|---|
| 資料寬度 | 42-bit counter | 模擬時間 |
| 同步基準 | SOF/SOSF 實體符號 | 事件驅動 |
| 傳輸策略 | 最低 MODCOD 廣播 | 前向調度 + ACM |
| 功能定位 | 硬體同步/測距 | MAC/PHY 排程時間基準 |
| 物理誤差 | 抖動/漂移 | 不模擬硬體雜訊，但保留時序效應 |

---

## 4. 鏈路自適應與 MODCOD 切換（ACM Operation）

> ACM 讓系統在不同環境下平衡「可靠性」與「頻譜效率」。

### 4.1 ACM 運作（規格觀點）
| 元件 | 說明 |
|---|---|
| Feedback loop | RCST 定期回報 SNIR/接收能力資訊（回傳鏈路） |
| 決策與切換 | NCC 維護 MODCOD 能力表；每發送 BBFRAME 可重新選擇最佳 MODCOD（最大化 ACM gain） |
| 最低 MODCOD 保留 | 用於 NCR、TBTP2 等關鍵同步/控制訊息 |
| Short frame 取捨 | 低延遲 vs 編碼增益（短影格通常損失 0.2–0.3 dB 但節省填充浪費） |
| Pilots 建議 | 指南建議 ACM 環境常開 pilots（維持鎖定能力） |

### 4.2 SNS3（Simulation）對照
| SNS3 模擬點 | 說明 |
|---|---|
| Scheduler role | ACM 邏輯主要位於 FWD link scheduler |
| Packet-by-packet SNIR | 逐封包 SNIR + co-channel / intra-beam 干擾追蹤 |
| BER-based error model | 可模擬高階 MODCOD 下通道突變造成解碼失敗 |
| 模擬結果（你提供的敘述） | 前向鏈路頻譜效率可飽和至約 3.3 b/s/Hz，代表良好條件下使用 32APSK 等高階調變 |
| Channel dynamics | weather traces + LMS 作為 ACM 切換壓力測試環境 |

### 4.3 核心對照表
| 特性 | DVB-RCS2 指南 | SNS3 模擬 |
|---|---|---|
| 決策點 | NCC / Hub | FWD link scheduler |
| 回饋輸入 | 定期回報 SNIR | 逐封包動態 SNIR + 干擾追蹤 |
| 影格處理 | 可用短影格降延遲 | 保留 BBFRAME 容器架構 |
| 切換頻率 | 可每 BBFRAME 切換 | 支援 packet-level 動態切換 |
| 物理限制 | HPA 非線性影響 | BER 曲線 + 非線性失真模型評估 |

---

## 5. 回傳鏈路的新應用（DVB-S2X 導入 RTN）

### 5.1 DVB-S2X 正式導入回傳鏈路
- 舊連續載波（continuous carrier mode）採用率低 → 規範趨勢改用 DVB-S2X 波形取代
- DVB-S2X 波形可用於：
  - 連續載波（continuous）
  - MF-TDMA 網格內 timeslot（burst）
- 引入 Beam Hopping 波形：長 superframe 起始（SOSF）preamble 可輔助 burst acquisition

### 5.2 Jumbo Frames 與 GSE 支援
- 傳統 RLE 限制 PDU 約 4 kB，不利於 9 kB jumbo frames
- 新規範允許 RTN 使用 GSE 作為 RLE 替代：
  - GSE 支援 64 kB PDU
  - 支援 jumbo frames 需求

### 5.3 頻譜效率與符號率擴充
- Lower roll-off：20% → 引入 10% 與 5%（更密集頻域網格）
- 擴展符號率：支援至 500 Msps；相關信令表欄位由 24-bit 擴展至 32-bit（如 FCT3）

### 5.4 RCST 四種新運作模式
1. DVB-RCS2 波形（線性/擴頻/CPM）burst mode  
2. DVB-S2X 波形 burst mode  
3. DVB-S2X 波形非持久性 continuous（依流量在 MF-TDMA/continuous 切換）  
4. DVB-S2X 波形持久性 continuous  

### 5.5 終端自主選擇 MODCOD
- 傳統：MODCOD 由 Hub（NCC）決定
- 新版：允許終端自主選擇 S2X MODCOD
  - Hub 透過 RTMST 表提供可用 MODCOD 列表
  - 終端依自身鏈路狀況選最佳參數

### 5.6 SNS3 對照實作（你提供的整理）
| 面向 | SNS3 支援點 |
|---|---|
| 協定棧 | 完整 DVB-RCS2 + DVB-S2；含 RLE/GSE encapsulator |
| 多樣化存取 | DAMA（CRA/RBDC/VBDC/FCA）與隨機存取（含 CRDSA） |
| 評估價值 | 用於比較新 RTN 機制下的回傳效率與配置策略 |
