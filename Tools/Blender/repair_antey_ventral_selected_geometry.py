"""Transfer the complete artist-selected Ventral surface from static hull to rudder.

This is a scoped production repair.  It starts from an existing GameReady
source-first candidate, transfers only real lower-hull source faces required by
`selected_geometry_ventral.txt`, regenerates Ventral LOD1..3, and hard-fails if
any selected vertex/edge remains loose-only or the neutral exterior union
changes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

import bpy
from mathutils import Vector

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

from antey_ventral_selection import parse_selection, resolve_on_mesh, strict_surface_faces, surface_incidence
from build_antey_source_first import _corrected_rudder_mesh, _regenerate_rudder_lods, _coordinate_face_key

RUDDER = "SM_Antey_LOD0_Rudder_Ventral"
HULL = "SM_Antey_LOD0_Hull_LowerSource"
TOLERANCE_M = 1.0e-5


def options() -> argparse.Namespace:
    values = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--selection", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    return parser.parse_args(values)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def world_face_counter(obj: bpy.types.Object, digits: int = 6) -> Counter:
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    return Counter(_coordinate_face_key(points, polygon, digits=digits) for polygon in obj.data.polygons)


def neutral_union() -> Counter:
    result = Counter()
    for name in (HULL, RUDDER):
        obj = bpy.data.objects.get(name)
        if obj is None or obj.type != "MESH":
            raise RuntimeError(f"Required ventral object missing: {name}")
        result.update(world_face_counter(obj))
    return result


def remove_faces_preserve_object_space(obj: bpy.types.Object, remove_indices: set[int]) -> None:
    """Delete polygons without rebaking world coordinates into object space."""
    old = obj.data
    vertices = [vertex.co.copy() for vertex in old.vertices]
    faces = [tuple(polygon.vertices) for polygon in old.polygons if polygon.index not in remove_indices]
    edges = [tuple(edge.vertices) for edge in old.edges]
    mesh = bpy.data.meshes.new(f"{obj.name}_VentralSurfaceTransferMesh")
    mesh.from_pydata(vertices, edges, faces)
    mesh.update(calc_edges=True)
    for material in old.materials:
        mesh.materials.append(material)
    obj.data = mesh
    if old.users == 0:
        bpy.data.meshes.remove(old)


def compatible_resolved(selection: dict, hull: bpy.types.Object, hull_resolved: dict) -> dict:
    """Create the legacy builder record while retaining all 357 fixture edges.

    Some fixture edges already live only on the articulated rudder and are not
    present on the static lower hull.  `_corrected_rudder_mesh` only needs their
    world endpoints to preserve them; final surface incidence proves they are
    backed by polygons after the source-face transfer.
    """
    records = []
    for edge_id, left, right in selection["edges"]:
        pair = tuple(sorted((hull_resolved["vertices"][left], hull_resolved["vertices"][right])))
        records.append({
            "legacyIndex": int(edge_id),
            "legacyVertices": [int(left), int(right)],
            "resolvedVertices": list(pair),
            "resolvedEdgeIndex": int(hull_resolved["edgeIndexByPair"].get(pair, -1)),
            "endpointWorld": [list(selection["vertices"][left]), list(selection["vertices"][right])],
        })
    return {
        "resolvedVertices": dict(hull_resolved["vertices"]),
        "resolvedEdges": records,
        "vertexResolution": [
            {"legacyIndex": int(legacy), "resolvedIndex": int(index), "method": "WORLD_COORDINATE_RESOLUTION", "errorM": 0.0, "world": list(selection["vertices"][legacy])}
            for legacy, index in sorted(hull_resolved["vertices"].items())
        ],
    }


def make_patch(hull: bpy.types.Object, selection: dict, hull_resolved: dict) -> dict:
    strict = strict_surface_faces(hull.data, hull_resolved)
    face_indices = strict["faceIndices"]
    face_edges = {tuple(sorted(edge)) for index in face_indices for edge in hull.data.polygons[index].edge_keys}
    face_vertices = {vertex for index in face_indices for vertex in hull.data.polygons[index].vertices}
    selected_vertices = set(hull_resolved["vertices"].values())
    resolved_edge_indices = {
        edge_id: hull_resolved["edgeIndexByPair"].get(pair, -1)
        for edge_id, pair in hull_resolved["edges"].items()
    }
    selected_face_set = set(strict["allVerticesSelectedFaceIndices"])
    edge_face_set = set(strict["selectedEdgeFaceIndices"])
    touching = set(strict["touchingFaceIndices"])
    return {
        "manualSelectedFaceIndices": sorted(selected_face_set),
        "thicknessSideFaceIndices": sorted(edge_face_set - selected_face_set),
        "patchFaceIndices": list(face_indices),
        "boundaryCandidateFaceIndices": sorted(touching - set(face_indices)),
        "fixedNeighborFaceIndices": [],
        "incidentFaceIndices": sorted(touching),
        "patchEdges": face_edges,
        "patchVertices": face_vertices,
        "maskOnlyVertexIndices": sorted(selected_vertices - face_vertices),
        "maskOnlyResolvedEdgeIndices": sorted(index for index in resolved_edge_indices.values() if index >= 0 and tuple(sorted(hull.data.edges[index].vertices)) not in face_edges),
        "selectedEdgeCoverage": len(hull_resolved["edgeSet"] & face_edges),
        "selectedVertexCoverage": len(selected_vertices & face_vertices),
        "strictSurfaceAudit": strict,
    }


def main() -> None:
    opts = options()
    source = opts.input.resolve(strict=True)
    selection_path = opts.selection.resolve(strict=True)
    output = opts.output.resolve()
    report_path = opts.report.resolve()
    source_hash = sha256(source)
    bpy.ops.wm.open_mainfile(filepath=str(source))
    if Path(bpy.data.filepath).resolve(strict=True) != source:
        raise RuntimeError("GameReady fresh-open mismatch")

    selection = parse_selection(selection_path)
    rudder = bpy.data.objects.get(RUDDER)
    hull = bpy.data.objects.get(HULL)
    if rudder is None or rudder.type != "MESH" or hull is None or hull.type != "MESH":
        raise RuntimeError(f"Required objects missing: rudder={rudder} hull={hull}")

    before_incidence = surface_incidence(rudder, selection)
    before_union = neutral_union()
    before_rudder_faces = len(rudder.data.polygons)
    before_rudder_tris = sum(len(p.vertices) - 2 for p in rudder.data.polygons)
    before_hull_faces = len(hull.data.polygons)

    hull_resolved = resolve_on_mesh(hull.data, selection, matrix=hull.matrix_world, tolerance=TOLERANCE_M, require_all_edges=False)
    patch = make_patch(hull, selection, hull_resolved)
    if not patch["patchFaceIndices"]:
        raise RuntimeError("Ventral selected geometry repair found no source-derived lower-hull faces to transfer")

    # Never re-add a face already owned by the articulated rudder.  Face
    # identity is compared in production world coordinates, not polygon ids.
    rudder_faces = set(world_face_counter(rudder))
    hull_points = [hull.matrix_world @ vertex.co for vertex in hull.data.vertices]
    delta_faces = []
    already_owned = []
    for index in patch["patchFaceIndices"]:
        key = _coordinate_face_key(hull_points, hull.data.polygons[index], digits=6)
        if key in rudder_faces:
            already_owned.append(index)
        else:
            delta_faces.append(index)
    patch["patchFaceIndices"] = delta_faces
    patch["alreadyOwnedFaceIndices"] = already_owned

    resolved = compatible_resolved(selection, hull, hull_resolved)
    mask_record = {
        "object": RUDDER,
        "expectedVertexCount": len(selection["vertices"]),
        "expectedEdgeCount": len(selection["edges"]),
        "vertices": [{"legacyIndex": index, "world": list(point)} for index, point in sorted(selection["vertices"].items())],
        "edges": [{"legacyIndex": edge_id, "legacyVertices": [left, right]} for edge_id, left, right in selection["edges"]],
    }
    correction = _corrected_rudder_mesh(rudder, hull, "Ventral", mask_record, resolved, patch)
    remove_faces_preserve_object_space(hull, set(delta_faces))
    changed_lods = _regenerate_rudder_lods(rudder, "Ventral")
    bpy.context.view_layer.update()
    bpy.context.evaluated_depsgraph_get().update()

    after_incidence = surface_incidence(rudder, selection)
    if not after_incidence["pass"]:
        raise RuntimeError(f"Ventral selection is still loose after repair: {after_incidence}")
    after_union = neutral_union()
    before_unique = Counter({face: 1 for face in before_union})
    after_unique = Counter({face: 1 for face in after_union})
    union_missing = sum((before_unique - after_unique).values())
    union_extra = sum((after_unique - before_unique).values())
    if union_missing or union_extra:
        raise RuntimeError(f"Neutral ventral exterior union changed: missing={union_missing} extra={union_extra}")

    rudder["VENTRAL_SELECTION_FIXTURE"] = selection_path.name
    rudder["VENTRAL_SELECTION_SURFACE_VERTEX_COUNT"] = len(selection["vertices"])
    rudder["VENTRAL_SELECTION_SURFACE_EDGE_COUNT"] = len(selection["edges"])
    rudder["VENTRAL_SELECTION_TRANSFERRED_FACE_COUNT"] = len(delta_faces)
    rudder["VENTRAL_SELECTION_SURFACE_OWNERSHIP"] = "ALL_SELECTED_VERTICES_AND_EDGES_POLYGON_INCIDENT"
    bpy.context.scene["ventral_rudder_selected_geometry_contract"] = {
        "fixture": selection_path.name,
        "vertices": len(selection["vertices"]),
        "edges": len(selection["edges"]),
        "surfaceOwnedVertices": after_incidence["surfaceOwnedVertices"],
        "surfaceOwnedEdges": after_incidence["surfaceOwnedEdges"],
        "transferredStaticHullFaces": len(delta_faces),
        "neutralUnionMissing": union_missing,
        "neutralUnionExtra": union_extra,
        "status": "PASS",
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output), check_existing=False)
    # Fresh reopen before accepting the candidate file.
    bpy.ops.wm.open_mainfile(filepath=str(output.resolve(strict=True)))
    reopened = bpy.data.objects.get(RUDDER)
    if reopened is None:
        raise RuntimeError("Reopened candidate lost Ventral rudder")
    reopened_incidence = surface_incidence(reopened, selection)
    if not reopened_incidence["pass"]:
        raise RuntimeError(f"Fresh-reopen Ventral surface coverage failed: {reopened_incidence}")

    report = {
        "input": str(source),
        "inputSha256": source_hash,
        "candidate": str(output),
        "candidateSha256": sha256(output),
        "selection": str(selection_path),
        "fixtureVertices": len(selection["vertices"]),
        "fixtureEdges": len(selection["edges"]),
        "before": {
            "rudderFaces": before_rudder_faces,
            "rudderTriangles": before_rudder_tris,
            "hullFaces": before_hull_faces,
            "surfaceIncidence": before_incidence,
        },
        "transfer": {
            "strictCandidateFaces": len(patch["strictSurfaceAudit"]["faceIndices"]),
            "alreadyOwnedFaces": len(already_owned),
            "transferredFaces": len(delta_faces),
            "sourceFaceIndices": delta_faces,
            "lodsRegenerated": changed_lods,
        },
        "after": {
            "rudderFaces": len(reopened.data.polygons),
            "rudderTriangles": sum(len(p.vertices) - 2 for p in reopened.data.polygons),
            "surfaceIncidence": reopened_incidence,
            "neutralUnionMissing": union_missing,
            "neutralUnionExtra": union_extra,
        },
        "correction": {
            "newRudderPolygonCount": correction.get("newRudderPolygonCount"),
            "newRudderTriangleCount": correction.get("newRudderTriangleCount"),
            "duplicatePolygonOriginsRemoved": correction.get("duplicatePolygonOriginsRemoved", []),
        },
        "pass": reopened_incidence["pass"] and union_missing == 0 and union_extra == 0,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({
        "beforeSurfaceVertices": before_incidence["surfaceOwnedVertices"],
        "beforeSurfaceEdges": before_incidence["surfaceOwnedEdges"],
        "afterSurfaceVertices": reopened_incidence["surfaceOwnedVertices"],
        "afterSurfaceEdges": reopened_incidence["surfaceOwnedEdges"],
        "transferredFaces": len(delta_faces),
        "neutralUnionMissing": union_missing,
        "neutralUnionExtra": union_extra,
        "candidateSha256": report["candidateSha256"],
    }))
    print(f"ANTEY_VENTRAL_SELECTED_GEOMETRY_REPAIR_OK {output}")


if __name__ == "__main__":
    main()
