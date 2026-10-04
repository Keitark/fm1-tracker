"""Windows SCSI adapter with aligned buffers, sense data and strict bounds.

No command generation, retry, reset or loader selection is implemented here.
DeviceIoControl follows Microsoft's IOCTL_SCSI_PASS_THROUGH_DIRECT contract.
"""
import ctypes as ct
from ctypes import wintypes as wt
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'jl-uboot-tool'))
from scsiio.win32port import SCSIDev as BaseDevice, SCSI_PASS_THROUGH_DIRECT
from scsiio.common import SCSIException

QUERY_PROPERTY=0x2d1400
PASS_THROUGH=0x4d014
kernel=ct.WinDLL('kernel32',use_last_error=True)
ioctl=kernel.DeviceIoControl
ioctl.restype=wt.BOOL
ioctl.argtypes=[wt.HANDLE,wt.DWORD,ct.c_void_p,wt.DWORD,ct.c_void_p,wt.DWORD,
                ct.POINTER(wt.DWORD),ct.c_void_p]


class Packet(ct.Structure):
    _fields_=[('spt',SCSI_PASS_THROUGH_DIRECT),('sense',wt.BYTE*32)]


def aligned_buffer(size,mask):
    if not 1<=size<=512 or not 0<=mask<=65535 or mask&(mask+1):
        raise ValueError('Unsupported transfer size/alignment mask')
    # Page alignment also avoids crossing a page for our <=512-byte transfers.
    alignment_mask=max(mask,4095)
    owner=ct.create_string_buffer(size+alignment_mask)
    pointer=(ct.addressof(owner)+alignment_mask)&~alignment_mask
    return owner,pointer


class SCSIDev(BaseDevice):
    def __init__(self,path):
        super().__init__(path)
        try:
            # STORAGE_PROPERTY_QUERY: StorageAdapterProperty=1, standard=0.
            query=ct.create_string_buffer(struct.pack('<III',1,0,0),12)
            descriptor=ct.create_string_buffer(1024)
            returned=self._ioctl(QUERY_PROPERTY,query,descriptor)
            if returned<20:raise IOError('Short storage adapter descriptor')
            version,size,maximum,pages,mask=struct.unpack_from('<5I',descriptor.raw)
            if version<20 or size<20 or maximum<512 or mask>65535 or mask&(mask+1):
                raise IOError('Unsupported storage adapter requirements')
            self.adapter_info={'alignment_mask':mask,'buffer_alignment':max(mask+1,4096),
                               'maximum_transfer_bytes':maximum,'maximum_physical_pages':pages}
        except Exception:
            self.close();raise

    def _ioctl(self,code,source,destination):
        returned=wt.DWORD()
        success=ioctl(self.fhandle,code,ct.byref(source),ct.sizeof(source),
                      ct.byref(destination),ct.sizeof(destination),ct.byref(returned),None)
        if not success:raise ct.WinError(ct.get_last_error())
        return returned.value

    def execute(self,cdb,data_out,data_in,max_sense_len=32,return_sense_buffer=False):
        if not self.is_open:raise ValueError('Device is closed')
        if not 1<=len(cdb)<=16 or max_sense_len!=32:
            raise ValueError('Unsupported CDB/sense size')
        if (data_out is None)==(data_in is None):
            raise ValueError('Exactly one data direction is required')
        length=len(data_out) if data_out is not None else len(data_in)
        owner,pointer=aligned_buffer(length,self.adapter_info['alignment_mask'])
        if data_out is not None:ct.memmove(pointer,data_out,length)
        packet=Packet();spt=packet.spt
        spt.len=ct.sizeof(SCSI_PASS_THROUGH_DIRECT)
        # The port driver fills path/target/LUN; leave these zero.
        spt.cdb_len=len(cdb);spt.sense_len=32;spt.sense_off=Packet.sense.offset
        spt.dir=0 if data_out is not None else 1
        spt.data_len=length;spt.timeout=5
        spt.data_ptr=ct.cast(pointer,ct.POINTER(wt.BYTE))
        for index,value in enumerate(cdb):spt.cdb[index]=value
        try:
            returned=self._ioctl(PASS_THROUGH,packet,packet)
        except OSError as error:
            sense=bytes(packet.sense)[:min(spt.sense_len,32)].hex()
            raise SCSIException(f'Transfer failed: {error}; SCSI status=0x{spt.status:02x}; sense={sense}') from error
        if returned<ct.sizeof(SCSI_PASS_THROUGH_DIRECT):raise SCSIException('Short pass-through header')
        if spt.sense_len>32 or spt.data_len>length:raise SCSIException('Driver exceeded requested bounds')
        sense=bytes(packet.sense)[:spt.sense_len]
        if spt.status:
            raise SCSIException(f'SCSI status=0x{spt.status:02x}; sense={sense.hex()}')
        if data_in is not None:data_in[:spt.data_len]=ct.string_at(pointer,spt.data_len)
        residue=length-spt.data_len
        return (residue,sense) if return_sense_buffer else residue
