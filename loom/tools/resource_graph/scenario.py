"""Safe synthetic end-to-end demonstration; no user data, network or model calls."""
import hashlib
import io
import json
from pathlib import Path
import zipfile
from .core import ResourceGraph, SyntaxAdapter
from .mapping_adapter import MappingAdapter
from .projection import entity, ident, make_packet, relation
from .discovery import export_discovery_packet


def _write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def run(output: Path, *, library=None):
    output.mkdir(parents=True, exist_ok=True)
    source = output / 'synthetic-profile.json'
    value = {'temperature':0.25, 'future':{'records':[{'ticket':'a','payload':{'answer':42}}, {'ticket':'b','payload':{'answer':43}}]}}
    _write_json(source,value)
    before = hashlib.sha256(source.read_bytes()).hexdigest()
    inner=io.BytesIO()
    with zipfile.ZipFile(inner,'w') as z: z.writestr('profile.json',source.read_bytes())
    archive=output/'synthetic-nested.zip'
    with zipfile.ZipFile(archive,'w') as z: z.writestr('inner.zip',inner.getvalue())
    graph=ResourceGraph()
    graph.attach(source,logical_id='demo-profile')
    graph.attach(archive,logical_id='demo-nested',members=['inner.zip','profile.json'])
    reference = graph.reference_packet('demo-profile')
    assert reference['task']['materialized_fields']==0 and graph.metrics['opens']==0
    assert graph.select('demo-nested','/temperature')==graph.select('demo-profile','/temperature')
    packet=graph.project('demo-profile','/future',depth=4)
    _write_json(output/'example-packet-for-E.json',packet)
    discovery=graph.discover('demo-profile')
    _write_json(output/'discovery.json',discovery)
    _write_json(output/'discovery-packet.json',export_discovery_packet(discovery,observed_on='2026-10-09'))
    candidates=[row for row in discovery['alternatives'] if row.get('mapping')]
    mapping=candidates[0]['mapping']
    graph.register_adapter('new-data-mapping',MappingAdapter(SyntaxAdapter(),mapping))
    graph.attach(source,logical_id='demo-mapped',adapter='new-data-mapping')
    mapped=graph.project('demo-mapped',depth=2)
    _write_json(output/'mapped-packet.json',mapped)
    # The existing public configuration consumer reads this same source path.
    # This is a headless read, not activation of a native runtime or B's hook.
    from engine.config import Config
    consumer=Config(source)
    effective=consumer.get('temperature')
    assert effective==graph.select('demo-profile','/temperature')
    assert hashlib.sha256(source.read_bytes()).hexdigest()==before
    setting=graph.project('demo-profile','/temperature',depth=0)
    field=next(row for row in setting['entities'] if row['kind']=='resource_field')
    setting_id, consumer_id=ident('demo-setting'),ident('engine.config.Config')
    setting=make_packet(setting['entities']+[
        entity(setting_id,'setting','temperature',{'effective_value':effective,'source_selector':'/temperature'}),
        entity(consumer_id,'consumer','engine.config.Config',{'executed':'get','native_runtime_connected':False})],
        setting['claims']+[
        relation(field['id'],'configures',setting_id,version='1'),
        relation(setting_id,'consumed_by',consumer_id,version='1')],setting['sources'],task={'operation':'profile_consumer_read','native_runtime_connected':False})
    _write_json(output/'profile-consumer-packet.json',setting)
    result={'fixture':'generated_synthetic','user_data':False,'source_unchanged':True,
            'reference_without_io':True,'nested_zip_equal':True,'profile_consumer_read':effective,
            'runtime_hook_connected':False,'new_mapping_adapter_projected':True,
            'native':{'native_executed':False,'reason':'library_not_requested'},
            'materialized_fields':packet['task']['materialized_fields']}
    if library:
        from .native import roundtrip
        native=roundtrip(packet,library,output/'native-store')
        result['native']={key:val for key,val in native.items() if key!='packet'}
    _write_json(output/'result.json',result)
    return result
