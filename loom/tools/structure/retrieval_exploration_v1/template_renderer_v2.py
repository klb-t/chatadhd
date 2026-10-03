"""Data-backed renderer; preserves the executed v1 instrument unchanged."""
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import explore


def render(row, config):
    q = row['query']
    nodes = {n['id']: n for n in row['prefix_payload']['node_inventory']}
    source, target = nodes[q['source']], nodes[q['target']]
    values = {'query_text': row['query_text'], 'actor': q['attributed_to'], 'relation': q['relation'],
              'source_text': source['text'], 'target_text': target['text'],
              'source_aliases': source['aliases'], 'target_aliases': target['aliases']}
    try:
        special = config['language_relation_templates'][row['language']][q['relation']]
    except KeyError as e:
        raise ValueError('unregistered_language_or_relation') from e
    result = {}
    for name, recipe in config['variants'].items():
        if 'template' in recipe:
            result[name] = recipe['template'].format_map(values)
        elif 'join' in recipe:
            parts = []
            for field in recipe['join']:
                value = values[field]
                parts.extend(value if isinstance(value, list) else [value])
            if not all(isinstance(v, str) for v in parts):
                raise ValueError('representation_values_not_strings')
            result[name] = recipe['separator'].join(parts)
        else:
            raise ValueError('unregistered_representation_operation')
    result.update({name: template.format_map(values) for name, template in special.items()})
    return result


def freeze():
    names = ['template_renderer_v2.py', 'representation_templates_v2.json', 'test_template_renderer_v2.py']
    explore.write_new(explore.HERE / 'template_renderer_v2_freeze.json', {'created_at': datetime.now(timezone.utc).isoformat(),
        'files_sha256': {name: explore.sha(explore.HERE / name) for name in names},
        'first_executed_v1_renderer_unchanged': True, 'primary_quality_scores_unchanged': True})


def parity():
    before = json.loads((explore.HERE / 'template_renderer_v2_freeze.json').read_text())
    for name, h in before['files_sha256'].items():
        if explore.sha(explore.HERE / name) != h:
            raise ValueError('template_renderer_freeze_drift')
    prepared = json.loads((explore.BASE / 'prepared_inputs.json').read_text())
    config = json.loads((explore.HERE / 'representation_templates_v2.json').read_text())
    changed = []
    pairs = 0
    for row in prepared['queries']:
        original, new = explore.render_variants(row), render(row, config)
        if set(original) != set(new):
            raise ValueError('representation_variant_inventory_drift')
        for name in original:
            pairs += 1
            if original[name].encode() != new[name].encode():
                changed.append({'query_id': row['query_id'], 'variant': name})
    result = {'schema': 'loom.research.retrieval_template_transport_parity/1', 'created_at': datetime.now(timezone.utc).isoformat(),
        'queries': len(prepared['queries']), 'representation_pairs': pairs, 'changed': changed,
        'identical_utf8_representations': pairs - len(changed), 'gold_read': False, 'quality_remeasured': False,
        'paid_calls': 0, 'validation_accessed': False, 'config_sha256': explore.sha(explore.HERE / 'representation_templates_v2.json')}
    explore.write_new(explore.HERE / 'template_renderer_v2_parity.json', result)
    if changed:
        raise ValueError('representation_transport_not_equivalent')


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage', choices=('freeze', 'parity'))
    args = p.parse_args()
    globals()[args.stage]()
