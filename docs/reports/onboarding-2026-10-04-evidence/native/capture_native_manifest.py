from pathlib import Path
import hashlib, json, subprocess, datetime
root = Path('/workspace/scratch/4db3ee41b13b/chatadhd')
build = Path('/tmp/onboarding-build-2026-10-04')
inputs = {Path(e['file']) for e in json.loads((build / 'compile_commands.json').read_text())}
for rel in ['loom/include', 'loom/src', 'loom/third_party']:
    inputs.update(p for p in (root / rel).rglob('*') if p.is_file() and p.suffix in ['.h', '.hpp', '.inc'])
for rel in ['loom/data/onboarding', 'loom/data/profiles']:
    inputs.update(p for p in (root / rel).rglob('*') if p.is_file())
inputs.update([root / 'loom/CMakeLists.txt', root / 'loom/server/CMakeLists.txt', root / 'loom/cli/CMakeLists.txt'])
manifest = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(inputs)}
identity = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
out = {
    'captured_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'head_at_capture': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
    'base_head': '30ad7d37337d6641cb7714b03e9feff0e6e25d25',
    'input_count': len(manifest),
    'manifest_sha256': identity,
    'sha256': manifest,
}
Path('/tmp/onboarding-verification-2026-10-04/native-source-hashes-final.json').write_text(json.dumps(out, indent=2) + '\n')
print(json.dumps({k: v for k, v in out.items() if k != 'sha256'}, indent=2))
