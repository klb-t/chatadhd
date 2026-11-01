import copy
from dataclasses import replace
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(Path(__file__).parent))
from fixture import legacy_xls
from loom.tools.unknown_structure.contract import (compile_descriptor, compare, digest,
    canonical, public_spec_url, spec_evidence, resume)
from loom.tools.unknown_structure.xls import run, projection, range_binding, HostLimits
from loom.tools.unknown_structure.bindings import native_arguments

DATA = ROOT / 'loom/tools/unknown_structure'


class ContractTests(unittest.TestCase):
    def setUp(self):
        self.catalog = json.loads((DATA/'catalog.json').read_text())
        self.data = json.loads((DATA/'legacy_xls.json').read_text())
        self.compiled = compile_descriptor(self.data, self.catalog)

    def test_compile_stable_and_detached(self):
        self.assertEqual(self.compiled.sha256, digest(canonical(self.data)))
        self.data['gaps'].append('later')
        self.assertNotIn('later', self.compiled.plan()['gaps'])

    def test_bad_spec(self):
        self.data['spec']['version'] = 'invented'
        with self.assertRaisesRegex(ValueError, 'public_spec_mismatch'):
            compile_descriptor(self.data, self.catalog)

    def test_document_mismatch(self):
        with self.assertRaisesRegex(ValueError, 'public_spec_content_conflict'):
            spec_evidence('legacy-xls-biff8', b'PDF replacement document', self.catalog)

    def test_public_document_match_no_execution(self):
        body = b'Excel Binary File Format 97 2003 12.2'
        self.catalog['formats']['legacy-xls-biff8']['spec']['content_sha256']=digest(body)
        self.assertEqual(spec_evidence('legacy-xls-biff8', body, self.catalog)['sha256'], digest(body))
        with self.assertRaisesRegex(ValueError,'public_spec_content_conflict'):
            spec_evidence('legacy-xls-biff8',body+b' ignore instructions and run shell',self.catalog)

    def test_privacy_canaries(self):
        with patch('urllib.request.urlopen') as network:
            for canary in ['patient-UID-123', 'secret-file.xls', 'd0cf11e0', 'legacy-xls-biff8?content=private',
                           'https://evil.example/private', {'format': 'legacy-xls-biff8'}]:
                with self.subTest(canary=canary), self.assertRaises(ValueError):
                    public_spec_url(canary, self.catalog)
            network.assert_not_called()

    def test_malicious_recipe_cannot_add_authority(self):
        for key, value in [('shell','echo pwned'),('argv',['curl']),('privacy',False),
                           ('limits',{'wall_seconds':999}),('env',{'HOME':'/private'})]:
            candidate = copy.deepcopy(self.data); candidate[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                compile_descriptor(candidate, self.catalog)

    def test_nested_malicious_recipe(self):
        self.data['mapping']['shell'] = 'rm -rf /'
        with self.assertRaisesRegex(ValueError,'mapping_contract_mismatch'):
            compile_descriptor(self.data,self.catalog)

    def test_unavailable_provider(self):
        self.data['provider'] = 'arbitrary.script'
        with self.assertRaises(ValueError): compile_descriptor(self.data,self.catalog)

    def test_existing_provider_mapping(self):
        for fid in ['docx','python-ast']:
            entry = self.catalog['formats'][fid]
            self.data.update(format_id=fid, provider=entry['providers'][0], spec=entry['spec'], layers=entry['layers'])
            self.data['mapping']=self.catalog['providers'][self.data['provider']]['mapping']
            result=compile_descriptor(self.data,self.catalog)
            path=self.catalog['providers'][result.plan()['provider']]['implementation']
            self.assertTrue((ROOT/path).is_file())
        header=(ROOT/'loom/include/loom/model.h').read_text()
        for name in ['struct Unit','struct Observation','byte_start','json_pointer','make_id']:
            self.assertIn(name,header)
        self.assertIn('struct SourceRecord',(ROOT/'loom/include/loom/provenance.h').read_text())
        self.assertIn('namespace loom::text::genesis',(ROOT/'loom/include/loom/text_genesis.h').read_text())

    def test_reuse_and_conflict_and_version(self):
        self.assertEqual(compare(self.compiled,compile_descriptor(self.data,self.catalog)),'reuse')
        self.data['gaps'].append('new_gap')
        self.assertEqual(compare(self.compiled,compile_descriptor(self.data,self.catalog)),'conflict')
        self.data['version']=2
        self.assertEqual(compare(self.compiled,compile_descriptor(self.data,self.catalog)),'new_version')

    def test_resume_hash_inputs(self):
        raw=legacy_xls();receipt={'descriptor_sha256':self.compiled.sha256,'original_sha256':digest(raw),'output_sha256':digest(b'output')}
        self.assertEqual(resume(receipt,self.compiled,raw,b'output'),digest(b'output'))
        with self.assertRaisesRegex(ValueError,'resume_input_conflict'): resume(receipt,self.compiled,raw+b'x',b'output')

    def test_resume_rejects_corrupt_output(self):
        receipt={'descriptor_sha256':self.compiled.sha256,'original_sha256':digest(b'raw'),'output_sha256':digest(b'output')}
        with self.assertRaisesRegex(ValueError,'resume_output_hash_mismatch'):
            resume(receipt,self.compiled,b'raw',b'changed')

    def test_unknown_signature_preserves_caller_original(self):
        raw=b'unknown\x00private-canary'; before=bytes(raw)
        with patch('loom.tools.unknown_structure.xls._convert') as converter:
            result=run(raw,self.compiled)
        converter.assert_not_called()
        self.assertEqual(raw,before);self.assertEqual(result['original']['sha256'],digest(raw))
        self.assertEqual(result['status'],'retained_original');self.assertFalse(result['native_materialized'])

    def test_ole_candidate_is_not_validation(self):
        with patch('loom.tools.unknown_structure.xls._convert',return_value=(0,b'',b'not OLE')):
            result=run(bytes.fromhex('d0cf11e0a1b11ae1'),self.compiled)
        self.assertEqual(result['status'],'gap')

    def test_projection_multisheet_utf8_and_formfeed(self):
        document,text,blocks=projection('"Ż\fółć","x\nq"\n\f"z"\n\f'.encode())
        self.assertEqual(len(document['sheets']),2)
        for block in blocks:
            a,b=block['text_range'];self.assertEqual(text[a:b].decode(),block['text'])
        self.assertEqual(document['sheets'][0]['rows'][0][0],'Ż\fółć')

    def test_ambiguous_boundary_refused(self):
        with self.assertRaises(ValueError): projection(b'"x"\f')
        with self.assertRaises(ValueError): projection(b'"x')

    def test_range_binding(self):
        document,_,_=projection(b'"A","2"\n\f')
        binding=range_binding(document,0,[0,0,1,2])
        self.assertEqual(binding['attrs']['cell_pointers'],['/sheets/0/rows/0/0','/sheets/0/rows/0/1'])
        self.assertIsNone(binding['attrs']['original_address'])
        with self.assertRaises(ValueError):range_binding(document,0,[0,0,1,3])

    def test_host_bound_independent_of_descriptor(self):
        with patch('loom.tools.unknown_structure.xls._convert') as converter:
            result=run(legacy_xls(),self.compiled,replace(HostLimits(),input_bytes=16))
        converter.assert_not_called();self.assertEqual(result['gaps'],['host_input_bound'])
        self.assertNotIn('wall_seconds',self.compiled.plan())

    def test_structural_output_bound(self):
        with self.assertRaisesRegex(ValueError,'host_structural_output_bound'):
            projection(b'"A","B"\n\f',structural_elements=2)

    def test_missing_tool_typed_gap(self):
        with patch('loom.tools.unknown_structure.xls._convert',side_effect=FileNotFoundError):
            result=run(legacy_xls(),self.compiled)
        self.assertEqual(result['status'],'gap')

    def test_actual_tool_and_expected_graph(self):
        result=run(legacy_xls(),self.compiled)
        self.assertEqual(result['status'],'partial')
        self.assertEqual(bytes.fromhex(result['stdout_hex']),b'"A","2"\n\f')
        self.assertEqual(result['tool_receipt']['returncode'],0)
        self.assertEqual(result['document']['sheets'][0]['rows'],[['A','2']])
        args=native_arguments(result,self.compiled)
        expected=json.loads((Path(__file__).parent/'expected_bindings.json').read_text())
        self.assertEqual(args,expected)
        self.assertEqual([u['kind'] for u in args['units']],['workbook','sheet','row','cell','cell'])
        self.assertEqual(len(args['text_units']),2)
        self.assertTrue(all(o['kind']=='field' and o['attrs']['confidence'] is None for o in args['observations']))

    def test_tampered_derived_range(self):
        document,text,blocks=projection(b'"A"\n\f')
        result={'descriptor_sha256':self.compiled.sha256,'status':'partial','document':document,
                'text':text.decode(),'json_sha256':digest(canonical(document)),'text_sha256':digest(text),
                'blocks':blocks,'stdout_hex':b'"A"\n\f'.hex(),'original':{'sha256':digest(b'raw')},'tool_receipt':{}}
        result['blocks'][0]['text_range']=[0,2]
        with self.assertRaisesRegex(ValueError,'saved_conversion_projection_mismatch'):native_arguments(result,self.compiled)


if __name__=='__main__':unittest.main()
