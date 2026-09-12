from pathlib import Path

ROOT = Path('.')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

# Emit exact private node references from the reopened production BLEND into the authoring sidecar.
path = ROOT / 'Tools/Blender/write_production_sidecars.py'
text = path.read_text(encoding='utf-8')
anchor = '''def production_physics_proxies(objects: dict[str, bpy.types.Object]) -> tuple[list[dict], dict]:
'''
insert = '''def production_depth_plane_authoring(objects: dict[str, bpy.types.Object]) -> list[dict]:
    """Publish private source-first depth-plane node references for runtime binding resolution.

    This is authoring metadata only. Gameplay/runtime public definitions consume opaque binding indices and
    never depend on raw Blender/GLB object names.
    """
    candidates = sorted(
        (
            obj
            for obj in objects.values()
            if obj.type == "MESH"
            and bool(obj.get("runtime_export", False))
            and int(obj.get("lod", -1)) == 0
            and obj.get("CONTROL_SURFACE_ROLE") in ("BOW_DEPTH_PLANE", "STERN_DEPTH_PLANE")
        ),
        key=lambda obj: obj.name,
    )
    if len(candidates) != 4:
        raise RuntimeError(f"Production Antey requires exactly four source-first depth planes, found {len(candidates)}")

    records = []
    group_counts = {"BOW": 0, "STERN": 0}
    for obj in candidates:
        role = obj.get("CONTROL_SURFACE_ROLE")
        if obj.get("ARTICULATION") != "ROTATION" or obj.get("HINGE_AXIS") != "LOCAL_Y" or not bool(obj.get("SIMULATION_OWNS_ANGLE", False)):
            raise RuntimeError(f"Depth plane has invalid articulation contract: {obj.name}")
        group = "BOW" if role == "BOW_DEPTH_PLANE" else "STERN"
        group_counts[group] += 1
        records.append({
            "semanticId": f"depth-plane.{group.lower()}.{group_counts[group]:02d}",
            "group": group,
            "nodeReference": obj.name,
            "articulation": "ROTATION",
            "hingeAxisSource": "LOCAL_Y",
            "simulationOwnsAngle": True,
        })
    if group_counts != {"BOW": 2, "STERN": 2}:
        raise RuntimeError(f"Production Antey depth-plane group counts are invalid: {group_counts}")
    return records


'''
text = replace_once(text, anchor, insert + anchor, 'add source-first depth plane authoring writer')
old = '''    collision_proxies, buoyancy_proxy = production_physics_proxies(objects)
'''
new = '''    collision_proxies, buoyancy_proxy = production_physics_proxies(objects)
    depth_plane_authoring = production_depth_plane_authoring(objects)
'''
text = replace_once(text, old, new, 'build depth plane authoring records')
old = '''        "retractableSailDevices": sail_devices,
        "collision": collision_proxies,
'''
new = '''        "retractableSailDevices": sail_devices,
        "controlSurfaces": depth_plane_authoring,
        "collision": collision_proxies,
'''
text = replace_once(text, old, new, 'publish depth plane authoring records')
path.write_text(text, encoding='utf-8')

# Runtime loader consumes only the private authoring records and publishes opaque bindings.
path = ROOT / 'Game/Submarine/ProductionAnteyAsset.cpp'
text = path.read_text(encoding='utf-8')
old = '''        // M5-V2-B consumes production control-surface authoring only as a private node-resolution source.
        // The public runtime definition retains semantic group + opaque node binding, never raw GLB names.
        const Json& controlSurfaceAuthoring = metadata.at("controlSurfaceAuthoring");
        Require(controlSurfaceAuthoring.is_object(), "Antey controlSurfaceAuthoring must be an object");
        std::unordered_set<std::size_t> depthPlaneBindingIndices;
        const auto appendDepthPlanes = [&](const std::string_view key,
                                           const ProductionDepthPlaneGroup group,
                                           const std::string_view semanticPrefix)
        {
            const Json& records = controlSurfaceAuthoring.at(std::string(key));
            Require(records.is_array() && records.size() == 2U,
                    std::format("Antey {} must contain two depth planes", key));
            for (std::size_t index = 0; index < records.size(); ++index)
            {
                const std::string privateNodeReference = records[index].get<std::string>();
                const std::size_t bindingIndex = ResolvePresentationNodeBindingIndex(**model, privateNodeReference);
                Require((**model).nodeBindings.at(bindingIndex).meshNodeIndex.has_value(),
                        "Antey depth-plane binding must resolve to a drawable mesh node");
                Require(depthPlaneBindingIndices.insert(bindingIndex).second,
                        "Antey depth-plane bindings must be unique");
                definition.depthPlanes.push_back({
                    .semanticId = std::format("depth-plane.{}.{:02}", semanticPrefix, index + 1U),
                    .group = group,
                    .presentationNodeBindingIndex = bindingIndex});
            }
        };
        appendDepthPlanes("bowPlanes", ProductionDepthPlaneGroup::Bow, "bow");
        appendDepthPlanes("sternPlanes", ProductionDepthPlaneGroup::Stern, "stern");
        Require(definition.depthPlanes.size() == 4U, "Antey must expose four production depth-plane bindings");

'''
new = '''        // M5-V2-B: exact node references are private source-first authoring metadata. Resolve them once here
        // and expose only semantic group + opaque model binding index to Game/runtime code.
        const Json& controlSurfaces = authoring.at("controlSurfaces");
        Require(controlSurfaces.is_array() && controlSurfaces.size() == 4U,
                "Antey authoring must contain four production depth-plane records");
        std::unordered_set<std::size_t> depthPlaneBindingIndices;
        std::size_t bowPlaneCount = 0U;
        std::size_t sternPlaneCount = 0U;
        for (const Json& record : controlSurfaces)
        {
            Require(record.is_object(), "Antey depth-plane authoring record must be an object");
            const std::string semanticId = record.at("semanticId").get<std::string>();
            const std::string groupValue = record.at("group").get<std::string>();
            const std::string privateNodeReference = record.at("nodeReference").get<std::string>();
            Require(record.at("articulation").get<std::string>() == "ROTATION" &&
                    record.at("hingeAxisSource").get<std::string>() == "LOCAL_Y" &&
                    record.at("simulationOwnsAngle").get<bool>(),
                    "Antey depth-plane articulation authoring contract is invalid");
            const ProductionDepthPlaneGroup group = groupValue == "BOW"
                ? ProductionDepthPlaneGroup::Bow
                : groupValue == "STERN"
                    ? ProductionDepthPlaneGroup::Stern
                    : throw std::runtime_error("Antey depth-plane group must be BOW or STERN");
            if (group == ProductionDepthPlaneGroup::Bow) ++bowPlaneCount;
            else ++sternPlaneCount;
            const std::size_t bindingIndex = ResolvePresentationNodeBindingIndex(**model, privateNodeReference);
            Require((**model).nodeBindings.at(bindingIndex).meshNodeIndex.has_value(),
                    "Antey depth-plane binding must resolve to a drawable mesh node");
            Require(depthPlaneBindingIndices.insert(bindingIndex).second,
                    "Antey depth-plane bindings must be unique");
            definition.depthPlanes.push_back({
                .semanticId = semanticId,
                .group = group,
                .presentationNodeBindingIndex = bindingIndex});
        }
        Require(bowPlaneCount == 2U && sternPlaneCount == 2U,
                "Antey must expose two bow and two stern production depth planes");

'''
text = replace_once(text, old, new, 'consume private source-first depth plane authoring')
path.write_text(text, encoding='utf-8')
