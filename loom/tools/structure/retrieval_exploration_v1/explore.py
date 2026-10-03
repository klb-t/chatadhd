"""Offline DEV-only conditional evidence ranking; no API, no judgment promotion."""
from __future__ import annotations
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import time

HERE = Path(__file__).resolve().parent
STRUCTURE = HERE.parent
ROOT = STRUCTURE.parents[2]
DOCS = ROOT / 'docs/research/retrieval_exploration_v1'
BASE = STRUCTURE / 'graph_local_baselines_v1'
CACHE = STRUCTURE / 'local_embedding_panel_v1'
sys.path.insert(0, str(STRUCTURE))
sys.path.insert(0, str(BASE))
import local_baselines as baseline


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_new(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as out:
        out.write(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + '\n')


def token_list(text):
    return re.findall(r'\w+', text.casefold())


def bm25(query, documents, k1=1.2, b=0.75):
    """IDF and mean length see this source prefix only; repeated query terms count."""
    if k1 <= 0 or not 0 <= b <= 1:
        raise ValueError('invalid_bm25_parameters')
    if not documents:
        return []
    counted = [Counter(token_list(d)) for d in documents]
    lengths = [sum(d.values()) for d in counted]
    mean_length = sum(lengths) / len(lengths)
    result = []
    for doc, length in zip(counted, lengths):
        score = 0.0
        for term, qf in Counter(token_list(query)).items():
            tf = doc[term]
            if not tf:
                continue
            df = sum(term in d for d in counted)
            idf = math.log(1 + (len(documents) - df + .5) / (df + .5))
            normalization = (1 - b + b * length / mean_length) if mean_length else 1.0
            score += qf * idf * tf * (k1 + 1) / (tf + k1 * normalization)
        result.append(score)
    return result


def ranked_ids(candidates, scores):
    if len(scores) != len(candidates):
        raise ValueError('candidate_score_inventory_drift')
    return [c['turn_id'] for c, score in sorted(zip(candidates, scores), key=lambda pair:
        (pair[1] is None, -pair[1] if pair[1] is not None else 0,
         pair[0]['eligible_order'], pair[0]['turn_id'])) if score is not None]


def rrf(candidate_ids, rankings, rank_constant=60):
    if rank_constant <= 0:
        raise ValueError('invalid_rrf_constant')
    if len(candidate_ids) != len(set(candidate_ids)):
        raise ValueError('duplicate_candidate_identity')
    inventory = set(candidate_ids)
    values = dict.fromkeys(candidate_ids, 0.0)
    for ranking in rankings:
        if len(ranking) != len(set(ranking)) or not set(ranking) <= inventory:
            raise ValueError('rrf_inventory_drift')
        for rank, ident in enumerate(ranking, 1):
            values[ident] += 1 / (rank_constant + rank)
    return [values[ident] for ident in candidate_ids]


def render_variants(row):
    q = row['query']
    nodes = {n['id']: n for n in row['prefix_payload']['node_inventory']}
    s, t = nodes[q['source']]['text'], nodes[q['target']]['text']
    actor, relation = q['attributed_to'], q['relation']
    if relation not in ('implies', 'causes'):
        raise ValueError('unregistered_relation')
    if row['language'] == 'pl':
        verb = 'implikuje' if relation == 'implies' else 'powoduje'
        forward = f'Według {actor}: jeżeli {s}, to {t}.' if relation == 'implies' else f'Według {actor}: „{s}” powoduje „{t}”.'
        question = f'Czy według {actor} „{s}” {verb} „{t}”?'
        denial = f'Czy {actor} zaprzecza temu, że „{s}” {verb} „{t}”?'
        reverse = f'Czy według {actor} „{t}” {verb} „{s}”?'
    elif row['language'] == 'en':
        verb = 'imply' if relation == 'implies' else 'cause'
        declarative = 'implies' if relation == 'implies' else 'causes'
        forward = f'According to {actor}: if {s}, then {t}.' if relation == 'implies' else f'According to {actor}: “{s}” causes “{t}”.'
        question = f'According to {actor}, does “{s}” {verb} “{t}”?'
        denial = f'Does {actor} deny that “{s}” {declarative} “{t}”?'
        reverse = f'According to {actor}, does “{t}” {verb} “{s}”?'
    else:
        raise ValueError('unregistered_language')
    return {'structured': row['query_text'], 'endpoints': s + '\n' + t,
        'forward': forward, 'typed': f'actor: {actor}\nrelation: {relation}\nantecedent: {s}\nconsequent: {t}',
        'question': question, 'denial': denial, 'reverse': reverse,
        'aliases': ' '.join([actor, relation, s, *nodes[q['source']]['aliases'], t, *nodes[q['target']]['aliases']])}


def endpoint_coverage(source, target, document, ordered_bonus=0.0):
    """Symmetric token recall plus explicit directional proximity diagnostic."""
    s, t, d = token_list(source), token_list(target), token_list(document)
    if not s or not t or not d:
        return 0.0
    ds = set(d)
    coverage = min(sum(x in ds for x in s) / len(s), sum(x in ds for x in t) / len(t))
    # Exact contiguous spans preserve operators/negation and cannot infer paraphrases.
    def starts(parts):
        return [i for i in range(len(d) - len(parts) + 1) if d[i:i + len(parts)] == parts]
    left, right = starts(s), starts(t)
    ordered = any(a + len(s) <= b for a in left for b in right)
    return coverage + (ordered_bonus if ordered else 0.0)


def embedding_run(prepared, policy):
    """Encode only new frozen views; cached pinned model, no network or training."""
    sys.path.insert(0, str(CACHE / 'runtime_packages'))
    import local_embedding_panel as instrument
    env = json.loads((CACHE / 'instrument_environment.json').read_text())
    np, embed, runtime = instrument.load_instrument(CACHE, env['threads'])
    variants = {r['query_id']: render_variants(r) for r in prepared['queries']}
    texts = sorted({v[name] for v in variants.values() for name in policy['primary_embedding_variants']} |
        {c['representation_text'] for r in prepared['queries'] for c in r['candidates']})
    begin = time.monotonic()
    vectors, index = [], []
    for text in texts:
        vector, meta = embed(text)
        vectors.append(vector)
        index.append({'text': text, 'text_sha256': hashlib.sha256(text.encode()).hexdigest(), 'tokenization': meta})
    matrix = np.stack(vectors).astype(np.float32)
    output = HERE / 'first_embedding'
    output.mkdir(exist_ok=False)
    with (output / 'vectors.npy').open('xb') as out:
        np.save(out, matrix, allow_pickle=False)
    lookup = {text: i for i, text in enumerate(texts)}
    scores = {}
    for row in prepared['queries']:
        per_variant = {}
        for name in policy['primary_embedding_variants']:
            a = matrix[lookup[variants[row['query_id']][name]]].astype(np.float64)
            per_variant[name] = [float(np.dot(a, matrix[lookup[c['representation_text']]].astype(np.float64)))
                for c in row['candidates']]
        scores[row['query_id']] = per_variant
    elapsed = time.monotonic() - begin
    write_new(output / 'index.json', {'rows': index})
    metadata = {'schema': 'loom.research.retrieval_exploration_embedding/1', 'scores': scores,
        'model_id': instrument.MODEL_ID, 'revision': instrument.REVISION, 'dimensions': 384,
        'max_tokens': instrument.MAX_TOKENS, 'runtime': runtime, 'threads': env['threads'],
        'unique_texts': len(texts), 'truncated_texts': sum(r['tokenization']['truncated'] for r in index),
        'elapsed_encode_and_score_seconds': elapsed, 'initialization_excluded': True,
        'model_manifest_sha256': sha(CACHE / 'model_manifest.json'),
        'instrument_sha256': sha(STRUCTURE / 'local_embedding_panel.py'),
        'vector_sha256': sha(output / 'vectors.npy'), 'index_sha256': sha(output / 'index.json'),
        'training': False, 'new_download': False, 'paid_calls': 0}
    write_new(output / 'scores.json', metadata)
    return metadata


def compute_rows(prepared, original, embedding, policy):
    base_rows = {r['query_id']: r for r in original['rows']}
    result = []
    for row in prepared['queries']:
        candidates = row['candidates']
        original_row = base_rows[row['query_id']]
        if original_row['prefix_sha256'] != row['prefix_sha256']:
            raise ValueError('original_prefix_drift')
        if [c['turn_id'] for c in original_row['candidates']] != [c['turn_id'] for c in candidates]:
            raise ValueError('original_candidate_drift')
        scores = {name: [c['scores'][name] for c in original_row['candidates']]
            for name in original_row['rankings']}
        variants = render_variants(row)
        docs = [c['representation_text'] for c in candidates]
        for variant in policy['query_variants']:
            scores['bm25_' + variant] = bm25(variants[variant], docs,
                policy['bm25']['k1'], policy['bm25']['primary_b'])
        for b in policy['bm25']['b_controls']:
            scores['bm25_structured_b' + str(b)] = bm25(variants['structured'], docs, policy['bm25']['k1'], b)
        nodes = {n['id']: n for n in row['prefix_payload']['node_inventory']}
        s, t = nodes[row['query']['source']]['text'], nodes[row['query']['target']]['text']
        for bonus, name in ((0., 'endpoint_coverage'), (policy['direction_order_bonus'], 'endpoint_order')):
            scores[name] = [endpoint_coverage(s, t, c['text'], bonus) for c in candidates]
        for bonus in policy['speaker_soft_bonuses']:
            scores['bm25_speaker_soft_' + str(bonus)] = [base + (bonus if c['speaker'].casefold() ==
                row['query']['attributed_to'].casefold() else 0.) for base, c in zip(scores['bm25_structured'], candidates)]
        for variant, values in embedding['scores'][row['query_id']].items():
            scores['minilm_' + variant] = values
        scores['minilm_multi_max'] = [max(values) for values in zip(*[scores['minilm_' + v]
            for v in policy['multiquery_variants']])]
        rankings = {name: ranked_ids(candidates, values) for name, values in scores.items()}
        ids = [c['turn_id'] for c in candidates]
        for constant in policy['rrf_rank_constants']:
            name = 'rrf_original_' + str(constant)
            scores[name] = rrf(ids, [rankings[v] for v in ('lexical_token_cosine', 'character_3_5_cosine', 'learned_minilm_cosine')], constant)
            rankings[name] = ranked_ids(candidates, scores[name])
        for name, channels in (('rrf_bm25_minilm_60', ('bm25_structured', 'learned_minilm_cosine')),
            ('rrf_multiquery_60', tuple('minilm_' + v for v in policy['multiquery_variants'])),
            ('rrf_bm25_multiquery_60', tuple('bm25_' + v for v in policy['multiquery_variants']))):
            scores[name] = rrf(ids, [rankings[v] for v in channels], 60)
            rankings[name] = ranked_ids(candidates, scores[name])
        result.append({'case_id': row['case_id'], 'query_id': row['query_id'], 'language': row['language'],
            'prefix_sha256': row['prefix_sha256'], 'as_of': row['query']['as_of'], 'variants': variants,
            'future_excluded_turn_count': row['future_excluded_turn_count'], 'rankings': rankings,
            'candidates': [{k: c[k] for k in ('source_id', 'turn_id', 'known_at', 'text_sha256', 'eligible_order', 'span')} |
                {'scores': {name: values[i] for name, values in scores.items()}} for i, c in enumerate(candidates)],
            'source_judgment_available': False, 'prediction': None, 'retrieval_available': bool(candidates)})
    return result


def freeze():
    names = [HERE / 'policy.json', HERE / 'explore.py', HERE / 'evaluate.py', HERE / 'test_explore.py',
        DOCS / 'PROTOCOL.md', BASE / 'prepared_inputs.json', BASE / 'first_scores.json', BASE / 'evaluate_dev.py',
        STRUCTURE / 'graph_panel_live.py', CACHE / 'model_manifest.json', STRUCTURE / 'local_embedding_panel.py']
    write_new(HERE / 'freeze_before_scores2.json', {'schema': 'loom.research.retrieval_exploration_freeze/1',
        'created_at': datetime.now(timezone.utc).isoformat(), 'files_sha256': {str(p.relative_to(ROOT)): sha(p) for p in names},
        'gold_access': 'existing_dev_outcomes_seen_but_no_new_gold_feature_inputs', 'validation_accessed': False})


def run():
    frozen = json.loads((HERE / 'freeze_before_scores2.json').read_text())
    for name, value in frozen['files_sha256'].items():
        if sha(ROOT / name) != value:
            raise ValueError('frozen_dependency_drift:' + name)
    policy = json.loads((HERE / 'policy.json').read_text())
    prepared = json.loads((BASE / 'prepared_inputs.json').read_text())
    original = json.loads((BASE / 'first_scores.json').read_text())
    if prepared['split'] != 'dev' or prepared['query_count'] != 96:
        raise ValueError('only_frozen_dev96')
    embedding = embedding_run(prepared, policy)
    rows = compute_rows(prepared, original, embedding, policy)
    write_new(HERE / 'first_scores.json', {'schema': 'loom.research.retrieval_exploration_scores/1',
        'split': 'dev', 'prepared_sha256': sha(BASE / 'prepared_inputs.json'), 'original_scores_sha256': sha(BASE / 'first_scores.json'),
        'embedding_scores_sha256': sha(HERE / 'first_embedding/scores.json'), 'queries': len(rows),
        'methods': sorted(rows[0]['rankings']), 'rows': rows, 'source_judgment_available': False, 'no_graph_promotion': True})
    write_new(HERE / 'freeze_before_gold.json', {'created_at': datetime.now(timezone.utc).isoformat(),
        'scores_sha256': sha(HERE / 'first_scores.json'), 'vectors_sha256': sha(HERE / 'first_embedding/vectors.npy'),
        'index_sha256': sha(HERE / 'first_embedding/index.json'), 'validation_accessed': False})


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', choices=('freeze', 'run'))
    args = parser.parse_args()
    globals()[args.stage]()
