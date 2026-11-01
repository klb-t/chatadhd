"""Package trusted local readers. No downloads, runtime data or user configuration."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def stage(source, destination, python):
    python = python.resolve(strict=True)
    version = subprocess.check_output([str(python), '-I', '-B', '-S', '-c',
        'import sys,ast,resource; assert sys.version_info >= (3,11); print(sys.version.split()[0])'], text=True).strip()
    files = {'worker.py': 'structure_readers/worker.py', 'profile.json': 'structure_readers/profile.json',
             'csv/reader.py': 'csv_structure/reader.py', 'python_ast/reader.py': 'python_ast_basic/reader.py',
             'python_ast/profile.json': 'python_ast_basic/profile.json'}
    destination.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name, origin in files.items():
        target = destination/name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source/origin, target)
        hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
    manifest = {'schema': 'loom.structure_runtime/1', 'python': str(python),
                'python_sha256': hashlib.sha256(python.read_bytes()).hexdigest(),
                'python_version': version, 'files': hashes}
    (destination/'runtime.json').write_text(json.dumps(manifest, indent=2)+'\n')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--destination', type=Path, required=True)
    p.add_argument('--python', type=Path, required=True)
    a = p.parse_args(); stage(a.source, a.destination, a.python)
