"""Offline independent-formula recount by the same author; no model/API run."""
from collections import Counter
from datetime import datetime, timezone
import hashlib
import io
import json
import math
from pathlib import Path
import re
import sys
import zipfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE.parent / 'local_embedding_panel_v1/runtime_packages'))
import numpy as np


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def archived(folder):
    sidecar = json.loads((folder / 'FIRST_ARCHIVE.json').read_text())
    raw = (folder / 'first_evidence.zip').read_bytes()
    assert digest(raw) == sidecar['archive_sha256']
    with zipfile.ZipFile(io.BytesIO(raw)) as z:
        entries = json.loads(z.read('INVENTORY.json'))['entries']
        assert entries == sidecar['entries']
        assert set(z.namelist()) == {e['path'] for e in entries} | {'INVENTORY.json'}
        values = {}
        for e in entries:
            value = z.read(e['path'])
            assert len(value) == e['bytes'] and digest(value) == e['sha256']
            values[e['path']] = value
    return values


def time_of(value):
    return datetime.fromisoformat(value.replace('Z', '+00:00'))


def tokens(text):
    return re.findall(r'\w+', text.casefold())


def independent_bm25(query, docs, k1, b):
    counts = [Counter(tokens(doc)) for doc in docs]
    mean = sum(sum(c.values()) for c in counts) / len(counts)
    scores = []
    for c in counts:
        value = 0.
        for word, qfreq in sorted(Counter(tokens(query)).items()):
            freq = c[word]
            if freq:
                df = sum(word in d for d in counts)
                idf = math.log((len(counts) + 1) / (df + .5))
                value += qfreq * idf * (freq * (k1 + 1)) / (freq + k1 * (1 - b + b * sum(c.values()) / mean))
        scores.append(value)
    return scores


def close(a, b, tolerance=1e-12):
    if not math.isclose(a, b, abs_tol=tolerance, rel_tol=0):
        raise AssertionError(f'numeric_recount_disagrees:{a}:{b}')


def audit():
    main = archived(HERE)
    cross = archived(HERE / 'cross_encoder_v1')
    for freeze_name in ('freeze_before_scores2.json',):
        frozen = json.loads((HERE / freeze_name).read_text())
        for name, value in frozen['files_sha256'].items():
            assert digest((ROOT / name).read_bytes()) == value
    frozen_cross = json.loads((HERE / 'cross_encoder_v1/freeze_before_scores.json').read_text())
    for name, value in frozen_cross['files_sha256'].items():
        assert digest((ROOT / name).read_bytes()) == value
    scores = json.loads(main['first_scores.json'])
    results = json.loads(main['first_results.json'])
    cross_scores = json.loads(cross['first_scores.json'])
    cross_results = json.loads(cross['first_results.json'])
    output = json.loads(cross['first_outputs.json'])
    policy = json.loads((HERE / 'policy.json').read_text())
    prepared = json.loads((HERE.parent / 'graph_local_baselines_v1/prepared_inputs.json').read_text())
    original = json.loads((HERE.parent / 'graph_local_baselines_v1/first_scores.json').read_text())
    old_map = {r['query_id']: r for r in original['rows']}
    prepared_map = {q['query_id']: q for q in prepared['queries']}
    matrix = np.load(io.BytesIO(main['first_embedding/vectors.npy']), allow_pickle=False)
    index = json.loads(main['first_embedding/index.json'])['rows']
    lookup = {r['text']: i for i, r in enumerate(index)}
    assert matrix.shape == (422, 384)
    assert max(abs(np.linalg.norm(v.astype(np.float64)) - 1) for v in matrix) < 1e-6
    vector_bindings = bm25_bindings = fusion_bindings = physical_bindings = cross_bindings = 0
    for row in scores['rows']:
        q = prepared_map[row['query_id']]
        assert row['prefix_sha256'] == q['prefix_sha256']
        assert [c['turn_id'] for c in row['candidates']] == [c['turn_id'] for c in q['candidates']]
        for c, source in zip(row['candidates'], q['candidates']):
            assert c['source_id'] == source['source_id']
            assert c['text_sha256'] == digest(source['text'].encode())
            assert time_of(c['known_at']) <= time_of(row['as_of'])
            assert c['span']['byte_end'] == len(source['text'].encode())
            physical_bindings += 1
            for view in policy['primary_embedding_variants']:
                a = matrix[lookup[row['variants'][view]]].astype(np.float64)
                b = matrix[lookup[source['representation_text']]].astype(np.float64)
                close(float(np.dot(a, b)), c['scores']['minilm_' + view])
                vector_bindings += 1
        docs = [c['representation_text'] for c in q['candidates']]
        for view in policy['query_variants']:
            recomputed = independent_bm25(row['variants'][view], docs, 1.2, .75)
            for c, value in zip(row['candidates'], recomputed):
                close(c['scores']['bm25_' + view], value)
                bm25_bindings += 1
        for b in (0., 1.):
            recomputed = independent_bm25(row['variants']['structured'], docs, 1.2, b)
            for c, value in zip(row['candidates'], recomputed):
                close(c['scores']['bm25_structured_b' + str(b)], value)
                bm25_bindings += 1
        fusions = {'rrf_original_10': (10, ['lexical_token_cosine', 'character_3_5_cosine', 'learned_minilm_cosine']),
            'rrf_original_60': (60, ['lexical_token_cosine', 'character_3_5_cosine', 'learned_minilm_cosine']),
            'rrf_bm25_minilm_60': (60, ['bm25_structured', 'learned_minilm_cosine']),
            'rrf_multiquery_60': (60, ['minilm_' + v for v in policy['multiquery_variants']]),
            'rrf_bm25_multiquery_60': (60, ['bm25_' + v for v in policy['multiquery_variants']])}
        for name, (constant, channels) in fusions.items():
            for c in row['candidates']:
                value = sum(1 / (constant + row['rankings'][channel].index(c['turn_id']) + 1) for channel in channels)
                close(value, c['scores'][name])
                fusion_bindings += 1
    for row in cross_scores['rows']:
        q = prepared_map[row['query_id']]
        assert row['prefix_sha256'] == q['prefix_sha256']
        for candidate, source in zip(row['candidates'], q['candidates']):
            assert candidate['turn_id'] == source['turn_id']
            for method, key in candidate['crossencoder_pair_refs'].items():
                raw = output['outputs'][key]
                assert raw['candidate_text'] == source['representation_text']
                assert raw['candidate_sha256'] == digest(source['representation_text'].encode())
                assert raw['output_activation'] == 'identity'
                assert raw['encoded_tokens'] <= raw['max_tokens']
                assert raw['truncated'] == (raw['untruncated_tokens'] > raw['max_tokens'])
                close(candidate['scores'][method], raw['raw_logit'])
                cross_bindings += 1
    # Recompute rank metrics directly, without importing either research scorer.
    recounted = {}
    targets = {q['query_id']: q for q in results['query_targets']}
    for corpus, measured in ((scores, results), (cross_scores, cross_results)):
        for method in corpus['methods']:
            hits = covered = selected = relevant_selected = 0
            rr = 0.
            for row in corpus['rows']:
                target = targets[row['query_id']]
                relevant = set(target['relevant_turn_ids'])
                candidates = row['candidates']
                rebuilt = [c['turn_id'] for c in sorted(candidates, key=lambda c:
                    (-c['scores'][method], c['eligible_order'], c['turn_id']))]
                assert rebuilt == row['rankings'][method]
                first = rebuilt[:1]
                selected += len(first)
                hits += bool(set(first) & relevant)
                relevant_selected += len(set(first) & relevant)
                covered += sum(e['turn_id'] in first for e in target['relevant_evidence'])
                rank = next((i for i, ident in enumerate(rebuilt, 1) if ident in relevant), None)
                if rank:
                    rr += 1 / rank
            m = measured['methods'][method]
            assert hits == m['overall']['hit_at_k']['1']['hit_queries']
            assert covered == m['overall']['evidence_recall_at_k']['1']['covered_spans']
            assert selected == m['top1_precision_recall']['selected_turns']
            assert relevant_selected == m['top1_precision_recall']['relevant_selected_turns']
            close(rr / 60, m['overall']['mean_reciprocal_first_evidence_rank'])
            recounted[method] = {'hit_queries': hits, 'covered_spans': covered, 'selected_turns': selected}
    report = {'schema': 'loom.research.retrieval_offline_recount/1', 'created_at': datetime.now(timezone.utc).isoformat(),
        'physical_source_bindings_verified': physical_bindings, 'new_minilm_dot_products_recomputed': vector_bindings,
        'bm25_scores_recomputed_independent_algebra': bm25_bindings, 'rrf_scores_recomputed': fusion_bindings,
        'crossencoder_raw_logit_bindings_verified': cross_bindings, 'unique_methods_recounted': len(recounted),
        'methods': recounted, 'main_archive_sha256': digest((HERE / 'first_evidence.zip').read_bytes()),
        'cross_archive_sha256': digest((HERE / 'cross_encoder_v1/first_evidence.zip').read_bytes()),
        'validation_accessed': False, 'paid_calls': 0, 'new_model_inference': False,
        'independent_implementation_by_same_author': True, 'independent_reviewer_claim': False,
        'gold_target_derivation_independently_revalidated': False,
        'scope': 'first_byte_integrity_prefix_binding_model_output_relevance_metric_recount'}
    with (HERE / 'OFFLINE_RECOUNT.json').open('x') as out:
        out.write(json.dumps(report, sort_keys=True, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ['physical_source_bindings_verified', 'new_minilm_dot_products_recomputed',
        'bm25_scores_recomputed_independent_algebra', 'rrf_scores_recomputed', 'crossencoder_raw_logit_bindings_verified',
        'unique_methods_recounted', 'paid_calls']}))


if __name__ == '__main__':
    audit()
