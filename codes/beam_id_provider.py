# tools/beam_proxy/beam_id_provider.py
from __future__ import annotations
from dataclasses import dataclass
import math
from typing import Optional, Tuple, Dict, Any, Iterable

# ----------------------------
# Geo helpers
# ----------------------------
EARTH_RADIUS_M = 6378137.0  # WGS84 equatorial-ish; good enough for proxy

def wrap_lon_deg(lon: float) -> float:
    """Wrap longitude to [-180, 180)."""
    x = (lon + 180.0) % 360.0 - 180.0
    return x

def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Great-circle distance in meters."""
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = phi2 - phi1
    dlambda = math.radians(wrap_lon_deg(lon2 - lon1))
    a = math.sin(dphi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    c = 2.0 * math.asin(min(1.0, math.sqrt(a)))
    return EARTH_RADIUS_M * c

def initial_bearing_deg(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """
    Bearing from point1 -> point2 in degrees [0, 360).
    Using spherical approximation.
    """
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dlambda = math.radians(wrap_lon_deg(lon2 - lon1))
    y = math.sin(dlambda) * math.cos(phi2)
    x = math.cos(phi1) * math.sin(phi2) - math.sin(phi1) * math.cos(phi2) * math.cos(dlambda)
    theta = math.atan2(y, x)
    brng = (math.degrees(theta) + 360.0) % 360.0
    return brng

# ----------------------------
# Proxy beam config
# ----------------------------
@dataclass(frozen=True)
class ProxyBeamConfig:
    K_sectors: int = 12            # azimuth sectors
    R_rings: int = 3               # radial rings
    dmax_m: float = 900_000.0      # max footprint radius for ring quantization
    # Optional offset to rotate sector boundaries, so beam IDs are not aligned to North.
    sector_offset_deg: float = 0.0

    def kr(self) -> int:
        return self.K_sectors * self.R_rings

@dataclass(frozen=True)
class ProxyHopConfig:
    T_hop_s: float = 10.0
    mode: str = "single"  # "single" or "sector" or "ring"
    # "single": only one (ring, sector) active per slot
    # "sector": one sector active, all rings active
    # "ring": one ring active, all sectors active

# ----------------------------
# Core mapping: UT + sub-sat -> local beam ID
# ----------------------------
def proxy_local_beam_id(
    ut_lat: float, ut_lon: float,
    sub_lat: float, sub_lon: float,
    cfg: ProxyBeamConfig,
) -> Tuple[int, Dict[str, Any]]:
    """
    Compute local beam id in [0, K*R-1] based on:
      - azimuth sector from sub-satellite point
      - radial ring based on distance
    """
    # Distance from sub-sat point to UT on Earth surface
    d = haversine_m(sub_lat, sub_lon, ut_lat, ut_lon)

    # Azimuth bearing from sub-sat point to UT
    az = initial_bearing_deg(sub_lat, sub_lon, ut_lat, ut_lon)
    az = (az + cfg.sector_offset_deg) % 360.0

    # Sector index
    K = cfg.K_sectors
    sector_width = 360.0 / K
    sector = int(math.floor(az / sector_width))
    sector = max(0, min(K - 1, sector))

    # Ring index
    R = cfg.R_rings
    d_clamped = min(max(d, 0.0), cfg.dmax_m)
    ring_width = cfg.dmax_m / R
    ring = int(math.floor(d_clamped / ring_width)) if ring_width > 0 else 0
    ring = max(0, min(R - 1, ring))

    local_id = ring * K + sector

    meta = {
        "d_m": d,
        "az_deg": az,
        "ring": ring,
        "sector": sector,
        "ring_width_m": ring_width,
        "sector_width_deg": sector_width,
    }
    return local_id, meta

# ----------------------------
# Proxy hopping schedule: decide what's active at time t
# ----------------------------
def proxy_active_local_id(
    t_s: float,
    cfg_beam: ProxyBeamConfig,
    cfg_hop: ProxyHopConfig,
) -> Tuple[int, Dict[str, Any]]:
    """
    Returns (active_local_id, meta)
    - For mode="single": active_local_id cycles over [0..KR-1]
    - For mode="sector": active_sector cycles over [0..K-1], active_local_id = active_sector (ring=0 placeholder)
    - For mode="ring": active_ring cycles over [0..R-1], active_local_id = active_ring*K (sector=0 placeholder)
    """
    if cfg_hop.T_hop_s <= 0:
        raise ValueError("T_hop_s must be > 0")

    hop_slot = int(math.floor(t_s / cfg_hop.T_hop_s))
    K, R = cfg_beam.K_sectors, cfg_beam.R_rings
    KR = cfg_beam.kr()

    mode = cfg_hop.mode.lower().strip()
    if mode == "single":
        active = hop_slot % KR
        meta = {"hop_slot": hop_slot, "mode": mode, "active_local_id": active}
        return active, meta
    elif mode == "sector":
        active_sector = hop_slot % K
        active = active_sector  # ring=0 placeholder
        meta = {"hop_slot": hop_slot, "mode": mode, "active_sector": active_sector, "active_local_id": active}
        return active, meta
    elif mode == "ring":
        active_ring = hop_slot % R
        active = active_ring * K  # sector=0 placeholder
        meta = {"hop_slot": hop_slot, "mode": mode, "active_ring": active_ring, "active_local_id": active}
        return active, meta
    else:
        raise ValueError(f"Unknown hop mode: {cfg_hop.mode}")

def is_beam_active(
    local_id: int,
    t_s: float,
    cfg_beam: ProxyBeamConfig,
    cfg_hop: ProxyHopConfig,
) -> Tuple[int, Dict[str, Any]]:
    """
    Decide whether the UT's local beam is active at time t_s.
    """
    active_local, hop_meta = proxy_active_local_id(t_s, cfg_beam, cfg_hop)
    K, R = cfg_beam.K_sectors, cfg_beam.R_rings
    mode = cfg_hop.mode.lower().strip()

    if mode == "single":
        active = 1 if local_id == active_local else 0
        return active, {**hop_meta, "decision": "local==active_local"}
    elif mode == "sector":
        # Active if UT sector matches active sector, regardless of ring
        ut_sector = local_id % K
        active_sector = hop_meta["active_sector"]
        active = 1 if ut_sector == active_sector else 0
        return active, {**hop_meta, "ut_sector": ut_sector, "decision": "sector match"}
    elif mode == "ring":
        ut_ring = local_id // K
        active_ring = hop_meta["active_ring"]
        active = 1 if ut_ring == active_ring else 0
        return active, {**hop_meta, "ut_ring": ut_ring, "decision": "ring match"}
    else:
        raise ValueError(f"Unknown hop mode: {cfg_hop.mode}")

# ----------------------------
# High-level API: compute both IDs + active flag
# ----------------------------
def proxy_beam_and_hop(
    t_s: float,
    ut_lat: float, ut_lon: float,
    sat_id: int,
    sub_lat: float, sub_lon: float,
    cfg_beam: ProxyBeamConfig,
    cfg_hop: ProxyHopConfig,
    include_sat_in_global_id: bool = True,
) -> Dict[str, Any]:
    local_id, meta = proxy_local_beam_id(ut_lat, ut_lon, sub_lat, sub_lon, cfg_beam)
    active, hop_meta = is_beam_active(local_id, t_s, cfg_beam, cfg_hop)

    KR = cfg_beam.kr()
    if include_sat_in_global_id:
        beam_id = sat_id * KR + local_id
    else:
        beam_id = local_id

    out = {
        "t": t_s,
        "sat_id": sat_id,
        "beam_local_id": local_id,
        "beam_id": beam_id,
        "beam_active": active,
        "hop_slot": hop_meta["hop_slot"],
        "active_local_id": hop_meta["active_local_id"],
        "meta": meta,
        "hop_meta": hop_meta,
    }
    return out
