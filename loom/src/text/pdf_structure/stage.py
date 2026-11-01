"""Stage immutable PDF projection payload using the explicit DOCX interpreter."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--destination', type=Path, required=True)
    p.add_argument('--python', type=Path, required=True)
    a = p.parse_args()
    python = a.python.resolve(strict=True)
    version = subprocess.check_output([str(python), '-I', '-B', '-S', '-c',
        'import sys,resource; assert sys.version_info >= (3,11); print(sys.version.split()[0])'], text=True).strip()
    a.destination.mkdir(parents=True, exist_ok=True)
    files = {}
    for name in ['projection.py', 'worker.py', 'profile.json']:
        target = a.destination/name
        shutil.copyfile(a.source/name, target)
        files[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    manifest = {'schema': 'loom.pdf_structure.runtime/1', 'files': files,
                'python': str(python), 'python_sha256': hashlib.sha256(python.read_bytes()).hexdigest(),
                'python_version': version}
    (a.destination/'runtime.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    main()
