衛星物理資源的分配 (Time vs. Frequency)
## 1.1 Frequency
 - carrier : 總頻寬被切分為多個載波（Carriers）。
 - ISL 資源: 點對點固定頻寬
 - User Link: 不同的 Beam 可以使用相同的頻率
## 1.2 Time
- Slots:最小分配單位
- Frame: 20ms-30ms
- Beam Hopping Window :Frames組成的週期
  

|機制|執行層級 (Layer)|執行時機 (When)|執行方式 (How)|
|-|-|-|-|
|Routing|網路層 (L3)|模擬啟動前或衛星切換時 (30s~60s)|透過 Ipv4StaticRouting 限縮 FT-Sat-UT 的pipeline|
|Beam Hopping|跨層 (L2/L1)|週期性執行 (10ms~50ms)|控制器檢查各波束隊列，透過 SatBeamHelper 開關波束物理層增益|
|Scheduling|MAC 層 (L2)|每一訊框 (Per Frame) (1ms~5ms)|根據 UT 請求 (RBDC) 與 QoS 權重，在當前開啟的波束中分配時隙|

---

|類別|參數名稱|建議值|說明|
|-|-|-|-|
|LEO 軌道|Altitude|600 km|影響傳輸延遲與可視窗口|
|ISL 鏈路|IslDataRate|1 Gbps|衛星間骨幹頻寬|
|路由限縮|UpdateInterval|30 s|重新計算最佳 FT pair 的時間|
|BH 週期|T_bh|20 ms|波束在所有 Cell 間跳轉一次的總時間|
|BH 停留|DwellTime|2 ms (min)|波束停留於單一 Cell 的最小單位|
|QoS 權重|WFQ_Weights|5:3:2|Gold, Silver, Bronze 的頻寬佔比。|




Routing：Helper 建場景時，屬於 slow control-plane\
Beam hopping：SatBstpController（通常掛在 GW/beam 控制）按 plan 定期 toggle beam\
Scheduling：SatMac 每 frame/round 呼叫 SatFwdLinkScheduler，屬於 fast data-plane\

(A) `sat-qos-flowid-fwd-min.cc`
控制面整合 + 參數注入，將 policy 與 tag 放進系統
- 定義：scenarioFolder、simTime、beamId、traffic profile、QoS class/tag
- 呼叫 Helper 建場景、裝置、應用
- 註冊 trace / 設定輸出路徑（outDir）
- Inputs：scenarioFolder / simTime / beamId / QoS weights / app rates
- Outputs：run.out、CSV traces

(B) SatHelper / SimulationHelper（建場景）
把情境變成 ns-3 objects，config（scenario）→ ns-3 object graph
- 讀 scenario folder（星系/beam/UT/GW 佈局、link 設定）
- Create nodes（SAT/GW/UT）、install stacks、connect ISL/GSL
- 產生並 attach SatNetDevice 到每個 node


(C) SatNetDevice（裝置層）
把上層封包引入 satellite stack + 接好 MAC/PHY，FlowId / QoS tag 通常在進 MAC 前要被保存/轉譯，否則scheduler 看不到。

- Application 層送 packet 會進到 NetDevice
- NetDevice 把 packet 交給對應的 MAC（forward/return 方向）

(D) SatMac（MAC：queueing + 封裝 + 與 scheduler 互動）
隊列管理與送出觸發
- 收到 packet → enqueue 到某個 queue（按 flow/class 分開）
- 每個 frame tick / grant 觸發 → 呼叫 scheduler 決定要服務誰、服務多少
- 依 scheduler decision dequeue 封包送到 PHY


(E) SatFwdLinkScheduler
資源分配決策（per frame/per round）
- Input：各 queue backlog、HOL delay、served history、權重
- Output：本輪服務清單（誰被 serve、serve 幾 bytes/slots）

served_bytes.csv / tx_outcome.csv / seen_objects.csv 都是這層

<img width="1144" height="1472" alt="mermaid-diagram" src="https://github.com/user-attachments/assets/c951f13a-2cd1-4817-98ea-a4fb0f9dea6a" />
