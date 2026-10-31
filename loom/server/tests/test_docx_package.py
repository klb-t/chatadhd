#!/usr/bin/env python3
"""Run from an installed staging artifact, with the original checkout denied.

Only selected new packaging cases run; the accepted parser/backend suites are not replayed.
The harness copies used by staging are test support, never runtime dependencies.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import unittest
from server_flow_harness import HttpFlowCase
from test_server_docx_flow import DocxFlow, fixtures

PACKAGE = Path(os.environ['LOOM_PACKAGE_ROOT']).resolve()
RUNTIME = PACKAGE / 'share/loom/docx_basic'

class PackageFlow(DocxFlow):
    def setUp(self):
        # Deliberately omit DocxFlow's explicit own-helper override.
        HttpFlowCase.setUp(self)

    def test_default_http(self):
        self.test_upload_context_source_repeat_followup_reopen_restart_original()

    def test_missing_packaged_helper(self):
        helper = RUNTIME / 'reader.py'
        saved = RUNTIME / 'reader.py.test-saved'
        helper.rename(saved)
        try: self.refuse(fixtures.package())
        finally: saved.rename(helper)

    def test_missing_configured_interpreter(self):
        manifest = RUNTIME / 'runtime.json'
        saved = manifest.read_bytes()
        changed = json.loads(saved)
        changed['python'] = str(PACKAGE / 'missing-interpreter')
        manifest.write_text(json.dumps(changed))
        try: self.refuse(fixtures.package())
        finally: manifest.write_bytes(saved)

    def test_upload_cannot_select_runtime(self):
        manifest = (RUNTIME / 'runtime.json').read_bytes()
        helper = (RUNTIME / 'reader.py').read_bytes()
        # A document called reader.py is data and cannot replace executable configuration.
        self.refuse(b'raise RuntimeError("synthetic upload must not execute")', 'reader.py.docx')
        self.assertEqual((RUNTIME / 'runtime.json').read_bytes(), manifest)
        self.assertEqual((RUNTIME / 'reader.py').read_bytes(), helper)
        ref, _, _ = self.upload()
        self.success('Odczytaj dokument po odmowie.', [ref])
        self.assertIn('Zażółć gęślą jaźń', self.text(self.wire_messages()[0]))

    def test_shared_library_default(self):
        lib = ctypes.CDLL(str(PACKAGE / 'lib/libloom.so'))
        ptr=ctypes.c_void_p; text=ctypes.c_char_p
        for name,args,ret in [('loom_init_ex',[text,ptr],ptr),('loom_shutdown',[ptr],None),
                              ('loom_free_string',[ptr],None),('loom_set_config_json',[ptr,text],ctypes.c_int),
                              ('loom_chat_ex',[ptr,text,ptr,ptr],ptr)]:
            fn=getattr(lib,name);fn.argtypes=args;fn.restype=ret
        def encoded(x): return json.dumps(x,ensure_ascii=False).encode()
        def read(p):
            self.assertTrue(p)
            try:return json.loads(ctypes.string_at(p))
            finally:lib.loom_free_string(p)
        data=self.root/'capi-default';document=self.root/'biblioteka.docx'
        document.write_bytes(fixtures.package());digest=hashlib.sha256(document.read_bytes()).hexdigest()
        ctx=lib.loom_init_ex(encoded({'data_dir':str(data),'start_workers':False}),None)
        self.assertTrue(ctx)
        try:
            self.assertEqual(lib.loom_set_config_json(ctx,encoded({'llm_provider':'local',
                'local_model_base_url':self.provider.base,'base_url':self.provider.base,
                'default_model':'mock/http-flow','auto_title':False,'semantic_analysis':False,'stream':False})),0)
            result=read(lib.loom_chat_ex(ctx,encoded({'message':'Odczytaj polski dokument.',
                'attachments':[str(document)],'stream':False,'include_memory':False,'include_graph_memory':False}),None,None))
            self.assertNotIn('error',result)
            self.assertIn('Zażółć gęślą jaźń',json.dumps(self.provider.calls[-1],ensure_ascii=False))
            self.assertEqual(hashlib.sha256(document.read_bytes()).hexdigest(),digest)
        finally:lib.loom_shutdown(ctx)

if __name__ == '__main__':
    forbidden=json.loads(os.environ['LOOM_DENIED_SOURCE_PATHS'])
    for path in forbidden:
        try: Path(path).read_bytes()
        except PermissionError: print('PASS checkout denied: '+path,flush=True)
        else: raise AssertionError('original checkout must be inaccessible: '+path)
    manifest=json.loads((RUNTIME/'runtime.json').read_text())
    assert manifest['reader_sha256']==hashlib.sha256((RUNTIME/'reader.py').read_bytes()).hexdigest()
    assert Path(manifest['python']).is_absolute()
    names=['test_default_http','test_missing_packaged_helper','test_missing_configured_interpreter',
           'test_upload_cannot_select_runtime','test_shared_library_default']
    result=unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(PackageFlow(n) for n in names))
    raise SystemExit(not result.wasSuccessful())
