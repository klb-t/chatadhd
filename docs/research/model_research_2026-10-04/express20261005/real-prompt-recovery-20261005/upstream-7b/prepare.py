#!/usr/bin/env python3
"""Offline factorial request materializer; all study choices are design data."""
import argparse,hashlib,itertools,json
from pathlib import Path

def canonical(x):return json.dumps(x,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode()
def digest(x):return hashlib.sha256(x).hexdigest()
def read_bound(path,expected):
 raw=path.read_bytes()
 if digest(raw)!=expected:raise ValueError('source_hash_mismatch')
 return json.loads(raw),raw
def write_exact(path,raw):
 path.parent.mkdir(parents=True,exist_ok=True)
 if path.exists() and path.read_bytes()!=raw:raise ValueError('immutable_artifact_collision')
 path.write_bytes(raw)
def prepare(design_path,out):
 design_path=Path(design_path);root=design_path.parent;out=Path(out)
 design_raw=design_path.read_bytes();d=json.loads(design_raw);s=d['source']
 _,inputs_raw=read_bound(root/s['inputs_file'],s['inputs_sha256'])
 _,gold_raw=read_bound(root/s['gold_file'],s['gold_sha256'])
 source,source_raw=read_bound(root/s['prepared_manifest_file'],s['prepared_manifest_sha256'])
 queries={}
 for op in source['operations']:
  if op['metadata']['arm_id']!=s['prepared_arm']:continue
  query_id=op['metadata']['context_preparation']['case_id']
  if query_id not in d['selection']['query_ids']:continue
  body,raw=read_bound((root/s['prepared_manifest_file']).parent/op['request_file'],op['request_sha256'])
  queries[query_id]={'text':body['state']['text'],'source_request_sha256':digest(raw)}
 if set(queries)!=set(d['selection']['query_ids']):raise ValueError('selected_query_missing')
 configurations=[];operations=[];crosswalk=[]
 for strategy,temp,maximum,pref in itertools.product(d['prompt_strategies'],d['temperatures'],d['max_tokens'],d['user_preferences']):
  config={'strategy_id':strategy['id'],'temperature':temp,'max_tokens':maximum,'preference_id':pref['id']}
  cid='cfg.'+digest(canonical(config))[:20]
  prompt='\n\n'.join([d['prompt_common'],strategy['instructions'],pref['instructions']])
  configurations.append({'configuration_id':cid,**config,'prompt_sha256':digest(prompt.encode()),'prompt_version':'express-prompt-parameters-v1','system_prompt':prompt})
  for qid in d['selection']['query_ids']:
   body={**d['request_defaults'],'temperature':temp,'max_tokens':maximum,'messages':[{'role':'system','content':prompt},{'role':'user','content':queries[qid]['text']}]}
   raw=canonical(body);rh=digest(raw);name='requests/'+rh+'.json';write_exact(out/name,raw)
   oid=d['study_id']+'.'+cid+'.'+qid
   units=d['units_presets'];metadata={'configuration_id':cid,'query_id':qid,'arm_id':cid,'prompt_sha256':digest(prompt.encode()),'prompt_version':'express-prompt-parameters-v1','design_sha256':digest(design_raw),'source_inputs_sha256':digest(inputs_raw),'source_gold_sha256':digest(gold_raw),'source_request_sha256':queries[qid]['source_request_sha256'],'input_text_sha256':digest(queries[qid]['text'].encode())}
   operations.append({'operation_id':oid,'route_id':'chat','request_file':name,'request_sha256':rh,'model_id':body['model'],'provider_id':body['provider']['only'][0],'units_upper_bounds':{'prompt':len(raw)+units['prompt_fixed_allowance']+units['prompt_per_message_allowance']*len(body['messages']),'completion':maximum,'request':units['request'],'reasoning':units['reasoning']},'minimum_reservation_usd':units['minimum_reservation_usd'],'metadata':metadata})
   crosswalk.append({'operation_id':oid,'configuration_id':cid,'query_id':qid,'request_sha256':rh,**config})
 manifest={'schema':'loom.research_programme_manifest/1','programme_id':d['programme_id'],'stage_id':d['stage_id'],'operations':operations,'metadata':{'design_sha256':digest(design_raw),'paid_calls':0,'full_factorial':True,'configuration_count':len(configurations),'query_count':len(queries),'first_response_only':True}}
 for name,value in [('manifest.json',manifest),('configurations.json',configurations),('crosswalk.json',crosswalk)]:write_exact(out/name,canonical(value)+b'\n')
 return manifest
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('--design',type=Path,default=Path(__file__).with_name('design.json'));a.add_argument('--output',type=Path,required=True);args=a.parse_args();m=prepare(args.design,args.output);print(json.dumps({'operations':len(m['operations']),'paid_calls':0}))


# Offline first-response scoring continues the existing preparation. It neither
# dispatches requests nor reads a payer ledger, credentials or private captures.
from datetime import datetime
from decimal import Decimal, InvalidOperation
import re


class ScoringError(ValueError):
    """Fixed diagnostic codes without remote/request text."""


def strict_json(raw):
    def pairs(items):
        value = {}
        for key, item in items:
            if key in value:
                raise ScoringError('duplicate_json_key')
            value[key] = item
        return value

    def constant(_):
        raise ScoringError('nonfinite_json')

    def number(text):
        value = float(text)
        if not Decimal(text).is_finite() or value in (float('inf'), float('-inf')):
            raise ScoringError('nonfinite_json')
        return value

    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant, parse_float=number)


def _bound_score(path, expected):
    raw = Path(path).read_bytes()
    if not isinstance(expected, str) or not re.fullmatch(r'[0-9a-f]{64}', expected):
        raise ScoringError('unbound_artifact')
    if digest(raw) != expected:
        raise ScoringError('artifact_hash_changed')
    return strict_json(raw), raw


def _score_inventory(rows, key, allowed=None):
    if not isinstance(rows, list):
        raise ScoringError('invalid_inventory')
    result = {}
    for row in rows:
        ident = row.get(key) if isinstance(row, dict) else None
        if not isinstance(ident, str) or not ident or ident in result or (allowed is not None and ident not in allowed):
            raise ScoringError('duplicate_or_unknown_inventory_identity')
        result[ident] = row
    return result


def _score_time(value):
    try:
        result = datetime.fromisoformat(value.replace('Z', '+00:00'))
        if result.tzinfo is None:
            raise ValueError
        return result
    except (AttributeError, TypeError, ValueError):
        raise ScoringError('invalid_source_time') from None


def _score_money(value):
    if type(value) not in (str, int, float):
        raise ScoringError('invalid_cost')
    try:
        result = Decimal(str(value))
    except InvalidOperation:
        raise ScoringError('invalid_cost') from None
    if not result.is_finite() or result < 0:
        raise ScoringError('invalid_cost')
    return result


def response_schema_valid(value):
    """Exact ORIGINAL four-field schema; never repair or infer missing fields."""
    if not isinstance(value, dict) or set(value) != {'label', 'evidence', 'explanation', 'counterarguments'}:
        return False
    if value['label'] not in ('supported', 'refuted', 'unknown') or not isinstance(value['explanation'], str):
        return False
    if not isinstance(value['evidence'], list) or not isinstance(value['counterarguments'], list):
        return False
    if any(not isinstance(item, str) for item in value['counterarguments']):
        return False
    return all(isinstance(item, dict) and set(item) == {'turn_id', 'quote'}
               and isinstance(item['turn_id'], str) and bool(item['turn_id'])
               and isinstance(item['quote'], str) and bool(item['quote'])
               for item in value['evidence'])


def score_content(content, source_turns, gold_label, finish_reason=None):
    """Pure mechanical observations; manual adequacy/preferences remain null."""
    result = {'strict_json_valid': False, 'schema_valid': False, 'json_error': None,
              'incomplete_json': False, 'finish_reason': finish_reason,
              'finish_reason_length': finish_reason == 'length', 'label': None,
              'gold_label_match': None, 'evidence_quotes': None, 'quote_count': None,
              'grounded_quote_count': None, 'quote_grounding_ratio': None,
              'all_quotes_grounded': None, 'explanation_characters': None,
              'evidence_turns': None, 'counterarguments_count': None,
              'preference_adherence_manual': None, 'semantic_grounding_manual': None}
    try:
        value = strict_json(content)
    except ScoringError as error:
        result['json_error'] = str(error)
        return result
    except json.JSONDecodeError as error:
        result['json_error'] = 'invalid_json'
        stripped = content.rstrip() if isinstance(content, str) else ''
        result['incomplete_json'] = error.pos >= len(stripped) or error.msg.startswith('Unterminated string')
        return result
    except (UnicodeError, TypeError, ValueError):
        result['json_error'] = 'invalid_json'
        return result
    result['strict_json_valid'] = True
    result['schema_valid'] = response_schema_valid(value)
    if not result['schema_valid']:
        return result
    turns = _score_inventory(source_turns, 'id')
    quotes = []
    for citation in value['evidence']:
        turn = turns.get(citation['turn_id'])
        text = turn.get('text') if turn is not None else None
        matches = len([m.start() for m in re.finditer('(?=' + re.escape(citation['quote']) + ')', text)]) if isinstance(text, str) else 0
        quotes.append({**citation, 'visible_turn': turn is not None,
                       'exact_source_quote': matches > 0, 'source_occurrences': matches,
                       'unique_source_occurrence': matches == 1})
    grounded = sum(q['exact_source_quote'] for q in quotes)
    result.update(label=value['label'], gold_label_match=value['label'] == gold_label,
                  evidence_quotes=quotes, quote_count=len(quotes), grounded_quote_count=grounded,
                  quote_grounding_ratio=grounded / len(quotes) if quotes else None,
                  all_quotes_grounded=grounded == len(quotes) if quotes else None,
                  explanation_characters=len(value['explanation']),
                  evidence_turns=len({q['turn_id'] for q in quotes}),
                  counterarguments_count=len(value['counterarguments']))
    return result


def _scoring_sources(design_path, expected_design_sha256):
    d, design_raw = _bound_score(design_path, expected_design_sha256)
    root = Path(design_path).parent
    inputs, _ = _bound_score(root / d['source']['inputs_file'], d['source']['inputs_sha256'])
    gold, _ = _bound_score(root / d['source']['gold_file'], d['source']['gold_sha256'])
    cases = _score_inventory(inputs.get('cases'), 'id')
    gold_cases = _score_inventory(gold.get('cases'), 'id')
    if set(cases) != set(gold_cases):
        raise ScoringError('gold_source_family_mismatch')
    queries = {}
    for ident, case in cases.items():
        source_queries = _score_inventory(case.get('judgment_queries'), 'id')
        judgments = _score_inventory(gold_cases[ident].get('judgments'), 'query_id')
        _score_inventory(case.get('turns'), 'id')
        if set(source_queries) != set(judgments):
            raise ScoringError('gold_source_query_mismatch')
        for qid, query in source_queries.items():
            if qid in queries or judgments[qid].get('label') not in ('supported', 'refuted', 'unknown'):
                raise ScoringError('gold_query_identity_invalid')
            visible = [t for t in case['turns'] if _score_time(t['known_at']) <= _score_time(query['as_of'])]
            payload = {'case_id': case['id'], 'source_id': case['source_id'], 'turns': visible,
                       'node_inventory': case['node_inventory'], 'query': query}
            queries[qid] = {'case': case, 'query': query, 'visible': visible,
                            'payload': payload, 'gold_label': judgments[qid]['label']}
    chosen = d['selection']['query_ids']
    if len(chosen) != len(set(chosen)) or any(q not in queries for q in chosen):
        raise ScoringError('selected_query_inventory_changed')
    if {queries[q]['case']['id'] for q in chosen} != set(d['selection']['family_ids']):
        raise ScoringError('selected_family_inventory_changed')
    path = root / d['source']['prepared_manifest_file']
    source, _ = _bound_score(path, d['source']['prepared_manifest_sha256'])
    found = {}
    for op in source['operations']:
        if op['metadata']['arm_id'] != d['source']['prepared_arm']:
            continue
        qid = op['metadata']['context_preparation']['case_id']
        if qid not in chosen:
            continue
        if qid in found:
            raise ScoringError('duplicate_source_query')
        body, raw = _bound_score(path.parent / op['request_file'], op['request_sha256'])
        text = body['state']['text']
        if strict_json(text) != queries[qid]['payload']:
            raise ScoringError('source_time_query_text_changed')
        found[qid] = {'text': text, 'sha256': digest(raw)}
    if set(found) != set(chosen):
        raise ScoringError('selected_source_query_missing')
    return d, design_raw, {q: {**queries[q], **found[q]} for q in chosen}


def score_first_responses(design_path, manifest_path, bundle_raw, *,
                          expected_design_sha256, expected_manifest_sha256,
                          expected_bundle_sha256, observed_on):
    """Score hash-bound loom.programme_results/1, preserving every planned slot.

    Public projections/attestations are cross-checked, not claimed as private
    capture replay. Exact source/gold/design/request identities are mandatory.
    Repeated rows, captures, generations and retries are rejected, never selected.
    """
    if not isinstance(observed_on, str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', observed_on):
        raise ScoringError('scoring_date_required')
    try:
        datetime.fromisoformat(observed_on)
    except ValueError:
        raise ScoringError('invalid_scoring_date') from None
    d, design_raw, sources = _scoring_sources(design_path, expected_design_sha256)
    manifest, manifest_raw = _bound_score(manifest_path, expected_manifest_sha256)
    if digest(bundle_raw) != expected_bundle_sha256:
        raise ScoringError('bundle_hash_changed')
    bundle = strict_json(bundle_raw)
    if (manifest.get('schema') != 'loom.research_programme_manifest/1'
            or manifest.get('programme_id') != d['programme_id'] or manifest.get('stage_id') != d['stage_id']
            or manifest.get('metadata', {}).get('design_sha256') != digest(design_raw)
            or manifest.get('metadata', {}).get('first_response_only') is not True):
        raise ScoringError('manifest_design_changed')
    operations = _score_inventory(manifest.get('operations'), 'operation_id')
    expected = []; descriptors = {}; bodies = {}
    for strategy, temp, maximum, pref in itertools.product(d['prompt_strategies'], d['temperatures'], d['max_tokens'], d['user_preferences']):
        config = {'strategy_id': strategy['id'], 'temperature': temp, 'max_tokens': maximum, 'preference_id': pref['id']}
        cid = 'cfg.' + digest(canonical(config))[:20]
        if cid in descriptors:
            raise ScoringError('duplicate_configuration')
        prompt = '\n\n'.join([d['prompt_common'], strategy['instructions'], pref['instructions']])
        descriptors[cid] = {**config, 'configuration_id': cid, 'prompt_sha256': digest(prompt.encode())}
        for qid in d['selection']['query_ids']:
            oid = d['study_id'] + '.' + cid + '.' + qid
            expected.append(oid)
            body = {**d['request_defaults'], 'temperature': temp, 'max_tokens': maximum,
                    'messages': [{'role': 'system', 'content': prompt}, {'role': 'user', 'content': sources[qid]['text']}]}
            op = operations.get(oid)
            if op is None:
                raise ScoringError('planned_operation_missing')
            path = (Path(manifest_path).parent / op['request_file']).resolve()
            if not path.is_relative_to(Path(manifest_path).parent.resolve()):
                raise ScoringError('request_outside_manifest')
            actual, _ = _bound_score(path, op['request_sha256'])
            metadata = op.get('metadata', {})
            if (actual != body or canonical(actual) != path.read_bytes()
                    or op.get('route_id') != 'chat' or op.get('model_id') != body['model']
                    or op.get('provider_id') != body['provider']['only'][0]
                    or metadata.get('configuration_id') != cid or metadata.get('arm_id') != cid
                    or metadata.get('query_id') != qid or metadata.get('prompt_sha256') != digest(prompt.encode())
                    or metadata.get('design_sha256') != digest(design_raw)
                    or metadata.get('source_inputs_sha256') != d['source']['inputs_sha256']
                    or metadata.get('source_gold_sha256') != d['source']['gold_sha256']
                    or metadata.get('source_request_sha256') != sources[qid]['sha256']
                    or metadata.get('input_text_sha256') != digest(sources[qid]['text'].encode())):
                raise ScoringError('exact_request_source_configuration_changed')
            bodies[oid] = actual
    if list(operations) != expected:
        raise ScoringError('operation_order_or_inventory_changed')
    if (bundle.get('schema') != 'loom.programme_results/1' or bundle.get('programme_id') != manifest['programme_id']
            or bundle.get('stage_id') != manifest['stage_id'] or bundle.get('manifest_sha256') != digest(manifest_raw)
            or bundle.get('planned_operation_ids') != expected or bundle.get('planned_operations') != len(expected)):
        raise ScoringError('normalized_bundle_binding_changed')
    rows = _score_inventory(bundle.get('rows'), 'operation_id', operations)
    responses = _score_inventory(bundle.get('responses'), 'operation_id', operations)
    requests = _score_inventory(bundle.get('requests'), 'operation_id', operations)
    if set(requests) != set(operations) or set(responses) - set(rows) or bundle.get('saved_row_count') != len(rows):
        raise ScoringError('first_response_inventory_changed')
    row_generations = [row.get('generation_id') for row in rows.values() if row.get('generation_id') is not None]
    if any(not isinstance(g, str) or not g for g in row_generations) or len(set(row_generations)) != len(row_generations):
        raise ScoringError('duplicate_or_changed_generation')
    records = []; generation_ids = set(); capture_hashes = set(); billing_hashes = set()
    for ident, op in operations.items():
        meta = op['metadata']; request = requests[ident]
        public_meta = {key: meta.get(key) for key in ('arm_id', 'prepared_request_id', 'source_manifest_sha256')}
        if (request.get('request_sha256') != op['request_sha256'] or request.get('body') != bodies[ident]
                or request.get('metadata') != public_meta
                or any(request.get(k) != op[k] for k in ('route_id', 'model_id', 'provider_id'))):
            raise ScoringError('public_request_changed')
        row = rows.get(ident); capture = responses.get(ident); projected = None if capture is None else capture.get('projection')
        http_ok = row is not None and type(row.get('http_status')) is int and 200 <= row['http_status'] < 300
        envelope = isinstance(projected, dict) and isinstance(projected.get('choices'), list) and len(projected['choices']) == 1
        choice = projected['choices'][0] if envelope else None
        envelope = envelope and isinstance(choice, dict) and isinstance(choice.get('message'), dict)
        if envelope:
            message = choice['message']
            envelope = (message.get('role') == 'assistant' and (message.get('content') is None or isinstance(message.get('content'), str))
                        and (choice.get('finish_reason') is None or isinstance(choice.get('finish_reason'), str))
                        and isinstance(projected.get('id'), str) and bool(projected['id'])
                        and isinstance(projected.get('model'), str) and bool(projected['model']))
        content = choice['message'].get('content') if envelope else None
        nonempty = isinstance(content, str) and bool(content.strip())
        finish = choice.get('finish_reason') if envelope else None
        available = http_ok and envelope and nonempty
        cost = None
        if row is not None:
            if (row.get('manifest_sha256') != digest(manifest_raw) or row.get('request_sha256') != op['request_sha256']
                    or row.get('metadata') != public_meta or row.get('requested_model') != op['model_id']
                    or row.get('requested_provider') != op['provider_id']):
                raise ScoringError('first_row_binding_changed')
            if capture is not None:
                rh = row.get('response_sha256'); gh = row.get('generation_sha256')
                if (not isinstance(rh, str) or not re.fullmatch(r'[0-9a-f]{64}', rh)
                        or capture.get('response_sha256') != rh or capture.get('generation_sha256') != gh
                        or gh is not None and (not isinstance(gh, str) or not re.fullmatch(r'[0-9a-f]{64}', gh))):
                    raise ScoringError('first_capture_binding_changed')
                if available and rh in capture_hashes:
                    raise ScoringError('duplicate_first_capture')
                if available:
                    capture_hashes.add(rh)
            generation = projected.get('id') if isinstance(projected, dict) else None
            if generation is not None:
                if not isinstance(generation, str) or not generation or generation != row.get('generation_id') or generation in generation_ids:
                    raise ScoringError('duplicate_or_changed_generation')
                generation_ids.add(generation)
            if isinstance(projected, dict) and projected.get('model') != row.get('response_model'):
                raise ScoringError('response_model_changed')
            if row.get('billing_verified') is True and row.get('billing_replay_verified') is True:
                receipt = capture.get('generation_projection', {}) if capture is not None else {}
                gh = row.get('generation_sha256')
                if (row.get('is_byok') is not False or projected is None or projected.get('usage', {}).get('is_byok') is True
                        or row.get('billing_mode') not in (None, 'credits') or not isinstance(gh, str)
                        or receipt.get('is_byok') is not False or receipt.get('id') != generation
                        or receipt.get('model') != row.get('observed_model')
                        or receipt.get('provider_name') != row.get('observed_provider') or gh in billing_hashes):
                    raise ScoringError('billing_projection_changed')
                cost = _score_money(row.get('actual_cost_usd'))
                if cost != _score_money(receipt.get('total_cost')) or cost != _score_money(row.get('reported_cost_usd')) or cost != _score_money(projected.get('usage', {}).get('cost')):
                    raise ScoringError('verified_cost_changed')
                billing_hashes.add(gh)
        source = sources[meta['query_id']]
        observed = score_content(content, source['visible'], source['gold_label'], finish) if available else {
            'strict_json_valid': None, 'schema_valid': None, 'json_error': None,
            'finish_reason': finish, 'finish_reason_length': finish == 'length', 'incomplete_json': None,
            'label': None, 'gold_label_match': None, 'evidence_quotes': None, 'quote_count': None,
            'grounded_quote_count': None, 'quote_grounding_ratio': None, 'all_quotes_grounded': None,
            'explanation_characters': None, 'evidence_turns': None, 'counterarguments_count': None,
            'preference_adherence_manual': None, 'semantic_grounding_manual': None}
        if available and (row.get('response_available') is not True or row.get('response_ledger_bound') is not True
                          or row.get('exact_sent_request_capture_verified') is not True or not isinstance(projected.get('id'), str)):
            raise ScoringError('available_response_attestation_missing')
        produced_by = {'configuration_id': meta['configuration_id'], 'method_version': 'express-prompt-parameters-v1',
                       'prompt_sha256': meta['prompt_sha256'],
                       'parameters': {key: descriptors[meta['configuration_id']][key] for key in
                                      ('strategy_id', 'temperature', 'max_tokens', 'preference_id')},
                       'requested_model': op['model_id'], 'requested_provider': op['provider_id'],
                       'observed_model': None if row is None else row.get('observed_model'),
                       'observed_provider': None if row is None else row.get('observed_provider')}
        records.append({'operation_id': ident, 'configuration_id': meta['configuration_id'], 'query_id': meta['query_id'],
                        'family_id': source['case']['id'], 'language': source['case']['language'],
                        'observed_on': observed_on, 'produced_by': produced_by,
                        'attempted': row is not None, 'http_2xx': http_ok, 'valid_provider_envelope': bool(envelope),
                        'nonempty_content': nonempty, 'available': bool(available), **observed,
                        'actual_cost_usd': format(cost, 'f') if cost is not None else None,
                        'observed_model': None if row is None else row.get('observed_model'),
                        'observed_provider': None if row is None else row.get('observed_provider'),
                        'provider_reported_input_tokens': None if row is None else row.get('input_tokens'),
                        'provider_reported_output_tokens': None if row is None else row.get('output_tokens'),
                        'response_sha256': None if row is None else row.get('response_sha256')})
    groups = []
    for cid, descriptor in descriptors.items():
        selected = [r for r in records if r['configuration_id'] == cid]
        correct = sum(r['gold_label_match'] is True for r in selected)
        valid = sum(r['schema_valid'] is True for r in selected)
        known = sum((_score_money(r['actual_cost_usd']) for r in selected if r['actual_cost_usd'] is not None), Decimal(0))
        attempted = sum(r['attempted'] for r in selected)
        unattempted = len(selected) - attempted
        unknown = sum(r['attempted'] and r['actual_cost_usd'] is None for r in selected)
        quoted = sum(r['quote_count'] for r in selected if r['quote_count'] is not None)
        grounded = sum(r['grounded_quote_count'] for r in selected if r['grounded_quote_count'] is not None)
        observed_models = sorted({r['observed_model'] for r in selected if isinstance(r['observed_model'], str)})
        observed_providers = sorted({r['observed_provider'] for r in selected if isinstance(r['observed_provider'], str)})
        produced_by = {**selected[0]['produced_by'],
                       'observed_model': observed_models[0] if len(observed_models) == 1 else None,
                       'observed_provider': observed_providers[0] if len(observed_providers) == 1 else None,
                       'observed_models': observed_models, 'observed_providers': observed_providers}
        groups.append({**descriptor, 'planned_queries': len(selected), 'observed_on': observed_on,
                       'produced_by': produced_by,
                       'evidence': {'normalized_bundle_sha256': digest(bundle_raw),
                                    'operation_ids': [r['operation_id'] for r in selected]},
                       'counts': {key: sum(r[key] is True for r in selected) for key in
                                  ('attempted', 'http_2xx', 'valid_provider_envelope', 'nonempty_content', 'available',
                                   'strict_json_valid', 'schema_valid', 'finish_reason_length', 'incomplete_json')},
                       'correct_labels': correct, 'label_match_all_planned': correct / len(selected) if attempted else None,
                       'label_match_valid_schema': correct / valid if valid else None,
                       'missing_semantic_results': sum(r['gold_label_match'] is None for r in selected),
                       'observed_wrong_labels': sum(r['gold_label_match'] is False for r in selected),
                       'quote_grounding': {'quoted_items': quoted, 'literal_matches': grounded,
                                           'ratio': grounded / quoted if quoted else None,
                                           'schema_valid_without_quotes': sum(r['schema_valid'] is True and r['quote_count'] == 0 for r in selected)},
                       'observable_preference_features': {
                           'query_ids_in_source_order': [r['query_id'] for r in selected],
                           'explanation_character_counts': [r['explanation_characters'] for r in selected],
                           'evidence_turn_counts': [r['evidence_turns'] for r in selected],
                           'counterargument_counts': [r['counterarguments_count'] for r in selected],
                           'no_automatic_adherence_score': True},
                       'known_verified_cost_usd': format(known, 'f'), 'unknown_cost_operations': unknown,
                       'unattempted_operations': unattempted,
                       'cost_scope': 'Saved first attempts only; unattempted slots are not unknown charges.',
                       'total_cost_usd': format(known, 'f') if unknown == 0 else None,
                       'preference_adherence_manual': None, 'semantic_grounding_manual': None})
    return {'schema': 'loom.express_prompt_parameters.first_response_score/1',
            'programme_id': manifest['programme_id'], 'stage_id': manifest['stage_id'],
            'observed_on': observed_on,
            'input_sha256': {'design': digest(design_raw), 'manifest': digest(manifest_raw),
                             'bundle': digest(bundle_raw), 'source': d['source']['inputs_sha256'],
                             'gold': d['source']['gold_sha256'], 'scorer': digest(Path(__file__).read_bytes())},
            'planned_operations': len(expected), 'first_rows': len(rows), 'records': records, 'configurations': groups,
            'first_response_only': True, 'answer_repair': False, 'new_model_calls': 0,
            'native_graph_write': False, 'holdout_evaluation': False,
            'evidence_boundary': 'Authored visible synthetic sources; normalized first-response projections and billing attestations, not private capture replay. Literal quote matching is not semantic adequacy. Length/counts are observable preference features, not preference success scores.'}


def shared_metrics_by_configuration(score, *, source_id):
    """Pure portable ModelProfile metric projection, not native graph storage.

    source_id identifies the saved score artifact. Evidence pointers refer to
    that artifact's configuration groups; a caller may rebind them on export.
    This returns per-configuration dictionaries rather than assuming that an
    exporter group with another observed model has the same cohort of queries.
    """
    if not isinstance(source_id, str) or not source_id:
        raise ScoringError('metric_source_id_required')
    if score.get('schema') != 'loom.express_prompt_parameters.first_response_score/1':
        raise ScoringError('metric_score_schema_changed')
    result = {}
    for index, group in enumerate(score['configurations']):
        planned = group['planned_queries']; attempted = group['counts']['attempted']
        prefix = '/configurations/' + str(index)
        metrics = {}

        def metric(name, value, unit, note, path, numerator=None, denominator=None):
            metrics[name] = {'value': value, 'numerator': numerator, 'denominator': denominator,
                             'method': 'unavailable' if value is None else 'counted_ratio' if denominator is not None else 'reported_scalar',
                             'unit': unit, 'note': note,
                             'evidence': {'source_id': source_id, 'location': prefix + path}}

        metric('source_gold_label_match_all_planned', group['label_match_all_planned'], 'ratio',
               'Frozen source-relative gold, all planned slots retained after an attempt; no-attempt quality is unavailable. Two authored families are not a blind population.',
               '/label_match_all_planned', group['correct_labels'], planned)
        metric('source_gold_label_match_valid_schema', group['label_match_valid_schema'], 'ratio',
               'Conditional agreement among exact-schema outputs; inspect missing semantic results and the all-planned denominator separately.',
               '/label_match_valid_schema', group['correct_labels'], group['counts']['schema_valid'])
        for key in ('http_2xx', 'valid_provider_envelope', 'nonempty_content', 'available', 'strict_json_valid',
                    'schema_valid', 'finish_reason_length', 'incomplete_json'):
            count = group['counts'][key]
            metric(key + '_all_planned', count / planned if attempted else None, 'ratio',
                   'Separate first-response observation with all planned slots retained; no attempts yields null, not observed model quality.',
                   '/counts/' + key, count, planned)
        quotes = group['quote_grounding']
        metric('literal_source_quote_match', quotes['ratio'], 'ratio',
               'Exact contiguous matches inside the visible source turn; repeated substrings are valid. Zero quotes is null. This is not semantic adequacy.',
               '/quote_grounding', quotes['literal_matches'], quotes['quoted_items'])
        for key in ('known_verified_cost_usd', 'total_cost_usd'):
            value = group[key]
            metric(key, float(Decimal(value)) if value is not None else None, 'USD',
                   'Saved first attempts only; verified monetary decimals remain in the score artifact. Unattempted slots are not unknown charges.',
                   '/' + key)
        for key in ('unknown_cost_operations', 'unattempted_operations'):
            metric(key, group[key], 'operations', 'Missing verified cost for attempted operations is separate from unattempted planned slots.', '/' + key)
        features = group['observable_preference_features']
        for key, unit in (('explanation_character_counts', 'characters'), ('evidence_turn_counts', 'turns'), ('counterargument_counts', 'items')):
            values = [v for v in features[key] if v is not None]
            total = sum(values)
            metric('mean_' + key, total / len(values) if values else None, unit,
                   'Observable feature among exact-schema responses, not automatic preference adherence. Longer or shorter alone gives no success score.',
                   '/observable_preference_features/' + key, total, len(values))
        for key in ('preference_adherence_manual', 'semantic_grounding_manual'):
            metric(key, None, 'score', 'Requires the separately frozen source-relative manual rubric and assessor evidence; not inferred by this mechanical scorer.', '/' + key)
        result[group['configuration_id']] = metrics
    return result
