# ZIP projection preview w istniejącym UI — 2026-11-01

TARGET_ENV_POLICY_APPLIED. Baza `a1623f0a331f97ca3925d909c95e73aeea66469d`
zachowana; kod UI **e0fc62e7527d941132fbc0c2d16243b23c6235a9**. Zmiana nie została wdrożona ani opublikowana.

## Zachowanie

Istniejący panel wywołuje `projection_check` dla ZIP i pokazuje zweryfikowany
manifest archive → folder → file → child projection. Tożsamość i rodzicielstwo
wynikają z ID, locatorów i hashów, nie z nazwy, pozycji lub równej treści.
Widoczne są oryginalne SHA-256, źródła, provenance, projekcje części i ich statusy.
Pobranie oryginału korzysta z istniejącego mechanizmu content-addressed z kontrolą SHA.

ZIP nie ma przycisku potwierdzania ani reprojekcji. Adapter nadal odmawia
`projection_reproject`; kod backendu nie był zmieniany. Root bez tekstowych
wystąpień i nieobsługiwany człon mają jawne `not_exercised`; niepełny lub niespójny
deskryptor jest odrzucany. Puste mapowanie nie oznacza zgodnej migracji.
Ścisły kontrakt odpowiedzi tekstowych pozostaje zachowany. Preview nie jest
naprawą całego grafu, audytem wszystkich tabel ani automatycznym remapowaniem wyboru.

## Wykonane kontrole

- TypeScript + Vite PASS (Vite 21,36 s). Pierwszy typecheck odmówił renderowania
  `unknown` jako ReactNode; poprawiono konwersję etykiety formatu. Pierwszy log zachowany.
- **Jedna zbiorcza bramka browser 1/1 PASS** na własnym serwerze i świeżych danych:
  upload ZIP, materializacja istniejącą ścieżką, realny panel, `a/same.txt` i
  `b/same.txt` o równych bajtach lecz odrębnych ID, zagnieżdżony ZIP z polskim
  tekstem, nieobsługiwany `opaque.bin`, oryginał/download/SHA, reload i zachowana
  globalna selekcja. Native odpowiedź rzeczywiście przeszła walidator i render.
- Wstrzyknięcie niepełnej odpowiedzi HTTP 200 zostało odrzucone przez UI.
  To test mechanizmu frontendowego, **nie nowy dowód odmowy backendu**.
- Podgląd nie wysłał żadnej zmiany konfiguracji ani selektora; lista węzłów przed/po
  identyczna. Osobno rozpoznano dozwolony zapis dziennika układu okien podczas
  nawigacji; reszta konfiguracji i `context_execution` identyczne po reload.
- Pierwsza próba zatrzymała się na zbyt szerokiej asercji całej konfiguracji po
  reload (dziennik okien). Próba kontynuacji w nowej sesji gościa nie znalazła
  poprzedniego źródła. Poprawiono wyłącznie harness, zachowano oba niepowodzenia;
  końcowa próba użyła jednej sesji i nowej syntetycznej fixture. Bez kolejnego buildu UI.
- Archiwum: **106/106 payloadów** i manifest sprawdzone strumieniowo, ścieżki,
  tryby regularnych plików i cele symlinków zgodne. Pierwszy verifier błędnie
  porównał tryb symlinka z trybem jego celu; poprawiono verifier, nie artefakt.
- Izolowany start z dokładnie tych samych bajtów stagingu: health 200, web 200
  z identycznym SHA, auth JSON 200, niezalogowany config 401. Landlock, whitelist
  env, loopback, nowe syntetyczne dane; wszystkie własne procesy zakończone.

## Dokładny kandydat i pochodzenie

| Składnik | Binding |
|---|---|
| Archiwum | `chatadhd-server-linux-x64-e0fc62e7-release-archive-preview-ui-20261101.tar.gz` |
| SHA-256 archiwum | `de0c4930e82ac80e3c0837b735303c0c3413a12d9a02760b25c94178475245dc` |
| Manifest SHA-256 | `457ec107d1cb91eb144e5b7a8012c74024c6eff95f2cb972835bf25be9837bfa` |
| Web index SHA-256 | `eb3414da0aabafb3f1b013b41824efca27b18360d0a7d85a957ddb461815dc08` |
| Server SHA-256 | `ada3e54e89b3a84064fd0774bf0f553e4c8426bae0498e06fe6c8d0f5bd71bab` |
| Źródło serwera | `3d022d3bb8dfe4fe00dd04c1fd3794edbce6092c` |
| Lib SHA-256 (bez zmiany) | `7004d54bd106e281acddd5d352e8ab0e2ced0d710245f8a17c003ccb336e610a` |
| Źródło lib | `e27ce9ff0076c287963a588d942d68e7bebaca3f` |

Serwer `ada3…` pochodzi z odebranego przyrostu API. W tej turze użyto go bez
kompilacji; wcześniejszego testu API nie ponawiano. Biblioteka `7004…` **nie zawiera
nowego ZIP preview** i nie jest nowym odbiorem C ABI/JNI. 89 pozostałych payloadów
runtime/helper jest identycznych z 5db9; obecny web z nowego buildu.
Archiwum ma 38218879 B i niezależne bajty. Staging wykorzystuje hardlinki
wejść, więc sam nie jest immutable kopią buildu; przed ponownym użyciem należy
sprawdzić manifest. Stare archiwa d490/5db9 zachowane, bez relabelingu.

## Ograniczenia i następny krok

Pełny `deploy/verify-package.sh` z dodatkowym rozpakowaniem: **NOT RUN z powodu
capacity**. Pomiar: 1107079168 B wolne; rozpakowanie
100204674 B + bufor 8388608 B
przy progu 1 GiB wymaga dodatkowo **75255938 B**.
Nie usuwano dawnych dowodów/cache ani nie powiększano dysku. Start stagingu nie
jest przedstawiany jako start z nowego pełnego rozpakowania.

Nie wykonywano ZIP migration/mapping, shared-library/JNI preview, starych
API/CSV/AST/P0/kompatybilności/bramek projekcji, pełnego repo gate, modeli,
providerów, vision, CI ani deploy. Historyczne projection/rollback bindings
pozostają historyczne, nie nowym testem tego archiwum.

Next: administrator odbiera dokładny kandydat i brakującą kontrolę rozpakowania.
Dalszy kod to jawny kontrakt tree version/mapping i decyzji o selektorze,
z zachowaniem starych receipts/Units/originals; mutująca migracja poza tą partią.

Dowody prywatne: `.integration-archive-preview-ui-20261101/` — `accepted-ui.json`,
logi build/browser (także failures), `code-files.json`, `release-package.json`,
`archive-verification.json`, `candidate-smoke-result.json`, final manifest i CP.
Nie kopiowano syntetycznych DB, logów ani danych administratora do Git.
