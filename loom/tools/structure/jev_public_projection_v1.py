"""Allowlisted public aggregates from a hash-bound private Jev validation.

Full replay stays private. Public method claims reference only newly constructed
public aggregate receipts and hash-reference method declarations. No request,
response, row, query/gold pair, filename or private source text is copied.

7A must approve the publication contract's public identifiers/metadata and freeze
the consumed method declarations before collection. Attach their verified
``publication_method_bindings`` and ``cohort_release_sha256`` to the private
post-score validation, without changing its scores, then bind that whole private
validation SHA-256 in the publication contract. Missing bindings fail closed.
The digests attest private replay; they do not establish independent public
replay, source authenticity, or automatic anonymization of approved metadata.
The rich Jev exporter remains suitable only for private replay.
"""
from __future__ import annotations

import argparse
from decimal import Decimal, InvalidOperation
import gzip
from pathlib import Path
import re

from . import method_graph_export_v1 as graph
from . import openrouter_runner as wire
from . import programme_results_v1 as results


class ProjectionError(ValueError):
    """Public fixed codes only; never echo rejected input values."""


ARMS = ('j_active', 'j_directed')
LANGUAGES = ('pl', 'en')
LABELS = ('supported', 'refuted', 'unknown')
AGGREGATES = ('accuracy', 'availability', 'protocol', 'errors', 'confusion', 'paired', 'cost')
ERRORS = ('correct', 'source_label_mismatch', 'conflicting_label', 'protocol_invalid',
          'missing_first_row', 'missing_first_capture', 'first_attempt_unavailable', 'unavailable')
SAMPLING = ('temperature', 'top_p', 'top_k', 'max_tokens', 'max_output_tokens', 'seed')


def _keys(value, keys):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ProjectionError('publication_contract_keys_invalid')


def _sha(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{64}', value):
        raise ProjectionError('publication_sha256_invalid')
    return value


def _count(value):
    if type(value) is not int or value < 0:
        raise ProjectionError('aggregate_count_invalid')
    return value


def _money(value):
    if type(value) not in (str, int, float) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+)?', str(value)):
        raise ProjectionError('aggregate_cost_invalid')
    try:
        number = Decimal(str(value))
    except InvalidOperation:
        raise ProjectionError('aggregate_cost_invalid') from None
    if not number.is_finite() or number < 0:
        raise ProjectionError('aggregate_cost_invalid')
    return format(number, 'f')


def validate_contract(contract):
    _keys(contract, ('schema', 'binding_status', 'private_validation_sha256', 'cohort_release_sha256',
                     'public_release_id', 'observed_on', 'aggregates', 'languages', 'population', 'methods'))
    if (contract['schema'] != 'loom.jev_publication_contract/1' or contract['binding_status'] != 'bound'
            or not isinstance(contract['public_release_id'], str)
            or not re.fullmatch(r'jev-real-[0-9]{8}-[a-z0-9]{8,32}', contract['public_release_id'])):
        raise ProjectionError('publication_contract_not_bound')
    _sha(contract['private_validation_sha256']); _sha(contract['cohort_release_sha256'])
    try:
        graph.date_text(contract['observed_on'])
    except (ValueError, TypeError):
        raise ProjectionError('publication_date_invalid') from None
    for field, allowed in (('aggregates', AGGREGATES), ('languages', LANGUAGES)):
        values = contract[field]
        if not isinstance(values, list) or any(not isinstance(v, str) for v in values) or len(values) != len(set(values)) or set(values) - set(allowed):
            raise ProjectionError('publication_selection_invalid')
    _keys(contract['population'], ('planned_queries_per_arm', 'planned_families', 'language_queries', 'language_families'))
    population = contract['population']
    if not _count(population['planned_queries_per_arm']) or not _count(population['planned_families']):
        raise ProjectionError('publication_population_empty')
    for field, total in (('language_queries', population['planned_queries_per_arm']), ('language_families', population['planned_families'])):
        _keys(population[field], LANGUAGES)
        if sum(_count(v) for v in population[field].values()) != total:
            raise ProjectionError('publication_language_population_invalid')
    _keys(contract['methods'], ARMS)
    for method in contract['methods'].values():
        _keys(method, ('requested_model_id', 'prompt_sha256', 'version_sha256', 'sampling_parameters'))
        model = method['requested_model_id']
        if not isinstance(model, str) or not re.fullmatch(r'[A-Za-z0-9_-]+/[A-Za-z0-9][A-Za-z0-9._:-]{0,120}', model):
            raise ProjectionError('publication_model_id_invalid')
        _sha(method['prompt_sha256']); _sha(method['version_sha256'])
        parameters = method['sampling_parameters']
        if not isinstance(parameters, dict) or set(parameters) - set(SAMPLING):
            raise ProjectionError('publication_sampling_parameters_invalid')
        for value in parameters.values():
            if type(value) not in (int, float) or not Decimal(str(value)).is_finite():
                raise ProjectionError('publication_sampling_parameters_invalid')
    return contract


def _cohort(source, queries, families, selections):
    if (_count(source['planned_queries']) != queries or _count(source['planned_families']) != families
            or type(source['first_attempt_outcomes_measured']) is not bool
            or type(source.get('actual_model_answer_labels_measured')) is not bool):
        raise ProjectionError('aggregate_population_changed')
    correct = _count(source['correct']); available = _count(source['available'])
    unavailable = _count(source['unavailable']); protocol = _count(source['protocol_valid'])
    if correct > available or available + unavailable != queries or available > protocol or protocol > queries:
        raise ProjectionError('aggregate_counts_inconsistent')
    if not source['first_attempt_outcomes_measured'] and (correct or available or protocol):
        raise ProjectionError('unattempted_aggregate_has_observations')
    if ((not source['actual_model_answer_labels_measured'] and available)
            or (not source['first_attempt_outcomes_measured'] and source['actual_model_answer_labels_measured'])):
        raise ProjectionError('aggregate_model_answer_scope_invalid')
    expected_accuracy = correct / queries if source['first_attempt_outcomes_measured'] else None
    if source['accuracy_all_planned'] != expected_accuracy:
        raise ProjectionError('aggregate_accuracy_inconsistent')
    metrics = {}
    def metric(name, numerator, value, note):
        metrics[name] = results.shared_metric(value, numerator=numerator, denominator=queries, unit='fraction', note=note)
    if 'accuracy' in selections:
        metric('accuracy_all_planned', correct, expected_accuracy,
               'Private first-response replay aggregate; missing and invalid attempts retain the planned denominator. Entirely unattempted population is null.')
    if 'availability' in selections:
        metric('label_availability', available, available / queries, 'Valid ternary labels; conflicting and missing labels remain unavailable.')
    if 'protocol' in selections:
        metric('protocol_validity', protocol, protocol / queries, 'Valid two-probability first answers; conflicting labels can be protocol valid.')
    public = {'planned_queries': queries, 'planned_families': families,
              'first_attempt_outcomes_measured': source['first_attempt_outcomes_measured'],
              'model_answer_labels_measured': source['actual_model_answer_labels_measured'], 'metrics': metrics}
    errors = source['error_classes']
    if not isinstance(errors, dict) or set(errors) - set(ERRORS) or sum(_count(v) for v in errors.values()) != queries:
        raise ProjectionError('aggregate_error_classes_invalid')
    if (errors.get('correct', 0) != correct or errors.get('source_label_mismatch', 0) != available - correct
            or sum(errors.get(k, 0) for k in ERRORS if k not in ('correct', 'source_label_mismatch')) != unavailable
            or errors.get('conflicting_label', 0) > protocol - available
            or source['first_attempt_outcomes_measured'] != (errors.get('missing_first_row', 0) < queries)):
        raise ProjectionError('aggregate_error_classes_inconsistent')
    if 'errors' in selections:
        public['error_counts'] = {key: _count(errors.get(key, 0)) for key in ERRORS}
    known = _money(source['known_verified_cost_usd']); unknown = _count(source['unknown_cost_operations'])
    if unknown > queries:
        raise ProjectionError('aggregate_cost_count_invalid')
    total = source['actual_total_cost_usd']
    if (unknown and total is not None) or (not unknown and _money(total) != known):
        raise ProjectionError('aggregate_cost_completeness_invalid')
    if not source['first_attempt_outcomes_measured'] and (Decimal(known) != 0 or unknown != queries):
        raise ProjectionError('unattempted_aggregate_has_cost')
    if 'cost' in selections:
        public['cost'] = {'known_generation_verified_usd': known, 'unknown_operations': unknown,
                          'complete': unknown == 0, 'actual_total_usd': None if unknown else known}
    return public


def project(validation_raw, contract_raw, *, contract_sha256):
    """Construct fresh public fields; never redact/copy arbitrary private objects."""
    if results.sha(contract_raw) != _sha(contract_sha256):
        raise ProjectionError('publication_contract_hash_changed')
    contract = validate_contract(wire.parse_json(contract_raw))
    if results.sha(validation_raw) != contract['private_validation_sha256']:
        raise ProjectionError('private_validation_hash_changed')
    private = wire.parse_json(validation_raw)
    if private.get('schema') != 'loom.jev_validation_results/1':
        raise ProjectionError('private_validation_schema_invalid')
    if private.get('observed_on') != contract['observed_on']:
        raise ProjectionError('private_validation_date_changed')
    try:
        exact_method_binding = wire.canonical(private.get('publication_method_bindings')) == wire.canonical(contract['methods'])
    except (ValueError, TypeError):
        exact_method_binding = False
    if (private.get('cohort_release_sha256') != contract['cohort_release_sha256'] or not exact_method_binding):
        raise ProjectionError('private_method_or_cohort_binding_changed')
    selections = contract['aggregates']; population = contract['population']; reports = {}
    for arm in ARMS:
        source = private['reports'][arm]
        report = _cohort(source, population['planned_queries_per_arm'], population['planned_families'], selections)
        language_sources = {}
        for language in LANGUAGES:
            count = population['language_queries'][language]
            if count:
                language_source = source['by_language'][language]
                _cohort(language_source, count, population['language_families'][language], ())
                language_sources[language] = language_source
        for field in ('correct', 'available', 'unavailable', 'protocol_valid', 'unknown_cost_operations'):
            if sum(v[field] for v in language_sources.values()) != source[field]:
                raise ProjectionError('language_aggregate_counts_inconsistent')
        if (sum((Decimal(_money(v['known_verified_cost_usd'])) for v in language_sources.values()), Decimal(0))
                != Decimal(_money(source['known_verified_cost_usd']))
                or any(v['first_attempt_outcomes_measured'] for v in language_sources.values()) != source['first_attempt_outcomes_measured']
                or any(v['actual_model_answer_labels_measured'] for v in language_sources.values()) != source['actual_model_answer_labels_measured']
                or any(sum(v['error_classes'].get(key, 0) for v in language_sources.values()) != source['error_classes'].get(key, 0)
                       for key in ERRORS)):
            raise ProjectionError('language_aggregate_outcomes_inconsistent')
        report['by_language'] = {}
        for language in contract['languages']:
            count = population['language_queries'][language]
            if not count:
                raise ProjectionError('selected_language_population_empty')
            report['by_language'][language] = _cohort(source['by_language'][language], count,
                                                      population['language_families'][language], selections)
        if 'confusion' in selections:
            matrix = source['confusion']; _keys(matrix, LABELS)
            checked = {}
            for label in LABELS:
                _keys(matrix[label], (*LABELS, 'unavailable'))
                checked[label] = {predicted: _count(matrix[label][predicted]) for predicted in (*LABELS, 'unavailable')}
            if (sum(sum(row.values()) for row in checked.values()) != population['planned_queries_per_arm']
                    or sum(checked[label][label] for label in LABELS) != source['correct']
                    or sum(checked[label]['unavailable'] for label in LABELS) != source['unavailable']):
                raise ProjectionError('aggregate_confusion_inconsistent')
            report['confusion_counts'] = checked
        reports[arm] = report
    paired = None
    if 'paired' in selections:
        candidates = [p for p in private['paired_changes'] if p.get('baseline') == ARMS[0] and p.get('candidate') == ARMS[1]]
        if len(candidates) != 1 or _count(candidates[0]['planned_pairs']) != population['planned_queries_per_arm']:
            raise ProjectionError('aggregate_paired_population_changed')
        counts = candidates[0]['counts']; keys = ('disagreement', 'correction', 'regression', 'availability_gained', 'availability_lost')
        _keys(counts, keys)
        if any(_count(counts[k]) > population['planned_queries_per_arm'] for k in keys):
            raise ProjectionError('aggregate_paired_count_invalid')
        baseline, candidate = (private['reports'][arm] for arm in ARMS)
        if (counts['availability_gained'] - counts['availability_lost'] != candidate['available'] - baseline['available']
                or counts['availability_gained'] > min(baseline['unavailable'], candidate['available'])
                or counts['availability_lost'] > min(baseline['available'], candidate['unavailable'])
                or counts['correction'] > min(baseline['available'] - baseline['correct'], candidate['correct'])
                or counts['regression'] > min(baseline['correct'], candidate['available'] - candidate['correct'])
                or counts['correction'] + counts['availability_gained'] > candidate['available']
                or counts['regression'] + counts['availability_lost'] > baseline['available']
                or counts['correction'] + counts['regression'] > baseline['available'] - counts['availability_lost']
                or not (-counts['availability_lost'] <= candidate['correct'] - baseline['correct']
                        - (counts['correction'] - counts['regression']) <= counts['availability_gained'])
                or sum(counts[k] for k in keys if k != 'disagreement') > counts['disagreement']):
            raise ProjectionError('aggregate_paired_counts_inconsistent')
        paired = {'baseline': ARMS[0], 'candidate': ARMS[1], 'planned_pairs': population['planned_queries_per_arm'],
                  'counts': {k: _count(counts[k]) for k in keys}}
    return {'schema': 'loom.jev_public_aggregate/1', 'public_release_id': contract['public_release_id'],
            'observed_on': contract['observed_on'], 'private_validation_commitment_sha256': contract['private_validation_sha256'],
            'cohort_release_commitment_sha256': contract['cohort_release_sha256'], 'publication_contract_sha256': contract_sha256,
            'methods': {arm: {key: contract['methods'][arm][key] for key in
                             ('requested_model_id', 'prompt_sha256', 'version_sha256', 'sampling_parameters')} for arm in ARMS},
            'reports': reports, 'paired_aggregate': paired, 'new_model_calls': 0,
            'new_model_calls_scope': 'Offline public projection only; original collection calls are not counted here.',
            'evidence_boundary': 'Aggregate attestation from private real-export first-response replay. Source text, private IDs, query-level gold and exact requests/responses remain private; public hashes do not provide independent source replay.',
            'projection_loss': 'All private source material and per-operation records are omitted. No private file is referenced by the public graph.',
            'automatic_promotion': False}


def export(validation_raw, contract_raw, output, *, contract_sha256):
    public = project(validation_raw, contract_raw, contract_sha256=contract_sha256)
    output = Path(output); output.mkdir(parents=True, exist_ok=False)
    results.write(output / 'PUBLIC_VALIDATION.json', public)
    def source(name):
        return {'path': name, 'sha256': results.sha((output / name).read_bytes())}
    methods = []
    for arm in ARMS:
        version = public['methods'][arm]
        recipe = {'representation': 'private_prompt_hash_reference', 'prompt_sha256': version['prompt_sha256'],
                  'private_prompt_text_withheld': True}
        parameters = {'requested_model': version['requested_model_id'], **version['sampling_parameters']}
        methods.append({'id': arm, 'method_id': 'research.programme.' + arm, 'version': version['version_sha256'],
                        'recipe_material': recipe, 'recipe_sha256': graph.codec.digest(recipe),
                        'prompt_sha256': {'private_prompt': version['prompt_sha256']}, 'parameters': parameters,
                        'preset': {'id': 'public_declared_numeric_sampling', 'version': graph.codec.digest(parameters), 'values': parameters},
                        'components': [], 'execution_kind': 'private_first_response_aggregate_attestation',
                        'model_identity_scope': 'Requested model identity declared; exact private receipt identity is not independently replayable publicly.'})
    results.write(output / 'METHODS.json', methods)
    runs = []
    for arm in ARMS:
        report = public['reports'][arm]; metrics = []
        for language in (None, *report['by_language']):
            cohort = report if language is None else report['by_language'][language]
            prefix = '/reports/' + arm + ('' if language is None else '/by_language/' + language)
            for key, metric in cohort['metrics'].items():
                metrics.append({'id': key if language is None else key + '_' + language,
                                'location': prefix + '/metrics/' + key, 'metric_pointer': prefix + '/metrics/' + key,
                                'axis': ('model_quality' if cohort['model_answer_labels_measured'] else
                                         'mechanism' if cohort['first_attempt_outcomes_measured'] else 'unavailable')
                                        if key == 'accuracy_all_planned' else 'mechanism'})
        runs.append({'id': public['public_release_id'] + ':' + arm, 'method_ref': arm,
                     'source': source('PUBLIC_VALIDATION.json'), 'observed_on': public['observed_on'],
                     'measurement_kind': ('historical_actual_model_response_replay' if report['model_answer_labels_measured']
                                          else 'private_first_attempt_outcome_aggregate_attestation' if report['first_attempt_outcomes_measured']
                                          else 'private_unattempted_population_aggregate_attestation'), 'metrics': metrics,
                     'population': {'planned': report['planned_queries'], 'available': report['planned_queries'], 'missing': 0,
                                    'unit': 'planned query slots with aggregate outcome; unattempted slots retained',
                                    'dependent_observations': 'Private real source families shared across paired recipes; query rows are dependent. Public evidence is an aggregate attestation.'}})
    configuration = {'public_artifacts_only': True, 'exported_on': public['observed_on'],
                     'methods': [{**method, 'declaration_source': source('METHODS.json'), 'declaration_pointer': '/' + str(i)}
                                 for i, method in enumerate(methods)], 'runs': runs}
    results.write(output / 'configuration.json', configuration)
    packet = graph.export(configuration, root=output)
    raw = graph.codec.encode_packet(packet)
    with (output / 'packet.json.gz').open('xb') as file:
        with gzip.GzipFile(filename='', mode='wb', fileobj=file, mtime=0) as zipped:
            zipped.write(raw)
    receipt = {'schema': 'loom.jev_public_aggregate_verification/1', 'packet_id': packet['packet_id'],
               'packet_sha256': results.sha(raw), 'private_validation_input_read': True,
               'full_request_response_replay_read': False, 'private_material_published': False,
               'new_model_calls': 0, 'new_model_calls_scope': public['new_model_calls_scope'],
               'publication_contract_sha256': contract_sha256,
               'output_sha256': {p.name: results.sha(p.read_bytes()) for p in sorted(output.iterdir()) if p.is_file()}}
    results.write(output / 'VERIFICATION.json', receipt)
    return public, packet, receipt


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    for name in ('validation', 'contract', 'contract-sha256', 'output'):
        cli.add_argument('--' + name, required=True)
    args = cli.parse_args()
    _, _, receipt = export(Path(args.validation).read_bytes(), Path(args.contract).read_bytes(), args.output,
                           contract_sha256=args.contract_sha256)
    print(wire.canonical({'packet_id': receipt['packet_id'], 'new_model_calls': 0}).decode())


if __name__ == '__main__':
    main()
