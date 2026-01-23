# Task 1 : SNS3-native LEO Constellation (Mobility only; no comms)
## 1. Instantiate LEO satellites using SNS3 (SGP4/TLE)
Objective:\
Instantiate a LEO satellite constellation using the **SNS3 satellite framework**, where satellite mobility is driven by the **built-in SGP4/TLE mobility model**, not by any custom orbit or motion code.

### 1.1 Scope and Assumptions
- No traffic, beam scheduling, or link-layer performance is considered in this task.
- Only on constellation instantiation and satellite mobility.
- All satellite positions must originate from SNS3 internal mobility models.

### 1.2 Method
#### 1.2.1 檢查可用範例和參數
```
cd ~/workspace/bake/source/ns-3.43
./ns3 run "sat-constellation-example --PrintHelp"
```
output
```
[Program Options] [General Arguments]

Program Options:
    --packetSize:      Size of constant packet (bytes) [512]
    --interval:        Interval to sent packets in seconds (e.g. (1s)) [20ms]
    --scenarioFolder:  Scenario folder (e.g. constellation-eutelsat-geo-2-sats-isls) [constellation-eutelsat-geo-2-sats-isls]
    --OutputPath:      Output path for storing the simulation statistics

General Arguments:
    --PrintGlobals:              Print the list of globals.
    --PrintGroups:               Print the list of groups.
    --PrintGroup=[group]:        Print all TypeIds of group.
    --PrintTypeIds:              Print all TypeIds.
    --PrintAttributes=[typeid]:  Print all attributes of typeid.
    --PrintVersion:              Print the ns-3 version.
    --PrintHelp:                 Print this help message.
```
```
export ROOT="$HOME/beam_hopping/sns3_leo_env"
export OUT="$ROOT/results/p1_leo_mobility"
mkdir -p "$OUT/logs" "$OUT/traces"

export LEO_SCN="constellation-leo-2-satellites
./ns3 run "sat-constellation-example \
  --scenarioFolder=$LEO_SCN \
  --OutputPath=$OUT" --no-build 2>/dev/null |& tee "$OUT/logs/run_task11_constellation.log" || \
./ns3 run "sat-constellation-example \
  --scenarioFolder=$LEO_SCN \
  --OutputPath=$OUT" |& tee "$OUT/logs/run_task11_constellation.log"

```
[OUTPUT](https://github.com/bmw-ntust-internship/Lucy/blob/7b15f819fe678ebb68a88737837cbc9cbf1fa3a6/Logs/Satellite%20topology)
- Satellites
  - Multiple satellites (SAT ID = 0, 1) are created.
  - Each satellite reports a (latitude, longitude, altitude) triple.\
  `SAT: ID = 0, at 4.39344,113.854,415387`: Altitudes (~413–415 km) correspond to typical LEO orbits.
- Inter-Satellite Links (ISLs)
  - ISL NetDevices are created between satellites, confirming a constellation rather than a single-satellite setup.
- Beams
  - Each satellite exposes multiple beam IDs.
  - Beams are instantiated as part of the SNS3 abstraction layer
- Gateways (GWs)
  - Each GW is associated with a satellite and beam.
- User Terminals (UTs)
  - UTs are placed on the ground.
  - SNS3 automatically determines:
    - closest satellite
    - best beam
    - associated gateway.
- SatIdMapper
  - A mapping between NetDevice MAC addresses and Satellite IDs
  - Mapping is essential for later trace-based analysis (e.g., beam hopping, traffic attribution).


## 2  Standardize position traces to a common CSV format
Objective: 
- Create a stable interface for downstream tasks (coverage/beam/routing).
- Standard output format `time`,`nodeId`,`lat`,`lon`,`alt`

### 2.1 Workflow
```
# 1. 設定輸出目錄（一次一個實驗）
export OUT="$HOME/beam_hopping/results/p1_leo_mobility"
mkdir -p "$OUT/logs" "$OUT/traces"

# 2. 執行 SNS3 constellation 範例
./ns3 run "sat-constellation-example \
  --scenarioFolder=constellation-telesat-351-sats \
  --OutputPath=$OUT" \
  | tee "$OUT/logs/run.log"
```
output

```
# 目的：從 SNS3 輸出中，取得 Satellite nodes 的時間序列 ECEF 座標
# SNS3 已輸出 positions_xyz.csv（格式：t,ctx,x,y,z）

awk -F, 'NR==1 || $2 ~ /^node=/' \
  "$OUT/positions_xyz.csv" \
  > "$OUT/traces/sat_positions_xyz.csv"
```
```
# 目的：產生 下游唯一可依賴的 mobility artifact。

python3 analysis/positions_to_csv.py \
  --input  "$OUT/traces/sat_positions_xyz.csv" \
  --output "$OUT/traces/leo_positions.csv"
```
output:results/p1_leo_mobility/traces/leo_positions.csv
- 固定格式 `time,nodeId,lat,lon,alt`

```
# 確認至少一顆衛星「真的有移動」。
# 計算每顆衛星的經度變化量
awk -F, '
NR==1{next}
{
  lon[$2]=$4+0
  if(!($2 in min)||lon[$2]<min[$2])min[$2]=lon[$2]
  if(!($2 in max)||lon[$2]>max[$2])max[$2]=lon[$2]
}
END{
  for(n in min)
    printf("node=%s lon_range_deg=%.6f\n", n, (max[n]-min[n]))
}
' "$OUT/traces/leo_positions.csv" | sort

```
- 至少一顆 Satellite 滿足：`lon_range_deg > 0`
