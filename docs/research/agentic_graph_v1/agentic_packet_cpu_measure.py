import hashlib,json,time
from pathlib import Path
from loom.tools.structure.agentic_graph_v1 import packet as c
from loom.tools.structure.agentic_graph_v1.test_packet import fixture,MODEL,AUTO
root=Path('/workspace/scratch/34e008d7a951/research-recovery')
protocol=json.loads((root/'agentic_packet_cpu_protocol.json').read_text())
source=Path(c.__file__).read_bytes()
assert hashlib.sha256(source).hexdigest()==protocol['source_sha256']
results=[];p=fixture();start=time.perf_counter()
for event_count in [0,5,20,50]:
 while len(p['history'])<event_count:
  p,_=c.apply_diff(p,c.empty_diff(p,proposal_id='cpu-'+str(len(p['history'])),origin=MODEL),AUTO)
 raw=c.encode_packet(p)
 construction=time.perf_counter()-start
 for replicate in range(3):
  order=['json_roundtrip','codec_validation'] if replicate%2==0 else ['codec_validation','json_roundtrip']
  for method in order:
   wall,cpu=time.perf_counter(),time.process_time()
   if method=='json_roundtrip':
    r=json.loads(json.dumps(p,ensure_ascii=False,sort_keys=True,separators=(',',':'),allow_nan=False));assert r==p
   else:c.validate_packet(p)
   results.append({'history_events':event_count,'packet_canonical_bytes':len(raw),'replicate':replicate+1,'method':method,'wall_seconds':time.perf_counter()-wall,'process_cpu_seconds':time.process_time()-cpu,'cumulative_construction_wall_seconds':construction})
 first={'schema':'loom.agentic_packet_cpu_measurement/1','protocol':protocol,'results':results,'decision':'investigate_scaling_not_rewrite_after_measurement','paid_cost_usd':0,'model_quality_measured':False,'code_modified':False}
 (root/'agentic_packet_cpu_first.json').write_text(json.dumps(first,indent=2)+'\n')
 print('measured_history_events='+str(event_count),flush=True)
print(json.dumps({'rows':len(results),'history_event_counts':[0,5,20,50],'elapsed_seconds':time.perf_counter()-start}))
