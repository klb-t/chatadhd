#!/usr/bin/env python3
"""Synthetic localhost packet HTTP check. Takes the built loom-server path."""
import json
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
import requests

repo=Path(__file__).resolve().parents[4]
server=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='loom-packet-http-') as tmp:
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(server),'--host','127.0.0.1','--port',str(port),'--data-dir',tmp,'--token','synthetic-local-token'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    base=f'http://127.0.0.1:{port}'
    h={'Authorization':'Bearer synthetic-local-token','Content-Type':'application/json'}
    try:
        deadline=time.monotonic()+20
        while True:
            if proc.poll() is not None:raise RuntimeError('server exited')
            try:
                if requests.get(base+'/api/healthz',headers=h,timeout=1).status_code==200:break
            except requests.RequestException:pass
            if time.monotonic()>deadline:raise RuntimeError('server startup timeout')
            time.sleep(.05)
        response=requests.post(base+'/api/packet',json={'operation':'capabilities'},timeout=5)
        assert response.status_code==401,response.text
        fixture=json.loads((repo/'loom/src/packet/tests/reply-fixtures.json').read_text())['cases'][0]
        before=requests.get(base+'/api/knowledge/runs',headers=h,timeout=5).json()
        response=requests.post(base+'/api/packet',data=json.dumps(fixture['request'],ensure_ascii=False).encode(),headers=h,timeout=20)
        assert response.status_code==200,response.text
        assert response.json()==fixture['expected']
        compilation=response.json()
        local_id=next(iter(compilation['node_ids']))
        fragment_request={'operation':'reply_fragment','packet':fixture['request']['packet'],
                          'compilation':compilation,'address':{'local_id':local_id}}
        response=requests.post(base+'/api/packet',json=fragment_request,headers=h,timeout=20)
        assert response.status_code==200,response.text
        assert response.json()['node_id']==compilation['node_ids'][local_id]
        assert response.json()['model_origin']['kind']=='model'
        response=requests.post(base+'/api/packet',json={**fragment_request,'address':{'node_id':'unknown'}},headers=h,timeout=20)
        assert response.status_code==400,response.text
        response=requests.post(base+'/api/packet',json={**fixture['request'],'resource_limits':{'max_string_bytes':300}},headers=h,timeout=20)
        assert response.status_code==400,response.text
        assert response.json()['error']['message']=='graph_packet_configured_resource_limit'
        assert response.json()['error']['raw_capture']==compilation['raw_capture']
        response=requests.post(base+'/api/packet',data='{"operation":"capture","raw":"a","raw":"b"}',headers=h,timeout=5)
        assert response.status_code==400,response.text
        response=requests.post(base+'/api/packet',data=b'{"operation":"capture","raw":"a"}\x00ignored',headers=h,timeout=5)
        assert response.status_code==400,response.text
        assert response.json()['error']['code']=='parse'
        response=requests.post(base+'/api/packet',json={'operation':'capture','raw':'a\x00b'},headers=h,timeout=5)
        assert response.status_code==200,response.text
        assert response.json()['byte_len']==3 and response.json()['raw_base64']=='YQBi'
        response=requests.post(base+'/api/packet',json={'operation':'validate','packet':{'invalid':True}},headers=h,timeout=5)
        assert response.status_code==400,response.text
        response=requests.post(base+'/api/packet',json={'operation':'compile_reply','packet':fixture['request']['packet'],'raw':'first invalid response','host':fixture['request']['host']},headers=h,timeout=5)
        assert response.status_code==400,response.text
        assert response.json()['error']['raw_capture']['byte_len']==len('first invalid response')
        after=requests.get(base+'/api/knowledge/runs',headers=h,timeout=5).json()
        assert before==after,(before,after)
        print('packet HTTP: 11/11 scenarios passed (auth, compilation, fragment, unknown fragment, configured output limit, duplicate JSON, literal NUL, escaped NUL capture, invalid packet, first capture, no knowledge writes)')
    finally:
        proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
