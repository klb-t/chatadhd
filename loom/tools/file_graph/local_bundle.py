"""Byte-only bridge for existing exporter/ZIP importer, no user filesystem paths."""
import base64
import hashlib
import json
import os
from pathlib import Path


def transform(raw, fmt, root):
    from conversation_bundle.bundle import create_bundle, stage_bundle, preset
    work=Path.cwd()
    if str(work)!=os.environ.get('TMPDIR') or str(work)!=os.environ.get('HOME'):raise ValueError('private_workspace_required')
    limits,formats=preset()
    result={'schema':'loom.file_graph.drafts/1','format':fmt,'original_sha256':hashlib.sha256(raw).hexdigest(),'original_size':len(raw)}
    if fmt=='bundle_export':
        job=json.loads(raw);blobroot=work/'blobs';blobroot.mkdir()
        for digest,data in job['originals'].items():
            b=base64.b64decode(data,validate=True)
            if hashlib.sha256(b).hexdigest()!=digest:raise ValueError('original_hash_mismatch')
            p=blobroot/digest[:2]/digest[2:4]/digest;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
        exports={'json':base64.b64decode(job['snapshot'],validate=True)}
        outcome=create_bundle(work/'bundle.zip',exports,job['bindings'],blobroot,limits=limits,formats=formats,allow_incomplete=job['allow_incomplete'])
        result.update(outcome);result['base64']=base64.b64encode((work/'bundle.zip').read_bytes()).decode()
    else:
        archive=work/'bundle.zip';archive.write_bytes(raw)
        # Incomplete input remains visible; the native caller must explicitly opt in.
        with stage_bundle(archive,staging_parent=work,limits=limits,formats=formats,expected_sha256=result['original_sha256'],require_complete=False) as plan:
            result['manifest']=plan.manifest;result['manifest_sha256']=plan.manifest_sha256
            result['snapshot']=json.loads((plan.members/'conversation.json').read_bytes())
            result['originals']={o['sha256']:base64.b64encode((plan.members/o['member']).read_bytes()).decode() for o in plan.manifest['originals']}
    return result
