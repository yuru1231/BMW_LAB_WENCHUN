from beam_id_provider import ProxyBeamConfig, ProxyHopConfig, proxy_beam_and_hop

def main():
    beam_cfg = ProxyBeamConfig(K_sectors=12, R_rings=3, dmax_m=900_000, sector_offset_deg=0.0)
    hop_cfg  = ProxyHopConfig(T_hop_s=10.0, mode="sector")  # "single" / "sector" / "ring"

    t = 20.0
    ut_lat, ut_lon = 25.0, 121.5
    sat_id = 3
    sub_lat, sub_lon = 24.0, 120.0

    row = proxy_beam_and_hop(t, ut_lat, ut_lon, sat_id, sub_lat, sub_lon, beam_cfg, hop_cfg)

    print("=== demo_run ===")
    print(f"t={row['t']} sat_id={row['sat_id']}")
    print(f"beam_local_id={row['beam_local_id']}  beam_id={row['beam_id']}")
    print(f"beam_active={row['beam_active']}  hop_slot={row['hop_slot']}  active_local_id={row['active_local_id']}")
    print(f"meta: d_m={row['meta']['d_m']:.1f}, az_deg={row['meta']['az_deg']:.2f}, ring={row['meta']['ring']}, sector={row['meta']['sector']}")

if __name__ == "__main__":
    main()
