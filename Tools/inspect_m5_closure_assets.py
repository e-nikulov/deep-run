from __future__ import annotations

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GLB = ROOT / "Content/submarines/Antey/Antey.glb"
AUTHORING = ROOT / "Content/submarines/Antey/Antey.authoring.json"


def mat_mul(a, b):
    out = [[0.0] * 4 for _ in range(4)]
    for r in range(4):
        for c in range(4):
            out[r][c] = sum(a[r][k] * b[k][c] for k in range(4))
    return out


def local_matrix(node):
    if "matrix" in node:
        v = node["matrix"]
        return [[v[c * 4 + r] for c in range(4)] for r in range(4)]
    t = node.get("translation", [0.0, 0.0, 0.0])
    q = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    s = node.get("scale", [1.0, 1.0, 1.0])
    x, y, z, w = q
    n = math.sqrt(x*x+y*y+z*z+w*w) or 1.0
    x, y, z, w = x/n, y/n, z/n, w/n
    r = [
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), 0.0],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), 0.0],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
    for rr in range(3):
        for cc in range(3):
            r[rr][cc] *= s[cc]
    r[0][3], r[1][3], r[2][3] = t
    return r


def transform(m, p):
    x, y, z = p
    return tuple(sum(m[r][c] * (x, y, z, 1.0)[c] for c in range(4)) for r in range(3))


def read_glb_json(path: Path):
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<4sII", data, 0)
    assert magic == b"glTF" and version == 2 and length == len(data)
    off = 12
    chunk_len, chunk_type = struct.unpack_from("<II", data, off)
    off += 8
    assert chunk_type == 0x4E4F534A
    return json.loads(data[off:off+chunk_len].decode("utf-8"))


gltf = read_glb_json(GLB)
nodes = gltf["nodes"]
meshes = gltf["meshes"]
accessors = gltf["accessors"]
parents = {}
for i, node in enumerate(nodes):
    for child in node.get("children", []):
        parents[child] = i

world_cache = {}
def world_matrix(i):
    if i in world_cache:
        return world_cache[i]
    local = local_matrix(nodes[i])
    parent = parents.get(i)
    world_cache[i] = local if parent is None else mat_mul(world_matrix(parent), local)
    return world_cache[i]


def mesh_bounds(node_index):
    node = nodes[node_index]
    mesh_index = node.get("mesh")
    if mesh_index is None:
        return None
    lo = [float("inf")]*3
    hi = [float("-inf")]*3
    found = False
    for prim in meshes[mesh_index].get("primitives", []):
        pos = prim.get("attributes", {}).get("POSITION")
        if pos is None:
            continue
        acc = accessors[pos]
        if "min" not in acc or "max" not in acc:
            continue
        amin, amax = acc["min"], acc["max"]
        for sx in (amin[0], amax[0]):
            for sy in (amin[1], amax[1]):
                for sz in (amin[2], amax[2]):
                    p = transform(world_matrix(node_index), (sx, sy, sz))
                    for k in range(3):
                        lo[k] = min(lo[k], p[k]); hi[k] = max(hi[k], p[k])
        found = True
    return (lo, hi) if found else None


authoring = json.loads(AUTHORING.read_text(encoding="utf-8"))
print("=== RETRACTABLE SAIL DEVICES ===")
for rec in authoring["retractableSailDevices"]:
    name = rec["nodeReference"]
    matches = [i for i,n in enumerate(nodes) if n.get("name") == name]
    if not matches:
        print(name, "MISSING")
        continue
    b = mesh_bounds(matches[0])
    if b is None:
        print(name, "NO_MESH_BOUNDS")
        continue
    lo, hi = b
    dims = [hi[i]-lo[i] for i in range(3)]
    center = [(hi[i]+lo[i])*0.5 for i in range(3)]
    stow = rec["stowedLocalPostTransform"]
    stow_delta = [stow[0][3], stow[1][3], stow[2][3]]
    print(f"{rec['semanticId']} {name}: center={center}, dims={dims}, min={lo}, max={hi}, stow_delta={stow_delta}")

print("=== DEPTH PLANES ===")
for name in [n.get("name","") for n in nodes if "BowPlane" in n.get("name","") or "SternPlane" in n.get("name","")]:
    i = next(j for j,n in enumerate(nodes) if n.get("name") == name)
    print(name, mesh_bounds(i))
