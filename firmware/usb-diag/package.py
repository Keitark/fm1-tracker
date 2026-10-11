"""Re-use the stock-layout packager offline; no device access or flash path."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(HERE.parent/'nes'))
import build_stock_image as pack

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True)
    a=p.parse_args();folder=a.build.resolve()
    manifest=json.loads((folder/'build-manifest.json').read_text())
    if manifest.get('status')!='LINKED_USB_DIAGNOSTIC_UNTESTED':raise ValueError('Build did not pass')
    for field in ('source_sha256','archive_sha256','vendor_overlay_sources'):
        for name,digest in manifest[field].items():
            if hashlib.sha256(Path(name).read_bytes()).hexdigest()!=digest:raise ValueError('Stale input: '+name)
    app=(folder/'fm1-usb-diag.app.bin').read_bytes();elf=(folder/'fm1-usb-diag.elf').read_bytes()
    if hashlib.sha256(app).hexdigest()!=manifest['application_sha256']:raise ValueError('Stale application')
    raw=pack.V14_BACKUP.read_bytes()
    image,report=pack.build_candidate(raw,elf,app,require_peripherals=False,usb_only=True,
                                    usb_peripheral_tests=manifest.get('peripheral_tests',False),
                                    usb_mdx=manifest.get('mdx',False),
                                    usb_mod=manifest.get('mod',False),usb_audio=manifest.get('usb_audio',False),factory_diag=manifest.get('factory_diag',False),factory_user_module=manifest.get('factory_user_module',False),factory_clock=manifest.get('forge_clock','sdk-default'),
                                    usb_nes=manifest.get('nes_smb1',False),rom='smb1' if manifest.get('nes_smb1') else 'rom50')
    out=ROOT/'private-backups/usb-diag-candidates'/report['candidate_sha256']
    out.mkdir(parents=True,exist_ok=True)
    target=out/'fm1-usb-diag.UNQUALIFIED.bin'
    if target.exists() and target.read_bytes()!=image:raise ValueError('Refusing overwrite')
    target.write_bytes(image)
    report.update({'status':'USB0_CANDIDATE_NOT_BENCH_QUALIFIED','flashable':False,
                   'peripheral_tests':manifest.get('peripheral_tests',False),
                   'nes_smb1':manifest.get('nes_smb1',False),
                   'mdx':manifest.get('mdx',False),
                   'mod':manifest.get('mod',False),
                   'factory_diag':manifest.get('factory_diag',False),'usb_audio':manifest.get('usb_audio',False),
                   'lcd_test':manifest.get('lcd_test','row-addressed-rgb'),
                   'serial_uboot':'implemented; transition not hardware-qualified',
                   'update_commit':'BLOCKED: stock layout has no app_dir_head2',
                   'controller_routing_verified':False,'usb_enumeration_verified':False,
                   'terminal_verified':False,'no_uac_update_verified':False,
                   'initial_install_may_require_uac':True,
                   'unverified':['USB connector routing','USB enumeration and log reception',
                                 'physical startup',
                                 'safe CDC flash writer - not implemented']})
    if manifest.get('peripheral_tests'):
        report['unverified']+=['LCD picture','DAC output and audible tone','physical key map',
                               'encoder identity/direction and fast rotation','serial UBOOT transition after peripheral shutdown']
    if manifest.get('factory_diag'):
        report['install_transport']='existing audited CDC to UBOOT, or already UBOOT, through the reviewed local session writer'
        report['direct_cdc_commit']=False
        report['unverified']=[item for item in report['unverified'] if item!='safe CDC flash writer - not implemented']
        report['unverified'].append('physical install and complete readback through the reviewed local session writer')
        if manifest.get('usb_audio'):
            report['unverified'].append('USB Audio class enumeration, capture and playback')
        report['usb_audio_contract']=manifest['usb_audio_contract']
        report['factory_schema']=manifest['factory_schema']
        report['factory_user_module']=manifest['factory_user_module']
        report['asset_free']=True
        report['external_media_required']=False
    (out/'package-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'image':str(target),'bytes':len(image),'sha256':report['candidate_sha256'],
                      'application_bytes':len(app),'preservation_checks':report['checks'],
                      'flashable':False,'device_io':False},indent=2))

if __name__=='__main__':main()
