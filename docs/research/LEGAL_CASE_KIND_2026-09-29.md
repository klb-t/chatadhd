# T11 / R9 — legal_case: proposed data specification

Authored and locally audited **2026-09-30**; filename follows the 2026-09-29
work queue. **[U]** = owner requirement; **[P]** = proposed design;
**[H]** = hypothesis requiring measurement. This document changes no production
pack, schema, rule, C++ code or graph. Every legal norm, jurisdiction, institution,
case, calendar and deadline in its examples is fictional. No real legal period,
service rule, holiday, remedy or procedural entitlement is asserted.

## 1. Existing contract and precise scope

[U] R9 requests parallel proceedings across multiple institutions/codes, immutable
evidence and transcripts, strategy, source-grounded legal-basis search, deadlines
as data, citation/deadline/contradiction checks, and explicit external-data control.
Loom sources, observations, claims, assessments, judgement events and products
remain the only canonical objects; this specification describes graph attributes,
relations and derived projections. It does not introduce a legal-case database.

Read current sources:

- `loom/data/project_kinds/legal_case.json`: `loom.kb.project_kind/1`, version 1,
  candidate, **21 domain kinds**. Inspected SHA-256:
  `719f0d23a753d607fa54328054ef23f5f8e2f2580e4dede7a5166a9b09851e3c`.
- `OWNER_REQUIREMENTS_2026-09-26.md` R9 and
  `LOOM_CONCEPTUAL_MODEL.md` §§1–3, §8, §11: raw source preservation,
  assessment/known-time, fourteen universal roles, competing interpretations.
- `loom/data/rules/inference_rules.json`, `r.deadline_from_delivery`: an existing
  data rule references `date_add(trigger, legal_basis)` and fixes confidence 0.9.
  Its presence is **not a demonstration** of calendar correctness, empirical
  confidence, authority/currentness of a norm or its applicability to a case.
  This task did not audit or run its native evaluator. Do not infer readiness
  for real deadline tracking from this specification.

[P] A future pack extension should keep old IDs, add versioned attributes,
validation profiles and explicit alternative derivations. Enabling it requires
native validation and migration/replay checks; all recipe/projection schemas
below are **research proposals**, not accepted runtime input.

## 2. Domain kinds and graph relationships

The current kinds already cover every universal role. Extend their data slots
instead of inventing a new role or mapping the same domain kind to several roles.

| Current domain kind / role | Proposed attributes and relationships |
|---|---|
| `desired_outcome` / intent | Owner's requested outcome, scope, constraints and alternatives; not a claim that the outcome is available under law. |
| `legal_norm`, `procedural_rule` / constraint | Jurisdiction/code/version/effective interval, authoritative-source snapshot, provision locator, interpretation candidates, applicability checks and deadline recipe refs. |
| `proceeding` / part | Institution-scoped identifier, jurisdiction/code refs, instance/stage, parent/parallel proceeding relations, participating actors, known status claims. |
| `count` / part | Requested relief/allegation/charge as a case component; distinguish this domain term from Loom's epistemic **Claim**. Link propositions, supporting/contrary evidence and unresolved questions. |
| `party`, `institution` / actor | Stable entity refs plus roles qualified by proceeding/time; a person acting as judge remains a person with a role, not automatically identical to the institution. |
| `evidence_item` / resource | Original source ref/hash, acquisition/custody event refs, disclosure restrictions, derivative refs and admissibility questions; matching hashes prove byte identity, not authenticity or truth. |
| `channel` / interface | Institution/action/time-scoped submission route, capability/status checks, authentication requirements and evidence of receipt/dispatch; availability is distinct from legal effectiveness. |
| `procedural_step` / flow | Preconditions, action, actor, input/output artifacts, branch/alternative steps and dependencies; no inferred step becomes mandatory merely because a template includes it. |
| `hearing`, `deadline` / event | Subtype-qualified events; distinguish hearing/service/delivery/dispatch/receipt. Deadlines retain rule, trigger, calendar, alternatives and verification genealogy. |
| `pleading`, `transcript` / artifact | Draft/source/translation/version refs; transcript segments carry time spans, speaker hypotheses and ASR/OCR recipe refs. |
| `drafting` / transformation | Input claims/sources/preferences, recipe/tool identity and output product ref. Drafting and sending/filing are separate actions. |
| `citation_check`, `deadline_check`, `contradiction_check` / check | Versioned check profile, exact inputs/as-of, results, unknowns and evidence refs; verification scope remains explicit. |
| `filed_document` / output | Artifact ref plus separate claims about transmission, receipt, acceptance and reported decision; a generated draft is not a filed document. |
| `strategy_decision` / decision | Selected alternative, rationale, affected values, uncertainties, owner judgement and supersession/reversion events. |
| `legal_question` / question | Missing information, scope, sources searched, candidate interpretations and conditions that would settle it. |

[P] Keep two proceedings distinct even if the same institution, evidence or
person occurs in both. A filing/service event must identify its proceeding and
action. Shared evidence is one source with multiple graph relations, not copied
facts whose repetition creates independent support. `same_as` resolves identity;
`part_of`, `participates_in`, `responds_to`, lineage and cross-proceeding relevance
remain distinct relations with provenance/time qualifiers.

### Fictional parallel-proceeding example

[P] Fictional project X contains `P1` (review at invented Institution A under
Toy Code X) and `P2` (disciplinary inquiry at invented Institution B; its code
and period rules are unknown). One original email E1 is cited in both. E1 has
one source hash, while each support/use relation identifies its own proceeding,
proposition and disclosure scope. The same fictional person can be an applicant
in P1 and a witness in P2 without either role becoming a global identity alias.
Only P1 participates in the toy date arithmetic below; its candidates never
populate P2. P2 remains a partial instance with explicit missing legal-basis,
trigger and applicability information. Neither has an established legal deadline.

## 3. Evidence, source verification and time

[P] Each legally relevant assertion follows this auditable path:

1. Immutable source bytes and acquisition/import event: hash, original locator,
   retention policy, reported provenance, custody events, actual `known_at`.
2. Located observations: exact bytes/JSON pointer, page region or recording
   interval. An OCR/ASR/translation result is a new derivative artifact with its
   own hash and transformation recipe; it never replaces the original.
3. Claims about what the source says; separately, candidate claims about what
   happened or what a legal norm means. A delivery log saying a date is observed
   text; legally effective service on that date is a different assessed claim.
4. Verification events scoped to exact source/version/context/as-of. Citation
   matching, authority, validity interval and applicability have separate results.
5. Derived products and conditional calculations list their actual premises,
   operators/versions, check refs, conflicts, counter-evidence and alternatives.

`source_created_at`, event time, acquisition time, valid interval, `known_at`
and calculation/check time have different meanings. Unknown source know-time
stays unknown; an import date must not be backfilled with the date printed in a
letter. A retrospective run must not masquerade as knowledge available earlier.
New source/check/judgement events create a new projection version; old results
and the date at which a hypothesis was produced remain recoverable.

A custody entry is a claim/event, not proof of an unbroken custody chain. Record
actor, action (obtained/copied/transformed/exported), source/output hashes,
timestamp/uncertainty, predecessor event, tool recipe and supporting observation.
Gaps remain explicit. Distinct copies with the same hash are not independent
witnesses. Redacted disclosures are derivative artifacts with exact disclosure
mapping and access policy, not mutation of the original.

## 4. Citation and applicability checks as data

[P] A `verification_profile` defines operations and criteria, as separate
versioned data: source-byte/quote matching, provision locator, authority/source
identity, jurisdiction, norm effective interval, amendment/version checks,
context scope, cross-reference closure, applicability and unresolved conflicts.
Each criterion produces `pass | fail | unknown | not_applicable` with evidence;
`not_applicable` requires its own reason. A failed network fetch is `unknown`,
not evidence that a citation is absent. Model agreement is instrument output,
not source verification; record conditional ModelProfile/recipe identities.

A source can be authoritative and quoted accurately while its norm is
inapplicable, repealed for the relevant period or overridden by an exception.
The owner may select an interpretation/strategy or a planning assumption;
that decision is not itself an external-law verification event. Both remain
visible and replayable. No global confidence value substitutes for this chain.

Contradiction checks compare scoped propositions (who, event, interval,
modality, legal regime and source), retain both sides and report uncertainty.
Differences between a reported event, hearsay, denial, transcript hypothesis
and legal interpretation are not automatically contradictions. Repeated model
judgements or derivatives of one original do not create additional sources.

## 5. Deadline recipe: required explicit dimensions

[P] A deadline recipe references a norm/version **and** applicability conditions.
A recipe is not enabled merely because `legal_basis` and `trigger` are nonempty.

| Dimension | Required explicit data; missing value behavior |
|---|---|
| Scope | Jurisdiction, code/version, proceeding type/stage/action and effective interval. Unknown applicability leaves the output hypothetical. |
| Trigger | Physical delivery/dispatch/receipt/event claim, service method and separate effective-date rule. Competing trigger claims yield separate scenarios; no silent selection. |
| Period | Amount, unit (calendar/business days, weeks, months, years), basis and exception refs. Unsupported/unknown arithmetic yields no computed date. |
| Start/end | Include/exclude trigger day, end clock time and timezone. A month/year template additionally needs overflow/clamping policy. No inherited country default. |
| Nonworking days | Calendar ID/version/hash, coverage, working-day definitions, exceptional closures, end adjustment and governing basis. Missing coverage prevents a verified shifted end. |
| Completion | Receipt vs dispatch vs accepted submission, channel, cutoff, timezone and evidence requirement. Unknown completion semantics stay unresolved even if arithmetic dates coincide. |
| Exceptions | Suspension/interruption/restart/extension/waiver or special-event hypotheses, each with applicability and source. Empty refs mean none supplied, not proof no exception exists. |
| Temporal knowledge | Source/claim know-times, norm valid interval, pack/calculator versions, prior result and revalidation trigger. Later evidence creates a new version rather than rewriting the old date. |

Calendar definitions are data per legal regime/period, not a single global list.
The runtime may implement universal date/interval operations, but their legal
composition and selection are policy data. DST/ambiguous local time, leap dates,
month overflow, calendar coverage and unsupported operators must have explicit
results. Do not add a calendar silently when only a jurisdiction name is known.

[P] Compute a **scenario set**, including unresolved inputs and a calculation
trace. If premises are missing/unverified/disputed, any date is explicitly
conditional (`mode = hypothetical`, assessed candidate). With no bounded
complete scenario, return no date plus missing-premise requirements. An earliest
value among listed scenarios is not a proved earliest legal deadline when the
scenario space is incomplete. A chosen reminder/planning date is a separate
owner action and never changes the legal assessment.

## 6. Proposed projection contract and fictional bundle

The following JSON Schema draft 2020-12 covers only a proposed deadline recipe,
calendar and hypothetical projection. It deliberately cannot express a verified
legal conclusion. Reference strings are illustrative placeholders resolving to
existing graph IDs in a future adapter, not a new identity/storage mechanism.
Structural validation does not check authority, applicability, arithmetic,
reference existence, source bytes or legal correctness.

The projection's evidence/validation fields are presentation snapshots of the
native assessment, not another assessment store. A future adapter must bind the
actual Claim/Assessment, Expected Property and product-dependency references;
the compact research schema is not a substitute for the full native contract.
Hypothetical date projections never supply observations or premises to later
legal conclusions merely because they passed this schema.

The fictional source texts and pack bytes are printed below; their SHA-256
values are real hashes of those **invented UTF-8 strings**. All sample timestamps
belong to the fictional example, not real acquisition/verification records.
The toy source supports only a reported five-day rule; start/end adjustments,
calendar choice and effectiveness are assumptions, deliberately unverified.

### Proposed schema

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://loom.invalid/research/legal-case-deadline/0",
  "title": "Proposed legal-case recipe/projection; not a native runtime schema",
  "oneOf": [
    {
      "$ref": "#/$defs/rule"
    },
    {
      "$ref": "#/$defs/calendar"
    },
    {
      "$ref": "#/$defs/proposal"
    }
  ],
  "$defs": {
    "source": {
      "type": "object",
      "properties": {
        "source_ref": {
          "type": "string",
          "minLength": 1
        },
        "sha256": {
          "type": "string",
          "pattern": "^[0-9a-f]{64}$"
        },
        "locator": {
          "type": "string",
          "minLength": 1
        },
        "source_created_at": {
          "anyOf": [
            {
              "type": "string",
              "format": "date-time"
            },
            {
              "type": "null"
            }
          ]
        },
        "known_at": {
          "anyOf": [
            {
              "type": "string",
              "format": "date-time"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "source_ref",
        "sha256",
        "locator",
        "source_created_at",
        "known_at"
      ],
      "additionalProperties": false
    },
    "period": {
      "type": "object",
      "properties": {
        "amount": {
          "anyOf": [
            {
              "type": "integer",
              "minimum": 1
            },
            {
              "type": "null"
            }
          ]
        },
        "unit": {
          "enum": [
            "calendar_days",
            "business_days",
            "weeks",
            "months",
            "years",
            "unknown"
          ]
        }
      },
      "required": [
        "amount",
        "unit"
      ],
      "additionalProperties": false
    },
    "rule": {
      "type": "object",
      "properties": {
        "schema": {
          "const": "loom.research.deadline_rule/0"
        },
        "id": {
          "type": "string",
          "minLength": 1
        },
        "version": {
          "type": "integer",
          "minimum": 1
        },
        "jurisdiction_ref": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1
            },
            {
              "type": "null"
            }
          ]
        },
        "code_ref": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1
            },
            {
              "type": "null"
            }
          ]
        },
        "proceeding_kind": {
          "type": "string",
          "minLength": 1
        },
        "action_kind": {
          "type": "string",
          "minLength": 1
        },
        "applicability_predicates": {
          "type": "array",
          "items": {
            "type": "string",
            "minLength": 1
          }
        },
        "trigger": {
          "type": "object",
          "properties": {
            "event_relation": {
              "type": "string",
              "minLength": 1
            },
            "effective_date_rule_ref": {
              "anyOf": [
                {
                  "type": "string",
                  "minLength": 1
                },
                {
                  "type": "null"
                }
              ]
            },
            "start_day": {
              "enum": [
                "exclude",
                "include",
                "unknown"
              ]
            },
            "timezone": {
              "anyOf": [
                {
                  "type": "string",
                  "minLength": 1
                },
                {
                  "type": "null"
                }
              ]
            }
          },
          "required": [
            "event_relation",
            "effective_date_rule_ref",
            "start_day",
            "timezone"
          ],
          "additionalProperties": false
        },
        "period": {
          "$ref": "#/$defs/period"
        },
        "counting": {
          "type": "object",
          "properties": {
            "end_time_local": {
              "anyOf": [
                {
                  "type": "string",
                  "pattern": "^(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]$"
                },
                {
                  "type": "null"
                }
              ]
            },
            "nonworking_end": {
              "enum": [
                "no_shift",
                "following_workday",
                "preceding_workday",
                "unknown"
              ]
            },
            "calendar_ref": {
              "anyOf": [
                {
                  "type": "string",
                  "minLength": 1
                },
                {
                  "type": "null"
                }
              ]
            },
            "month_end": {
              "enum": [
                "same_day_or_last",
                "reject_overflow",
                "unknown"
              ]
            }
          },
          "required": [
            "end_time_local",
            "nonworking_end",
            "calendar_ref",
            "month_end"
          ],
          "additionalProperties": false
        },
        "completion_event": {
          "enum": [
            "receipt",
            "dispatch",
            "accepted_submission",
            "unknown"
          ]
        },
        "exception_rule_refs": {
          "type": "array",
          "items": {
            "type": "string",
            "minLength": 1
          }
        },
        "legal_basis_ref": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1
            },
            {
              "type": "null"
            }
          ]
        },
        "verification": {
          "type": "object",
          "properties": {
            "text_status": {
              "enum": [
                "unknown",
                "unverified",
                "verified_for_snapshot",
                "disputed"
              ]
            },
            "applicability_status": {
              "enum": [
                "unknown",
                "unverified",
                "verified_for_snapshot",
                "disputed"
              ]
            },
            "calendar_status": {
              "enum": [
                "unknown",
                "unverified",
                "verified_for_snapshot",
                "disputed"
              ]
            },
            "check_refs": {
              "type": "array",
              "items": {
                "type": "string",
                "minLength": 1
              }
            }
          },
          "required": [
            "text_status",
            "applicability_status",
            "calendar_status",
            "check_refs"
          ],
          "additionalProperties": false
        },
        "provenance": {
          "$ref": "#/$defs/source"
        },
        "known_at": {
          "anyOf": [
            {
              "type": "string",
              "format": "date-time"
            },
            {
              "type": "null"
            }
          ]
        },
        "pack_hash": {
          "type": "string",
          "pattern": "^[0-9a-f]{64}$"
        }
      },
      "required": [
        "schema",
        "id",
        "version",
        "jurisdiction_ref",
        "code_ref",
        "proceeding_kind",
        "action_kind",
        "applicability_predicates",
        "trigger",
        "period",
        "counting",
        "completion_event",
        "exception_rule_refs",
        "legal_basis_ref",
        "verification",
        "provenance",
        "known_at",
        "pack_hash"
      ],
      "additionalProperties": false
    },
    "calendar": {
      "type": "object",
      "properties": {
        "schema": {
          "const": "loom.research.calendar/0"
        },
        "id": {
          "type": "string",
          "minLength": 1
        },
        "version": {
          "type": "integer",
          "minimum": 1
        },
        "timezone": {
          "type": "string",
          "minLength": 1
        },
        "working_iso_weekdays": {
          "type": "array",
          "uniqueItems": true,
          "items": {
            "type": "integer",
            "minimum": 1,
            "maximum": 7
          }
        },
        "closed_dates": {
          "type": "array",
          "uniqueItems": true,
          "items": {
            "type": "string",
            "format": "date"
          }
        },
        "coverage": {
          "type": "object",
          "properties": {
            "from": {
              "type": "string",
              "format": "date"
            },
            "through": {
              "type": "string",
              "format": "date"
            }
          },
          "required": [
            "from",
            "through"
          ],
          "additionalProperties": false
        },
        "verification_status": {
          "enum": [
            "unknown",
            "unverified",
            "verified_for_snapshot",
            "disputed"
          ]
        },
        "provenance": {
          "$ref": "#/$defs/source"
        },
        "known_at": {
          "anyOf": [
            {
              "type": "string",
              "format": "date-time"
            },
            {
              "type": "null"
            }
          ]
        }
      },
      "required": [
        "schema",
        "id",
        "version",
        "timezone",
        "working_iso_weekdays",
        "closed_dates",
        "coverage",
        "verification_status",
        "provenance",
        "known_at"
      ],
      "additionalProperties": false
    },
    "scenario": {
      "type": "object",
      "properties": {
        "id": {
          "type": "string",
          "minLength": 1
        },
        "trigger_claim_ref": {
          "type": "string",
          "minLength": 1
        },
        "rule_ref": {
          "type": "string",
          "minLength": 1
        },
        "calendar_ref": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1
            },
            {
              "type": "null"
            }
          ]
        },
        "assumptions": {
          "type": "array",
          "minItems": 1,
          "items": {
            "type": "string",
            "minLength": 1
          }
        },
        "due_date": {
          "anyOf": [
            {
              "type": "string",
              "format": "date"
            },
            {
              "type": "null"
            }
          ]
        },
        "calculation_trace": {
          "type": "array",
          "items": {
            "type": "string",
            "minLength": 1
          }
        }
      },
      "required": [
        "id",
        "trigger_claim_ref",
        "rule_ref",
        "calendar_ref",
        "assumptions",
        "due_date",
        "calculation_trace"
      ],
      "additionalProperties": false
    },
    "proposal": {
      "type": "object",
      "properties": {
        "schema": {
          "const": "loom.research.deadline_projection/0"
        },
        "id": {
          "type": "string",
          "minLength": 1
        },
        "proceeding_ref": {
          "type": "string",
          "minLength": 1
        },
        "action_ref": {
          "type": "string",
          "minLength": 1
        },
        "mode": {
          "const": "hypothetical"
        },
        "evidence_class": {
          "const": "inferred"
        },
        "validation_status": {
          "const": "candidate"
        },
        "known_at": {
          "anyOf": [
            {
              "type": "string",
              "format": "date-time"
            },
            {
              "type": "null"
            }
          ]
        },
        "computed_at": {
          "type": "string",
          "format": "date-time"
        },
        "scenarios": {
          "type": "array",
          "minItems": 1,
          "items": {
            "$ref": "#/$defs/scenario"
          }
        },
        "unresolved": {
          "type": "array",
          "minItems": 1,
          "items": {
            "type": "string",
            "minLength": 1
          }
        },
        "review_policy_ref": {
          "type": "string",
          "minLength": 1
        },
        "premise_claim_refs": {
          "type": "array",
          "minItems": 1,
          "items": {
            "type": "string",
            "minLength": 1
          }
        },
        "source_refs": {
          "type": "array",
          "minItems": 1,
          "items": {
            "type": "string",
            "minLength": 1
          }
        },
        "operator_ref": {
          "type": "string",
          "minLength": 1
        },
        "prior_projection_ref": {
          "anyOf": [
            {
              "type": "string",
              "minLength": 1
            },
            {
              "type": "null"
            }
          ]
        },
        "planning_alert": {
          "type": "object",
          "properties": {
            "enabled": {
              "type": "boolean"
            },
            "date": {
              "anyOf": [
                {
                  "type": "string",
                  "format": "date"
                },
                {
                  "type": "null"
                }
              ]
            },
            "meaning": {
              "const": "owner_selected_planning_date_not_verified_legal_due"
            }
          },
          "required": [
            "enabled",
            "date",
            "meaning"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "schema",
        "id",
        "proceeding_ref",
        "action_ref",
        "mode",
        "evidence_class",
        "validation_status",
        "known_at",
        "computed_at",
        "scenarios",
        "unresolved",
        "review_policy_ref",
        "premise_claim_refs",
        "source_refs",
        "operator_ref",
        "prior_projection_ref",
        "planning_alert"
      ],
      "additionalProperties": false
    }
  }
}
```

### Fictional source strings and bindings

```json
{
  "fictional": true,
  "sources": [
    {
      "ref": "source:toy-code-x",
      "utf8": "FICTIONAL TRAINING TEXT. Toy Code X: action R follows delivery by five calendar days. This is an invented exercise, not law.\n"
    },
    {
      "ref": "source:toy-calendar-X",
      "utf8": "FICTIONAL CALENDAR X. Working weekdays: 1,2,3,4,5. Closed date: 2026-09-21. Coverage: 2026-09-01 through 2026-09-30.\n"
    },
    {
      "ref": "source:toy-delivery-note",
      "utf8": "FICTIONAL NOTE. Delivery log A reports 2026-09-14; legal effectiveness is unknown.\n"
    }
  ],
  "pack_utf8": "FICTIONAL PACK X v0: five calendar days; exclude start; alternatives end shift/no shift; no verified applicability.\n",
  "delivery_source": {
    "source_ref": "source:toy-delivery-note",
    "sha256": "122bca069b4936d20f331175a10de0d446f44c167c9ec28ccfaca0333303dc42",
    "locator": "utf8:0..83",
    "source_created_at": "2026-09-14T00:00:00Z",
    "known_at": "2026-09-30T00:00:00Z"
  },
  "reference_bindings": {
    "jurisdiction:fictional-X": "fictional regime",
    "norm:toy-code-X": "fictional code",
    "norm:toy-code-X#fictional-R": "fictional provision",
    "proceeding:toy-P1": "fictional review; no relation to any actual case",
    "action:R": "fictional action",
    "condition:proceeding-is-review": "unverified applicability predicate",
    "condition:delivery-effective": "unverified effectiveness predicate",
    "claim:toy-service-date-A": "observation of fictional delivery-note text, effectiveness unknown",
    "claim:toy-rule-text": "observation of invented source text",
    "claim:toy-calendar-text": "observation of invented calendar text",
    "policy:deadline-review-v0": "proposed check profile; no completed checks",
    "operator:calendar-count-proposal-v0": "proposed universal-arithmetic recipe; not native legal verification"
  }
}
```

### Fictional unverified recipe

```json
{
  "schema": "loom.research.deadline_rule/0",
  "id": "rule:toy-X-R",
  "version": 1,
  "jurisdiction_ref": "jurisdiction:fictional-X",
  "code_ref": "norm:toy-code-X",
  "proceeding_kind": "fictional_review",
  "action_kind": "action:R",
  "applicability_predicates": [
    "condition:proceeding-is-review",
    "condition:delivery-effective"
  ],
  "trigger": {
    "event_relation": "service_event",
    "effective_date_rule_ref": null,
    "start_day": "exclude",
    "timezone": "UTC"
  },
  "period": {
    "amount": 5,
    "unit": "calendar_days"
  },
  "counting": {
    "end_time_local": "23:59:59",
    "nonworking_end": "following_workday",
    "calendar_ref": "calendar:toy-X",
    "month_end": "unknown"
  },
  "completion_event": "unknown",
  "exception_rule_refs": [],
  "legal_basis_ref": "norm:toy-code-X#fictional-R",
  "verification": {
    "text_status": "unverified",
    "applicability_status": "unknown",
    "calendar_status": "unverified",
    "check_refs": []
  },
  "provenance": {
    "source_ref": "source:toy-code-x",
    "sha256": "0f87a7c2b4923d9b87a7142353862fde4e45d963d1ebe1d9b3faeb43dc5ea5e6",
    "locator": "utf8:0..125",
    "source_created_at": "2026-09-01T00:00:00Z",
    "known_at": "2026-09-30T00:00:00Z"
  },
  "known_at": "2026-09-30T00:00:00Z",
  "pack_hash": "b4630c627b6be0174e81d043496accf11a2faf386c00f51ea6a0372ed4b1e63b"
}
```

### Fictional competing recipe: no end shift

```json
{
  "schema": "loom.research.deadline_rule/0",
  "id": "rule:toy-X-R-no-shift",
  "version": 1,
  "jurisdiction_ref": "jurisdiction:fictional-X",
  "code_ref": "norm:toy-code-X",
  "proceeding_kind": "fictional_review",
  "action_kind": "action:R",
  "applicability_predicates": [
    "condition:proceeding-is-review",
    "condition:delivery-effective"
  ],
  "trigger": {
    "event_relation": "service_event",
    "effective_date_rule_ref": null,
    "start_day": "exclude",
    "timezone": "UTC"
  },
  "period": {
    "amount": 5,
    "unit": "calendar_days"
  },
  "counting": {
    "end_time_local": "23:59:59",
    "nonworking_end": "no_shift",
    "calendar_ref": null,
    "month_end": "unknown"
  },
  "completion_event": "unknown",
  "exception_rule_refs": [],
  "legal_basis_ref": "norm:toy-code-X#fictional-R",
  "verification": {
    "text_status": "unverified",
    "applicability_status": "unknown",
    "calendar_status": "unverified",
    "check_refs": []
  },
  "provenance": {
    "source_ref": "source:toy-code-x",
    "sha256": "0f87a7c2b4923d9b87a7142353862fde4e45d963d1ebe1d9b3faeb43dc5ea5e6",
    "locator": "utf8:0..125",
    "source_created_at": "2026-09-01T00:00:00Z",
    "known_at": "2026-09-30T00:00:00Z"
  },
  "known_at": "2026-09-30T00:00:00Z",
  "pack_hash": "b4630c627b6be0174e81d043496accf11a2faf386c00f51ea6a0372ed4b1e63b"
}
```

### Fictional calendar: not a real holiday calendar

```json
{
  "schema": "loom.research.calendar/0",
  "id": "calendar:toy-X",
  "version": 1,
  "timezone": "UTC",
  "working_iso_weekdays": [
    1,
    2,
    3,
    4,
    5
  ],
  "closed_dates": [
    "2026-09-21"
  ],
  "coverage": {
    "from": "2026-09-01",
    "through": "2026-09-30"
  },
  "verification_status": "unverified",
  "provenance": {
    "source_ref": "source:toy-calendar-X",
    "sha256": "2a143a9d6c4788c9067378b0df38dd67143ef455b5d38c2c55bbf926214d24a2",
    "locator": "utf8:0..117",
    "source_created_at": "2026-09-01T00:00:00Z",
    "known_at": "2026-09-30T00:00:00Z"
  },
  "known_at": "2026-09-30T00:00:00Z"
}
```

### Fictional conditional projection

```json
{
  "schema": "loom.research.deadline_projection/0",
  "id": "projection:toy-R-v1",
  "proceeding_ref": "proceeding:toy-P1",
  "action_ref": "action:R",
  "mode": "hypothetical",
  "evidence_class": "inferred",
  "validation_status": "candidate",
  "known_at": "2026-09-30T00:00:00Z",
  "computed_at": "2026-09-30T00:00:00Z",
  "scenarios": [
    {
      "id": "scenario:toy-workday-shift",
      "trigger_claim_ref": "claim:toy-service-date-A",
      "rule_ref": "rule:toy-X-R",
      "calendar_ref": "calendar:toy-X",
      "assumptions": [
        "delivery-on-2026-09-14-effective",
        "exclude-start-day",
        "five-calendar-days",
        "toy-calendar-is-applicable",
        "following-workday-end-rule-applies"
      ],
      "due_date": "2026-09-22",
      "calculation_trace": [
        "start=2026-09-14",
        "counted=15,16,17,18,19",
        "initial_end=2026-09-19",
        "20=nonworking",
        "21=toy-closed",
        "next_workday=2026-09-22"
      ]
    },
    {
      "id": "scenario:toy-no-end-shift",
      "trigger_claim_ref": "claim:toy-service-date-A",
      "rule_ref": "rule:toy-X-R-no-shift",
      "calendar_ref": null,
      "assumptions": [
        "delivery-on-2026-09-14-effective",
        "exclude-start-day",
        "five-calendar-days",
        "no-end-shift-rule-applies"
      ],
      "due_date": "2026-09-19",
      "calculation_trace": [
        "start=2026-09-14",
        "counted=15,16,17,18,19",
        "end=2026-09-19"
      ]
    }
  ],
  "unresolved": [
    "effective_service_not_verified",
    "applicability_not_verified",
    "calendar_not_verified",
    "receipt_or_dispatch_unknown",
    "exception_rules_not_verified"
  ],
  "review_policy_ref": "policy:deadline-review-v0",
  "premise_claim_refs": [
    "claim:toy-service-date-A",
    "claim:toy-rule-text",
    "claim:toy-calendar-text"
  ],
  "source_refs": [
    "source:toy-code-x",
    "source:toy-calendar-X",
    "source:toy-delivery-note"
  ],
  "operator_ref": "operator:calendar-count-proposal-v0",
  "prior_projection_ref": null,
  "planning_alert": {
    "enabled": false,
    "date": null,
    "meaning": "owner_selected_planning_date_not_verified_legal_due"
  }
}
```

The two scenarios have different candidate dates (19 and 22 September) solely
under their listed toy assumptions. No filing deadline is established. This is
a retrospective fictional reconstruction. `planning_alert.enabled = false`;
no reminder, filing, message or external call is performed. An actual service
date dispute would add a separately sourced trigger scenario, not overwrite
`claim:toy-service-date-A`.

## 7. Validation profile and lifecycle

[P] Profile `policy:deadline-review-v0` stores the following independent checks
as data. Their universal executors must advertise availability and version;
missing executors result in unknown/unverified output, not fabricated passes.

| Check | Input and honest outcome |
|---|---|
| Source integrity | Hash/range matches immutable bytes; says nothing about external authenticity. |
| Citation fidelity | Exact observation/provision/version match; legal authority/currentness separate. |
| Applicability | Proceeding/action/jurisdiction/valid interval and exception criteria with sources; unknown/disputed contexts stay separate scenarios. |
| Trigger effectiveness | Delivery/receipt evidence and effective-date interpretation independently assessed. |
| Calendar coverage | Exact calendar version, required dates and governing adjustment basis. |
| Arithmetic | Pure calculation compared to an independent clock oracle for the specified recipe; not legal verification. |
| Completion | Channel/cutoff/receipt-dispatch semantics and evidence; no acceptance inferred from local send completion. |
| Provenance and conflict | Every output depends on traceable claims; missing premises/conflicting interpretations stay visible; copies are not independent support. |
| Privacy/release | Selected source projection, redaction mapping, provider capability and owner authorization; never infer release permission from relevance. |

Use native assessments/judgement events for actual conclusions. A future
verified projection requires explicit source-backed check results and a complete
relevant contract, with no automatic promotion based on model score. Review of
one rule version/context does not validate a different regime/version/action.
A rule amendment, source hash change, trigger correction, calendar update or
new exception invalidates the dependent derived result for re-evaluation;
keep prior claim/projection and why it was superseded.

A submission product lists sources/claims/preferences and cites located text;
unsupported assertions remain questions or marked hypotheses. Transmission
requires a separate authorized action; draft production does not authorize it.
Raw confidential sources retain their existing source policy. The threat-model
workstream owns broader privacy profiles; this document does not claim a new
security implementation.

## 8. Proposed acceptance checks and limits

[P] Native implementation should demonstrate the following before operational
use: source-byte preservation across transforms; citation/version mismatch;
unknown service effectiveness; two proceedings with one shared original;
conflicting delivery dates and code interpretations; absent period/unit;
unknown calendar/coverage; weekend and invented exceptional closure; leap date;
month overflow; ambiguous timezone; dispatch vs receipt; interrupted/restarted
period; later amendment as-of; partial source know-time; old result replay;
owner planning override retaining unknown legal status; model-only candidate
remaining unverified; citation lookup unavailable; no outgoing action from a
draft. Do not copy these toy date answers into an operational quality gate.

[H] A richer legal-case graph may reduce missed dependencies and wrong context
selection. This requires new, independently scored cases; schema validity or
model agreement does not establish legal accuracy. No real case, legal-source
verification, native schema migration, date operator, filing route, OCR/ASR or
external service was evaluated here.

### Local document audit

First execution completed with no failures: **7 structurally valid objects**,
**13 rejected negative mutations**, 3 source hash/exact-range checks, 2 fictional
pack hash checks, 25 resolved reference occurrences, and an independent toy
clock calculation (22 September with the invented end adjustment, 19 September
without it). The schema itself validates. Current 21 domain kinds cover exactly
14 universal roles; the inspected production-file hash is unchanged. Hypothetical
status and unresolved requirements remain present. These are document/mechanism
checks, **not legal verification, native integration tests or measured legal accuracy**.

The audit rejects a negative period, unknown unit, malformed know-time, end
clock `24:00:00`, malformed source hash, weekday 8, duplicate/invalid calendar
dates, a `verified` mode, invalid due date, missing unresolved/premise lists and
an added `is_legally_effective` field. The human applicability/source contract
still requires separate checks; passing JSON Schema cannot establish it.

Reproduce from the repository root (requires the existing `jsonschema` research
dependency; no network/service calls). The following code reads the embedded
examples and performs the same independent audit:

```sh
python - <<'PY'
import copy,datetime,hashlib,json,re
from pathlib import Path
import jsonschema
root=Path.cwd();path=root/'docs/research/LEGAL_CASE_KIND_2026-09-29.md'
blocks=[json.loads(s) for s in re.findall(r'```json\n(.*?)\n```',path.read_text(),re.S)]
schema,bundle,rule,no_shift,calendar,projection=blocks
jsonschema.Draft202012Validator.check_schema(schema)
validator=jsonschema.Draft202012Validator(schema,format_checker=jsonschema.FormatChecker())
for item in [rule,no_shift,calendar,projection]:validator.validate(item)
source_validator=jsonschema.Draft202012Validator({'$defs':schema['$defs'],'$ref':'#/$defs/source'},format_checker=jsonschema.FormatChecker())
for item in [rule['provenance'],calendar['provenance'],bundle['delivery_source']]:source_validator.validate(item)
raw={s['ref']:s['utf8'] for s in bundle['sources']}
for item in [rule['provenance'],calendar['provenance'],bundle['delivery_source']]:
 data=raw[item['source_ref']].encode();assert item['sha256']==hashlib.sha256(data).hexdigest();assert item['locator']==f'utf8:0..{len(data)}'
 assert datetime.datetime.fromisoformat(item['known_at'].replace('Z','+00:00')) >=datetime.datetime.fromisoformat(item['source_created_at'].replace('Z','+00:00'))
for item in [rule,no_shift]:assert item['pack_hash']==hashlib.sha256(bundle['pack_utf8'].encode()).hexdigest()
refs=set(bundle['reference_bindings'])|set(raw)|{rule['id'],no_shift['id'],calendar['id'],projection['id']}|{s['id'] for s in projection['scenarios']}
checkedrefs=[]
def checkrefs(item):
 if isinstance(item,dict):
  for k,v in item.items():
   if k.endswith('_ref') and v is not None:assert v in refs,(k,v);checkedrefs.append(v)
   elif k.endswith('_refs'):
    for ref in v:assert ref in refs,(k,ref);checkedrefs.append(ref)
   else:checkrefs(v)
 elif isinstance(item,list):
  for v in item:checkrefs(v)
for item in [rule,no_shift,calendar,projection]:checkrefs(item)
for item in [rule,no_shift]:
 assert item['action_kind'] in refs
 assert set(item['applicability_predicates'])<=refs
 assert item['verification']['applicability_status']=='unknown'
assert projection['mode']=='hypothetical' and projection['validation_status']=='candidate' and not projection['planning_alert']['enabled']
start=datetime.date(2026,9,14);unshifted=start+datetime.timedelta(days=5);shifted=unshifted
closed={datetime.date.fromisoformat(d) for d in calendar['closed_dates']}
while shifted.isoweekday() not in calendar['working_iso_weekdays'] or shifted in closed:shifted+=datetime.timedelta(days=1)
assert [s['due_date'] for s in projection['scenarios']]==[shifted.isoformat(),unshifted.isoformat()]
for d in [start,unshifted,shifted]:assert datetime.date.fromisoformat(calendar['coverage']['from'])<=d<=datetime.date.fromisoformat(calendar['coverage']['through'])
mutations=[('negative_period',rule,('period','amount'),-1),('bad_period_unit',rule,('period','unit'),'alien'),('bad_known_at',rule,('known_at',),'yesterday'),('bad_end_clock',rule,('counting','end_time_local'),'24:00:00'),('bad_source_hash',rule,('provenance','sha256'),'tampered'),('bad_workday',calendar,('working_iso_weekdays',),[8]),('duplicate_calendar_date',calendar,('closed_dates',),['2026-09-21','2026-09-21']),('bad_calendar_date',calendar,('closed_dates',),['2026-02-30']),('confirmed_mode',projection,('mode',),'verified'),('invalid_due_date',projection,('scenarios',0,'due_date'),'2026-02-30'),('missing_unresolved',projection,('unresolved',),[]),('missing_premises',projection,('premise_claim_refs',),[])]
rejections=[]
for name,base,keys,value in mutations:
 m=copy.deepcopy(base);container=m
 for key in keys[:-1]:container=container[key]
 container[keys[-1]]=value
 assert list(validator.iter_errors(m)),name
 rejections.append(name)
m=copy.deepcopy(projection);m['is_legally_effective']=True;assert list(validator.iter_errors(m));rejections.append('unrequested_legal_certainty_field')
kind=json.loads((root/'loom/data/project_kinds/legal_case.json').read_text())
assert len(kind['domain_kinds'])==21
assert set(k['role'] for k in kind['domain_kinds'])=={'intent','constraint','part','actor','resource','interface','flow','event','artifact','transformation','check','output','decision','question'}
assert hashlib.sha256((root/'loom/data/project_kinds/legal_case.json').read_bytes()).hexdigest()=='719f0d23a753d607fa54328054ef23f5f8e2f2580e4dede7a5166a9b09851e3c'
report={'doc':str(path),'schema_valid':True,'parsed_json_blocks':len(blocks),'schema_valid_positive_objects':7,'rejected_negative_mutations':rejections,'source_hash_and_exact_range_checks':3,'pack_hash_checks':2,'resolved_reference_occurrences':len(checkedrefs),'toy_due_dates':[shifted.isoformat(),unshifted.isoformat()],'unresolved_hypothetical_preserved':True,'domain_kinds':21,'universal_roles':14,'production_changes':False,'legal_accuracy_measured':False,'paid_api_calls':0}
print(json.dumps(report,indent=2))
PY
```
