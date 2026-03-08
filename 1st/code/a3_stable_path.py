#!/usr/bin/env python3
import os, csv, json
from collections import defaultdict, Counter, deque

def read_edges_by_time(path):
    edges_by_t = defaultdict(list)
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            t = int(float(row["time_sec"]))
            src = row["src"].strip()
            dst = row["dst"].strip()
            et  = row["edge_type"].strip()
            edges_by_t[t].append((src, dst, et))
    return dict(sorted(edges_by_t.items()))

def build_adj(edges):
    adj = defaultdict(set)
    for a,b,_ in edges:
        adj[a].add(b)
        adj[b].add(a)
    return adj

def bfs_all_shortest_paths(adj, src, dst):
    if src not in adj or dst not in adj:
        return []
    q = deque([src])
    dist = {src: 0}
    parent = defaultdict(list)

    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in dist:
                dist[v] = dist[u] + 1
                parent[v].append(u)
                q.append(v)
            elif dist[v] == dist[u] + 1:
                parent[v].append(u)

    if dst not in dist:
        return []

    paths = []
    stack = [(dst, [dst])]
    while stack:
        node, path = stack.pop()
        if node == src:
            paths.append(list(reversed(path)))
            continue
        for p in parent[node]:
            stack.append((p, path + [p]))
    return paths

def sat_in_path(path):
    return [n for n in path if n.startswith("SAT")]

def score_path(path, prev_sat, continuity_bonus):
    score = 0.0
    score -= (len(path) - 1)  # hop penalty
    sats = sat_in_path(path)
    if prev_sat is not None and prev_sat in sats:
        score += continuity_bonus
    return score

def pick_best_path(paths, prev_sat, continuity_bonus):
    if not paths:
        return None, None
    best, best_score = None, None
    for p in paths:
        s = score_path(p, prev_sat, continuity_bonus)
        if best_score is None or s > best_score:
            best, best_score = p, s
    sats = sat_in_path(best)
    chosen_sat = sats[0] if sats else None
    return best, chosen_sat

def longest_run(seq):
    best_item, best_len, best_s, best_e = None, 0, 0, -1
    cur_item, cur_len, cur_s = None, 0, 0
    for i, x in enumerate(seq):
        if x is None:
            cur_item, cur_len = None, 0
            continue
        if x == cur_item:
            cur_len += 1
        else:
            cur_item, cur_len, cur_s = x, 1, i
        if cur_len > best_len:
            best_item, best_len, best_s, best_e = cur_item, cur_len, cur_s, i
    return best_item, best_len, best_s, best_e

def main():
    in_csv = os.environ["IN_CSV"]
    out_dir = os.environ["OUT_DIR"]
    src = os.environ.get("SRC_NODE", "GW2").strip()
    dst = os.environ.get("DST_NODE", "UT4").strip()
    continuity_bonus = float(os.environ.get("CONTINUITY_BONUS", "5.0"))
    os.makedirs(out_dir, exist_ok=True)

    edges_by_t = read_edges_by_time(in_csv)
    times = sorted(edges_by_t.keys())

    best_rows = []
    chosen_paths = []
    prev_sat = None

    for t in times:
        adj = build_adj(edges_by_t[t])
        paths = bfs_all_shortest_paths(adj, src, dst)
        best, prev_sat = pick_best_path(paths, prev_sat, continuity_bonus)

        if best is None:
            best_rows.append([t, src, dst, "", 0, "NO_PATH"])
            chosen_paths.append(None)
        else:
            path_str = "->".join(best)
            best_rows.append([t, src, dst, path_str, len(best)-1, "OK"])
            chosen_paths.append(path_str)

    valid_paths = [p for p in chosen_paths if p is not None]
    majority_path, majority_count = (None, 0)
    if valid_paths:
        c = Counter(valid_paths)
        majority_path, majority_count = c.most_common(1)[0]

    run_item, run_len, run_s, run_e = longest_run(chosen_paths)
    run_start_t = times[run_s] if run_len > 0 else None
    run_end_t   = times[run_e] if run_len > 0 else None

    golden = majority_path if majority_path is not None else run_item

    out_best = os.path.join(out_dir, "best_path_timeseries.csv")
    with open(out_best, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time_sec", "src", "dst", "path", "hop_count", "status"])
        w.writerows(best_rows)

    out_json = os.path.join(out_dir, "golden_path_summary.json")
    summary = {
        "src": src,
        "dst": dst,
        "input_connectivity": in_csv,
        "scoring": {
            "continuity_bonus": continuity_bonus,
            "objective": "shortest_path + continuity_preference"
        },
        "golden_path": golden,
        "majority_path": {
            "path": majority_path,
            "count": majority_count,
            "total_times": len(times),
            "availability_ratio": (majority_count / len(times)) if times else 0.0
        },
        "longest_contiguous_run": {
            "path": run_item,
            "run_len_samples": run_len,
            "t_start": run_start_t,
            "t_end": run_end_t
        }
    }
    with open(out_json, "w") as f:
        json.dump(summary, f, indent=2)

    print(f"[OK] wrote: {out_best}")
    print(f"[OK] wrote: {out_json}")

if __name__ == "__main__":
    main()
