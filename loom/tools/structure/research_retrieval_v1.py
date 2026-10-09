"""Source-bound local retrieval research, independent of answer generation.

Operators are extensible callables. Named methods, compositions and selection
policies are caller data. No model downloads, API calls or profile adoption.
"""
from __future__ import annotations

import argparse
from collections import Counter, deque, defaultdict
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
import sys
import time
import tracemalloc

from loom.tools.structure.experiment_context_preparation_v1 import text_projection
from loom.tools.structure.graph_local_baselines_v1 import local_baselines as baseline
from loom.tools.structure.retrieval_exploration_v1 import explore

VERSION = 'loom.research_retrieval/1'


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()


def sha(value):
    return hashlib.sha256(value).hexdigest()


def write_new(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(canonical(value) + b'\n')


def load_source(path):
    """Whole native messages, no inferred edges or silently truncated text."""
    path = Path(path)
    raw = path.read_bytes()
    source = json.loads(raw)
    records = []
    for message in source['messages']:
        projection = text_projection(message['native_message'])
        records.append({
            'message_id': message['message_id'], 'node_id': message['node_id'],
            'role': message['role'], 'text': '\n'.join(projection['parts']),
            'parent_ids': message['parent_ids'], 'child_ids': message['child_ids'],
            'created_at': message['created_at'], 'source_pointer': message['source_pointer'],
            'projection': projection, 'family_id': source['family_id'],
        })
    if len({r['message_id'] for r in records}) != len(records):
        raise ValueError('duplicate_message_identity')
    records.sort(key=lambda r: (str(r['created_at']), r['message_id']))
    return source, records, sha(raw)


def ranked(records, scores, min_score=0.0):
    if len(records) != len(scores) or any(not math.isfinite(s) for s in scores):
        raise ValueError('score_inventory_invalid')
    return [r for r, s in sorted(zip(records, scores), key=lambda pair:
        (-pair[1], pair[0]['message_id'])) if s > min_score]


def context_record(record):
    return {k: record[k] for k in ('message_id', 'role', 'text')}


def select_context(ranking, max_bytes, max_records):
    if max_bytes < 2 or max_records < 1:
        raise ValueError('invalid_context_budget')
    selected, omitted, used = [], [], 2
    for record in ranking:
        size = len(canonical(context_record(record))) + bool(selected)
        if len(selected) >= max_records:
            omitted.append({'message_id': record['message_id'], 'reason': 'record_budget'})
        elif used + size > max_bytes:
            omitted.append({'message_id': record['message_id'], 'reason': 'whole_record_exceeds_remaining_byte_budget'})
        else:
            selected.append(record)
            used += size
    rendered = canonical([context_record(r) for r in selected])
    assert len(rendered) == used
    return {'records': selected, 'bytes': used, 'record_count': len(selected),
            'rendered_sha256': sha(rendered), 'omitted': omitted,
            'model_tokens': None, 'model_tokenizer': None,
            'regex_word_units': sum(sum(baseline.tokens(r['text']).values()) for r in selected)}


class Engine:
    """Operations are extensible without enumerating allowed method profiles."""
    def __init__(self, records, config):
        self.records, self.config = records, config
        self.operators = {}
        self.register('token_cosine', self._token)
        self.register('bm25', self._bm25)
        self.register('character_cosine', self._character)
        self.register('graph_distance', self._graph)
        self.register('weighted_rrf', self._rrf)
        self._tokens = [baseline.tokens(r['text']) for r in records]
        self._chars = {}
        self._adjacency = {}

    def register(self, name, callable_):
        if name in self.operators:
            raise ValueError('operator_already_registered')
        self.operators[name] = callable_

    def _token(self, task, params, prior):
        query = baseline.tokens(task['query'])
        return [baseline.cosine(query, value) or 0.0 for value in self._tokens]

    def _bm25(self, task, params, prior):
        return explore.bm25(task['query'], [r['text'] for r in self.records],
                            k1=params['k1'], b=params['b'])

    def _character(self, task, params, prior):
        bounds = tuple(params['ngram_range'])
        if len(bounds) != 2 or not 1 <= bounds[0] <= bounds[1]:
            raise ValueError('invalid_ngram_range')
        def representation(text):
            if bounds == (3, 5):
                return baseline.chars(text)
            value = ' '.join(text.casefold().split())
            return Counter((n, value[i:i+n]) for n in range(bounds[0], bounds[1]+1)
                           for i in range(len(value)-n+1))
        if bounds not in self._chars:
            self._chars[bounds] = [representation(r['text']) for r in self.records]
        query = representation(task['query'])
        return [baseline.cosine(query, value) or 0.0 for value in self._chars[bounds]]

    def _graph(self, task, params, prior):
        """Traverse explicit native adjacency only; does not infer semantics."""
        if params.get('unanchored') not in ('latest_observed_message', 'none'):
            raise ValueError('unsupported_unanchored_graph_policy')
        adjacency_policy = params.get('adjacency_policy', 'declared_fields')
        if adjacency_policy not in ('declared_fields', 'observed_parent_reciprocal'):
            raise ValueError('unsupported_native_adjacency_policy')
        by_node = {r['node_id']: r['message_id'] for r in self.records}
        by_message = {r['message_id']: r for r in self.records}
        adjacency = defaultdict(set)
        for record in self.records:
            for direction in params['directions']:
                for node in record[direction]:
                    if node in by_node:
                        adjacency[record['message_id']].add(by_node[node])
            if adjacency_policy == 'observed_parent_reciprocal' and 'child_ids' in params['directions']:
                for parent_node in record['parent_ids']:
                    if parent_node in by_node:
                        adjacency[by_node[parent_node]].add(record['message_id'])
        if 'seed_method' in params:
            seed_ranking = ranked(self.records, prior[params['seed_method']], params['seed_min_score'])
            seeds = [r['message_id'] for r in seed_ranking[:params['seed_count']]]
        elif task.get('anchor_message_id'):
            seeds = [task['anchor_message_id']]
            if seeds[0] not in by_message:
                raise ValueError('anchor_not_in_candidate_pool')
        elif params['unanchored'] == 'latest_observed_message':
            seeds = [self.records[-1]['message_id']] if self.records else []
        else:
            seeds = []
        distance, queue = {}, deque((s, 0) for s in seeds)
        while queue:
            ident, depth = queue.popleft()
            if ident in distance:
                continue
            distance[ident] = depth
            if depth < params['max_hops']:
                queue.extend((neighbor, depth+1) for neighbor in sorted(adjacency[ident]))
        return [params['decay'] ** distance[r['message_id']] if r['message_id'] in distance else 0.0
                for r in self.records]

    def _rrf(self, task, params, prior):
        if params['rank_constant'] <= 0:
            raise ValueError('invalid_rank_constant')
        result = dict.fromkeys((r['message_id'] for r in self.records), 0.0)
        for component in params['components']:
            ranking = ranked(self.records, prior[component['method']], component['min_score'])
            for rank, record in enumerate(ranking, 1):
                result[record['message_id']] += component['weight'] / (params['rank_constant'] + rank)
        return [result[r['message_id']] for r in self.records]

    def score(self, task, reuse=None):
        values, measurements = {}, {}
        dependencies = {}
        reuse = reuse or {}
        for method in self.config['methods']:
            ident = method['id']
            if ident in values or method['operator'] not in self.operators:
                raise ValueError('invalid_method_identity_or_unavailable_operator')
            if ident in reuse:
                scores, prior_measurement = reuse[ident]
                ranked(self.records,scores)
                values[ident] = scores
                measurements[ident] = dict(prior_measurement)
                measurements[ident]['operator_replayed_not_executed'] = True
                dependencies[ident] = {ident}
                continue
            tracemalloc.start()
            started = time.perf_counter()
            try:
                scores = self.operators[method['operator']](task, method['parameters'], values)
                elapsed = time.perf_counter() - started
                _, peak = tracemalloc.get_traced_memory()
            finally:
                tracemalloc.stop()
            ranked(self.records, scores)
            values[ident] = scores
            direct = ([method['parameters']['seed_method']] if 'seed_method' in method['parameters'] else [])
            direct += [c['method'] for c in method['parameters'].get('components', [])]
            dependencies[ident] = {ident}
            for dependency in direct:
                dependencies[ident].update(dependencies[dependency])
            measurements[ident] = {'operator_seconds': elapsed, 'python_peak_allocated_bytes': peak,
                'memory_kind': 'tracemalloc_python_allocations_not_process_RSS', 'operator_replayed_not_executed':False}
            measurements[ident]['method_total_seconds'] = sum(measurements[d]['operator_seconds'] for d in dependencies[ident])
            measurements[ident]['dependency_operator_count'] = len(dependencies[ident])
        return values, measurements


def evaluate(task, ranking, selected, evaluation):
    if any(not isinstance(k,int) or isinstance(k,bool) or k<1 for k in evaluation['ranking_cutoffs'] + evaluation['ndcg_cutoffs']):
        raise ValueError('invalid_evaluation_cutoff')
    if task['answerability'] not in ('answerable', 'unanswerable_in_source'):
        raise ValueError('unrecognized_answerability_contract')
    if any(e.get('reason') not in ('wrong_version', 'wrong_branch', 'wrong_attribution', 'other_annotated_distractor') for e in task.get('forbidden_evidence', [])):
        raise ValueError('unrecognized_distractor_reason_contract')
    evidence_sets = task.get('evidence_sets') or [[e['message_id'] for e in task['expected_evidence'] if e.get('required', True)]]
    relevant = {e['message_id'] for e in task['expected_evidence']}
    rank_ids = [r['message_id'] for r in ranking]
    selected_ids = {r['message_id'] for r in selected['records']}
    has_gold = bool(relevant)
    def recall(ids):
        valid = [set(group) for group in evidence_sets if group]
        return max((len(set(ids) & group) / len(group) for group in valid), default=None)
    metrics = {}
    for k in evaluation['ranking_cutoffs']:
        prefix = rank_ids[:k]
        metrics[f'precision_at_{k}'] = len(set(prefix) & relevant) / k if has_gold else None
        metrics[f'recall_at_{k}'] = recall(prefix)
    first = next((i for i, ident in enumerate(rank_ids, 1) if ident in relevant), None)
    metrics['reciprocal_rank'] = (1.0 / first if first else 0.0) if has_gold else None
    for k in evaluation['ndcg_cutoffs']:
        gains = sum(1/math.log2(i+1) for i, ident in enumerate(rank_ids[:k], 1) if ident in relevant)
        ideal = sum(1/math.log2(i+1) for i in range(1, min(k, len(relevant))+1))
        metrics[f'ndcg_at_{k}'] = gains / ideal if ideal else None
    metrics['context_evidence_recall'] = recall(selected_ids)
    metrics['context_all_required_evidence'] = (metrics['context_evidence_recall'] == 1.0) if has_gold else None
    metrics['context_precision'] = len(selected_ids & relevant)/len(selected_ids) if has_gold and selected_ids else None
    selected_text = {r['message_id']: r['text'] for r in selected['records']}
    quote_groups = [[e for e in task['expected_evidence'] if e['message_id'] in group and e.get('quote')]
                    for group in evidence_sets if group]
    metrics['required_quote_retention'] = max((sum(e['quote'] in selected_text.get(e['message_id'], '') for e in quotes)/len(quotes)
        for quotes in quote_groups if quotes), default=None)
    forbidden = task.get('forbidden_evidence', [])
    metrics['annotated_distractors_selected'] = sum(e['message_id'] in selected_ids for e in forbidden) if forbidden else None
    for kind in ('version', 'branch'):
        distractors = {e['message_id'] for e in forbidden if e.get('reason') == 'wrong_' + kind}
        metrics['annotated_' + kind + '_distractors_selected'] = len(distractors & selected_ids) if distractors else None
        metrics['annotated_' + kind + '_distractor_ranked_first'] = (bool(rank_ids) and rank_ids[0] in distractors) if distractors else None
    metrics['no_answer_retrieval_abstained'] = not bool(selected_ids) if task['answerability'] == 'unanswerable_in_source' else None
    metrics['context_bytes'] = selected['bytes']
    metrics['context_records'] = selected['record_count']
    metrics['model_tokens'] = None
    metrics['regex_word_units'] = selected['regex_word_units']
    metrics['answer_correctness'] = None
    return metrics


def budget_ceiling(task, records, budget):
    """Oracle packing diagnostic from frozen gold; never a retrieval input."""
    by_id = {r['message_id']:r for r in records}
    groups = task.get('evidence_sets') or [[e['message_id'] for e in task['expected_evidence'] if e.get('required',True)]]
    recalls, feasible = [], []
    for group in groups:
        if not group:
            continue
        sizes = sorted(len(canonical(context_record(by_id[ident]))) for ident in set(group))
        used, count = 2, 0
        for size in sizes:
            required = size + bool(count)
            if count < budget['max_records'] and used + required <= budget['max_bytes']:
                used += required
                count += 1
        recalls.append(count/len(sizes))
        feasible.append(count==len(sizes))
    return {'oracle_context_evidence_recall_ceiling':max(recalls) if recalls else None,
            'gold_complete_evidence_fits_budget':any(feasible) if feasible else None}


def aggregate(rows, evaluation):
    """Equal family weight, then task weight within family; no iid task CI."""
    groups = defaultdict(list)
    for row in rows:
        groups[(row['split'], row['method'], row['budget_id'])].append(row)
    summaries = []
    for (split, method, budget), group in sorted(groups.items()):
        metrics = {}
        for key in sorted({k for r in group for k in r['metrics']}):
            family_values = defaultdict(list)
            for row in group:
                value = row['metrics'].get(key)
                if isinstance(value, (float, int)):
                    family_values[row['family_id']].append(float(value))
            means = [statistics.mean(values) for values in family_values.values()]
            metrics[key] = {'family_macro_mean': statistics.mean(means) if means else None,
                            'families_observed': len(means), 'tasks_observed': sum(map(len, family_values.values()))}
        summaries.append({'split': split, 'method': method, 'budget_id': budget,
            'family_count': len({r['family_id'] for r in group}), 'task_count': len(group), 'metrics': metrics})
    pairs = []
    for split in sorted({r['split'] for r in rows}):
        for budget in sorted({r['budget_id'] for r in rows}):
            methods = sorted({r['method'] for r in rows})
            reference = evaluation['paired_reference_method']
            if reference not in methods:
                raise ValueError('paired_reference_not_observed')
            for method in [m for m in methods if m != reference]:
                left = {r['task_id']: r for r in rows if r['split'] == split and r['budget_id'] == budget and r['method'] == reference}
                right = {r['task_id']: r for r in rows if r['split'] == split and r['budget_id'] == budget and r['method'] == method}
                values = defaultdict(list)
                for ident in left.keys() & right.keys():
                    a, b = left[ident]['metrics'][evaluation['paired_metric']], right[ident]['metrics'][evaluation['paired_metric']]
                    if a is not None and b is not None:
                        values[left[ident]['family_id']].append(b-a)
                means = [statistics.mean(v) for v in values.values()]
                pairs.append({'split': split, 'budget_id': budget, 'reference': reference, 'method': method,
                    'metric': evaluation['paired_metric'], 'family_macro_paired_delta': statistics.mean(means) if means else None,
                    'families': len(means), 'paired_tasks': sum(map(len, values.values())), 'confidence_interval': None})
    return {'schema': VERSION, 'summaries': summaries, 'paired_comparisons': pairs,
            'unit_of_dependence': 'conversation_family', 'independent_holdout': False,
            'semantic_answer_quality_measured': False, 'llm_calls': 0, 'usd_cost': 0}


def error_analysis(tasks, rows, rankings):
    """Public projection uses task pseudonyms and classes, never source text/IDs."""
    by_task = {t['task_id']:t for t in tasks}
    by_rank = {(r['task_id'],r['method']):r for r in rankings}
    cases, counts = [], Counter()
    for row in rows:
        task = by_task[row['task_id']]
        relevant = {e['message_id'] for e in task['expected_evidence']}
        rank = by_rank[(row['task_id'],row['method'])]['ranking']
        omitted = {e['message_id']:e['reason'] for e in row['omitted']}
        classes = []
        if row['metrics']['context_evidence_recall'] is not None and row['metrics']['context_evidence_recall'] < 1:
            classes.append('required_evidence_incomplete')
            if any(omitted.get(i) == 'whole_record_exceeds_remaining_byte_budget' for i in relevant):
                classes.append('relevant_whole_message_did_not_fit_byte_budget')
            if any(omitted.get(i) == 'record_budget' for i in relevant):
                classes.append('relevant_message_beyond_record_budget')
            if any(i not in rank for i in relevant):
                classes.append('relevant_message_has_no_positive_score')
        if row['metrics']['no_answer_retrieval_abstained'] is False:
            classes.append('no_answer_task_nonempty_context_not_semantic_answer_error')
        for kind in ('version','branch'):
            if row['metrics']['annotated_'+kind+'_distractors_selected']:
                classes.append('context_contains_annotated_'+kind+'_distractor')
        if classes:
            cases.append({'task_id':row['task_id'],'category':row['category'],'split':row['split'],
                'method':row['method'],'budget_id':row['budget_id'],'classes':classes,
                'context_evidence_recall':row['metrics']['context_evidence_recall'],
                'context_bytes':row['metrics']['context_bytes'],'context_records':row['metrics']['context_records']})
        for category in classes:
            counts[(row['split'],row['method'],row['budget_id'],category)] += 1
    return {'schema':VERSION,'cases':cases,'counts':[
        {'split':s,'method':m,'budget_id':b,'class':c,'task_count':n}
        for (s,m,b,c),n in sorted(counts.items())],
        'interpretation':'retrieval failures and distractor inclusion; no LLM answer was generated or judged'}


def verify_freeze(path):
    """Require an immutable hash manifest written before methods are evaluated."""
    path = Path(path)
    freeze = json.loads(path.read_bytes())
    entries = freeze['files']
    if isinstance(entries, dict):
        entries = [{'path': k, **(v if isinstance(v, dict) else {'sha256': v})} for k,v in entries.items()]
    for entry in entries:
        relative = Path(entry['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('invalid_freeze_path')
        if sha((path.parent / relative).read_bytes()) != entry['sha256']:
            raise ValueError('frozen_input_mismatch')
    return sha(path.read_bytes())


def validate_reuse_dependencies(methods, reuse_ids):
    """A saved operator result is valid only with saved dependency closure."""
    by_id = {method['id']:method for method in methods}
    if len(by_id) != len(methods) or not set(reuse_ids) <= by_id.keys():
        raise ValueError('invalid_reuse_method_inventory')
    closures = {}
    def closure(ident, active):
        if ident not in by_id:
            raise ValueError('unavailable_method_dependency')
        if ident in active:
            raise ValueError('cyclic_method_dependency')
        if ident in closures:
            return closures[ident]
        params = by_id[ident]['parameters']
        direct = set(c['method'] for c in params.get('components', []))
        if 'seed_method' in params:
            direct.add(params['seed_method'])
        result = {ident}
        for dependency in direct:
            result.update(closure(dependency, active | {ident}))
        closures[ident] = result
        return result
    for ident in reuse_ids:
        if not closure(ident, set()) <= set(reuse_ids):
            raise ValueError('reused_method_depends_on_changed_upstream')


def run(tasks_path, methods_path, freeze_path, output, evaluation_path, reuse_previous=None, previous_methods_path=None):
    freeze_sha = verify_freeze(freeze_path)
    tasks_raw, methods_raw = Path(tasks_path).read_bytes(), Path(methods_path).read_bytes()
    frozen = json.loads(Path(freeze_path).read_bytes())['files']
    frozen_hashes = {v['sha256'] if isinstance(v, dict) else v for v in frozen.values()} if isinstance(frozen, dict) else {v['sha256'] for v in frozen}
    if sha(tasks_raw) not in frozen_hashes or sha(methods_raw) not in frozen_hashes:
        raise ValueError('task_and_method_inputs_must_be_frozen')
    tasks, config = json.loads(tasks_raw), json.loads(methods_raw)
    evaluation_raw = Path(evaluation_path).read_bytes()
    evaluation = json.loads(evaluation_raw)
    if sha(evaluation_raw) not in frozen_hashes:
        raise ValueError('evaluation_must_be_frozen_before_new_rankings')
    reuse_ids = set()
    if reuse_previous is not None:
        reuse_previous = Path(reuse_previous)
        verify_freeze(reuse_previous/'MANIFEST.json')
        previous_start=json.loads((reuse_previous/'START.json').read_bytes())
        previous_raw=Path(previous_methods_path).read_bytes()
        if previous_start['tasks_sha256'] != sha(tasks_raw) or previous_start['methods_sha256'] != sha(previous_raw):
            raise ValueError('reuse_previous_input_binding_mismatch')
        old_methods={m['id']:m for m in json.loads(previous_raw)['methods']}
        new_methods={m['id']:m for m in config['methods']}
        reuse_ids=set(config['reuse_method_ids'])
        changed_ids=set(config['changed_method_ids'])
        if reuse_ids & changed_ids or reuse_ids | changed_ids != new_methods.keys():
            raise ValueError('changed_and_reused_method_partition_invalid')
        if any(old_methods.get(ident) != new_methods[ident] for ident in reuse_ids):
            raise ValueError('changed_method_cannot_reuse_previous_ranking')
        validate_reuse_dependencies(config['methods'], reuse_ids)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    write_new(output/'START.json', {'schema': VERSION, 'tasks_sha256': sha(tasks_raw),
        'methods_sha256': sha(methods_raw), 'freeze_sha256': freeze_sha,
        'evaluation_sha256':sha(evaluation_raw),
        'replayed_method_ids':sorted(reuse_ids),
        'previous_manifest_sha256':sha((reuse_previous/'MANIFEST.json').read_bytes()) if reuse_previous else None,
        'python': platform.python_version(), 'implementation_sha256': sha(Path(__file__).read_bytes()),
        'reused_components':{'local_baselines':sha(Path(baseline.__file__).read_bytes()),
            'bm25_explore':sha(Path(explore.__file__).read_bytes()),
            'text_projection_module':sha(Path(sys.modules[text_projection.__module__].__file__).read_bytes())},
        'tokenizer': None, 'embedding_status': config['embedding_status'], 'new_paid_calls': 0})
    cache, rows, ranking_rows = {}, [], []
    for task in tasks['tasks']:
        cache_key = (task['source_path'], tuple(task.get('candidate_message_ids', [])))
        if task.get('candidate_scope', {}).get('mode', 'family_full') != 'family_full' and not task.get('candidate_message_ids'):
            raise ValueError('scoped_task_requires_explicit_candidate_inventory')
        if cache_key not in cache:
            source, records, source_sha = load_source(task['source_path'])
            if 'candidate_message_ids' in task:
                selected_ids = set(task['candidate_message_ids'])
                if len(selected_ids) != len(task['candidate_message_ids']) or not selected_ids <= {r['message_id'] for r in records}:
                    raise ValueError('invalid_candidate_inventory')
                records = [r for r in records if r['message_id'] in selected_ids]
            started = time.perf_counter()
            tracemalloc.start()
            engine = Engine(records, config)
            _, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()
            cache[cache_key] = source, records, source_sha, engine, time.perf_counter()-started, peak
        source, records, source_sha, engine, init_time, init_peak = cache[cache_key]
        if source_sha != task['source_sha256'] or source['family_id'] != task['family_id'] or source['split'] != task['split']:
            raise ValueError('source_binding_mismatch')
        expected_ids = {e['message_id'] for e in task['expected_evidence']}
        if not expected_ids <= {r['message_id'] for r in records}:
            raise ValueError('gold_evidence_outside_source')
        record_text = {r['message_id']:r['text'] for r in records}
        for evidence in task['expected_evidence']:
            if evidence.get('quote') and (sha(evidence['quote'].encode()) != evidence['quote_sha256'] or evidence['quote'] not in record_text[evidence['message_id']]):
                raise ValueError('gold_quote_source_binding_mismatch')
        pool_hash = sha(canonical([{'message_id':r['message_id'],'text_sha256':sha(r['text'].encode())} for r in records]))
        reuse = {}
        if reuse_previous is not None:
            previous_task=json.loads((reuse_previous/'tasks'/f"{task['task_id']}.json").read_bytes())
            old_rankings={r['method']:r for r in previous_task['rankings']}
            for ident in reuse_ids:
                old=old_rankings[ident]
                if old['candidate_pool_sha256'] != pool_hash:
                    raise ValueError('reused_ranking_candidate_pool_mismatch')
                lookup=dict(old['scores'])
                if lookup.keys() != {r['message_id'] for r in records}:
                    raise ValueError('reused_score_inventory_mismatch')
                reuse[ident]=([lookup[r['message_id']] for r in records],old['measurement'])
        scores, measurements = engine.score(task,reuse)
        for method in config['methods']:
            ident = method['id']
            ranking = ranked(records, scores[ident], method['selection']['min_score'])
            ranking_rows.append({'task_id': task['task_id'], 'method': ident,
                'candidate_pool_sha256': pool_hash, 'scores': list(zip([r['message_id'] for r in records], scores[ident])),
                'ranking': [r['message_id'] for r in ranking], 'measurement': measurements[ident]})
            for budget in config['budgets']:
                started = time.perf_counter()
                selected = select_context(ranking, budget['max_bytes'], budget['max_records'])
                select_time = time.perf_counter()-started
                metrics = evaluate(task, ranking, selected, evaluation)
                metrics.update(budget_ceiling(task,records,budget))
                metrics.update(measurements[ident])
                metrics['selection_seconds'] = select_time
                metrics['family_index_seconds_shared'] = init_time
                metrics['family_index_python_peak_bytes_shared'] = init_peak
                rows.append({'task_id': task['task_id'], 'family_id': task['family_id'], 'split': task['split'],
                    'category': task['category'], 'method': ident, 'budget_id': budget['id'],
                    'candidate_scope':task.get('candidate_scope', {'mode':'family_full'}),
                    'source_sha256': source_sha, 'candidate_pool_sha256': pool_hash,
                    'candidate_records': len(records), 'candidate_text_bytes': sum(len(r['text'].encode()) for r in records),
                    'metrics': metrics, 'selected_message_ids': [r['message_id'] for r in selected['records']],
                    'context_sha256': selected['rendered_sha256'], 'omitted': selected['omitted']})
        write_new(output/'tasks'/f"{task['task_id']}.json", {'task_id':task['task_id'],
            'rankings':[r for r in ranking_rows if r['task_id']==task['task_id']],
            'measurements':[r for r in rows if r['task_id']==task['task_id']]})
    summary = aggregate(rows, evaluation)
    summary.update({'tasks_sha256':sha(tasks_raw), 'methods_sha256':sha(methods_raw), 'freeze_sha256':freeze_sha,
        'rows':len(rows), 'tasks':len(tasks['tasks']), 'families':len({t['family_id'] for t in tasks['tasks']}), 'methods':len(config['methods']),
        'budgets':config['budgets'], 'raw_rows_sha256':sha(canonical(rows)+b'\n')})
    summary['new_method_executions']=len(tasks['tasks'])*(len(config['methods'])-len(reuse_ids))
    summary['replayed_method_rankings']=len(tasks['tasks'])*len(reuse_ids)
    summary['timing_note']='dependency totals mix saved unchanged operator measurements with newly measured corrected operators' if reuse_ids else 'all operators measured in this run'
    write_new(output/'rows.json',rows)
    write_new(output/'summary.json',summary)
    write_new(output/'errors.json',error_analysis(tasks['tasks'],rows,ranking_rows))
    payloads=[{'path':str(p.relative_to(output)),'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size}
              for p in sorted(output.rglob('*')) if p.is_file()]
    write_new(output/'MANIFEST.json',{'schema':VERSION,'files':payloads,'new_paid_calls':0,'usd_cost':0})
    return summary


def reanalyze_saved(tasks_path, methods_path, freeze_path, previous, output, evaluation_path):
    """Replay immutable observed rankings, no method execution or new timing."""
    verify_freeze(freeze_path)
    previous, output = Path(previous), Path(output)
    prior_manifest = previous/'MANIFEST.json'
    verify_freeze(prior_manifest)
    tasks, config = json.loads(Path(tasks_path).read_bytes()), json.loads(Path(methods_path).read_bytes())
    evaluation_raw=Path(evaluation_path).read_bytes()
    evaluation=json.loads(evaluation_raw)
    start = json.loads((previous/'START.json').read_bytes())
    if sha(Path(tasks_path).read_bytes()) != start['tasks_sha256'] or sha(Path(methods_path).read_bytes()) != start['methods_sha256']:
        raise ValueError('replay_frozen_binding_mismatch')
    output.mkdir(parents=True,exist_ok=False)
    write_new(output/'evaluation.json',evaluation)
    write_new(output/'START.json', {'schema':VERSION,'analysis_revision':3,'parent_manifest_sha256':sha(prior_manifest.read_bytes()),
        'tasks_sha256':start['tasks_sha256'],'methods_sha256':start['methods_sha256'],
        'implementation_sha256':sha(Path(__file__).read_bytes()),
        'evaluation_sha256':sha(evaluation_raw),
        'new_rankings':0,'new_timing_measurements':0,'new_paid_calls':0,
        'changes':['empty_selected_context_precision_is_unknown','oracle_whole_message_budget_feasibility','evaluation_cutoffs_and_pairing_reference_are_versioned_data_no_retuning']})
    rows, ranking_rows = [], []
    methods = {m['id']:m for m in config['methods']}
    budgets = {b['id']:b for b in config['budgets']}
    for task in tasks['tasks']:
        source, records, source_sha = load_source(task['source_path'])
        if source_sha != task['source_sha256']:
            raise ValueError('replay_source_hash_mismatch')
        if 'candidate_message_ids' in task:
            ids=set(task['candidate_message_ids'])
            records=[r for r in records if r['message_id'] in ids]
        saved=json.loads((previous/'tasks'/f"{task['task_id']}.json").read_bytes())
        per_method={r['method']:r for r in saved['rankings']}
        ranking_rows.extend(saved['rankings'])
        for row in saved['measurements']:
            scores=dict(per_method[row['method']]['scores'])
            ranking=ranked(records,[scores[r['message_id']] for r in records],methods[row['method']]['selection']['min_score'])
            if [r['message_id'] for r in ranking] != per_method[row['method']]['ranking']:
                raise ValueError('replay_ranking_drift')
            budget=budgets[row['budget_id']]
            selected=select_context(ranking,budget['max_bytes'],budget['max_records'])
            if selected['rendered_sha256'] != row['context_sha256']:
                raise ValueError('replay_context_drift')
            row['metrics'].update(evaluate(task,ranking,selected,evaluation))
            row['metrics'].update(budget_ceiling(task,records,budget))
            rows.append(row)
    summary=aggregate(rows,evaluation)
    summary.update({'analysis_revision':3,'parent_manifest_sha256':sha(prior_manifest.read_bytes()),
        'tasks_sha256':start['tasks_sha256'],'methods_sha256':start['methods_sha256'],
        'tasks':len(tasks['tasks']),'families':len({t['family_id'] for t in tasks['tasks']}),
        'methods':len(methods),'rows':len(rows),'budgets':config['budgets'],
        'raw_rows_sha256':sha(canonical(rows)+b'\n'),'new_rankings':0,'new_timing_measurements':0})
    summary['evaluation_sha256']=sha(evaluation_raw)
    write_new(output/'rows.json',rows)
    write_new(output/'summary.json',summary)
    write_new(output/'errors.json',error_analysis(tasks['tasks'],rows,ranking_rows))
    files=[{'path':str(p.relative_to(output)),'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size}
           for p in sorted(output.rglob('*')) if p.is_file()]
    write_new(output/'MANIFEST.json',{'schema':VERSION,'files':files,'new_paid_calls':0,'usd_cost':0})
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tasks', required=True)
    parser.add_argument('--methods', required=True)
    parser.add_argument('--freeze', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--evaluation', required=True)
    parser.add_argument('--reuse-previous')
    parser.add_argument('--previous-methods')
    args = parser.parse_args()
    result = run(args.tasks,args.methods,args.freeze,args.output,args.evaluation,args.reuse_previous,args.previous_methods)
    print(json.dumps({k:result[k] for k in ('tasks','families','methods','rows','new_paid_calls') if k in result}))
