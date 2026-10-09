# Task B / C boundary: synthetic credential fixtures

Status: implementation proposal for owner C, not a new owner requirement.
Task B does not edit `loom/tools/structure` or integrate C's branch.
Product source tested: 24c9fc3; observed dev gate: 145/146 (249.35 s).

`loom/tools/structure/test_credential_handoff_v2.py:19` fixes its temporary
root to `/tmp`, ignoring `TMPDIR`. The cloud platform places a protected `.git`
marker there. Existing `credential_handoff.outside_git` correctly rejects a
session anywhere beneath such a marker with `private_path_inside_git`.
All eight HybridHandoffTests consequently fail in setUp before their assertions.
With `TMPDIR=/var/tmp`, the other research fixtures work: the full research
suite executes 1271 cases and only these eight remain errors. Full stdout is
retained; evidence guard rejects the failed entry.

Proposed interface: fixtures that write private-shaped synthetic material honor
Python's configured temporary root (`tempfile.TemporaryDirectory()` without
`dir="/tmp"`). The test environment supplies `TMPDIR` pointing to a permitted
location outside every Git marker. Production path validation, permissions,
expiry, tamper rejection, secret-output checks and WebCrypto execution remain
unchanged. Do not remove the platform marker or allow Git paths.

Acceptance for C: run all eight HybridHandoffTests with an external temporary
root; keep/add the negative test demonstrating that an actual Git-root session
is rejected. Then rerun the full research.structure through CTest and the
repository evidence guard with 10 MiB stdout. A passing isolated fixture alone
does not replace that whole gate. No real credentials or provider requests.

Alternatives: platform supplies a different unmarked `/tmp` (deployment choice),
or the fixtures expose a separate explicit directory argument. Respecting the
standard `TMPDIR` needs no new contract/engine and preserves production policy.
Task B records the proposed handoff here; publication is not confirmation that
C has read or accepted it.
