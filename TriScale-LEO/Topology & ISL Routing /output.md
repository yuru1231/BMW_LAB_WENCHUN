`routing_plan.csv`
```
time_start  time_end  serving_sat  path                                                             hop_count  status  gw_index  reason
0.000       30.000    44           UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0  7          OK      0         best_elevation_then_min_hops
30.000      60.000    44           UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0  7          OK      0         best_elevation_then_min_hops
60.000      90.000    44           UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0  7          OK      0         best_elevation_then_min_hops
90.000      120.000   44           UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0  7          OK      0         best_elevation_then_min_hops
120.000     150.000   44           UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0  7          OK      0         best_elevation_then_min_hops
150.000     180.000   44           UT->SAT44->SAT43->SAT42->SAT41->SAT40->SAT39->SAT51->SAT64->GW0  7          OK      0         best_elevation_then_min_hops
```
| 欄位          | 意義                         |
| ----------- | -------------------------- |
| time_start  | routing window 起始時間        |
| time_end    | routing window 結束時間        |
| serving_sat | 被選為 serving satellite 的 ID |
| path        | UT → SAT → ... → GW 的完整路徑  |
| hop_count   | path 中 SAT-SAT hop 數       |
| status      | routing 是否成功               |
| gw_index    | 使用哪個 gateway               |
| reason      | 為什麼選這顆 satellite           |

`best_elevation_then_min_hops `:elevation 最大；相同 → hop 最少

---

`candidate_sats.csv`
```
time     satId  elev_ref_deg  isl_to_gw  path_hops  candidate
0.000    0      -64.508       1          5          0
0.000    1      -52.804       1          6          0
0.000    2      -39.701       1          7          0
0.000    3      -25.856       1          8          0
0.000    4      -11.745       1          9          0
0.000    5      -1.738        1          10         0
0.000    6      -7.497        1          10         0
0.000    7      -21.163       1          9          0
0.000    8      -35.186       1          8          0
```
| 欄位           | 意義                                            |
| ------------ | --------------------------------------------- |
| time         | 模擬時間 (秒)                                      |
| satId        | 衛星 ID                                         |
| elev_ref_deg | 該衛星相對 reference location（台北）的 elevation angle |
| isl_to_gw    | 是否存在 ISL 路徑到 gateway anchor satellite         |
| path_hops    | 從該 satellite 到 gateway 的 hop 數                |
| candidate    | 是否符合 serving satellite 候選條件                   |

candidate 判定條件:
```
candidate =
(elev_ref_deg >= elevDeg)
AND
(isl_to_gw == 1)
```
---

`isl_connectivity.csv`
```
time     satA  satB  distance_km  active
0.000    0     1     3522.941     1
0.000    0     2     6851.804     0
0.000    0     3     9793.171     0
0.000    0     4     12167.993    0
0.000    0     5     13830.663    0
0.000    0     6     14683.557    0
0.000    0     7     14680.074    0
0.000    0     8     13821.563    0
0.000    0     9     12156.708    0
```
| 欄位          | 意義            |
| ----------- | ------------- |
| time        | 模擬時間          |
| satA        | 衛星 A          |
| satB        | 衛星 B          |
| distance_km | 兩衛星之間距離       |
| active      | 是否存在 ISL link |

`active = HasEdge(adj, satA, satB) `: ISL adjacency graph 裡是否有這條 edge

---
`summary.json`
```
"event_name": "E20260305_ISL_BH",
  "mode": "d1_final",
  "scenario_folder": "constellation-telesat-351-sats",
  "simTime": 180,
  "tStart": 0,
  "tEnd": 180,
  "dt": 1,
  "planWindow": 30,
  "refLat": 25.033,
  "refLon": 121.565,
  "elevDeg": 20,
  "gwIndex": 0,
  "gwInfo": "GW0(index=0)",
  "statsLevel": "min",
  "generated_files": [
    "route_dump.txt",
    "forward_path.log",
    "candidate_sats.csv",
    "isl_connectivity.csv",
    "routing_plan.csv"
  ]
```


`run.out`
```
[  0%] Building CXX object scratch/CMakeFiles/scratch_isl-leo-candidate.dir/isl-leo-candidate.cc.o
[  0%] Linking CXX executable ../../build/scratch/ns3.43-isl-leo-candidate-optimized
[D1] orbiter nodes = 351
[D1] gw nodes = 2
[D1] ISL adjacency sats = 351
[D1] GW anchor sat count = 1
[D1] GW anchor sats: 64
[D1] GetNOrbiterNodes() = 351
[D1] SAT0 geo = lat=-0.113272 lon=-99.9514 alt=1.01681e+06
[D1] =====================================
[D1] isl-leo-candidate start
[D1] mode=d1_final
[D1] scenarioFolder=constellation-telesat-351-sats
[D1] outDir=/home/wenj/workspace/ns-3.43/events/E20260305_ISL_BH/outputs
[D1] simTime=180
[D1] tStart=0 tEnd=180
[D1] ref=(25.033,121.565)
[D1] elevDeg=20
[D1] =====================================
[D1] mode=d1_final scenario=constellation-telesat-351-sats
[D1] ref=(25.033, 121.565) elev=20
[D1] t=[0, 180] dt=1 planWindow=30
[D1] real orbiter count = 351
[D1] real gw count = 2
[D1] real orbiter count = 351
[D1] real gw count = 2
[D1] real gw count = 2
[D1] done. outDir=/home/wenj/workspace/ns-3.43/events/E20260305_ISL_BH/outputs

```

