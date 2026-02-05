### Environment
```
cd ~/workspace/bake/source/ns-3.43
export PHASE2="$HOME/beam_hopping/results/p2_phase2_$(date +%Y%m%d_%H%M%S)"
export TR="$PHASE2/traces"
export LG="$PHASE2/logs"
mkdir -p "$TR" "$LG"
```
## Task 2.1 — UT/GW presence (no comms)
### 2.1-A Snapshot
```
export PHASE2="$HOME/beam_hopping/results/p2_phase2_$(date +%Y%m%d_%H%M%S)"
export TR="$PHASE2/traces"
export LG="$PHASE2/logs"
mkdir -p "$TR" "$LG"
rm -f "$TR"/*.csv

./ns3 run "sat-constellation-example \
  --scenarioFolder=constellation-telesat-351-sats \
  --simTime=3 \
  --OutputPath=$TR \
  --enableUtGwPosTrace=1 \
  --posTraceMode=0 \
  --posTraceExitAfterSnapshot=1" \
|& tee "$LG/run_2p1_snapshot.log"

```
#### 2.1 DoD 
```
ls -lh "$TR"/*snapshot.csv
head -n 5 "$TR/ut_positions_xyz_snapshot.csv"
head -n 5 "$TR/gw_positions_xyz_snapshot.csv"
```
#### output
```
-rw-rw-r-- 1 wenj wenj 171  一  29 18:22 /home/wenj/beam_hopping/results/p2_phase2_20260129_182255/traces/gw_positions_xyz_snapshot.csv
-rw-rw-r-- 1 wenj wenj 24K  一  29 18:22 /home/wenj/beam_hopping/results/p2_phase2_20260129_182255/traces/sat_positions_xyz_snapshot.csv
-rw-rw-r-- 1 wenj wenj 237  一  29 18:22 /home/wenj/beam_hopping/results/p2_phase2_20260129_182255/traces/ut_positions_xyz_snapshot.csv
time_sec,role,nodeId,x_m,y_m,z_m
0.00000000e+00,UT,354,1.76329400e+06,-5.02064122e+06,-3.51633155e+06
0.00000000e+00,UT,357,-2.76703175e+06,4.76160857e+06,3.21733597e+06
0.00000000e+00,UT,360,3.80694782e+06,3.72815329e+06,3.50551750e+06
time_sec,role,nodeId,x_m,y_m,z_m
0.00000000e+00,GW,351,4.28240203e+06,-4.02249821e+06,-2.48217094e+06
0.00000000e+00,GW,353,4.97793027e+06,-3.96580950e+06,-4.16169267e+05
-rw-rw-r-- 1 wenj wenj 24K  一  29 18:22 /home/wenj/beam_hopping/results/p2_phase2_20260129_182255/traces/sat_positions_xyz_snapshot.csv

```
- snapshot CSV exist and > 0B
- time_sec,role,nodeId,x_m,y_m,z_m header correctly
- UT/GW at least 1 record

> 「UT/GW nodes exist and traceable」established (traceable = location can be captured by a tracer; comms/PHY/MAC are not verified at this stage)


## Task 2.2 — Serving precheck（t=0） by Snapshot
### 2.2-A Convert LLA（UT/GW/SAT）
```
python3 analysis/positions_to_csv.py \
  --in  "$TR/ut_positions_xyz_snapshot.csv" \
  --out "$TR/ut_positions_lla.csv" \
  --role UT

python3 analysis/positions_to_csv.py \
  --in  "$TR/gw_positions_xyz_snapshot.csv" \
  --out "$TR/gw_positions_lla.csv" \
  --role GW

python3 analysis/positions_to_csv.py \
  --in  "$TR/sat_positions_xyz_snapshot.csv" \
  --out "$TR/sat_positions_lla.csv" \
  --role SAT

```
```
python3 analysis/ut_serving_precheck.py \
  --ut  "$TR/ut_positions_lla.csv" \
  --sat "$TR/sat_positions_lla.csv" \
  --out "$TR/serving_timeline.csv"

```
```
[OK] wrote /home/wenj/beam_hopping/results/p2_phase2_20260129_182255/traces/serving_timeline.csv, rows=3
```
```
ls -lh "$TR/serving_timeline.csv"
head -n 20 "$TR/serving_timeline.csv"
```
```
-rw-rw-r-- 1 wenj wenj 114  一  29 18:29 /home/wenj/beam_hopping/results/p2_phase2_20260129_182255/traces/serving_timeline.csv
time,utId,satId,beamId,reason,elevation_deg
0.0,354,38,-1,ok,50.04
0.0,357,44,-1,ok,56.57
0.0,360,317,-1,ok,42.51

```
- UT 354 served by SAT 128 (elevation ≥ threshold)
- UT 357 unserved (below minimum elevation)
- UT 360 served by SAT 245
- Serving decision is fully explainable by geometry

> UT → best SAT elevation precheck；beamId=-1 means without beam selection 

## Task 2.3 —Beam ID precheck（geometry-only, no SatNetDevice, no traffic）

### 2.3-A Proxy Beam（sector + ring）+ Proxy Hopping
Fixed schema:
- `t`（sec, float）
- `ut_id`（int）
- `sat_id`（int）→ servingSat / bestSat
- `elev_deg`（float, optional；(Not null)
- `beam_local_id`（int）→ beam index in a satellite（0..K*R-1）
- `beam_id`（int）→ global beam id（ sat_id*KR + local）
- `beam_active`（0/1）→ proxy hopping (light up or not)
- `hop_slot`（int）→ floor(t / T_hop)
- `active_local_id`（int）→ The hop_slot lighted up local beam id

route: workspace/bake/source/ns-3.43/analysis\
[beam_id_provider.py](https://github.com/bmw-ntust-internship/Lucy/blob/4fa8b31ef95a7579d38ca1c3afa784e821e640d2/codes/beam_id_provider.py)\
[demo_run.py](https://github.com/bmw-ntust-internship/Lucy/tree/41af20c38d55e4327edd8169f49082406321eb81/codes)\
[sanity_checks.py](https://github.com/bmw-ntust-internship/Lucy/blob/43a16477d8e6cde51732f6442e4bbe70fd4f45ee/codes/sanity_checks.py)

command:
```
python3 demo_run.py
```
output:
```
=== demo_run ===
t=20.0 sat_id=3
beam_local_id=1  beam_id=109
beam_active=0  hop_slot=2  active_local_id=2
meta: d_m=188356.1, az_deg=53.46, ring=0, sector=1
```
- hop_slot=2：Because T_hop=10s, t=20s → slot=floor(20/10)=2 
- beam_local_id=1: UT at serving Set footprint, assigned to ring0, sector=1 (sector partitioning)
- active_local_id=2：This slot is illuminated as local beam 2 (determined by hopping mode).
- beam_active=0：UT  beam_local_id=1 ≠ active_local_id=2 (or the sector does not match) → the UT is "not lit".
```
python3 sanity_checks.py
```
```
=== sanity_checks ===
[OK] hop_slot boundary checks
[OK] deterministic checks
[OK] active ratio check (single)
[OK] active ratio check (sector)
[OK] active ratio check (ring)
ALL PASSED ✅
```
