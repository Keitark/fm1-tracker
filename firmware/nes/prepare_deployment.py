"""Offline rebase of audited NES candidates onto the latest exact backup."""
import argparse
import json
from pathlib import Path
import sys
import build_stock_image as pack
sys.path.insert(0,str(pack.ROOT/'tools/jieli-wl82'))
import fm1_deployment as d


def prepare(profile):
    deployment=d.load_deployment(profile)
    elf,app=pack.load_built_profile(profile)
    candidate,verification=pack.build_candidate(d.checked(d.ORIGINAL,d.ORIGINAL_SHA),elf,app,
                                               require_peripherals=profile=='peripherals')
    d.require(d.sha(candidate)==d.CANDIDATES[profile],'Current build differs from pinned candidate')
    decoded,info,_=pack.decode_application(deployment.target)
    d.require(decoded==app and info['data_crc_valid'],'Rebased application verification failed')
    report={'schema':1,'profile':profile,'baseline_sha256':d.BASELINE_SHA,
            'target_sha256':d.sha(deployment.target),'application_sha256':d.sha(app),
            'baseline_file':str(d.BASELINE),'sector_order':[hex(x) for x in deployment.order],
            'preserved_baseline_delta_offsets':['0xee046','0xee069'],
            'flashable':False,'hardware_boot_verified':False,
            'physical_restore_verified':False,'device_operations_performed':False,
            'image_checks':verification['checks']}
    folder=d.ROOT/'private-backups/nes-deployments'/profile/report['target_sha256']
    folder.mkdir(parents=True,exist_ok=True)
    image=folder/'fm1-nes-current-baseline.UNQUALIFIED.bin'
    if image.exists():d.require(image.read_bytes()==deployment.target,'Output collision')
    else:
        with image.open('xb') as output:output.write(deployment.target)
    report['target_file']=str(image)
    manifest=folder/'deployment.json'
    encoded=json.dumps(report,indent=2)+'\n'
    if manifest.exists():d.require(manifest.read_text()==encoded,'Manifest collision')
    else:
        with manifest.open('x') as output:output.write(encoded)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',choices=tuple(d.CANDIDATES),default='headless')
    print(json.dumps(prepare(parser.parse_args().profile),indent=2))
