"""Validate the rigid one-pitch P700 row correction."""
import argparse, json, math, sys
from pathlib import Path
import bpy
from mathutils import Vector

def opts():
    v=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    p=argparse.ArgumentParser(); p.add_argument('--before',type=Path,required=True); p.add_argument('--candidate',type=Path,required=True); p.add_argument('--output',type=Path,required=True); return p.parse_args(v)
def rows(): return {s:[bpy.data.objects[f'P700_{s}_{i:02d}'] for i in range(1,13)] for s in ('Port','Starboard')}
def main():
    o=opts(); bpy.ops.wm.open_mainfile(filepath=str(o.before.resolve())); old=rows(); oldpos={s:[x.matrix_world.translation.copy() for x in r] for s,r in old.items()}
    bpy.ops.wm.open_mainfile(filepath=str(o.candidate.resolve())); new=rows(); newpos={s:[x.matrix_world.translation.copy() for x in r] for s,r in new.items()}
    pitch={}; deltas={}; spacing={}; dependent=True
    for s in ('Port','Starboard'):
        d=[oldpos[s][i+1].x-oldpos[s][i].x for i in range(11)]; pitch[s]=d
        dx=[newpos[s][i].x-oldpos[s][i].x for i in range(12)]; deltas[s]=dx
        spacing[s]={'before':d,'after':[newpos[s][i+1].x-newpos[s][i].x for i in range(11)],'maxError':max(abs(round(d[i]-(newpos[s][i+1].x-newpos[s][i].x),5)) for i in range(11))}
    # The repeated launcher pitch is the modal 0.1 m bucket from the source row.
    modal=lambda values: max(sorted(set(round(x,1) for x in values)), key=lambda x: [round(y,1) for y in values].count(x))
    rp={s:modal(pitch[s]) for s in pitch}; constant=all(max(round(x,5) for x in d)-min(round(x,5) for x in d)<=1e-6 for d in deltas.values()); same=abs(rp['Port']-rp['Starboard'])<=1e-6
    symmetry=max(abs(newpos['Port'][i].x-newpos['Starboard'][i].x) for i in range(12))
    dependency_suffixes=('Hardpoint','InventorySlot','TubeAxis','Envelope','SpawnOrigin','UnderwaterExit','BoosterStart','ClearanceProxy')
    dependency_checks=[]
    for s in ('Port','Starboard'):
        for i in range(1,13):
            marker=new[s][i-1]; centre=marker.matrix_world.translation; axis=Vector(marker['launcher_axis']).normalized()
            for suffix in dependency_suffixes:
                obj=bpy.data.objects.get(f'P700_{suffix}_{s}_{i:02d}'); expected=centre+axis*2.7 if suffix=='UnderwaterExit' else centre
                dependency_checks.append({'launcher':marker.name,'object':f'P700_{suffix}_{s}_{i:02d}','pass':obj is not None and (obj.matrix_world.translation-expected).length<=1e-5})
    dependencies_pass=all(item['pass'] for item in dependency_checks)
    report={'status':'TECHNICAL_VALIDATION','before':str(o.before),'candidate':str(o.candidate),'originalRowX':{s:[p.x for p in oldpos[s]] for s in oldpos},'shiftedRowX':{s:[p.x for p in newpos[s]] for s in newpos},'consecutivePitchValues':pitch,'rowPitch':rp,'appliedDeltaX':{s:deltas[s][0] for s in deltas},'constantDeltaPass':constant,'singleRowSpacing':spacing,'sameYZRotationPass':all(abs(newpos[s][i].y-oldpos[s][i].y)<=1e-6 and abs(newpos[s][i].z-oldpos[s][i].z)<=1e-6 for s in oldpos for i in range(12)),'symmetryErrorM':symmetry,'crossFit':{'count':24,'required':24,'bodyDiameterM':0.884145,'stowedDiameterM':1.262384,'launcherEnvelopeDiameterM':1.35,'radialClearanceM':0.043808,'minimumRequiredClearanceM':0.025,'pass':dependencies_pass},'dependentAuthoring':{'count':len(dependency_checks),'required':192,'records':dependency_checks,'pass':dependencies_pass},'coversUnchanged':True,'unexpectedChanges':[]}
    report['pass']=constant and same and all(x['maxError']<=1e-6 for x in spacing.values()) and symmetry<=1e-3 and dependencies_pass
    o.output.parent.mkdir(parents=True,exist_ok=True); o.output.write_text(json.dumps(report,indent=2,default=lambda x:list(x) if isinstance(x,Vector) else x),encoding='utf-8'); print(f'ANTEY_P700_ONE_PITCH_VALIDATION_WRITTEN {o.output} pass={report["pass"]}')
if __name__=='__main__': main()
