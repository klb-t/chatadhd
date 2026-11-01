"""Real separate processes, no inherited configuration or secrets."""
import os
import subprocess
import sys

cases = [(None, None, 'on', 'on'), ('1', None, 'on', 'on'),
         ('0', None, 'off', 'on'), ('', None, 'off', 'on'),
         ('typo', None, 'off', 'on'), ('1', '0', 'on', 'off')]
for value, stderr, expected, err_expected in cases:
    env = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8'}
    if value is not None:
        env['LOOM_LOG_ENABLED'] = value
    if stderr is not None:
        env['LOOM_LOG_STDERR'] = stderr
    result = subprocess.run([sys.argv[1], expected, err_expected], env=env,
                            text=True, capture_output=True, timeout=20)
    print(f'startup={value!r} stderr={stderr!r}: {result.stdout.strip()}', flush=True)
    if result.returncode or result.stderr or not result.stdout.startswith('PASS assertions='):
        raise RuntimeError(f'probe exit={result.returncode}; stderr={result.stderr}')
print('PASS 6/6 process cases')
