
# SNS3 Satellite Trace Output 實作筆記

> 本文件記錄在 **ns-3.43 + SNS3-satellite** 環境下，  
> 從環境確認、介面理解、模組建置，到成功執行 `sat-trace-output-example`，  
> 並產生 **衛星通道與吞吐量分析輸出** 的完整流程。

---
> Refrence
- https://github.com/kevin940822-beep/OpenAirInterface-NTN-Integration/blob/main/sns3/sns3-sat-rtn-system-test_note.md
- https://github.com/kevin940822-beep/OpenAirInterface-NTN-Integration/blob/main/sns3/SNS3_installation.md
- https://github.com/liang924/SNS3/blob/dfde8e9b69fbb50e9f6159a1a4ac4c7c1d0bb9b0/SNS3%20Installation.md
## 0. 環境與版本確認

### 0.1 確認 ns-3 版本

```bash
cd ~/workspace/bake/source/ns-3.43
ls VERSION 2>/dev/null && cat VERSION
````

**Output**

```text
VERSION
3.43
```

---

### 0.2 確認 ns-3 Wrapper 介面

```bash
cd ~/workspace/bake/source/ns-3.43
./ns3 --help | head -n 80
```

**Output（節錄）**

```text
ns-3 wrapper for the CMake build system

usage: ns3 [-h] [--dry-run] [-j JOBS] [--quiet] [-v]
           {help,build,configure,clean,distclean,install,uninstall,run,shell,docs,show}
           ...
```

**說明**

* `configure`：設定模組與功能
* `build`：編譯整個專案或指定 target
* `run`：執行 runnable target
* `show`：顯示 build profile / modules / targets

---

### 0.3 建置流程紀錄（保存 log）

```bash
cd ~/workspace/bake/source/ns-3.43

./ns3 clean     |& tee ~/workspace/bake/source/logs/ns3_clean_$(date +%Y%m%d).txt
./ns3 configure |& tee ~/workspace/bake/source/logs/ns3_configure_ok_$(date +%Y%m%d).txt
./ns3 build     |& tee ~/workspace/bake/source/logs/ns3_build_ok_$(date +%Y%m%d).txt
```

**目的**

* 確保 build 狀態乾淨
* 留存可回溯的建置紀錄
---

## 1. 測試環境與 Runnable Targets

### 1.1 檢查是否啟用 examples

```bash
./ns3 show
```

**Output（重點）**

```text
Examples : OFF (not requested)
```

---

### 1.2 啟用 examples 並重新 build

```bash
cd ~/workspace/bake/source/ns-3.43
./ns3 configure --enable-examples
./ns3 build
```

**結果**

* Runnable targets 數量增加
* 可使用 satellite / dvb-s2 相關示範程式

```text
Runnable/Buildable targets: 354
```

**已建置模組（節錄）**

```text
satellite
stats
traffic
magister-stats
mobility
spectrum
```

---

## 2. SNS3 Satellite 模擬實作

### 2.1 建立結果資料夾

```bash
mkdir -p ~/workspace/bake/source/results
```

---

### 2.2 執行 sat-trace-output-example
https://www.sns3.org/api/sat-trace-output-example_8cc.html?utm_source=chatgpt.com
```bash
cd ~/workspace/bake/source/ns-3.43
./build/contrib/satellite/examples/ns3.43-sat-trace-output-example-default
```

**用途說明**

此範例會啟動一個：

> 「**最小但完整**」的衛星通訊系統，
> 並將 **通道（Channel）／鏈路（Link）／流量（Traffic）**
> 狀態完整輸出成 trace 與統計檔案。

---

### 2.3 模擬系統架構（完整訊號路徑）

```
CBR Application (Sender)
        ↓
Satellite MAC / Scheduler
        ↓
Satellite PHY abstraction
        ↓
Satellite Channel
   (Forward / Return)
   (User / Feeder)
        ↓
Gateway / User Terminal
        ↓
Packet Sink (Receiver)
```

---

### 2.4 Console Banner 與 Scenario 說明

```text
--- Trace-output-example ---
Scenario used: simple
```

**`simple` Scenario 意義**

* Gateway × 1
* User Terminal × 1
* Forward link × 1
* Return link × 1

👉 用於專注驗證「單一衛星鏈路」的通道與效能模型。

---

## 3. Traffic 與 Application 行為

### 3.1 Traffic 參數（由 example 原始碼設定）

| 參數         | 意義            |
| ---------- | ------------- |
| PacketSize | 每個封包大小（bytes） |
| Interval   | 每隔多久送一次封包     |

**本次設定**

```text
PacketSize: 512
Interval: 1s
```

**Traffic 類型**

* CBR（Constant Bit Rate）
* 每 1 秒送出 512 bytes
* 資料會送入 Satellite MAC / PHY

---

### 3.2 Application 封包流程

```
Application → MAC → PHY → Channel → PHY → MAC → Sink
```

Console log 中可看到：

```text
At time Xs cbr application sent 512 bytes
At time Ys packet sink received 512 bytes
```
---

## 4. Physical Channel 層（Layer 1）輸出

SNS3 在模擬期間會計算並輸出：

* Rx Power（接收功率）
* Composite SINR
* Interference
* Fading

### 4.1 Trace 檔名解析

範例：

```text
rx_power_output_trace_BEAM_8_UT_1_channelType_FORWARD_USER_CH
```

| 片段      | 意義                     |
| ------- | ---------------------- |
| BEAM_8  | 使用的 beam 編號            |
| UT_1    | User Terminal          |
| FORWARD | Forward link           |
| USER_CH | User channel（非 feeder） |

👉 表示 SNS3 明確區分不同衛星通道角色。

---

## 5. Throughput 與統計輸出（Layer 2 / Layer 3）

SNS3 使用 `stats / magister-stats` 模組輸出吞吐量統計。

### 5.1 統計檔案範例

```text
stat-global-fwd-app-throughput-scalar.txt
stat-per-ut-rtn-user-mac-throughput-scatter-1.txt
```

### 5.2 檔名結構解析

| 區段                       | 意義                |
| ------------------------ | ----------------- |
| global / per-ut / per-gw | 全系統 / 單 UT / 單 GW |
| fwd / rtn                | Forward / Return  |
| app / mac                | 應用層 / MAC 層       |
| scalar                   | 平均或總量             |
| scatter                  | 時間序列              |

👉 此層結果可直接對應 **DVB-S2 的有效吞吐量分析**。

---

## 6. 本次實驗總結與定位

### 已完成

* ✅ SNS3 satellite 模組成功執行
* ✅ 通道層（SINR / RxPower / Fading）trace 輸出
* ✅ Throughput 統計資料產生
* ✅ 建立模擬 → trace → 分析 的完整流程

### 後續銜接方向

* **DVB-S2**：Link budget / MODCOD / throughput
* **Hypatia**：Link capacity / routing / end-to-end performance

```

```
