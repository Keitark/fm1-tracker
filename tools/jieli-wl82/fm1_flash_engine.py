"""Verified sector executor. Protocol is injected; no direct USB imports.

This code implements writes but is not a claim of hardware qualification.
Every ambiguity poisons this executor: no retry, reset or automatic restore.
"""
import time
from fm1_deployment import SIZE,SECTOR,PAGE,require,sha,restore_scope


class Executor:
    def __init__(self,protocol,deployment,journal,clock=time.monotonic,sleep=time.sleep):
        deployment.validate()
        self.p=protocol;self.d=deployment;self.log=journal
        self.clock=clock;self.sleep=sleep
        self.used=False;self.mutations_attempted=0;self.status_mask=None
        self.progress=None

    def ready(self,timeout=30.0):
        deadline=self.clock()+timeout
        count=0;busy=False
        while True:
            if self.clock()>=deadline:raise TimeoutError('NOR ready deadline exceeded')
            value=self.p.status();count+=1
            if not isinstance(value,int) or not 0<=value<=255:raise IOError('Invalid SR1')
            if self.clock()>=deadline:raise TimeoutError('NOR status response exceeded deadline')
            if self.status_mask is not None and value&0xfc!=self.status_mask:
                raise IOError('NOR non-busy status bits changed')
            if not value&1:
                if value&2:raise IOError('NOR WEL remains set while ready')
                if self.status_mask is None:self.status_mask=value&0xfc
                return {'sr1':value,'polls':count,'busy_seen':busy}
            busy=True;self.sleep(0.01)

    def read(self,address,length):
        require(address>=0 and length>0 and address+length<=SIZE,'Invalid read range')
        result=bytearray()
        for start in range(address,address+length,PAGE):
            size=min(PAGE,address+length-start)
            data=self.p.read(start,size)
            if len(data)!=size:raise IOError(f'Short read at 0x{start:x}')
            result.extend(data)
        return bytes(result)

    def full_read(self,label,expected=None):
        self.ready()
        result=bytearray()
        self.progress={'stage':label,'matched_bytes':0}
        for offset in range(0,SIZE,PAGE):
            data=self.read(offset,PAGE)
            if expected is not None and data!=expected[offset:offset+PAGE]:
                raise IOError(f'{label}: image mismatch at 0x{offset:x}')
            result.extend(data)
            self.progress['matched_bytes']=offset+PAGE
            if (offset+PAGE)%0x20000==0:
                print(f'{label}: {offset+PAGE}/{SIZE}',flush=True)
        status=self.ready()
        self.log({'event':'full_read_verified','stage':label,'sha256':sha(result),'status':status})
        return bytes(result)

    def mutate(self,kind,address,data=None):
        require(address//SECTOR*SECTOR in self.d.order,'Mutation outside NES sectors')
        require(kind in ('erase','program'),'Forbidden mutation')
        if kind=='erase':require(address%SECTOR==0 and data is None,'Bad erase geometry')
        else:require(address%PAGE==0 and type(data) is bytes and len(data)==PAGE,'Bad page geometry')
        before=self.ready()
        event={'event':'mutation_intent','kind':kind,'address':hex(address),
               'bytes':SECTOR if data is None else PAGE,'ready_before':before}
        if data is not None:event['payload_sha256']=sha(data)
        # Persist intent BEFORE touching flash. Failure here prevents mutation.
        self.log(event)
        self.mutations_attempted+=1
        if kind=='erase':self.p.erase(address)
        else:self.p.program(address,data)
        after=self.ready()
        expected=bytes([255])*SECTOR if data is None else data
        if self.read(address,len(expected))!=expected:
            raise IOError(f'{kind} readback mismatch at 0x{address:x}')
        self.log({'event':'mutation_verified','kind':kind,'address':hex(address),
                  'ready_after':after,'sha256':sha(expected)})

    def run(self,mode):
        require(not self.used,'Executor already consumed; no retry or mode switch')
        self.used=True
        require(mode in ('qualify','write','restore'),'Unknown mode')
        try:
            self.log({'event':'session_start','mode':mode,'profile':self.d.profile,
                      'baseline_sha256':sha(self.d.baseline),'target_sha256':sha(self.d.target)})
            expected=None if mode=='restore' else self.d.baseline
            first=self.full_read('preflight_1',expected)
            self.full_read('preflight_2',first)
            if mode=='restore':restore_scope(first,self.d)
            self.log({'event':'read_preflight_passed','write_completion_hardware_proven':False})
            if mode=='qualify':return {'status':'read_preflight_passed','mutations_attempted':0}
            target=self.d.baseline if mode=='restore' else self.d.target
            for sector in self.d.order:
                old=first[sector:sector+SECTOR];new=target[sector:sector+SECTOR]
                if old==new:continue
                self.progress={'stage':'sector','address':hex(sector)}
                self.ready()
                if self.read(sector,SECTOR)!=old:
                    raise IOError(f'Sector preimage changed at 0x{sector:x}')
                self.mutate('erase',sector)
                for offset in range(sector,sector+SECTOR,PAGE):
                    self.mutate('program',offset,target[offset:offset+PAGE])
                self.ready()
                if self.read(sector,SECTOR)!=new:raise IOError(f'Sector mismatch at 0x{sector:x}')
                self.log({'event':'sector_verified','address':hex(sector),'sha256':sha(new)})
                print(f'Sector verified: 0x{sector:x}',flush=True)
            self.full_read('final_1',target)
            self.full_read('final_2',target)
            result={'status':mode+'_readback_verified','sha256':sha(target),
                    'mutations_attempted':self.mutations_attempted,'boot_verified':False,
                    'reset_requested':False}
            self.log({'event':'complete',**result})
            return result
        except Exception as error:
            # Logging failure must not trigger any further device operation.
            self.log({'event':'stopped','error':str(error),
                      'mutations_attempted':self.mutations_attempted,'progress':self.progress,
                      'automatic_retry_reset_or_restore':False})
            raise
