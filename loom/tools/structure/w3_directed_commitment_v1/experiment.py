"""W3 offline recipe preparation and exact first-response replay; no live entrypoint."""
from __future__ import annotations

import argparse
from collections import Counter
from copy import deepcopy
from decimal import Decimal
from pathlib import Path
import hashlib
import subprocess
import tempfile
from zipfile import ZipFile

from .. import source_view_experiment as base
from .. import source_view_roles_v1 as roles

panel, integrity, jev, safe = base.panel, base.replay, base.jev, base.safe
ROOT = panel.ROOT
HERE = Path(__file__).parent
BASE_SHA = 'b118c80e981c08ec6d7f9ab6aacc177979186cf2'
# Commit identities can change when private author metadata is sanitized. The
# source tree is immutable and pins the original experimental inputs unchanged.
BASE_TREE_SHA = 'b122b30314009a4b7d793a2c634e5b65d6835397'
HISTORY = ROOT / 'docs/research/source_view_experiment_2026-09-30'
CONTROLS = ROOT / 'loom/tests/fixtures/research/w3_directed_commitment_v1'
ARMS = (*base.ARMS, 'directed_refute_v3')


def recipe():
    value = integrity.read(HERE / 'recipe.json')
    if (set(value) != {'schema', 'id', 'parent', 'changed_field', 'append'}
            or value['schema'] != 'loom.w3_directed_commitment.recipe/1'
            or value['id'] != ARMS[-1] or value['parent'] != base.ARMS[-1]
            or value['changed_field'] != 'questions.q02.criteria.false'
            or not isinstance(value['append'], str) or not value['append'].strip()):
        raise ValueError('recipe_contract')
    return value


def specs(cases, arm):
    if arm not in ARMS:
        raise ValueError('unknown_recipe')
    rows = base.specs(cases, arm if arm in base.ARMS else 'active_refute_v2')
    if arm == 'directed_refute_v3':
        for row in rows:
            row['questions']['q02']['criteria']['false'] += recipe()['append']
    ids = [r['case_id'] for r in rows]
    if len(ids) != len(set(ids)) or not ids:
        raise ValueError('query_inventory')
    for row in rows:
        payload = safe.parse_json(row['state']['text'])
        # Reuse the role renderer's identity checks without changing representation.
        roles.role_representation(payload)
        tids = [t['id'] for t in payload['turns']]
        if len(tids) != len(set(tids)):
            raise ValueError('ambiguous_source_turn')
        jev.validate_body({'model': jev.MODEL, 'state': row['state'],
                          'questions': row['questions'], 'provider': provider()})
    return rows


def provider():
    return {'only': ['typesafe'], 'allow_fallbacks': False,
            'max_price': {'prompt': '0.042', 'completion': '0'}}


def load_controls(name):
    value = integrity.read(CONTROLS / name)
    if value['split'] != 'dev':
        raise ValueError('dev_only')
    return value['cases']


def prepare(output):
    """Writes runner input files; never calls runner.prepare (which uses network)."""
    output = Path(output)
    populations = {'historical_dev': base.load_fixture('inputs_dev.json'),
                   'independent_dev': load_controls('inputs_dev.json')}
    output.mkdir(parents=True, exist_ok=False)
    index = []
    for population, cases in populations.items():
        for arm in ('active_refute_v2', 'directed_refute_v3'):
            rows = specs(cases, arm)
            existing = population == 'historical_dev' and arm == 'active_refute_v2'
            if len(rows) > 64:
                raise ValueError('split_plan_required_for_existing_runner')
            relative = Path(population) / arm
            panel.write_new(output / relative / 'inputs.json', rows)
            panel.write_new(output / relative / 'request.json', {
                'schema': 'loom.jev_pilot_request/1', 'enabled': not existing,
                'experiment_id': f'w3-{population}-{arm}-20260930',
                'model': jev.MODEL, 'budget_usd': '2', 'batch_cap_usd': '0.10',
                'max_requests': len(rows)})
            index.append({'population': population, 'recipe': arm,
                          'directory': str(relative), 'queries': len(rows),
                          'inputs_sha256': safe.digest(rows),
                          'execution_kind': 'replay_existing_first' if existing else 'unmeasured_new_recipe_or_population',
                          'reservation_usd': '0' if existing else str(jev.RESERVE * len(rows))})
    record = {'schema': 'loom.w3_directed_commitment.plan/1', 'batches': index,
              'new_model_calls': 0, 'gold_read': False, 'measured_quality': None,
              'shared_nonresetting_cap_usd': '2', 'execution_authorized_by_this_plan': False,
              'all_variants_reservation_usd': str(sum((Decimal(r['reservation_usd']) for r in index), Decimal(0))),
              'note': 'Reservation planning only; ROOT selects execution after live accounting.'}
    panel.write_new(output / 'plan.json', record)
    dependencies = plan_dependencies()
    panel.write_new(output / 'freeze.json', {
        'schema': 'loom.w3_directed_commitment.freeze/1', 'new_model_calls': 0,
        'gold_content_loaded': False, 'gold_bytes_hashed': True,
        'source_files_sha256': {str(p.relative_to(ROOT)): panel.digest_file(p) for p in dependencies},
        'plan_files_sha256': {str(p.relative_to(output)): panel.digest_file(p)
                             for p in sorted(output.rglob('*.json'))}})
    return record


def plan_dependencies():
    return [HERE / 'recipe.json', Path(__file__), HERE / 'test_experiment.py',
                    HERE / 'test_independent.py', Path(base.__file__),
                    Path(roles.__file__), Path(panel.__file__), Path(integrity.__file__),
                    Path(jev.__file__), Path(safe.__file__),
                    base.FIXTURE / 'manifest.json', base.FIXTURE / 'inputs_dev.json',
                    base.FIXTURE / 'gold_dev.json', CONTROLS / 'inputs_dev.json',
                    CONTROLS / 'gold_dev.json',
                    ROOT / 'docs/research/w3_directed_commitment_v1/PROTOCOL.md']


def verify_plan(output):
    output = Path(output)
    freeze = integrity.read(output / 'freeze.json')
    expected_plans = {'plan.json'} | {
        f'{population}/{arm}/{name}.json'
        for population in ('historical_dev', 'independent_dev')
        for arm in ('active_refute_v2', 'directed_refute_v3') for name in ('inputs', 'request')}
    if (freeze.get('schema') != 'loom.w3_directed_commitment.freeze/1'
            or set(freeze['source_files_sha256']) != {str(p.relative_to(ROOT)) for p in plan_dependencies()}
            or set(freeze['plan_files_sha256']) != expected_plans):
        raise ValueError('freeze_inventory')
    for root, field in ((ROOT, 'source_files_sha256'), (output, 'plan_files_sha256')):
        for name, expected in freeze[field].items():
            path = (root / name).resolve()
            if not path.is_relative_to(root.resolve()) or panel.digest_file(path) != expected:
                raise ValueError('frozen_plan_drift')
    plan = integrity.read(output / 'plan.json')
    expected_keys = {(p, a) for p in ('historical_dev', 'independent_dev')
                     for a in ('active_refute_v2', 'directed_refute_v3')}
    batches = plan.get('batches', [])
    if (plan.get('schema') != 'loom.w3_directed_commitment.plan/1' or len(batches) != 4
            or {(b['population'], b['recipe']) for b in batches} != expected_keys):
        raise ValueError('plan_inventory')
    for batch in batches:
        population, arm = batch['population'], batch['recipe']
        cases = base.load_fixture('inputs_dev.json') if population == 'historical_dev' else load_controls('inputs_dev.json')
        rows = specs(cases, arm)
        relative = f'{population}/{arm}'
        existing = population == 'historical_dev' and arm == 'active_refute_v2'
        request = integrity.read(output / relative / 'request.json')
        expected_request = {'schema': 'loom.jev_pilot_request/1', 'enabled': not existing,
            'experiment_id': f'w3-{population}-{arm}-20260930', 'model': jev.MODEL,
            'budget_usd': '2', 'batch_cap_usd': '0.10', 'max_requests': len(rows)}
        if (batch['directory'] != relative or batch['queries'] != len(rows)
                or batch['inputs_sha256'] != safe.digest(rows)
                or integrity.read(output / relative / 'inputs.json') != rows
                or request != expected_request
                or batch['execution_kind'] != ('replay_existing_first' if existing else 'unmeasured_new_recipe_or_population')
                or Decimal(batch['reservation_usd']) != (0 if existing else len(rows) * jev.RESERVE)):
            raise ValueError('plan_rows_drift')
    return plan


def restore_history(destination):
    """Verify original archive before restoring only run artifacts to a new folder."""
    archive = HISTORY / 'first_evidence.zip'
    receipt = integrity.read(HISTORY / 'FIRST_EVIDENCE_ARCHIVE.json')
    raw = archive.read_bytes()
    if len(raw) != receipt['archive_bytes'] or hashlib.sha256(raw).hexdigest() != receipt['archive_sha256']:
        raise ValueError('archive_container_drift')
    payloads = {}
    with ZipFile(archive) as zipped:
        names = zipped.namelist()
        inventory = safe.parse_json(zipped.read('inventory.json'))
        if (len(names) != len(set(names)) or inventory['files'] != receipt['files']
                or set(names) != set(inventory['files']) | {'inventory.json'}):
            raise ValueError('archive_inventory_drift')
        for name, expected in inventory['files'].items():
            data = zipped.read(name)
            if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
                raise ValueError('archive_member_drift')
            # No arbitrary archive paths are written: only exact manifest-approved paths.
            for arm in base.ARMS:
                prefix = str((HISTORY / arm / 'run').relative_to(ROOT)) + '/'
                if name.startswith(prefix):
                    leaf = name[len(prefix):]
                    if Path(leaf).name != leaf or leaf in ('.', '..'):
                        raise ValueError('archive_unsafe_run_path')
                    payloads[Path(arm) / leaf] = data
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in payloads.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(data)
    return {'container_sha256': receipt['archive_sha256'],
            'members': {str(p): hashlib.sha256(data).hexdigest() for p, data in payloads.items()}}


def validate_requests(manifest_path, cases, arm):
    manifest_path = Path(manifest_path)
    manifest = integrity.read(manifest_path)
    jev.validate_manifest(manifest)
    expected = specs(cases, arm)
    snapshot = integrity.read(manifest_path.with_name('endpoint_snapshot.json'))
    if (manifest.get('inputs_hash') != safe.digest(expected)
            or manifest['model_aliases'] != jev.endpoint_identity(snapshot)
            or manifest['endpoint_snapshot_hash'] != safe.digest(snapshot)
            or len(manifest['requests']) != len(expected)):
        raise ValueError('recipe_or_endpoint_identity_drift')
    for actual, row in zip(manifest['requests'], expected):
        body = {'model': jev.MODEL, 'state': row['state'],
                'questions': row['questions'], 'provider': provider()}
        if (actual['id'] != row['case_id'] or actual['language'] != row['language']
                or actual['body'] != body):
            raise ValueError('exact_request_drift')
    return manifest


def replay(manifest_path, run_dir, cases, arm):
    manifest = validate_requests(manifest_path, cases, arm)
    ledger = integrity.read(Path(run_dir) / 'ledger.json')
    for attempt in ledger.get('attempts', []):
        if attempt.get('state') == 'completed' and attempt.get('http_status') != 200:
            raise ValueError('completed_without_http_success')
    # The existing replay retains every planned query and checks raw/ledger billing.
    predictions, summary = roles.replay_outputs(manifest, run_dir)
    summary['arm'] = arm
    identities = set()
    for attempt in ledger['attempts']:
        if attempt['state'] == 'completed':
            raw = safe.parse_json((Path(run_dir) / attempt['response_file']).read_bytes())
            identities.add((raw['model'], raw['provider']))
    summary['returned_identities'] = [list(x) for x in sorted(identities)]
    summary['generation_audit_states'] = dict(Counter(
        a.get('generation_billing', {}).get('audit_status', 'not_recorded') for a in ledger['attempts']))
    summary['invoice_verified_total_usd'] = None
    summary['observed_dates'] = sorted({a['started_at'][:10] for a in ledger['attempts']})
    summary['outcomes'] = dict(Counter(p.get('label', 'unavailable') for p in predictions))
    return predictions, summary


def source_record(path, identifier):
    """Prove current bytes really occur at the recorded base; never stamp HEAD blindly."""
    path = Path(path)
    relative = str(path.relative_to(ROOT))
    current = path.read_bytes()
    original = subprocess.run(['git', 'rev-parse', '--verify', f'{BASE_SHA}^{{commit}}'],
                              cwd=ROOT, capture_output=True)
    revision = BASE_SHA
    if original.returncode:
        mapping = integrity.read(ROOT / 'docs/archive/public-history-map.json')
        if not isinstance(mapping, list):
            raise ValueError('historical_revision_map_invalid')
        matches = [row for row in mapping if isinstance(row, dict) and row.get('original') == BASE_SHA]
        if len(matches) != 1:
            raise ValueError('historical_revision_map_entry_missing_or_ambiguous')
        row = matches[0]
        if row.get('tree') != BASE_TREE_SHA:
            raise ValueError('historical_revision_map_tree_mismatch')
        revision = row.get('sanitized')
        if (not isinstance(revision, str) or len(revision) != 40
                or any(c not in '0123456789abcdef' for c in revision)):
            raise ValueError('historical_revision_map_invalid')
        resolved = subprocess.run(['git', 'rev-parse', '--verify', f'{revision}^{{commit}}'],
                                  cwd=ROOT, check=True, capture_output=True).stdout.decode().strip()
        if resolved != revision:
            raise ValueError('historical_revision_map_invalid')
    tree = subprocess.run(['git', 'rev-parse', '--verify', f'{revision}^{{tree}}'],
                          cwd=ROOT, check=True, capture_output=True).stdout.decode().strip()
    if tree != BASE_TREE_SHA:
        raise ValueError('historical_source_tree_mismatch')
    committed = subprocess.run(['git', 'show', f'{revision}:{relative}'], cwd=ROOT,
                               check=True, capture_output=True).stdout
    if current != committed:
        raise ValueError('historical_source_not_at_base_commit')
    blob = hashlib.sha1(b'blob ' + str(len(current)).encode() + b'\0' + current).hexdigest()
    return {'id': identifier, 'path': relative, 'git_blob_sha': blob, 'commit': BASE_SHA,
            'resolved_commit': revision, 'git_tree_sha': tree}


def historical(output):
    from ...eval import model_profiles as profiles
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    cases = base.load_fixture('inputs_dev.json')
    all_rows, summaries, sources = {}, {}, []
    # Freeze all historical inputs by checking actual Git membership, before replay.
    paths = [HISTORY / 'first_evidence.zip', HISTORY / 'FIRST_EVIDENCE_ARCHIVE.json',
             Path(base.__file__), Path(panel.__file__), Path(roles.__file__),
             Path(integrity.__file__), Path(jev.__file__), Path(safe.__file__),
             base.FIXTURE / 'manifest.json', base.FIXTURE / 'inputs_dev.json',
             base.FIXTURE / 'gold_dev.json']
    for arm in base.ARMS:
        paths += sorted(p for folder in ('prepared', 'run', 'first_score')
                        for p in (HISTORY / arm / folder).iterdir() if p.is_file())
    for number, path in enumerate(paths):
        sources.append(source_record(path, f's{number:03}'))
    source_ids = {s['path']: s['id'] for s in sources}
    with tempfile.TemporaryDirectory(prefix='w3-history-') as temporary:
        restored = Path(temporary) / 'runs'
        archive_binding = restore_history(restored)
        for arm in base.ARMS:
            directory = HISTORY / arm
            rows, summary = replay(directory / 'prepared/manifest.json', restored / arm, cases, arm)
            if rows != integrity.read(directory / 'first_score/compiled_first.json'):
                raise ValueError('saved_first_compilation_drift')
            all_rows[arm], summaries[arm] = rows, summary
    extract = {'predictions': all_rows, 'execution': summaries, 'sources': sources,
               'archive_binding': archive_binding,
               'execution_kind': 'offline_replay_of_actual_first_responses', 'new_model_calls': 0}
    panel.write_new(output / 'replayed_first.json', extract)
    # No development reference labels are loaded until first replayed outputs persist.
    golds = base.load_fixture('gold_dev.json')
    result = []
    scores = {}
    for arm in base.ARMS:
        score = panel.score_judgments(golds, all_rows[arm])
        saved = integrity.read(HISTORY / arm / 'first_score/score_first.json')
        if any(saved[k] != v for k, v in score.items()):
            raise ValueError('saved_primary_score_drift')
        scores[arm] = score
        summary = summaries[arm]
        if len(summary['returned_identities']) != 1 or len(summary['observed_dates']) != 1:
            raise ValueError('split_profile_by_returned_identity_and_date')
        model, provider_name = summary['returned_identities'][0]
        sid = source_ids[str((HISTORY / arm / 'first_score/score_first.json').relative_to(ROOT))]
        recipe_sid = source_ids[str(Path(base.__file__).relative_to(ROOT))]
        p = profiles.base_profile(f'w3-{arm}', jev.MODEL, model, provider_name,
            'latest_directed_source_commitment', None, arm, recipe_sid, 'specs',
            score['query_count'], score['available'], summary['observed_dates'][0],
            ['synthetic_dev', 'pl', 'en'], 'Supplied nodes and query; physical source prefix; two Noul questions')
        p['population']['unit'] = 'planned query requests (two dependent binary judgments each)'
        note = 'Correlated authored DEV families, translations and prefixes; not population accuracy.'
        correct = sum(score['confusion'][k][k] for k in panel.LABELS)
        p['metrics'] = {'accuracy': profiles.ratio(correct, score['query_count'], sid, '/confusion', note),
                        'coverage': profiles.ratio(score['available'], score['query_count'], sid, '/available', note)}
        for label in panel.LABELS:
            counts = score['per_class'][label]
            for metric, denominator in [('precision', counts['tp'] + counts['fp']),
                                        ('recall', counts['tp'] + counts['fn'])]:
                p['metrics'][f'{label}_{metric}'] = profiles.ratio(counts['tp'], denominator, sid,
                    f'/per_class/{label}', note)
        p['cost']['batch_ref'] = p['latency']['batch_ref'] = arm
        p['cost']['note'] = 'Cost counted once in batches; this replay incurs no new model cost.'
        p['latency']['note'] = 'Two judgments share one POST; per-question timing unavailable.'
        p['failure_modes'] = [{'kind': 'unknown_classified_as_refuted', 'status': 'reported_observation',
            'description': 'Missing applicable negative commitment classified as refuted; see retained failures.',
            'count': score['confusion']['unknown']['refuted'], 'evidence': profiles.evidence(sid, '/failures')}]
        result.append(p)
    document = {'schema': 'loom.model_profiles/1', 'source_extract_sha256': safe.digest(extract),
        'new_model_calls': 0, 'sources': sources, 'profiles': result, 'batches': summaries,
        'mechanism_evidence': {'kind': 'exact_offline_replay', 'not_new_inference': True},
        'owner_observations': [], 'compensation': {'status': 'proposed_unrun',
            'operation': 'latest_directed_source_commitment',
            'instruction_pl': 'Dopasuj uporządkowane końce relacji i autora przed zastosowaniem wycofania. Wycofane zaprzeczenie nie odżywa po zmianie tematu.',
            'ablation': {'unit': 'Conversation family with correlated languages and temporal prefixes',
                'arms': ['active_refute_v2', 'directed_refute_v3'],
                'primary_criteria': ['All-query accuracy and unknown-to-refuted errors; retain regressions'],
                'secondary_criteria': ['Cost, availability, semantic conflicts, input tokens'],
                'controls': ['Identical source/q01; independent authored DEV; unchanged threshold'],
                'current_results': None, 'promotion': 'requires_frozen_data_authorized_run_and_review'}}}
    issues = profiles.validate(document)
    if issues:
        raise ValueError(issues)
    panel.write_new(output / 'profiles.json', document)
    panel.write_new(output / 'scores.json', scores)
    return {'new_model_calls': 0, 'profiles': len(result), 'verified_sources': len(sources),
            'correct_over_planned': {p['key']['recipe_id']: [p['metrics']['accuracy']['numerator'],
                                    p['metrics']['accuracy']['denominator']] for p in result}}


def score_plan(plan_dir, population, arm, manifest_path, run_dir, output):
    plan = verify_plan(plan_dir)
    match = [b for b in plan['batches'] if b['population'] == population and b['recipe'] == arm]
    if len(match) != 1:
        raise ValueError('plan_population_or_recipe')
    cases = base.load_fixture('inputs_dev.json') if population == 'historical_dev' else load_controls('inputs_dev.json')
    rows, summary = replay(manifest_path, run_dir, cases, arm)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    panel.write_new(output / 'compiled_first.json', rows)
    panel.write_new(output / 'execution.json', summary)
    golds = base.load_fixture('gold_dev.json') if population == 'historical_dev' else load_controls('gold_dev.json')
    result = panel.score_judgments(golds, rows)
    panel.write_new(output / 'score_first.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'historical'):
        sub.add_parser(name).add_argument('--output', type=Path, required=True)
    sub.add_parser('verify').add_argument('--plan', type=Path, required=True)
    score = sub.add_parser('score')
    for name in ('plan', 'manifest', 'run-dir', 'output'):
        score.add_argument('--' + name, type=Path, required=True)
    score.add_argument('--population', choices=('historical_dev', 'independent_dev'), required=True)
    score.add_argument('--arm', choices=('active_refute_v2', 'directed_refute_v3'), required=True)
    args = parser.parse_args()
    if args.command == 'prepare': result = prepare(args.output)
    elif args.command == 'historical': result = historical(args.output)
    elif args.command == 'verify': result = verify_plan(args.plan)
    else: result = score_plan(args.plan, args.population, args.arm, args.manifest, args.run_dir, args.output)
    print(safe.canonical(result).decode())


if __name__ == '__main__':
    main()
