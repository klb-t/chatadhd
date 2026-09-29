"""T3: prepare/verify frozen Jev request bodies. No HTTP, keys or inference.

Input cases are human-readable authored fixtures, not extracted model output.
Gold and annotations are evaluator-only and never consumed by build().
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import io
import json
import math
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[3]
FIXTURE = ROOT / 'loom/tests/fixtures/eval/jev_recipes_v1'


def encode(value):
    return (json.dumps(value, ensure_ascii=False, allow_nan=False, indent=2) + '\n').encode('utf-8')


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read(path: Path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON key')
            result[key] = value
        return result
    def invalid(_):
        raise ValueError('non-finite JSON value')
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=pairs, parse_constant=invalid)


def validate_body(body: dict) -> None:
    """Narrow local contract based on the repository pilot; not endpoint verification."""
    if not isinstance(body, dict) or set(body) != {'model', 'provider', 'state', 'questions'}:
        raise ValueError('invalid request envelope')
    if not isinstance(body['model'], str) or not body['model'] or not isinstance(body['state'], dict):
        raise ValueError('model/state required')
    if body['provider'] != {'only': ['typesafe'], 'allow_fallbacks': False}:
        raise ValueError('provider pin changed')
    if not isinstance(body['questions'], dict) or not body['questions']:
        raise ValueError('questions required')
    for qid, q in body['questions'].items():
        if not qid or not isinstance(q, dict) or set(q) != {'type', 'instructions', 'criteria'}:
            raise ValueError('invalid question')
        if not isinstance(q['instructions'], (str, dict)) or not q['instructions']:
            raise ValueError('instructions required')
        if q['type'] == 'noul':
            if not isinstance(q['criteria'], dict) or set(q['criteria']) != {'true', 'false'}:
                raise ValueError('binary criteria required')
        elif q['type'] == 'choice':
            if not isinstance(q['criteria'], dict) or not 2 <= len(q['criteria']) <= 255:
                raise ValueError('choice criteria required')
        else:
            raise ValueError('unsupported recipe question type')
    encode(body)  # reject non-JSON/non-finite inputs


def instruction(recipes, task, language, arm, candidate_id=''):
    values = [s.replace('{candidate_id}', candidate_id) for s in recipes['rules'][task][language]]
    keys = recipes['arms'][arm]
    if keys is None:
        return '\n'.join(values)
    if len(keys) != len(values) or len(set(keys)) != len(keys):
        raise ValueError('instruction keys must preserve every value exactly once')
    return dict(zip(keys, values))


def question(recipes, task, language, arm, *, candidate_id='', criteria=None):
    binary = criteria is None
    return {'type': 'noul' if binary else 'choice',
            'instructions': instruction(recipes, task, language, arm, candidate_id),
            'criteria': {'true': 'Yes / Tak', 'false': 'No / Nie'} if binary else copy.deepcopy(criteria)}


def build(corpus: dict, recipes: dict):
    """Build using inputs and definitions ONLY. Never read gold to choose a branch."""
    cases = corpus['cases']
    if len({c['id'] for c in cases}) != len(cases):
        raise ValueError('duplicate case identity')
    bodies, records, episodes = {}, [], []

    def add(case, arm, questions, suffix=''):
        rid = case['id'] + '.' + arm + suffix
        name = 'requests/' + rid + '.json'
        if name in bodies or any(ch in rid for ch in '/\\') or '..' in rid:
            raise ValueError('invalid/duplicate request id')
        body = {'model': recipes['model'], 'provider': copy.deepcopy(recipes['provider']),
                'state': copy.deepcopy(case['state']), 'questions': questions}
        validate_body(body)
        raw = encode(body)
        bodies[name] = raw
        records.append({'request_id': rid, 'file': name, 'sha256': digest(raw),
                        'bytes': len(raw), 'case_id': case['id'], 'family': case['family'],
                        'language': case['language'], 'split': case['split'], 'arm': arm,
                        'questions': list(questions), 'status': 'prepared_not_run'})
        return rid

    for case in cases:
        lang = case['language']
        if lang not in {'pl', 'en'} or case['split'] not in {'development', 'validation'}:
            raise ValueError('unknown language/split')
        if case['family'] == 'relation':
            for arm in recipes['arms']:
                add(case, arm, {t: question(recipes, t, lang, arm) for t in ('expressed', 'inferred')})
        elif case['family'] == 'context':
            candidates = case['state']['candidate_subgraphs']
            if len({s['id'] for s in candidates}) != len(candidates):
                raise ValueError('duplicate subgraph')
            for s in candidates:
                if set(s['representations']) != {'label', 'summary', 'full', 'raw'}:
                    raise ValueError('all actual representations must be supplied')
            for arm in recipes['arms']:
                questions = {'relation_relevance': question(recipes, 'relation_relevance', lang, arm)}
                for s in candidates:
                    sid = s['id']
                    questions['relevant_' + sid] = question(recipes, 'subgraph', lang, arm, candidate_id=sid)
                    questions['detail_' + sid] = question(recipes, 'detail', lang, arm,
                                                          candidate_id=sid, criteria=recipes['detail_options'])
                add(case, arm, questions)
        elif case['family'] == 'routing':
            for count in recipes['routing_option_counts']:
                universe = recipes['routing_options'][:count]
                if len(universe) != count:
                    raise ValueError('not enough routing choices')
                # Rotation depends only on the public numeric case id, never on gold.
                shift = (int(case['id'][1:]) - 1) % count
                universe = universe[shift:] + universe[:shift]
                flat_criteria = {o['id']: o['description'][lang] for o in universe}
                arm = 'object_meaningful'
                flat = add(case, arm, {'route': question(recipes, 'route', lang, arm, criteria=flat_criteria)}, f'.flat{count}')
                groups = {group: {o['id']: o['description'][lang] for o in universe if o['group'] == group}
                          for group in recipes['routing_groups']}
                if any(len(v) < 2 for v in groups.values()):
                    raise ValueError('each child needs at least two available categories')
                root = add(case, arm, {'route': question(recipes, 'route_group', lang, arm, criteria=groups)}, f'.root{count}')
                children = {group: add(case, arm, {'route': question(recipes, 'route', lang, arm, criteria=criteria)},
                                       f'.child{count}.{group}') for group, criteria in groups.items()}
                episodes.append({'episode_id': f"{case['id']}.n{count}", 'case_id': case['id'],
                                 'option_count': count, 'flat_request_id': flat, 'root_request_id': root,
                                 'child_request_ids': children,
                                 'routing': 'max_probability; tie follows root criteria order; no gold or second-child rescue',
                                 'max_calls_for_both_strategies': 3})
        else:
            raise ValueError('unknown recipe family')
    return bodies, {'schema': 'loom.jev_recipes_requests/1', 'execution_enabled': False,
                    'authorized_cost_usd': None, 'new_model_calls': 0,
                    'body_count': len(bodies), 'max_calls_if_all_succeed_once':
                    sum(r['family'] != 'routing' for r in records) + 3 * len(episodes),
                    'records': records, 'routing_episodes': episodes}


def next_child(episode: dict, root_body: dict, probabilities: dict) -> str | None:
    """Pure routing helper; incomplete/bad probability distribution -> unresolved."""
    keys = list(root_body['questions']['route']['criteria'])
    if not isinstance(probabilities, dict) or set(probabilities) != set(keys):
        return None
    vals = [probabilities[k] for k in keys]
    if any(type(p) not in (int, float) or not math.isfinite(p) or not 0 <= p <= 1 for p in vals):
        return None
    if abs(sum(vals) - 1) > 0.02:
        return None
    best = max(range(len(keys)), key=vals.__getitem__)
    return episode['child_request_ids'].get(keys[best])


def archive_bytes(bodies: dict[str, bytes], index: dict) -> bytes:
    """Solid-compressed interchange; export recreates exact standalone body bytes."""
    lines = ''.join(json.dumps({'file': name, 'body': json.loads(raw)},
                              ensure_ascii=False, allow_nan=False, separators=(',', ':')) + '\n'
                    for name, raw in sorted(bodies.items())).encode('utf-8')
    out = io.BytesIO()
    with ZipFile(out, 'w', compression=ZIP_DEFLATED, compresslevel=9) as z:
        for name, raw in [('requests.jsonl', lines), ('request_index.json', encode(index))]:
            info = ZipInfo(name, date_time=(2026, 9, 29, 0, 0, 0))
            info.compress_type = ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            z.writestr(info, raw, compress_type=ZIP_DEFLATED, compresslevel=9)
    return out.getvalue()


def unpack(archive: Path):
    """No zip extraction: validate bounded known members and public generated names."""
    with ZipFile(archive) as z:
        if sorted(z.namelist()) != ['request_index.json', 'requests.jsonl']:
            raise ValueError('unexpected archive members')
        if any(i.file_size > 12 * 1024 * 1024 for i in z.infolist()):
            raise ValueError('archive exceeds local resource guard')
        index = json.loads(z.read('request_index.json'))
        bodies = {}
        for line in z.read('requests.jsonl').decode('utf-8').splitlines():
            rec = json.loads(line)
            name = rec['file']
            if name in bodies or not name.startswith('requests/') or not name.endswith('.json'):
                raise ValueError('duplicate or invalid body name')
            if '..' in name or '\\' in name or name.count('/') != 1:
                raise ValueError('unsafe body name')
            validate_body(rec['body'])
            bodies[name] = encode(rec['body'])
    return bodies, index


def verify(root: Path = ROOT):
    fixture = root / 'loom/tests/fixtures/eval/jev_recipes_v1'
    issues = []
    for manifest_name in ('source_freeze.json', 'manifest.json'):
        lock = read(fixture / manifest_name)
        for path, expected in lock['files'].items():
            item = root / path
            if item.resolve().is_relative_to(root.resolve()) and item.is_file():
                if digest(item.read_bytes()) != expected:
                    issues.append('hash:' + path)
            else:
                issues.append('path:' + path)
    bodies, index = build(read(fixture / 'corpus.json'), read(fixture / 'recipes.json'))
    archived, archived_index = unpack(fixture / 'requests.zip')
    if index != archived_index:
        issues.append('request_index differs from input-only regeneration')
    if bodies != archived:
        issues.append('archive differs from input-only regeneration')
    return issues


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=ROOT)
    p.add_argument('--export-to', type=Path)
    args = p.parse_args(argv)
    try:
        issues = verify(args.root)
        if issues:
            print(json.dumps({'valid': False, 'issues': issues})); return 1
        fixture = args.root / 'loom/tests/fixtures/eval/jev_recipes_v1'
        bodies, index = unpack(fixture / 'requests.zip')
        if args.export_to:
            # Refuse overwrite; generated names only, after byte-for-byte verification.
            args.export_to.mkdir(parents=True, exist_ok=False)
            for name, raw in bodies.items():
                target = args.export_to / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)
            (args.export_to / 'request_index.json').write_bytes(encode(index))
        print(json.dumps({'valid': True, 'prepared_bodies': index['body_count'],
                          'live_calls': 0, 'exported': bool(args.export_to)}))
        return 0
    except (OSError, ValueError, KeyError, TypeError):
        print(json.dumps({'valid': False, 'error': 'input_or_export_failure'})); return 2


if __name__ == '__main__':
    raise SystemExit(main())
