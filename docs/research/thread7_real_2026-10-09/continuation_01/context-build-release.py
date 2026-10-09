"""Build a private source-bound context release from the already frozen panel.

Never opens a credential or calls a model. Optional annotations are private
reviewed exact source spans; lexical detections alone are not annotations.
"""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT))
from loom.tools.structure import experiment_context_preparation_v1 as c


def build(corpus, output, plan_path, profiles_path, task_policy_path,
          annotations_path=None, preceding_manifest=None, annotate_panel=False):
    read = lambda p: c.strict_json(Path(p).read_bytes())
    plan, profiles, task_policy = read(plan_path), read(profiles_path), read(task_policy_path)
    sources, tasks = c.expanded_panel_sources(corpus, task_policy)
    if annotations_path is not None:
        annotations = read(annotations_path)
        require = c.require
        require(annotations['status'] == 'provisional_researcher_annotation', 'annotation_status_missing')
        selected = annotations['families']
        require(set(selected) <= {s['family_id'] for s in sources}, 'annotation_family_not_in_panel')
        if not annotate_panel:
            sources = [s for s in sources if s['family_id'] in selected]
            tasks = [t for t in tasks if t['family_id'] in selected]
        for source in sources:
            if source['family_id'] not in selected:
                continue
            expected = selected[source['family_id']]
            require(source['source_sha256'] == expected['source_sha256'], 'annotation_family_source_hash_mismatch')
            source['explicit_preference_evidence'] = deepcopy(expected['evidence'])
            source['annotation_sha256'] = c.digest(annotations)
        if not annotate_panel:
            plan['id'] = plan['id'] + '-source-preference-supplement-v1'
        plan['preceding_preparation_manifest_sha256'] = preceding_manifest
        plan['annotation_sha256'] = c.digest(annotations)
        if not annotate_panel:
            for axis in plan['variants']['axes']:
                if axis['name'] == 'preference_mode':
                    axis['values'] = ['source_explicit']
    return c.prepare_release(sources, tasks, plan, profiles, output)


if __name__ == '__main__':
    root = Path(__file__).parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--plan', type=Path, default=root/'context-plan.json')
    parser.add_argument('--profiles', type=Path, default=root/'context-profiles.json')
    parser.add_argument('--task-policy', type=Path, default=root/'context-task-policy.json')
    parser.add_argument('--annotations', type=Path)
    parser.add_argument('--preceding-manifest-sha256')
    parser.add_argument('--annotate-panel', action='store_true', help='Preserve all families and axes while adding available exact annotations.')
    args = parser.parse_args()
    print(json.dumps(build(args.corpus, args.output, args.plan, args.profiles,
                           args.task_policy, args.annotations, args.preceding_manifest_sha256,
                           args.annotate_panel), indent=2))
