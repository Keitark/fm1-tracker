"""OFFLINE ONLY: sector update/restore planning and a NOR byte-array model.

No USB/SCSI imports, no device argument, no payload uploader or live executor.
Model success does not establish physical readiness, timing or recovery.
"""
import argparse
from dataclasses import dataclass,field
import hashlib
import json
from pathlib import Path

import build_stock_image as pack

PAGE=256
SIZE=1048576
ERASED=bytes([255])*pack.SECTOR


def sha(data):
    return hashlib.sha256(data).hexdigest()


def changed_sectors(source,target):
    pack.require(len(source)==len(target)==SIZE,'Expected two full 1 MiB images')
    offsets=[i for i in range(0,SIZE,pack.SECTOR)
             if source[i:i+pack.SECTOR]!=target[i:i+pack.SECTOR]]
    # The first sector contains both metadata and code. Last is not atomic:
    # old metadata still references code overwritten in earlier transactions.
    return [i for i in offsets if i!=pack.AREA]+([pack.AREA] if pack.AREA in offsets else [])


def build_plan(original,candidate,app):
    verification=pack.verify_candidate(original,candidate,app)
    offsets=changed_sectors(original,candidate)
    sectors=[]
    for i in offsets:
        old=original[i:i+pack.SECTOR];new=candidate[i:i+pack.SECTOR]
        changed=sum(a!=b for a,b in zip(old,new))
        sectors.append({'offset':hex(i),'bytes':pack.SECTOR,
                        'original_sha256':sha(old),'candidate_sha256':sha(new),
                        'changed_bytes':changed,
                        'unchanged_neighbor_bytes_rewritten':pack.SECTOR-changed,
                        'page_offsets':[hex(i+p) for p in range(0,pack.SECTOR,PAGE)]})
    return {'schema':1,'offline_only':True,'device_operations_performed':False,
            'physical_write_verified':False,'physical_restore_verified':False,
            'flashable':False,'original_sha256':sha(original),
            'candidate_sha256':sha(candidate),'application_sha256':sha(app),
            'image_bytes':SIZE,'sector_bytes':pack.SECTOR,'program_chunk_bytes':PAGE,
            'sector_count':len(sectors),'sector_order':sectors,
            'directory_sector_last':True,'atomic_update':False,
            'required_preimage':'fresh full read exactly matching original_sha256',
            'verification_checks':verification['checks'],
            'procedure_per_sector':['verify current sector against original sector',
                'erase exactly one 4096-byte sector',
                'establish ready with a separately qualified method',
                'read full sector and require all FF',
                'program all sixteen 256-byte pages, preserving unchanged neighbors',
                'establish ready and verify each programmed page',
                'read full sector and compare candidate_sha256'],
            'final_verification':'fresh full 1 MiB read must match candidate_sha256',
            'failure_policy':'stop; preserve evidence; no automatic retry, reset, or rollback',
            'restore_policy':'separately authorized; rebuild whole changed sectors from original; '
                'reject any differences outside this plan; final full original hash required',
            'unresolved':['exact-loader host ready/status path',
                          'loader status-register changes',
                          'physical boot-mode recovery and restore',
                          'hardware boot and peripherals']}


@dataclass(frozen=True)
class ModelOperation:
    kind:str
    offset:int
    data:bytes=field(default=b'',repr=False) # Never dump private sector bytes.


def model_operations(source,target,allowed):
    """Memory-model operations only. No live transport can consume this here."""
    allowed=set(allowed)
    pack.require(all(i%pack.SECTOR==0 and pack.AREA<=i<SIZE for i in allowed),
                 'Invalid model sector allowlist')
    offsets=changed_sectors(source,target)
    pack.require(set(offsets)<=allowed,'Difference outside planned sectors; refuse broad restore')
    for i in offsets:
        yield ModelOperation('erase',i)
        for p in range(0,pack.SECTOR,PAGE):
            yield ModelOperation('program',i+p,bytes(target[i+p:i+p+PAGE]))


def model_apply(state,operation):
    """Immediate ideal-NOR model; excludes busy timing, voltage and wear."""
    pack.require(len(state)==SIZE,'Model needs a full image')
    offset=operation.offset
    if operation.kind=='erase':
        pack.require(offset%pack.SECTOR==0 and 0<=offset<=SIZE-pack.SECTOR,
                     'Unaligned/out-of-range model erase')
        state[offset:offset+pack.SECTOR]=ERASED
    elif operation.kind=='program':
        data=operation.data
        pack.require(len(data)==PAGE and offset%PAGE==0 and 0<=offset<=SIZE-PAGE,
                     'Unaligned/out-of-range model page')
        previous=state[offset:offset+PAGE]
        pack.require(all((a&b)==b for a,b in zip(previous,data)),
                     'Model cannot program a zero bit back to one without erase')
        state[offset:offset+PAGE]=bytes(a&b for a,b in zip(previous,data))
    else:raise ValueError('Unknown model operation')


def simulate(original,candidate,plan):
    pack.require(sha(original)==plan['original_sha256'] and
                 sha(candidate)==plan['candidate_sha256'],'Model image identity mismatch')
    allowed=[int(s['offset'],16) for s in plan['sector_order']]
    state=bytearray(original)
    operations=list(model_operations(original,candidate,allowed))
    for operation in operations:model_apply(state,operation)
    pack.require(state==candidate,'Model update did not reproduce candidate')
    restore=list(model_operations(state,original,allowed))
    for operation in restore:model_apply(state,operation)
    pack.require(state==original,'Model restore did not reproduce original')
    return {'model_update_operations':len(operations),'model_restore_operations':len(restore),
            'byte_array_update_and_restore_match':True,'physical_recovery_proven':False}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile',choices=('headless','peripherals'),default='headless')
    args=ap.parse_args()
    elf,app=pack.load_built_profile(args.profile)
    original=pack.BACKUP.read_bytes()
    candidate,report=pack.build_candidate(original,elf,app,args.profile=='peripherals')
    image=pack.ROOT/'private-backups/nes-candidates'/args.profile/report['candidate_sha256']/\
        'fm1-nes-stock-layout.UNQUALIFIED.bin'
    pack.require(image.is_file() and image.read_bytes()==candidate,
                 'Build the matching private raw package before planning')
    plan=build_plan(original,candidate,app)
    plan.update(profile=args.profile,original_file=str(pack.BACKUP),candidate_file=str(image),
                planner_sha256=sha(Path(__file__).read_bytes()))
    plan['simulation']=simulate(original,candidate,plan)
    encoded=(json.dumps(plan,indent=2)+'\n').encode()
    output=image.parent/('offline-sector-plan-'+sha(encoded)+'.json')
    if output.exists():pack.require(output.read_bytes()==encoded,'Plan output collision')
    else:
        with output.open('xb') as stream:stream.write(encoded)
    print(json.dumps({'profile':args.profile,'plan':str(output),
                      'sector_count':plan['sector_count'],'simulation':plan['simulation'],
                      'flashable':False,'device_operations_performed':False},indent=2))


if __name__=='__main__':main()
