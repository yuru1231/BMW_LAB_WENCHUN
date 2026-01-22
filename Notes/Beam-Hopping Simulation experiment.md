## 1. 模擬驗證 Beam Hopping 邏輯本身，不受 traffic / scheduler / congestion 影響
### 1.1 可判定的條件
- Period 正確：每次切換間隔 Δt = hoppingPeriod
- Switch 時刻正確：在預期的時間點發生
- Active set 正確：每個時刻的 active beam ID(s) 與 schedule 一致
- 無重疊：不應出現兩個互斥 beam 同時 ON（除非你的配置允許 multi-beam）。
- 無空窗：不應出現全部 OFF（除非設計上有 guard time/idle）。
- Determinism：同一組設定、多次 run，log 序列一致（或至少在容許抖動內一致）

## 1.2 Keep BeamHopping State Machine
### A.不產生 traffic
- 不啟動 CBR/OnOff/任何 App
- 保留基本 topology + beam hopping controller

### B.有 traffic 但不影響
- 封包大小設到極小、速率設到極低、或 appStartTime 設到模擬結束

## 1.3 STEP
### Phase 0 查看原始參數
```
cd ~/workspace/bake/source/ns-3.43 && \
SIM=60; \
OUT="$HOME/beam_hopping/results_verify"; \
LOG="$HOME/beam_hopping/logs/run_bh_verify_$(date +%Y%m%d_%H%M%S).log"; \
XML="contrib/satellite/examples/sys-rtn-test.xml"; \
SEED=1; RUN=1; \
mkdir -p "$OUT" "$(dirname "$LOG")" && \
{ \
  echo "===== Beam Hopping Verify Config ====="; \
  echo "simLength:  $SIM"; \
  echo "InputXml:  $XML"; \
  echo "OutputPath:$OUT"; \
  echo "RngSeed:   $SEED"; \
  echo "RngRun:    $RUN"; \
  echo; \
  ./ns3 run "sat-beam-hopping-test-example \
    --simLength=$SIM \
    --InputXml=$XML \
    --OutputPath=$OUT \
    --RngSeed=$SEED \
    --RngRun=$RUN"; \
} 2>&1 | tee "$LOG"
```

> 補充工具
- `PrintHelp`：確認哪些參數能從 command line 設定
- `PrintGlobals`：確認是否支援 RNG 固定


### Phase 1 No-traffic BeamHopping verification
- utAppStartTime > simLength
- trafficModel 保留設 0
- 固定 RngSeed / RngRun
- 週期性 Beam Hopping（t = 10,20,30,…）
```
cd ~/workspace/bake/source/ns-3.43

SIM=60
OUT="$HOME/beam_hopping/results_long"
LOG="$HOME/beam_hopping/logs/no_traffic_bh_sim${SIM}_$(date +%Y%m%d_%H%M%S).log"
mkdir -p "$OUT" "$(dirname "$LOG")"

./ns3 run "sat-beam-hopping-test-example \
  --trafficModel=0 \
  --utAppStartTime=+100000s \
  --simLength=$SIM \
  --OutputPath=$OUT \
  --RngSeed=1 \
  --RngRun=1" 2>&1 | tee "$LOG"
```
Beam Hopping 原始 Log
```
grep "\[BeamHopping\]" "$LOG"
```
OUTPUT
``` 
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=30s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=40s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=50s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=60s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=30s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=40s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=50s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=60s: Beam 27 -> OFF, Beam 26 -> ON
```
- [X] Δt 是否等於 period (短期驗證一次)
- [X] beam 切換序列是否符合預期（26 ↔ 27）
- [X] 是否存在 overlap / idle
<img width="770" height="607" alt="螢幕擷取畫面 2026-01-14 165502" src="https://github.com/user-attachments/assets/717db3fa-bc7f-49c4-a42f-d9d59f95ff48" />
<img width="581" height="457" alt="螢幕擷取畫面 2026-01-14 165523" src="https://github.com/user-attachments/assets/30b09765-6864-4119-b24f-dff055f272ed" />


### Phase 2 Determinism test
```
wenj@wenj-virtual-machine:~/workspace/bake/source/ns-3.43$ OUT="$HOME/beam_hopping/results_determinism"
mkdir -p "$OUT" "$HOME/beam_hopping/logs"

for i in 1 2; do
  LOG="$HOME/beam_hopping/logs/determinism_run${i}.log"
  ./ns3 run "sat-beam-hopping-test-example \
    --trafficModel=0 \
    --utAppStartTime=+100000s \
    --simLength=120 \
    --OutputPath=$OUT \
    --RngSeed=1 \
    --RngRun=1" 2>&1 | tee "$LOG"
  grep "\[BeamHopping\]" "$LOG" > "$OUT/run${i}.txt"

```
```
--- sat-rtn-sys-test ---
  Test case: 0
  Traffic model: 0
  Simulation length: 120
  Number of UTs: 10
  Number of end users per UT: 10
  
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=30s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=40s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=50s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=60s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=70s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=80s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=90s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=100s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=110s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=120s: Beam 27 -> OFF, Beam 26 -> ON
Trace not found: RbdcTrace (skipped)
Trace not found: VbdcTrace (skipped)
Trace not found: AvbdcTrace (skipped)
Trace not found: DaResourcesTrace (skipped)
--- sat-rtn-sys-test ---
  Test case: 0
  Traffic model: 0
  Simulation length: 120
  Number of UTs: 10
  Number of end users per UT: 10
  
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=30s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=40s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=50s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=60s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=70s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=80s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=90s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=100s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=110s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=120s: Beam 27 -> OFF, Beam 26 -> ON
```
- [X] beam hopping log(重複驗證)

### Phase 3 Parameter sweep sanity check
#### Phase 3.1 
```
cd ~/workspace/bake/source/ns-3.43

OUT="$HOME/beam_hopping/results_sweep"
mkdir -p "$OUT" "$HOME/beam_hopping/logs"

for SIM in 60 120 300; do
  LOG="$HOME/beam_hopping/logs/phase3_sim${SIM}_$(date +%Y%m%d_%H%M%S).log"

  echo "===== Phase 3: simLength=$SIM ====="
  ./ns3 run "sat-beam-hopping-test-example \
    --trafficModel=0 \
    --utAppStartTime=+100000s \
    --simLength=$SIM \
    --OutputPath=$OUT \
    --RngSeed=1 \
    --RngRun=1" 2>&1 | tee "$LOG"

  CNT=$(grep "\[BeamHopping\]" "$LOG" | wc -l)
  echo "BeamHopping events: $CNT (expected ≈ $((SIM/10)))"
  echo
done

```
```
===== Phase 3: simLength=60 =====
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
BeamHopping events: 6 (expected ≈ 6)

===== Phase 3: simLength=120 =====
Trace not found: RbdcTrace (skipped)
Trace not found: VbdcTrace (skipped)
Trace not found: AvbdcTrace (skipped)
Trace not found: DaResourcesTrace (skipped)
--- sat-rtn-sys-test ---
  Test case: 0
  Traffic model: 0
  Simulation length: 120
  Number of UTs: 10
  Number of end users per UT: 10
  
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=30s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=40s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=50s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=60s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=70s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=80s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=90s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=100s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=110s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=120s: Beam 27 -> OFF, Beam 26 -> ON
BeamHopping events: 12 (expected ≈ 12)

===== Phase 3: simLength=300 =====
Trace not found: RbdcTrace (skipped)
Trace not found: VbdcTrace (skipped)
Trace not found: AvbdcTrace (skipped)
Trace not found: DaResourcesTrace (skipped)
--- sat-rtn-sys-test ---
  Test case: 0
  Traffic model: 0
  Simulation length: 300
  Number of UTs: 10
  Number of end users per UT: 10
  
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=30s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=40s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=50s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=60s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=70s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=80s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=90s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=100s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=110s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=120s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=130s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=140s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=150s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=160s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=170s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=180s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=190s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=200s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=210s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=220s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=230s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=240s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=250s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=260s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=270s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=280s: Beam 27 -> OFF, Beam 26 -> ON
[BeamHopping] t=290s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=300s: Beam 27 -> OFF, Beam 26 -> ON
BeamHopping events: 30 (expected ≈ 30)

```
Table of different simLength
|simLength (s)|period=10s|
|---|---|
|60|~6|
|120|~12|
|300|~30|
