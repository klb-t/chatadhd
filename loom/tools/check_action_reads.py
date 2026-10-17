#!/usr/bin/env python3
"""Check that the settings an action says it reads (actions.pack `reads`) are the settings its executor reads.

M1-D1. For every action of loom/data/runtime/actions.pack that declares `reads` (settings keys of
settings_fields.pack) the files in `implemented_in` are searched for the keys:

  * a C++/C/Python/TypeScript file: every literal "<key>" of a settings key;
  * a data file (.pack/.json), optionally narrowed by a JSON pointer ("loom/data/runtime/archive.pack#/defaults/refinement"):
    only the values that name a setting - the values under SETTING_KEYS (`setting`, `model_setting`, `model_keys[]`,
    the values of `param_settings` ...) and the keys of a dict under SETTING_MAP_KEYS (`when`: {setting: values}). A
    pointer that ends on such a key counts its value. Any other string (a param `name`, an `$arg`, a description) is
    not evidence, and the documentation keys of an action entry (human, reads, implemented_in, uses_role, subflows) are
    left out, so a declaration never proves itself.

A key found but not declared, or declared but not found, is a difference; both directions fail (exit 1)
unless loom/tools/check_action_reads_allow.json lists the exception with a reason (an exception nothing
needs any more fails too, so the list never goes stale). A `not_a_read` literal never counts as found, for
any action (narrowed by its optional `actions`), and is pinned to its `count` of literals in the file: a new
literal of that key in that file fails until someone looks at it again.

The same run checks the other half of the picture, settings_fields.pack human.flows ("the flows that read
this field"):
  * a field that names a flow which declares `reads` must be read by it (its effective reads, below);
  * a flow that declares it reads a field must be named by that field (own reads only: a step such as
    use_model is not repeated on every field);
  * a field whose status is planned names the flows that will read it; it is skipped.

Effective reads of a flow = its own reads, the reads of its `subflows` and of its steps. A step's reads that only
the action runner reads when it runs the step (action_run.pack <step>.model_setting: default_model for use_model)
count only for a flow the runner executes (no consumer, or consumer runner.cpp); a flow that calls the library
itself (semantic.analyse calls llm::use_model with its own model) does not read them.

It also checks the shape of the M1-D1 annotations: `uses_role` names a role of ports.pack that has a policy
in guardian.pack models.policies, whose explicit setting the flow itself reads (own reads and subflows, not
steps), or its caller reads (a flow that lists it in `subflows` and hands it the model); `human` has title
{pl, en}, steps [{label {pl, en}}] and steps_are "description" (the steps describe the flow; they are not what
runs - P37); a planned action declares no executor and no reads.

Usage: check_action_reads.py [--verbose] [--discover ACTION] [--self-test]
  --discover ACTION  print, for one action, every settings key found in its implemented_in with the
                     line numbers (a helper for writing `reads`); exit 0.
  --self-test        run the check on mutated copies of the data (a removed read, a false role, a field naming
                     a flow that does not read it, a declaration whose only literal is not a read ...) and
                     expect each to fail; exit 1 when one passes.
"""
import copy
import json
import re
import sys
from pathlib import Path

LOOM = Path(__file__).resolve().parents[1]
REPO = LOOM.parent
ACTIONS = LOOM / 'data/runtime/actions.pack'
FIELDS = LOOM / 'data/runtime/settings_fields.pack'
PORTS = LOOM / 'data/runtime/ports.pack'
GUARDIAN = LOOM / 'data/runtime/guardian.pack'
ACTION_RUN = LOOM / 'data/runtime/action_run.pack'
ALLOW = LOOM / 'tools/check_action_reads_allow.json'
DOC_KEYS = {'human', 'reads', 'implemented_in', 'uses_role', 'subflows'}
CODE_SUFFIXES = {'.cpp', '.cc', '.h', '.hpp', '.inc', '.py', '.ts', '.tsx', '.kt', '.java'}
DATA_SUFFIXES = {'.pack', '.json'}
# Keys of the data whose value names a setting (a string, a list of strings, or a dict whose values are settings).
SETTING_KEYS = {'setting', 'model_setting', 'model_fallback_setting', 'model_keys', 'param_settings', 'x-setting',
                'levels_setting', 'capability_setting', 'person_setting', 'batch_secret_key'}
# Keys of the data holding a dict whose KEYS are settings ({setting: accepted values}).
SETTING_MAP_KEYS = {'when'}
RUNNER_CONSUMERS = {'', 'loom/src/actions/runner.cpp'}


class Data:
    """The documents of one run; `overrides` {path: doc} replaces a file (the self-test mutates copies, never files)."""

    def __init__(self, overrides=None):
        self.overrides = overrides or {}
        self.cache = {}

    def load(self, path):
        path = Path(path)
        if path in self.overrides:
            return self.overrides[path]
        if path not in self.cache:
            self.cache[path] = json.loads(path.read_text(encoding='utf-8'))
        return self.cache[path]


def pointer(doc, ptr):
    if not ptr:
        return doc
    if not ptr.startswith('/'):
        raise KeyError(ptr)
    for raw in ptr[1:].split('/'):
        part = raw.replace('~1', '/').replace('~0', '~')
        if isinstance(doc, list):
            doc = doc[int(part)]
        elif isinstance(doc, dict):
            doc = doc[part]
        else:
            raise KeyError(ptr)
    return doc


def named_settings(node, out, parent=None, top=True):
    """The strings of `node` that name a setting: values under SETTING_KEYS, keys of a dict under SETTING_MAP_KEYS."""
    if parent in SETTING_KEYS:
        if isinstance(node, str):
            out.append(node)
        elif isinstance(node, list):
            out.extend(v for v in node if isinstance(v, str))
        elif isinstance(node, dict):
            out.extend(v for v in node.values() if isinstance(v, str))
        return
    if parent in SETTING_MAP_KEYS and isinstance(node, dict):
        out.extend(node.keys())
    if isinstance(node, dict):
        for k, v in node.items():
            if top and k in DOC_KEYS:
                continue
            named_settings(v, out, k, False)
    elif isinstance(node, list):
        for v in node:
            named_settings(v, out, None, False)


def literal_count(path_text, key):
    path = REPO / path_text
    if not path.is_file():
        return 0
    return path.read_text(encoding='utf-8').count(f'"{key}"')


def found_in(data, entry, keys, pattern):
    """{key: [where]} for one implemented_in entry; raises ValueError for a bad entry."""
    path_text, _, ptr = entry.partition('#')
    path = REPO / path_text
    if not path.is_file():
        raise ValueError(f'implemented_in names a missing file: {entry}')
    hits = {}
    if path.suffix in CODE_SUFFIXES:
        if ptr:
            raise ValueError(f'a pointer only narrows a data file: {entry}')
        for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
            for m in pattern.finditer(line):
                hits.setdefault(m.group(1), []).append(f'{path_text}:{n}')
    elif path.suffix in DATA_SUFFIXES:
        try:
            node = pointer(data.load(path), ptr)
        except (KeyError, IndexError, ValueError):
            raise ValueError(f'implemented_in pointer does not resolve: {entry}') from None
        values = []
        last = ptr.rsplit('/', 1)[-1] if ptr else None
        named_settings(node, values, last if last in SETTING_KEYS | SETTING_MAP_KEYS else None)
        for v in values:
            if v in keys:
                hits.setdefault(v, []).append(entry)
    else:
        raise ValueError(f'implemented_in names a file of unknown kind: {entry}')
    return hits


def text_pair(v):
    return isinstance(v, dict) and isinstance(v.get('pl'), str) and v['pl'].strip() and isinstance(v.get('en'), str) and v['en'].strip()


def check(data, verbose=False, discover=None, out=print):
    """The list of problems (empty = clean). With `discover`, prints the keys of one action and returns []."""
    actions = data.load(ACTIONS)['defaults']['actions']
    fields_doc = data.load(FIELDS)['defaults']
    fields = {f['key']: f for f in fields_doc['fields']}
    keys = set(fields)
    pattern = re.compile('"(' + '|'.join(sorted(map(re.escape, keys), key=len, reverse=True)) + ')"')
    roles = data.load(PORTS)['defaults']['roles']
    policies = data.load(GUARDIAN)['defaults']['models']['policies']
    action_run = data.load(ACTION_RUN)['defaults']
    allow = data.load(ALLOW)
    errors = []

    # Allowlist entries, each with a reason; usage is tracked so a stale exception fails.
    def entries(kind, required):
        rows = []
        for i, e in enumerate(allow.get(kind, [])):
            if not all(isinstance(e.get(r), str) and e[r] for r in required + ['reason']):
                errors.append(f'allowlist {kind}[{i}] needs {", ".join(required)} and a reason')
                continue
            rows.append([e, False])
        return rows
    not_a_read = entries('not_a_read', ['file', 'key'])
    read_elsewhere = entries('read_elsewhere', ['action', 'key'])
    flow_exceptions = entries('flows', ['field', 'action'])
    # A not_a_read exception is pinned to the literals the file has now.
    for e, _ in not_a_read:
        want = e.get('count')
        have = literal_count(e['file'], e['key'])
        if not isinstance(want, int) or want < 1:
            errors.append(f'allowlist not_a_read {e["file"]} "{e["key"]}" needs count (the number of literals "{e["key"]}" in the file, now {have})')
        elif have != want:
            errors.append(f'allowlist not_a_read {e["file"]} "{e["key"]}": the file has {have} literal(s), the exception explains {want}; '
                          f'look at the new or removed one (a read is declared in reads, a non-read raises the count)')

    def allowed(table, match):
        hit = False
        for row in table:
            if match(row[0]):
                row[1] = True
                hit = True
        return hit

    def evidence(aid, entry):
        """found_in without the literals the allowlist says are not reads (for this action)."""
        hits = found_in(data, entry, keys, pattern)
        path_text = entry.partition('#')[0]
        for k in list(hits):
            if allowed(not_a_read, lambda e, k=k: e['file'] == path_text and e['key'] == k and aid in e.get('actions', [aid])):
                del hits[k]
        return hits

    if discover:
        a = actions.get(discover)
        if a is None:
            out(f'no action {discover}')
            return [f'no action {discover}']
        for entry in a.get('implemented_in', []):
            for k, where in sorted(found_in(data, entry, keys, pattern).items()):
                mark = '' if k in a.get('reads', []) else '   <- not in reads'
                out(f'{k}: {", ".join(where)}{mark}')
        return []

    annotated = {aid: a for aid, a in actions.items() if 'reads' in a}
    own = {}
    for aid, a in annotated.items():
        reads = a['reads']
        if not isinstance(reads, list) or not all(isinstance(k, str) for k in reads):
            errors.append(f'{aid}: reads must be a list of settings keys')
            continue
        if len(set(reads)) != len(reads):
            errors.append(f'{aid}: reads lists a key twice')
        own[aid] = set(reads)
        for k in reads:
            if k not in keys:
                errors.append(f'{aid}: reads names {k!r}, which is not a settings field')
        impl = a.get('implemented_in', [])
        if a.get('status') == 'planned':
            if impl or reads:
                errors.append(f'{aid}: a planned action has no executor yet: implemented_in and reads stay empty')
            continue
        if not impl:
            errors.append(f'{aid}: reads without implemented_in cannot be checked')
            continue
        found = {}
        for entry in impl:
            try:
                for k, where in evidence(aid, entry).items():
                    found.setdefault(k, []).extend(where)
            except ValueError as e:
                errors.append(f'{aid}: {e}')
        for k in sorted(set(found) - own[aid]):
            errors.append(f'{aid}: reads {k!r} in {", ".join(found[k])} but does not declare it '
                          f'(add it to reads, or explain in the allowlist why it is not a read)')
        for k in sorted(own[aid] - set(found)):
            if not allowed(read_elsewhere, lambda e, k=k: e['action'] == aid and e['key'] == k):
                errors.append(f'{aid}: declares {k!r} but no file of implemented_in names it (add the reading file, or explain in the allowlist)')
        if verbose:
            out(f'{aid}: {len(own[aid])} reads, {len(impl)} files')

    # What only the runner reads when it runs a step (action_run.pack <step>.model_setting).
    runner_only = {sid: {v['model_setting']} for sid, v in action_run.items() if isinstance(v, dict) and isinstance(v.get('model_setting'), str)}

    def runner_run(aid):
        return actions.get(aid, {}).get('consumer', '') in RUNNER_CONSUMERS

    def subflows_of(aid):
        result = []
        for s in actions.get(aid, {}).get('subflows', []) or []:
            if s not in actions:
                errors.append(f'{aid}: subflow {s!r} is not an action')
                continue
            result.append(s)
        return result

    memo = {}

    def effective(aid, seen=()):
        """Own reads, the reads of the subflows and of the steps (runner-only step reads only for a runner-run flow)."""
        if aid in memo:
            return memo[aid]
        if aid in seen:
            return set()
        a = actions.get(aid, {})
        result = set(own.get(aid, set()))
        for s in a.get('steps', []) or []:
            sid = s.get('action', '') if isinstance(s, dict) else str(s)
            got = effective(sid, seen + (aid,))
            result |= got if runner_run(aid) else got - runner_only.get(sid, set())
        for s in subflows_of(aid):
            result |= effective(s, seen + (aid,)) - runner_only.get(s, set())
        memo[aid] = result
        return result

    def direct(aid, seen=()):
        """Own reads and the subflows' (what the flow itself reads to choose its model; steps are not counted)."""
        if aid in seen:
            return set()
        result = set(own.get(aid, set()))
        for s in subflows_of(aid):
            result |= direct(s, seen + (aid,)) - runner_only.get(s, set())
        return result

    callers = {}
    for aid, a in actions.items():
        for s in a.get('subflows', []) or []:
            callers.setdefault(s, []).append(aid)

    # Roles and policies, and the human description.
    for aid, a in actions.items():
        role = a.get('uses_role')
        if role is not None:
            if role not in roles:
                errors.append(f'{aid}: uses_role {role!r} is not a role of ports.pack')
            pol = [p for p in policies.values() if p.get('role') == role]
            if not pol:
                errors.append(f'{aid}: uses_role {role!r} has no policy in guardian.pack models.policies')
            elif aid in annotated and a.get('status') != 'planned':
                for p in pol:
                    explicit = [s.get('setting') for s in p.get('chain', []) if s.get('type') == 'explicit' and s.get('setting')]
                    if explicit and explicit[0] not in direct(aid) and not any(explicit[0] in direct(c) for c in callers.get(aid, [])):
                        errors.append(f'{aid}: uses_role {role!r}, whose policy starts with the setting {explicit[0]!r}, which neither the flow '
                                      f'nor a flow that calls it (subflows) reads')
        h = a.get('human')
        if h is None:
            continue
        if not text_pair(h.get('title')):
            errors.append(f'{aid}: human.title needs pl and en')
        steps = h.get('steps')
        if not isinstance(steps, list) or not steps or not all(isinstance(s, dict) and text_pair(s.get('label')) for s in steps):
            errors.append(f'{aid}: human.steps needs at least one step, each with label pl and en')
        if h.get('steps_are') != 'description':
            errors.append(f'{aid}: human.steps_are must be "description" (the steps describe the flow, they are not what runs)')

    # settings_fields.pack human.flows against reads.
    for k, f in fields.items():
        if f.get('status', {}).get('state') == 'planned':
            continue
        for aid in f.get('human', {}).get('flows', []) or []:
            if aid in annotated and actions[aid].get('status') != 'planned' and k not in effective(aid):
                if not allowed(flow_exceptions, lambda e, aid=aid, k=k: e['field'] == k and e['action'] == aid):
                    errors.append(f'settings field {k!r} names the flow {aid!r}, which does not read it (fix human.flows or the reads of {aid})')
    for aid, reads in own.items():
        for k in sorted(reads):
            if k in fields and aid not in (fields[k].get('human', {}).get('flows', []) or []):
                if not allowed(flow_exceptions, lambda e, aid=aid, k=k: e['field'] == k and e['action'] == aid):
                    errors.append(f'{aid} reads {k!r} but the settings field {k!r} does not name it in human.flows')

    for kind, table in (('not_a_read', not_a_read), ('read_elsewhere', read_elsewhere), ('flows', flow_exceptions)):
        for e, used in table:
            if not used:
                errors.append(f'allowlist {kind} entry no longer needed: {json.dumps({x: y for x, y in e.items() if x != "reason"}, ensure_ascii=False)}')
    if verbose:
        out(f'check_action_reads: {len(annotated)} actions with reads')
    return errors


def self_test():
    """Each mutation of a copy of the data must make the check fail with the expected message."""
    base = Data()
    clean = check(base)
    failures = []
    if clean:
        failures.append('the unmutated data is not clean: ' + '; '.join(clean[:3]))

    def mutated(path, change):
        doc = copy.deepcopy(base.load(path))
        change(doc)
        return Data({Path(path): doc})

    def acts(doc):
        return doc['defaults']['actions']

    def field(doc, key):
        return next(f for f in doc['defaults']['fields'] if f['key'] == key)

    def drop_param_settings(doc):
        for p in acts(doc)['narrate']['params']:
            p.pop('setting', None)

    def drop_impl(aid, entry):
        def change(doc):
            acts(doc)[aid]['implemented_in'].remove(entry)
        return change

    def set_role(aid, role):
        def change(doc):
            acts(doc)[aid]['uses_role'] = role
        return change

    def add_flow(key, aid):
        def change(doc):
            field(doc, key)['human']['flows'].append(aid)
        return change

    def add_read(aid, key):
        def change(doc):
            acts(doc)[aid]['reads'].append(key)
        return change

    def drop_read(aid, key):
        def change(doc):
            acts(doc)[aid]['reads'].remove(key)
        return change

    def bump_count(doc):
        doc['not_a_read'][0]['count'] += 1

    cases = [
        # a param name, an $arg or a when.arg equal to a key is no evidence: without `setting` nothing reads speech_model
        ('narrate without param settings', mutated(ACTIONS, drop_param_settings), "narrate: declares 'speech_model'"),
        # producer names in guardian.cpp ("guardian_model") are not reads; the policy is the evidence
        ('guardian.model_read without its policy', mutated(ACTIONS, drop_impl('guardian.model_read', 'loom/data/runtime/guardian.pack#/defaults/models/policies/guardian')),
         "guardian.model_read: declares 'guardian_model'"),
        # a background reader that would inherit the conversation model through the use_model step
        ('semantic.analyse with role chat.default', mutated(ACTIONS, set_role('semantic.analyse', 'chat.default')), "semantic.analyse: uses_role 'chat.default'"),
        ('semantic.batch with role chat.default', mutated(ACTIONS, set_role('semantic.batch', 'chat.default')), "semantic.batch: uses_role 'chat.default'"),
        ('goal.typing with role chat.default', mutated(ACTIONS, set_role('goal.typing', 'chat.default')), "goal.typing: uses_role 'chat.default'"),
        ('guardian.model_read with role chat.default', mutated(ACTIONS, set_role('guardian.model_read', 'chat.default')), "guardian.model_read: uses_role 'chat.default'"),
        ('default_model names semantic.analyse', mutated(FIELDS, add_flow('default_model', 'semantic.analyse')),
         "settings field 'default_model' names the flow 'semantic.analyse'"),
        ('default_model names goal.typing', mutated(FIELDS, add_flow('default_model', 'goal.typing')),
         "settings field 'default_model' names the flow 'goal.typing'"),
        ('default_model names guardian.model_read', mutated(FIELDS, add_flow('default_model', 'guardian.model_read')),
         "settings field 'default_model' names the flow 'guardian.model_read'"),
        # a declaration whose only literal is an explained non-read
        ('semantic.analyse declares max_tokens', mutated(ACTIONS, add_read('semantic.analyse', 'max_tokens')), "semantic.analyse: declares 'max_tokens'"),
        # the plain cases
        ('goal.typing without semantic_model', mutated(ACTIONS, drop_read('goal.typing', 'semantic_model')), "goal.typing: reads 'semantic_model'"),
        ('semantic.analyse with role intent.routing', mutated(ACTIONS, set_role('semantic.analyse', 'intent.routing')), "semantic.analyse: uses_role 'intent.routing'"),
        ('a not_a_read count that no longer matches', mutated(ALLOW, bump_count), 'the exception explains'),
    ]
    for name, data, expect in cases:
        errs = check(data)
        if not any(expect in e for e in errs):
            failures.append(f'mutation "{name}" was not caught (expected a problem containing {expect!r}; got {errs[:3]})')
    for f in failures:
        print(f, file=sys.stderr)
    print(f'check_action_reads --self-test: {len(cases)} mutations, {len(failures)} not caught')
    return 1 if failures else 0


def main(argv):
    if '--self-test' in argv:
        return self_test()
    verbose = '--verbose' in argv
    discover = argv[argv.index('--discover') + 1] if '--discover' in argv else None
    data = Data()
    errors = check(data, verbose, discover)
    if discover:
        return 1 if errors else 0
    for e in errors:
        print(e, file=sys.stderr)
    annotated = sum(1 for a in data.load(ACTIONS)['defaults']['actions'].values() if 'reads' in a)
    print(f'check_action_reads: {annotated} actions with reads, {len(errors)} problems')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
