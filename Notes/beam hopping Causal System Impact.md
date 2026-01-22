### Phase 0 環境設定
#### 0.1 創建資料夾(避免混淆)
```
cd ~/workspace/bake/source/ns-3.43
export ROOT=$HOME/beam_hopping/traffic_aware_beam_hopping
mkdir -p \
  $ROOT/baseline_fixed_beam/{traces,logs} \
  $ROOT/dynamic_beam_hopping/{traces,logs} \
  $ROOT/{plots,scripts}
echo "ROOT=$ROOT"
find "$ROOT" -maxdepth 3 -type d | sort
ls -R "$ROOT"
```
#### 0.2 參數清單(PrintHelp)
```
cd ~/workspace/bake/source/ns-3.43

./ns3 run "sat-beam-hopping-test-example --PrintHelp" \
  | tee $ROOT/dynamic_beam_hopping/logs/printhelp.txt
```
#### 0.3 複製資料夾(不變更原始檔案)
```
cp contrib/satellite/examples/sys-rtn-test.xml \
  $ROOT/baseline_fixed_beam/traces/sys-rtn-test-baseline.xml

cp contrib/satellite/examples/sys-rtn-test.xml \
  $ROOT/dynamic_beam_hopping/traces/sys-rtn-test-hopping.xml
```
### Phase 1
#### 1.1 設定共用變數
```
cd ~/workspace/bake/source/ns-3.43

export ROOT=$HOME/beam_hopping/traffic_aware_beam_hopping
export SIMLEN=60
export APPSTART=0.1   # 對應 +100ms

```
#### 1.2 修改 Beam hopping 的核心控制（Enable + Period）
``` 
bool enableBh = true;
double bhPeriodSec = 10.0;

cmd.AddValue("enableBh", "...", enableBh);
cmd.AddValue("bhPeriodSec", "...", bhPeriodSec);

```
#### 1.3 用 Simulator::Schedule 交替切換 Beam Set
for-loop，讓 hopping 可以跑到 sim end，並且每個週期交替 beamA/beamB：
``` 
if (enableBh)
{
  for (double t = periodSec; t <= simLengthSec; t += periodSec)
  {
    // odd/even 交替
    Simulator::Schedule(Seconds(t), &SwitchBeamSet, helper, offBeam, onBeam);
  }
}
else
{
  NS_LOG_INFO("Baseline mode: beam hopping disabled");
}
```
[sat-beam-hopping-test-example.txt](https://github.com/user-attachments/files/24634244/sat-beam-hopping-test-example.txt)
### Phase 2 Modes
#### 2.1 baseline
```
./ns3 run "sat-beam-hopping-test-example \
  --simLength=$SIMLEN \
  --utAppStartTime=${APPSTART}s \
  --trafficModel=0 \
  --enableBh=false \
  --OutputPath=$ROOT/baseline_fixed_beam/traces" \
| tee $ROOT/baseline_fixed_beam/logs/run_baseline.log
```
```
Trace not found: RbdcTrace (skipped)
Trace not found: VbdcTrace (skipped)
Trace not found: AvbdcTrace (skipped)
Trace not found: DaResourcesTrace (skipped)
--- sat-rtn-sys-test ---
  Test case: 0
  Traffic model: 0
  Simulation length: 60
  Number of UTs: 10
  Number of end users per UT: 10
  
Baseline mode: beam hopping disabled

```
### 2.2 hopping
```
./ns3 run "sat-beam-hopping-test-example \
  --simLength=$SIMLEN \
  --utAppStartTime=${APPSTART}s \
  --trafficModel=0 \
  --enableBh=true \
  --bhPeriodSec=10 \
  --OutputPath=$ROOT/dynamic_beam_hopping/traces" \
| tee $ROOT/dynamic_beam_hopping/logs/run_hopping.log
```
```
Trace not found: RbdcTrace (skipped)
Trace not found: VbdcTrace (skipped)
Trace not found: AvbdcTrace (skipped)
Trace not found: DaResourcesTrace (skipped)
--- sat-rtn-sys-test ---
  Test case: 0
  Traffic model: 0
  Simulation length: 60
  Number of UTs: 10
  Number of end users per UT: 10
  
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=30s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=40s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=50s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=60s: Beam 27 -> OFF, Beam 26 -> ON
```

### Phase 3 Sanity Check
#### 3.1 RTN Application Throughput CSV
command
```
head -n 10 $ROOT/baseline_fixed_beam/traces/rtn_app_throughput.csv
head -n 10 $ROOT/dynamic_beam_hopping/traces/rtn_app_throughput.csv
```
- time_s 模擬時間（秒）
- delta_bytes 本秒接收的位元組增量
- throughput_mbps 即時吞吐量（Mbps）
- total_rx_bytes 累積接收位元組


#### 3.2 Beam Hopping 紀錄 CSV
```
cat $ROOT/dynamic_beam_hopping/traces/bh_active_beam.csv
```
```
time_s,active_beam,off_beam
10,27,26 
20,26,27
30,27,26
40,26,27
50,27,26
60,26,27

=> hopping 週期 10 秒
=> beamA / beamB 交替切換
```
### Phase 4 Throughput vs Time

<img width="653" height="488" alt="螢幕擷取畫面 2026-01-15 203851" src="https://github.com/user-attachments/assets/e1e8ab8b-b060-46b3-aafb-153808ac5fdc" />

- X 軸：Simulation Time (s)
- Y 軸：Throughput (Mbps)
- 藍線：Baseline（固定 Beam）
- 橘線：Beam Hopping（10 秒週期）
- 藍色虛線：Beam 切換時間點

### Milestone
- [X] Baseline / Beam hopping 皆可穩定執行
- [X] Beam hopping 時序正確
- [X] Traffic 正確安裝至 UT user nodes
- [X] Throughput CSV
