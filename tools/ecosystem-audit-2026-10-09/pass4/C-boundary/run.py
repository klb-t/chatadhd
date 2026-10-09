#!/usr/bin/env python3
"""PASS4 independent C boundaries. No model requests; synthetic inputs only.

Executes the checkout's handoff fixture, preparation connector, final projection
producer and (when supplied) actual native GraphPacket store. It does not copy
any of those implementations. FAIL acceptance and PASS reproduction are separate.
"""
import argparse
from contextlib import redirect_stdout, redirect_stderr
from copy import deepcopy
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo', type=Path, required=True)
p.add_argument('--sha', required=True)
p.add_argument('--prior-repo', type=Path, required=True)
p.add_argument('--prior-sha', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--native-lib', type=Path)
p.add_argument('--native-source', type=Path)
p.add_argument('--native-sha')
p.add_argument('--packet-output', type=Path)
a = p.parse_args()
sys.dont_write_bytecode = True
repo = a.repo.resolve()
prior = a.prior_repo.resolve()
for checkout, expected in [(repo, a.sha), (prior, a.prior_sha)]:
    if subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip() != expected:
        raise SystemExit('checkout_sha_mismatch')
sys.path[:0] = [str(repo), str(repo/'loom/tools/structure'), str(repo/'loom/tools/seeding')]
from loom.tools.seeding import method_graph as graph
from loom.tools.structure import credential_handoff_v2 as handoff
from loom.tools.structure import experiment_workflow_v1 as workflow

base = repo/'docs/research/thread7_real_2026-10-09/continuation_01'
tests = []
sha = lambda raw: hashlib.sha256(raw).hexdigest()


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


connector = load('audit_c4_connector', base/'connect_prepared.py')
builder = load('audit_c4_final_builder', base/'build_final_handoff.py')


def add(identifier, kind, passed, **evidence):
    tests.append({'id': identifier, 'category': kind,
                  'status': 'PASS' if passed else 'FAIL', 'evidence': evidence})


def blocked(identifier, reason, **evidence):
    tests.append({'id': identifier, 'category': 'integration', 'status': 'BLOCKED',
                  'evidence': {'reason': reason, **evidence}})


def denied_network(*args, **kwargs):
    raise RuntimeError('audit_external_transport_blocked')


def err_summary(exc):
    return {'type': type(exc).__name__, 'message_sha256': sha(str(exc).encode())}


def credential_tests():
    # The environment affects tempfile rather than the consumer's admission.
    # No real /tmp or existing Git metadata is changed.
    with tempfile.TemporaryDirectory(prefix='audit-c4-credentials-', dir='/var/tmp') as td:
        root = Path(td); valid = root/'allowed'; valid.mkdir()
        forbidden = root/'synthetic-git'; forbidden.mkdir(); (forbidden/'.git').mkdir()
        redirected = forbidden/'tmp'; redirected.mkdir()
        factory = tempfile.TemporaryDirectory
        original_tempdir = tempfile.tempdir
        original_tmp_env = os.environ.get('TMPDIR')
        try:
            os.environ['TMPDIR'] = str(valid)
            tempfile.tempdir = None
            configured = Path(tempfile.gettempdir()).resolve() == valid.resolve()
            current = load('audit_current_credential_tests', repo/'loom/tools/structure/test_credential_handoff_v2.py')
            output = io.StringIO()
            result = unittest.TextTestRunner(stream=output, verbosity=0).run(unittest.defaultTestLoader.loadTestsFromModule(current))
            a.output.with_name('credential-author-tests.log').write_text(output.getvalue())
            add('C4-CREDENTIAL-01', 'repair_acceptance', configured and result.wasSuccessful() and result.testsRun == 10,
                actual_author_tests=result.testsRun, errors=len(result.errors), failures=len(result.failures),
                skipped=len(result.skipped), configured_root_observed=configured, configuration='TMPDIR environment; tempfile cache reset before test import',
                log_sha256=sha(output.getvalue().encode()), uses_real_python_crypto_and_node_webcrypto=True)

            # Controlled reproduction of the actual prior hard-coded /tmp call
            # under the problematic condition: that root is inside a Git tree.
            old = load('audit_prior_credential_tests', prior/'loom/tools/structure/test_credential_handoff_v2.py')
            calls = []
            def route_tmp(*args, **kwargs):
                if kwargs.get('dir') == '/tmp':
                    calls.append(True); kwargs['dir'] = str(redirected)
                return factory(*args, **kwargs)
            with patch.object(tempfile, 'TemporaryDirectory', route_tmp):
                instance = old.HybridHandoffTests('test_cli_error_does_not_print_credential_or_private_material')
                error = None
                try: instance.setUp()
                except Exception as exc: error = str(exc)
                finally: instance.doCleanups()
            add('C4-CREDENTIAL-02', 'bug_reproduction', bool(calls) and error == 'private_path_inside_git',
                prior_sha=a.prior_sha, explicit_tmp_routed_to_synthetic_git_root=bool(calls),
                real_storage_guard_error=error, changed_platform_git_metadata=False)
            for kind in ['git-directory', 'git-file', 'symlink']:
                candidate = root/kind; candidate.mkdir()
                if kind == 'git-directory': (candidate/'.git').mkdir()
                elif kind == 'git-file': (candidate/'.git').write_text('gitdir: synthetic-only\n')
                else:
                    actual = root/'symlink-target'; actual.mkdir()
                    (candidate/'alias').symlink_to(actual, target_is_directory=True)
                    candidate = candidate/'alias'
                session = candidate/'session'; html = root/(kind+'.html'); error = None
                try: handoff.generate(session, html, 'synthetic-audit-only', now=1000)
                except ValueError as exc: error = str(exc)
                add('C4-CREDENTIAL-03-'+kind, 'contract', error in {'private_path_inside_git', 'private_path_not_allowed'} and not session.exists() and not html.exists(),
                    actual_guard_error=error, private_material_created=session.exists(), public_html_created=html.exists())
        finally:
            tempfile.tempdir = original_tempdir
            if original_tmp_env is None: os.environ.pop('TMPDIR', None)
            else: os.environ['TMPDIR'] = original_tmp_env


def synthetic_preparation(root, mismatch=False, reorder=False):
    prepared, corpus = root/'prepared', root/'corpus'
    prepared.mkdir(parents=True); corpus.mkdir()
    plan = json.loads((base/'connection-plan.json').read_bytes())
    plan['campaign_id'] = 'synthetic-pass4-only'
    plan['provider_order'] = ['synthetic-provider']
    plan['scopes'] = [{'id': 'synthetic-scope', 'variants': plan['scopes'][0]['variants'][:2]}]
    source_hash = sha(b'synthetic source revision 2')
    prepared_source_hash = sha(b'synthetic source revision 1') if mismatch else source_hash
    rows, raw_bodies = [], {}
    for index, variant in enumerate(plan['scopes'][0]['variants']):
        body = {'model': 'synthetic/model', 'provider': {'only': ['openai'], 'allow_fallbacks': False},
                'max_tokens': 8, 'messages': [{'role': 'system', 'content': 'synthetic mechanism fixture'},
                {'role': 'user', 'content': json.dumps({'context': {'variant': variant, 'marker': index}})}]}
        raw = workflow.canonical(body); name = 'requests/'+sha(raw)+'.json'
        connector.write(prepared/name, raw); raw_bodies[sha(raw)] = raw
        rows.append({'family_id': 'synthetic-stable-family', 'phase': 'answer', 'variant': variant,
                     'body_ready': True, 'status': 'prepared', 'body_path': name, 'body_sha256': sha(raw),
                     'preparation_id': sha(raw), 'task_id': 'synthetic-task-'+str(index),
                     'source_sha256': prepared_source_hash})
    corpus_value = {'sources': [{'family_id': 'synthetic-stable-family', 'provider': 'synthetic-provider',
                    'split': 'tuning', 'features': {'text_chars': 16}, 'raw_sha256': source_hash}]}
    def permute(value):
        if isinstance(value, dict): return {k: permute(v) for k, v in reversed(list(value.items()))}
        if isinstance(value, list): return [permute(v) for v in value]
        return value
    if reorder: rows, corpus_value = permute(rows), permute(corpus_value)
    (prepared/'jobs-index.jsonl').write_bytes(b'\n'.join(json.dumps(r).encode() for r in rows))
    (corpus/'panel.json').write_bytes(json.dumps(corpus_value, indent=3 if reorder else None).encode())
    manifest = {'files': {str(f.relative_to(prepared)): sha(f.read_bytes()) for f in sorted(prepared.rglob('*')) if f.is_file()}}
    if reorder: manifest = permute(manifest)
    (prepared/'MANIFEST.json').write_bytes(json.dumps(manifest, indent=2 if reorder else None).encode())
    plan['corpus_panel_sha256'] = sha((corpus/'panel.json').read_bytes())
    plan['preparation_manifest_sha256'] = sha((prepared/'MANIFEST.json').read_bytes())
    return prepared, corpus, plan, raw_bodies


def observe_connection(output, plan, originals):
    target = output/plan['scopes'][0]['id']
    manifest = connector.read(target/'payer/manifest.json')
    connector.payer_manifest.validate_manifest(manifest, base_dir=target/'payer')
    bindings = connector.read(target/'analysis-bindings.json')
    frozen_spec = connector.read(target/'spec.json')
    q = workflow.Queue(target/'queue.sqlite')
    try: snapshot, order = q.snapshot(), q.actual_order()
    finally: q.db.close()
    operations = manifest['operations']
    checks = {
        'exact_request_bytes': all((target/'payer'/o['request_file']).read_bytes() == originals[o['request_sha256']] for o in operations),
        'same_operation_and_request_bindings': {(o['operation_id'],o['request_sha256']) for o in operations} == {(b['operation_id'],b['request_sha256']) for b in bindings},
        'queue_ordinals_preserved': [o['metadata']['queue_ordinal'] for o in operations] == list(range(len(operations))),
        'no_dispatch': order == [] and all(x['state'] == 'prepared' and x['result'] is None for x in snapshot),
        'not_authorized_by_preparation': manifest['metadata']['dispatch_ready'] is False and manifest['metadata']['reservation_booked'] is False,
        'source_versions_match': all(next(c['source_sha256'] for c in frozen_spec['scope'] if c['family'] == b['family']) == b['source_sha256'] for b in bindings),
    }
    # Compare stable logical selection and exact requests, not the provenance
    # hashes of deliberately reserialized wrapper files.
    meaning = [(o['operation_id'], o['request_sha256'], o['metadata']['queue_ordinal']) for o in operations]
    return checks, meaning, snapshot, bindings


def connection_tests():
    with tempfile.TemporaryDirectory(prefix='audit-c4-connect-', dir='/var/tmp') as td:
        root = Path(td); observations = []
        for name, reordered in [('original',False), ('equivalent',True)]:
            prepared, corpus, plan, originals = synthetic_preparation(root/name, reorder=reordered)
            if name == 'original': frozen_original_plan = deepcopy(plan)
            output = root/(name+'-output')
            receipt = connector.connect(prepared, corpus, output, plan)
            checks, meaning, snapshot, bindings = observe_connection(output, plan, originals)
            observations.append(meaning)
            add('C4-CONNECTION-01-'+name, 'integration', all(checks.values()), checks=checks,
                queued=len(snapshot), producer_reports_new_calls=receipt[0]['new_calls'],
                actual_consumers=['connect_prepared.connect','experiment_workflow_v1.Queue','research_programme_manifest.validate_manifest'])
        add('C4-CONNECTION-02', 'contract', observations[0] == observations[1],
            transformation='reverse only object keys and change whitespace of wrapper manifest/index/panel; keep frozen request bytes identical',
            invariant='operation identity, ordered exact request digests and scheduler ordinals',
            syntax_equivalence=True, array_order_changed=False, identities_equal=observations[0] == observations[1])

        # Relocate unchanged frozen inputs; the manifest and pinned versions do
        # not change. Graph/source identity must not depend on this local path.
        original_root=root/'original'; moved=root/'relocated'; moved.mkdir()
        shutil.copytree(original_root/'prepared',moved/'prepared')
        shutil.copytree(original_root/'corpus',moved/'corpus')
        original_plan=frozen_original_plan
        original_requests={f.stem:f.read_bytes() for f in (moved/'prepared'/'requests').glob('*.json')}
        relocated_output=root/'relocated-output'
        connector.connect(moved/'prepared',moved/'corpus',relocated_output,original_plan)
        checks,meaning,_,_=observe_connection(relocated_output,original_plan,original_requests)
        add('C4-CONNECTION-05','contract',all(checks.values()) and meaning==observations[0],
            invariant='same pinned manifest/panel/body versions, operation IDs and request order after local relocation',
            identical_versions=True,identities_equal=meaning==observations[0],checks=checks)

        # A separate transformation: serialize/reload the plan without changing
        # any value or list ordering. Match-field order must not emerge from an
        # otherwise unordered JSON object. The real Queue guards spec identity.
        reloaded_plan=connector.read(root/'original-output'/'PLAN.json')
        reordered_output=root/'plan-key-order-output'
        connector.connect(moved/'prepared',moved/'corpus',reordered_output,reloaded_plan)
        checks,reordered_meaning,reordered_snapshot,_=observe_connection(reordered_output,reloaded_plan,original_requests)
        original_spec=connector.read(root/'original-output'/'synthetic-scope'/'spec.json')
        reordered_spec=connector.read(reordered_output/'synthetic-scope'/'spec.json')
        fields_a=original_spec['request_template']['$match']['fields']
        fields_b=reordered_spec['request_template']['$match']['fields']
        q=workflow.Queue(root/'original-output'/'synthetic-scope'/'queue.sqlite'); queue_error=None; added=None
        try:
            try: added=q.add([r['job'] for r in reordered_snapshot])
            except ValueError as exc: queue_error=str(exc)
            count_after=len(q.snapshot())
        finally:q.db.close()
        evidence={'finding':'A4-C-002','plan_values_equal':original_plan==reloaded_plan,
                  'identical_pinned_input_bytes':True,'generated_match_fields_original':fields_a,
                  'generated_match_fields_reloaded':fields_b,'field_sets_equal':set(fields_a)==set(fields_b),
                  'exact_requests_equal':[x[1] for x in reordered_meaning]==[x[1] for x in observations[0]],
                  'operation_identities_equal':reordered_meaning==observations[0],
                  'actual_queue_resume_error':queue_error,'jobs_after_attempt':count_after,
                  'invariant':'Equivalent JSON plan object key order must not create a different matching recipe/spec or prevent identity-preserving re-add.'}
        reproduced=original_plan==reloaded_plan and fields_a!=fields_b and set(fields_a)==set(fields_b) and queue_error=='queue_spec_identity_reused'
        add('C4-CONNECTION-06-A','bug_reproduction',reproduced,**evidence)
        add('C4-CONNECTION-06-B','acceptance',reordered_meaning==observations[0] and queue_error is None and added==0 and count_after==2,**evidence)

        prepared, corpus, plan, originals = synthetic_preparation(root/'mismatched-source', mismatch=True)
        output = root/'mismatched-source-output'; error = None
        try: connector.connect(prepared, corpus, output, plan)
        except Exception as exc: error = err_summary(exc)
        disagree = False; details = {}
        if (output/plan['scopes'][0]['id']/'analysis-bindings.json').exists():
            checks, meaning, snapshot, bindings = observe_connection(output, plan, originals)
            disagree = not checks['source_versions_match']
            spec = connector.read(output/plan['scopes'][0]['id']/'spec.json')
            details = {'queue_spec_source_sha256': spec['scope'][0]['source_sha256'], 'analysis_source_sha256': bindings[0]['source_sha256'],
                       'same_operation_id': snapshot[0]['job']['operation_id'] in [b['operation_id'] for b in bindings], 'checks': checks}
        add('C4-CONNECTION-03-A', 'bug_reproduction', error is None and disagree,
            finding='A4-C-001', both_frozen_input_sets_validate=True, error=error, **details)
        add('C4-CONNECTION-03-B', 'acceptance', error is not None and not disagree,
            finding='A4-C-001', expected='reject or explicitly reconcile preparation source version before queue/payer/analysis bindings are emitted', error=error)

        # Actual file write interruption; do not mock the queue or its state.
        prepared, corpus, plan, originals = synthetic_preparation(root/'interrupted')
        output = root/'interrupted-output'; real_write = connector.write
        def stop_after_queue(path, raw):
            if str(path).endswith('/payer/manifest.json'): raise OSError('synthetic_audit_interruption')
            return real_write(path, raw)
        first_error = None
        with patch.object(connector, 'write', stop_after_queue):
            try: connector.connect(prepared, corpus, output, plan)
            except OSError as exc: first_error = str(exc)
        q = workflow.Queue(output/plan['scopes'][0]['id']/'queue.sqlite')
        try: snapshot, actual_order = q.snapshot(), q.actual_order()
        finally: q.db.close()
        retry_error = None
        try: connector.connect(prepared, corpus, output, plan)
        except ValueError as exc: retry_error = str(exc)
        add('C4-CONNECTION-04', 'contract', first_error == 'synthetic_audit_interruption' and
            retry_error == 'new_private_output_required' and not (output/'RECEIPT.json').exists() and
            actual_order == [] and all(x['state'] == 'prepared' and x['result'] is None for x in snapshot),
            interruption='write payer manifest after actual durable Queue has been created', queued=len(snapshot),
            complete_receipt_written=(output/'RECEIPT.json').exists(), explicit_retry_error=retry_error,
            boundary='Interrupted output is not reported as executed/completed; no resumable in-place connector contract is exposed.')


def projection_tests():
    required = ['handoff-example.json','handoff-contract.json','context-expanded-v2-receipt.json',
                'connection-receipt.json','FINAL_TESTS.json','evaluation-protocol.json','handoff-projection.json']
    for filename in [None, 'handoff-example.json','evaluation-protocol.json','handoff-projection.json']:
        canary = ('synthetic-pass4-canary-'+sha(str(filename).encode())).encode()
        with tempfile.TemporaryDirectory(prefix='audit-c4-projection-', dir='/var/tmp') as td:
            directory=Path(td)
            for name in required: shutil.copyfile(base/name, directory/name)
            if filename:
                value=graph.strict_json((directory/filename).read_bytes())
                value['audit_unknown_auxiliary']={'synthetic_private_marker':canary.decode()}
                (directory/filename).write_bytes(graph.canonical(value))
            log=io.StringIO(); error=None
            try:
                with patch.object(builder,'__file__',str(directory/'build_final_handoff.py')), redirect_stdout(log), redirect_stderr(log): builder.main()
            except Exception as exc:
                error={**err_summary(exc), 'canary_in_exception':canary in str(exc).encode()}
                with redirect_stderr(log): sys.excepthook(type(exc),exc,exc.__traceback__)
            target=directory/'handoff-final-artifact-v3.json.gz'; roles=[]; raw_hit=False
            if target.exists():
                artifact=graph.strict_json(gzip.decompress(target.read_bytes()))
                raw_hit=canary in graph.canonical(artifact)
                roles=[role for role,data in graph.recover_files(artifact).items() if canary in data or (data.startswith(b'\x1f\x8b') and canary in gzip.decompress(data))]
            leaked=bool(raw_hit or roles or canary in log.getvalue().encode() or (error and error['canary_in_exception']))
            suffix='control' if filename is None else filename.split('.')[0]
            evidence={'input':filename,'artifact_emitted':target.exists(),'source_roles_hit':roles,'raw_artifact_hit':raw_hit,
                      'diagnostic_canary_hit':canary in log.getvalue().encode(),'error':error,'canary_sha256':sha(canary)}
            if filename:
                finding='A2-C-004' if filename=='handoff-example.json' else 'A2-C-001'
                add('C4-PUBLIC-'+suffix+'-A','bug_reproduction',leaked,finding=finding,**evidence)
                add('C4-PUBLIC-'+suffix+'-B','acceptance',not leaked and bool(error),finding=finding,**evidence)
            else: add('C4-PUBLIC-control','contract',target.exists() and error is None and not leaked,**evidence)


def native_test():
    path=base/'handoff-final-artifact-v3.json.gz'; artifact=graph.strict_json(gzip.decompress(path.read_bytes()))
    packet=artifact['packet']; files=graph.recover_files(artifact); results=graph.recover_results(artifact)
    profile={'vocabulary':artifact['contract']['vocabulary'], **{k:packet[k] for k in ['entities','claims','sources']},
             'selection':{'members':[{'method_version_id':artifact['contract']['bindings']['method_version_id']}],
             'parameter_layers':['method','member','selection','user']}}
    if a.packet_output:
        a.packet_output.parent.mkdir(parents=True,exist_ok=True)
        a.packet_output.write_bytes(graph.canonical({'profile':profile,'packet':packet}))
    if not a.native_lib:
        blocked('C4-NATIVE-01','native library argument not supplied; final packet emitted for separate registry integration')
        return
    if not a.native_source or not a.native_sha: raise ValueError('native_provenance_required')
    head=subprocess.check_output(['git','-C',str(a.native_source),'rev-parse','HEAD'],text=True).strip()
    if head!=a.native_sha: raise ValueError('native_checkout_sha_mismatch')
    from loom.tools.coordination.graph_store import NativeGraphStore
    with tempfile.TemporaryDirectory(prefix='audit-c4-native-',dir='/var/tmp') as td:
        selection={k:[graph.codec.record_id(k,row) for row in packet[k]] for k in ['entities','claims','sources']}
        expected={k:{v:None for v in ids} for k,ids in selection.items()}
        with NativeGraphStore(a.native_lib,Path(td)/'store') as store:
            receipt=store.accept(packet,target='audit-c4-final-handoff',selection=selection,expected_rows=expected,explicitly_accepted=True)['receipt']
        with NativeGraphStore(a.native_lib,Path(td)/'store') as store:
            read=store.read(receipt['id']); replay=store.replay(receipt['id'])
        restored={**artifact,'packet':read['receipt']['packet']}
        checks={'packet_exact':restored['packet']==packet,'receipt_immutable':replay['receipt']==receipt,
                'rows_match':read['row_drift']['matches'],'source_bytes_exact':graph.recover_files(restored)==files,
                'results_exact':graph.recover_results(restored)==results,'truth_not_established':receipt['acceptance_establishes_content_truth'] is False}
        add('C4-NATIVE-01','integration',all(checks.values()),checks=checks,counts={k:len(v) for k,v in selection.items()},
            result_count=len(results),artifact_sha256=sha(path.read_bytes()),native_sha=a.native_sha,
            native_library_sha256=sha(a.native_lib.read_bytes()),restart='close actual native context then open a new Runtime on same files',
            scope='store/read/replay only; MethodRegistry and executor availability are a separate gate')


with patch.object(socket.socket,'connect',denied_network),patch('socket.create_connection',denied_network):
    for name, action in [('credential',credential_tests),('connection',connection_tests),('projection',projection_tests),('native',native_test)]:
        try: action()
        except Exception as exc: add('C4-HARNESS-'+name,'harness_error',False,**err_summary(exc))

receipt={'schema':'klb.audit.pass4.c-boundary/1','repo':'klb-t/chatadhd','sha':a.sha,'prior_sha':a.prior_sha,
         'test_source_sha256':sha(Path(__file__).read_bytes()),'tests':tests,
         'counts':{s:sum(t['status']==s for t in tests) for s in ['PASS','FAIL','BLOCKED']},
         'counts_by_category':{c:{s:sum(t['category']==c and t['status']==s for t in tests) for s in ['PASS','FAIL','BLOCKED']} for c in sorted({t['category'] for t in tests})},
         'private_inputs_read':False,'real_network_calls':0,'paid_calls':0,
         'transport_guard':'Python connect/create_connection denied; all inputs public/synthetic; native workers disabled by existing wrapper; Node executes crypto in VM without network APIs',
         'notes':['Bug reproduction PASS does not mean product PASS.','No author queue unit suite rerun. Credential suite specifically requested by owner.']}
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'output':str(a.output),'counts':receipt['counts'],'categories':receipt['counts_by_category']}))
raise SystemExit(1 if any(t['status']=='FAIL' and t['category']!='bug_reproduction' for t in tests) else 0)
