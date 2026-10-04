#!/usr/bin/env python3
"""Reproduce the three 2026-10-04 packet audit findings, offline/synthetic.

Run against a library built at 3caa6b4 for the pre-fix behavior, or the current
branch for rejection. Prints observations; --expect turns them into a gate.
The old audit transcript was retained separately, not reconstructed by this file.
"""
import argparse
import copy
import ctypes
import hashlib
import json
import tempfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('library', type=Path)
parser.add_argument('--expect', choices=('accepted', 'rejected'))
args = parser.parse_args()
repo = Path(__file__).resolve().parents[4]
fixture = json.loads((repo / 'loom/src/packet/tests/reply-fixtures.json').read_text())['cases'][0]
lib = ctypes.CDLL(str(args.library.resolve()))
lib.loom_init_ex.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p)]
lib.loom_init_ex.restype = ctypes.c_void_p
lib.loom_packet.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.loom_packet.restype = ctypes.c_void_p
lib.loom_free_string.argtypes = [ctypes.c_void_p]
lib.loom_shutdown.argtypes = [ctypes.c_void_p]


def string_bytes(value):
    if isinstance(value, str):
        return len(value.encode('utf-8'))
    if isinstance(value, list):
        return sum(map(string_bytes, value))
    if isinstance(value, dict):
        return sum(string_bytes(k) + string_bytes(v) for k, v in value.items())
    return 0


with tempfile.TemporaryDirectory(prefix='packet-audit-') as tmp:
    error = ctypes.c_void_p()
    ctx = lib.loom_init_ex(json.dumps({'data_dir': tmp, 'start_workers': False}).encode(), ctypes.byref(error))
    if not ctx:
        raise RuntimeError('native context initialization failed')
    try:
        def invoke(request):
            pointer = lib.loom_packet(ctx, json.dumps(request, ensure_ascii=False).encode())
            if not pointer:
                raise RuntimeError('native command returned no JSON')
            try:
                return json.loads(ctypes.string_at(pointer))
            finally:
                lib.loom_free_string(pointer)

        def required(request):
            value = invoke(request)
            if 'error' in value:
                raise RuntimeError(value['error'])
            return value

        observations = []
        def record(case, value, **details):
            observations.append({'case': case, 'status': 'rejected' if 'error' in value else 'accepted',
                                 'error': value.get('error', {}).get('message'), **details})

        p = fixture['request']['packet']
        diff = fixture['expected']['diff']
        policy = {'schema': 'loom.graph_packet_apply_policy/1', 'acceptance': 'auto',
                  'allow_source_tombstones': False}
        applied = required({'operation': 'apply', 'packet': p, 'diff': diff, 'policy': policy})
        for field, value in {'accepted': False, 'candidate_packet_sha256': 'forged',
                             'canonical_store_written': True, 'acceptance_establishes_content_truth': True}.items():
            receipt = copy.deepcopy(applied['receipt'])
            receipt[field] = value
            # This synthetic fixture uses ordinary finite JSON numbers; the
            # sorted, compact UTF-8 spelling matches the pinned canonical codec.
            receipt.pop('receipt_sha256')
            canonical = json.dumps(receipt, sort_keys=True, separators=(',', ':'), ensure_ascii=False)
            receipt['receipt_sha256'] = hashlib.sha256(canonical.encode()).hexdigest()
            record('inverse.' + field, invoke({'operation': 'invert', 'packet': applied['packet'], 'receipt': receipt}))
        for cap in (300, 500, 1000, 2000):
            result = invoke({**fixture['request'], 'resource_limits': {'max_string_bytes': cap}})
            record('compile.max_string_bytes.' + str(cap), result,
                   generated_string_bytes=string_bytes(result) if 'error' not in result else None)
        request = {'operation': 'make', 'origin': diff['origin']}
        for name in ('definitions', 'entities', 'claims', 'sources'):
            request[name] = copy.deepcopy(diff[name]['add'])
        request['claims'][0]['assessment']['counter']['observations'] = ['missing-source']
        record('make.dangling_counter_observation', invoke(request))
        print(json.dumps({'schema': 'loom.packet_audit_observations/1', 'mode': 'offline synthetic',
                          'cases': observations}, indent=2))
        if args.expect and any(item['status'] != args.expect for item in observations):
            raise SystemExit('audit result differs from --expect ' + args.expect)
    finally:
        lib.loom_shutdown(ctx)
