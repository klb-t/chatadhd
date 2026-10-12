# Briefy T9 dla agentów Sonnet — indeks, szablon, kolejność, wpis dla S1

Napisane 2026-10-12 w sesji S2 (Fable 5.1), gałąź `claude/t9-ecosystem-synthesis`. Dokumenty
nadrzędne: `docs/architecture/ECOSYSTEM_INTEGRATION_2026-10-11.md` (plan, priorytety, cykl) i
`docs/architecture/EVERYTHING_IN_THE_GRAPH_2026-10-11.md` (paradygmat, niezmienniki G1–G10).
Źródło ustaleń: audyt `origin/gpt/ecosystem-audit-2026-10-09` (`findings.jsonl`,
`pass4/findings.jsonl`); lokatory w briefach są z audytu i **każdy brief zaczyna od ich
potwierdzenia na bieżącym SHA**.

## Indeks i kolejność (fale z `ECOSYSTEM_INTEGRATION` §2.9)

| Fala | Brief | Repo | Ustalenia | Rozmiar | Czeka na |
|---|---|---|---|---|---|
| W1 | [B-LEM-01](B-LEM-01-migration-raw-evidence-credentials.md) | LEM-Workbench | LEM-005, LEM-003, A4-LEM-001/002 | S | — |
| W1 | [B-CKP-01](B-CKP-01-late-response-tasks-providers.md) | Custom-Keyboard-Pro | CKP-A-012 (hipoteza najpierw), CKP-A-001/002 | M | — |
| W1 | [B-AG-01](B-AG-01-asr-recipe-job-pin-scan-limits.md) | AGEDS | EA-AGEDS-001/002/003 | M | — |
| W1 | [B-WD-01](B-WD-01-analysis-correctness-provenance.md) | Watchdog-JH16 | WD-002, A4-WD-002 (+ weryfikacja 5 ID) | S | — |
| W1 | [B-ECO-01](B-ECO-01-repos-and-audit-in-graph.md) | chatadhd | 7 repo + audyt jako dane grafu | S | — |
| W1 | [B-CH-01](B-CH-01-startup-defaults.md) | chatadhd | CH-001, domeny bootstrapu, lustro Python | M | T7 |
| W2 | [B-CH-02](B-CH-02-semantic-llm-method.md) | chatadhd | CH-002/003, wątek 1 | L | T2, T6 |
| W2 | [B-CH-03](B-CH-03-ingest-admission-merge.md) | chatadhd | CH-006/010 | M | T2 |
| W2 | [B-WD-02](B-WD-02-generation-routing-context-profiles.md) | Watchdog-JH16 | WD-003/005/009 | M | B-WD-01 |
| W2 | [B-CKP-02](B-CKP-02-planner-suggestions-goalprompt.md) | Custom-Keyboard-Pro | CKP-A-003/005/007 | M | B-CKP-01 |
| W2 | [B-AG-02](B-AG-02-parser-vocab-priority-search.md) | AGEDS | EA-AGEDS-004/005/009 | M | B-AG-01 |
| W2 | [B-LEM-02](B-LEM-02-experiment-config-recipe-registry.md) | LEM-Workbench | LEM-001/002/004 | M | B-LEM-01 |
| W3 | [B-CH-04](B-CH-04-context-renderer-recipe.md) | chatadhd | CH-007, wątek 3 | M | T2 |
| W3 | [B-CH-05](B-CH-05-web-state-perspectives-locale.md) | chatadhd | CH-008/009, A4-CH-VIEW001, A3-WEB-001, A4-E-001, wątek 10 | L | T3, B-ECO-02 |
| W3 | [B-WD-03](B-WD-03-preferences-locale-scheduler.md) | Watchdog-JH16 | WD-004/006/010 | M | B-WD-01 |
| W3 | [B-CKP-03](B-CKP-03-settings-descriptors-capture.md) | Custom-Keyboard-Pro | CKP-A-004/006/011, A4-CKP-001/002 | L | B-CKP-02 |
| W3 | [B-AG-03](B-AG-03-config-graph-exclusions-ui.md) | AGEDS | EA-AGEDS-006/007/008 | M | B-AG-02 |
| W3 | [B-LEM-03](B-LEM-03-ui-theme-graph-description.md) | LEM-Workbench | LEM-006/007 | M | B-LEM-02 |
| W3 | [B-ECO-02](B-ECO-02-second-memory-guard.md) | chatadhd → każde repo | strażnik drugiej pamięci G1/G5 | M | T5 |
| W4 | [B-CH-06](B-CH-06-identity-provenance-hygiene.md) | chatadhd | A4-C-001/002, A2-C-001/004, A3-DISC-001, A4-D-001, A4-B-RES001 | M | T1, T4 |

Repozytoria są rozłączne: fale mówią, co w danym repo jest pierwsze. Cztery repozytoria poza
`chatadhd` mogą iść w osobnych sesjach równolegle z S1; w jednej sesji jeden agent naraz
(`CLAUDE.md`, `THREADS` P0). Rozmiar: S ≤ 5 plików produktu i jeden test; M ≤ 15 plików;
L powyżej albo migracja danych użytkownika.

## Szablon briefu (wszystkie 20 mają ten układ)

1. **Tabela nagłówkowa**: repo i SHA bazy audytu (+ nakaz potwierdzenia na bieżącym `main`),
   fala/priorytet, ID ustaleń, niezmienniki EITG, rozmiar/model, zależności, worktree.
2. **Zakres** — co ma się zmienić dla użytkownika.
3. **Pliki** — lokatory z audytu (`plik:linie`, funkcje, dane, konsumenci, testy/sondy).
4. **Mechanizm docelowy (istniejący)** — nigdy nowy magazyn, resolver ani silnik.
5. **Kroki** w czterech fazach: rozważenie → implementacja → optymalizacja → iteracja
   (`ECOSYSTEM_INTEGRATION` §3.1), z wymuszoną kolejnością implementacji: fixture konsumenta →
   dane z obecnym zachowaniem jako preset bajt w bajt → konsument czyta dane → test A/B na
   przechwyconym żądaniu → `produced_by`/hash w wyniku → allowlista strażnika z kategorią i powodem.
6. **Kryterium zamknięcia** — testy akceptacyjne z audytu + niezmiennik G + brama repo.
7. **Czego nie robić** — ryzyka migracji z audytu.
8. **Co zapisać** — rekord decyzji (`decisions/<ID>.jsonl`, postać `decisions.jsonl` audytu:
   `id`, `finding_ids`, `sha_before`, `sha_after`, `status`, `reason`, `evidence_refs`) i liczby
   z mianownikami.

## Jak sesja orkiestrująca używa briefu

1. Wkleja agentowi (Sonnet 5.5, effort high) treść briefu + `CLAUDE.md` + `AGENTS.md`
   repozytorium docelowego; dla `chatadhd` dodatkowo `ECOSYSTEM_INTEGRATION` §3.2 (zasady).
2. Agent pracuje w worktree o nazwie z briefu, commituje punkty kontrolne; po limicie jest
   wznawiany, nie startowany od nowa.
3. Faza rozważenia kończy się rekordem decyzji **przed** zmianą kodu; jeśli lokator z audytu nie
   zgadza się z bieżącym SHA, agent zapisuje różnicę i status (naprawione / przeniesione /
   zamaskowane / nadal wadliwe / niezweryfikowane) zamiast zgadywać.
4. Przegląd Opus 5.5 przed scaleniem sprawdza: istniejący mechanizm (nie nowy magazyn), preset
   `legacy` bajt w bajt z hashem, test A/B, `produced_by`, brak płatnych wywołań, brak sekretów,
   brama bez osłabień.
5. Status ustaleń audytu zmienia wyłącznie rekord decyzji briefu; B-ECO-01 importuje rekordy do grafu.

## Wpis dla S1 (do wklejenia do `docs/reports/INDEX.md` i `wp-queue`; S2 nie edytuje tych plików — `THREADS` §9)

> **T9 (S2, Fable 5.1, 2026-10-12, gałąź `claude/t9-ecosystem-synthesis`, tylko dokumenty):**
> `docs/architecture/EVERYTHING_IN_THE_GRAPH_2026-10-11.md` — definicja paradygmatu „wszystkie dane
> systemu są w grafie” jako indeksu z pochodzeniem nad magazynami (nie kopia, nie jedna tabela),
> niezmienniki G1–G10 z testami, zgodność z R50/R51, koperta odniesienia `loom.ecosystem_ref/1`
> (= postać lokatorów `findings.jsonl`), mapa ustaleń audytu → łamane niezmienniki, propozycje
> terminów do `LOOM_CONCEPTUAL_MODEL` v1.2.
> `docs/architecture/ECOSYSTEM_INTEGRATION_2026-10-11.md` — synteza audytu 2026-10-09 w osiem
> wzorców, priorytety migracji per repo według wpływu na użytkownika (U1–U5), fale W1–W4, cykl
> rozważenie → implementacja → optymalizacja → iteracja jako metoda z regułą stopu, bramy per repo,
> mosty M1–M5 wyłącznie rozważone, decyzje D-E1…D-E9.
> `docs/reports/t9-briefs/` — 20 briefów dla Sonnet (6 chatadhd, 2 ekosystem, 3 Watchdog,
> 3 IO Matrix, 3 AGEDS, 3 LEM) z lokatorami audytu, mechanizmem istniejącym, czterema fazami,
> kryterium i ryzykami. Zależności od T1–T8: B-CH-01←T7, B-CH-02/03/04←T2, B-CH-05←T3,
> B-CH-06←T1/T4, B-ECO-02←T5; B-ECO-01 i cztery inne repo bez zależności.
> Do T1: A3-IMP-CH004 jako przypadek negatywny, A4-B-RES001 w `catalog/scan.cpp`. Do T3: packi
> jako węzły z rewizją; obszar repozytoriów z B-ECO-01. Do T5: schemat węzła magazynu EITG §6.
> Korekta lokatorów sekcji 4 `THREADS`: audyt jest na `origin/gpt/ecosystem-audit-2026-10-09`
> (`backlog.md` w korzeniu, nie w `pass4/`), `thread-11.md` na `origin/gpt/data-profiles-2026-10-04`.
> 0 płatnych wywołań; 0 agentów; 0 zmian kodu.

## Czego tu nie ma (świadomie)

- Briefów mostów między repozytoriami (M1–M5): tylko rozważenie w `ECOSYSTEM_INTEGRATION` §5;
  briefy po W2, gdy druga strona ma profil.
- Briefów dla ustaleń przypisanych T1–T8 (`ECOSYSTEM_INTEGRATION` §1.5).
- Briefu dla standalone `loom` (zachowany baseline) i `ar-drafty` (prywatne) poza węzłami w B-ECO-01.
- Liczb tokenów: wchodzą do rekordów decyzji po fakcie i stają się danymi metody cyklu (R41).
