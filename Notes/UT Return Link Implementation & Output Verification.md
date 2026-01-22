UT Return Link Implementation & Output Verification
> Reference:
-https://github.com/sns3/sns3-satellite/tree/0fc2b8c74f0d9c2b0c3ee4ed132064a40ad2daf1
- 使用基本指令驗證內容
### 1.1 Environment Setup and Execution

**Objective**
驗證 UT (User Terminal) return link成功執行，並正確產出資訊

**Commands Executed**

```bash
cd ~/workspace/bake/source/ns-3.43
mkdir -p ~/rtn_today/{c0,c1,logs}

./ns3 run "sat-rtn-system-test-example \
  --frameConf=Configuration_0 \
  --trafficModel=0 \
  --simLength=30 \
  --OutputPath=$HOME/rtn_today/c0" | tee $HOME/rtn_today/logs/run_c0.log

./ns3 run "sat-rtn-system-test-example \
  --frameConf=Configuration_1 \
  --trafficModel=0 \
  --simLength=30 \
  --OutputPath=$HOME/rtn_today/c1" | tee $HOME/rtn_today/logs/run_c1.log
```

**說明**:
在相同的流量條件下（CBR，30 秒）執行兩次(CBR, 30 seconds).僅更改 `frameConf`隔離不同 MF-TDMA superframe 在 UT return link 影響


### 1.2 Output  Verification

**Objective**
確認模擬器產生有效的 UT、波束、GW 和全域統計輸出檔。

**Command**

```bash
echo "=== c0 outputs ==="
find $HOME/rtn_today/c0 -maxdepth 2 -type f -printf "%p\n" | sort

echo "=== c1 outputs ==="
find $HOME/rtn_today/c1 -maxdepth 2 -type f -printf "%p\n" | sort
```

**Result**
輸出目錄包含多個統計文件，涵蓋應用層吞吐量、MAC/PHY 吞吐量、延遲分佈以及每個 UT/每個波束/每個網關的指標，表明模擬執行成功。

---

### 1.3 UT-Centric Performance Metrics

**Objective**
識別並解釋用於評估 UT 在 return link的表現.

#### 1.3.1 Per-UT Application Throughput

* **File**: `stat-per-ut-rtn-app-throughput-scalar.txt`
* **Meaning**: Average application-layer throughput achieved by each UT.
* **Purpose**: 評估 MF-TDMA 調度和 superframe配置如何影響單一終端單元 (UT) 的效能和公平性。
#### 1.3.2 Global / Per-Beam Application Throughput

* **Files**:
  * `stat-global-rtn-app-throughput-scalar.txt`
  * `stat-per-beam-rtn-app-throughput-scalar.txt`
* **Meaning**: 系統和波束層級的聚合回傳鏈路吞吐量。
* **Purpose**: 表示系統整體容量，並反白顯示不同配置的影響。

#### 1.3.3 UT Application Delay Distribution

* **File**: `stat-average-ut-user-rtn-app-delay-cdf.txt`
* **Meaning**: UT 應用層資料包延遲的累積分佈函數 (CDF)。
* **Purpose**: 捕捉 MF-TDMA frame/slot分配所引入的調度延遲。

---

### 1.4 Architecture Reference (Figure 1)

![Fig(1) Return Link](https://github.com/user-attachments/assets/d237e937-2989-4546-9800-9927a4b59fa1)

**Description**
Figure 1 UT-衛星-GW Return Link架構。應用層產生的 UT 流量在 DVB-LLS 存取層根據 MF-TDMA 訊框/載波/時隙結構進行資料包化和調度。衛星以彎管模式運行，將上行鏈路訊號直接中繼到網關，網關接收、解碼並聚合突發訊號。 

### 1.5 MF-TDMA 資源配置
<img width="5820" height="785" alt="Untitled diagram-2026-01-12-091823" src="https://github.com/user-attachments/assets/679e7e15-989c-4cfe-be97-33fbcbace3ae" />
在頻率維度上，回程頻寬被切分為多個 carrier；在時間維度上，每個 carrier 再被切分為多個 time slot。 一個 time–frequency 格子（carrier × slot）對應一個 UT 在特定時間與頻率上傳送 burst 的機會。 不同的 superframe configuration 透過改變每個 frame 的 AllocatedBandwidth 與 CarrierAllocatedBandwidth，影響 carrier 的數量，進而改變 MF 維度大小。 carrier 切得越細，代表可同時服務的 UT 數量越多；carrier 切得越粗，則 MF 維度較小。

### 1.6 時間結構上的階層關係

<img width="1300" height="2680" alt="Untitled diagram-2026-01-12-091839" src="https://github.com/user-attachments/assets/9782c973-531c-4f56-8882-55c697954ee9" />
最外層為 Superframe，由多個 frame 所組成；每個 frame 再被切分為多個 slot，而每個 slot 則承載一個 burst。
為避免不同 UT 傳輸間的干擾，每個 burst 前後皆包含 guard time，以容忍傳輸延遲與同步誤差。

部分 frame 可被設定為 Random Access Frame，允許 UT 於該 frame 中進行隨機接入；其餘 frame 則用於正常排程回傳。
透過調整 superframe 中 frame 的數量、類型與配置，可靈活控制回程鏈路的排程週期與接入行為。
