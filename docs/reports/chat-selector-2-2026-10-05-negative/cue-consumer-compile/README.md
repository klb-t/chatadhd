# Preserved actual cue consumer fixture compile failure

Archive only; the selected fixture uses an explicit `.get<std::string>()`.
The original test attempted `baseline.type == defaults.at("fallback_goal_type")`
and failed at compile time. Full source/command/diagnostics are preserved;
recorded source SHA matches failed-fixture.cpp. Public header dependencies
are unchanged from product base 0a81480. The private sibling header is frozen.

Replay from a checkout retaining this base:

```sh
python3 docs/reports/chat-selector-2-2026-10-05-negative/cue-consumer-compile/replay.py --repo . --output /tmp/w3-cue-negative-replay
```

The replay relocates original include flags, reconstructs the original private
source path, and succeeds only on the recorded comparison compile failure.
It retains full relocated command/log. No link, provider call or paid call.

## Do wątku N

- **9:** retain on archive; select the corrected fixture on the feature branch.
