"""One-shot source patch: make the source-first builder honor the Ventral artist surface fixture.

This script is deliberately strict: every replacement must match exactly once.
It is used by the branch generation workflow so the resulting builder diff is
committed together with the regenerated production assets.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = ROOT / "Tools" / "Blender" / "build_antey_source_first.py"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.DOTALL)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one regex match, got {count}")
    return result


def main() -> None:
    text = TARGET.read_text(encoding="utf-8")

    text = replace_once(
        text,
        "from mathutils import Matrix, Vector\n\nSOURCE_LENGTH =",
        "from mathutils import Matrix, Vector\n\nsys.path.insert(0, str(Path(__file__).resolve().parent))\nfrom antey_ventral_selection import parse_selection as parse_ventral_selection, resolve_on_mesh as resolve_ventral_selection, strict_surface_faces as ventral_surface_faces, surface_incidence as ventral_surface_incidence\n\nVENTRAL_SELECTION_FIXTURE = Path(__file__).resolve().parents[2] / \"Content\" / \"submarines\" / \"Antey\" / \"selected_geometry_ventral.txt\"\n\nSOURCE_LENGTH =",
        "shared Ventral fixture import",
    )

    ventral_pattern = r'''    else:\n        outline = \[Vector\(path_point\) for path in paths for path_point in path\["coordinates"\]\[:-1\]\]\n        selected_indices = \[\]\n        allowed_indices = \{index for index, polygon in enumerate\(mesh\.polygons\) if tuple\(polygon\.vertices\) in \{tuple\(face\) for face in component_faces_in\}\}\n.*?        selected_indices = sorted\(selected_set\)\n(?=    if not selected_indices:)'''
    ventral_replacement = '''    else:
        # The artist-selected Ventral graph is the ownership contract.  The
        # previous abs(centroid.y) >= 0.18 heuristic retained only an outer
        # skin and silently left valid selected topology in the static lower
        # hull.  Resolve the committed world-space fixture against the complete
        # production-normalized source mesh and transfer every source face that
        # makes an explicitly selected vertex/edge part of a real polygon.
        ventral_selection = parse_ventral_selection(VENTRAL_SELECTION_FIXTURE)
        ventral_resolved = resolve_ventral_selection(mesh, ventral_selection, require_all_edges=True)
        ventral_surface = ventral_surface_faces(mesh, ventral_resolved, component_faces_in)
        selected_indices = list(ventral_surface["faceIndices"])
        if not ventral_surface["pass"]:
            raise RuntimeError(
                "Ventral selected-geometry source ownership is incomplete: "
                f"vertices={ventral_surface['missingSurfaceVertices']} "
                f"edges={ventral_surface['missingSurfaceEdges']}"
            )
'''
    text = regex_once(text, ventral_pattern, ventral_replacement, "replace legacy Ventral centroid heuristic")

    text = replace_once(
        text,
        '    key = ("RUDDER", side)\n',
        '    if side == "Ventral":\n        metadata["selectedGeometryFixture"] = str(VENTRAL_SELECTION_FIXTURE)\n        metadata["selectedGeometryVertexCount"] = len(ventral_selection["vertices"])\n        metadata["selectedGeometryEdgeCount"] = len(ventral_selection["edges"])\n        metadata["selectedGeometrySurfaceFaceCount"] = len(selected_indices)\n        metadata["selectedGeometrySurfaceOwnership"] = "ALL_SELECTED_VERTICES_AND_EDGES_POLYGON_INCIDENT"\n    key = ("RUDDER", side)\n',
        "record Ventral fixture provenance",
    )

    text = replace_once(
        text,
        'def _positive_face_patch(obj: bpy.types.Object, resolved: dict[str, object]) -> dict[str, object]:\n',
        'def _positive_face_patch(obj: bpy.types.Object, resolved: dict[str, object], surface_rule: str = "LEGACY") -> dict[str, object]:\n',
        "positive face patch signature",
    )
    text = replace_once(
        text,
        '''        if set(polygon.vertices) <= selected_vertices:
            manual_faces.append(polygon.index)
        elif selected_edge_count >= 2 and selected_vertex_count >= 3:
            thickness_faces.append(polygon.index)
        elif selected_edge_count:
''',
        '''        if set(polygon.vertices) <= selected_vertices:
            manual_faces.append(polygon.index)
        elif surface_rule == "VENTRAL_SELECTED_SURFACE" and selected_edge_count >= 1 and selected_vertex_count >= 2:
            thickness_faces.append(polygon.index)
        elif selected_edge_count >= 2 and selected_vertex_count >= 3:
            thickness_faces.append(polygon.index)
        elif selected_edge_count:
''',
        "positive Ventral surface rule",
    )

    text = replace_once(
        text,
        '    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]\n    faces = [tuple(polygon.vertices) for polygon in obj.data.polygons if polygon.index not in remove_indices]\n',
        '    # Preserve object-local coordinates and the existing transform.\n    # Baking world coordinates into a mesh while leaving matrix_world intact\n    # would apply the transform twice on a non-identity future candidate.\n    points = [vertex.co.copy() for vertex in obj.data.vertices]\n    faces = [tuple(polygon.vertices) for polygon in obj.data.polygons if polygon.index not in remove_indices]\n',
        "safe static face removal coordinates",
    )

    text = replace_once(
        text,
        '''        resolved = _resolve_positive_mask(hull, mask_record)
        patch = _positive_face_patch(hull, resolved)
        prior_closure_count = int(rudder.get("SYNTHETIC_CLOSURE_FACE_COUNT", 0))
''',
        '''        resolved = _resolve_positive_mask(hull, mask_record)
        transfer_selected_surface = bool(
            side == "Ventral"
            and mask_record.get("surfaceOwnership") == "TRANSFER_SELECTED_SOURCE_FACES"
        )
        patch = _positive_face_patch(
            hull,
            resolved,
            surface_rule="VENTRAL_SELECTED_SURFACE" if transfer_selected_surface else "LEGACY",
        )
        prior_closure_count = int(rudder.get("SYNTHETIC_CLOSURE_FACE_COUNT", 0))
''',
        "enable Ventral transfer mode",
    )

    text = replace_once(
        text,
        '''        if source_first_rebuild:
            # The fresh source-first build already owns every source face in
            # exactly one static or articulated component.  The accepted
            # positive mask is retained as topology metadata (loose seam
            # vertices/edges); transferring the same source patch a second
            # time would duplicate closure/source faces and violate the new
            # source exterior accounting contract.
            transfer_patch = {**patch, "patchFaceIndices": []}
            record = _corrected_rudder_mesh(rudder, hull, side, mask_record, resolved, transfer_patch)
        else:
            record = _corrected_rudder_mesh(rudder, hull, side, mask_record, resolved, patch)
            _replace_object_faces_preserve_vertices(hull, set(patch["patchFaceIndices"]))
''',
        '''        if source_first_rebuild and not transfer_selected_surface:
            # Dorsal and legacy masks remain metadata-only on an already
            # partitioned source-first candidate.
            transfer_patch = {**patch, "patchFaceIndices": []}
            record = _corrected_rudder_mesh(rudder, hull, side, mask_record, resolved, transfer_patch)
        else:
            # Ventral artist surface ownership is a real face transfer even on
            # a source-first candidate: move the selected source-derived faces
            # from the static lower hull into the articulated rudder so the
            # neutral union is unchanged and no selected vertex/edge is loose.
            record = _corrected_rudder_mesh(rudder, hull, side, mask_record, resolved, patch)
            _replace_object_faces_preserve_vertices(hull, set(patch["patchFaceIndices"]))
            if transfer_selected_surface:
                incidence = ventral_surface_incidence(rudder, parse_ventral_selection(VENTRAL_SELECTION_FIXTURE))
                if not incidence["pass"]:
                    raise RuntimeError(f"Ventral surface ownership still contains loose selected topology: {incidence}")
                record["selectedGeometrySurfaceIncidence"] = incidence
''',
        "source-first Ventral face transfer",
    )

    text = replace_once(
        text,
        '"sourceFirstStaticHullPreserved": source_first_rebuild, "note": "fresh source-first candidates retain source faces in their original static/articulated owners; positive-mask seam vertices and edges are preserved without a second face transfer"',
        '"sourceFirstStaticHullPreserved": source_first_rebuild and not any(record.get("selectedGeometrySurfaceIncidence") for record in rudder_records.values()), "note": "legacy source-first masks remain metadata-only; Ventral TRANSFER_SELECTED_SOURCE_FACES moves the exact selected source surface from static lower hull to articulated ownership while preserving the neutral union"',
        "source-first correction note",
    )

    TARGET.write_text(text, encoding="utf-8")
    print(f"PATCHED {TARGET}")


if __name__ == "__main__":
    main()
