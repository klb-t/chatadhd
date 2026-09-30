"""One isolated structure follow-up after preserving the full native first run."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

from run_final_native_ctest import ROOT, RECOVERY, CTEST, sha

HERE = Path(__file__).resolve().parent


def save(name, value):
    with (HERE/name).open('x') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())


def main():
    names = ('structure_followup_environment.json', 'structure_followup.log',
             'structure_followup.xml', 'structure_followup_summary.json')
    if any((HERE/name).exists() for name in names):
        raise SystemExit('Existing first follow-up outcome: refusing overwrite')
    first = json.loads((HERE/'final_native_ctest_environment.json').read_text())
    env = first['selected_public_environment']
    native_pins = first['native_source_files_sha256']
    if any(sha(ROOT/name) != expected for name, expected in native_pins.items()):
        raise SystemExit('Native inputs changed; isolated follow-up is insufficient')
    if any(sha(ROOT/name) != expected for name, expected in first['binaries_sha256'].items()):
        raise SystemExit('Native binaries changed; isolated follow-up is insufficient')
    python_pins = {str(path.relative_to(ROOT)): sha(path)
                   for path in sorted((ROOT/'loom/tools/structure').rglob('*.py'))}
    changes = {name: {'before': first['python_code_sha256'].get(name), 'after': digest}
               for name, digest in python_pins.items()
               if first['python_code_sha256'].get(name) != digest}
    command = [str(CTEST), '--test-dir', str(ROOT/'loom/build/dev'),
               '--tests-regex', '^research.structure$', '--output-on-failure',
               '--output-junit', str(HERE/names[2])]
    save(names[0], {'started_at': datetime.now(timezone.utc).isoformat(),
        'source_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'command': command, 'selected_public_environment': env,
        'python_code_sha256': python_pins, 'changes_since_full_native_first': changes,
        'native_source_and_binary_pins_unchanged': True, 'frozen_core_sha256': sha(ROOT/'loom/tools/contracts/analysis_plan_ref.py'),
        'actual_model_tool_api_calls': 0, 'sealed_validation_opened': False})
    started = time.monotonic()
    with (HERE/names[1]).open('x') as log:
        result = subprocess.run(command, cwd=ROOT, env=os.environ | env, stdout=log, stderr=subprocess.STDOUT)
        log.flush()
        os.fsync(log.fileno())
    cases = list(ET.parse(HERE/names[2]).getroot().iter('testcase')) if (HERE/names[2]).exists() else []
    counts = [int(n) for case in cases for n in re.findall(r'Ran (\d+) tests? in ', ''.join(case.itertext()))]
    unchanged = all(sha(ROOT/name) == expected for name, expected in python_pins.items())
    save(names[3], {'exit_code': result.returncode, 'elapsed_seconds': time.monotonic()-started,
        'ctest_cases': len(cases), 'failed': sum(bool(list(case.iter('failure'))) for case in cases),
        'unittest_counts': counts, 'python_pins_unchanged': unchanged,
        'full_native_first_retained': True, 'other_73_native_results_reused': True})
    print((HERE/names[3]).read_text())
    raise SystemExit(result.returncode if unchanged else 2)


if __name__ == '__main__':
    main()
