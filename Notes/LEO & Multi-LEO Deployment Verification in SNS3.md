## verify using LEO environment
> Reference
- https://www.projectguideline.com/installing-sns3-satellite-network-extension-module-under-ns-3/
- https://github.com/sns3/sns3-data/tree/master/scenarios
### 0 Setup 
```
#ns-3 root directory
export NS3=~/workspace/bake/source/ns-3.43

#Project workspace
export PROJ=~/beam_hopping/sns3_leo_env
mkdir -p "$PROJ/logs" "$PROJ/results"

#Output Directory Preparation
OUT="$PROJ/results/leo2_verify_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
echo "OUT=$OUT"

#Run sat-beam-position-tracer
cd "$NS3"
#執行 tracer 並保存 stdout log
LOG="$PROJ/logs/run_sat-beam-position-tracer_$(date +%Y%m%d_%H%M%S).log"
./ns3 run "sat-beam-position-tracer --OutputPath=$OUT" | tee "$LOG"
echo "LOG=$LOG"
```
從執行檔 `sat-beam-position-tracer` 得知Tracer 每一列輸出格式
```
time_s ; satId ; beamId ; centerLat_deg ; centerLon_deg
```
### 1.Proof
#### 1.1 Proof 1 地面投影移動速度：beam center longitude 隨時間的變化量作為判斷依據
- LEO：地面投影在數分鐘內會有明顯經緯度變化
- GEO：地面投影在短時間內近似固定（經度幾乎不變）

選定單一衛星與單一波束`satId=0, beamId=60`
```
awk -F';' 'NF==5 && $2==0 && $3==60 {print $1,$5}' "$LOG" | head
awk -F';' 'NF==5 && $2==0 && $3==60 {print $1,$5}' "$LOG" | tail
```
```
#time_s longitude
0 113.604
10 113.966
20 114.328
30 114.691
40 115.055
...
180 120.264
```
> 180 秒內經度變化約 6.66° ,驗證為LEO模擬

#### 1.2 Proof 2 存在多顆衛星
tracer 輸出中是否同時存在 多個不同的 `satId`，且具有實質資料量
```
awk -F';' 'NF==5{print $2}' "$LOG" | sort -n | uniq -c
```
```
1368 0
1368 1
```

#### 1.3 Proof 3 t Multi-LEO Operation
證明LEO 為同時運作，而非時間輪替
```
awk -F';' 'NF==5 {print $1,$2}' "$LOG" \
  | sort -n | uniq \
  | awk '{t=$1; s[t]=s[t]" "$2} END{for (t in s) print t ":" s[t]}' \
  | awk 'NF>=3' \
  | sort -n | head -n 20
```
```
0:   0 1
10:  0 1
20:  0 1
...
180: 0 1
```
>　多個模擬時間點（0s、10s、20s、…、180s），同時觀察到 satId=0 與 satId=1 的輸出記錄
