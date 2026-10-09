# ChatADHD — semantyczne domknięcie 7/2/2

Main: `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9`. Data: 2026-10-09. Przeczytano pełne różnice **11/11 wskazanych plików** (W5:7, W3:2, W4:2) i kontekst zmienionych funkcji. Nie jest to nowy audyt wszystkich plików tych gałęzi.

W 11 ocenionych różnicach brak potwierdzonego zgubienia pierwszego przyrostu. Różne blob OID wynikają z późniejszych integracji, podłączenia profili oraz poprawek. Nie dodawać automatycznie tych 11 plików do backlogu nieprzyjętych prac.

Kompatybilność/migracja paczek stemming/1 pozostaje niezweryfikowana; definicje nowych presetów i zewnętrzne zależności poza zakresem. Nie jest to certyfikat całego main ani R42.

| Grupa | SHA pierwszego przyrostu | Pliki |
|---|---|---:|
| W5 | `5f0abd20dd83e5c0e7f43e84c333d5e23a5e681f` | 7 |
| W3 | `03c670caa6ee3d8ac2c478186548114f8e83927f` | 2 |
| W4 | `b302df25e1a65f20c395eadc5ad5ef065d26e33d` | 2 |

Aktualny `docs/reports/INDEX.md` na powyższym main, linie18–23, opisuje przyjęcie drugich W5/W4/W3; linie45–49 późniejsze odtworzenie osadzonych presetów. Tego opisu nie użyto jako zamiennika analizy różnic. Same dodatnie `git cherry` ani odmienny blob nie dowodzą nieprzyjętej funkcji.

## CHAT-BRSEM-W5-001 — loom/cli/main.cpp

Wynik: **replaced-and-later-main-fix**. Main, linie 62–124, 422–506, 876–894, 963–985, 1182–1292, 1319–1423.

Help, flagi, aliasy, wartości domyślne list, formatowanie i interwały czytają RuntimeProfile. cmd_import wczytuje pełne wartości import-preset/audit-preset, następnie jawne argumenty liczbowe nadpisują preset; walidacja poprzedza import. ArchiveProfile i profil knowledge są ponownie stosowane po nadpisaniach. cmd_profile umożliwia inspect/validate/save i w main kończy się przed Runtime::open.

Handlery poleceń i ścieżka usage admission → import → audit pozostają; usunięte literały/pomoc mają konsumenta profilu. Brak dowodu zgubienia funkcji pierwszego W5 w tych zmianach.

Ograniczenie / ryzyko: Nie otwierano definicji profili ani ich generatorów w tej końcowej próbie; nie stwierdzono równości wszystkich historycznych wartości domyślnych ani kompletności UI. Zakres nie jest ponownym audytem wszystkich pozostałych literałów.

Atrybucja zmian (metadane historii pliku): `66da570d3b5379492e128d940ad474467082c59f` — Integrate streaming archive import, forward-only annotations and OCR checkpoint fixes [skip ci]; `215563c8fddca0187f99f16ce8c2894459dbc53e` — Integrate rebased runtime profile foundation with canonical usage projection and positive source fixtures [skip ci]; `0724b7821d3d7442b8b4a5be982531051088bc43` — Use canonical native import presets and bind semantic cache plus admission identity [skip ci].

## CHAT-BRSEM-W5-002 — loom/include/loom/importer.h

Wynik: **replaced-by-canonical-preset**. Main, linie 98–148.

ImportPresetValues/default_import_preset zastępują początkowe wartości stream_threshold_bytes, json_read_chunk_bytes, json_max_depth, json_inline_threshold_bytes i generic_inference_max_bytes.

Pola pozostają parametrami ImportOptions; domyślne wartości pobiera ta sama konstrukcja opcji, a CLI nadal przyjmuje jawne nadpisania. Nie są nowym niezmiennym limitem.

Ograniczenie / ryzyko: Sprawdzenie samej deklaracji nie dowodzi walidacji dostawcy default_import_preset; dostawca poza 11 plikami.

Atrybucja zmian (metadane historii pliku): `66da570d3b5379492e128d940ad474467082c59f` — Integrate streaming archive import, forward-only annotations and OCR checkpoint fixes [skip ci]; `0724b7821d3d7442b8b4a5be982531051088bc43` — Use canonical native import presets and bind semantic cache plus admission identity [skip ci].

## CHAT-BRSEM-W5-003 — loom/src/import/export_internal.h

Wynik: **replaced-by-canonical-preset**. Main, linie 153–163.

Loader pobiera domyślne chunk_bytes i max_depth z default_import_preset zamiast dwóch literałów.

Parametry konstruktora i dalszy odczyt strumieniowy pozostają. To wymiana źródła domyślnej konfiguracji.

Ograniczenie / ryzyko: Nie uruchamiano parsera; nie porównywano danych presetu poza zakresem.

Atrybucja zmian (metadane historii pliku): `66da570d3b5379492e128d940ad474467082c59f` — Integrate streaming archive import, forward-only annotations and OCR checkpoint fixes [skip ci]; `0724b7821d3d7442b8b4a5be982531051088bc43` — Use canonical native import presets and bind semantic cache plus admission identity [skip ci].

## CHAT-BRSEM-W5-004 — loom/src/import/import_audit.cpp

Wynik: **replaced-by-canonical-preset**. Main, linie 14–76.

Dodano parser kompletnego presetu audytu, kontrolę skończonych liczb i relacji low>=high>0, schema-check źródła compiled_import_preset_source, apply przez zwalidowany obiekt zastępczy oraz inspect z pochodzeniem i hashem.

Pozostały algorytm audytu pierwszego W5 nie został usunięty; nowe funkcje zasilają istniejące opcje. apply nie pozostawia częściowo nadpisanych opcji przy błędzie.

Ograniczenie / ryzyko: Nie przeprowadzono testu z błędnym osadzonym presetem ani porównania starego/nowego wyniku liczbowego.

Atrybucja zmian (metadane historii pliku): `66da570d3b5379492e128d940ad474467082c59f` — Integrate streaming archive import, forward-only annotations and OCR checkpoint fixes [skip ci]; `0724b7821d3d7442b8b4a5be982531051088bc43` — Use canonical native import presets and bind semantic cache plus admission identity [skip ci].

## CHAT-BRSEM-W5-005 — loom/src/import/import_audit.h

Wynik: **replaced-by-canonical-preset**. Main, linie 10–35.

ImportAuditPresetValues i funkcje parse/apply/inspect są nowym źródłem domyślnych active_only/chars_per_token_low/chars_per_token_high/output_ratio.

Opcjonalne ceny wejścia/wyjścia i publiczny interfejs audytu pozostają. Parametry nie zostały utracone.

Ograniczenie / ryzyko: Definicje samego presetu poza zakresem; deklaracja oceniona wraz z import_audit.cpp i cmd_import.

Atrybucja zmian (metadane historii pliku): `66da570d3b5379492e128d940ad474467082c59f` — Integrate streaming archive import, forward-only annotations and OCR checkpoint fixes [skip ci]; `0724b7821d3d7442b8b4a5be982531051088bc43` — Use canonical native import presets and bind semantic cache plus admission identity [skip ci].

## CHAT-BRSEM-W5-006 — loom/src/import/import_usage.cpp

Wynik: **later-main-fix**. Main, linie 45–72.

Dla niehistorycznej projekcji estimate przechowuje import_preset_values oraz identity/projection hash przed request_local_source_usage. Historyczna projekcja zachowuje stary estimate.

Admission pozostaje w tej samej ścieżce; poprawka wiąże zgodę z semantyką importu, nie tylko bajtami wejścia.

Ograniczenie / ryzyko: Implementacja funkcji hash i pełna księga użycia poza zakresem; nie wykonano nowej próby autoryzacji.

Atrybucja zmian (metadane historii pliku): `66da570d3b5379492e128d940ad474467082c59f` — Integrate streaming archive import, forward-only annotations and OCR checkpoint fixes [skip ci]; `0724b7821d3d7442b8b4a5be982531051088bc43` — Use canonical native import presets and bind semantic cache plus admission identity [skip ci].

## CHAT-BRSEM-W5-007 — loom/src/import/importer_core.cpp

Wynik: **later-main-fix**. Main, linie 176–204, 279–305, 321–345.

Cache i resume wymagają zgodności import_projection_hash; jedynie brak pola umożliwia legacy zgodność. Nowe source oraz ZIPmember zachowują hash niehistorycznej projekcji; reuse ZIPmember porównuje pełne metadata.

Resume, źródłowy blob, provenance i kontrole zgodności bajtów z admission pozostają. Stary cache po zmianie semantyki opcji nie jest bezwarunkowo używany.

Ograniczenie / ryzyko: Hash implementation poza zakresem. Zmiana może tworzyć nowy source zamiast ponownie użyć starego; to oczekiwane rozdzielenie tożsamości, wymagające testów migracji/objętości w implementacji.

Atrybucja zmian (metadane historii pliku): `66da570d3b5379492e128d940ad474467082c59f` — Integrate streaming archive import, forward-only annotations and OCR checkpoint fixes [skip ci]; `0724b7821d3d7442b8b4a5be982531051088bc43` — Use canonical native import presets and bind semantic cache plus admission identity [skip ci].

## CHAT-BRSEM-W3-001 — loom/src/chat/chat_engine.cpp

Wynik: **replaced-by-canonical-preset**. Main, linie 838–857, 1011–1024, 1103–1104, 1364–1367.

configure_reasoning usuwa lokalną listę modeli i budżety, stosując reasoning_recipe/apply_reasoning_recipe. send rozwiązuje config chat_reasoning przed wykonaniem, stosuje tę receptę do payload i zachowuje jawny snapshot w metadata żądania oraz odpowiedzi, również przy record_context=false.

Wywołanie konfiguracji reasoning nadal znajduje się w rzeczywistej ścieżce wysyłki. W hunks nie usunięto mechaniki metod/GraphPacket pierwszego W3.

Ograniczenie / ryzyko: Kod resolvera/provider mapping poza tym plikiem nie był w końcowym zakresie. To dowód podłączenia konsumenta, nie dowód kompletności całej strategii ani płatny test.

Atrybucja zmian (metadane historii pliku): `a1be689dc026367783473133de3c25cf5ae60971` — Integrate graph method registry, chat selector and native GraphPacket contract [skip ci]; `4aa9a3b4e75d6a7bf320124d6bd3250a6b35eed9` — Consume configurable goal and chat reasoning snapshots.

## CHAT-BRSEM-W3-002 — loom/src/context/context_engine.cpp

Wynik: **replaced-by-canonical-preset**. Main, linie 369–400, 404–474, 529–552, 708–714.

Wagi wskazówek, confidence bez wskazówki, offset mianownika i fallback goal type/strategy są konsumowane z GoalCuePolicy. Config context_goal_cues oraz nadpisanie execution_scope.goal_typing.goal_cues trafiają przez resolve_runtime_preset do classify_by_cues. Wariant error nie wybiera automatycznie pierwszego typu. Jawny/zmieniony preset zachowuje snapshot i wpływa na ID celu.

Jawny req.goal_type nadal omija klasyfikację; mechanizm cue pozostaje. Poprzedni fallback pierwszego ID jest reprezentowalną operacją first_by_id. Offline preview/warunek autoryzacji nie zostały w tych zmianach usunięte.

Ograniczenie / ryzyko: Walidowane jest dokładnie sześć pól obsługiwanych przez tę operację, nie dowolny język strategii. Nie weryfikowano definicji presetów ani pełnej ścieżki UI; pozostałe niezmienione polityki nie są certyfikowane.

Atrybucja zmian (metadane historii pliku): `a1be689dc026367783473133de3c25cf5ae60971` — Integrate graph method registry, chat selector and native GraphPacket contract [skip ci]; `4aa9a3b4e75d6a7bf320124d6bd3250a6b35eed9` — Consume configurable goal and chat reasoning snapshots.

## CHAT-BRSEM-W4-001 — loom/server/src/app.cpp

Wynik: **later-main-integration-and-fix**. Main, linie 300–311, 461–505, 520–572.

Dodano rejestrację natywnych tras onboarding/method/analysis, media status/transcribe i adapter wspólnego usage-policy. PATCH config przekazuje oryginalne bajty do ordered native reader pod mutexem; PUT wymaga value, zachowuje kolejność ordered_json i obsługuje odmowę native setter.

Trasy grafu i kontekstu nadal są rejestrowane; wcześniejszy backend kontraktu nie został zastąpiony równoległym ledgerem. Zmiana config usuwa utratę kolejności przez parse/dump.

Ograniczenie / ryzyko: Są to punkty rejestracji i adaptery; implementacje wywołanych modułów UI poza 11 plikami. Build bez LOOM_SERVER_HAS_USAGE_POLICY jawnie zwraca unavailable. Nie wykonano HTTP/E2E.

Atrybucja zmian (metadane historii pliku): `a1be689dc026367783473133de3c25cf5ae60971` — Integrate graph method registry, chat selector and native GraphPacket contract [skip ci]; `e8fbb6d356247efe2943ff91cfea5a567f7eb3d3` — Integrate rebased first interface increment preserving native packet routes [skip ci]; `134024de3faf849c6cd05470516916cbe0dd4ad1` — feat(ui): wire native method analysis and onboarding routes [skip ci].

## CHAT-BRSEM-W4-002 — loom/src/kb/pack.cpp

Wynik: **replaced-schema-with-migration-unverified**. Main, linie 68–76, 86–96, 370–408, 1222–1227, 1677–1708.

Walidator stemming wymaga normalization recipe, typów/przekrojów referencji lexicon/fields i skończonych współczynników. min_token/min_stem tracą arbitralną górną granicę32 na rzecz reprezentowalnego size_t. Dispatch schematu zmienia loom.kb.stemming/1 na /2; dodane cross-reference checks obejmują źródła stopwords.

Nie widać usunięcia metody/GraphPacket pierwszego W4; różnica dotyczy późniejszego przeniesienia polityki normalizacji do danych. Zamiana schematu jest realną zmianą kompatybilności, nie równoważnym blobem.

Ograniczenie / ryzyko: Zgodność lub migracja zewnętrznych paczek stemming/1 NIEPOTWIERDZONA: adaptera nie wykazano w ocenianych hunks. Nie nazywać tego udowodnionym brakiem pierwszego W4; osobny dokładny punkt wznowienia to native load/validation historycznej paczki i polityka migracji, bez nadpisywania oryginału.

Atrybucja zmian (metadane historii pliku): `a1be689dc026367783473133de3c25cf5ae60971` — Integrate graph method registry, chat selector and native GraphPacket contract [skip ci]; `00ea92469b02ff74857608759a05d4203f95afb9` — Read native normalization recipe from validated KB pack data [skip ci]; `5d1e7ddd8c5ee625f2d2ce4935d34b5a78204706` — Accept finite normalization cutoffs without preset bounds [skip ci].

## Granice i punkt wznowienia

To analiza statyczna zmian; nie uruchamiano modeli, CI, serwera ani nowych testów produktu. Nie zmieniano produktu, findings ani pozostałych artefaktów. Nie czytano `eval/real-holdout-key`, archive histories, danych prywatnych, ZIP ani request bodies. Dokładne stare i nowe zakresy każdego hunka są w JSON.

Jeżeli potrzebna jest gwarancja migracji, przygotować offline fixture paczki stemming/1 i sprawdzić aktualny native loader/validator + jawny kanał migracji. Nie poszerzać tej analizy do dawnych night/eco ani chronionych źródeł bez osobnego celu.
