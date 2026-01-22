## Task2 
### 0 Condition check
Object:Verifies whether the geometric and temporal conditions allow UT coverage and handover to exist at all.

If coverage never occurs, then:\
- servedSatId will always be -1
- handover_events.csv will be empty
- All later logic is invalid by constructio

### 1 Original Failure Case
#### 1.1 Expected Behavior
- servedSatId should not be always -1
- There should exist at least one time window with coverage
- At least one handover event should occur

#### 1.2 Actual Output
- `unique servedSatId (excluding -1) = 0`
- `servedSatId = -1` for all time steps
- `coverage ratio = 1.0 `(no coverage)
- `handover_events.csv` empty

#### 1.3 Diagnosis
elevation 永遠小於門檻 ELEV_MIN_DEG = 10\
Conclusion:\
- servedSatId 永遠是 -1
- coverage ratio = 0
- handover 不會發生


### 2 Fixes Applied
#### 2.1 Changed
|Item	|Before	|After|
|---|---|---|
|Number of satellites|	66	|24|
|Simulation length	|short|	3600 s|
|Time step	|implicit|	10 s|
|Output structure	|flat	|parameterized input folder|
|Output file naming	|ambiguous|	leo_positions_N24_T3600_S10.csv|

#### 2.2 Why These Changes Were Made
- Longer simulation window: to allow full LEO passes over UT
- Reduced satellite count: to avoid low-angle clustering
- Explicit time sampling: for stable time-series precheck
-  Parameterized filenames: to ensure traceability across experiments

### 3 New Results (After Fix)
#### 3.1 Execution
```
./ns3 run "sat-leo-positions-minimal \
--nSats=24 \
--altM=600000 \
--incDeg=53 \
--simLength=3600 \
--stepSec=10 \
--outCsv=$ROOT/d3d4_precheck/input/leo_positions_N24_T3600_S10.csv"

python3 ut_serving_precheck_lla_full.py "$POS"
```
#### 3.2 Quantitative Results
|Metric|	Before	|After|
|--|--|--|
|Time points	|undefined|	360|
|Coverage points|	0	|54|
|Coverage ratio	|0.0|	0.15|
|servedSatId ≠ -1	|❌	|✅|
|Handover events	|0	|7|
|Output CSVs generated|	❌|	✅|

#### 3.3 Evidence
```
coverage_points=54 total=360 ratio=0.15
overlap_points=0 total=360 ratio=0
handovers=7
```
Sample handover events:
```
time,utId,oldSatId,newSatId,reason
2810.0,0,-1,21,max_elevation_changed
2870.0,0,21,-1,max_elevation_changed
3020.0,0,-1,20,max_elevation_changed
```
### 4 Checklist
- [X] UT can be served
- [X] Coverage exists
- [X] Handover events exist
