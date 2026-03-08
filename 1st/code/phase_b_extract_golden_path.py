#!/usr/bin/env python3
import argparse
import csv
from collections import Counter, defaultdict

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--in_csv", required=True)
    p.add_argument("--out_dir", required=True)
    p.add_argument("--t0", type=float, default=None)
    p.add_argument("--t1", type=float, default=None)
    p.add_argument("--ok_value", default="OK")
    p.add_argument("--sep", default="->")
    return p.parse_args()

def in_window(t, t0, t1):
    if t0 is not None and t < t0: return False
    if t1 is not None and t > t1: return False
    return True

def main():
    args = parse_args()

    counts = Counter()
    best_hop = defaultdict(lambda: None)
    first_seen = {}

    with open(args.in_csv, newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            t = float(row["time_sec"])
            if not in_window(t, args.t0, args.t1):
                continue

            status = (row.get("status") or "").strip()
            path = (row.get("path") or "").strip()
            if status != args.ok_value or not path:
                continue

            hop = row.get("hop_count")
            hop = int(hop) if hop is not None and str(hop).strip() != "" else None

            counts[path] += 1
            if hop is not None:
                if best_hop[path] is None or hop < best_hop[path]:
                    best_hop[path] = hop
            if path not in first_seen:
                first_seen[path] = t

    if not counts:
        raise SystemExit("No valid path rows found (status=OK and path non-empty) in the selected window.")

    max_count = max(counts.values())
    candidates = [p for p,c in counts.items() if c == max_count]

    def hop_val(p):
        return best_hop[p] if best_hop[p] is not None else 10**9

    min_hop = min(hop_val(p) for p in candidates)
    candidates = [p for p in candidates if hop_val(p) == min_hop]

    min_fs = min(first_seen[p] for p in candidates)
    candidates = [p for p in candidates if first_seen[p] == min_fs]

    golden = sorted(candidates)[0]

    nodes = [x.strip() for x in golden.split(args.sep) if x.strip()]
    node_set = sorted(set(nodes))
    link_set = [f"{a}-{b}" for a,b in zip(nodes, nodes[1:])]

    import os
    os.makedirs(args.out_dir, exist_ok=True)

    golden_path_txt = os.path.join(args.out_dir, "golden_path.txt")
    pruned_nodes_txt = os.path.join(args.out_dir, "pruned_node_set.txt")
    pruned_links_txt = os.path.join(args.out_dir, "pruned_link_set.txt")
    summary_txt = os.path.join(args.out_dir, "golden_path_summary.txt")

    with open(golden_path_txt, "w") as f:
        f.write(" -> ".join(nodes) + "\n")

    with open(pruned_nodes_txt, "w") as f:
        for n in node_set:
            f.write(n + "\n")

    with open(pruned_links_txt, "w") as f:
        for e in link_set:
            f.write(e + "\n")

    with open(summary_txt, "w") as f:
        f.write(f"in_csv={args.in_csv}\n")
        f.write(f"t0={args.t0}, t1={args.t1}\n")
        f.write(f"golden_path={golden}\n")
        f.write(f"count={counts[golden]}\n")
        f.write(f"hop_min={hop_val(golden)}\n")
        f.write(f"first_seen={first_seen[golden]}\n")
        f.write(f"unique_paths={len(counts)}\n")

    print("[OK] golden_path:", golden)
    print("[OK] wrote:", golden_path_txt)
    print("[OK] wrote:", pruned_nodes_txt)
    print("[OK] wrote:", pruned_links_txt)
    print("[OK] wrote:", summary_txt)

if __name__ == "__main__":
    main()
