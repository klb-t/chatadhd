# Integrator9 — local CLI permission failure, 2026-10-04

Source: selected W2 code/data5a73a36 rebased onto Claude/main;
commit6930fd2bb3c01da16f56320ecc48ec2d87d108d4.
First full CTest: **107/108 in281.37s**. Only cli.smoke fails with
PermissionError; the actual linked CLI file has mode0644.
The runtime cause of that local mode change is not established.

Restoring mode0755 leaves all32182400 bytes unchanged, SHA256
758f7624d3646e32023314c06d03fd9153c56220a495ab7292c831828251428a.
No source, test, timeout or assertion is changed. A new complete serial CTest
run is required; individual retries are not combined into a passing full gate.

[Complete original evidence](integrator-usage-permission-2026-10-04/evidence.zip):
ZIP SHA256 `e7f35ede4c4e9a877586d5b5e786b8d1d3d76726c1d05fcc0968e35c5b3e8aa6`, 38538 bytes.
It retains full XML/output/manifest, unchanged guard rejection, actual129-object
build hashes and before/after CLI permission/hash receipt. Zero paid calls.

The failure state can be reproduced on a disposable completed build by setting
its CLI artifact0644 and running cli.smoke/fullCTest; restore0755 afterwards.
This is an archived local-artifact failure, not a demonstrated W2 code defect.

## Do wątku N

- **9:** require executable artifacts and a fresh complete green CTest before
  main admission; preserve this first failure separately from a later pass.
