"""Build the selected Forge configuration and user module; never flashes."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys
from config import validate,ROOT,HALS
def command(project,output,python=sys.executable):
    project=Path(project).resolve();output=Path(output).resolve()
    config=validate(json.loads((project/'forge.json').read_text(encoding='utf-8-sig')))
    args=[str(python),str(ROOT/'firmware/usb-diag/build.py'),'--controller','0','--factory-diag','--forge-clock',config['clockPreset'],'--forge-lcd',config['lcdPreset'],'--out',str(output)]
    for name in HALS:args+=['--forge-hal-'+name,str(int(config['hal'][name]))]
    if not config['usb']['uac']:args+=['--forge-no-uac']
    if config['usb']['uvc']:args+=['--forge-uvc']
    if config['module']=='user':
        module=project/'user.c'
        if not module.is_file() or module.is_symlink() or module.resolve().parent!=project:raise ValueError('User module must be project-local user.c')
        args+=['--forge-user-module',str(module)]
    return args
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--project',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--dry-run',action='store_true')
    a=p.parse_args();project=a.project.resolve()
    output=a.out or project/'build'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    args=command(project,output)
    if a.dry_run:print(json.dumps(args));return
    if output.exists() and any(output.iterdir()):raise ValueError('Build output already contains files; choose a new directory')
    subprocess.run(args,cwd=ROOT,check=True)
if __name__=='__main__':main()
