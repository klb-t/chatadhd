# Lossless raw measurement packaging

The original 53 member pins and `SOURCE_PACKAGE_FREEZE.json` are unchanged.
Seven raw fixture/measurement files totaling 8,984,217 bytes remain in the local
working tree and are covered by one exact-byte ZIP archive for integration.
The staging manifest excludes those duplicate raw files and includes the archive,
complete member SHA256/size inventory, restoration receipt, code, protocols,
reports and original freeze. No benchmark was rerun and no original was deleted.

The ZIP has deterministic metadata: UTF8 lexical member order, fixed 1980 epoch,
regular-file mode 100644, empty comments/extra fields, DEFLATE level 9. Two builds
in the recorded Python/zlib runtime must yield identical archive bytes. The member
inventory records original exact bytes including formatting/newlines, not
reformatted JSON. Its archive hash and original source-package hash are explicit;
the eventual Git commit supplies the external anchor.

Verification restores every member into a fresh temporary root and compares its
exact SHA256 and byte count. Existing-file, partial-destination, corrupted-archive,
altered-member-hash and traversal-name controls must reject before any write.
Existing directories can be used only by explicit option, and all target files
are preflighted before writing with exclusive creation. Existing targets always
reject. A trusted local filesystem/runtime is assumed; there is no claim of a
hostile-process filesystem sandbox. A partial unexpected I/O failure is surfaced,
not repaired by deleting caller files.

To restore archived inputs after a fresh checkout into the existing repo without
overwriting any tracked or locally present raw file:

```bash
python -m loom.tools.structure.agentic_graph_cache_v1.package_archive restore \
  --archive docs/research/agentic_graph_cache_v1/raw_measurements_v1.zip \
  --inventory docs/research/agentic_graph_cache_v1/RAW_ARCHIVE_INVENTORY.json \
  --destination . --allow-existing-root
```

If the originals are already present, that command intentionally rejects and
changes nothing. For independent verification choose a nonexistent temporary
root and omit `--allow-existing-root`. `STAGING_MANIFEST.json` is the exact
integration list; its own hash is reported outside itself to avoid a recursive
digest. The 53-original-file freeze remains historical evidence even when seven
members are materialized from the archive rather than staged separately.
