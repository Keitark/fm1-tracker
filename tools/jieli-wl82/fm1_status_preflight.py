"""Cold RAM status-loader preflight. No flash erase/program implementation.

The original helper's initialization/online operation may modify NOR status
and protection bits. The array itself is only read in this preflight.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'tools/jl-uboot-tool'))
from fm1_scsi import SCSIDev
from jltech.crc import jl_crc16
from read_fm1_firmware import read_inquiry, LOADER_SIDE_EFFECT_WARNING

LOADER=ROOT/'tmp/wl82-confined-status-loader/loader.mengli.bin'
LOADER_SHA='288dcd73fed7eeb4fd144aa943edcbf4dfe410bcaf695e18f70ecd2f2b1a806d'
ORIGINAL_LOADER=ROOT/'tools/jl-uboot-tool/data/loaderblobs/usb/wl82loader.bin'
ORIGINAL_LOADER_SHA='d41da6126760c9d66660bcc0cac8d27d221806c5e369a8036921efe68dca5376'
BACKUP=ROOT/'private-backups/readback-20260926-024637/fm1-flash-1MiB.bin'
BACKUP_SHA='a45aa21afd8a659612427213ce80c7500a42fcbd8548262fb611f03df84e8b61'
BASE=0x1c02000
FLASH_SIZE=0x100000


def checked_file(path,size,digest):
    data=path.read_bytes()
    if len(data)!=size or hashlib.sha256(data).hexdigest()!=digest:
        raise ValueError(f'Unexpected file identity: {path.name}')
    return data


def cdb(op,args=b''):
    command=op.to_bytes(2,'big')+args
    if len(command)>16:raise ValueError('CDB overflow')
    return command+b'\xff'*(16-len(command))


class Transport:
    """Strict bounded transfers, no automatic retry/reset or key commands."""
    def __init__(self,dev,original_control=False,read_delay_ms=0):
        if read_delay_ms not in (0,10):raise ValueError('Unreviewed read pacing')
        self.dev=dev;self.commands=0;self.last_command=None
        self.original_control=original_control
        self.read_delay_ms=read_delay_ms
        self.compare_progress=None
    def transfer(self,op,args=b'',payload=None,length=16):
        if op not in (0xfb06,0xfb08,0xfc03,0xfc0a,0xfd05):
            raise ValueError('Command not permitted by preflight')
        if self.original_control and op==0xfc03:
            raise ValueError('Original loader does not support FC03; control forbids it')
        buffer=None if payload is not None else bytearray(length)
        self.commands+=1
        self.last_command={'opcode':f'{op:04x}','number':self.commands,
                           'bytes':len(payload) if payload is not None else length}
        if op in (0xfb06,0xfd05):
            self.last_command['address']=hex(int.from_bytes(args[:4],'big'))
        started=time.perf_counter()
        try:
            residue=self.dev.execute(cdb(op,args),payload,buffer)
        finally:
            self.last_command['elapsed_ms']=round((time.perf_counter()-started)*1000,3)
        if residue!=0 or (buffer is not None and len(buffer)!=length):
            raise IOError(f'Incomplete command {op:04x}; state uncertain, no automatic retry')
        if buffer is not None and op!=0xfd05 and buffer[:2]!=op.to_bytes(2,'big'):
            raise IOError(f'Wrong echo for {op:04x}; stop')
        return bytes(buffer) if buffer is not None else None
    def upload(self,raw):
        digest=ORIGINAL_LOADER_SHA if self.original_control else LOADER_SHA
        if len(raw)!=24064 or hashlib.sha256(raw).hexdigest()!=digest:
            raise ValueError('Unreviewed or wrong-profile helper')
        for offset in range(0,len(raw),512):
            data=raw[offset:offset+512]
            args=(BASE+offset).to_bytes(4,'big')+len(data).to_bytes(2,'big')+b'\0'+jl_crc16(data).to_bytes(2,'little')
            self.transfer(0xfb06,args,payload=data)
        self.transfer(0xfb08,BASE.to_bytes(4,'big')+b'\0\1')
    def status(self,allow_uninitialized=False):
        reply=self.transfer(0xfc03)
        if reply[4:8]!=b'FMS1' or reply[8:]!=bytes(8):
            raise IOError('Status extension signature/reserved bytes mismatch')
        if reply[2]==1 and allow_uninitialized:return None
        if reply[2]!=0:raise IOError(f'Status helper error {reply[2]}; stop')
        return reply[3]
    def online(self):
        reply=self.transfer(0xfc0a)
        if reply[2]!=3 or int.from_bytes(reply[4:8],'little')!=0x856014:
            raise IOError('Unexpected online NOR identity')
    def ready(self,timeout=30.0):
        deadline=time.monotonic()+timeout
        count=0;busy_seen=False
        while True:
            status=self.status();count+=1
            if not status&1:return {'sr1':status,'polls':count,'busy_seen':busy_seen}
            busy_seen=True
            if time.monotonic()>=deadline:raise TimeoutError('NOR busy deadline exceeded; stop without retry/reset')
            time.sleep(0.01)
    def read(self,address,length):
        if address<0 or not 1<=length<=256 or address+length>FLASH_SIZE:
            raise ValueError('Read outside reviewed flash bounds')
        if self.read_delay_ms:time.sleep(self.read_delay_ms/1000)
        try:
            return self.transfer(0xfd05,address.to_bytes(4,'big')+length.to_bytes(2,'big'),length=length)
        except Exception as error:
            raise IOError(f'Flash read at 0x{address:x}, {length} bytes failed: {error}') from error
    def compare(self,expected,pass_number):
        if len(expected)!=FLASH_SIZE:raise ValueError('Expected full flash image')
        self.compare_progress={'pass':pass_number,'matched_bytes':0}
        initial=None if self.original_control else self.ready()
        if initial is not None and initial['sr1']&2:
            raise IOError('Unexpected NOR write-enable latch before readback')
        digest=hashlib.sha256()
        for address in range(0,FLASH_SIZE,256):
            data=self.read(address,256)
            if data!=expected[address:address+256]:
                raise IOError(f'Full-image comparison failed at 0x{address:x}; no array writes performed by preflight')
            digest.update(data)
            self.compare_progress['matched_bytes']=address+256
            if (address+256)%0x20000==0:print(f'Readback pass {pass_number}: {address+256}/{FLASH_SIZE}',flush=True)
        final=None if self.original_control else self.ready()
        if final is not None and final['sr1']!=initial['sr1']:
            raise IOError('NOR status changed during readback')
        return {'sha256':digest.hexdigest(),'status_before':initial,'status_after':final}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',required=True)
    parser.add_argument('--confirm-cold-uboot',action='store_true',required=True)
    parser.add_argument('--result',type=Path,required=True)
    parser.add_argument('--original-control',action='store_true',
                        help='Unmodified original loader, no FC03; read-path control only')
    parser.add_argument('--read-delay-ms',type=int,choices=(0,10),default=0,
                        help='Pause before each FD05 read; does not retry failed commands')
    args=parser.parse_args()
    if args.result.exists():raise ValueError('Result exists; will not repeat the run')
    loader_path=ORIGINAL_LOADER if args.original_control else LOADER
    loader_sha=ORIGINAL_LOADER_SHA if args.original_control else LOADER_SHA
    raw=checked_file(loader_path,24064,loader_sha)
    original=checked_file(BACKUP,FLASH_SIZE,BACKUP_SHA)
    report={'status':'incomplete','stage':'open','array_program_or_erase_commands_sent':False,
            'loader_sha256':loader_sha,'chip_key_queried':False,'reset_requested':False,
            'original_control':args.original_control,'flash_path_qualified':False,
            'read_delay_ms':args.read_delay_ms}
    dev=None;io=None
    started=time.perf_counter()
    print(LOADER_SIDE_EFFECT_WARNING,flush=True)
    try:
        dev=SCSIDev(args.device)
        report['transport']='fm1_scsi_aligned_sense_v1'
        report['adapter_info']=getattr(dev,'adapter_info',None)
        identity=read_inquiry(dev)
        if identity!=('WL82','UBOOT1.00','1.00'):raise ValueError('Wrong device identity')
        report['identity']=identity
        io=Transport(dev,original_control=args.original_control,read_delay_ms=args.read_delay_ms)
        report['stage']='cold RAM upload';io.upload(raw)
        report['stage']='online';io.online()
        # A/B probes separate ordinary loader read failure from SPI-status
        # interference. Do not issue FC03 until baseline array read succeeds.
        report['stage']='baseline read before status'
        baseline=io.read(0,256)
        if baseline!=original[:256]:raise IOError('Baseline probe differs from original')
        report['baseline_probe_sha256']=hashlib.sha256(baseline).hexdigest()
        if not args.original_control:
            report['stage']='status samples';report['status_samples']=[io.ready() for _ in range(4)]
            report['stage']='probe after status'
            if io.read(0,256)!=baseline:raise IOError('Array probe changed after status sampling')
            report['probe_after_status_matches']=True
        report['stage']='full readback';report['readbacks']=[]
        for n in (1,2):report['readbacks'].append(io.compare(original,n))
        report['commands_after_inquiry']=io.commands
        report['stage']='complete';report['status']='complete'
        report['device']=args.device
        label='Original-loader read control' if args.original_control else 'Status extension preflight'
        print(f'{label}: two full original-image comparisons passed. Flash path is not qualified.',flush=True)
    except Exception as error:
        report['status']='failed';report['error']=str(error)
        raise
    finally:
        report['total_elapsed_ms']=round((time.perf_counter()-started)*1000,3)
        if io is not None:
            report['last_command']=io.last_command
            report['commands_after_inquiry']=io.commands
            report['compare_progress']=io.compare_progress
        if dev is not None:dev.close()
        with args.result.open('x',encoding='utf-8') as output:
            json.dump(report,output,indent=2)


if __name__=='__main__':main()
