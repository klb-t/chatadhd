"""Project existing measured DEV scores; offline, no gold/validation/API access.

Only the explicit first-result allowlist below is read. Profile fragments use
the unchanged T4 schema and arithmetic helpers; the richer envelope is research
data, not a canonical ModelProfile store or a replacement contract.
"""
from __future__ import annotations

from collections import Counter
from copy import deepcopy
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics
import subprocess
import sys
import zipfile

from jsonschema import Draft202012Validator, FormatChecker

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('existing_t4_profiles', ROOT / 'loom/tools/eval/model_profiles.py')
t4 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t4)
CONTEXT = 'docs/research/model_method_panel_v1/jev_context'
GRAPH = 'docs/research/graph_method_panel_v1'
PAIRS = 'docs/research/model_method_panel_v1/gpt41mini_same_pairs'
T3 = 'docs/research/jev_recipes_first32_2026-09-30'


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def git(*args):
    result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, check=False)
    return result.stdout if result.returncode == 0 else None


class Sources:
    def __init__(self, original=None):
        self.items = {}
        self.original = original or {}

    def read(self, ident, path, member=None):
        assert not any(part in path for part in ('key-handoff', 'real-holdout-key', 'gold_', 'gold.json', 'validation'))
        container = (ROOT / path).read_bytes()
        if member:
            with zipfile.ZipFile(ROOT / path) as archive:
                raw = archive.read(member)
        else:
            raw = container
        head_raw = git('show', 'HEAD:' + path)
        same_as_head = head_raw is not None and head_raw == container
        commit = git('log', '-1', '--format=%H', '--', path) if same_as_head else None
        row = {'id': ident, 'path': path, 'member': member, 'sha256': sha(raw),
               'source_commit': commit.decode().strip() if commit else None,
               'source_commit_status': 'committed_container_bytes_match' if commit else 'current_bytes_not_committed',
               'container_sha256': sha(container) if member else None,
               'git_blob_sha_current_container': hashlib.sha1(b'blob ' + str(len(container)).encode() + b'\0' + container).hexdigest(),
               'location_kind': 'zip_member' if member else 'repository_file'}
        if ident in self.original:
            before = self.original[ident]
            assert before['sha256'] == row['sha256'], 'first source content changed: ' + ident
            # Commit/status metadata describes the original projection snapshot;
            # a later commit must not silently rewrite that first known state.
            row = deepcopy(before)
        self.items[ident] = row
        return t4.load(ROOT / path) if not member else json.loads(raw)


def execution(sources, ident, bases, requested_model, requested_provider, jev=False):
    attempts = []
    ledger_refs = []
    returned_models, returned_providers = Counter(), Counter()
    raw_identity_refs = []
    for index, base in enumerate(bases, 1):
        ledger_id = ident + '.ledger' + str(index)
        ledger = sources.read(ledger_id, base + '/run/ledger.json')
        ledger_refs.append(ledger_id)
        for attempt in ledger['attempts']:
            attempts.append(attempt)
            if jev:
                if attempt.get('model'):
                    returned_models[attempt['model']] += 1
                if attempt.get('provider'):
                    returned_providers[attempt['provider']] += 1
            elif attempt['state'] == 'completed':
                # Read model/provider identity only, not reinterpretation/scoring.
                raw = (ROOT / base / 'run' / attempt['response_file']).read_bytes()
                assert sha(raw) == attempt['response_sha256']
                response = json.loads(raw)
                returned_models[response.get('model')] += 1
                returned_providers[response.get('provider')] += 1
                raw_identity_refs.append({'path': base + '/run/' + attempt['response_file'],
                                          'sha256': sha(raw)})
    return summarize_execution(ident, attempts, ledger_refs, requested_model,
                               requested_provider, jev, returned_models,
                               returned_providers, raw_identity_refs)


def summarize_execution(ident, attempts, refs, requested_model, requested_provider,
                        jev, returned_models=None, returned_providers=None, identity_refs=None):
    models = returned_models if returned_models is not None else Counter(a.get('model') for a in attempts if a.get('model'))
    providers = returned_providers if returned_providers is not None else Counter(a.get('provider') for a in attempts if a.get('provider'))
    elapsed = [a['elapsed_seconds'] for a in attempts if a.get('elapsed_seconds') is not None]
    known_costs = [Decimal(a['reported_cost_usd']) for a in attempts if a.get('reported_cost_usd') is not None]
    unknown_costs = sum(a.get('reported_cost_usd') is None for a in attempts)
    generation = Counter(str(a.get('generation_billing', {}).get('audit_status', 'not_measured')) for a in attempts)
    start = min(a['started_at'] for a in attempts)
    finish = max(a['finished_at'] for a in attempts)
    def instant(value):
        return datetime.fromisoformat(value.replace('Z', '+00:00'))
    wall_sum = sum((instant(a['finished_at']) - instant(a['started_at'])).total_seconds() for a in attempts)
    return {'id': ident, 'ledger_source_ids': refs, 'attempted_requests': len(attempts),
            'attempt_state_counts': dict(Counter(a['state'] for a in attempts)),
            'http_status_counts': dict(Counter(str(a.get('http_status')) for a in attempts)),
            'instrument': {'requested_model': requested_model, 'requested_provider': requested_provider,
                           'returned_model_counts': dict(models), 'returned_provider_counts': dict(providers),
                           'returned_identifier_resolution': 'reported_dated_identifier_not_verified_weights' if jev and models else 'reported_unversioned_alias_not_immutable_snapshot' if models else 'no_returned_identifier',
                           'immutable_weights_snapshot_verified': False,
                           'returned_identifier_source': 'saved_ledger' if jev else 'hash_checked_first_response_metadata',
                           'identity_response_refs': identity_refs or []},
            'source_response_interval': {'first_started_at': start, 'last_finished_at': finish},
            'cost': {'known_usage_total_usd': str(sum(known_costs, Decimal(0))),
                     'known_usage_cost_attempts': len(known_costs), 'unknown_usage_cost_attempts': unknown_costs,
                     'total_invoice_cost_usd': None, 'total_invoice_cost_status': 'not_independently_verified',
                     'generation_audit_status_counts': dict(generation),
                     'generation_audit_independently_verified_total_usd': None,
                     'per_question_cost_usd': None,
                     'shared_key_balance_delta_attribution': 'unavailable; concurrent arms must not be assigned the same delta'},
            'latency': {'recorded_request_seconds_sum': sum(elapsed), 'recorded_request_seconds_count': len(elapsed),
                        'recorded_request_seconds_mean': statistics.mean(elapsed),
                        'recorded_request_seconds_median': statistics.median(elapsed),
                        'recorded_request_seconds_min': min(elapsed), 'recorded_request_seconds_max': max(elapsed),
                        'sum_attempt_finished_minus_started_seconds': wall_sum,
                        'request_timer_scope': 'POST plus local safety checks; excludes generation/key GET' if jev else 'frozen runner per-request elapsed timer; no hardware-controlled comparison',
                        'non_request_timer_residual_sum_seconds': wall_sum - sum(elapsed),
                        'pure_generation_get_seconds': None, 'pure_key_get_seconds': None,
                        'per_question_seconds': None,
                        'end_to_end_instrument_latency_seconds': None}}


def ratio(n, d, source, location, note):
    return t4.ratio(n, d, source, location, note)


def binary_metrics(counts, source, pointer, planned=None):
    tp, fp, fn, tn = (counts[k] for k in ('tp', 'fp', 'fn', 'tn'))
    total = planned if planned is not None else tp + fp + fn + tn
    return {'positive_precision': ratio(tp, tp + fp, source, pointer, 'TP / predicted positives; independent precision denominator.'),
            'positive_recall': ratio(tp, tp + fn, source, pointer, 'TP / gold positives; missing positives remain FN.'),
            'accuracy_all_planned': ratio(tp + tn, total, source, pointer, 'Correct threshold decisions / all planned decisions, not world truth.')}


def profile(pid, batch, operation, question, recipe_id, recipe_source, recipe_location,
            planned, available, unit, shape, metrics, domain):
    identity = batch['instrument']
    returned = list(identity['returned_model_counts'])
    assert len(returned) <= 1, 'different returned versions require distinct profiles'
    version = returned[0] if returned else None
    p = t4.base_profile(pid, identity['requested_model'], version,
        next(iter(identity['returned_provider_counts']), identity['requested_provider']),
        operation, question, recipe_id, recipe_source, recipe_location, planned,
        available, batch['source_response_interval']['last_finished_at'][:10], domain, shape)
    p['population']['unit'] = unit
    p['population']['dependent_observations'] = 'Authored DEV; related source families, translations, causal views and candidate decisions share observations. Not independent natural-text trials.'
    p['metrics'] = metrics
    p['cost'] = {'reported_total_usd': None, 'allocation': 'shared_batch_only',
                 'batch_ref': batch['id'], 'note': 'Exact request-level usage totals and unknown billing states are in the research batch. No independent per-question charge allocated.'}
    p['latency'] = {'per_question_seconds': None, 'batch_ref': batch['id'],
                    'note': 'Recorded request timer and interval definitions are in the research batch. Do not divide multiquestion duration by question count.'}
    p['calibration']['reason'] = 'Empirical DEV Brier/ECE, when present, are diagnostics for this task/recipe/population. No transferred calibration or certified model reliability.'
    p['promotion']['checks'] = ['No automatic graph promotion; source relevance/judgment is not content truth.',
        'Keep task, representation, recipe, population and model/provider identifiers distinct.',
        'Freeze any compensation or next population before new outcomes; no inherited reliability.']
    return p


def build(original=None):
    sources = Sources({s['id']: s for s in original['sources']} if original else None)
    context = sources.read('context.score', CONTEXT + '/first_score.json')
    context_manifest = sources.read('context.recipe', CONTEXT + '/prepared/offline_manifest.json')
    context_live = sources.read('context.live_manifest', CONTEXT + '/live-prepared/manifest.json')
    graph = sources.read('graph.score', GRAPH + '/aggregate_first.json')
    graph_freeze = sources.read('graph.freeze', GRAPH + '/FREEZE.json')
    graph_jev = sources.read('graph.jev_recipe', GRAPH + '/jev_judge_batch01/prepared/manifest.json')
    graph_v2 = sources.read('graph.gpt_v2_recipe', GRAPH + '/gpt_judge_v2_batch01/prepared/manifest.json')
    graph_extraction = sources.read('graph.extraction_recipe', GRAPH + '/extraction/prepared/manifest.json')
    graph_v1 = sources.read('graph.gpt_v1_recipe', GRAPH + '/gpt_judge_batch01/prepared/manifest.json')
    pairs = sources.read('pairs.score', PAIRS + '/score.json')
    pair_manifest = sources.read('pairs.recipe', PAIRS + '/manifest.json')
    t3_score = sources.read('t3.score', T3 + '/first_score.json')
    t3_manifest = sources.read('t3.recipe', T3 + '/first_evidence.zip', 'prepared/manifest.json')
    t3_ledger = sources.read('t3.ledger', T3 + '/first_evidence.zip', 'run/ledger.json')

    batches = {}
    batches['context48'] = execution(sources, 'context48', [CONTEXT], 'typesafe/jev-1.13', 'typesafe', True)
    batches['graph_jev96'] = execution(sources, 'graph_jev96', [GRAPH + '/jev_judge_batch01', GRAPH + '/jev_judge_batch02'], 'typesafe/jev-1.13', 'typesafe', True)
    batches['graph_gpt_v2_96'] = execution(sources, 'graph_gpt_v2_96', [GRAPH + '/gpt_judge_v2_batch01', GRAPH + '/gpt_judge_v2_batch02'], 'openai/gpt-4.1-mini', 'openai')
    batches['graph_extraction24'] = execution(sources, 'graph_extraction24', [GRAPH + '/extraction'], 'openai/gpt-4.1-mini', 'openai')
    batches['graph_gpt_v1_failed'] = execution(sources, 'graph_gpt_v1_failed', [GRAPH + '/gpt_judge_batch01'], 'openai/gpt-4.1-mini', 'openai')
    batches['pairs_gpt48'] = execution(sources, 'pairs_gpt48', [PAIRS], 'openai/gpt-4.1-mini', 'openai')
    batches['t3_first32'] = summarize_execution('t3_first32', t3_ledger['attempts'], ['t3.ledger'], 'typesafe/jev-1.13', 'typesafe', True)
    batches['graph_gpt_v1_failed']['planned_requests'] = 48
    batches['graph_gpt_v1_failed']['never_attempted_requests'] = 47
    batches['graph_gpt_v1_failed']['cost']['unknown_cost_reservation_usd'] = graph['retained_unknown_cost_reservation_usd']
    assert batches['graph_gpt_v1_failed']['cost']['unknown_usage_cost_attempts'] == 1
    assert batches['context48']['cost']['known_usage_total_usd'] == context['reported_cost_usd']
    assert batches['pairs_gpt48']['cost']['known_usage_total_usd'] == pairs['reported_cost_usd']
    assert batches['t3_first32']['cost']['known_usage_total_usd'] == t3_score['status']['reported_cost_usd']

    profiles, detail = [], {}
    def add(p, recipe_hash, corpus, results, notes, failure_modes=(), recipe_hash_basis='recorded_frozen_prompt_or_manifest_hash'):
        p['failure_modes'] = list(failure_modes)
        profiles.append(p)
        detail[p['id']] = {'recipe_sha256': recipe_hash, 'recipe_hash_basis': recipe_hash_basis, 'corpus': corpus,
            'measurements': results, 'limitations': notes,
            'measurement_known_at': None,
            'measurement_known_at_status': 'exact_first_score_publication_time_not_recorded',
            'source_response_interval': batches[p['cost']['batch_ref']]['source_response_interval'],
            'content_truth_accuracy': None, 'canonical': False}
    def failure(kind, status, description, count, source, location):
        return {'kind': kind, 'status': status, 'description': description, 'count': count,
                'evidence': {'source_id': source, 'location': location}}

    context_corpus = {'id': 'context_delta_v1', 'split': 'development', 'blind': False,
        'conversations': 8, 'queries': 48, 'supplied_candidates': True, 'causal_prefix': True,
        'labels_file_read_by_projection': False, 'development_labels_hash_recorded': context['development_labels_hash'],
        'frozen_source_hashes': context_manifest['source_files_sha256'], 'graph_projection': context_manifest['projection']}
    for kind, operation, pid in [('membership', 'supplied_topic_needed_for_next_answer', 'context.topic_needed'),
                                  ('selected_claim', 'supplied_claim_context_selection', 'context.claim_selection')]:
        v = context['overall']['bits'][kind]
        ptr = '/overall/bits/' + kind
        metrics = binary_metrics(v['all_query_counts'], 'context.score', ptr, v['planned'])
        exact = context['overall']['exact_sets'][kind]
        metrics['exact_set_all_queries'] = ratio(exact['correct'], exact['planned'], 'context.score', '/overall/exact_sets/' + kind, 'All 48 set queries; empty candidate sets remain visible separately.')
        metrics['brier_dev'] = t4.metric(v['brier_valid_outputs'], None, v['available'], 'reported_scalar', 'context.score', ptr + '/brier_valid_outputs', 'Existing frozen scorer output, not calibration certification.', 'mean_squared_error')
        select = v['selective']
        positives = v['all_query_counts']['tp'] + v['all_query_counts']['fn']
        metrics['selective_bit_coverage'] = ratio(select['retained'], v['planned'], 'context.score', ptr + '/selective', 'Predeclared p<=.2 or p>=.8; abstention is not correction.')
        metrics['selective_positive_coverage'] = ratio(select['counts']['tp'] + select['counts']['fn'], positives, 'context.score', ptr + '/selective', 'Retained gold positives / all gold positives; do not call conditional recall full recall.')
        p = profile(pid, batches['context48'], operation, kind, 'jev_context_v1/' + kind,
            'context.recipe', '/prompt_sha256 + /question_map_sha256', v['planned'], v['available'],
            'candidate relevance bits', 'Causal latest-message/current-request prefix plus supplied topic/Claim candidates; independently named Noul questions.', metrics, ['authored context DEV', 'pl', 'en'])
        add(p, context_manifest['prompt_sha256'], context_corpus,
            {'bits': v, 'exact_sets': exact, 'all_query_joint_exact_set': context['overall']['joint_exact_set'],
             'claim_candidate_subset': context['overall']['claim_candidate_subset'], 'empty_claim_queries': context['overall']['no_claim_candidates'],
             'question_map_sha256': context_manifest['question_map_sha256'], 'scorer_sha256': context['score_code_sha256']},
            ['Candidate selection, not free extraction or archive retrieval.', 'Selecting an unknown-time Claim does not establish a historical prior or world truth.', 'Half of the 48 queries have no eligible Claim; nonempty subset remains separate.'],
            [failure('candidate_false_positive', 'reported_observation', 'Frozen .5 selector over-selected supplied candidates; inspect original error IDs/source prefixes.', v['all_query_counts']['fp'], 'context.score', '/errors')])

    graph_corpus = {'id': 'graph_methods_panel_v1', 'split': 'dev', 'blind': False,
        'conversations': 24, 'queries': 96, 'families': 4, 'languages': ['en', 'pl'],
        'gold_class_counts': graph['gold_class_counts'], 'supplied_node_inventory': True,
        'labels_file_read_by_projection': False, 'sealed_validation_accessed': False,
        'fixture_manifest_sha256_recorded': graph_freeze['fixture_manifest_sha256']}
    for name, batch_id, recipe_source, recipe_hash in [
        ('jev_dual_noul', 'graph_jev96', 'graph.jev_recipe', sha(canonical(graph_jev['requests'][0]['body']['questions']))),
        ('gpt_ternary_v2', 'graph_gpt_v2_96', 'graph.gpt_v2_recipe', graph_v2['metadata']['recipe_sha256'])]:
        v = graph['supplied_edge_judgment'][name]
        ptr = '/supplied_edge_judgment/' + name
        metrics = {'accuracy_all_planned': ratio(v['correct'], v['query_count'], 'graph.score', ptr, 'All 96 planned source-predicate judgments; conflicts counted unavailable.'),
                   'semantic_label_coverage': ratio(v['available'], v['query_count'], 'graph.score', ptr, 'Semantic ternary availability, separate from completed HTTP responses.')}
        for label, counts in v['per_class'].items():
            metrics[label + '_precision'] = ratio(counts['tp'], counts['predicted'], 'graph.score', ptr + '/per_class/' + label, 'TP / predicted ' + label + '; unavailable does not become unknown.')
            metrics[label + '_recall'] = ratio(counts['tp'], counts['gold'], 'graph.score', ptr + '/per_class/' + label, 'TP / gold ' + label + '; unavailable remains in FN.')
        p = profile('graph.' + name, batches[batch_id], 'supplied_directed_source_relation_judgment',
            None, 'graph_methods_panel_v1/' + name, recipe_source, '/requests/0/body', v['query_count'], v['available'],
            'planned semantic source-relation labels; conflicts unavailable',
            'Exact supplied relation/endpoints, speaker attribution, as_of physical source prefix; dual Noul channels or ternary JSON.', metrics,
            ['authored source-graph DEV', 'pl', 'en'])
        failures = [failure('source_predicate_mismatch', 'reported_observation', 'Observed disagreement with fixed attributed/directed/temporal source gold, not content truth.', len(v['failures']), 'graph.score', ptr + '/failures')]
        if name == 'jev_dual_noul':
            failures.append(failure('dual_channel_semantic_conflict', 'reported_observation', 'Both support/refute channels positive; retained conflict, not transport loss or a fabricated unknown label.', len(v['conflicting_query_ids']), 'graph.score', ptr + '/conflicting_query_ids'))
        add(p, recipe_hash, graph_corpus, v,
            ['Supplied directed edge judgment is not source-only graph construction or archive recall.', 'Correction family refutation recall differs materially from the aggregate; retain full family metrics.', 'No Noul probability calibration measured here.'], failures)
        if name == 'jev_dual_noul':
            detail[p['id']]['recipe_hash_basis'] = 'post_result_identity_digest_of_saved_frozen_questions; UTF8 sorted compact JSON; not a new preregistration'

    for event, pid in [(False, 'graph.gpt_source_assertions'), (True, 'graph.gpt_status_events')]:
        field = 'strict_status_events' if event else 'strict_edges'
        v = graph['assisted_extraction'][field]
        ptr = '/assisted_extraction/' + field
        metrics = {'record_precision': ratio(v['tp'], v['predicted'], 'graph.score', ptr, 'TP / all proposed records, including rejected records.'),
                   'record_recall': ratio(v['tp'], v['gold'], 'graph.score', ptr, 'TP / all gold records; no conditional-validity denominator substituted.')}
        p = profile(pid, batches['graph_extraction24'], 'source_status_event_extraction' if event else 'source_assertion_record_extraction', None,
            'graph_methods_panel_v1/assisted_extraction', 'graph.extraction_recipe', '/requests/0/body/messages/0/content',
            24, 24, 'source packet requests; record metrics have their own denominators',
            'Source turns and supplied node inventory, no judgment queries; local source compiler locates unique exact quotes and generates coordinates.', metrics,
            ['authored source-graph DEV', 'pl', 'en'])
        notes = ['World truth and source-node discovery are not measured.',
                 'Record TP requires frozen typed endpoints and annotated turn match; semantic clause adequacy needs its separate independent audit.']
        if event:
            notes += ['Supersession-target convention was not fully fixed in the prompt; frozen primary scoring retained, alternate convention is diagnostic only.', 'Different-actor supersession remains a distinct observed error.']
        add(p, graph_freeze['extraction_prompt_sha256'], graph_corpus,
            {'primary_record_counts': v, 'by_family_and_language': graph['assisted_extraction']['groups'],
             'invalid_assertions': graph['assisted_extraction']['invalid_assertions'],
             'invalid_events': graph['assisted_extraction']['invalid_events'],
             'semantic_quote_review': graph['assisted_extraction']['semantic_quote_review'],
             'event_convention_diagnostic': graph['assisted_extraction']['event_convention_diagnostic']}, notes,
            [failure('proposed_record_false_positive', 'reported_observation', 'Frozen primary strict source-record score; invalid and semantic errors retained.', v['fp'], 'graph.score', ptr)])

    v = graph['v1_transport_failure_score_preserved']
    p = profile('graph.gpt_judge_v1_transport_failure', batches['graph_gpt_v1_failed'], 'supplied_directed_source_relation_judgment', None,
        'graph_methods_panel_v1/gpt_json_literal_v1_failed', 'graph.gpt_v1_recipe', '/requests/0/body/messages/0/content',
        48, 0, 'planned request labels; 1 HTTP400 and 47 never attempted',
        'Original ternary JSON-mode prompt missing required literal JSON; preserved failed first plan, not replaced with V2.',
        {'label_coverage': ratio(0, 48, 'graph.score', '/v1_transport_failure_score_preserved', 'Original 48 planned labels unavailable.'),
         'semantic_accuracy_conditional_on_valid_output': t4.metric(None, None, 0, 'unavailable', 'graph.score', '/v1_transport_failure_score_preserved', 'No valid outputs; do not infer semantic ability from transport failure.')},
        ['authored source-graph DEV', 'pl', 'en'])
    add(p, graph_freeze['judge_prompt_sha256'], dict(graph_corpus, queries=48), v,
        ['One unknown-cost first HTTP400 remains unknown, not zero billing; 47 requests never started.', 'Separate V2 recipe is independently measured and does not erase this result.'],
        [failure('provider_protocol_rejection', 'reported_observation', 'HTTP400 JSON-mode literal prerequisite; no paid retry of the same first request.', 1, 'graph.score', '/execution/gpt_judge_v1_transport_failure')])

    pair_metrics = binary_metrics(pairs['overall'], 'pairs.score', '/overall', 48)
    p = profile('pairs.gpt_same_structure', batches['pairs_gpt48'], 'two_supplied_passages_operation_skeleton_comparison', None,
        'structure_pair_chat_v1', 'pairs.recipe', '/metadata/system_prompt_sha256', 48, 48, 'supplied pair classification requests',
        'Two supplied passages, fixed same_operation_skeleton JSON classifier; original exploratory pairs reused.', pair_metrics,
        ['known exploratory structural pairs', 'pl', 'en'])
    add(p, pair_manifest['metadata']['system_prompt_sha256'],
        {'id': 'structure_pairs_v1', 'effective_split': 'known_exploratory_not_blind', 'blind': False,
         'original_named_split_counts': {k:v['planned'] for k,v in pairs['groups']['split'].items()},
         'gold_hash_recorded': pairs['gold_hash'], 'gold_file_read_by_projection': False},
        {'overall': pairs['overall'], 'groups': pairs['groups'], 'errors': pairs['errors']},
        ['Previously inspected authored pairs including a subset called validation; no sealed validation access or holdout claim.', 'Perfect pair decisions do not transfer to directed source-graph extraction or later graph-judgment recipes.'])

    for task_arm, v in t3_score['groups']['question_arm'].items():
        task, arm = task_arm.split('/')
        request_index, request = next((i, r) for i, r in enumerate(t3_manifest['requests']) if r['arm'] == arm)
        question = request['body']['questions'][task]
        metrics = binary_metrics(v, 't3.score', '/groups/question_arm/' + task_arm.replace('/', '~1'), v['planned'])
        metrics['brier_dev'] = t4.metric(v['brier_observed'], None, v['observed'], 'reported_scalar', 't3.score', '/groups/question_arm/' + task_arm.replace('/', '~1') + '/brier_observed', 'Observed mean squared probability error on this authored DEV task/arm.', 'mean_squared_error')
        metrics['ece_dev_five_bins'] = t4.metric(v['ece_observed'], None, v['observed'], 'reported_scalar', 't3.score', '/groups/question_arm/' + task_arm.replace('/', '~1') + '/ece_observed', 'Five-bin empirical DEV diagnostic, not calibration certification.')
        p = profile('t3.' + task + '.' + arm, batches['t3_first32'], 'source_expressed_relation' if task == 'expressed' else 'bounded_candidate_structural_inference', task,
            'jev_recipes_v1/' + task_arm, 't3.recipe', '/requests/' + str(request_index) + '/body/questions/' + task,
            v['planned'], v['observed'], 'question decisions; expressed/inferred share each arm request',
            'Source state/history/target_turn and candidate_relation; meaningful object vs string instructions are separately keyed recipes.', metrics,
            ['known synthetic authored T3 cases', 'pl', 'en'])
        add(p, sha(canonical(question)),
            {'id': 'jev_recipes_v1_first16', 'effective_split': 'known_synthetic_dev_not_blind', 'blind': False,
             'case_count': 16, 'original_named_split_decision_counts': {k:s['planned'] for k,s in t3_score['groups']['split'].items()},
             'gold_sha256_recorded': t3_score['gold_sha256'], 'gold_file_read_by_projection': False},
            {'task_arm_score': v, 'paired_format_contrast': t3_score['paired'][task]},
            ['Same 16 cases evaluated in both formats; question/arm decisions are dependent.', 'Named authored validation is already-known development evidence, not sealed holdout.',
             'A positive bounded inference is a research candidate, not observed intention or world fact.', 'Zero threshold mismatches does not select a format winner; paired Brier intervals include zero.'],
            recipe_hash_basis='post_result_identity_digest_of_saved_frozen_question; UTF8 sorted compact JSON; not a new preregistration')

    schema = sources.read('t4.schema', 'docs/contracts/model_profile.schema.json')
    sources.read('t4.historical_profile_document', 'loom/tests/fixtures/model_profiles_v1.json')
    fragment_schema = {'$schema': schema['$schema'], '$defs': schema['$defs'], '$ref': '#/$defs/profile'}
    validator = Draft202012Validator(fragment_schema, format_checker=FormatChecker())
    issues = {p['id']: [str(e.json_path) + ': ' + e.validator for e in validator.iter_errors(p)] for p in profiles}
    assert not any(issues.values()), issues
    assert len({p['id'] for p in profiles}) == len(profiles) == 12
    source_ids = set(sources.items)
    for p in profiles:
        assert p['population']['available'] + p['population']['missing'] == p['population']['planned']
        assert p['key']['recipe_source']['source_id'] in source_ids
        assert p['cost']['reported_total_usd'] is None
        assert p['latency']['per_question_seconds'] is None
        for m in p['metrics'].values():
            assert m['evidence']['source_id'] in source_ids
            if m['method'] == 'counted_ratio':
                assert 0 <= m['numerator'] <= m['denominator']
                assert m['value'] == (m['numerator'] / m['denominator'] if m['denominator'] else None)
        for f in p['failure_modes']:
            assert f['evidence']['source_id'] in source_ids
    known_at = original['projection_known_at'] if original else datetime.now(timezone.utc).isoformat()
    result = {'schema': 'loom.research.empirical_profiles/1', 'version': 1,
        'representation': 'research_projection_not_canonical', 'canonical': False,
        'projection_known_at': known_at, 'projection_known_at_basis': 'creation of this derived research projection, not first response or score publication time',
        'measurement_known_at_status': 'exact metric publication timestamps unavailable; response intervals retained separately',
        'repository_base_commit': original['repository_base_commit'] if original else git('rev-parse', 'HEAD').decode().strip(),
        'new_model_calls': 0, 'validation_accessed_by_projection': False, 'no_graph_promotion': True,
        'global_model_reliability': None, 'profiles': profiles, 'profile_details': detail,
        'batches': batches, 'sources': list(sources.items.values()),
        'contract_mapping': {'profile_fragment_schema': 'docs/contracts/model_profile.schema.json#/$defs/profile',
            'profile_fragment_shape_valid': True, 'full_t4_document_valid': False,
            'existing_python_full_document_semantic_validation_performed': False,
            'separate_artifact_integrity_checks': ['profile ID uniqueness', 'population reconciliation', 'metric ratio arithmetic', 'metric/recipe/failure evidence references', 'no allocated question cost/latency', 'usage totals agree with original scores', 'saved GPT model/provider metadata raw hashes'],
            'mapping_needs': ['Uncommitted/current source SHA + real commit membership; never fabricate source commit.', 'Exact measurement known_at and response observation interval as distinct fields.', 'Retained ternary confusion and semantic-conflict vs transport/parse/missing dispositions.', 'Requested/returned model/provider identifiers and immutable-version uncertainty.', 'Per-request usage cost vs independent generation billing availability and unknown-cost reservation.', 'Defined POST timer vs GET/local processing residual; no invented per-question latency.', 'Observed DEV ECE/Brier diagnostics without calibration certification.', 'Recipe hash, task representation and effective split/blinding status.']}}
    result['contract_mapping']['full_t4_root_schema_errors'] = sorted({str(e.json_path) + ': ' + str(e.validator) for e in Draft202012Validator(schema, format_checker=FormatChecker()).iter_errors(result)})
    return result


def main():
    target = HERE / 'profiles.json'
    if sys.argv[1:] == ['--check']:
        old = t4.load(target)
        fresh = build(old)
        assert fresh == old, 'research projection differs from preserved first projection'
        print(json.dumps({'valid_research_projection': True, 'strict_profile_fragments': len(old['profiles']),
            'full_t4_document_valid': False, 'new_model_calls': 0, 'all_source_hashes_match': True}))
    elif not sys.argv[1:]:
        result = build()
        with target.open('xb') as handle:
            handle.write(json.dumps(result, ensure_ascii=False, sort_keys=True, allow_nan=False, indent=2).encode() + b'\n')
        print(json.dumps({'created': str(target.relative_to(ROOT)), 'profiles': len(result['profiles']),
            'sha256': sha(target.read_bytes()), 'new_model_calls': 0}))
    else:
        raise SystemExit('Use no args to exclusively create first profiles.json or --check to reproduce it.')


if __name__ == '__main__':
    main()
