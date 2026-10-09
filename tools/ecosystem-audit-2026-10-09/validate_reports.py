#!/usr/bin/env python3
"""Validate public audit reports only; no source execution or private access."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess

import argparse
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repos-root', type=Path, required=True)
parser.add_argument('--reports-root', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
REPOS = ['chatadhd', 'Watchdog-JH16', 'Custom-Keyboard-Pro', 'AGEDS', 'LEM-Workbench', 'loom']
SCANNER = '88b6f460b471347a937a88bd11ebc7b0a504b022014a9ca0693f56835e4489d8'
RULES = 'b35034619f42b0199bd9a3c26245dcd6be0dd83ecffa8beb7bb4c01731e4e963'
CLASSES = ['naruszenie', 'dopuszczalny mechanizm', 'niepewne', 'już naprawione']
FORBIDDEN = re.compile(r'(?i)(^|/)[^/]*(blind|holdout|heldout|hold-out|held-out)[^/]*(/|$)|(^|/)(\.env(?:\.[^/]*)?|credentials?[^/]*|secrets?[^/]*)(/|$)')
ALIASES = {
    'requirement_source': ['requirement_source', 'requirement', 'requirements'],
    'target_data_graph_object': ['target_data_graph_object', 'target_data_graph'],
    'consuming_paths': ['consuming_paths', 'consumers', 'runtime_consumers'],
}


def sha256(content):
    return hashlib.sha256(content).hexdigest()


def present(value):
    return value is not None and value != '' and value != [] and value != {}


def get_alias(row, field):
    for key in ALIASES.get(field, [field]):
        if present(row.get(key)):
            return row[key], key
    return None, None


def locations(value, pointer='$'):
    if isinstance(value, dict):
        path = value.get('file') or value.get('path')
        start, end = value.get('line_start'), value.get('line_end')
        line_value = value.get('lines')
        if isinstance(line_value, dict):
            start, end = line_value.get('start'), line_value.get('end')
        elif isinstance(line_value, list) and len(line_value) == 2:
            start, end = line_value
        if path and (start is not None or end is not None):
            yield {'path': path, 'start': start, 'end': end, 'json_pointer': pointer}
        for key, item in value.items():
            if isinstance(item, (dict, list)):
                yield from locations(item, pointer + '.' + key)
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from locations(item, pointer + '[' + str(index) + ']')


def git_blob(repo, sha, path):
    result = subprocess.run(['git', '-C', str(args.repos_root/repo), 'show', sha + ':' + path],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return result.stdout if result.returncode == 0 else None


def main():
    issues = []
    repositories = []
    seen_ids = {}
    source_cache = {}
    global_classes = Counter()
    all_records = []
    for repo in REPOS:
        directory = args.reports_root / repo
        findings_path = directory / 'findings.jsonl'
        summary_path = directory / 'scan' / 'summary.json'
        finding_bytes = findings_path.read_bytes()
        summary_bytes = summary_path.read_bytes()
        finding_hash = sha256(finding_bytes)
        summary = json.loads(summary_bytes)
        report = {'repo': 'klb-t/' + repo, 'sha': summary['sha'],
                  'findings_sha256': finding_hash, 'scan_summary_sha256': sha256(summary_bytes),
                  'scanner_sha256': summary.get('scanner_sha256'), 'rules_sha256': summary.get('rules_sha256'),
                  'finding_count': 0, 'classifications': {}, 'location_count': 0,
                  'valid_location_count': 0, 'required_fields_pass_count': 0,
                  'inventory_denominator': summary['denominator'], 'finding_checks': []}
        for field, expected in [('scanner_sha256', SCANNER), ('rules_sha256', RULES)]:
            if summary.get(field) != expected:
                issues.append({'severity': 'error', 'repo': repo, 'check': field, 'expected': expected,
                               'actual': summary.get(field)})
        class_counts = Counter()
        for lineno, line in enumerate(finding_bytes.decode('utf-8').splitlines(), 1):
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                issues.append({'severity': 'error', 'repo': repo, 'check': 'jsonl_parse', 'line': lineno})
                continue
            fid = row.get('id')
            report['finding_count'] += 1
            errors, warnings = [], []
            if not fid or not isinstance(fid, str):
                errors.append({'check': 'stable_id_missing'})
            elif fid in seen_ids:
                errors.append({'check': 'duplicate_id', 'previous_repo': seen_ids[fid]})
            else:
                seen_ids[fid] = repo
            if row.get('repo') != 'klb-t/' + repo:
                errors.append({'check': 'repo_mismatch'})
            if row.get('sha') != summary['sha']:
                errors.append({'check': 'sha_mismatch', 'expected': summary['sha'], 'actual': row.get('sha')})
            classification = row.get('classification')
            class_counts[classification] += 1
            global_classes[classification] += 1
            if classification not in CLASSES:
                errors.append({'check': 'unknown_classification', 'actual': classification})
            resolved_fields = {}
            for field in ['evidence', 'impact', 'requirement_source', 'alternatives',
                          'target_data_graph_object', 'consuming_paths', 'acceptance_test', 'migration_risk']:
                value, key = get_alias(row, field)
                if value is None:
                    errors.append({'check': 'missing_required_field', 'field': field})
                else:
                    resolved_fields[field] = key
            if 'dependencies' not in row or not isinstance(row['dependencies'], list):
                errors.append({'check': 'missing_or_invalid_dependencies'})
            behavior = row.get('behavior')
            if not present(behavior) and isinstance(row.get('evidence'), dict):
                behavior = row['evidence'].get('behavior') or row['evidence'].get('observation')
            if not present(behavior):
                errors.append({'check': 'missing_actual_behavior'})
            if not present(row.get('interpretation')):
                warnings.append({'check': 'empty_interpretation', 'note': 'No separate auditor interpretation is supplied; not inferred from other text.'})
            if not present(row.get('recommendation')):
                warnings.append({'check': 'empty_recommendation', 'note': 'Target object and acceptance test can still be present, but recommendation is not explicit.'})
            alternatives = row.get('alternatives')
            if isinstance(alternatives, list):
                for index, option in enumerate(alternatives):
                    if not isinstance(option, dict) or not present(option.get('status')) or not any(present(option.get(k)) for k in ['name', 'option', 'variant']):
                        errors.append({'check': 'alternative_missing_name_or_status', 'index': index})
            elif alternatives is not None:
                errors.append({'check': 'alternatives_not_list'})
            r42 = row.get('r42_exception') or row.get('r42')
            if classification == 'dopuszczalny mechanizm':
                if isinstance(r42, dict) and present(r42.get('reason')) and (r42.get('categories') or r42.get('exception')):
                    r42_status = 'structured_category_and_reason'
                elif present(row.get('interpretation')) and 'R42' in json.dumps(row.get('requirements', []), ensure_ascii=False):
                    r42_status = 'narrative_category_and_reason_requires_semantic_review'
                else:
                    r42_status = 'missing'
                    errors.append({'check': 'r42_exception_missing_category_or_reason'})
            else:
                r42_status = 'not_claimed_as_allowed_mechanism'
            loc_checks = []
            for loc in locations(row):
                check = dict(loc)
                report['location_count'] += 1
                path, start, end = loc['path'], loc['start'], loc['end']
                if not isinstance(path, str) or path.startswith('/') or '..' in Path(path).parts:
                    check['status'] = 'invalid_path'
                elif FORBIDDEN.search(path):
                    check['status'] = 'excluded_sensitive_or_holdout_path_not_read'
                elif not isinstance(start, int) or not isinstance(end, int) or start < 1 or end < start:
                    check['status'] = 'invalid_span'
                else:
                    key = (repo, row.get('sha'), path)
                    if key not in source_cache:
                        content = git_blob(repo, row['sha'], path)
                        source_cache[key] = None if content is None else {
                            'line_count': len(content.splitlines()), 'source_sha256': sha256(content)}
                    metadata = source_cache[key]
                    if metadata is None:
                        check['status'] = 'file_missing_at_sha'
                    else:
                        check.update(metadata)
                        check['status'] = 'valid' if end <= metadata['line_count'] else 'span_exceeds_file'
                if check['status'] == 'valid':
                    report['valid_location_count'] += 1
                else:
                    errors.append({'check': 'locator_invalid', **check})
                loc_checks.append(check)
            if not loc_checks:
                errors.append({'check': 'no_structured_source_locator'})
            required_ok = not any(e['check'] in ['missing_required_field', 'missing_actual_behavior', 'missing_or_invalid_dependencies', 'alternative_missing_name_or_status', 'alternatives_not_list'] for e in errors)
            report['required_fields_pass_count'] += int(required_ok)
            finding_check = {'id': fid, 'classification': classification, 'field_aliases': resolved_fields,
                             'required_fields_valid': required_ok, 'r42_justification': r42_status,
                             'locations': loc_checks, 'errors': errors, 'warnings': warnings}
            report['finding_checks'].append(finding_check)
            for severity, items in [('error', errors), ('warning', warnings)]:
                for issue in items:
                    issues.append({'severity': severity, 'repo': repo, 'finding_id': fid, **issue})
            all_records.append(row)
        if sha256(findings_path.read_bytes()) != finding_hash:
            issues.append({'severity': 'error', 'repo': repo, 'check': 'findings_changed_during_validation'})
        report['classifications'] = {k: class_counts[k] for k in CLASSES}
        report['all_scan_hashes_expected'] = report['scanner_sha256'] == SCANNER and report['rules_sha256'] == RULES
        repositories.append(report)
    errors = sum(issue['severity'] == 'error' for issue in issues)
    warnings = len(issues) - errors
    result = {'schema': 'klbt.audit-artifacts.validation/1', 'date': '2026-10-09',
              'status': 'FAIL' if errors else ('PASS_WITH_WARNINGS' if warnings else 'PASS'),
              'scope': {'repositories': ['klb-t/' + r for r in REPOS],
                        'files': 'public/<repo>/findings.jsonl and scan/summary.json',
                        'source': 'Only public files named by structured finding locators, read at each exact SHA for existence/line counts/hash; no product execution.',
                        'private_data_accessed': False, 'holdout_content_accessed': False,
                        'semantic_claims_reproven': False},
              'expected_scanner_sha256': SCANNER, 'expected_rules_sha256': RULES,
              'finding_count': len(all_records), 'unique_finding_ids': len(seen_ids),
              'classifications': {k: global_classes[k] for k in CLASSES},
              'structured_locations': sum(r['location_count'] for r in repositories),
              'valid_structured_locations': sum(r['valid_location_count'] for r in repositories),
              'distinct_source_files_at_sha': len(source_cache),
              'error_count': errors, 'warning_count': warnings,
              'repositories': repositories, 'issues': issues,
              'limitations': ['Locators embedded only in free text are not parsed or validated.',
                              'Required-field validation proves presence and basic shape, not truth or completeness of semantic analysis.',
                              'Narrative R42 justifications remain narrative; no new exception approval is made.',
                              'No inference that a non-executed source-path analysis is a runtime experiment.',
                              'This validates inputs by recorded hashes; edits after this receipt require rerun.']}
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    (out/'validation-summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    md = ['# Walidacja publicznych artefaktów audytu — 2026-10-09', '',
          f"Status: **{result['status']}**. {len(all_records)} findings, {len(seen_ids)} unikalnych ID; "
          f"{result['valid_structured_locations']}/{result['structured_locations']} poprawnych lokatorów na wskazanych SHA.", '',
          f"Błędy: **{errors}**; ostrzeżenia: **{warnings}**.", '',
          '| Repo | SHA | Findings | Naruszenia | Mechanizmy | Naprawione | Niepewne | Lokatory | Hash skanera/reguł |',
          '|---|---|---:|---:|---:|---:|---:|---:|---|']
    for item in repositories:
        cls=item['classifications']
        md.append(f"| {item['repo']} | `{item['sha']}` | {item['finding_count']} | {cls['naruszenie']} | {cls['dopuszczalny mechanizm']} | {cls['już naprawione']} | {cls['niepewne']} | {item['valid_location_count']}/{item['location_count']} | {'PASS' if item['all_scan_hashes_expected'] else 'FAIL'} |")
    md += ['', 'Walidacja obejmuje istnienie plików i zakresów linii, unikalność ID, zgodność SHA z summary, obecność pól wymaganych przez właściciela (z obsługą aliasów schematów), statusy alternatyw i uzasadnienie przywołanych wyjątków R42. Puste dependencies są poprawną deklaracją braku zależności.', '',
           'Nie uruchamiano ani nie importowano produktu. Nie czytano prywatnych repo ani korpusów blind/holdout. To kontrola integralności artefaktów; nie stanowi ponownego dowodu wszystkich twierdzeń semantycznych.', '',
           '## Konkretne braki i ostrzeżenia', '']
    if not issues:
        md.append('Brak stwierdzonych błędów lub ostrzeżeń w zdefiniowanym zakresie.')
    for issue in issues:
        loc = issue.get('path', '')
        extra = (' — '+loc+':'+str(issue.get('start'))+'–'+str(issue.get('end'))) if loc else ''
        field = (' — '+issue['field']) if 'field' in issue else ''
        md.append(f"- **{issue['severity']}** {issue.get('finding_id', issue['repo'])}: `{issue['check']}`{extra}{field}.")
    md += ['', '## Granice', '',
           'Lokatory występujące wyłącznie w swobodnym tekście nie są automatycznie parsowane. Obecność pól nie dowodzi poprawności zachowania. Puste interpretation/recommendation są zgłaszane osobno jako braki jawnego rozdzielenia warstw, bez dopisywania intencji autora. Źródła i wejściowe findings mają hashe w JSON; późniejsza zmiana raportu wymaga ponowienia tej walidacji.', '']
    (out/'validation-summary.md').write_text('\n'.join(md))
    print(json.dumps({k:result[k] for k in ['status','finding_count','unique_finding_ids','classifications','structured_locations','valid_structured_locations','distinct_source_files_at_sha','error_count','warning_count']},ensure_ascii=False))
    for issue in issues:
        print(json.dumps(issue,ensure_ascii=False))


if __name__ == '__main__':
    main()
