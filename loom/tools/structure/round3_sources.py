"""Measure optional source-envelope coverage, separately from labelled fixtures.

Source candidates retain exact source bytes/spans and are not gold observations
of relation truth. This diagnostic creates zero canonical claims or mutations.
"""
from collections import Counter
import argparse
import hashlib
import json
from pathlib import Path
try:
    from . import extract, repo_experiment, round3_eval
except ImportError:
    import extract, repo_experiment, round3_eval

def run(root):
    before = round3_eval.implementation_hashes()
    sources = []
    for path in repo_experiment.SOURCE_PATHS:
        raw = (root / path).read_bytes(); text = raw.decode('utf-8')
        assert text.encode('utf-8') == raw
        source_hash = hashlib.sha256(raw).hexdigest()
        record = {'id': path, 'source_id': 'sha256:' + source_hash,
                  'text': text, 'locator': {'member': path, 'source': 'sha256:' + source_hash},
                  'known_at': None, 'known_at_status': 'not_supplied_by_source_snapshot'}
        results = {policy: extract.extract_record(record, policy=policy)
                   for policy in ('bounded', 'explicit_relations')}
        summaries = {}
        for policy, result in results.items():
            families = Counter(c['operation_family'] for c in result['candidates'])
            refusals = Counter(c['reason'] for c in result['unknown'])
            for candidate in result['candidates']:
                for slot in list(candidate['slots'].values()) + [candidate]:
                    s = slot['span']; assert text[s['char_start']:s['char_end']] == s['quote']
                    assert raw[s['byte_start']:s['byte_end']].decode() == s['quote']
            summaries[policy] = {'version': result['version'], 'coverage': result['coverage'],
                                'families': dict(families), 'refusals': dict(refusals),
                                'candidates': result['candidates'],
                                'unit_definition': 'physical_nonempty_lines' if policy == 'bounded' else 'proposed_sentences_from_reversible_blocks'}
        sources.append({'path': path, 'source_sha256': source_hash, 'bytes': len(raw),
                        'characters': len(text), 'policies': summaries})
        assert hashlib.sha256((root / path).read_bytes()).hexdigest() == source_hash
    assert before == round3_eval.implementation_hashes()
    return {'schema': 'loom.research.structure_round3_sources/1', 'sources': sources,
            'implementation_sha256': before, 'semantic_accuracy': None,
            'source_bytes_rewritten': 0, 'canonical_claims_created': 0, 'graph_mutations': 0,
            'limits': ['architecture_text_not_gold_recall', 'policies_have_different_segmentation_denominators',
                      'quotes_code_or_unclear_scope_abstain_in_selected_channel_only',
                      'optional_channel_not_wired_to_native_pipeline_or_default_research_pipeline']}

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    result = run(round3_eval.ROOT)
    round3_eval.save_new(args.output, result)
    print(json.dumps([{'path': s['path'], 'counts': {p: v['coverage']['recognized_envelopes']
                       for p, v in s['policies'].items()}} for s in result['sources']]))

if __name__ == '__main__': main()
