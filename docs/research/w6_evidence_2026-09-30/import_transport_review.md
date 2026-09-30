# Independent import-agent review of transport evidence

Reviewed `transport_audit.py`, protocol, amendment 01, report, and attempt-02
saved API transcript, captures, checks and trace baselines. This is an independent
artifact review, not another native execution or an addition to its denominator.

The claimed bounded result is supported: 72/72 recorded checks, eight chat
attempts, six captures, six trace baselines. I independently decoded all six
base64 raw request bodies, reproduced their SHA-256 values and compared their
parsed JSON to the recorded bodies. Five captured message arrays match stored
trace arrays; the sixth corresponds to explicit opt-out. All six trace array
SHA-256 values reproduce from compact UTF-8 JSON. Captures include one HTTP 500
and one request with `stream:true`. The transcript includes one explicit graph
reindex call. No discrepancy found in these saved artifacts.

Methodological boundaries are handled correctly: first attempt's empty
knowledge prompt is retained and called a harness coverage defect; amendment
01 requests explicit native entity targets before attempt 02 and adds nonempty
checks. Fixed fake answers are not interpreted as semantic-quality evidence.
A retained missing-key trace is distinguished from observed provider receipt.
Provider binding and native binding are loopback; proxy variables are removed
for the child and disabled for the client. Dummy secrets are excluded from the
published API transcript and HTTP headers are not captured.

Two limits should remain explicit:

- The history oracle checks two IDs and one exact prior-user occurrence; it
  does not independently assert the prior-assistant occurrence count. The saved
  actual request contains the expected assistant message, so this is a narrow
  future-regression coverage gap, not an observed failure in this run.
- Graph memory is disabled in relevant requests but no populated adversarial
  graph sentinel is independently tested. The report's narrower memory/history
  claims are justified; do not generalize them into proof that every graph
  source-control combination behaves correctly.

Reopen/reindex equality is exercised by the harness against SQLite, while the
public artifacts retain resulting checks and baseline traces rather than a
second complete database snapshot. Accordingly, this review validates the
instrument's method and saved consistency evidence, not crash durability or
an independently rerun persistence test. No production changes requested here.
