"""Pinned local deployment inputs. No USB or cipher imports, no device I/O."""
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
SIZE=0x100000
SECTOR=4096
PAGE=256
BASELINE=ROOT/'private-backups/original-reader-repeat-20260927-220410/fm1-flash-1MiB.bin'
BASELINE_SHA='4d69ef5bbd662b0b762dc01956378f7c1776b317e463ccc15815cd5bc0b338a3'
ORIGINAL=ROOT/'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin'
ORIGINAL_SHA='a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61'
CANDIDATES={
    'headless':'69c018fad3c2b6e53b5e8322548cedd0eeba6bd0d3596e696b3404c3426e62aa',
    'peripherals':'a6e81059ee918e970853d1ce4fd3878caea1e8a76bfc719075462d8b559b4a1d'}
ORDER=tuple(range(0x5000,0x2f000,SECTOR))+(0x4000,)


def require(condition,message):
    if not condition:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def checked(path,digest,size=SIZE):
    data=path.read_bytes()
    require(len(data)==size and sha(data)==digest,f'Input identity mismatch: {path.name}')
    return data


@dataclass(frozen=True)
class Deployment:
    profile:str
    baseline:bytes
    target:bytes
    order:tuple

    def validate(self):
        require(self.profile in CANDIDATES,'Unknown profile')
        require(type(self.baseline) is bytes and type(self.target) is bytes,'Immutable images required')
        require(len(self.baseline)==len(self.target)==SIZE,'Full 1 MiB images required')
        require(sha(self.baseline)==BASELINE_SHA,'Unreviewed current baseline')
        require(self.order==ORDER,'Sector scope/order changed')
        changed=tuple(i for i in range(0,SIZE,SECTOR)
                      if self.baseline[i:i+SECTOR]!=self.target[i:i+SECTOR])
        require(set(changed)==set(ORDER),'Changed sectors differ from reviewed plan')
        # Normalize only the two reviewed non-application differences, then
        # require the exact earlier audited candidate hash. Never serialize keys.
        old=checked(ORIGINAL,ORIGINAL_SHA)
        normalized=bytearray(self.target)
        for offset in (0xee046,0xee069):normalized[offset]=old[offset]
        require(sha(normalized)==CANDIDATES[self.profile],'Unreviewed NES payload/layout')


def load_deployment(profile):
    require(profile in CANDIDATES,'Unknown profile')
    old=checked(ORIGINAL,ORIGINAL_SHA)
    baseline=checked(BASELINE,BASELINE_SHA)
    require([i for i,(a,b) in enumerate(zip(old,baseline)) if a!=b]==[0xee046,0xee069],
            'Baseline delta changed')
    receipt=json.loads(BASELINE.with_suffix('.json').read_text())
    require(receipt['bytes']==SIZE and receipt['sha256']==BASELINE_SHA and
            receipt['verification']=='second complete read matched every byte',
            'Missing two-pass baseline receipt')
    digest=CANDIDATES[profile]
    source=ROOT/'private-backups/nes-candidates'/profile/digest/'fm1-nes-stock-layout.UNQUALIFIED.bin'
    old_target=checked(source,digest)
    target=bytearray(baseline)
    for offset in ORDER:target[offset:offset+SECTOR]=old_target[offset:offset+SECTOR]
    deployment=Deployment(profile,baseline,bytes(target),ORDER)
    deployment.validate()
    return deployment


def restore_scope(actual,deployment):
    """Permit interrupted/partial sectors, but never broaden recovery scope."""
    require(len(actual)==SIZE,'Recovery requires a full current image')
    for offset in range(0,SIZE,SECTOR):
        if offset not in deployment.order:
            require(actual[offset:offset+SECTOR]==deployment.baseline[offset:offset+SECTOR],
                    f'Recovery difference outside authorized sectors at 0x{offset:x}')

