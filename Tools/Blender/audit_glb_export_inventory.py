"""Fresh-import GLB inventory for runtime export boundary QA."""
import argparse, json, sys
from pathlib import Path
import bpy

def main():
    values=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    parser=argparse.ArgumentParser(); parser.add_argument('--glb',type=Path,required=True); parser.add_argument('--output',type=Path,required=True); options=parser.parse_args(values)
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(options.glb.resolve()))
    records=[]
    for obj in sorted((item for item in bpy.context.scene.objects if item.type=='MESH'),key=lambda item:item.name):
        obj.data.calc_loop_triangles(); name=obj.name
        lod=next((int(token[-1]) for token in name.split('_') if token.startswith('LOD') and token[-1:].isdigit()),None)
        category='LOD'+str(lod) if lod is not None else 'UNCLASSIFIED'
        records.append({'name':name,'lod':lod,'role':obj.get('source_first_role','IMPORTED_RUNTIME_MESH'),'runtime':True,'authoringDebug':name.startswith(('REF_','HP_','VOL_','COL_','PHY_','REVIEW_')),'materials':[slot.material.name for slot in obj.material_slots if slot.material],'primitiveCount':len(obj.data.materials) or 1,'triangles':len(obj.data.loop_triangles),'category':category})
    payload={'glb':str(options.glb.resolve()),'meshNodes':len(records),'records':records,'countsByCategory':{category:sum(1 for item in records if item['category']==category) for category in sorted({item['category'] for item in records})}}
    options.output.parent.mkdir(parents=True,exist_ok=True); options.output.write_text(json.dumps(payload,indent=2),encoding='utf-8'); print(f'GLB_EXPORT_INVENTORY_WRITTEN {options.output} nodes={len(records)}')
if __name__=='__main__': main()
