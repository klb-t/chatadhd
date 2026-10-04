#!/usr/bin/env python3
"""Extract four immutable W2 files into this external harness, never the repo."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

W2_COMMIT = '34cc920dd3cdb0c0fca0a514569b19111429583f'
TARGET = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository-root', type=Path,
                        default=Path('/workspace/scratch/943af489e102/chatadhd'))
    repo = parser.parse_args().repository_root.resolve()
    paths = {
        'loom/include/loom/usage_policy.h': 'overlay/include/loom/usage_policy.h',
        'loom/src/policy/usage_policy.cpp': 'overlay/usage_policy.cpp',
        'loom/src/core/config_usage_policy.cpp': 'overlay/config_usage_policy.cpp',
        'loom/src/core/config.cpp': 'overlay/config.cpp',
    }
    receipt = {'w2_branch': 'origin/gpt/usage-policy-2026-10-04', 'w2_commit': W2_COMMIT,
               'repository': str(repo), 'files': []}
    for source, relative in paths.items():
        data = subprocess.check_output(['git', 'show', f'{W2_COMMIT}:{source}'], cwd=repo)
        destination = TARGET / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        receipt['files'].append({'git_path': source, 'local_path': str(destination),
                                'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data)})
    header = subprocess.check_output(['git', 'show', f'{W2_COMMIT}:loom/include/loom/config.h'], cwd=repo)
    receipt['config_header_matches_thread5'] = header == (repo / 'loom/include/loom/config.h').read_bytes()
    receipt['config_header_sha256'] = hashlib.sha256(header).hexdigest()
    if not receipt['config_header_matches_thread5']:
        raise RuntimeError('Config header differs; external assembly needs deliberate compatibility review')
    (TARGET / 'source-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps({'prepared': str(TARGET), 'w2_commit': W2_COMMIT}))


if __name__ == '__main__':
    main()
