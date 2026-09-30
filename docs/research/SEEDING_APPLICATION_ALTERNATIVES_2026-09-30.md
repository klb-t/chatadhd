# T7 application-premise alternatives: protocol before correction outcomes

The independent reviewer supplied a mechanism counterexample: two independent
applications of one operator, justified separately by pA and pB, are currently
aggregated as `{pA, pB}` and required jointly. A target with pA is falsely
rejected despite satisfying the first donor application. This loses alternative
derivation chains and their genealogy.

## Fixed intervention

Preserve each donor decision/operator application independently, with its
principle conjunction, decision id and source locator/date record. Add policy
`capability_mapping = preserve_application_alternatives`: alternatives are OR,
principles *within* one application remain AND. Accept a donor operator if at
least one nonempty application conjunction maps entirely to visible target
principles. Empty/unknown premises do not vacuously qualify. Each candidate
witness records every matching application separately and the mapped premise
edges carry decision/source refs. Retain unmatched applications in provenance
as competing chains; never rewrite them as a single cross-application union.

The historical union summary and `preserve_justifying_principles` policy remain
available for exact reconstruction of v2/v3 behaviour. Default policies are
unchanged. Use all four ranking/baseline methods introduced in the matched
frequency control; candidate support still counts donor **projects**, not
number of applications, so repeated decisions cannot inflate rank evidence.

## Tests and measurements fixed before the run

- Separate donor pA and pB applications: target pA permits only the first,
  target pB only the second, target both permits both; target neither abstains.
- One application requiring pA AND pB: target pA alone still abstains.
- Empty application premises still abstain; legacy union mode reproduces its
  recorded behaviour and remains an explicit comparison.
- Matching witnesses must include donor decision id/source refs. Alternative
  applications remain distinguishable and no target prose is supplied.
- Preserve first outputs in `synthetic_lopo_v4_application_alternatives_first`,
  with frozen protocol/policy/code, predictions saved before scores and identical
  LOPO masks, controls, budgets and seeds. Compare every metric to v3.

The inspected development fixture has no conflicting nonempty premise sets
across repeated applications of the same operator in a donor project; expected
quality counters therefore remain identical. The counterexample tests establish
graph semantics, **not improved model quality**. If any measured score changes,
inspect all changed predictions and retain an explicit competing mode rather
than silently promoting a precision regression. No thresholds or target rules
are tuned. Corrected OR semantics may broaden candidate eligibility on future
data and still needs independent quality evaluation.

Neither union nor OR checks that an operator's natural-language situation is
applicable; broad principle agreement remains insufficient. This correction
preserves alternative derivations, not the missing applicability proof.
