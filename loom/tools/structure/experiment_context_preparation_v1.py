"""Source-bound offline context preparations; no inference, dispatch or profiles engine.

All source content and requests are private caller data. Configuration carries
prompts, parameters, dimension values and selection policies. Native source
graphs are structural export graphs, never asserted to be semantic extraction.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import itertools
import json
from pathlib import Path

from loom.tools.structure.experiment_workflow_v1 import canonical, digest, require, strict_json, variants

VERSION = 'experiment_context_preparation_v1/3'


def text_projection(message):
    """Project literal native text fields with explicit field provenance.

    Anthropic can carry available top-level text alongside non-text content
    blocks. Different native text representations are retained separately;
    byte-identical duplicates receive an explicit alias instead of duplication.
    """
    if message is None:
        return {'parts': [], 'fields': [], 'nontext_parts_omitted': 0, 'equivalent_aliases': []}
    content = message.get('content', {})
    texts, fields, omitted, aliases = [], [], 0, []
    if isinstance(content, str):
        texts.append(content); fields.append('/content')
        parts, base = [], '/content'
    elif isinstance(content, list):
        parts, base = content, '/content'
    else:
        parts, base = content.get('parts', []), '/content/parts'
        if isinstance(content.get('text'), str):
            texts.append(content['text']); fields.append('/content/text')
        elif not parts and content:
            omitted += 1
    for index, part in enumerate(parts):
        if isinstance(part, str):
            texts.append(part); fields.append(base + '/' + str(index))
        elif isinstance(part, dict) and part.get('type') == 'text' and isinstance(part.get('text'), str):
            texts.append(part['text']); fields.append(base + '/' + str(index) + '/text')
        else:
            omitted += 1
    if isinstance(message.get('text'), str):
        native = message['text']
        if native in texts:
            aliases.append({'field': '/text', 'identical_to_field': fields[texts.index(native)]})
        elif texts and native == ''.join(texts):
            aliases.append({'field': '/text', 'identical_to_concatenation_of_fields': list(fields), 'separator': ''})
        else:
            texts.append(native); fields.append('/text')
    return {'parts': texts, 'fields': fields, 'nontext_parts_omitted': omitted, 'equivalent_aliases': aliases}


def text_parts(message):
    projection = text_projection(message)
    return projection['parts'], projection['nontext_parts_omitted']


def validate_source(source):
    require(source.get('schema') == 'loom.research_context_source/1', 'invalid_context_source_schema')
    require(bool(source.get('family_id')), 'missing_source_family')
    require(len(source.get('source_sha256', '')) == 64, 'missing_original_source_hash')
    nodes = source['nodes']
    require(isinstance(nodes, list) and bool(nodes), 'empty_source_nodes')
    ids = [n['node_id'] for n in nodes]
    require(len(ids) == len(set(ids)), 'duplicate_source_node')
    by_id = {n['node_id']: n for n in nodes}
    for node in nodes:
        require('native_message' in node and 'source_pointer' in node, 'raw_message_and_pointer_required')
        parent = node.get('parent_node_id')
        require(parent is None or parent in by_id or parent in source.get('external_parent_ids', []), 'unresolved_source_parent')
        seen, current = set(), node['node_id']
        while current is not None and current in by_id:
            require(current not in seen, 'source_parent_cycle')
            seen.add(current)
            current = by_id[current].get('parent_node_id')
    require(source.get('split') in ('development', 'validation'), 'invalid_source_split')
    require(source.get('prior_use') != 'used_blind', 'used_blind_cannot_be_relabelled')
    return source


def validate_corpus(sources):
    families, hashes = {}, {}
    for source in sources:
        validate_source(source)
        for table, key in ((families, source['family_id']), (hashes, source['source_sha256'])):
            require(key not in table or table[key] == source['split'], 'source_family_split_leakage')
            table[key] = source['split']
    return sources


def ancestor_ids(source, boundary):
    by_id = {n['node_id']: n for n in source['nodes']}
    require(boundary in by_id, 'unknown_checkpoint_boundary')
    result, current = [], boundary
    while current is not None and current in by_id:
        result.append(current)
        current = by_id[current].get('parent_node_id')
    return list(reversed(result))


def selection(source, task, mode, policy):
    all_ids = [n['node_id'] for n in source['nodes']]
    if mode == 'full':
        return all_ids, {'kind': 'all_native_nodes', 'user_declared_checkpoint': False}
    if mode == 'checkpoint':
        boundary = task.get('checkpoint_boundary_node_id')
        if boundary is None:
            return None, {'pending_reason': 'observed_checkpoint_boundary_missing'}
        if source.get('parent_semantics') == 'not_available':
            order = source['source_order_node_ids']
            require(boundary in order, 'unknown_checkpoint_boundary')
            return order[:order.index(boundary) + 1], {
                'kind': 'derived_source_array_prefix_snapshot', 'boundary_node_id': boundary,
                'user_declared_checkpoint': False, 'summary_generated': False,
                'native_ancestry_known': False, 'array_order_is_not_claimed_chronology': True}
        return ancestor_ids(source, boundary), {
            'kind': 'derived_source_path_snapshot', 'boundary_node_id': boundary,
            'user_declared_checkpoint': False, 'summary_generated': False,
            'selection_authority': task.get('boundary_authority', 'research_protocol')}
    if mode == 'selected_subgraph':
        ids = task.get('selected_node_ids')
        if ids is None:
            boundary = task.get('checkpoint_boundary_node_id')
            if boundary is None:
                return None, {'pending_reason': 'subgraph_anchor_missing'}
            count = policy['ancestor_window_nodes']
            require(type(count) is int and count > 0, 'invalid_ancestor_window')
            if source.get('parent_semantics') == 'not_available':
                order = source['source_order_node_ids']
                require(boundary in order, 'unknown_checkpoint_boundary')
                ids = order[max(0, order.index(boundary) + 1-count):order.index(boundary) + 1]
            else:
                ids = ancestor_ids(source, boundary)[-count:]
        require(bool(ids) and len(ids) == len(set(ids)), 'empty_or_duplicate_selected_nodes')
        require(set(ids) <= set(all_ids), 'unknown_selected_node')
        return [x for x in all_ids if x in set(ids)], {
            'kind': 'explicit_structural_subgraph', 'selection_policy': deepcopy(policy),
            'semantic_relevance_inferred': False, 'user_declared_checkpoint': False,
            'basis': 'source_array_window' if source.get('parent_semantics') == 'not_available' else 'native_ancestor_window'}
    raise ValueError('unsupported_context_resolution')


def prepare_view(source, task, representation, resolution, policy):
    validate_source(source)
    selected, selection_record = selection(source, task, resolution, policy)
    identity = {'producer': VERSION, 'source_sha256': digest(source), 'task_sha256': digest(task),
                'representation': representation, 'resolution': resolution, 'policy': policy}
    if selected is None:
        return {'id': 'context:' + digest(identity), 'status': 'pending',
                'input_sha256': digest(source), 'output_sha256': None,
                'dependency': selection_record, 'transform': identity}
    by_id = {n['node_id']: n for n in source['nodes']}
    nodes = [by_id[x] for x in selected]
    selected_set = set(selected)
    boundary_edges = [{'child': n['node_id'], 'parent': n['parent_node_id']}
                      for n in nodes if n.get('parent_node_id') is not None and n['parent_node_id'] not in selected_set]
    losses = {'omitted_node_ids': [n['node_id'] for n in source['nodes'] if n['node_id'] not in selected_set],
              'boundary_edges': boundary_edges, 'nontext_parts_omitted': 0,
              'unresolved_native_parent_ids': deepcopy(source.get('external_parent_ids', [])),
              'native_metadata_omitted_from_request': False, 'attachments_decoded': False,
              'conversation_wrapper_metadata_in_request': False,
              'attachment_binary_coverage': source.get('attachment_binary_coverage')}
    augmentation = ['Source nodes and source pointers are encoded as research context data.']
    if representation == 'exact_text':
        segments = []
        for node in nodes:
            projected = text_projection(node['native_message'])
            losses['nontext_parts_omitted'] += projected['nontext_parts_omitted']
            segments.append({'node_id': node['node_id'], 'role': node.get('role'),
                             'source_pointer': node['source_pointer'], 'exact_text_parts': projected['parts'],
                             'native_text_field_pointers': projected['fields'],
                             'equivalent_native_text_aliases': projected['equivalent_aliases']})
        # JSON envelopes preserve every text string and part boundary, including
        # whitespace. This is not a lossless representation of native metadata.
        payload = {'segments': segments}
        losses['native_metadata_omitted_from_request'] = True
        augmentation.append('Text strings are exact; envelopes and role labels are structural framing.')
    elif representation == 'explicit_fields':
        fields = policy['explicit_fields']
        require(isinstance(fields, list) and bool(fields), 'empty_explicit_fields')
        require('native_message' in fields and 'node_id' in fields, 'structural_projection_requires_source_content')
        payload = {'records': [{k: deepcopy(n.get(k)) for k in fields} for n in nodes]}
        losses['native_metadata_omitted_from_request'] = any(set(n) - set(fields) for n in nodes)
        losses['omitted_wrapper_fields'] = sorted(set().union(*(set(n) - set(fields) for n in nodes)))
        augmentation.append('Only caller-selected explicit fields are projected; no semantic field extraction.')
    elif representation == 'source_graph':
        payload = {'nodes': deepcopy(nodes),
                   'edges': [{'source': n['parent_node_id'], 'target': n['node_id'], 'relation': 'native_parent'}
                             for n in nodes if n.get('parent_node_id') in selected_set],
                   'boundary_edges': boundary_edges}
        augmentation.append('Native parent edges are structural export facts, not inferred semantic relations.')
    else:
        raise ValueError('unsupported_context_representation')
    output = {'representation': representation, 'resolution': resolution,
              'source_family_id': source['family_id'], 'selection': selection_record, 'payload': payload}
    return {'id': 'context:' + digest(identity), 'status': 'prepared', 'transform': identity,
            'input_sha256': digest(source), 'original_source_sha256': source['source_sha256'],
            'output_sha256': digest(output), 'exact_output': output,
            'loss': losses, 'augmentation': augmentation, 'semantic_inference_performed': False}


def preference_context(source, mode, profiles, selected_profile, visible_node_ids=None):
    if mode == 'none':
        return {'status': 'prepared', 'mode': mode, 'value': None, 'user_preference_claimed': False}
    if mode == 'source_explicit':
        annotations = source.get('explicit_preference_evidence', [])
        review = source.get('preference_review')
        if review is not None:
            require(review.get('schema') == 'loom.research_preference_source_review/1', 'invalid_preference_review_schema')
            require(review.get('status') == 'completed', 'incomplete_preference_review')
            require(review.get('source_sha256') == source['source_sha256'], 'preference_review_source_mismatch')
            inventory = {n['node_id']: digest(n['native_message']) for n in source['nodes']}
            require(review.get('source_message_inventory_sha256') == digest(inventory), 'preference_review_inventory_mismatch')
            require(review.get('accepted_evidence_sha256') == digest(annotations), 'preference_review_evidence_mismatch')
            require(review.get('profile_adoption') is False, 'preference_review_cannot_adopt_profile')
            require(visible_node_ids is not None, 'reviewed_preference_visibility_required')
            require(set(visible_node_ids) <= set(inventory), 'preference_visibility_unknown_node')
        if not annotations and review is None:
            return {'status': 'pending', 'mode': mode, 'pending_reason': 'explicit_source_preference_annotations_missing'}
        by_id = {n['node_id']: n for n in source['nodes']}
        for evidence in annotations:
            require(evidence.get('node_id') in by_id, 'preference_source_node_missing')
            node = by_id[evidence['node_id']]
            require(node.get('role') == 'user', 'preference_is_not_user_statement')
            require(evidence.get('source_pointer') == node.get('source_message_pointer', node['source_pointer']), 'preference_pointer_mismatch')
            require(evidence.get('source_message_sha256') == digest(node['native_message']), 'preference_source_hash_mismatch')
            parts, _ = text_parts(node['native_message'])
            require(isinstance(evidence.get('quote'), str) and bool(evidence['quote']), 'preference_quote_missing')
            require(any(evidence['quote'] in text for text in parts), 'preference_quote_not_exact')
            require(evidence.get('annotation_authority') in ('owner_annotation', 'researcher_annotation'), 'preference_authority_missing')
            require(evidence.get('classification') == 'explicit_preference', 'preference_classification_missing')
            require(bool(evidence.get('scope')), 'preference_scope_missing')
        if review is not None:
            visible = set(visible_node_ids)
            annotations = [e for e in annotations if e['node_id'] in visible]
            # Preserve the full dossier privately. Do not leak later review
            # judgements, hidden relations or supersession times into a bounded
            # view merely because the original quote was visible.
            annotations = [{k: deepcopy(e[k]) for k in (
                'evidence_id', 'node_id', 'source_pointer', 'source_message_sha256',
                'quote', 'quote_utf8_sha256', 'literal_field_pointer',
                'quote_start_codepoints', 'quote_end_codepoints', 'role',
                'source_time', 'classification', 'annotation_authority', 'application') if k in e}
                           for e in annotations]
            for evidence in annotations:
                evidence['scope'] = {'source_node_id': evidence['node_id'],
                                     'applicability': 'source_utterance_only_not_active_task_instruction',
                                     'semantic_scope_interpretation': 'retained_privately_not_injected'}
            # The original source hash still binds the reviewed inventory; only
            # unresolved IDs, not posterior explanations, enter the view.
            uncertain = [{'evidence_id': e['evidence_id'], 'node_id': e['node_id'],
                          'reason': 'interpretation_uncertain_see_private_review'}
                         for e in review['uncertain_candidates'] if e['node_id'] in visible]
            return {'status': 'prepared', 'mode': mode, 'value': deepcopy(annotations),
                    'annotation_status': 'two_pass_same_session_provisional',
                    'evidence_use': 'historical_scoped_evidence_not_active_instruction',
                    'empty_result': not annotations, 'known_evidence_only': True,
                    'no_preference_exists_claimed': False,
                    'uncertain_candidates': deepcopy(uncertain),
                    'excluded_by_view_boundary_count': len(source.get('explicit_preference_evidence', [])) - len(annotations),
                    'review_sha256': review['review_sha256'],
                    'annotation_projection_loss': ['posterior_review_reason', 'relations_to_other_evidence',
                                                   'supersession_and_current_applicability', 'semantic_scope_interpretation'],
                    'temporal_resolution': 'unknown_not_injected_from_full_family_review',
                    'user_preference_claimed': bool(annotations), 'profile_engine_mutation': False}
        return {'status': 'prepared', 'mode': mode, 'value': deepcopy(annotations),
                'annotation_status': 'provisional_unless_owner_annotation',
                'user_preference_claimed': True, 'interpretation': 'classification is attributed to the named annotator'}
    if mode == 'selected_profile':
        if selected_profile is None:
            return {'status': 'pending', 'mode': mode, 'pending_reason': 'selected_existing_profile_reference_missing'}
        require(selected_profile in profiles, 'unknown_selected_profile')
        profile = profiles[selected_profile]
        require(all(k in profile for k in ('source_reference', 'source_sha256', 'version', 'value', 'selection_authority')),
                'profile_provenance_missing')
        require(profile['selection_authority'] in ('owner', 'research_protocol'), 'profile_selection_authority_missing')
        return {'status': 'prepared', 'mode': mode, 'value': deepcopy(profile),
                'user_preference_claimed': profile['selection_authority'] == 'owner',
                'profile_engine_mutation': False}
    raise ValueError('unsupported_preference_mode')


def prepare_variant(source, task, variant, plan, profiles):
    view = prepare_view(source, task, variant['context_representation'], variant['context_resolution'], plan['context_policy'])
    visible = None
    if view['status'] == 'prepared':
        omitted = set(view['loss']['omitted_node_ids'])
        visible = [n['node_id'] for n in source['nodes'] if n['node_id'] not in omitted]
    # A missing view is already a pending dependency; no source-wide annotation
    # may enter as a substitute for unavailable context.
    pref = preference_context(source, variant['preference_mode'], profiles, plan.get('selected_profile'),
                              visible_node_ids=visible if visible is not None else [])
    identity = {'source_sha256': digest(source), 'task_sha256': digest(task), 'variant': variant,
                'plan_sha256': digest(plan), 'profiles_sha256': digest(profiles), 'producer': VERSION}
    row = {'id': 'preparation:' + digest(identity), 'identity': identity, 'family_id': source['family_id'],
           'task_id': task['id'], 'split': source['split'], 'variant': deepcopy(variant),
           'view': view, 'preference': pref, 'inference_performed': False,
           'response_cache': False, 'paid_calls': 0, 'actual_cost_usd': '0',
           'cost_estimate_usd': None, 'provider_admission': 'unknown', 'calls': []}
    if view['status'] != 'prepared' or pref['status'] != 'prepared':
        row['status'] = 'pending'
        row['dependencies'] = [v.get('pending_reason', v.get('dependency', {}).get('pending_reason'))
                               for v in (view, pref) if v['status'] != 'prepared']
        return row
    mode = variant['response_form']
    require(mode in plan['response_instructions'], 'unsupported_response_form')
    system = plan['prompts']['system'] + '\n\n' + plan['response_instructions'][mode]
    user = {'task': deepcopy(task['query']), 'context': view['exact_output'], 'preferences': pref}
    body = deepcopy(plan['request_parameters'])
    require('messages' not in body, 'parameters_override_messages')
    body['messages'] = [{'role': 'system', 'content': system},
                        {'role': 'user', 'content': canonical(user).decode('utf-8')}]
    primary = {'id': row['id'] + ':answer', 'kind': 'answer', 'status': 'prepared', 'request': body,
               'request_sha256': digest(body), 'dependencies': [], 'headers': {'X-OpenRouter-Cache': 'false'},
               'requested_parameters': deepcopy(plan['request_parameters']), 'supported_parameters': None,
               'omitted_parameters': None, 'current_cost_upper_bound_usd': None}
    row['calls'].append(primary)
    if mode == 'text_then_structure':
        row['calls'].append({'id': row['id'] + ':extract', 'kind': 'structure_extraction', 'status': 'pending',
                             'request': None, 'request_sha256': None,
                             'dependencies': [{'call_id': primary['id'], 'requires': 'immutable_complete_first_text_response'}],
                             'recipe': deepcopy(plan['extraction_recipe']), 'current_cost_upper_bound_usd': None})
        row['status'] = 'primary_prepared_dependency_pending'
    else:
        row['status'] = 'prepared'
    row['transform_output_sha256'] = digest(row['calls'])
    return row


def materialize_extraction(row, response, plan):
    """Construct a NEW derived second-call request after a real captured response.

    No placeholder output can be turned into a prepared extraction request.
    This function never sends the request or marks the parent as executed.
    """
    require(row['variant']['response_form'] == 'text_then_structure', 'variant_has_no_second_call')
    primary = row['calls'][0]
    require(response.get('call_id') == primary['id'], 'response_call_mismatch')
    require(response.get('status') == 'completed' and response.get('origin') == 'model_inference', 'complete_model_response_required')
    require(response.get('response_cache_hit') is False, 'response_cache_not_independent')
    require(isinstance(response.get('text'), str) and response['text'] != '', 'missing_response_text')
    require(response.get('text_sha256') == hashlib.sha256(response['text'].encode()).hexdigest(), 'response_text_hash_mismatch')
    require(bool(response.get('immutable_capture_sha256')), 'immutable_capture_reference_missing')
    recipe = plan['extraction_recipe']
    body = deepcopy(recipe['request_parameters'])
    body['messages'] = [{'role': 'system', 'content': recipe['system_prompt']},
                        {'role': 'user', 'content': canonical({'answer_text': response['text'],
                          'source_context': row['view']['exact_output']}).decode('utf-8')}]
    return {'id': row['calls'][1]['id'], 'status': 'prepared', 'request': body, 'request_sha256': digest(body),
            'headers': {'X-OpenRouter-Cache': 'false'}, 'recipe_sha256': digest(recipe),
            'input_capture_sha256': response['immutable_capture_sha256'],
            'input_text_sha256': response['text_sha256'], 'parent_request_sha256': primary['request_sha256'],
            'inference_performed': False, 'cost_estimate_usd': None,
            'loss': 'Extraction may omit or misrepresent source/answer; requires a separate evaluated model call.',
            'augmentation': 'Structure is future model interpretation, not source observation.'}


def iter_preparations(sources, tasks, plan, profiles):
    """Streams Cartesian variant product. It never materializes the whole matrix."""
    validate_corpus(sources)
    by_family = {s['family_id']: s for s in sources}
    require(len(by_family) == len(sources), 'duplicate_family_record')
    for task in tasks:
        require(task['family_id'] in by_family, 'task_source_missing')
        for variant in variants({'variants': plan['variants']}):
            yield prepare_variant(by_family[task['family_id']], task, variant, plan, profiles)


def write_private(path, value):
    path = Path(path).resolve()
    require(not any((p / '.git').exists() for p in path.parents), 'private_context_inside_git')
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    raw = canonical(value)
    with path.open('xb') as output:
        output.write(raw)
    path.chmod(0o600)
    return hashlib.sha256(raw).hexdigest()


def prepare_release(sources, tasks, plan, profiles, output):
    output = Path(output).resolve()
    require(not output.exists(), 'new_context_release_directory_required')
    require(not any((p / '.git').exists() for p in (output, *output.parents)), 'private_context_inside_git')
    output.mkdir(parents=True, mode=0o700)
    files, counts, calls = {}, {}, {'prepared': 0, 'pending': 0}
    for name, value in (('sources.json', sources), ('tasks.json', tasks), ('plan.json', plan), ('profiles.json', profiles)):
        files[name] = write_private(output / name, value)
    with (output / 'matrix.jsonl').open('xb') as stream, (output / 'jobs-index.jsonl').open('xb') as index_stream:
        for ordinal, row in enumerate(iter_preparations(sources, tasks, plan, profiles)):
            counts[row['status']] = counts.get(row['status'], 0) + 1
            if row['view']['status'] == 'prepared':
                name = 'views/' + row['view']['output_sha256'] + '.json'
                if name not in files:
                    files[name] = write_private(output / name, row['view']['exact_output'])
                row['view']['output_file'] = name
                del row['view']['exact_output']
            # Shared content-addressed request objects avoid redundant copies.
            for call in row['calls']:
                calls[call['status']] += 1
                if call['status'] == 'prepared':
                    name = 'requests/' + call['request_sha256'] + '.json'
                    if name not in files:
                        files[name] = write_private(output / name, call['request'])
                    call['request_file'] = name
                    del call['request']
                index_stream.write(canonical({'operation_id': call['id'], 'preparation_id': row['id'],
                    'family_id': row['family_id'], 'task_id': row['task_id'], 'split': row['split'],
                    'variant': row['variant'], 'phase': call['kind'], 'status': call['status'],
                    'body_ready': call['status'] == 'prepared', 'dispatch_ready': False,
                    'body_sha256': call.get('request_sha256'), 'body_path': call.get('request_file'),
                    'dependencies': call['dependencies'], 'matrix_ordinal': ordinal,
                    'source_sha256': row['identity']['source_sha256'], 'plan_sha256': row['identity']['plan_sha256'],
                    'response_cache': False}) + b'\n')
            if not row['calls']:
                index_stream.write(canonical({'operation_id': row['id'] + ':answer', 'preparation_id': row['id'],
                    'family_id': row['family_id'], 'task_id': row['task_id'], 'split': row['split'],
                    'variant': row['variant'], 'phase': 'answer', 'status': 'pending',
                    'body_ready': False, 'dispatch_ready': False, 'body_sha256': None, 'body_path': None,
                    'dependencies': row['dependencies'], 'matrix_ordinal': ordinal,
                    'source_sha256': row['identity']['source_sha256'], 'plan_sha256': row['identity']['plan_sha256'],
                    'response_cache': False}) + b'\n')
            row['matrix_ordinal'] = ordinal
            stream.write(canonical(row) + b'\n')
    for name in ('matrix.jsonl', 'jobs-index.jsonl'):
        (output / name).chmod(0o600)
        files[name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
    raw = Path(__file__).read_bytes()
    (output / 'producer.py').write_bytes(raw); (output / 'producer.py').chmod(0o600)
    files['producer.py'] = hashlib.sha256(raw).hexdigest()
    receipt = {'schema': 'loom.context_preparation_receipt/1', 'producer': VERSION,
               'source_families': len(sources), 'tasks': len(tasks), 'slots': sum(counts.values()),
               'slot_status_counts': counts, 'call_status_counts': calls,
               'unique_prepared_request_bodies': len([x for x in files if x.startswith('requests/')]),
               'new_paid_calls': 0, 'new_cost_usd': '0', 'quality': None,
               'current_provider_admission': 'unknown', 'dispatch_ready': False,
               'preceding_checkpoint_sha256': plan['preceding_checkpoint_sha256'],
               'source_family_dependence': True, 'files': files}
    write_private(output / 'MANIFEST.json', receipt)
    return {k: v for k, v in receipt.items() if k != 'files'} | {
        'manifest_sha256': hashlib.sha256((output / 'MANIFEST.json').read_bytes()).hexdigest()}


def existing_panel_sources(graph_view, inputs):
    """Adapter for the verified recovered source graph; no original archive rewrite."""
    cases = {c['id']: c for c in inputs['cases']}
    sources, tasks = [], []
    for case in graph_view['cases']:
        old = cases[case['case_id']]
        family = old['source_id']
        source = {'schema': 'loom.research_context_source/1', 'family_id': family,
                  'split': 'development', 'prior_use': 'historical_development_panel',
                  'source_sha256': case['source_hashes']['original_bytes_sha256'],
                  'source_references': deepcopy(case['source_hashes']),
                  'attachment_binary_coverage': None, 'nodes': deepcopy(case['view']['nodes']),
                  'source_order_semantics': case['view']['order_semantics']}
        sources.append(source)
        turns = {n['turn_id']: n for n in source['nodes'] if n.get('turn_id')}
        for query in old['judgment_queries']:
            anchor = turns[query['as_of_turn_id']]['node_id']
            tasks.append({'id': query['id'], 'family_id': family, 'query': deepcopy(query),
                          'checkpoint_boundary_node_id': anchor,
                          'boundary_authority': 'previous_frozen_question_as_of_turn',
                          'annotation_reference_status': 'provisional_assistant_authored'})
    return sources, tasks


def expanded_panel_sources(panel_directory, task_policy):
    """Consume the separately frozen full-family corpus without redoing selection.

    Tasks are research-authored retrospective inspection, not synthetic source
    conversations or hidden next-turn predictions. Unsupported topology stays
    absent; an exported-array prefix is never labelled native ancestry.
    """
    root = Path(panel_directory).resolve()
    panel = strict_json((root / 'panel.json').read_bytes())
    sources, tasks = [], []
    for selected in panel['sources']:
        path = (root / selected['path']).resolve()
        require(path.is_relative_to(root), 'unsafe_panel_source_path')
        raw = path.read_bytes()
        require(hashlib.sha256(raw).hexdigest() == selected['normalized_sha256'], 'expanded_normalized_source_hash_mismatch')
        normalized = strict_json(raw)
        messages = {m['node_id']: m for m in normalized['messages']}
        nodes = []
        for node in normalized['source_graph']:
            require(len(node['parent_ids']) <= 1, 'multiple_parents_require_explicit_adapter')
            message = messages.get(node['node_id'])
            nodes.append({'node_id': node['node_id'],
                          'parent_node_id': node['parent_ids'][0] if node['parent_ids'] else None,
                          'role': message['role'] if message else None,
                          'source_create_time': message['created_at'] if message else None,
                          'source_message_pointer': message['source_pointer'] if message else None,
                          'native_message': deepcopy(message['native_message']) if message else None,
                          'native_node': deepcopy(node['native_node']), 'source_pointer': node['source_pointer']})
        has_parents = any(n['parent_node_id'] is not None for n in nodes)
        known_ids = {n['node_id'] for n in nodes}
        external_parents = sorted({n['parent_node_id'] for n in nodes if n['parent_node_id'] is not None and n['parent_node_id'] not in known_ids})
        source = {'schema': 'loom.research_context_source/1', 'family_id': normalized['family_id'],
                  'source_sha256': normalized['raw_sha256'], 'provider': normalized['provider'],
                  'split': 'development' if normalized['split'] == 'tuning' else 'validation',
                  'original_split_label': normalized['split'], 'validation_status': panel['validation_status'],
                  'prior_use': normalized['prior_exposure'], 'nodes': nodes,
                  'parent_semantics': 'native_export_parent' if has_parents else 'not_available',
                  'external_parent_ids': external_parents,
                  'source_order_node_ids': [n['node_id'] for n in normalized['messages']],
                  'source_references': {'panel_sha256': hashlib.sha256((root/'panel.json').read_bytes()).hexdigest(),
                                        'normalized_source_sha256': selected['normalized_sha256'],
                                        'source_pointer': normalized['source_pointer'],
                                        'source_member_sha256': normalized['source_member_sha256']},
                  'attachment_binary_coverage': None,
                  'native_conversation': deepcopy(normalized['native_conversation'])}
        candidates = [m for m in normalized['messages'] if m['role'] == 'user']
        require(bool(candidates), 'retrospective_task_needs_user_boundary')
        # This is explicit array-index selection, not a claim of timestamp order.
        boundary = candidates[-1]['node_id']
        task = {'id': 'retrospective:' + digest({'family': source['family_id'], 'policy': task_policy}),
                'family_id': source['family_id'], 'query': deepcopy(task_policy['query']),
                'checkpoint_boundary_node_id': boundary,
                'boundary_authority': 'research_protocol_last_user_in_export_array',
                'task_origin': 'researcher_authored_source_inspection',
                'task_policy_sha256': digest(task_policy), 'gold': None,
                'next_turn_prediction': False,
                'full_context_includes_all_exported_future_or_sibling_nodes': True}
        sources.append(source); tasks.append(task)
    return sources, tasks


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', type=Path, required=True)
    parser.add_argument('--tasks', type=Path, required=True)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--profiles', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    read = lambda path: strict_json(path.read_bytes())
    print(json.dumps(prepare_release(read(args.sources), read(args.tasks), read(args.plan), read(args.profiles), args.output), indent=2))
