#!/usr/bin/env python3
"""Convert a legacy occupancy-grid scenario (scenarios/*.json) into an authored
twin-world/1 document plus the Blueprint's mobile-robot `simulation` section.

This is the one-time migration that moved the indoor-drone demo map from a
hard-coded character grid into Studio's World & Layout model. The output is
vector geometry (rectangles in millimetres on named layers); rasterising it
with twin::scene reproduces the original grid cell for cell, which
tests/unit/scene/scene_test.cpp checks (DroneWorldReproducesTheLegacyMap).

Usage: grid_to_world.py SCENARIO.json ROOMS.json OUT_WORLD.json OUT_SIMULATION.json
ROOMS.json names rooms, markers and special objects (see examples/indoor-drone/rooms.json).
"""
import json
import sys


def rects(cells):
    """Greedy decomposition of a cell set into maximal axis-aligned rectangles."""
    cells = set(cells)
    covered = set()
    out = []
    for (x, y) in sorted(cells, key=lambda c: (c[1], c[0])):
        if (x, y) in covered:
            continue
        x2 = x
        while (x2 + 1, y) in cells and (x2 + 1, y) not in covered:
            x2 += 1
        y2 = y
        while all((xx, y2 + 1) in cells and (xx, y2 + 1) not in covered for xx in range(x, x2 + 1)):
            y2 += 1
        for yy in range(y, y2 + 1):
            for xx in range(x, x2 + 1):
                covered.add((xx, yy))
        out.append((x, y, x2, y2))
    return out


def main():
    scenario = json.load(open(sys.argv[1]))
    rooms = json.load(open(sys.argv[2]))
    cell = scenario.get("cell_size_mm", 500)
    gt = scenario["ground_truth"]
    pk = scenario["prior_knowledge"]
    H, W = len(gt), len(gt[0])

    def rect_geom(r):
        x0, y0, x1, y1 = r
        return {"x": x0 * cell, "y": y0 * cell, "w": (x1 - x0 + 1) * cell, "h": (y1 - y0 + 1) * cell}

    def centre(c):
        return {"x": c[0] * cell + cell // 2, "y": c[1] * cell + cell // 2}

    def room_of(x, y):
        for r in rooms["rooms"]:
            x0, y0, x1, y1 = r["cells"]
            if x0 <= x <= x1 and y0 <= y <= y1:
                return r
        return None

    objects = []
    counters = {}

    def add(layer, kind, semantic, name, geometry, **extra):
        counters[semantic] = counters.get(semantic, 0) + 1
        oid = extra.pop("id", None) or f"{semantic}-{counters[semantic]}"
        o = {"id": oid, "layer": layer, "kind": kind, "semanticType": semantic, "name": name,
             "geometry": geometry, "properties": extra.pop("properties", {}), "tags": extra.pop("tags", [])}
        if "asset" in extra:
            o["asset"] = extra.pop("asset")
        objects.append(o)
        return o

    special = {tuple(c) for s in rooms.get("special", []) for c in s["cells"]}

    # Shared structure: where the facility plan and the building agree.
    same = lambda ch: [(x, y) for y in range(H) for x in range(W)
                       if gt[y][x] == ch and pk[y][x] == ch and (x, y) not in special]
    add("structure", "rect", "floor", "Floor 1 slab", {"x": 0, "y": 0, "w": W * cell, "h": H * cell}, id="floor-1-slab", asset="floor-1")
    for i, r in enumerate(rects(same("#")), 1):
        add("structure", "rect", "wall", f"Wall W-{i}", rect_geom(r))
    for r in rects(same("D")):
        room = room_of(r[0], r[1] - 1) or room_of(r[0], r[3] + 1)
        name = f"Door to {room['name']}" if room else "Door"
        add("structure", "rect", "door", name, rect_geom(r), properties={"state": "open"})
    for r in rects(same("o")):
        room = room_of(r[0], r[1])
        name = room["furniture"] if room else "Obstacle"
        add("structure", "rect", "obstacle", name, rect_geom(r))

    # As-built ground truth that the facility plan does not show.
    for s in rooms.get("special", []):
        add("as-built", "rect", s["semanticType"], s["name"], rect_geom(rects([tuple(c) for c in s["cells"]])[0]),
            id=s["id"] + "-as-built", asset=s.get("asset"), properties=s.get("asBuilt", {}))
    hidden = lambda ch: [(x, y) for y in range(H) for x in range(W) if gt[y][x] == ch and pk[y][x] == "?"]
    for i, r in enumerate(rects(hidden("#")), 1):
        add("as-built", "rect", "wall", f"Partition P-{i} (refurbishment)", rect_geom(r))
    for r in rects(hidden("D")):
        add("as-built", "rect", "door", "Office door (refurbishment)", rect_geom(r), properties={"state": "open"})
    for r in rects(hidden("o")):
        add("as-built", "rect", "obstacle", "Desk cluster (refurbishment)", rect_geom(r))

    # The twin's initial knowledge where it differs: the outdated facility plan.
    for s in rooms.get("special", []):
        add("facility-plan", "rect", s["semanticType"], s["name"] + " (plan)", rect_geom(rects([tuple(c) for c in s["cells"]])[0]),
            id=s["id"] + "-plan", asset=s.get("asset"), properties=s.get("planned", {}))
    unknown = [(x, y) for y in range(H) for x in range(W) if pk[y][x] == "?"]
    for r in rects(unknown):
        add("facility-plan", "rect", "unknown", "Unmapped area (refurbished 2025)", rect_geom(r), id="unmapped-office")

    # Facility events (applied by the timeline only).
    for e in scenario.get("events", []):
        x0, y0, x1, y1 = e["rect"]
        add("facility-events", "rect", "hazard", e["description"].split(":")[0] if ":" in e["description"] else "Hazard",
            rect_geom((x0, y0, x1, y1)), id=rooms["eventObject"])

    # Annotations: rooms, the start pad, targets, the robot.
    for r in rooms["rooms"]:
        x0, y0, x1, y1 = r["cells"]
        add("annotations", "zone", r.get("semanticType", "room"), r["name"], rect_geom((x0, y0, x1, y1)), id=r["id"], asset=r.get("asset"))
    home = scenario["home"]
    add("annotations", "point", "start", rooms["start"]["name"], centre(home), id=rooms["start"]["id"], asset=rooms["start"].get("asset"))
    add("annotations", "point", "robot", rooms["robot"]["name"], centre(home), id=rooms["robot"]["id"], asset=rooms["robot"].get("asset"))
    target_ids = []
    for t, meta in zip(scenario["targets"], rooms["targets"]):
        o = add("annotations", "point", "target", t["name"], centre(t["cell"]), id=meta["id"], asset=meta.get("asset"),
                properties={"targetId": t["id"]})
        target_ids.append(o["id"])

    world = {
        "format": "twin-world/1",
        "mode": "spatial",
        "unit": "mm",
        "bounds": {"x": 0, "y": 0, "w": W * cell, "h": H * cell},
        "grid": {"size": cell, "snap": True},
        "layers": [
            {"id": "background", "name": "Background", "role": "background", "visible": True, "locked": True},
            {"id": "structure", "name": "Building structure", "role": "shared", "visible": True, "locked": False},
            {"id": "as-built", "name": "As-built (ground truth)", "role": "ground-truth", "visible": True, "locked": False},
            {"id": "facility-plan", "name": "Facility plan (twin knowledge)", "role": "knowledge", "visible": True, "locked": False},
            {"id": "facility-events", "name": "Facility events", "role": "event", "visible": True, "locked": False},
            {"id": "annotations", "name": "Rooms and markers", "role": "annotation", "visible": True, "locked": False},
        ],
        "objects": objects,
    }
    d = scenario.get("drone", {})
    fmt = lambda v: (f"{v:.6f}".rstrip("0").rstrip(".")) if isinstance(v, float) else str(v)
    simulation = {
        "kind": "mobile-robot",
        "name": scenario["name"],
        "description": scenario["description"],
        "cellSize": cell,
        "seed": scenario.get("seed", 0),
        "robot": {"maxSpeed": fmt(d.get("max_speed_mps", 1.2)), "maxAccel": fmt(d.get("max_accel_mps2", 1.5)),
                  "batteryCapacityWh": fmt(d.get("battery_capacity_wh", 10.0)),
                  "batteryStartPct": fmt(d.get("battery_start_pct", 100.0)), "reservePct": fmt(d.get("reserve_pct", 20.0))},
        "observation": {"sensorRange": fmt(d.get("sensor_range_m", 3.0)), "proximityRange": "1", "updateIntervalMs": 0,
                        "observes": ["wall", "obstacle", "door"], "noise": "none", "discovery": "on-sight",
                        "unobservableLayers": []},
        "mission": {"start": rooms["start"]["id"], "targets": target_ids},
        "timeline": [{"id": "notice-" + rooms["eventObject"], "at": e["at"], "kind": e.get("kind", "hazard"),
                      "object": rooms["eventObject"], "description": e["description"], "notify": e.get("notify", True)}
                     for e in scenario.get("events", [])],
    }
    json.dump(world, open(sys.argv[3], "w"), indent=1, sort_keys=True)
    json.dump(simulation, open(sys.argv[4], "w"), indent=1, sort_keys=True)
    print(f"{len(objects)} objects written to {sys.argv[3]}")


if __name__ == "__main__":
    main()
