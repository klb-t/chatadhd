# ChatADHD / Loom: introduction and demo

ChatADHD is a local-first conversation workbench. Loom is its reusable C++20
kernel: it preserves source material, exposes provenance and supports resumable
import, graph and context workflows through a C ABI, CLI and HTTP API.

The useful demonstration is the connection between those layers: a provider
export becomes inspectable native records, the workbench reveals the graph and
supporting evidence, and context preview explains what it would select. The
engineering preview is running software; automatic understanding of arbitrary
archives remains a research question.

## Five-minute walkthrough

Build and start the isolated synthetic demo using the [root README](../README.md#build-and-open-a-synthetic-demo).
Its [fixtures](../loom/tests/fixtures/eval/synthetic_dev/README.md) describe
fictional people and projects. No API key or private archive is needed.

| Step | What to show | What it establishes |
|---|---|---|
| 1. Open **Knowledge** | The completed native run and source catalog | The browser reads results from the C++ pipeline |
| 2. Select a claim or graph node | Evidence class, source locator, support and raw fields in the inspector | Records expose their evidence and derivation |
| 3. Duplicate a graph view | Adjust depth independently; optionally link a parameter | Multiple views can inspect the same native records with separate or linked state |
| 4. Save and restore a perspective | Reload the page and restore the selected layout | Workspace settings survive in this browser's local storage |
| 5. Add a context view | Enter a goal, build a preview, inspect inclusion reasons and dropped items | Context selection produces an inspectable result without sending a model request |

Catalog paths refer to files on the Loom host. Imported sources, source locators
and generated artifacts can contain the full original text; a public demo
should use the supplied fictional corpus in its own data directory.

For a second demonstration, run the Archive Intelligence pipeline from the
repository root. Use another isolated directory:

```bash
ARCHIVE_DEMO="$(mktemp -d)"
./loom/build/dev/cli/loom --data-dir "$ARCHIVE_DEMO/data" archive run \
  --source loom/tests/fixtures/eval/synthetic_dev/chatgpt_export.zip \
  --source loom/tests/fixtures/eval/synthetic_dev/claude_export.zip \
  --out "$ARCHIVE_DEMO/report" --seed NoteFlow --llm off
```

Inspect `MASTER.md`, `source_map.csv`, `timeline.json`, `items.jsonl` and
`gap_report.md` in the output directory. They demonstrate report generation,
traceable source selection and resumable stages. Offline cue-based extraction
can miss implicit decisions or misclassify text; review the source alongside
the generated claims.

## Evidence a visitor can check

The [verification report](verification/current-2026-10-02/RESULTS.md) records 107/107
CTest suite entries, the TypeScript/Vite build and native browser integration.
The [receipt](verification/current-2026-10-02/receipt.json) identifies the measured
source checkpoint and environment. Browser chat tests use a local fake
provider; passing them establishes the integration path, not response quality.

The Python/Kivy client is a retained prototype. The Android bridge has host-side
checks, with real-device validation still outstanding. Recipe efficacy,
arbitrary-export extraction quality and independent semantic evaluation remain
unproved. See [current state](STATE.md) and [limits](LIMITS_AND_WIRING_2026-10-01.md).

## Krótki opis po polsku

ChatADHD to lokalny interfejs do rozmów, pamięci i grafów. Loom to wspólny rdzeń
w C++20, który przechowuje źródła, śledzi pochodzenie danych i wykonuje
wznawialne procesy importu oraz analizy. Działający demonstrator pozwala obejrzeć
graf, sprawdzić źródło twierdzenia i zobaczyć, dlaczego dany fragment trafił do
kontekstu. To wersja rozwojowa dla programistów i testerów; skuteczność rozumienia
dowolnych archiwów nie jest jeszcze udowodniona.

The project is [source-available](../LICENSE) for noncommercial use.
[Commercial use](../COMMERCIAL_LICENSE.md) requires a separate license.
