## 1. 目的
使用 SNS3 satellite module 的 `sat-beam-hopping-test-example` 範例，完成 Beam Hopping 行為驗證：
- 在模擬時間內，觀察波束（Beam）於指定時間點切換 ON/OFF
- 產出可重現的執行 log 與輸出資料夾作為交付證據

## 2. 環境與前置條件
- ns-3 + SNS3-satellite 已可成功編譯
- 可使用 `./ns3 run` 執行範例程式

> 本文以 `--trafficModel=1`、`--simLength=30` 進行 30 秒驗證。

## 3. 資料夾準備（建議）
建立專用資料夾存放輸出與 log，確保交付清楚可追溯。

```bash
mkdir -p ~/beam_hopping/results_verify
mkdir -p ~/beam_hopping/logs
```
## 4. Command
```
./ns3 run "sat-beam-hopping-test-example --trafficModel=1 --simLength=30 --OutputPath=$HOME/beam_hopping/results_verify" \
  | tee ~/beam_hopping/logs/run_bh_verify.log
```
###4.1 參數說明

sat-beam-hopping-test-example：Beam Hopping 測試範例
- trafficModel=1：Traffic model（此案例使用 1）
- simLength=30：模擬長度 30 秒
- OutputPath=...：統計/輸出檔案存放路徑
- tee ...run_bh_verify.log：同時顯示在 terminal 並寫入 log

## 5. Output
可以在`run_bh_verify.log`
```
Consolidate compiler generated dependencies of target sat-beam-hopping-test-example
Trace not found: RbdcTrace (skipped)
Trace not found: VbdcTrace (skipped)
Trace not found: AvbdcTrace (skipped)
Trace not found: DaResourcesTrace (skipped)
--- sat-rtn-sys-test ---
  Test case: 0
  Traffic model: 1
  Simulation length: 30
  Number of UTs: 10
  Number of end users per UT: 10
  
[BeamHopping] t=10s: Beam 26 -> OFF, Beam 27 -> ON
[BeamHopping] t=20s: Beam 27 -> OFF, Beam 26 -> ON

```
