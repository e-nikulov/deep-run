"""Fresh-reopen technical checks for the source-first candidate."""
from __future__ import annotations
import argparse, json, math, re, sys
from collections import Counter
from pathlib import Path
import bpy
from mathutils import Vector

def main():
    values=sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else []
    parser=argparse.ArgumentParser(); parser.add_argument("--candidate",type=Path,required=True); parser.add_argument("--output",type=Path,required=True); opts=parser.parse_args(values)
    candidate=opts.candidate.resolve(strict=True); bpy.ops.wm.open_mainfile(filepath=str(candidate));
    if Path(bpy.data.filepath).resolve()!=candidate: raise RuntimeError("fresh reopen path mismatch")
    meshes=[o for o in bpy.context.scene.objects if o.type=="MESH"]; lod0=[o for o in meshes if int(o.get("lod",-1))==0 and o.get("runtime_export")]
    finite=True; total=0; roles=Counter(); materials=set(); bad=[]
    for obj in lod0:
        tris=sum(len(p.vertices)-2 for p in obj.data.polygons); total+=tris; roles[obj.get("source_first_role","UNKNOWN")]+=1
        materials.update(slot.material.name for slot in obj.material_slots if slot.material)
        for vertex in obj.data.vertices:
            if not all(math.isfinite(value) for value in vertex.co): finite=False; bad.append(obj.name); break
    # The authoring contract contains several P700 marker families per
    # launcher.  Only the 24 canonical launcher hardpoints count here.
    hardpoint_re = re.compile(r"^P700_(Port|Starboard)_\d{2}$")
    hardpoints=[o for o in bpy.context.scene.objects if o.type=="EMPTY" and hardpoint_re.fullmatch(o.name)]
    tubes=[o for o in bpy.context.scene.objects if o.type=="EMPTY" and (o.name.startswith("TorpedoTube_") or o.name.startswith("TorpedoTube533_") or o.name.startswith("TorpedoTube650_"))]
    required={"MAT_Antey_Hull","MAT_Antey_Propellers"}
    report={"candidate":str(candidate),"fresh_reopen":True,"source_first":bool(bpy.context.scene.get("source_first")),"lod0_objects":len(lod0),"lod0_triangles":total,"role_counts":dict(roles),"materials":sorted(materials),"only_stable_materials":materials.issubset(required),"finite_coordinates":finite,"bad_objects":sorted(set(bad)),"p700_hardpoints":len(hardpoints),"p700_port":sum(1 for o in hardpoints if o.name.startswith("P700_Port")),"p700_starboard":sum(1 for o in hardpoints if o.name.startswith("P700_Starboard")),"torpedo_markers":len(tubes),"torpedo_533mm":sum(1 for o in tubes if abs(float(o.get("diameter_m",0))-0.533)<1e-6),"torpedo_650mm":sum(1 for o in tubes if abs(float(o.get("diameter_m",0))-0.650)<1e-6),"physics_proxies":[o.name for o in bpy.context.scene.objects if o.get("physics_proxy_role")]}
    report["lod_counts"]={str(lod):{"objects":sum(1 for o in meshes if int(o.get("lod",-1))==lod and o.get("runtime_export")),"triangles":sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in meshes if int(o.get("lod",-1))==lod and o.get("runtime_export"))} for lod in range(4)}
    report["pass"]=report["source_first"] and report["fresh_reopen"] and report["finite_coordinates"] and report["only_stable_materials"] and report["p700_hardpoints"]==24 and report["torpedo_markers"]==6
    opts.output.parent.mkdir(parents=True,exist_ok=True); opts.output.write_text(json.dumps(report,indent=2),encoding="utf-8"); print("ANTEY_SOURCE_FIRST_VALIDATION_OK" if report["pass"] else "ANTEY_SOURCE_FIRST_VALIDATION_FAILED")
    if not report["pass"]: raise RuntimeError(json.dumps(report))
if __name__=="__main__": main()
