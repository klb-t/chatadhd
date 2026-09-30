#!/usr/bin/env python3
"""Offline native import fidelity, aggregate output only; input/runtime stay private."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import time


def sha(data):
    return hashlib.sha256(data).hexdigest()


def atoms(value, path=()):
    # Exact types distinguish bool/int and preserve empty arrays/objects.
    if isinstance(value, dict) and value:
        return {p: v for k, x in value.items() for p, v in atoms(x, path + (k,)).items()}
    if isinstance(value, list) and value:
        return {p: v for i, x in enumerate(value) for p, v in atoms(x, path + (i,)).items()}
    return {path: (type(value).__name__, value)}


def pointer(value, ptr):
    if ptr == '':
        return value
    if not ptr.startswith('/'):
        raise ValueError('not JSON Pointer')
    for s in ptr[1:].split('/'):
        for i, c in enumerate(s):
            if c == '~' and (i + 1 == len(s) or s[i + 1] not in '01'):
                raise ValueError('invalid JSON Pointer escape')
        s = s.replace('~1', '/').replace('~0', '~')
        if isinstance(value, list):
            if not s or any(c < '0' or c > '9' for c in s) or (len(s) > 1 and s[0] == '0'):
                raise ValueError('noncanonical JSON Pointer array index')
            value = value[int(s)]
        else:
            value = value[s]
    return value


def audit(cli, source, runtime, out):
    raw = source.read_bytes()
    obj = json.loads(raw)
    if not isinstance(obj, list):
        raise ValueError('this protocol accepts a conversation array only')
    before = atoms(obj)
    manifest = {'source_sha256': sha(raw), 'source_bytes': len(raw),
                'instrument_sha256': sha(Path(__file__).read_bytes()),
                'cli_sha256': sha(cli.read_bytes()), 'conversations_expected': len(obj),
                'scalar_leaves_expected': sum(not isinstance(v[1], (dict, list)) for v in before.values()),
                'empty_containers_expected': sum(isinstance(v[1], (dict, list)) for v in before.values()),
                'network_mode': 'no credentials; semantic disabled; background workers off in CLI',
                'scope': 'one availability-selected owner export shard; not representative or whole archive'}
    out.mkdir(parents=True, exist_ok=False)
    (out / 'real-export-before.json').write_text(json.dumps(manifest, indent=2)+'\n')
    runtime.mkdir(parents=True, mode=0o700, exist_ok=False)
    (runtime / 'config.json').write_text(json.dumps({'semantic_analysis':False,'semantic_model':'','base_url':'http://127.0.0.1:1','stream':False}))
    (runtime / 'config.json').chmod(0o600)
    started = time.monotonic()
    def command(*args):
        r = subprocess.run([str(cli),'--data-dir',str(runtime),'--json','--quiet',*args],capture_output=True,timeout=180)
        # No source text or runtime paths are exposed in public evidence.
        (runtime / ('call-'+str(time.time_ns())+'.stdout')).write_bytes(r.stdout)
        (runtime / ('call-'+str(time.time_ns())+'.stderr')).write_bytes(r.stderr)
        return r
    first = command('import',str(source),'--export-mode','on')
    report = dict(manifest, first_returncode=first.returncode, first_stdout_sha256=sha(first.stdout))
    db = sqlite3.connect(runtime / 'chatadhd.db')
    db.row_factory=sqlite3.Row
    cs=list(db.execute('select * from conversations order by rowid'))
    ms=list(db.execute('select * from messages order by rowid'))
    reconstructed=[None]*len(obj)
    raw_ok=raw_total=0
    statuses={}
    for c in cs:
        ce=json.loads(c['metadata']).get('export',{})
        if not ce:
            continue
        index=ce['index']
        restored=ce['fields'].copy()
        rows=[m for m in ms if m['conv_id']==c['id']]
        if 'mapping' in obj[index]:
            mapping={}
            for m in rows:
                ex=json.loads(m['metadata'])['export']
                node=ex['node'].copy()
                node['message']=ex['raw']
                mapping[ex['key']]=node
                raw_total+=1
                raw_ok+=atoms(ex['raw'])==atoms(obj[index]['mapping'][ex['key']]['message'])
            for n in ce.get('null_nodes',[]):
                mapping[n['id']]=n['node']
            if ce.get('has_mapping'):
                restored['mapping']=mapping
        else:
            arr=[json.loads(m['metadata'])['export']['raw'] for m in rows]
            if ce.get('has_mapping'):
                restored['chat_messages']=arr
            for a,b in zip(arr,obj[index].get('chat_messages',[])):
                raw_total+=1
                raw_ok+=atoms(a)==atoms(b)
        reconstructed[index]=restored
        for m in rows:
            statuses[m['status']]=statuses.get(m['status'],0)+1
    after=atoms(reconstructed)
    matching=sum(after.get(k)==v for k,v in before.items())
    provs=list(db.execute('select * from loom_provenance where subject_kind = ?',('message',)))
    locator_present=locator_resolved=locator_equal=0
    by_id={m['id']:m for m in ms}
    for p in provs:
        loc=json.loads(p['locator'])
        ptr=loc.get('json_pointer')
        if isinstance(ptr,str):
            locator_present+=1
            try:
                value=pointer(obj,ptr)
                locator_resolved+=1
                ex=json.loads(by_id[p['subject_id']]['metadata'])['export']
                locator_equal+=atoms(value)==atoms(ex['raw'])
            except (KeyError,IndexError,ValueError,TypeError):
                pass
    source_blob=runtime/'blobs'/manifest['source_sha256'][:2]/manifest['source_sha256'][2:4]/manifest['source_sha256']
    snapshot=[tuple(x) for x in db.execute('select id,metadata from messages order by rowid')]
    counts=(len(cs),len(ms))
    db.close()
    reopen=command('conv','list','--limit',str(max(1,len(cs))))
    second=command('import',str(source),'--export-mode','on')
    db=sqlite3.connect(runtime/'chatadhd.db')
    counts2=tuple(db.execute('select (select count(*) from conversations),(select count(*) from messages)').fetchone())
    snap2=list(db.execute('select id,metadata from messages order by rowid'))
    db.close()
    report.update(conversations_imported=len(cs),messages_imported=len(ms),message_status_counts=statuses,
        raw_messages_equal=raw_ok,raw_messages_checked=raw_total,
        exact_conversations=sum(atoms(a)==atoms(b) for a,b in zip(obj,reconstructed)),
        atoms_expected=len(before),atoms_preserved=matching,atoms_missing=sum(k not in after for k in before),
        atoms_changed=sum(k in after and after[k]!=v for k,v in before.items()),atoms_added=sum(k not in before for k in after),
        raw_source_blob_equal=source_blob.exists() and source_blob.read_bytes()==raw,
        message_provenance_rows=len(provs),message_json_pointers=locator_present,
        message_pointers_resolved=locator_resolved,message_pointers_equal_raw=locator_equal,
        reopen_returncode=reopen.returncode,reimport_returncode=second.returncode,
        reimport_counts_unchanged=counts==counts2,reimport_message_metadata_unchanged=snapshot==snap2,
        elapsed_seconds=round(time.monotonic()-started,3),
        attachments='references preserved as JSON; asset bytes not included in this shard audit',
        semantic_quality='not evaluated',provider_requests='not performed')
    (out/'real-export-first-result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--cli',type=Path,required=True)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    audit(a.cli.resolve(),a.source.resolve(),a.runtime.resolve(),a.out.resolve())
