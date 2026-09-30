# Independent AnalysisPlan mechanism audit

Protocol recorded before executing the probes. This is a local callback-only
mechanism audit, not measurement of any model or natural-language graph quality.
No network, credentials, paid service, graph application or validation fixture
is used. Owner code is read-only; first results and the reviewed source digest
are retained before requesting corrections.

Hypotheses and criteria:

1. A corrupted completion measurement must not reduce accounted resources while
   its unchanged first-result receipt still passes its own digest check.
2. A returned first output must survive invalid measurement metadata; an invalid
   response may remain uncertain and keep reservations, but must remain auditable.
3. An interrupted completion write must retain the original response if it was
   saved, retain all unknown reservations and prohibit repeating its callback.
4. Independent ledger instances concurrently admitting distinct attempts must
   share one cumulative cap. At most two one-unit attempts may enter under a
   two-unit limit, including when four instances race.
5. Mixed-radix variant decoding must match an independently constructed Cartesian
   product for a small finite test space; unused coordinates must not multiply
   a method's actual callback count.

The first two are adversarial integrity/first-evidence probes. The third and
fourth are failure/concurrency controls. Keep valid behavior, repair concrete
counterexamples before freeze, and retain all first results unchanged.
