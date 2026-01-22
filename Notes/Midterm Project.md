Note of Midterm Project

Environment Install

OS:Ubuntu 22.04 with 8vCPU 與 16 GB RAM

OAI:

5GCN:https://github.com/Kuan-K/2025_kuan_project/blob/main/OAI/CN5G%20built%2  0environment_.md

OAI RAN (gNB/UE):https://github.com/Kuan-K/2025_kuan_project/blob/main/OAI/E2E%20hands-on . md#installation-of-dependencies-and-compilation

SNS3:

ns-3.43:https://github.com/sns3/sns3-satellite

bake 安裝 ns-allinone-3.43:https://github.com/kevin940822-beep/OpenAirInterface-NTN-Integration/blob/mai  n/sns3/SNS3_installation.md

Xeoverse: https://github.com/raycg/xeoverse

Architecture

1.OAI:

建立 端對端（End-to-End, E2E） 的 NTN 測試環境，將系統分為 Backstage（協定處理與控制）與 Main Stage（通道模擬）兩個部分

Backstage:負責 5G NR 協定處理與控制訊號

OAI UE

OAI gNB

5GCN

Main Stage:RF-Simulator所實作的 LEO NTN 通道模型，用來即時產生延遲與 Doppler

架構分為 4 部分(實際端到端系統的組成模組，描述資料流向)

UE side（VM + OAI nr-uesoftmodem）

產生流量（ping/iperf3 client）

跑 UE 端 NR 協定堆疊，輸出 I/Q（--rfsim mode）

RF-Simulator（channel simulator）

夾在 UE 與 gNB 中間

套用 SAT_LEO_TRANS / SAT_LEO_REGEN 等模型，產生 delay/Doppler/path loss

OAI gNB (nr-uesoftmodem）

跑 gNB 協定堆疊

用 NTN 參數（Koffset、TA、HARQ/RLC timers）做補償

與 5GC 用 NG-C/NG-U 連線

5G Core（OAI 5GCN + test server）

AMF/SMF/UPF 做註冊與 PDU session

UPF 轉送 GTP-U 到測試伺服器，讓你量測 RTT/吞吐/丟包

2.SNS3:

3.Xeoverse:
