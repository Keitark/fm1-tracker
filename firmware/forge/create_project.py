"""Create a new local FM1 Forge starter. Never overwrites an existing directory."""
import argparse
import json
from pathlib import Path
from config import defaults,validate,slug,ROOT
TEMPLATE=Path(__file__).with_name('user_module.c')
def create(base,project_slug,display_name=None,configuration=None):
    project_slug=slug(project_slug)
    config=validate(configuration) if configuration is not None else defaults(project_slug,display_name or project_slug)
    if config['project']['slug']!=project_slug:raise ValueError('Config/project directory slug mismatch')
    base=Path(base).resolve();base.mkdir(parents=True,exist_ok=True)
    directory=base/project_slug
    # mkdir is atomic: concurrent creation also refuses an existing target.
    directory.mkdir()
    (directory/'forge.json').write_text(json.dumps(config,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (directory/'user.c').write_bytes(TEMPLATE.read_bytes())
    (directory/'README.md').write_text(f"# {config['project']['displayName']}\n\nGenerated FM1 Forge ABI1 starter. Edit `user.c` and set `module` to `user` in `forge.json`.\n\nBuild with:\n\n```powershell\npython {ROOT / 'firmware/forge/build_project.py'} --project .\n```\n\nBuild never writes a device. Review the exact artifact/plan before a separate physical transfer.\nThe fixed runtime owns USB CDC/UAC, serial-to-UBOOT, clocks, scanner, LCD DMA and master volume.\nSee `{ROOT / 'firmware/forge/README.md'}` for input mapping, callback bounds and bench gates.\n",encoding='utf-8')
    return directory
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base',type=Path,required=True);p.add_argument('--slug',required=True);p.add_argument('--name');p.add_argument('--config',type=Path)
    a=p.parse_args();config=json.loads(a.config.read_text(encoding='utf-8-sig')) if a.config else None
    print(create(a.base,a.slug,a.name,config))
if __name__=='__main__':main()
