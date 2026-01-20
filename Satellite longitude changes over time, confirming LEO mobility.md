# Task 1 — Minimal Low Earth Orbit (LEO) Constellation Setup
Objective:建立一個「最小可重現」的 LEO 衛星位置模擬範例，用於驗證
- [X] 多顆 LEO 衛星是否會隨時間產生合理的緯經度漂移
- ns-3 事件排程是否能穩定輸出 time-series 軌跡資料（CSV）

## 1 Example `sat-leo-positions-minimal.cc`
- 建 N 顆衛星
- 用簡化圓軌道計算每顆衛星在時間 t 的 Earth-Centered, Earth-Fixed (ECEF)位置
- 轉成 （Latitude / Longitude / Altitude, LLA）
- 寫一行 CSV


## 2 Physical & Mathematical Models
### 2.1 Constants
```
// 球形地球假設
static const double kMu = 3.986004418e14;   // 地球引力常數 μ
static const double kRe = 6378137.0;        // 地球半徑
static const double kOmegaE = 7.2921159e-5; // 地球自轉角速度
```
### 2.2 Keplerian Motion
```
r = kRe + altM;
n = sqrt(kMu / (r*r*r)); //軌道角速度
theta = n * tSec + phaseRad; //隨時間線性增加=>衛星沿軌道移動
```
### 2.3 Satellite Distribution (同軌道均勻分布)
```
phase = 2 * M_PI * satId / nSats;
```
## 3 Coordinate Transformations
### 3.1 Orbital Plane → Earth-Centered Inertial(ECI)
- incDeg ↑ => 緯度擺幅 ↑
```
(x_orb, y_orb, 0) //在軌道平面算
//繞x-axis算
y_eci = y_orb * cos(incRad);
z_eci = y_orb * sin(incRad);
```
### 3.2 ECI → ECEF (Earth Rotation)
- 地球自轉造成 地面經度漂移
- 軌道固定，地面軌跡仍會向西滑移
- ECEF = Rz(+gst) * ECI
```
double gst = kOmegaE * tSec;
x =  x_eci * cos(gst) + y_eci * sin(gst);
y = -x_eci * sin(gst) + y_eci * cos(gst);
```
### 3.3 ECEF → LLA (Latitude/ Longitude / Altitude)
- 驗證緯度振盪、經度隨時間漂移
```
const double r = sqrt(x*x + y*y + z*z);
lat = asin(z / r);
lon = atan2(y, x);
alt = r - kRe;
```
## 4 ns-3 Simulation Scheduling
### 4.1 Time Control
```
Simulator::Stop(Seconds(simLength));
```
### 4.2 Periodic Logging Mechanism
```
Simulator::Schedule(Seconds(0.0), &LogPositions, ...);
Simulator::Run();
```
`LogPositions()`:取得目前 tSec、計算每顆衛星的位置、輸出一行 CSV、排程下一次(tNext = tSec + stepSec;)直到 `simLength`為止
### 4.3 Cleanup
```
Simulator::Destroy();
```
## 5 Minimal Reproducible Parameter Set
| Parameter | Type      | Value             |
| --------- | ----------| ----------------  |
| nSats     | uint32_t  | 6                 |
| altM      | double    | 600000 m          |
| incDeg    | double    |53                 |
| simLength | double    |300 s              |
| stepSec   | double    | 10 s              |
| outCsv    | string    |`leo_positions.csv`|

## 6 Implementation

### 6.1 Setup Path
```
export ROOT="$HOME/beam_hopping/leo_minimal"
mkdir -p "$ROOT/logs" "$ROOT/traces"
```
### 6.2 新增檔案
新增檔案至
`~/workspace/bake/source/ns-3.43/contrib/satellite/examples/sat-leo-positions-minimal.cc`
- [sat-leo-positions-minimal.cc](https://github.com/bmw-ntust-internship/Lucy/blob/e01d0a8cb331c9f2d175162fd551e22829142b4e/codes/sat-beam-hopping-test-example)

### 6.3 example 加到 satellite examples（wscript）
`~/workspace/bake/source/ns-3.43/contrib/satellite/examples/wscript`
```
obj = bld.create_ns3_program('sat-leo-positions-minimal', ['satellite'])
obj.source = 'sat-leo-positions-minimal.cc'
```
### 6.4 把 example 加進 CMake
`~/workspace/bake/source/ns-3.43/contrib/satellite/examples/CMakeLists.txt`

在 `set(base_examples` 加入 `sat-leo-positions-minimal` 

- 不要加 .cc 縮排一致

### 6.5 configure + build
```
cd ~/workspace/bake/source/ns-3.43
./ns3 configure --enable-examples
./ns3 build
./ns3 run "sat-leo-positions-minimal --help"
```

output
```
Program Options:
--nSats: Number of LEO satellites [6]
--altM: Orbit altitude [m] [600000]
--incDeg: Inclination [deg] [53]
--simLength: Simulation length [s] [300]
--stepSec: Log step [s] [10]
--outCsv: Output CSV file [leo_positions.csv]

General Arguments:
--PrintGlobals: Print the list of globals.
--PrintGroups: Print the list of groups.
--PrintGroup=[group]: Print all TypeIds of group.
--PrintTypeIds: Print all TypeIds.
--PrintAttributes=[typeid]: Print all attributes of typeid.
--PrintVersion: Print the ns-3 version.
--PrintHelp: Print this help message.
```

### 6.6 Logs
```
export ROOT="$HOME/beam_hopping/leo_minimal"
mkdir -p "$ROOT/logs" "$ROOT/traces"

cd ~/workspace/bake/source/ns-3.43

./ns3 run "sat-leo-positions-minimal \
  --nSats=6 \
  --altM=600000 \
  --incDeg=53 \
  --simLength=300 \
  --stepSec=10 \
  --outCsv=$ROOT/traces/leo_positions.csv" \
| tee "$ROOT/logs/run_leo_positions.log"
```
### 6.7 Output Verification (CSV sanity check)

##### bash
```
CSV="$ROOT/traces/leo_positions.csv"
# 1) 檔案是否存在、前幾行是否有資料
ls -la "$CSV"
```
##### output
```
-rw-rw-r-- 1 wenj wenj 123456 Jan 20 14:32 /home/wenj/beam_hopping/leo_minimal/traces/leo_positions.csv
```
##### bash
```
head -n 5 "$CSV"
```
##### output
```
time,satId,lat,lon,alt
0.000000,0,0.000000,0.000000,600000.000000
0.000000,1,0.000000,60.000000,600000.000000
0.000000,2,0.000000,120.000000,600000.000000
0.000000,3,0.000000,180.000000,600000.000000
```
##### bash
```
# 2) 計算筆數是否符合：rows = (simLength/stepSec + 1) * nSats (+1 若含 header)
# 以 simLength=300, stepSec=10 => time points = 31
# 若每個 time point 輸出 nSats 行 => 31*6 = 186
wc -l "$CSV"
```
##### output
```
187 /home/wenj/beam_hopping/leo_minimal/traces/leo_positions.csv
//simLength=300, stepSec=10 應該有31個時間點
//每個時間點有nSats行 再加一行header 
```
##### bash
```
# 3) 抽一顆衛星看經度漂移範圍（示例：satId=0）
awk -F, 'NR>1 && $2==0 {lon=$4; if(n==0||lon<min)min=lon; if(n==0||lon>max)max=lon; n++}
END{print "satId=0 lon_range_deg=", (max-min), "samples=", n}' "$CSV"
```
##### output
```
satId=0 lon_range_deg= 359.668 samples= 32
//確認時間點數、lon_range
```
