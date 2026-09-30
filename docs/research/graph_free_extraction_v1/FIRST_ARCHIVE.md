# First evidence archive: graph_free_extraction_v1

This archive preserves the original development-only request, raw response, ledger,
first prediction/score, audit and frozen replay bytes. It includes the source/code
closure needed for the offline commands below. Archive creation made no API call,
read no sealed validation input and changed no original artifact.

Container SHA256: `5571ab4ff59c85b23bd32f9733df68495155998666203394120a1b99632530ef`. Compressed bytes: 151101.
62 payload files, 24 physical response artifacts;
22/24 completed
requests, known reported usage cost USD 0.0263512,
0 unknown-cost attempts. These are preserved
provider usage measurements, not a new authorization or invoice audit.

`FIRST_EVIDENCE_ARCHIVE.json` records every payload path, byte count and SHA256;
`inventory.json` inside the ZIP records the same payload inventory. Locks, caches,
the archive itself and these new archive documents are excluded. ZIP paths begin
at the repository root. Original loose raw files remain available locally; Git
may preserve these bytes through this canonical ZIP instead of duplicate loose
raw-response paths.

## Restore with zero overwrite

Save this standard-library script as `restore_first_evidence.py`, then run it
with the ZIP, its adjacent receipt and a **new, nonexistent destination**. It
verifies the entire container and all members before creating the destination.
It rejects an existing destination, unsafe paths and symlinks; each payload is
created exclusively. An interrupted restore may leave a partial destination;
choose another new destination after investigating the failure. Do not use a
plain overwriting `unzip` command in an existing checkout.

```python
import hashlib, json, pathlib, stat, sys, zipfile
archive, receipt, destination = map(pathlib.Path, sys.argv[1:])
expected = json.loads(receipt.read_text(encoding='utf-8'))
data = archive.read_bytes()
if len(data) != expected['archive_bytes'] or hashlib.sha256(data).hexdigest() != expected['archive_sha256']:
    raise SystemExit('Archive container hash/size mismatch')
with zipfile.ZipFile(archive) as z:
    names = z.namelist()
    inventory = json.loads(z.read('inventory.json'))
    if len(names) != len(set(names)) or set(names) != set(inventory['files']) | {'inventory.json'}:
        raise SystemExit('Archive member inventory mismatch')
    if inventory['files'] != expected['files']:
        raise SystemExit('External/internal inventory mismatch')
    payloads = {}
    for name, metadata in inventory['files'].items():
        p = pathlib.PurePosixPath(name)
        if p.is_absolute() or '..' in p.parts or str(p) != name or '\\' in name:
            raise SystemExit('Unsafe archive member path')
        mode = z.getinfo(name).external_attr >> 16
        if stat.S_ISLNK(mode):
            raise SystemExit('Symlink archive member forbidden')
        content = z.read(name)
        if len(content) != metadata['bytes'] or hashlib.sha256(content).hexdigest() != metadata['sha256']:
            raise SystemExit('Archive member hash/size mismatch')
        payloads[name] = content
    if destination.exists() or destination.is_symlink():
        raise SystemExit('Destination must be a NEW path; zero overwrite allowed')
    destination.mkdir(parents=True, exist_ok=False)
    for name, content in payloads.items():
        target = destination.joinpath(*pathlib.PurePosixPath(name).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(content)
print('Verified and restored', len(payloads), 'payload files into', destination)
```

```sh
python3 restore_first_evidence.py docs/research/graph_free_extraction_v1/first_evidence.zip docs/research/graph_free_extraction_v1/FIRST_EVIDENCE_ARCHIVE.json /NEW/PATH/restored_repo
cd /NEW/PATH/restored_repo
python3 -m loom.tools.structure.graph_free_extraction score docs/research/graph_free_extraction_v1/prepared/batch01/prepared/manifest.json docs/research/graph_free_extraction_v1/prepared/batch01/run --output replay_free_batch01
python3 -m loom.tools.structure.graph_free_extraction score docs/research/graph_free_extraction_v1/prepared/batch02/prepared/manifest.json docs/research/graph_free_extraction_v1/prepared/batch02/run --output replay_free_batch02
```

These `score` commands replay preserved bytes locally into new output directories.
They neither prepare paid execution nor call any model. Compare replayed JSON
with the archived first-score JSON; keep the first outputs unchanged. Packing
selection remains post-outcome diagnostic DEV with two declared measurements per
cell and no causal/stochastic generalization. Free graph alignment remains a
strict representation-dependent lower bound; source commitments remain unverified
content rather than world facts.
