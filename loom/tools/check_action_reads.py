#!/usr/bin/env python3
"""Check that the settings an action says it reads (actions.pack `reads`) are the settings its executor reads.

M1-D1. For every action of loom/data/runtime/actions.pack that declares `reads` (settings keys of
settings_fields.pack) the files in `implemented_in` are searched for the keys:

  * a C++/C/Python/TypeScript file: every literal "<key>" of a settings key;
  * a data file (.pack/.json), optionally narrowed by a JSON pointer ("loom/data/runtime/archive.pack#/defaults/refinement"):
    every string value equal to a settings key (the documentation keys of an action entry - human, reads,
    implemented_in, uses_role, subflows - are left out, so a declaration never proves itself).

A key found but not declared, or declared but not found, is a difference; both directions fail (exit 1)
unless loom/tools/check_action_reads_allow.json lists the exception with a reason (an exception nothing
needs any more fails too, so the list never goes stale).

The same run checks the other half of the picture, settings_fields.pack human.flows ("the flows that read
this field"):
  * a field that names a flow which declares `reads` must be read by it (its own reads, the reads of its
    steps and of its `subflows`);
  * a flow that declares it reads a field must be named by that field (own reads only: a step such as
    use_model is not repeated on every field);
  * a field whose status is planned names the flows that will read it; it is skipped.

It also checks the shape of the M1-D1 annotations: `uses_role` names a role of ports.pack that has a policy
in guardian.pack models.policies, whose explicit setting the flow reads; `human` has title {pl, en}, steps
[{label {pl, en}}] and steps_are "description" (the steps describe the flow; they are not what runs - P37);
a planned action declares no executor and no reads.

Usage: check_action_reads.py [--verbose] [--discover ACTION]
  --discover ACTION  print, for one action, every settings key found in its implemented_in with the
                     line numbers (a helper for writing `reads`); exit 0.
"""
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
ALLOW = LOOM / 'tools/check_action_reads_allow.json'
DOC_KEYS = {'human', 'reads', 'implemented_in', 'uses_role', 'subflows'}
CODE_SUFFIXES = {'.cpp', '.cc', '.h', '.hpp', '.inc', '.py', '.ts', '.tsx', '.kt', '.java'}
DATA_SUFFIXES = {'.pack', '.json'}


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


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


def strings(node, out, top=True):
    if isinstance(node, dict):
        for k, v in node.items():
            if top and k in DOC_KEYS:
                continue
            strings(v, out, False)
    elif isinstance(node, list):
        for v in node:
            strings(v, out, False)
    elif isinstance(node, str):
        out.append(node)


def found_in(entry, keys, pattern):
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
            node = pointer(load(path), ptr)
        except (KeyError, IndexError, ValueError):
            raise ValueError(f'implemented_in pointer does not resolve: {entry}') from None
        values = []
        strings(node, values)
        for v in values:
            if v in keys:
                hits.setdefault(v, []).append(entry)
    else:
        raise ValueError(f'implemented_in names a file of unknown kind: {entry}')
    return hits


def text_pair(v):
    return isinstance(v, dict) and isinstance(v.get('pl'), str) and v['pl'].strip() and isinstance(v.get('en'), str) and v['en'].strip()


def main(argv):
    verbose = '--verbose' in argv
    discover = argv[argv.index('--discover') + 1] if '--discover' in argv else None
    actions = load(ACTIONS)['defaults']['actions']
    fields_doc = load(FIELDS)['defaults']
    fields = {f['key']: f for f in fields_doc['fields']}
    keys = set(fields)
    pattern = re.compile('"(' + '|'.join(sorted(map(re.escape, keys), key=len, reverse=True)) + ')"')
    roles = load(PORTS)['defaults']['roles']
    policies = load(GUARDIAN)['defaults']['models']['policies']
    allow = load(ALLOW)
    errors = []

    if discover:
        a = actions.get(discover)
        if a is None:
            print(f'no action {discover}')
            return 1
        for entry in a.get('implemented_in', []):
            for k, where in sorted(found_in(entry, keys, pattern).items()):
                mark = '' if k in a.get('reads', []) else '   <- not in reads'
                print(f'{k}: {", ".join(where)}{mark}')
        return 0

    # Allowlist entries, each with a reason; usage is tracked so a stale exception fails.
    def entries(kind, required):
        out = []
        for i, e in enumerate(allow.get(kind, [])):
            if not all(isinstance(e.get(r), str) and e[r] for r in required + ['reason']):
                errors.append(f'allowlist {kind}[{i}] needs {", ".join(required)} and a reason')
                continue
            out.append([e, False])
        return out
    not_a_read = entries('not_a_read', ['file', 'key'])
    read_elsewhere = entries('read_elsewhere', ['action', 'key'])
    flow_exceptions = entries('flows', ['field', 'action'])

    def allowed(table, match):
        hit = False
        for row in table:
            if match(row[0]):
                row[1] = True
                hit = True
        return hit

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
                for k, where in found_in(entry, keys, pattern).items():
                    found.setdefault(k, []).extend(where)
            except ValueError as e:
                errors.append(f'{aid}: {e}')
        for k in sorted(set(found) - own[aid]):
            files = {w.split(':')[0].split('#')[0] for w in found[k]}
            unexplained = [f for f in sorted(files)
                           if not allowed(not_a_read, lambda e, f=f, k=k: e['file'] == f and e['key'] == k and aid in e.get('actions', [aid]))]
            if unexplained:
                where = ', '.join(w for w in found[k] if w.split(':')[0].split('#')[0] in unexplained)
                errors.append(f'{aid}: reads {k!r} in {where} but does not declare it (add it to reads, or explain in the allowlist why it is not a read)')
        for k in sorted(own[aid] - set(found)):
            if not allowed(read_elsewhere, lambda e, k=k: e['action'] == aid and e['key'] == k):
                errors.append(f'{aid}: declares {k!r} but no file of implemented_in names it (add the reading file, or explain in the allowlist)')
        if verbose:
            print(f'{aid}: {len(own[aid])} reads, {len(impl)} files')

    # Effective reads: own reads, the reads of the steps and of the subflows (recursively).
    memo = {}

    def effective(aid, seen=()):
        if aid in memo:
            return memo[aid]
        if aid in seen:
            return set()
        a = actions.get(aid, {})
        out = set(own.get(aid, set()))
        for s in a.get('steps', []) or []:
            out |= effective(s.get('action', ''), seen + (aid,))
        for s in a.get('subflows', []) or []:
            if s not in actions:
                errors.append(f'{aid}: subflow {s!r} is not an action')
                continue
            out |= effective(s, seen + (aid,))
        memo[aid] = out
        return out

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
                    if explicit and explicit[0] not in effective(aid):
                        errors.append(f'{aid}: uses_role {role!r}, whose policy starts with the setting {explicit[0]!r}, which the flow does not read')
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

    for e in errors:
        print(e, file=sys.stderr)
    if verbose or not errors:
        print(f'check_action_reads: {len(annotated)} actions with reads, {len(errors)} problems')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
