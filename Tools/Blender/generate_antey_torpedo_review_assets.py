#!/usr/bin/env python3
"""Generate Deep Run visual-review GLBs for Antey torpedo content.

These are original procedural review meshes. They are not runtime/gameplay
integration and do not copy geometry, materials or textures from the user-
supplied reference pack.
"""
from __future__ import annotations

import json
import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Content" / "Weapons" / "Torpedoes"


def lathe(profile: list[tuple[float, float]], segments: int = 20):
    vertices: list[tuple[float, float, float]] = []
    rings: list[list[int]] = []
    for x, radius in profile:
        if abs(radius) < 1e-9:
            rings.append([len(vertices)])
            vertices.append((x, 0.0, 0.0))
            continue
        ring: list[int] = []
        for i in range(segments):
            angle = 2.0 * math.pi * i / segments
            ring.append(len(vertices))
            vertices.append((x, radius * math.cos(angle), radius * math.sin(angle)))
        rings.append(ring)

    faces: list[tuple[int, int, int]] = []
    for left, right in zip(rings, rings[1:]):
        if len(left) == 1:
            pole = left[0]
            for i in range(len(right)):
                faces.append((pole, right[(i + 1) % len(right)], right[i]))
        elif len(right) == 1:
            pole = right[0]
            for i in range(len(left)):
                faces.append((left[i], left[(i + 1) % len(left)], pole))
        else:
            for i in range(len(left)):
                j = (i + 1) % len(left)
                faces.extend(((left[i], right[i], right[j]), (left[i], right[j], left[j])))
    return vertices, faces


def add_fin(vertices, faces, x0, x1, body_radius, tip_radius, thickness, axis, sign):
    base = len(vertices)
    outline = ((x0, body_radius), (x0, tip_radius), (x1, tip_radius * 0.93), (x1, body_radius * 0.92))
    for offset in (-thickness / 2.0, thickness / 2.0):
        for x, radius in outline:
            radial = radius * sign
            vertices.append((x, radial, offset) if axis == "y" else (x, offset, radial))
    faces.extend(((base, base + 1, base + 2), (base, base + 2, base + 3),
                  (base + 4, base + 6, base + 5), (base + 4, base + 7, base + 6)))
    for i in range(4):
        j = (i + 1) % 4
        faces.extend(((base + i, base + j, base + 4 + j), (base + i, base + 4 + j, base + 4 + i)))


def add_propeller(vertices, faces, x, hub_radius, blade_radius, phase):
    hub_vertices, hub_faces = lathe(((x - 0.05, hub_radius), (x + 0.05, hub_radius)), 10)
    offset = len(vertices)
    vertices.extend(hub_vertices)
    faces.extend(tuple(offset + index for index in face) for face in hub_faces)
    for blade in range(4):
        angle = phase + 2.0 * math.pi * blade / 4.0
        base = len(vertices)
        blade_vertices = []
        for xx in (x - 0.025, x + 0.025):
            for radius, local_angle in ((hub_radius * 0.9, angle - 0.12),
                                        (blade_radius, angle + 0.18),
                                        (blade_radius, angle + 0.40),
                                        (hub_radius * 0.9, angle + 0.12)):
                blade_vertices.append((xx, radius * math.cos(local_angle), radius * math.sin(local_angle)))
        vertices.extend(blade_vertices)
        faces.extend(((base, base + 1, base + 2), (base, base + 2, base + 3),
                      (base + 4, base + 6, base + 5), (base + 4, base + 7, base + 6)))
        for i in range(4):
            j = (i + 1) % 4
            faces.extend(((base + i, base + j, base + 4 + j), (base + i, base + 4 + j, base + 4 + i)))


def build(identity: str):
    if identity == "USET80":
        length, diameter = 7.9, 0.533
        radius = diameter / 2.0
        profile = ((length / 2.0, 0.0),
                   (length / 2.0 - 0.18, radius * 0.72),
                   (length / 2.0 - 0.55, radius),
                   (-length / 2.0 + 1.15, radius),
                   (-length / 2.0 + 0.72, radius * 0.82),
                   (-length / 2.0 + 0.38, radius * 0.55),
                   (-length / 2.0 + 0.12, radius * 0.35))
        x0, x1, fin_tip, fin_thickness = -length / 2.0 + 1.0, -length / 2.0 + 0.28, 1.70, 0.045
        propeller_x = (-length / 2.0 + 0.18, -length / 2.0 + 0.02)
    else:
        length, diameter = 11.3, 0.650
        radius = diameter / 2.0
        profile = ((length / 2.0, 0.0),
                   (length / 2.0 - 0.24, radius * 0.72),
                   (length / 2.0 - 0.72, radius),
                   (-length / 2.0 + 1.45, radius),
                   (-length / 2.0 + 0.90, radius * 0.84),
                   (-length / 2.0 + 0.45, radius * 0.55),
                   (-length / 2.0 + 0.16, radius * 0.34))
        x0, x1, fin_tip, fin_thickness = -length / 2.0 + 1.25, -length / 2.0 + 0.35, 1.78, 0.055
        propeller_x = (-length / 2.0 + 0.22, -length / 2.0 + 0.04)
    vertices, faces = lathe(profile)
    for axis in ("y", "z"):
        for sign in (-1, 1):
            add_fin(vertices, faces, x0, x1, radius * 0.83, radius * fin_tip, fin_thickness, axis, sign)
    add_propeller(vertices, faces, propeller_x[0], radius * 0.30, radius * 0.88, 0.15)
    add_propeller(vertices, faces, propeller_x[1], radius * 0.26, radius * 0.82, -0.35)
    return length, diameter, vertices, faces


def write_glb(path: Path, identity: str, length: float, diameter: float, vertices, faces):
    flat_indices = [index for face in faces for index in face]
    position_bytes = b"".join(struct.pack("<fff", *vertex) for vertex in vertices)
    index_bytes = b"".join(struct.pack("<H", index) for index in flat_indices)
    index_offset = (len(position_bytes) + 3) & ~3
    binary = bytearray(position_bytes)
    binary.extend(b"\0" * (index_offset - len(binary)))
    binary.extend(index_bytes)
    while len(binary) % 4:
        binary.append(0)
    f32 = lambda value: struct.unpack("<f", struct.pack("<f", value))[0]
    minimum = [f32(min(vertex[axis] for vertex in vertices)) for axis in range(3)]
    maximum = [f32(max(vertex[axis] for vertex in vertices)) for axis in range(3)]
    gltf = {
        "asset": {"version": "2.0", "generator": "Deep Run procedural review draft"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": f"SM_{identity}_Draft", "mesh": 0}],
        "meshes": [{"name": f"SM_{identity}_Draft", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
        "materials": [{"name": f"M_{identity}_Draft", "pbrMetallicRoughness": {"baseColorFactor": [0.28, 0.31, 0.22, 1.0], "metallicFactor": 0.35, "roughnessFactor": 0.58}}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(position_bytes), "target": 34962},
                        {"buffer": 0, "byteOffset": index_offset, "byteLength": len(index_bytes), "target": 34963}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": len(vertices), "type": "VEC3", "min": minimum, "max": maximum},
                      {"bufferView": 1, "componentType": 5123, "count": len(flat_indices), "type": "SCALAR", "min": [min(flat_indices)], "max": [max(flat_indices)]}],
        "extras": {"reviewStatus": "VISUAL_DRAFT", "lengthMeters": length, "diameterMeters": diameter, "forwardAxis": "+X"},
    }
    json_chunk = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(json_chunk) % 4:
        json_chunk += b" "
    total = 12 + 8 + len(json_chunk) + 8 + len(binary)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(struct.pack("<4sII", b"glTF", 2, total))
        output.write(struct.pack("<I4s", len(json_chunk), b"JSON")); output.write(json_chunk)
        output.write(struct.pack("<I4s", len(binary), b"BIN\0")); output.write(binary)


def main():
    for identity, directory, filename in (("USET80", "USET80", "USET80_review.glb"),
                                          ("65-76A_Kit", "65-76A", "65-76A_Kit_review.glb")):
        length, diameter, vertices, faces = build(identity)
        write_glb(OUT / directory / filename, identity, length, diameter, vertices, faces)
        print(f"{identity}: {length:.3f} m x {diameter:.3f} m, {len(vertices)} vertices, {len(faces)} triangles")


if __name__ == "__main__":
    main()
