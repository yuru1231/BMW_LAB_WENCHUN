import math
from beam_id_provider import ProxyBeamConfig, ProxyHopConfig, proxy_beam_and_hop

def check_hop_slot_boundary():
    hop_cfg  = ProxyHopConfig(T_hop_s=10.0, mode="sector")
    T = hop_cfg.T_hop_s

    assert math.floor(9.9 / T) == 0
    assert math.floor(10.0 / T) == 1
    assert math.floor(19.999 / T) == 1
    assert math.floor(20.0 / T) == 2

def check_deterministic():
    beam_cfg = ProxyBeamConfig(K_sectors=12, R_rings=3, dmax_m=900_000)
    # 你可以改 mode 看行為差異
    hop_cfg  = ProxyHopConfig(T_hop_s=10.0, mode="sector")

    t = 12.0
    ut_lat, ut_lon = 25.0, 121.5
    sat_id = 3
    sub_lat, sub_lon = 24.0, 120.0

    r1 = proxy_beam_and_hop(t, ut_lat, ut_lon, sat_id, sub_lat, sub_lon, beam_cfg, hop_cfg)
    r2 = proxy_beam_and_hop(t, ut_lat, ut_lon, sat_id, sub_lat, sub_lon, beam_cfg, hop_cfg)

    assert r1["beam_id"] == r2["beam_id"]
    assert r1["beam_active"] == r2["beam_active"]
    assert r1["beam_local_id"] == r2["beam_local_id"]
    assert r1["hop_slot"] == r2["hop_slot"]

def check_active_ratio(mode: str, expected_ratio: float, tol: float = 0.03):
    beam_cfg = ProxyBeamConfig(K_sectors=12, R_rings=3, dmax_m=900_000)
    hop_cfg  = ProxyHopConfig(T_hop_s=10.0, mode=mode)

    ut_lat, ut_lon = 25.0, 121.5
    sat_id = 3
    sub_lat, sub_lon = 24.0, 120.0

    # 用多個時間點估計 active 比例（越多越穩）
    t0, t1, dt = 0.0, 600.0, 1.0  # 0~600s, step=1s
    active_cnt = 0
    total = 0

    t = t0
    while t <= t1:
        row = proxy_beam_and_hop(t, ut_lat, ut_lon, sat_id, sub_lat, sub_lon, beam_cfg, hop_cfg)
        active_cnt += int(row["beam_active"])
        total += 1
        t += dt

    ratio = active_cnt / total
    assert abs(ratio - expected_ratio) <= tol, f"mode={mode} ratio={ratio:.4f} expected={expected_ratio:.4f} tol={tol}"

def main():
    print("=== sanity_checks ===")

    # 4.1 hop_slot 邊界
    check_hop_slot_boundary()
    print("[OK] hop_slot boundary checks")

    # 4.2 deterministic
    check_deterministic()
    print("[OK] deterministic checks")

    # 4.3 active ratio（理論值）
    beam_cfg = ProxyBeamConfig(K_sectors=12, R_rings=3, dmax_m=900_000)
    K, R = beam_cfg.K_sectors, beam_cfg.R_rings

    # single: 1/(K*R), sector: 1/K, ring: 1/R
    check_active_ratio("single", expected_ratio=1/(K*R), tol=0.02)
    print("[OK] active ratio check (single)")

    check_active_ratio("sector", expected_ratio=1/K, tol=0.02)
    print("[OK] active ratio check (sector)")

    check_active_ratio("ring", expected_ratio=1/R, tol=0.02)
    print("[OK] active ratio check (ring)")

    print("ALL PASSED ✅")

if __name__ == "__main__":
    main()
