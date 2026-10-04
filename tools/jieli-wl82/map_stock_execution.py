"""Whole-image, byte-checked FM-1_010 disassembly index. Offline only.

Does NOT equate a linear sweep with executable code or recover mask ROM.
Direct-flow closure explores both sides of branches, assumes calls can return,
and explicitly stops at unresolved indirect jumps/table branches/unknown code.
Bootloader and application RAM are separate phases despite overlapping VMAs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from trace_stock_calls import ANALYSIS, APP_SHA
from audit_stock_bootloader import decode as decode_boot
from compare_stock_images import decode_application

BOOT_SHA='fcaf033c5e10526353ecb11279a7556ac8645026d370c0267912cb4bfa7c7e4d'
LINE=re.compile(r'^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2}\s+)+)\s+(.*)')
BRANCH=re.compile(r'\b(call|goto) (-?0x[0-9a-f]+)(?:\s|$)')
INDIRECT=re.compile(r'\b(call|goto) r\d+\b|\b(tb[bh]) \[?r\d+\b')
TABLES=(
 ('early',0x2083964,0x2083974,0x20047c2),
 ('platform',0x2083978,0x208397c,0x20047da),
 ('normal',0x208395c,0x2083964,0x2004e4e),
 ('module',0x208397c,0x2083980,0x2004e66),
 ('late',0x2083974,0x2083978,0x2004fbc),
)


def parse_listing(text,blob,base):
    rows={};covered=bytearray(len(blob));stack=[];blocks=[];last=None
    for line in text.splitlines():
        m=LINE.match(line)
        if not m:
            marker=line.strip()
            if marker.startswith('}') and stack:
                if marker.startswith('} else {'):
                    # ELSE changes the active arm; it does not close the IF.
                    # Popping here falsely attached later code to loops in data.
                    stack[-1]['then_end']=last
                    trailing=marker[len('} else {'):].strip()
                else:
                    block=stack.pop();block['end']=last;blocks.append(block)
                    trailing=marker[1:].strip()
                if trailing=='{' and last is not None:
                    # Objdump can put a REP's opening brace on the next line.
                    stack.append({'begin':last})
            continue
        address=int(m[1],16);code=bytes.fromhex(m[2]);insn=m[3].strip()
        off=address-base
        if not code or off<0 or off+len(code)>len(blob) or blob[off:off+len(code)]!=code:
            raise ValueError('Listing bytes differ from pinned image at '+hex(address))
        if address in rows or any(covered[off:off+len(code)]):
            raise ValueError('Overlapping disassembly row')
        covered[off:off+len(code)]=bytes([1])*len(code)
        branch=BRANCH.search(insn)
        row={'address':address,'size':len(code),'bytes':code.hex(),'instruction':insn,
             'direct_target':address+len(code)+int(branch[2],16) if branch else None,
             'transfer':branch[1] if branch else None,'line':line}
        rows[address]=row;last=address
        if insn.endswith('{') and not insn.startswith('{'):
            stack.append({'begin':address})
    # Predicated instruction bundles have a no-body path. Hardware repeats
    # have a body backedge; do not mistake braces in a register pop for blocks.
    malformed=[]
    for block in blocks:
        begin,end=block['begin'],block['end']
        if end is None:continue
        after=end+rows[end]['size']
        if rows[begin]['instruction'].startswith('rep '):
            repeat=re.match(r'rep (0x[0-9a-f]+)',rows[begin]['instruction'])
            if not repeat or after!=begin+rows[begin]['size']+int(repeat[1],16):
                malformed.append(begin)
                continue
            rows[end].setdefault('extra',[]).append(begin+rows[begin]['size'])
        elif 'then_end' in block:
            then_end=block['then_end']
            rows[begin].setdefault('extra',[]).append(then_end+rows[then_end]['size'])
            rows[then_end]['fallthrough_override']=after
        else:rows[begin].setdefault('extra',[]).append(after)
    gaps=[];start=None
    for off,used in enumerate(covered):
        if not used and start is None:start=off
        if used and start is not None:gaps.append([base+start,base+off]);start=None
    if start is not None:gaps.append([base+start,base+len(blob)])
    unclosed=[block['begin'] for block in stack]
    for address in unclosed:
        rows[address]['unclosed_printed_block']=True
    for address in malformed:rows[address]['malformed_printed_block']=True
    return rows,{'bytes':len(blob),'listed_bytes':sum(covered),'unlisted_ranges':gaps,
                 'rows':len(rows),'unclosed_printed_blocks':len(stack),
                 'unclosed_block_addresses':unclosed,
                 'malformed_repeat_blocks':malformed}


def successors(row):
    insn=row['instruction'];next_=row['address']+row['size']
    result=list(row.get('extra',[]));target=row['direct_target']
    if target is not None:result.append(target)
    conditional=insn.startswith('if')
    return_=bool(re.search(r'\brt[site]x?\b|\bpc\b.*= \[sp\+\+\]',insn))
    indirect=INDIRECT.search(insn)
    terminal=(return_ or insn.startswith('goto ') or
              (indirect and indirect[0].startswith(('goto','tbb','tbh'))) or
              '<unknown' in insn or '<invalid' in insn or '(bad)' in insn)
    if not terminal or conditional:result.append(row.get('fallthrough_override',next_))
    return list(dict.fromkeys(result))


def closure(rows,roots,resolutions=None,stop_at=()):
    resolutions=resolutions or {};seen=set();missing=set();todo=list(roots)
    stop_at=set(stop_at)
    while todo:
        address=todo.pop()
        if address in seen or address in missing:continue
        if address not in rows:missing.add(address);continue
        seen.add(address)
        if address in stop_at:continue
        todo.extend(successors(rows[address]))
        todo.extend(resolutions.get(address,[]))
    unresolved=[a for a in sorted(seen) if INDIRECT.search(rows[a]['instruction']) and a not in resolutions]
    return seen,{'root_addresses':list(roots),'potentially_reachable_rows':len(seen),
                 'unresolved_indirect_sites':unresolved,'external_or_nonboundary_targets':sorted(missing),
                 'reached_unknown_instructions':[a for a in sorted(seen) if '<unknown' in rows[a]['instruction']],
                 'reached_unclosed_blocks':[a for a in sorted(seen) if rows[a].get('unclosed_printed_block')],
                 'reached_malformed_blocks':[a for a in sorted(seen) if rows[a].get('malformed_printed_block')],
                 'stop_addresses':sorted(stop_at),'reached_stop_addresses':sorted(seen & stop_at),
                 'not_runtime_proof':True,'call_returns_assumed':True}


def u32(blob,address):
    offset=address-0x2000120
    if offset<0 or offset+4>len(blob):raise ValueError('Pointer outside application')
    return struct.unpack_from('<I',blob,offset)[0]


def callback_table(app,runtime,start,end,stride,pointer_offset,call,register,indirect_offset=None):
    """Only explicitly reviewed registries, never arbitrary pointer hits."""
    if runtime[call]['instruction']!='call '+register:
        raise ValueError('Registry caller changed')
    if (end-start)%stride:raise ValueError('Nonintegral registry size')
    entries=[]
    for entry in range(start,end,stride):
        pointer=u32(app,entry+pointer_offset)
        target=u32(app,pointer+indirect_offset) if indirect_offset is not None else pointer
        if target and target not in runtime:raise ValueError('Callback outside indexed code')
        entries.append({'entry':entry,'pointer':pointer,'target':target})
    return {'start':start,'end':end,'stride':stride,'pointer_offset':pointer_offset,
            'indirect_offset':indirect_offset,'callsite':call,'entries':entries}


def table_branch(blob,base,rows,site,count,width,register):
    """Reviewed bounded pi32v2 TBB/TBH, not an automatic table detector.

    Vendor clang emits entries as (label - table_start)/2 for both widths;
    TBH's index register is already scaled by two. See stock audit evidence.
    The caller must prove the bound/count from the instructions before site.
    """
    if width not in (1,2) or count<=0:
        raise ValueError('Invalid reviewed table extent')
    kind='tbb' if width==1 else 'tbh'
    row=rows.get(site,{})
    if row.get('instruction')!=f'{kind} [{register}]' or row.get('size')!=2:
        raise ValueError('Table branch caller changed')
    start=site+2;offset=start-base;end=start+count*width
    if offset<0 or offset+count*width>len(blob):
        raise ValueError('Table outside pinned payload')
    values=struct.unpack_from('<'+str(count)+('B' if width==1 else 'H'),blob,offset)
    targets=[start+2*value for value in values]
    if any(a<end or a not in rows or '<unknown' in rows[a]['instruction'] for a in targets):
        raise ValueError('Table target outside indexed code or inside table')
    return {'site':site,'table_start':start,'table_end':end,'count':count,
            'entry_bytes':width,'entries':list(values),'targets':targets,
            'bound_manually_reviewed':True,'table_bytes_are_not_instructions':True}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=ANALYSIS/'execution-map-20260928')
    args=ap.parse_args()
    app=(ANALYSIS/'app-FM-1_010.bin').read_bytes()
    boot=(ANALYSIS/'bootloader/stock-boot-code.bin').read_bytes()
    if hashlib.sha256(app).hexdigest()!=APP_SHA or hashlib.sha256(boot).hexdigest()!=BOOT_SHA:
        raise ValueError('Unreviewed stock code')
    # Re-derive executable payloads from the original CRC/hash-checked backup.
    # Do not extract configuration, identity/key or persistent reserved regions.
    raw=(ANALYSIS.parent/'fm1-flash-1MiB.bin').read_bytes()
    checked_boot,boot_evidence=decode_boot(raw)
    checked_app,app_evidence,layout=decode_application(raw)
    if checked_boot!=boot or checked_app!=app or not app_evidence['data_crc_valid']:
        raise ValueError('Analysis payloads do not match original stock backup')
    ram=app[0x20848e0-0x2000120:0x20848e0-0x2000120+0x9e5c]
    cache_data=app[0x208e73c-0x2000120:]
    if len(cache_data)!=32:raise ValueError('Cache-RAM initializer extent changed')
    areas={};summary={};inputs={}
    for name,blob,base,file in (
        ('application',app,0x2000120,'app-linear-sweep.asm'),
        ('runtime-ram',ram,0x1c00000,'ram-copy-linear-sweep.asm'),
        ('bootloader',boot,0x1c02000,'bootloader/stock-boot-linear-sweep.asm')):
        text=(ANALYSIS/file).read_text()
        areas[name],summary[name]=parse_listing(text,blob,base)
        inputs[name]={'binary_sha256':hashlib.sha256(blob).hexdigest(),
                      'listing_sha256':hashlib.sha256((ANALYSIS/file).read_bytes()).hexdigest()}
    runtime={**areas['application'],**areas['runtime-ram']}
    resolutions={};table_report=[]
    for name,start,end,call in TABLES:
        targets=[u32(app,a) for a in range(start,end,4)]
        if runtime[call]['instruction']!='call r0' or any(a not in runtime for a in targets):
            raise ValueError('Reviewed initialization table/callsite mismatch')
        resolutions[call]=targets
        table_report.append({'group':name,'start':start,'end':end,'callsite':call,'targets':targets})
    devices=[]
    for entry in range(0x20839fc,0x2083a14,12):
        name,ops,data=(u32(app,entry+i) for i in (0,4,8))
        offset=name-0x2000120;end=app.index(0,offset,offset+32)
        name=app[offset:end].decode('ascii');init=u32(app,ops+4)
        if init not in runtime:raise ValueError('Device initializer outside disassembly')
        devices.append({'entry':entry,'name':name,'ops':ops,'platform_data':data,'init':init})
    if runtime[0x20047a0]['instruction']!='call r2':raise ValueError('Device table caller changed')
    resolutions[0x20047a0]=[d['init'] for d in devices]
    registries=[
        {'name':'audio_subdevices',**callback_table(app,runtime,0x2083ad4,0x2083b1c,12,8,0x203b4f6,'r1',0)},
        {'name':'configuration_backends',**callback_table(app,runtime,0x2083998,0x20839ec,28,0,0x2032ae4,'r0')},
    ]
    for name,offset,site,register in (
        ('configuration_read_predicate',4,0x20027a4,'r1'),
        ('configuration_read',8,0x20027b4,'r3'),
        ('configuration_write_predicate',4,0x20033f8,'r1'),
        ('configuration_write',12,0x2003410,'r3'),
    ):
        registries.append({'name':name,**callback_table(
            app,runtime,0x2083998,0x20839ec,28,offset,site,register)})
    for registry in registries:
        resolutions[registry['callsite']]=[e['target'] for e in registry['entries'] if e['target']]
    switches=[]
    for name,site,count,width,register in (
        ('flash_read_mode',0x1c0035a,8,1,'r0'),
        ('format_character',0x2002e28,22,2,'r2'),
        ('format_integer_type',0x2003040,21,2,'r1'),
        ('flash_control_200_206',0x201d4bc,7,1,'r0'),
        ('flash_control_100_103',0x201d4d4,4,1,'r0'),
        ('flash_erase_200_204',0x1c00666,5,1,'r2'),
    ):
        in_ram=site<0x2000120
        item={'name':name,**table_branch(ram if in_ram else app,
            0x1c00000 if in_ram else 0x2000120,runtime,site,count,width,register)}
        switches.append(item)
        resolutions[site]=list(dict.fromkeys(item['targets']))
    # Explicitly reviewed handoffs/registrations, not arbitrary pointer hits.
    resolutions[0x20001b6]=[0x2001c24]  # startup goto r6, checked below
    if runtime[0x20001b6]['instruction']!='goto r6':
        raise ValueError('Startup main transfer changed')
    # OS task entry pointers are passed/stored, not ordinary immediate calls.
    # Additional roots are labelled registrations; do not fake direct edges.
    roots=[0x2000120,0x2004734,0x2022e90]
    reached,graph=closure(runtime,roots,resolutions)
    boot_rows=areas['bootloader']
    if boot_rows[0x1c0202a]['bytes']!='c4ff3620c001' or boot_rows[0x1c02034]['instruction']!='rti':
        raise ValueError('Bootloader RETI entry bridge changed')
    boot_reached,boot_graph=closure(boot_rows,[0x1c02000],{0x1c02034:[0x1c02036]})
    args.out.mkdir(parents=True,exist_ok=True)
    def save(name,obj):(args.out/name).write_text(json.dumps(obj,indent=2)+'\n')
    save('init-tables.json',table_report);save('device-init.json',devices)
    save('cache-ram-data.json',{'source':0x208e73c,'destination':0x1f28000,
         'bytes':32,'sha256':hashlib.sha256(cache_data).hexdigest(),
         'words':[{'storage_address':0x1f28000+i,'initial_value':u32(app,0x208e73c+i)}
                  for i in range(0,32,4)],
         'classification':'Initialized state and function pointers, not a proven executable section'})
    save('nested-callback-tables.json',registries)
    save('reviewed-table-branches.json',switches)
    save('application-flow.json',graph);save('bootloader-flow.json',boot_graph)
    phases={}
    for name,entry,stop in (
        ('application_entry_to_os_start',0x2000120,0x2002070),
        ('app_task_before_ui_setup_call',0x2004734,0x2005184),
        ('ui_setup_to_task_registration',0x200429e,0x2004688),
        ('ui_task_to_lcd_spi_setup',0x2022e90,0x2023086),
    ):
        _,phases[name]=closure(runtime,[entry],resolutions,[stop])
    save('boot-phase-frontiers.json',phases)
    # Potential code pointers are an exhaustive aligned scan of the application,
    # not a claim that every hit is a callback (numeric data can look identical).
    pointers=[]
    for offset in range(0,len(app)-3,4):
        target=struct.unpack_from('<I',app,offset)[0]
        if target in runtime:
            pointers.append({'storage_address':0x2000120+offset,'target':target,
                             'candidate_only':True})
    save('application-code-pointer-candidates.json',pointers)
    interrupt_sites=[]
    ordered=sorted(runtime)
    for i,address in enumerate(ordered):
        row=runtime[address]
        if row['transfer']=='call' and row['direct_target']==0x20017f2:
            interrupt_sites.append({'site':address,'in_root_closure':address in reached,
                'preceding_instructions':[runtime[a]['line'] for a in ordered[max(0,i-16):i+1]],
                'arguments_require_manual_dataflow_review':True})
    save('interrupt-registration-sites.json',interrupt_sites)
    frontier=[]
    for address in graph['unresolved_indirect_sites']:
        i=ordered.index(address)
        frontier.append({'site':address,'instruction':runtime[address]['instruction'],
            'preceding_instructions':[runtime[a]['line'] for a in ordered[max(0,i-8):i+1]]})
    save('unresolved-application-frontier.json',frontier)
    entrypoints=set(roots)
    for targets in resolutions.values():entrypoints.update(targets)
    entrypoints.update(r['direct_target'] for r in runtime.values()
                      if r['transfer']=='call' and r['direct_target'] in runtime)
    save('function-entry-candidates.json',[
        {'address':a,'in_root_closure':a in reached,'candidate_only':True,
         'first_instruction':runtime[a]['instruction']} for a in sorted(entrypoints)])
    for name,rows in areas.items():
        live=boot_reached if name=='bootloader' else reached
        calls=[];indirect=[];literals=[];lines=[]
        for addr,row in rows.items():
            suffix=[]
            if row['direct_target'] is not None:
                suffix.append(f"{row['transfer']} -> {row['direct_target']:#010x}")
                if row['transfer']=='call':calls.append({'site':addr,'target':row['direct_target'],'in_root_closure':addr in live})
            if INDIRECT.search(row['instruction']):
                indirect.append({'site':addr,'instruction':row['instruction'],
                    'in_root_closure':addr in live,'reviewed_targets':resolutions.get(addr,[]) if name!='bootloader' else []})
            literal=re.search(r'\br\d+ = (0x[0-9a-f]+)\b',row['instruction'])
            if literal:
                value=int(literal[1],16)
                if 0x10000<=value<0x60000:
                    literals.append({'site':addr,'value':value,'instruction':row['instruction'],
                                     'in_root_closure':addr in live,'not_proven_mmio_use':True})
            if addr in live:suffix.append('potential root closure; not observed execution')
            lines.append(row['line']+(' ; '+'; '.join(suffix) if suffix else ''))
        (args.out/(name+'-indexed.asm')).write_text('\n'.join(lines)+'\n')
        save(name+'-direct-calls.json',calls);save(name+'-indirect-sites.json',indirect)
        save(name+'-mmio-literal-candidates.json',literals)
        summary[name].update({'direct_call_sites':len(calls),'indirect_sites':len(indirect),
                             'unknown_instruction_rows':sum('<unknown' in r['instruction'] for r in rows.values()),
                             'mmio_literal_candidates':len(literals)})
    report={'stock_app_sha256':APP_SHA,'inputs':inputs,'coverage':summary,
            'original_backup_sha256':hashlib.sha256(raw).hexdigest(),
            'decoded_payloads_match_backup':True,'boot_crc_valid':boot_evidence['header_and_data_crc_valid'],
            'application_crc_valid':app_evidence['data_crc_valid'],
            'unique_decoded_payload_bytes':len(app)+len(boot),
            'runtime_ram_is_duplicate_view_of_application':True,
            'cache_ram_initialized_data_bytes':32,
            'nested_callback_registries':registries,
            'reviewed_table_branches':switches,
            'code_pointer_candidates':len(pointers),'interrupt_registration_candidates':len(interrupt_sites),
            'init_table_groups':len(table_report),'device_initializers':devices,
            'root_notes':{'0x02000120':'application entry',
                          '0x02004734':'app-task pointer at 0x02002050',
                          '0x02022e90':'UI-task pointer at 0x02004676; stored to stack at 0x02004680'},
            'limitations':['Linear disassembly includes data and is not function recovery',
                'Mask ROM is not present in the flash image',
                'Indirect calls, jump tables and callback order remain partly unresolved',
                'Runtime RAM has its own address namespace; bootloader RAM is a separate phase',
                'MMIO-sized literals are candidates, not established register writes',
                'Unknown instruction rows and unclosed printed blocks are retained, not silently discarded',
                'Registered tasks/interrupts/timers beyond the three named roots remain incomplete',
                'Semantic equivalence and required-process completeness are NOT established'],
            'required_process_audit_complete':False,'device_operations_performed':False}
    save('manifest.json',report)
    print(json.dumps({'output':str(args.out),'coverage':{
        name:{k:v for k,v in item.items() if k not in ('unclosed_block_addresses','malformed_repeat_blocks')} for name,item in summary.items()},
                      'application_closure_rows':len(reached),
                      'application_unresolved_indirect':len(graph['unresolved_indirect_sites']),
                      'bootloader_closure_rows':len(boot_reached),
                      'required_process_audit_complete':False},indent=2))


if __name__=='__main__':main()
