#!/usr/bin/env python3
import csv, math, json
from collections import defaultdict
from pathlib import Path

ROOT = Path.home() / "workspace/sns3_env/experiments/tri1_phasea_a2_connectivity_v2_elev_hyst"
META = ROOT / "meta.json"
OUT  = ROOT / "outputs/connectivity_timeseries.csv"

def load_meta():
    return json.loads(META.read_text())

def norm(v): return math.sqrt(v[0]**2 + v[1]**2 + v[2]**2)
def dot(a,b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
def sub(a,b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])

def elevation_deg(ground_xyz, sat_xyz):
    up = ground_xyz
    los = sub(sat_xyz, ground_xyz)
    up_n = norm(up); los_n = norm(los)
    if up_n == 0 or los_n == 0:
        return -90.0
    sin_el = dot(los, up) / (los_n * up_n)
    sin_el = max(-1.0, min(1.0, sin_el))
    return math.degrees(math.asin(sin_el))

def load_positions(path, role):
    d = defaultdict(dict)
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            if row["role"] != role:
                continue
            t = float(row["time_sec"])
            nid = int(row["nodeId"])
            d[t][nid] = (float(row["x_m"]), float(row["y_m"]), float(row["z_m"]))
    return d

def hysteresis(series, enter_slots, exit_slots):
    state = False
    consec_true = 0
    consec_false = 0
    out = {}
    for t, feas in series:
        if feas:
            consec_true += 1
            consec_false = 0
        else:
            consec_false += 1
            consec_true = 0

        if (not state) and consec_true >= enter_slots:
            state = True
        elif state and consec_false >= exit_slots:
            state = False

        out[t] = state
    return out

def read_isls_txt(isls_path):
    """
    Parse ISL adjacency from scenario isls.txt.
    We accept very permissive parsing:
      - ignore empty/comment lines
      - extract first two integers per line as sat ids
    """
    edges = set()
    if not isls_path.exists():
        return edges
    for line in isls_path.read_text().splitlines():
        s=line.strip()
        if not s or s.startswith("#"):
            continue
        # keep digits and separators
        parts = s.replace(","," ").replace(";"," ").split()
        ints=[]
        for p in parts:
            try:
                ints.append(int(p))
            except:
                pass
        if len(ints) >= 2:
            a,b = ints[0], ints[1]
            if a != b:
                edges.add((min(a,b), max(a,b)))
    return edges

def main():
    meta = load_meta()

    # --- inputs ---
    a1_rel = meta["inputs"]["a1_traces"]
    a1_dir = Path.home() / a1_rel
    scenario_dir = Path.home() / meta["inputs"]["scenario_dir"]
    isls_txt = scenario_dir / "positions" / "isls.txt"
    # some scenarios place isls outside positions/
    if not isls_txt.exists():
        isls_txt = scenario_dir / "isls.txt"

    # --- GSL settings ---
    eps = float(meta["GSL"]["epsilon_deg"])
    enter_slots = int(meta["GSL"]["stability"]["enter_slots"])
    exit_slots  = int(meta["GSL"]["stability"]["exit_slots"])

    sat = load_positions(a1_dir / "sat_positions_xyz_timeseries.csv", "SAT")
    gw  = load_positions(a1_dir / "gw_positions_xyz_timeseries.csv",  "GW")
    ut  = load_positions(a1_dir / "ut_positions_xyz_timeseries.csv",  "UT")

    times = sorted(set(sat.keys()) & set(gw.keys()) & set(ut.keys()))
    if not times:
        raise RuntimeError("No common time samples among SAT/GW/UT traces.")

    # --- build GSL feasibility for GW and UT ---
    raw = defaultdict(list)  # (role, gid, sid) -> [(t, feasible)]
    for t in times:
        sats = sat[t]
        gws  = gw[t]
        uts  = ut[t]

        for gid, gxyz in gws.items():
            for sid, sxyz in sats.items():
                el = elevation_deg(gxyz, sxyz)
                raw[("GW", gid, sid)].append((t, el > eps))

        for uid, uxyz in uts.items():
            for sid, sxyz in sats.items():
                el = elevation_deg(uxyz, sxyz)
                raw[("UT", uid, sid)].append((t, el > eps))

    # --- apply hysteresis ---
    active_edges_by_time = defaultdict(list)
    for (role, gid, sid), series in raw.items():
        series = sorted(series, key=lambda x: x[0])
        active = hysteresis(series, enter_slots, exit_slots)
        for t, is_on in active.items():
            if is_on:
                src = f"{role}{gid}"
                dst = f"SAT{sid}"
                active_edges_by_time[t].append((src, dst, "GSL"))

    # --- ISL inclusion ---
    isl_mode = meta.get("ISL", {}).get("mode", "none")
    isl_edges = set()
    if isl_mode == "always_up":
        isl_edges = read_isls_txt(isls_txt)

    # write outputs
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time_sec", "src", "dst", "edge_type"])
        for t in times:
            ti = int(round(t))
            # GSL edges at time t
            for (src, dst, typ) in active_edges_by_time.get(t, []):
                w.writerow([ti, src, dst, typ])
            # ISL edges (static, present at all time samples)
            if isl_mode == "always_up":
                for a,b in sorted(isl_edges):
                    w.writerow([ti, f"SAT{a}", f"SAT{b}", "ISL"])

    print(f"[OK] wrote {OUT}")
    print(f"[INFO] scenario_dir={scenario_dir}")
    print(f"[INFO] isls_txt={isls_txt} (exists={isls_txt.exists()})")
    print(f"[INFO] ISL.mode={isl_mode}, isl_edges={len(isl_edges)}")
    print(f"[INFO] GSL eps_deg={eps}, enter={enter_slots}, exit={exit_slots}")

if __name__ == "__main__":
    main()
