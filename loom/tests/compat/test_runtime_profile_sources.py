#!/usr/bin/env python3
"""Offline generator transaction, canonical-source and provenance regressions."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

LOOM = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('runtime_sources_generator', LOOM / 'src/model/gen_runtime_profiles.py')
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


def unpack_serialized_table(text, name):
    table = text.split(f'{name}[] = {{', 1)[1].split('\n};', 1)[0]
    result = {}
    entries = re.findall(r'^  \{"([a-z0-9_-]+)", ([A-Za-z0-9_]+)\},$', table, re.M)
    for domain, chunks in entries:
        body = text.split(f'{chunks}[] = {{', 1)[1].split('\n};', 1)[0]
        raw = ''.join(re.findall(r'R"LPROFILE\((.*?)\)LPROFILE"', body, re.S))
        result[domain] = raw.encode('utf-8')
    return result


def unpack_table(text, name):
    return {domain: json.loads(raw) for domain, raw in unpack_serialized_table(text, name).items()}


class RuntimeProfileSourceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.data = self.root / 'data'
        (self.data / 'runtime').mkdir(parents=True)
        (self.data / 'policy').mkdir()
        for relative in ('runtime_sources.pack', 'runtime/usage_policy.pack', 'policy/usage_policy.pack'):
            shutil.copyfile(LOOM / 'data' / relative, self.data / relative)
        self.output = self.root / 'generated.inc'
        self.manifest = json.loads((self.data / 'runtime_sources.pack').read_bytes())

    def write_manifest(self):
        (self.data / 'runtime_sources.pack').write_text(json.dumps(self.manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def plan(self):
        return GEN.plan(self.data, self.output)

    def assert_compiled_tables(self, generated, expected):
        candidates = [shlex.split(os.environ.get('CXX', ''))]
        candidates += [[name] for name in ['g++', 'clang++'] +
                       [f'clang++-{version}' for version in range(21, 13, -1)]]
        compilers, resolved = [], set()
        for command in candidates:
            executable = shutil.which(command[0]) if command else None
            if executable and Path(executable).resolve() not in resolved:
                resolved.add(Path(executable).resolve())
                compilers.append([executable, *command[1:]])
        self.assertTrue(compilers, 'a C++ compiler is required for embedded byte verification')
        self.output.write_bytes(generated)
        probe = self.root / 'probe.cpp'
        probe.write_text(
            '#include <cstdio>\n#include <span>\n#include <string>\n'
            '#include <string_view>\n#include <utility>\n#include "generated.inc"\n'
            'template <class T, std::size_t N> void write_table(const T (&table)[N]) {\n'
            '  std::printf("%zu\\n", N);\n'
            '  for (const auto& entry : table) {\n'
            '    std::fwrite(entry.first.data(), 1, entry.first.size(), stdout);\n'
            '    std::putchar(\'\\n\');\n'
            '    std::string document;\n'
            '    for (auto chunk : entry.second) document.append(chunk);\n'
            '    std::printf("%zu\\n", document.size());\n'
            '    std::fwrite(document.data(), 1, document.size(), stdout);\n'
            '  }\n}\n'
            'int main() {\n'
            '  write_table(kRuntimeProfiles);\n'
            '  write_table(kRuntimeProfileSources);\n'
            '  return std::ferror(stdout) ? 1 : 0;\n}\n', encoding='utf-8')
        for compiler in compilers:
            with self.subTest(compiler=compiler):
                binary = self.root / 'probe'
                result = subprocess.run(
                    [*compiler, '-std=c++20', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                     '-Woverlength-strings', str(probe), '-o', str(binary)],
                    capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8', errors='replace'))
                raw = subprocess.check_output([str(binary)], timeout=10)
                actual = {}
                for name in ('kRuntimeProfiles', 'kRuntimeProfileSources'):
                    count, raw = raw.split(b'\n', 1)
                    actual[name] = {}
                    for _ in range(int(count)):
                        domain, raw = raw.split(b'\n', 1)
                        size, raw = raw.split(b'\n', 1)
                        size = int(size)
                        self.assertGreaterEqual(len(raw), size)
                        self.assertNotIn(domain.decode(), actual[name])
                        actual[name][domain.decode()] = raw[:size]
                        raw = raw[size:]
                self.assertEqual(raw, b'')
                self.assertEqual(actual, expected)

    def run_generator(self, check=False):
        command = [sys.executable, str(LOOM / 'src/model/gen_runtime_profiles.py'),
                   '--data-dir', str(self.data), '--output', str(self.output)]
        if check:
            command.append('--check')
        return subprocess.run(command, capture_output=True, check=False)

    def assert_failure_preserves_outputs(self, change):
        GEN.apply_plan(self.plan())
        before = {path: path.read_bytes() for path in (self.output, self.data / 'runtime/usage_policy.pack')}
        change()
        result = self.run_generator()
        self.assertNotEqual(result.returncode, 0, result.stdout.decode())
        for path, raw in before.items():
            self.assertEqual(path.read_bytes(), raw)
        self.assertFalse(list(self.root.rglob('.*.inc.*')))

    def test_original_wrapper_definition_and_hash_are_preserved(self):
        outputs = self.plan()
        wrapper = outputs[self.data / 'runtime/usage_policy.pack']
        self.assertEqual(wrapper, (LOOM / 'data/runtime/usage_policy.pack').read_bytes())
        definition = json.loads(wrapper)
        canonical = json.dumps({'definition': definition, 'values': definition['defaults']},
                               sort_keys=True, ensure_ascii=False, separators=(',', ':')).encode()
        self.assertEqual(hashlib.sha256(canonical).hexdigest(),
                         'e8b8d4e5b098213a421b88b1f11db949a8a6056838a2f8c9487303913da5a8bd')
        embedded = unpack_table(outputs[self.output].decode(), 'kRuntimeProfiles')
        self.assertEqual(embedded['usage_policy'], definition)

    def test_all_original_definition_entries_and_output_are_portable(self):
        shutil.rmtree(self.data / 'runtime')
        shutil.copytree(LOOM / 'data/runtime', self.data / 'runtime')
        local = GEN.plan(LOOM / 'data', self.root / 'original.inc')
        shadow = self.plan()
        self.assertEqual(local[self.root / 'original.inc'], shadow[self.output])
        definitions = unpack_table(shadow[self.output].decode(), 'kRuntimeProfiles')
        for path in (LOOM / 'data/runtime').glob('*.pack'):
            self.assertEqual(definitions[path.stem], json.loads(path.read_bytes()))

    def test_current_definition_and_provenance_bytes_compile_without_overlength_literals(self):
        generated = GEN.plan(LOOM / 'data', self.output)[self.output]
        expected = {name: unpack_serialized_table(generated.decode('utf-8'), name)
                    for name in ('kRuntimeProfiles', 'kRuntimeProfileSources')}
        self.assertEqual(set(expected['kRuntimeProfiles']), set(expected['kRuntimeProfileSources']))
        for path in (LOOM / 'data/runtime').glob('*.pack'):
            definition, _ = GEN.read_document(path)
            serialized = json.dumps(definition, ensure_ascii=False, allow_nan=False, separators=(',', ':'))
            self.assertEqual(expected['kRuntimeProfiles'][path.stem], serialized.encode('utf-8'))
        self.assert_compiled_tables(generated, expected)

    def test_large_utf8_unknown_values_and_escaped_nul_survive_compiled_chunks(self):
        source_path = self.data / 'policy/usage_policy.pack'
        source = json.loads(source_path.read_bytes())
        source['future_setting'] = {'text': 'zażółć 🧠 \0' * 9000,
                                    'integer': 9007199254740993, 'fraction': 1.0,
                                    'unknown': {'nullable': None, 'items': [False, True]}}
        source_key = 'nested/' + 'ź🧠/' * 9000
        source_path.write_text(json.dumps({source_key: source}, ensure_ascii=False), encoding='utf-8')
        pointer = '/' + source_key.replace('~', '~0').replace('/', '~1')
        self.manifest['recipes'][0]['projections'][0]['pointer'] = pointer
        self.write_manifest()
        outputs = self.plan()
        generated = outputs[self.output]
        expected = {name: unpack_serialized_table(generated.decode('utf-8'), name)
                    for name in ('kRuntimeProfiles', 'kRuntimeProfileSources')}
        definition = json.loads(outputs[self.data / 'runtime/usage_policy.pack'])
        self.assertEqual(definition['defaults'], source)
        serialized = json.dumps(definition, ensure_ascii=False, allow_nan=False, separators=(',', ':')).encode('utf-8')
        self.assertGreater(len(serialized), 65536)
        self.assertEqual(expected['kRuntimeProfiles']['usage_policy'], serialized)
        self.assertIn(b'\\u0000', serialized)
        self.assertNotIn(b'\0', serialized)
        self.assertIn(b'9007199254740993', serialized)
        self.assertIn(b'"fraction":1.0', serialized)
        provenance = expected['kRuntimeProfileSources']['usage_policy']
        self.assertGreater(len(provenance), 65536)
        self.assertEqual(json.loads(provenance)[1]['pointer'], pointer)
        self.assert_compiled_tables(generated, expected)

    def test_canonical_source_edit_updates_wrapper_and_embedding_without_copy(self):
        source_path = self.data / 'policy/usage_policy.pack'
        source = json.loads(source_path.read_bytes())
        source['baseline_window'] = None
        source['growth_factor'] = 23.0
        source['initial_baselines'] = {'offline': {'tokens': 12345, 'bytes': None}}
        source['future_setting'] = {'names': ['żółć', 'alternative']}
        source_path.write_text(json.dumps(source, ensure_ascii=False) + '\n', encoding='utf-8')
        (self.data / 'runtime/usage_policy.pack').write_text('{broken generated output', encoding='utf-8')
        output = self.plan()
        definition = json.loads(output[self.data / 'runtime/usage_policy.pack'])
        self.assertEqual(definition['defaults'], source)
        self.assertEqual(definition['revision'], self.manifest['recipes'][0]['definition']['revision'])
        self.assertEqual(definition['description'], self.manifest['recipes'][0]['definition']['description'])
        self.assertEqual(unpack_table(output[self.output].decode(), 'kRuntimeProfiles')['usage_policy'], definition)
        GEN.apply_plan(output)
        self.assertEqual(self.run_generator(check=True).returncode, 0)

    def test_raw_source_identity_preserves_bom_and_line_endings_separately(self):
        before = self.plan()
        path = self.data / 'policy/usage_policy.pack'
        raw = b'\xef\xbb\xbf' + path.read_bytes().replace(b'\n', b'\r\n')
        path.write_bytes(raw)
        after = self.plan()
        self.assertEqual(before[self.data / 'runtime/usage_policy.pack'], after[self.data / 'runtime/usage_policy.pack'])
        text = after[self.output].decode()
        sources = unpack_table(text, 'kRuntimeProfileSources')['usage_policy']
        source = next(s for s in sources if s['path'] == 'loom/data/policy/usage_policy.pack')
        self.assertEqual(source, {'path': 'loom/data/policy/usage_policy.pack', 'pointer': '',
                                 'raw_sha256': hashlib.sha256(raw).hexdigest(), 'target': '/defaults'})
        self.assertNotEqual(before[self.output], after[self.output])

    def test_check_verifies_derived_wrapper_as_well_as_embedding(self):
        GEN.apply_plan(self.plan())
        self.assertEqual(self.run_generator(check=True).returncode, 0)
        wrapper = self.data / 'runtime/usage_policy.pack'
        wrapper.write_bytes(wrapper.read_bytes() + b' ')
        result = self.run_generator(check=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'runtime/usage_policy.pack', result.stderr)
        GEN.apply_plan(self.plan())
        self.output.write_bytes(self.output.read_bytes() + b' ')
        self.assertNotEqual(self.run_generator(check=True).returncode, 0)

    def test_generic_full_definition_and_escaped_array_projection(self):
        definition = {'schema': 'loom.runtime_profile/1', 'domain': 'unrelated', 'revision': 2,
                      'defaults': {'a/b~c': [None]},
                      'value_schema': {'type': 'object', 'properties': {'a/b~c':
                          {'type': 'array', 'items': {'type': 'integer'}}}}}
        source = {'descriptor': definition, 'values': [777]}
        (self.data / 'other.pack').write_text(json.dumps(source), encoding='utf-8')
        self.manifest['recipes'] = [{'output': 'runtime/unrelated.pack', 'definition': {},
                                    'projections': [{'source': 'other.pack', 'pointer': '/descriptor', 'target': ''}]}]
        # Apply a nested projection in a separate recipe/template: overlapping
        # projections within one recipe are intentionally rejected.
        definition['defaults']['a/b~c'][0] = 777
        self.manifest['recipes'][0]['definition'] = definition
        self.manifest['recipes'][0]['projections'] = [{'source': 'other.pack', 'pointer': '/values/0',
                                                     'target': '/defaults/a~1b~0c/0'}]
        self.write_manifest()
        outputs = self.plan()
        self.assertEqual(json.loads(outputs[self.data / 'runtime/unrelated.pack'])['defaults'], {'a/b~c': [777]})
        source['descriptor']['defaults']['a/b~c'][0] = 777
        (self.data / 'other.pack').write_text(json.dumps(source), encoding='utf-8')
        self.manifest['recipes'][0]['definition'] = {}
        self.manifest['recipes'][0]['projections'] = [{'source': 'other.pack', 'pointer': '/descriptor', 'target': ''}]
        self.write_manifest()
        self.assertEqual(json.loads(self.plan()[self.data / 'runtime/unrelated.pack']), source['descriptor'])

    def test_bad_sources_fail_before_any_write(self):
        bad_sources = [b'{broken', b'{"same":1,"same":2}', b'{"n":NaN}', b'{"n":Infinity}',
                       b'{"n":1e999}', b'{"n":"\\ud800"}', b'\xff']
        path = self.data / 'policy/usage_policy.pack'
        original = path.read_bytes()
        for raw in bad_sources:
            with self.subTest(raw=raw):
                path.write_bytes(original)
                self.assert_failure_preserves_outputs(lambda: path.write_bytes(raw))
        path.write_bytes(original)

    def test_schema_invalid_source_and_second_recipe_are_transactional(self):
        path = self.data / 'policy/usage_policy.pack'
        original = path.read_bytes()
        source = json.loads(original)
        source['baseline_window'] = 0
        self.assert_failure_preserves_outputs(lambda: path.write_text(json.dumps(source), encoding='utf-8'))
        path.write_bytes(original)
        def broken_second_recipe():
            recipe = copy.deepcopy(self.manifest['recipes'][0])
            recipe['output'] = 'runtime/second.pack'
            recipe['definition']['domain'] = 'second'
            recipe['projections'][0]['pointer'] = '/absent'
            self.manifest['recipes'].append(recipe)
            self.write_manifest()
        self.assert_failure_preserves_outputs(broken_second_recipe)
        self.assertFalse((self.data / 'runtime/second.pack').exists())

    def test_path_escapes_and_invalid_pointers_fail_closed(self):
        original = copy.deepcopy(self.manifest)
        for field, values in [('source', ['../outside.pack', '/absolute.pack', 'policy/../outside.pack',
                                         'policy\\usage_policy.pack']),
                              ('pointer', ['/missing', '/bad~2escape', 'not-a-pointer']),
                              ('target', ['/missing', '/bad~2escape', 'not-a-pointer'])]:
            for value in values:
                with self.subTest(field=field, value=value):
                    self.manifest = copy.deepcopy(original)
                    self.write_manifest()
                    def change():
                        self.manifest['recipes'][0]['projections'][0][field] = value
                        self.write_manifest()
                    self.assert_failure_preserves_outputs(change)
        self.manifest = original
        self.write_manifest()
        outside = self.root / 'outside.pack'
        outside.write_bytes((self.data / 'policy/usage_policy.pack').read_bytes())
        def symlink_escape():
            path = self.data / 'policy/usage_policy.pack'
            path.unlink()
            path.symlink_to(outside)
        self.assert_failure_preserves_outputs(symlink_escape)

    def test_duplicate_outputs_cycles_overlap_and_unknown_fields_rejected(self):
        original = copy.deepcopy(self.manifest)
        changes = [lambda m: m['recipes'].append(copy.deepcopy(m['recipes'][0])),
                   lambda m: m['recipes'][0]['projections'][0].update(source='runtime/usage_policy.pack'),
                   lambda m: m['recipes'][0]['projections'].append(copy.deepcopy(m['recipes'][0]['projections'][0])),
                   lambda m: m['recipes'][0].update(unknown=True),
                   lambda m: m['recipes'][0]['definition']['value_schema'].update(uniqueItems=True)]
        for index, change in enumerate(changes):
            with self.subTest(index=index):
                self.manifest = copy.deepcopy(original)
                self.write_manifest()
                def apply_change():
                    change(self.manifest)
                    self.write_manifest()
                self.assert_failure_preserves_outputs(apply_change)

    def test_symlink_alias_cannot_turn_generated_output_into_canonical_input(self):
        original = copy.deepcopy(self.manifest)
        def source_alias():
            (self.data / 'alias.pack').symlink_to(self.data / 'runtime/usage_policy.pack')
            self.manifest['recipes'][0]['projections'][0]['source'] = 'alias.pack'
            self.write_manifest()
        self.assert_failure_preserves_outputs(source_alias)
        self.manifest = original
        self.write_manifest()
        def output_alias():
            wrapper = self.data / 'runtime/usage_policy.pack'
            elsewhere = self.data / 'saved.pack'
            elsewhere.write_bytes(wrapper.read_bytes())
            wrapper.unlink()
            wrapper.symlink_to(elsewhere)
        self.assert_failure_preserves_outputs(output_alias)

    def test_hardlink_alias_cannot_turn_generated_output_into_canonical_input(self):
        def source_alias():
            os.link(self.data / 'runtime/usage_policy.pack', self.data / 'alias.pack')
            self.manifest['recipes'][0]['projections'][0]['source'] = 'alias.pack'
            self.write_manifest()
        self.assert_failure_preserves_outputs(source_alias)

    def test_embedding_cannot_overwrite_source_and_delimiter_text_is_inert(self):
        self.assertRaises(ValueError, GEN.plan, self.data, self.data / 'policy/usage_policy.pack')
        original = copy.deepcopy(self.manifest)
        def invalid_delimiter():
            self.manifest['recipes'][0]['definition']['description'] = ')LPROFILE"'
            self.write_manifest()
        # JSON escapes the quote, so this text is safe and must remain data.
        invalid_delimiter()
        GEN.apply_plan(self.plan())
        self.assertEqual(json.loads((self.data / 'runtime/usage_policy.pack').read_bytes())['description'], ')LPROFILE"')
        self.manifest = original
        self.write_manifest()
        def actual_raw_delimiter():
            self.manifest['recipes'][0]['definition']['description'] = ')LPROFILE'
            self.write_manifest()
        self.assert_failure_preserves_outputs(actual_raw_delimiter)

    def test_malformed_standalone_profile_also_prevents_derived_writes(self):
        def malformed_standalone():
            (self.data / 'runtime/z_last.pack').write_text('{broken', encoding='utf-8')
        self.assert_failure_preserves_outputs(malformed_standalone)

    def test_integer_bounds_and_enum_follow_native_exact_comparison(self):
        schema = {'type': 'integer', 'maximum': 9007199254740992.0}
        GEN.validate_schema(schema)
        GEN.validate_value(9007199254740992, schema)
        self.assertRaises(ValueError, GEN.validate_value, 9007199254740993, schema)
        self.assertRaises(ValueError, GEN.validate_value, True, schema)
        enum = {'type': 'integer', 'enum': [-1]}
        self.assertRaises(ValueError, GEN.validate_value, 18446744073709551615, enum)
        self.assertRaises(ValueError, GEN.validate_schema, {'type': 'object', 'additionalProperties': 1})

    def test_wide_finite_integer_tokens_follow_native_double_representation(self):
        path = self.data / 'policy/usage_policy.pack'
        source = json.loads(path.read_bytes())
        source['initial_baselines'] = {'wide': {'quantity': 36893488147419103232}}
        source['future_setting'] = 18446744073709551616
        path.write_text(json.dumps(source), encoding='utf-8')
        integer_token = self.plan()
        parsed, _ = GEN.read_document(path)
        self.assertIs(type(parsed['future_setting']), float)
        self.assertIs(type(parsed['initial_baselines']['wide']['quantity']), float)
        source['initial_baselines']['wide']['quantity'] = 3.6893488147419103e19
        source['future_setting'] = 1.8446744073709552e19
        path.write_text(json.dumps(source), encoding='utf-8')
        exponent_token = self.plan()
        wrapper = self.data / 'runtime/usage_policy.pack'
        self.assertEqual(integer_token[wrapper], exponent_token[wrapper])
        source['baseline_window'] = 36893488147419103232
        path.write_text(json.dumps(source), encoding='utf-8')
        self.assertRaises(ValueError, self.plan)  # Actual integer setting.


if __name__ == '__main__':
    unittest.main()
