"""Pinned public local cross-encoder; offline DEV retrieval, never graph truth."""
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import json
import math
from pathlib import Path
import platform
import resource
import sys
import time

HERE = Path(__file__).resolve().parent
PANEL = HERE.parent
sys.path.insert(0, str(PANEL))
import explore
import template_renderer_v2 as renderer


def pair_key(query, candidate, cap):
    raw = json.dumps([query, candidate, cap], ensure_ascii=False, separators=(',', ':')).encode()
    return hashlib.sha256(raw).hexdigest()


def extract_logit(output):
    if tuple(output.shape) != (1, 1):
        raise ValueError('unexpected_sequence_classifier_output_shape')
    value = float(output[0][0])
    if not math.isfinite(value):
        raise ValueError('nonfinite_relevance_logit')
    return value


def method_name(variant, cap):
    return f'crossencoder_{variant}_{cap}'


def score_rows(prepared, original, outputs, policy, templates):
    old = {r['query_id']: r for r in original['rows']}
    if set(old) != {r['query_id'] for r in prepared['queries']}:
        raise ValueError('original_query_inventory_drift')
    result = []
    for q in prepared['queries']:
        original_row = old[q['query_id']]
        if original_row['prefix_sha256'] != q['prefix_sha256']:
            raise ValueError('source_prefix_drift')
        if [c['turn_id'] for c in original_row['candidates']] != [c['turn_id'] for c in q['candidates']]:
            raise ValueError('candidate_binding_inventory_drift')
        rows = []
        views = renderer.render(q, templates)
        for c, previous in zip(q['candidates'], original_row['candidates']):
            score = dict(previous['scores'])
            references = {}
            for variant, cap in [(v, policy['primary_max_tokens']) for v in policy['query_variants']] + [
                    (policy['control_query_variant'], policy['control_max_tokens'])]:
                key = pair_key(views[variant], c['representation_text'], cap)
                output = outputs[key]
                if output['query_text'] != views[variant] or output['candidate_text'] != c['representation_text'] or output['max_tokens'] != cap:
                    raise ValueError('pair_output_binding_drift')
                if not isinstance(output['raw_logit'], (int, float)) or isinstance(output['raw_logit'], bool) or not math.isfinite(output['raw_logit']):
                    raise ValueError('nonfinite_or_nonnumeric_relevance_logit')
                name = method_name(variant, cap)
                score[name] = output['raw_logit']
                references[name] = key
            rows.append(dict(previous, scores=score, crossencoder_pair_refs=references))
        names = list(rows[0]['scores']) if rows else list(original_row['rankings']) + [method_name(v, policy['primary_max_tokens']) for v in policy['query_variants']] + [method_name(policy['control_query_variant'], policy['control_max_tokens'])]
        ranking = {m: explore.ranked_ids(q['candidates'], [c['scores'][m] for c in rows]) for m in names}
        result.append({k: v for k, v in original_row.items() if k not in ('rankings', 'candidates')} |
                      {'candidates': rows, 'rankings': ranking, 'source_judgment_available': False, 'prediction': None})
    return result


def freeze(cache):
    manifest = json.loads((cache / 'model_manifest.json').read_text())
    policy = json.loads((HERE / 'policy.json').read_text())
    if manifest['model_id'] != policy['model_id'] or manifest['revision'] != policy['revision']:
        raise ValueError('model_identity_drift')
    for item in manifest['artifacts']:
        if explore.sha(cache / item['file']) != item['sha256']:
            raise ValueError('model_artifact_drift:' + item['file'])
    explore.write_new(HERE / 'model_manifest.json', manifest)
    paths = [HERE / 'policy.json', HERE / 'cross_encoder.py', HERE / 'evaluate_cross.py', HERE / 'test_cross_encoder.py',
             HERE / 'model_manifest.json', explore.DOCS / 'CROSS_ENCODER_PROTOCOL.md', PANEL / 'representation_templates_v2.json',
             PANEL / 'template_renderer_v2.py', PANEL / 'first_scores.json', PANEL / 'first_results.json',
             PANEL / 'byte_budget_results.json', PANEL / 'evaluate.py', PANEL / 'byte_budget.py']
    explore.write_new(HERE / 'freeze_before_scores.json', {'created_at': datetime.now(timezone.utc).isoformat(),
        'files_sha256': {str(p.relative_to(explore.ROOT)): explore.sha(p) for p in paths},
        'validation_accessed': False, 'paid_calls': 0, 'prior_dev_results_seen': True})


def run(cache):
    before = json.loads((HERE / 'freeze_before_scores.json').read_text())
    for name, h in before['files_sha256'].items():
        if explore.sha(explore.ROOT / name) != h:
            raise ValueError('crossencoder_freeze_drift:' + name)
    if (HERE / 'first_outputs.json').exists() or (HERE / 'first_scores.json').exists():
        raise ValueError('first_outputs_already_exist')
    start_total = time.perf_counter()
    manifest = json.loads((HERE / 'model_manifest.json').read_text())
    for item in manifest['artifacts']:
        if explore.sha(cache / item['file']) != item['sha256']:
            raise ValueError('cached_weight_or_tokenizer_drift:' + item['file'])
    sys.path.insert(0, str(explore.CACHE / 'runtime_packages'))
    import numpy as np
    import onnxruntime as ort
    from tokenizers import Tokenizer
    policy = json.loads((HERE / 'policy.json').read_text())
    templates = json.loads((PANEL / 'representation_templates_v2.json').read_text())
    config = json.loads((cache / 'config.json').read_text())
    if config['model_type'] != 'bert' or config['architectures'] != ['BertForSequenceClassification'] or config['max_position_embeddings'] < policy['primary_max_tokens']:
        raise ValueError('unregistered_model_architecture_or_context_cap')
    initialization_start = time.perf_counter()
    tokenizer = Tokenizer.from_file(str(cache / 'tokenizer.json'))
    settings = ort.SessionOptions()
    settings.intra_op_num_threads = policy['cpu_threads']
    settings.inter_op_num_threads = policy['inter_op_threads']
    settings.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    session = ort.InferenceSession(str(cache / 'onnx/model.onnx'), sess_options=settings, providers=[policy['provider']])
    initialization = time.perf_counter() - initialization_start
    prepared = json.loads((explore.BASE / 'prepared_inputs.json').read_text())
    original = json.loads((PANEL / 'first_scores.json').read_text())
    if prepared['split'] != 'dev' or prepared['query_count'] != 96:
        raise ValueError('frozen_dev_only')
    output = {}
    source_bindings = []
    cpu_start = time.process_time()
    inference_start = time.perf_counter()
    inference_seconds = 0.0
    for q in prepared['queries']:
        variants = renderer.render(q, templates)
        for c in q['candidates']:
            for variant, cap in [(v, policy['primary_max_tokens']) for v in policy['query_variants']] + [
                    (policy['control_query_variant'], policy['control_max_tokens'])]:
                query, candidate = variants[variant], c['representation_text']
                key = pair_key(query, candidate, cap)
                source_bindings.append({'query_id': q['query_id'], 'turn_id': c['turn_id'], 'source_id': c['source_id'],
                    'known_at': c['known_at'], 'prefix_sha256': q['prefix_sha256'], 'pair_key': key,
                    'variant': variant, 'max_tokens': cap})
                if key in output:
                    continue
                tokenizer.no_truncation()
                tokenizer.no_padding()
                complete = tokenizer.encode(query, candidate, add_special_tokens=True)
                tokenizer.enable_truncation(max_length=cap, strategy='longest_first', direction='right')
                encoded = tokenizer.encode(query, candidate, add_special_tokens=True)
                tensors = {'input_ids': np.asarray([encoded.ids], dtype=np.int64),
                    'attention_mask': np.asarray([encoded.attention_mask], dtype=np.int64),
                    'token_type_ids': np.asarray([encoded.type_ids], dtype=np.int64)}
                feed = {item.name: tensors[item.name] for item in session.get_inputs()}
                tensor_hashes = {name: hashlib.sha256(value.astype('<i8').tobytes()).hexdigest() for name, value in feed.items()}
                begin = time.perf_counter()
                returned = session.run(None, feed)
                elapsed = time.perf_counter() - begin
                inference_seconds += elapsed
                value = extract_logit(returned[0])
                output[key] = {'query_text': query, 'candidate_text': candidate, 'max_tokens': cap,
                    'query_sha256': hashlib.sha256(query.encode()).hexdigest(), 'candidate_sha256': hashlib.sha256(candidate.encode()).hexdigest(),
                    'untruncated_tokens': len(complete.ids), 'encoded_tokens': len(encoded.ids),
                    'truncated': len(complete.ids) > cap, 'input_tensor_sha256': tensor_hashes,
                    'raw_logit': value, 'output_activation': 'identity', 'inference_seconds': elapsed}
    cpu_seconds = time.process_time() - cpu_start
    pair_elapsed = time.perf_counter() - inference_start
    versions = {name: importlib.metadata.version(name) for name in ('numpy', 'onnxruntime', 'tokenizers')}
    result = {'schema': 'loom.research.local_crossencoder_outputs/1', 'outputs': output, 'source_bindings': source_bindings,
        'model_manifest_sha256': explore.sha(HERE / 'model_manifest.json'), 'policy_sha256': explore.sha(HERE / 'policy.json'),
        'model_id': policy['model_id'], 'revision': policy['revision'], 'unique_pair_inferences': len(output),
        'query_candidate_variant_bindings': len(source_bindings), 'truncated_unique_pairs': sum(v['truncated'] for v in output.values()),
        'initialization_seconds': initialization, 'session_inference_seconds': inference_seconds,
        'pair_encode_and_inference_seconds': pair_elapsed, 'cpu_process_seconds_during_pair_phase': cpu_seconds,
        'total_seconds_including_integrity_import_init': time.perf_counter() - start_total,
        'max_rss_kib': resource.getrusage(resource.RUSAGE_SELF).ru_maxrss,
        'runtime': {'versions': versions, 'python': platform.python_version(), 'platform': platform.platform(),
            'machine': platform.machine(), 'providers': session.get_providers(), 'threads': policy['cpu_threads'],
            'inputs': [x.name for x in session.get_inputs()], 'outputs': [x.name for x in session.get_outputs()]},
        'paid_calls': 0, 'training': False, 'validation_accessed': False, 'source_judgment_available': False}
    explore.write_new(HERE / 'first_outputs.json', result)
    rows = score_rows(prepared, original, output, policy, templates)
    explore.write_new(HERE / 'first_scores.json', {'schema': 'loom.research.local_crossencoder_scores/1', 'split': 'dev',
        'rows': rows, 'queries': len(rows), 'methods': sorted(rows[0]['rankings']),
        'output_sha256': explore.sha(HERE / 'first_outputs.json'), 'prepared_sha256': explore.sha(explore.BASE / 'prepared_inputs.json'),
        'original_sha256': explore.sha(PANEL / 'first_scores.json'), 'source_judgment_available': False, 'prediction': None})
    explore.write_new(HERE / 'freeze_before_gold.json', {'created_at': datetime.now(timezone.utc).isoformat(),
        'scores_sha256': explore.sha(HERE / 'first_scores.json'), 'outputs_sha256': explore.sha(HERE / 'first_outputs.json'),
        'validation_accessed': False})
    print(json.dumps({k: result[k] for k in ['unique_pair_inferences', 'query_candidate_variant_bindings', 'truncated_unique_pairs',
        'initialization_seconds', 'session_inference_seconds', 'pair_encode_and_inference_seconds', 'paid_calls']}))


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', choices=('freeze', 'run'))
    p.add_argument('--cache', type=Path, required=True)
    args = p.parse_args()
    globals()[args.stage](args.cache)
