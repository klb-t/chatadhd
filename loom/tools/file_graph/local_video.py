"""Byte-only installed video host. Paths below are generated in the native private workspace."""
import base64
import hashlib
import json
import os
from pathlib import Path


def extract(raw, root, profile):
    from video_basic.video_basic import extract as decode, image_inputs
    work=Path.cwd()
    if str(work)!=os.environ.get('TMPDIR') or str(work)!=os.environ.get('HOME'):
        raise ValueError('video_private_workspace_required')
    for tool in ('/usr/bin/ffmpeg','/usr/bin/ffprobe','/usr/bin/setpriv'):
        if hashlib.sha256(Path(tool).read_bytes()).hexdigest()!=profile['runtime']['external_tools'][tool]:
            raise ValueError('video_tool_changed')
    original=hashlib.sha256(raw).hexdigest()
    (work/'original').write_bytes(raw)
    bundle=work/'frames'
    result=decode(work/'original',bundle,expected_sha256=original,ffprobe='/usr/bin/ffprobe',ffmpeg='/usr/bin/ffmpeg',host_context=(work,profile['runtime']))
    images=image_inputs(bundle,original_sha256=original,receipt_sha256=result['receipt_sha256'])
    receipt=(bundle/'receipt.json').read_bytes()
    return {'schema':'loom.file_graph.drafts/1','format':'video','original_sha256':original,'original_size':len(raw),
            'receipt_base64':base64.b64encode(receipt).decode(),'receipt_sha256':result['receipt_sha256'],
            'frames':[base64.b64encode(i.bytes).decode() for i in images],
            'policy':json.loads((root/'video-policy.json').read_text())}
