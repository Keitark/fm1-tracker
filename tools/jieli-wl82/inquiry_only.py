"""One standard SCSI INQUIRY. No vendor commands or loader operations."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'jl-uboot-tool'))
from scsiio import SCSIDev

INQUIRY = bytes.fromhex('12 00 00 00 24 00')


def inspect(device, factory=SCSIDev):
    dev=factory(device)
    try:
        response=bytearray(36)
        residue=dev.execute(INQUIRY,None,response)
        if residue != 0 or len(response) != 36:
            raise IOError('Incomplete INQUIRY response')
        identity=tuple(bytes(response[a:b]).decode('ascii').strip()
                       for a,b in ((8,16),(16,32),(32,36)))
        if identity != ('WL82','UBOOT1.00','1.00'):
            raise ValueError(f'Unexpected device identity: {identity!r}')
        return {'status':'complete','vendor':identity[0],'product':identity[1],
                'adapter_info':getattr(dev,'adapter_info',None),
                'revision':identity[2],'commands_sent':1,'cdb_hex':INQUIRY.hex(),
                'response_bytes':len(response),'ram_uploaded':False,
                'vendor_commands_sent':False,'flash_commands_sent':False,
                'reset_requested':False,'chip_key_queried':False}
    finally:
        dev.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',required=True)
    parser.add_argument('--aligned-adapter',action='store_true')
    args=parser.parse_args()
    factory=SCSIDev
    if args.aligned_adapter:
        from fm1_scsi import SCSIDev as factory
    print(json.dumps(inspect(args.device,factory),indent=2))
