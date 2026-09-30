# T9 — export format reference review

Reviewed 2026-09-30 against base `5816b557`, current format reference and
`loom/src/import/export_{openai,anthropic,archive,common,unknown}.cpp`.
[P] Documentation/code review, not inspection of a real account export.
No protected importer files were modified.

## Findings

1. **Preservation and interpretation remain different.** Mapping/current_node,
   branches, unknown content blocks/fields, tool/thought/reasoning records and
   account/project/member records are retained by current code. Counts and
   convenience projections do not establish that every retained field is
   semantically interpreted or rendered. The synthetic JSON-leaf equality
   check is narrower than complete export understanding.
2. **Do not claim an original request from a transcript.** API response fields,
   model context text, custom instructions, memory or tool blocks do not
   recover the exact provider request. `request_provenance=unknown` is correct
   until a separately captured request exists.
3. **Attachment matching needs ambiguity evidence.** The reference's summary
   that id-prefix/content matching "has to" be used overstates a reverse-
   engineered heuristic. Exact IDs/hashes, ambiguous prefixes, unavailable
   bytes and unreferenced members must remain distinct. One prefix match is
   not a vendor-guaranteed identity. Duplicate and unresolved pointer groups
   in the current audit are useful separate outputs.
4. **OpenAI project membership is unresolved.** A `gizmo_id` can describe
   different provider concepts; infer neither a guaranteed project object
   nor its complete membership from the field alone. Preserve original
   values and classify mappings with their evidence source.
5. **Claude attachment wording is too categorical.** Comment in
   `export_anthropic.cpp` says the export carries no file bytes, whereas the
   reference correctly calls this unknown/inferred. Metadata-only fixture
   coverage does not prove absence in all export variants. Preserve supplied
   bytes if present and retain unresolved metadata if absent.
6. **Claude project/memory shapes are hypotheses.** Code supports explicit
   project documents and one memory shape while retaining unrecognised shapes.
   This is appropriate fallback behavior; it does not certify a current
   vendor schema or guarantee a project link on each conversation.
7. **API schemas and third-party parsers are not an export contract.** The
   existing V mark means verified third-party code, not verified real export.
   In particular thinking/tool_use/tool_result/citation schemas from API
   contracts are plausible mappings, not proof of their ZIP representation.
8. **Scope of losslessness needs to remain explicit.** Original sources and
   unknown fields must remain recoverable; decoded JSON reconstruction is not
   byte-identical JSON serialization. The documented lone-surrogate repair
   changes a decoded value and must stay visible, even if original bytes are
   retained. Treat partial/errors/repairs separately from leaf-count equality.
9. **Full import and selective cataloging must be tested separately.** Current
   catalog CLI can select only relevant/candidate units; selection quality
   does not measure source fidelity. `catalog import --mode full` imports all
   catalogued units, but does not by itself establish that scanning found
   every export member or that every unknown structure was interpreted.

## Primary-source check (2026-09-30)

The official [OpenAI export guide](https://help.openai.com/en/articles/7260999-exporting-your-chatgpt-history-and-data)
describes requesting/downloading a ZIP with account data and history; it does
not publish a stable content schema. The official [Claude export guide](https://support.claude.com/en/articles/9450526-export-your-claude-data)
describes conversation/account data and the web/Desktop export flow; it likewise
does not settle member names or attachment-byte completeness. These two pages
were actually opened this review, improving the earlier OpenAI search-hit-only
evidence. No unofficial schema claim was upgraded to a vendor guarantee.

## Only real exports can settle

Keep a hash-only/member/count inventory first, then inspect locally: all archive
members and nested ZIPs, branch graph anomalies/current_node, content-type and
metadata key inventories, project/memory/account records, original attachment
bytes vs pointers, timestamp forms/precision, unknown wrappers/shards and repair
rates. The independent synthetic fixtures remain useful mechanism sentinels.
Use `docs/OWNER_REAL_EXPORT_GUIDE.md` for the first local run; no upload service,
paid model measurement or completeness guarantee is implied by this review.
