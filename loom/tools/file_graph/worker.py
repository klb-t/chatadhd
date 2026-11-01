"""Pinned format adapters -> common constructor drafts; no graph/IDs/filesystem extraction.
Executed only by the existing bounded native DOCX host, never from upload paths.
"""
import base64
import hashlib
import importlib
import json
from pathlib import Path
import resource
import sys

def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False)

def parse(raw, fmt, profile, root):
    from unknown_structure.contract import compile_descriptor
    descriptor = compile_descriptor(profile['descriptors'][fmt], profile['catalog'])
    policy = profile['parsers'][fmt]
    for key, value in policy.items():
        if key.startswith('max_') or key == 'read_chunk_bytes':
            if type(value) is not int or value <= 0: raise ValueError('positive_bound_required')
    structures, parts, chunks = [], [], []
    size = 0
    def structure(kind, title, parent, attrs=None):
        i=len(structures)
        if parent is not None and not 0 <= parent < i: raise ValueError('parent_invalid')
        structures.append({'kind':kind,'title':title,'parent':parent,'attrs':attrs or {}})
        return i
    def quote(value, kind, parent, attrs=None, span=None):
        nonlocal size
        if span is None:
            start=size; chunks.append(value+'\n');size+=len(value.encode())+1;span=[start,start+len(value.encode())]
        parts.append({'range':span,'kind':kind,'structure':parent,'attrs':attrs or {}})
    if fmt=='xlsx':
        from xlsx_basic.reader import read,column_name
        result=read(raw,limits=policy,expected_sha256=hashlib.sha256(raw).hexdigest())
        workbook=structure('workbook','',None)
        for sheet in result['sheets']:
            sid=structure('sheet',sheet['name'],workbook,{k:v for k,v in sheet.items() if k!='cells'})
            # Contiguous occupied row runs are derived ranges, NOT inferred Excel tables.
            groups=[]
            for cell in sheet['cells']:
                if not groups or cell['row']>groups[-1][-1]['row']+1: groups.append([])
                groups[-1].append(cell)
            for cells in groups:
                lo=min(c['column'] for c in cells);hi=max(c['column'] for c in cells)
                ref=f"{column_name(lo)}{cells[0]['row']}:{column_name(hi)}{cells[-1]['row']}"
                rid=structure('range',ref,sid,{'sheet':sheet['name'],'member':sheet['member'],'range':ref,'structure_evidence':'derived_populated_rows','not_declared_table':True})
                for cell in cells:
                    attrs={k:v for k,v in cell.items() if k not in ('text','value','formula')}
                    cid=structure('cell',cell['ref'],rid,attrs)
                    if cell['status']=='observed':quote(cell['text'],'cell',cid,{'original_locator':cell['locator'],'recognized_kind':'cell','evidence_class':'derived'})
        text=''.join(chunks)
    elif fmt=='odt':
        from odt_basic.reader import extract
        result=extract(raw,policy);text=result['text']
        for block in result['blocks']:
            sid=structure(block['kind'],'',block['parent'],{k:v for k,v in block.items() if k not in ['parent','byte_range']})
            quote('',block['kind'],sid,{'original_locator':block['raw_locator'],'recognized_kind':block['kind'],'evidence_class':'derived'},block['byte_range'])
    elif fmt=='epub':
        from epub_basic.reader import extract
        result=extract(raw,policy);text=result['text'];book=structure('publication','',None);chapters=[]
        for ch in result['chapters']:
            chapters.append(structure('chapter',ch['member'],book,ch))
        for i,block in enumerate(result['blocks']):
            sid=structure(block['kind'],'',chapters[block['chapter_index']],block)
            quote('',block['kind'],sid,{'original_locator':block['locator'],'recognized_kind':block['kind'],'evidence_class':'derived'},result['projection']['ranges'][i]['text_range'])
    elif fmt=='zip':
        # Same bounded full-stream CRC/path/alias validation as admitted EPUB.
        from epub_basic.reader import read_parts,safe_name
        import io,zipfile
        members=read_parts(raw,policy)
        with zipfile.ZipFile(io.BytesIO(raw)) as z:directories=[i.filename.rstrip('/') for i in z.infolist() if i.is_dir()]
        names=set(members)
        for name in list(names)+directories:
            pieces=name.split('/')
            directories.extend('/'.join(pieces[:n]) for n in range(1,len(pieces)))
        directories=sorted(set(directories),key=lambda v:(v.count('/'),v))
        if any(d in names for d in directories):raise ValueError('zip_file_folder_conflict')
        result={'directories':directories,'members':[{'name':name,'sha256':hashlib.sha256(data).hexdigest(),'size':len(data),'base64':base64.b64encode(data).decode()} for name,data in sorted(members.items())],'warnings':['archive_supported_members_only']}
        text=''
    else: raise ValueError('format_unavailable')
    if len(structures)+len(parts)>profile['max_nodes']:raise ValueError('node_bound')
    return {'schema':'loom.file_graph.drafts/1','format':fmt,'original_sha256':hashlib.sha256(raw).hexdigest(),'original_size':len(raw),
            'descriptor':descriptor.plan(),'descriptor_sha256':descriptor.sha256,'text':text,'structures':structures,'parts':parts,
            'extraction':result,'warnings':result['warnings']}

def main():
    try:
        job=json.loads(sys.argv[2]);profile=job['profile'];lim=profile['limits']
        resource.setrlimit(resource.RLIMIT_CPU,(lim['cpu_seconds'],lim['cpu_seconds']))
        resource.setrlimit(resource.RLIMIT_AS,(lim['memory_bytes'],lim['memory_bytes']))
        resource.setrlimit(resource.RLIMIT_CORE,(0,0))
        raw=sys.stdin.buffer.read(lim['max_input_bytes']+1)
        if len(raw)>lim['max_input_bytes']:raise ValueError('input_limit')
        root=Path(__file__).resolve().parent;sys.path.insert(0,str(root)) # pinned install only, -I -S
        result=parse(raw,job['format'],profile,root);encoded=canonical(result).encode()
        if len(encoded)+1>profile['max_output_bytes']:raise ValueError('output_limit')
        sys.stdout.buffer.write(encoded+b'\n');return 0
    except Exception:
        print('{"error":{"code":"file_graph_reader_refused"}}');return 2
if __name__=='__main__':raise SystemExit(main())
