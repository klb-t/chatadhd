#!/usr/bin/env python3
"""Synthetic, offline CLI regressions for the product literal inventory guard.

Each invocation scans a temporary source tree.  These tests never inventory the
checkout or construct an allowlist for production source files.
"""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


LOOM = Path(__file__).resolve().parents[2]
REPO = LOOM.parent
GUARD = LOOM / 'src/util/product_literal_guard.py'
POLICY = LOOM / 'data/validation/product_literals.pack'
SCHEMA = REPO / 'docs/contracts/product_literals.schema.json'
CATEGORIES = ('contract', 'external_standard', 'mechanism', 'serialization',
              'bootstrap', 'developer_diagnostic')


def sha256(raw):
    return hashlib.sha256(raw).hexdigest()


def duplicate_first_key(raw):
    document = json.loads(raw)
    key = next(iter(document))
    first = json.dumps({key: document[key]}, ensure_ascii=False)[:-1]
    return (first + ',' + json.dumps(document, ensure_ascii=False)[1:] + '\n').encode('utf-8')


class ProductLiteralGuardTests(unittest.TestCase):
    def setUp(self):
        self.maxDiff = None
        temporary = tempfile.TemporaryDirectory(prefix='product-literals-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.pack = self.root / 'loom/data/validation/product_literals.pack'
        self.schema = self.root / 'docs/contracts/product_literals.schema.json'
        self.report_path = self.root / 'report.json'
        self.manifest_path = self.root / 'manifest.json'
        self.pack.parent.mkdir(parents=True)
        self.schema.parent.mkdir(parents=True)
        shutil.copyfile(POLICY, self.pack)
        shutil.copyfile(SCHEMA, self.schema)
        self.policy = json.loads(self.pack.read_bytes())
        self.policy['scopes'] = ['loom/src']
        self.policy['optional_scopes'] = []
        self.policy['allowlist'] = []
        self.policy['generated_outputs'] = []
        self.policy['exclusions'] = [
            {'path_segment': segment, 'reason': 'Explicit synthetic fixture exclusion.'}
            for segment in ('tests', 'fixtures')]
        (self.root / 'loom/src').mkdir(parents=True)
        self.write_policy()

    def write_policy(self):
        self.pack.write_text(json.dumps(self.policy, ensure_ascii=False, indent=2) + '\n',
                             encoding='utf-8')

    def source(self, relative, raw):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(raw.encode('utf-8') if isinstance(raw, str) else raw)
        return path

    def anchor(self, relative, token, category='mechanism'):
        raw = (self.root / relative).read_bytes()
        token = token.encode('utf-8') if isinstance(token, str) else token
        start = raw.index(token)
        self.assertEqual(raw.count(token), 1, 'fixture anchor must be unambiguous')
        return {'path': relative, 'source_sha256': sha256(raw),
                'byte_start': start, 'byte_end': start + len(token),
                'literal_sha256': sha256(token), 'category': category,
                'reason': 'Synthetic fixture disposition for exact byte-anchor verification.'}

    def permit(self, relative, tokens, category='mechanism'):
        self.policy['allowlist'].extend(self.anchor(relative, token, category) for token in tokens)
        self.write_policy()

    def run_guard(self, expected):
        self.report_path.unlink(missing_ok=True)
        self.manifest_path.unlink(missing_ok=True)
        result = subprocess.run(
            [sys.executable, str(GUARD), '--root', str(self.root),
             '--pack', str(self.pack), '--schema', str(self.schema), '--check',
             '--report', str(self.report_path), '--manifest', str(self.manifest_path)],
            cwd=self.root, capture_output=True, timeout=60, check=False)
        details = (result.stdout.decode('utf-8', errors='replace') +
                   result.stderr.decode('utf-8', errors='replace'))
        if result.returncode != expected and self.report_path.is_file():
            details += self.report_path.read_text(encoding='utf-8', errors='replace')
        self.assertEqual(result.returncode, expected, details)
        self.assertTrue(self.report_path.is_file(), 'every check emits its requested report')
        self.assertTrue(self.manifest_path.is_file(), 'every check emits its requested manifest')
        report = json.loads(self.report_path.read_bytes())
        manifest = json.loads(self.manifest_path.read_bytes())
        self.assertEqual(report['schema'], 'loom.product_literals_report/1')
        self.assertEqual(manifest['schema'], 'loom.product_literals_manifest/1')
        self.assertIs(report['valid'], expected == 0)
        self.assertTrue({'files', 'candidates', 'allowed', 'unclassified',
                         'blocked_files', 'stale_allowlist'} <= report['counts'].keys())
        self.assertIsInstance(report['findings'], list)
        self.assertIsInstance(report['issues'], list)
        self.assertTrue(all(issue['status'] == 'BLOCKED' for issue in report['issues']))
        for finding in report['findings']:
            self.assertTrue({'path', 'source_sha256', 'kind', 'byte_start', 'byte_end',
                             'literal_sha256', 'raw', 'category', 'status', 'owner',
                             'line', 'column'} <= finding.keys())
            self.assertIn(finding['status'], ('ALLOWED', 'UNCLASSIFIED'))
            self.assertIs(type(finding['owner']), int)
            self.assertIs(type(finding['line']), int)
            self.assertIs(type(finding['column']), int)
            self.assertGreaterEqual(finding['line'], 1)
            self.assertGreaterEqual(finding['column'], 1)
            raw = (self.root / finding['path']).read_bytes()
            self.assertEqual(finding['source_sha256'], sha256(raw))
            self.assertIs(type(finding['byte_start']), int)
            self.assertIs(type(finding['byte_end']), int)
            self.assertGreaterEqual(finding['byte_start'], 0)
            self.assertLess(finding['byte_start'], finding['byte_end'])
            self.assertLessEqual(finding['byte_end'], len(raw))
            token = raw[finding['byte_start']:finding['byte_end']]
            self.assertEqual(finding['literal_sha256'], sha256(token))
            self.assertEqual(finding['raw'], token.decode('utf-8'))
        self.assertEqual(report['counts']['candidates'], len(report['findings']))
        self.assertEqual(report['counts']['allowed'],
                         sum(f['status'] == 'ALLOWED' for f in report['findings']))
        self.assertEqual(report['counts']['unclassified'],
                         sum(f['status'] == 'UNCLASSIFIED' for f in report['findings']))
        return report, manifest

    def assert_unclassified(self, report, tokens):
        self.assertEqual(sorted(f['raw'] for f in report['findings']), sorted(tokens))
        self.assertTrue(all(f['status'] == 'UNCLASSIFIED' for f in report['findings']))
        self.assertEqual(report['counts']['allowed'], 0)
        self.assertEqual(report['counts']['unclassified'], len(tokens))

    def test_empty_product_source_is_valid(self):
        self.source('loom/src/model/empty.cpp', '// no token candidates\n')
        report, manifest = self.run_guard(0)
        self.assertEqual(report['counts']['files'], 1)
        self.assertEqual(report['counts']['candidates'], 0)
        self.assertFalse(report['findings'])
        self.assertFalse(report['issues'])
        self.assertEqual([f['path'] for f in manifest['source_files']],
                         ['loom/src/model/empty.cpp'])

    def test_cpp_strings_characters_numbers_and_raw_strings_are_candidates(self):
        relative = 'loom/src/model/values.cpp'
        tokens = ['"literal"', "'x'", 'R"note(raw " quoted\n42)note"',
                  '17', '2.5e-3', '0x2aU']
        self.source(relative, '// "comment" 900\n/* \'q\' 801 */\n'
                    'int schema256;\n'
                    'const char* title = "literal";\nchar marker = \'x\';\n'
                    'auto r = R"note(raw " quoted\n42)note";\n'
                    'int n = 17;\ndouble f = 2.5e-3;\nunsigned mask = 0x2aU;\n')
        report, _ = self.run_guard(1)
        self.assert_unclassified(report, tokens)
        self.assertFalse(report['issues'])
        self.permit(relative, tokens)
        report, _ = self.run_guard(0)
        self.assertEqual(report['counts']['allowed'], len(tokens))
        self.assertTrue(all(f['status'] == 'ALLOWED' for f in report['findings']))

    def test_python_utf8_byte_anchors_triple_strings_and_numbers(self):
        relative = 'loom/src/util/values.py'
        tokens = ['"żółć"', '314', '1.25e-3', '"""line one\nline two"""']
        self.source(relative, '# żółć "comment" 90\npayload = "żółć"\ncount = 314\n'
                    'ratio = 1.25e-3\nmulti = """line one\nline two"""\n')
        report, _ = self.run_guard(1)
        self.assert_unclassified(report, tokens)
        first = next(f for f in report['findings'] if f['raw'] == '"żółć"')
        self.assertEqual(first['byte_start'], self.anchor(relative, '"żółć"')['byte_start'])
        self.assertEqual(first['line'], 2)
        self.assertEqual(first['column'], 11)
        self.permit(relative, tokens, 'serialization')
        self.run_guard(0)

    def test_javascript_java_and_kotlin_plain_literals(self):
        cases = [('values.js', 'const name = "js"; const count = 23; // "ignored" 91\n',
                  ['"js"', '23']),
                 ('Values.java', 'class Values { String s = "java"; char c = \'j\'; int n = 29; }\n',
                  ['"java"', "'j'", '29']),
                 ('Values.kt', 'val s = "kotlin"\nval c = \'k\'\nval n = 31\n',
                  ['"kotlin"', "'k'", '31'])]
        for name, text, tokens in cases:
            with self.subTest(name=name):
                relative = 'loom/src/model/' + name
                path = self.source(relative, text)
                try:
                    report, _ = self.run_guard(1)
                    self.assert_unclassified(report, tokens)
                    self.permit(relative, tokens)
                    self.run_guard(0)
                finally:
                    path.unlink()
                    self.policy['allowlist'] = []
                    self.write_policy()

    def test_every_declared_category_accepts_an_exact_anchor(self):
        relative = 'loom/src/model/categories.cpp'
        tokens = ['"' + category + '"' for category in CATEGORIES]
        self.source(relative, ';\n'.join(tokens) + ';\n')
        for category, token in zip(CATEGORIES, tokens):
            self.policy['allowlist'].append(self.anchor(relative, token, category))
        self.write_policy()
        report, _ = self.run_guard(0)
        self.assertEqual({f['category'] for f in report['findings']}, set(CATEGORIES))
        self.assertEqual(report['counts']['allowed'], len(CATEGORIES))

    def test_boolean_and_null_spellings_require_exact_classification(self):
        cases = [('keywords.cpp', 'auto yes = true; auto no = false; auto empty = nullptr;\n',
                  ['true', 'false', 'nullptr']),
                 ('keywords.py', 'yes = True\nno = False\nempty = None\n',
                  ['True', 'False', 'None']),
                 ('keywords.js', 'const yes = true; const no = false; const empty = null;\n',
                  ['true', 'false', 'null']),
                 ('Keywords.java', 'class Keywords { boolean yes = true; boolean no = false; Object empty = null; }\n',
                  ['true', 'false', 'null']),
                 ('keywords.kt', 'val yes = true\nval no = false\nval empty = null\n',
                  ['true', 'false', 'null'])]
        for name, text, tokens in cases:
            with self.subTest(name=name):
                relative = 'loom/src/model/' + name
                path = self.source(relative, text)
                try:
                    report, _ = self.run_guard(1)
                    self.assert_unclassified(report, tokens)
                    self.permit(relative, tokens, 'external_standard')
                    report, _ = self.run_guard(0)
                    self.assertEqual(report['counts']['allowed'], len(tokens))
                finally:
                    path.unlink()
                    self.policy['allowlist'] = []
                    self.write_policy()

    def test_whole_source_hash_change_invalidates_unchanged_literal(self):
        relative = 'loom/src/model/stale.cpp'
        path = self.source(relative, 'auto value = "stable";\n')
        self.permit(relative, ['"stable"'])
        original = copy.deepcopy(self.policy['allowlist'][0])
        self.run_guard(0)
        path.write_bytes(path.read_bytes() + b'// inserted comment\n')
        report, _ = self.run_guard(1)
        self.assert_unclassified(report, ['"stable"'])
        for field in ('byte_start', 'byte_end', 'literal_sha256'):
            self.assertEqual(report['findings'][0][field], original[field])
        self.assertNotEqual(report['findings'][0]['source_sha256'], original['source_sha256'])
        self.assertEqual(report['counts']['stale_allowlist'], 1)
        self.assertTrue(report['issues'])

    def test_inserting_literal_requires_its_own_classification(self):
        relative = 'loom/src/model/inserted.cpp'
        self.source(relative, '"original";\n')
        self.permit(relative, ['"original"'])
        self.source(relative, '"original";\n"inserted";\n')
        # Refresh only the existing fixture anchor; the new token stays unclassified.
        self.policy['allowlist'][0] = self.anchor(relative, '"original"')
        self.write_policy()
        report, _ = self.run_guard(1)
        self.assertEqual({f['raw']: f['status'] for f in report['findings']},
                         {'"original"': 'ALLOWED', '"inserted"': 'UNCLASSIFIED'})
        self.assertEqual(report['counts']['stale_allowlist'], 0)

    def test_tampered_byte_and_literal_hash_anchors_are_stale(self):
        relative = 'loom/src/model/tampered.cpp'
        self.source(relative, 'auto s = "anchored";\n')
        original = self.anchor(relative, '"anchored"')
        changes = [dict(byte_start=original['byte_start'] + 1),
                   dict(byte_end=original['byte_end'] - 1),
                   dict(literal_sha256='0' * 64)]
        for change in changes:
            with self.subTest(change=change):
                self.policy['allowlist'] = [dict(original, **change)]
                self.write_policy()
                report, _ = self.run_guard(1)
                self.assert_unclassified(report, ['"anchored"'])
                self.assertEqual(report['counts']['stale_allowlist'], 1)
                self.assertTrue(report['issues'])

    def test_missing_allowlisted_source_is_stale(self):
        relative = 'loom/src/model/removed.cpp'
        path = self.source(relative, '"gone";\n')
        self.permit(relative, ['"gone"'])
        path.unlink()
        report, _ = self.run_guard(1)
        self.assertEqual(report['counts']['stale_allowlist'], 1)
        self.assertFalse(report['findings'])
        self.assertTrue(report['issues'])

    def test_explicit_tests_and_fixtures_exclusions_are_recorded(self):
        paths = ['loom/src/tests/raw.unknown', 'loom/src/model/fixtures/raw.cpp']
        for relative in paths:
            self.source(relative, b'\xff "unreviewed" 987\n')
        report, manifest = self.run_guard(0)
        self.assertEqual(report['counts']['files'], len(paths))
        self.assertEqual(report['counts']['candidates'], 0)
        self.assertEqual(report['counts']['blocked_files'], 0)
        self.assertFalse(report['issues'])
        self.assertEqual({f['path'] for f in manifest['source_files']}, set(paths))
        self.assertTrue(all(f['status'] == 'EXCLUDED_TEST_FIXTURE' for f in manifest['source_files']))
        self.assertTrue(all(f.get('reason') for f in manifest['source_files']))

    def test_exclusions_are_policy_rules_not_implicit_scanner_shortcuts(self):
        self.source('loom/src/tests/value.cpp', '"test token";\n')
        self.policy['exclusions'] = []
        self.write_policy()
        report, _ = self.run_guard(1)
        self.assert_unclassified(report, ['"test token"'])

    def test_owner_rule_and_fallback_are_deterministic(self):
        relative = 'loom/src/model/owner.cpp'
        self.source(relative, '"owned";\n')
        self.policy['owner_rules'] = [{'prefix': 'loom/src', 'thread': 1},
                                      {'prefix': 'loom/src/model', 'thread': 2}]
        self.write_policy()
        report, _ = self.run_guard(1)
        self.assertEqual(report['findings'][0]['owner'], 2)
        self.policy['scopes'] = ['orphan']
        self.source('orphan/value.cpp', '"unowned";\n')
        self.write_policy()
        report, _ = self.run_guard(1)
        self.assertEqual(report['findings'][0]['owner'], self.policy['fallback_owner'])

    def test_unknown_suffix_and_unsupported_markup_css_fail_closed(self):
        cases = [('unknown.xyz', 'unreviewed source\n'),
                 ('page.html', '<div title="product">product</div>\n'),
                 ('style.css', '.product { color: #123456; }\n')]
        for name, text in cases:
            with self.subTest(name=name):
                path = self.source('loom/src/model/' + name, text)
                try:
                    report, _ = self.run_guard(1)
                    self.assertEqual(report['counts']['blocked_files'], 1)
                    self.assertTrue(report['issues'])
                finally:
                    path.unlink()

    def test_javascript_regex_and_templates_fail_closed(self):
        for text in ('const matcher = /[0-9]+/g;\n',
                     'const text = `hello ${name}`;\n'):
            with self.subTest(text=text):
                self.source('loom/src/model/unsupported.js', text)
                report, _ = self.run_guard(1)
                self.assertEqual(report['counts']['blocked_files'], 1)
                self.assertTrue(report['issues'])

    def test_unsupported_interpolations_text_blocks_and_nested_comments_block(self):
        cases = [('template.py', 's = f"hello {name}"\n'),
                 ('template.kt', 'val s = "hello ${name}"\n'),
                 ('Text.java', 'class Text { String s = """\ntext block\n"""; }\n'),
                 ('comment.kt', '/* outer /* nested */ outer */\nval name = "value"\n')]
        for name, text in cases:
            with self.subTest(name=name):
                path = self.source('loom/src/model/' + name, text)
                try:
                    report, _ = self.run_guard(1)
                    self.assertEqual(report['counts']['blocked_files'], 1)
                    self.assertTrue(report['issues'])
                finally:
                    path.unlink()

    def test_invalid_utf8_and_unterminated_tokens_fail_closed(self):
        cases = [('invalid.cpp', b'\xff'),
                 ('unterminated.cpp', b'auto s = "unterminated;\n'),
                 ('unterminated.py', b's = "unterminated\n')]
        for name, raw in cases:
            with self.subTest(name=name):
                path = self.source('loom/src/model/' + name, raw)
                try:
                    report, _ = self.run_guard(1)
                    self.assertEqual(report['counts']['blocked_files'], 1)
                    self.assertTrue(report['issues'])
                finally:
                    path.unlink()

    def test_missing_pack_and_schema_are_input_errors(self):
        for path in (self.pack, self.schema):
            with self.subTest(path=path.relative_to(self.root).as_posix()):
                original = path.read_bytes()
                path.unlink()
                try:
                    report, _ = self.run_guard(2)
                    self.assertTrue(report['issues'])
                finally:
                    path.write_bytes(original)

    def test_malformed_and_duplicate_pack_and_schema_are_input_errors(self):
        cases = [(self.pack, b'{broken'),
                 (self.pack, duplicate_first_key(self.pack.read_bytes())),
                 (self.schema, b'{broken'),
                 (self.schema, duplicate_first_key(self.schema.read_bytes()))]
        for path, raw in cases:
            with self.subTest(path=path.name, raw=raw):
                original = path.read_bytes()
                path.write_bytes(raw)
                try:
                    report, _ = self.run_guard(2)
                    self.assertTrue(report['issues'])
                finally:
                    path.write_bytes(original)

    def test_unknown_category_blank_reason_and_duplicate_anchor_are_input_errors(self):
        relative = 'loom/src/model/policy.cpp'
        self.source(relative, '"value";\n')
        anchor = self.anchor(relative, '"value"')
        lists = [[dict(anchor, category='unreviewed')],
                 [dict(anchor, reason=' \t\n')], [anchor, copy.deepcopy(anchor)]]
        for entries in lists:
            with self.subTest(entries=entries):
                self.policy['allowlist'] = entries
                self.write_policy()
                report, _ = self.run_guard(2)
                self.assertTrue(report['issues'])

    def test_unknown_policy_fields_and_categories_are_input_errors(self):
        original = copy.deepcopy(self.policy)
        for mutation in ('extra_field', 'extra_category', 'missing_category', 'revision'):
            with self.subTest(mutation=mutation):
                self.policy = copy.deepcopy(original)
                if mutation == 'extra_field':
                    self.policy['unrecognized'] = True
                elif mutation == 'extra_category':
                    self.policy['categories']['unreviewed'] = 'Unrecognized disposition.'
                elif mutation == 'missing_category':
                    del self.policy['categories']['contract']
                else:
                    self.policy['revision'] = 0
                self.write_policy()
                report, _ = self.run_guard(2)
                self.assertTrue(report['issues'])

    def test_json_valid_but_invalid_value_schema_is_an_input_error(self):
        schema = json.loads(self.schema.read_bytes())
        schema['type'] = 'unsupported-json-schema-type'
        self.schema.write_text(json.dumps(schema) + '\n', encoding='utf-8')
        report, _ = self.run_guard(2)
        self.assertTrue(report['issues'])

    def test_weakened_value_schema_is_an_input_error(self):
        self.schema.write_text(json.dumps({
            '$schema': 'https://json-schema.org/draft/2020-12/schema',
            'type': 'object'}) + '\n', encoding='utf-8')
        report, _ = self.run_guard(2)
        self.assertTrue(report['issues'])

    def test_empty_scope_is_an_input_error(self):
        self.policy['scopes'] = []
        self.write_policy()
        report, _ = self.run_guard(2)
        self.assertTrue(report['issues'])

    def test_missing_required_scope_fails_closed(self):
        self.policy['scopes'] = ['loom/src/missing']
        self.write_policy()
        report, _ = self.run_guard(1)
        self.assertTrue(report['issues'])

    def test_report_and_manifest_cannot_overwrite_product_source(self):
        relative = 'loom/src/model/preserved.cpp'
        path = self.source(relative, '"preserved";\n')
        self.permit(relative, ['"preserved"'])
        original = path.read_bytes()
        for flag in ('--report', '--manifest'):
            with self.subTest(flag=flag):
                try:
                    result = subprocess.run(
                        [sys.executable, str(GUARD), '--root', str(self.root),
                         '--pack', str(self.pack), '--schema', str(self.schema), '--check',
                         '--report', str(path if flag == '--report' else self.report_path),
                         '--manifest', str(path if flag == '--manifest' else self.manifest_path)],
                        cwd=self.root, capture_output=True, timeout=60, check=False)
                    self.assertEqual(result.returncode, 2,
                                     result.stdout.decode('utf-8', errors='replace') +
                                     result.stderr.decode('utf-8', errors='replace'))
                    observed = path.read_bytes()
                    self.assertEqual(observed, original,
                                     'Bytes after rejected output: ' + repr(observed))
                finally:
                    path.write_bytes(original)

    def test_input_error_cannot_make_report_overwrite_product_source(self):
        relative = 'loom/src/model/preserved.cpp'
        path = self.source(relative, '"preserved";\n')
        self.permit(relative, ['"preserved"'])
        self.policy['allowlist'][0]['reason'] = ' '
        self.write_policy()
        original = path.read_bytes()
        result = subprocess.run(
            [sys.executable, str(GUARD), '--root', str(self.root),
             '--pack', str(self.pack), '--schema', str(self.schema), '--check',
             '--report', str(path), '--manifest', str(self.manifest_path)],
            cwd=self.root, capture_output=True, timeout=60, check=False)
        self.assertEqual(result.returncode, 2)
        observed = path.read_bytes()
        self.assertEqual(observed, original, 'Bytes after rejected output: ' + repr(observed))

    def test_report_and_manifest_are_repeatable(self):
        relative = 'loom/src/model/repeat.cpp'
        self.source(relative, '"repeatable";\n41;\n')
        self.permit(relative, ['"repeatable"'], 'contract')
        self.run_guard(1)
        first = (self.report_path.read_bytes(), self.manifest_path.read_bytes())
        self.run_guard(1)
        self.assertEqual((self.report_path.read_bytes(), self.manifest_path.read_bytes()), first)

    def runtime_embedding_fixture(self):
        generator_relative = 'loom/src/model/gen_runtime_profiles.py'
        output_relative = 'loom/src/model/runtime_profiles_embedded.inc'
        manifest_relative = 'loom/data/runtime_sources.pack'
        profile_relative = 'loom/data/runtime/x.pack'
        generator = self.source(generator_relative,
                                (LOOM / 'src/model/gen_runtime_profiles.py').read_bytes())
        self.source(manifest_relative, json.dumps(
            {'schema': 'loom.runtime_profile_sources/1', 'recipes': []}) + '\n')
        profile = {'schema': 'loom.runtime_profile/1', 'domain': 'x', 'revision': 1,
                   'defaults': {'count': 1},
                   'value_schema': {'type': 'object', 'properties': {
                       'count': {'type': 'integer'}}, 'additionalProperties': False}}
        self.source(profile_relative, json.dumps(profile) + '\n')
        spec = importlib.util.spec_from_file_location('product_literal_fixture_generator', generator)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        output = self.root / output_relative
        # The trusted generator plans against only the toy fixture data in memory.
        expected = module.plan(self.root / 'loom/data', output)[output]
        self.source(output_relative, expected)
        self.policy['scopes'] = [output_relative]
        self.policy['generated_outputs'] = [{
            'path': output_relative, 'operation': 'runtime_profiles_v1',
            'generator': {'path': generator_relative, 'sha256': sha256(generator.read_bytes())},
            'inputs': [{'path': relative, 'sha256': sha256((self.root / relative).read_bytes())}
                       for relative in sorted((manifest_relative, profile_relative))],
            'output_sha256': sha256(expected),
            'reason': 'Synthetic table reproduced exactly from its pinned toy profile sources.'}]
        self.write_policy()
        return {'generator': generator, 'output': output,
                'profile': self.root / profile_relative,
                'manifest': self.root / manifest_relative}

    def test_runtime_embedding_is_accepted_only_by_exact_generator_provenance(self):
        fixture = self.runtime_embedding_fixture()
        report, manifest = self.run_guard(0)
        self.assertEqual(report['counts']['unclassified'], 0)
        self.assertEqual(report['counts']['blocked_files'], 0)
        self.assertFalse(report['issues'])
        entries = [f for f in manifest['source_files']
                   if f['path'] == fixture['output'].relative_to(self.root).as_posix()]
        self.assertEqual(len(entries), 1)
        self.assertNotEqual(entries[0]['status'], 'BLOCKED')
        self.assertTrue(entries[0].get('reason'))

    def test_runtime_embedding_source_output_and_generator_changes_block(self):
        fixture = self.runtime_embedding_fixture()
        originals = {name: path.read_bytes() for name, path in fixture.items()}
        for name in ('profile', 'manifest', 'output', 'generator'):
            with self.subTest(changed=name):
                path = fixture[name]
                path.write_bytes(originals[name] + b'\n')
                try:
                    report, _ = self.run_guard(1)
                    self.assertTrue(report['issues'])
                    self.assertGreaterEqual(report['counts']['blocked_files'], 1)
                finally:
                    path.write_bytes(originals[name])
        self.run_guard(0)

    def test_runtime_embedding_new_source_is_not_silently_adopted(self):
        self.runtime_embedding_fixture()
        self.source('loom/data/runtime/y.pack', json.dumps({
            'schema': 'loom.runtime_profile/1', 'domain': 'y', 'revision': 1,
            'defaults': {}, 'value_schema': {'type': 'object'}}) + '\n')
        report, _ = self.run_guard(1)
        self.assertTrue(report['issues'])
        self.assertGreaterEqual(report['counts']['blocked_files'], 1)

    def test_runtime_embedding_input_and_output_hash_tampering_blocks(self):
        self.runtime_embedding_fixture()
        original = copy.deepcopy(self.policy['generated_outputs'][0])
        for field in ('input', 'output'):
            with self.subTest(field=field):
                entry = copy.deepcopy(original)
                if field == 'input':
                    entry['inputs'][0]['sha256'] = '0' * 64
                else:
                    entry['output_sha256'] = '0' * 64
                self.policy['generated_outputs'] = [entry]
                self.write_policy()
                report, _ = self.run_guard(1)
                self.assertTrue(report['issues'])
                self.assertGreaterEqual(report['counts']['blocked_files'], 1)

    def test_runtime_embedding_untrusted_operation_and_generator_path_are_input_errors(self):
        self.runtime_embedding_fixture()
        original = copy.deepcopy(self.policy['generated_outputs'][0])
        for field in ('operation', 'generator'):
            with self.subTest(field=field):
                entry = copy.deepcopy(original)
                if field == 'operation':
                    entry['operation'] = 'import_arbitrary_function'
                else:
                    entry['generator']['path'] = 'loom/src/model/other_generator.py'
                self.policy['generated_outputs'] = [entry]
                self.write_policy()
                report, _ = self.run_guard(2)
                self.assertTrue(report['issues'])


if __name__ == '__main__':
    unittest.main()
