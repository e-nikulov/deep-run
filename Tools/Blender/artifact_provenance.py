"""Filesystem provenance helpers shared by Blender validation tools."""

from __future__ import annotations

import hashlib
import struct
from datetime import datetime, timezone
from pathlib import Path

import bpy


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def artifact_provenance(path: Path) -> dict:
    resolved = path.resolve(strict=True)
    stat = resolved.stat()
    return {
        "artifact_path": str(resolved),
        "artifact_sha256": sha256_file(resolved),
        "artifact_size_bytes": stat.st_size,
        "artifact_mtime": datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
        "validator_timestamp": datetime.now(timezone.utc).isoformat(),
        "blender_version": bpy.app.version_string,
    }


def require_path_suffix(path: Path, expected_suffix: str, label: str) -> None:
    normalized = path.resolve().as_posix().casefold()
    suffix = expected_suffix.replace("\\", "/").casefold()
    if not normalized.endswith(suffix):
        raise RuntimeError(f"{label} must end with {expected_suffix}: {path.resolve()}")


def mesh_geometry_fingerprint(obj: bpy.types.Object) -> dict:
    """Hash reopened mesh coordinates, connectivity, and object transform."""
    mesh = obj.data
    mesh.calc_loop_triangles()
    topology_digest = hashlib.sha256()
    coordinate_digest = hashlib.sha256()
    header = struct.pack("<QQQ", len(mesh.vertices), len(mesh.edges), len(mesh.loop_triangles))
    topology_digest.update(header)
    coordinate_digest.update(header)
    for row in obj.matrix_world:
        coordinate_digest.update(struct.pack("<4d", *map(float, row)))
    for vertex in mesh.vertices:
        coordinate_digest.update(struct.pack("<3d", *map(float, vertex.co)))
    for edge in mesh.edges:
        topology_digest.update(struct.pack("<2I", *sorted(edge.vertices)))
    for triangle in mesh.loop_triangles:
        topology_digest.update(struct.pack("<3I", *triangle.vertices))
    combined_digest = hashlib.sha256()
    combined_digest.update(bytes.fromhex(topology_digest.hexdigest()))
    combined_digest.update(bytes.fromhex(coordinate_digest.hexdigest()))
    return {
        "vertices": len(mesh.vertices),
        "edges": len(mesh.edges),
        "triangles": len(mesh.loop_triangles),
        "topology_sha256": topology_digest.hexdigest().upper(),
        "coordinates_sha256": coordinate_digest.hexdigest().upper(),
        "sha256": combined_digest.hexdigest().upper(),
        "algorithm": "separate SHA256 topology and matrix-plus-coordinates; combined SHA256 of both digests",
    }
