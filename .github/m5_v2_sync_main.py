import json
import subprocess
from pathlib import Path

ROOT = Path('.')
ALLOWED_CONFLICTS = {
    'Content/submarines/Antey/Antey.authoring.json',
    'Engine/Assets/submarines/Antey/Antey.authoring.json',
    'Game/Submarine/ProductionAnteyAsset.cpp',
    'Game/Submarine/ProductionAnteyAsset.h',
    'Tools/Blender/write_production_sidecars.py',
}


def run(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, text=True, capture_output=True)
    if result.stdout:
        print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if check and result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(args)}")
    return result


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)

feature_authoring = json.loads((ROOT / 'Content/submarines/Antey/Antey.authoring.json').read_text(encoding='utf-8'))
control_surfaces = feature_authoring.get('controlSurfaces')
if not isinstance(control_surfaces, list) or len(control_surfaces) != 4:
    raise RuntimeError('feature branch must contain exactly four V2 controlSurfaces before main sync')

run('git', 'fetch', 'origin', 'main')
run('git', 'merge', '--no-commit', '--no-ff', 'origin/main', check=False)
conflicts = set(filter(None, run('git', 'diff', '--name-only', '--diff-filter=U').stdout.splitlines()))
unexpected = conflicts - ALLOWED_CONFLICTS
if unexpected:
    raise RuntimeError(f'unexpected main-sync conflicts: {sorted(unexpected)}')
print(f'expected conflicts: {sorted(conflicts)}')

for path in sorted(ALLOWED_CONFLICTS):
    run('git', 'checkout', 'origin/main', '--', path)

for relative in (
    'Content/submarines/Antey/Antey.authoring.json',
    'Engine/Assets/submarines/Antey/Antey.authoring.json',
):
    path = ROOT / relative
    data = json.loads(path.read_text(encoding='utf-8'))
    data['controlSurfaces'] = control_surfaces
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')

header_path = ROOT / 'Game/Submarine/ProductionAnteyAsset.h'
header = header_path.read_text(encoding='utf-8')
depth_types = '''enum class ProductionDepthPlaneGroup
{
    Bow,
    Stern,
};

struct ProductionDepthPlane final
{
    std::string semanticId;
    ProductionDepthPlaneGroup group = ProductionDepthPlaneGroup::Bow;
    // Opaque Assets-layer binding index; raw GLB/source names stop in the loader.
    std::size_t presentationNodeBindingIndex = 0;
};

'''
if 'struct ProductionDepthPlane final' not in header:
    marker = '''struct ProductionCompartment final
{'''
    header = replace_once(header, marker, depth_types + marker, 'header depth-plane types')
if 'std::vector<ProductionDepthPlane> depthPlanes;' not in header:
    marker = '    std::vector<ProductionRetractableSailDevice> retractableSailDevices;\n'
    header = replace_once(header, marker, marker + '    std::vector<ProductionDepthPlane> depthPlanes;\n', 'header depth-plane vector')
header_path.write_text(header, encoding='utf-8')

cpp_path = ROOT / 'Game/Submarine/ProductionAnteyAsset.cpp'
cpp = cpp_path.read_text(encoding='utf-8')
if '.depthPlanes = {},' not in cpp:
    marker = '            .retractableSailDevices = {},\n'
    cpp = replace_once(cpp, marker, marker + '            .depthPlanes = {},\n', 'loader depth-plane initializer')
control_loader = '''        // M5-V2-B: exact node references are private source-first authoring metadata. Resolve them once here
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
if 'const Json& controlSurfaces = authoring.at("controlSurfaces")' not in cpp:
    marker = '        const Json& retractableSailDevices = authoring.at("retractableSailDevices");\n'
    cpp = replace_once(cpp, marker, control_loader + marker, 'loader controlSurfaces')
cpp_path.write_text(cpp, encoding='utf-8')

writer_path = ROOT / 'Tools/Blender/write_production_sidecars.py'
writer = writer_path.read_text(encoding='utf-8')
helper = '''def production_depth_plane_authoring(objects: dict[str, bpy.types.Object]) -> list[dict]:
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
if 'def production_depth_plane_authoring(' not in writer:
    writer = replace_once(writer, 'def production_physics_proxies(', helper + 'def production_physics_proxies(', 'writer depth-plane helper')
if 'depth_plane_authoring = production_depth_plane_authoring(objects)' not in writer:
    marker = '    collision_proxies, buoyancy_proxy = production_physics_proxies(objects)\n'
    writer = replace_once(writer, marker, marker + '    depth_plane_authoring = production_depth_plane_authoring(objects)\n', 'writer depth-plane derive')
if '"controlSurfaces": depth_plane_authoring,' not in writer:
    marker = '        "retractableSailDevices": sail_devices,\n'
    writer = replace_once(writer, marker, marker + '        "controlSurfaces": depth_plane_authoring,\n', 'writer depth-plane output')
writer_path.write_text(writer, encoding='utf-8')

remaining = set(filter(None, run('git', 'diff', '--name-only', '--diff-filter=U').stdout.splitlines()))
if remaining:
    raise RuntimeError(f'unresolved main-sync conflicts remain: {sorted(remaining)}')
run('git', 'add', '-A')
run('git', 'diff', '--cached', '--check')
print('MAIN_SYNC_RESOLVED')
