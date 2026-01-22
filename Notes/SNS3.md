
## 0. Environment Setup

### Virtual Machine
- **Platform:** VMware
- **OS:** Ubuntu 22.04 LTS
- **Disk:** 80 GB
- **Memory:** 8 GB RAM (recommended)

### Install step
https://github.com/kevin940822-beep/OpenAirInterface-NTN-Integration/blob/main/sns3/SNS3_installation.md
Simulate note by Lucy: 




## 1、Definition

- **SNS3**：Satellite Network Simulation Platform
- **Hypatia**：LEO Satellite System Model in SNS3
- **DVB-RCS2**：Real-world "GEO Satellite Communication Standard"

---

## 2、What is SNS3 

SNS3 is a satellite extension that runs on ns-3.

To figure out：
- ns-3 natively does not calculate satellite orbits
- It cannot handle extremely long **propagation delays**
- It cannot handle topology changes caused by high-speed satellite movement

SNS3 not a system, it's a platform to simulate.

---

## 3、Hypatia Model

Hypatia is the most mature model in SNS3, the most frequently cited model, and almost equivalent to the "standard model used in LEO-NTN research."

### What is Hypatia simulating？
- LEO satellite constellation (multiple orbits, multiple satellites)
- High-speed satellite movement
- Satellite ↔ Satellite ISL
- Gateway visibility changes
- Routing table is recalculated over time

### Essentially, it studies：
- delay / RTT is not a fixed value
- path can switch
- The impact of **routing** on performance

👉 Hypatia is concerned with behavior above the network layer, not PHY details.

---

## 4、DVB-RCS2

DVB-RCS2 is：
- A real-world communication standard
- Primarily used in GEO
- Commercial, stable, centralized

### Typical assumptions
- Satellites are almost stationary (GEO)
- One hub controls all terminals
- Almost no ISL
- High RTT, but stable topology
  
👉 It's just a "comparison architecture/reference system"

---

## 5、Hierarchy

【Real】
DVB-RCS2
→ Satellite communication standards (GEO, Hub-Spoke)

【Simulattion】
SNS3
└─ Hypatia
→ LEO、ISL、dynamic routing

## 6、Hypatia vs DVB-RCS2
| | Hypatia | DVB-RCS2 |
|---|---|---|
| Typr | Simulation Model | Communication standards |
|satellite | LEO | GEO |
| topo | Mesh + ISL | Hub-Spoke |
| Routing | dynamic | Central control |
| Delay | Time-varying | Almost fixed |
| Instructions | studies | business |



