# Wybrany OCR i projekcja PDF — odbiór wspólnego zakresu, 2026-11-01

Przyjęty **backend**: istniejący region OCR do rzeczywistego kontekstu oraz
projekcja tekstu PDF do istniejących Source/Unit/Observation/genesis, read/reopen
i selected context. To zakończony podzbiór fali, **nie odbiór wszystkich readerów**.
Nowy lokalny Release nie został wdrożony. Kanon §0 rewizja 2: dane oddzielone
od inwariantów, oryginał zachowany, inference jawnie oznaczone.

- Baza integratora: `572c9a0d283ba7892772f0ffee863d4de9bebea1`.
- Kod po testach: `36e945fd1e3c165252a6a6d93268be5c41b70455`; runtime tree `40f19694c362ac927cd2ddd045ce184acabaca34`.
- Źródłowa baza obu autorów: `e2f6873c7ca24b41957b36b0127439b926068f41`.
- `fleet-ocr-selected-context-20261101`: product SHA256
  `04534b2f700b36e555b3644f7dad38318c5e88d59c567937250edb0cc24cc772`;
  snapshot `1793513677046088575`.
- `fleet-pdf-structure-20261101`: product SHA256
  `d6d94d8a0ea7d030bb86c77b7ccd2c93fd93c42275e1f2b6e688227561ea0f85`;
  snapshot `1793513870179492915`.

Własny odczyt manifestów/statusu/HEAD/hashów przed i po przeglądzie potwierdził
stabilność. Zastosowano po jednym product patchu. Starych shared hooks nie
nałożono całościowo: PDF hook odmawiał na obecnym source_parts/pack, OCR wymagał
zachowania aktualnego callbacka CSV/AST. Scalono minimalne źródłowe zmiany,
zregenerowano textpack i sprawdzono `--check`. Ani checkpointy, administracyjne
archiwa, manifesty transportowe, ani STATE autorów nie weszły do produktu.

## Zachowanie i granice

OCR nie jest drugim importerem ani nowym silnikiem. Adapter odczytuje istniejące
Source, Unit, Observation i reprezentację OCR przez resolver bieżącego runtime
konta. Weryfikuje oryginał, reprezentację, hash, pointer, method i bounds.
Brak/odmowa/uszkodzenie źródła zatrzymuje cytowanie. Przy zerowym zasięgu grafu
wybór nie dodaje sąsiednich regionów; istniejące review/hops/detail/token/candidate
limits pozostają. Tekst OCR ma `inferred`, confidence `null`. Geometria pochodzi
z zapisanego OCR; nie dowodzi pomiaru rzeczywistego obrazu ani jakości rozpoznania.
Wybór nadal jest globalny dla konta, bez nowego ACL i bez zmiany ABI C.

PDF wykorzystuje istniejącą ekstrakcję Poppler. Oryginalny PDF jest osobnym
immutable blobem, tekst UTF-8 i receipt są pochodnymi. Ordinal `page:N` nie jest
offsetem bajtowym PDF. Zakresy cytatu/genesis są dokładnymi bajtami pochodnego
tekstu; akapity/rozdziały to wnioskowana struktura, nie zaobserwowany layout.
Zachowano podziały stron, puste bloki oraz osobne wystąpienia powtarzanej treści.
Brak obietnicy OCR PDF, pełnego układu, tabel, matematyki lub paginacji wizualnej.

Projekcja używa wspólnego bounded subprocess DOCX: host-owned anonimowy fd,
clean environment, limit wejścia/wyjścia, watchdog/cleanup i limity procesu.
Helper/profile/interpreter są przypięte do instalacji, nigdy do requestu.
Receiptu klienta nie uznaje się za pochodzenie. Read/context ponownie ekstrahuje
ORYGINAŁ i porównuje świeżą pochodną; zmienione artefakty są odrzucane przed
wywołaniem syntetycznego providera. PDF ma osobny manifest, więc **nie zmienia
manifestu i pięciu payloadów przyjętych CSV/AST**. Zmiana przypiętej instalacji
wymaga osobnej migracji; nie ma nowego parser migration registry. Cache projekcji
nie jest kontrolą ani naprawą całego podgrafu. SQL rollback nie gwarantuje
fizycznego usunięcia wszystkich blobów.

Poppler nadal działa w istniejącej ścieżce in-process; nie dowodzimy nowego
sandboxa CPU/wall dla samej ekstrakcji PDF. Wiążemy helper/profile/interpreter,
nie pełne systemowe biblioteki ani niezmierzoną wersję Popplera. Presety
bezpieczeństwa procesu są niezależne od wskazówek 180/600 s, które NIE stały
się timeoutem importu ani SLA.

## Wykonane własne kontrole

| Kontrola | Rzeczywisty wynik |
|---|---|
| Jeden incremental Release core/server `-j2` | PASS po poprawkach dotkniętych TU, bez monolitu/CLI/kopii cache |
| Wspólne CTest `-j1`: OCR6 + PDF5 + sześć dotkniętych sentinelów unified/G1/DOCX | Pierwszy przebieg **16/17 PASS**, 296/297 asercji; PDF read FAIL zachowany |
| Po poprawce porównania receiptu tylko dotknięte PDF read/corrupt-receipt | **2/2 PASS**, 32/32 asercje na końcowym core; nie relabelujemy wcześniejszego przebiegu jako nowego 17/17 |
| Mały sentinel wspólnego transportu CSV i AST | Materialize/read/reopen PASS; manifest + pięć payloadów byte-identical z bazą |
| HTTP wybranego OCR i PDF | **2/2 PASS**, 12,767 s: faktyczny system context + ID, brak niepowiązanych fragmentów, restart, original/hash, odmowy brakującego oryginału i uszkodzonego receiptu bez requestu providera |
| ABI C | **126** eksportów bez zmian względem przyjętej rzeczywistej lib |
| Generator / diff | `loom/src/text/gen_text_pack.py --check`, `git diff --check` PASS |
| Nowy pakiet `deploy/verify-package.sh` | PASS: checksum, startup/health/static/auth na własnym loopback; istniejący wariant BusyBox tar |
| Relokowany rzeczywisty pakiet | **34/34** payloady zgodne; domyślny packaged PDF helper, upload/materialize/read PL PASS przy niedostępnym checkout i pierwotnym stage |

Nie sumujemy powtórzonego testu do liczby unikalnych przypadków. Testy natywne
obejmują odmowy brakujących/odrzuconych źródeł, locator tamper, resealed malformed
OCR, budżety/review oraz PDF graph/provenance/idempotence/invalid/output bound.
Świadomie nie powtórzono autorskich 47 OCR, 22 PDF, 19 AST, 15 CSV ani
historycznych pełnych P0/share/accounts/backup/G1GUI. **NOT RUN**: nowy GUI,
mobile/JNI, realny model/provider, pełna bramka repo, rollback nowych projekcji,
wdrożenie. Historyczne PASS pozostają historyczne.

Zachowane błędy: pierwsza kompilacja wykazała brak `input_for` po adaptacji
transportu; przywrócono sprawdzenie spójności wejścia/wyjścia. Pierwszy native
PDF read ujawnił porównanie ordered JSON zależne od kolejności kluczy po zapisie
canonical JSON. Naprawa używa canonical equality bez zmiany treści, typów czy
hashów; dodatkowe asercje diagnozują konkretną granicę. Jedna reguła Landlock
`read-dir` na zwykłym pliku zwróciła EINVAL przed startem próby; poprawiono ją
na `read-file`. Końcowy błędny path generatora (`loom/tools/...`) dał ENOENT;
poprawny istniejący `loom/src/text/... --check` przeszedł. Logów nie usunięto.
Historyczne GNU tar ENOSYS nie jest PASS i nie było ponawiane.

Syntetyczne mechanizmy: istniejący PNG z polskim tekstem + lokalny OCR,
`documents/unicode-compressed.pdf` (dwie strony PL), minimalne mechanizmowe
JSON/źródła oraz pojedyncze świeże CSV/AST. Dostawca to własny loopback;
żadnych cudzych danych, zewnętrznych API ani prawdziwego modelu.
Landlock/env whitelist i zamknięcie własnych serwerów stosowano w bramkach.

Główne polecenia (pełne argv/env zachowane w lokalnych command receipts):

```sh
cmake --build .integration-release-20261031/build --target loom loom-server -j2
cmake --install .integration-release-20261031/build --component runtime --prefix NEW_STAGE
cmake -S loom/tests/pdf_structure -B OWN_FOCUSED_BUILD -G Ninja -DLOOM_SOURCE=ABS_LOOM -DLOOM_CORE_BUILD=OWN_RELEASE_BUILD
ctest --test-dir OWN_FOCUSED_BUILD -j1 -V
# po rzeczywistej poprawce tylko dwie dotknięte cases, następnie nowe HTTP2
python3 .integration-fleet-batch-20261101/run-affected.py
python3 .integration-fleet-batch-20261101/run-http.py
python3 loom/src/text/gen_text_pack.py --check
```

Nie uruchamiać ponownie tych wrapperów na istniejących syntetycznych rootach.
Przed buildem wolne 1 440 436 224 B, RAM dostępny ~13,6 GiB; nie było innego
ciężkiego procesu. Poprzedni właściciel slotu terminal/cleanup + bieżąca zgoda
administratora pozwoliły na jedną kompilację. Faktyczna faza build zaczęła się
07:03 UTC, binding po testach 07:15 UTC; poprawki TU i diagnostyka należą do
tej fazy. To pomiar fragmentu integracji, nie czas onboardingu autorów ani
przelicznik 180/600. Przyspieszenie: jeden wspólny target i tylko affected retest.

## Nowy lokalny artefakt

- Nazwa: `chatadhd-server-linux-x64-36e945fd-release-ocr-pdf-20261101.tar.gz`.
- SHA256: `f9767a0c20e628f942f8abda9b24daebab5fe9fb23666ae70324f132840754fb`.
- MANIFEST SHA256: `1e12d21ee32cab888beb4df0ce98ea2864656a1b37a6415138764a8b46711048`.
- server SHA256: `444b69b166a2039164ea284c8ca80b2d651fcc7c4433455b0e223d3011b370f7`.
- lib SHA256: `0c2052d8bd31214876a5690f05394db0e4f4ca862c7d882dfe7e6f6f2202e4c5`.
- 16 payloadów web zachowano byte-identical; web source `ccada5e5c2542252112fe40caa95d4b407048e80`, bez nowego webbuild/GUI.
- Poprzedni pakiet `42cf960c0f89d55be91ad5175ff0b0d082a5ca1216168c0fc5a9d88329f2f61d` zachowany bez zmian; nie relabelowany.

Runtime wymaga dotychczasowych bibliotek/systemowego Pythona, Poppler-GLib,
Tesseract dla istniejącego OCR; helper PDF wymaga `resource`/Linux/glibc.
Manifest wiąże 36 plików zmiany i faktyczne binaria; nie oznacza przenośności
na inny host. Pakiet bez DB, konfiguracji użytkowników i prywatnych danych.
Obecny `install.sh --from-tarball` pozostaje kontraktem operatora; brak drugiego
deployera. Produkcyjny backup/restore i zgoda na deploy pozostają poza tym odbiorem.

## Pozostałe moduły — jawnie nieprzyjęty zakres

Odczytano i zabezpieczono dziewięć stabilnych przekazań. Połączenie gotowych
OCR/PDF jest obecnym zakresem; pozostałe propozycje wymagają dalszej implementacji
integratora, a nie powtórzenia testów autora. Nie jest to twierdzenie, że ogólny
trusted host nie istnieje — istnieje od przyjętego CSV/AST i musi zostać użyty.
Brak dowodu native nie stanowi PASS. Cała fala pozostaje nieukończona.

| Moduł | Product SHA256 | Konkretne next, nienałożone |
|---|---|---|
| `odt-reader` | `53407512629be083873df8c368aa3a4635c8603b0609ae737f368baa5e160867` | Połączyć wieloźródłową projekcję, nested Units i replay z istniejącym hostem/native persistence. |
| `epub-reader` | `351e26839174b61b6bb4502becac56bb046036c091179f41527b1e17114184d8` | Dostosować dziesięć dawnych shared hooks do obecnego transportu; chapter/attachment/re-extraction, bez nadpisania przyjętych plików. |
| `xlsx-reader` | `e43243b6673a4000f449e2495df919dd6c084d82d2b03dfbbdc8706c8ea9b9e6` | Native sheet/cell/range i derived Sources; Python resource_graph nie jest Loom binding. Minimalnie poprawić wskazany EOF whitespace przy odbiorze. |
| `selection-extent` | `a944f72dc44cfd1e1283afa84aed38e4df8d09bb4eff2d223ee40d9a3bf8cf16` | Sprawdzić rzeczywistego konsumenta native; Python validator nie tworzy nowego ABI ani uwierzytelnionego provenance. |
| `video-helper` | `30978008148ec953515c28dc94447db4c5dbb6973425ae37c9e58b9ba42d381d` | Pinning procesu i blobów; helper nie stanowi importera/persistence. |
| `video-source-plan` | `1b016107394338578c04aaca81b56a4be0c59cb4e24657934b9aebdf1c39648a` | Powiązać returned BlobStore hashes/transaction/idempotence/HTTP; zachować has_ocr=false i ordinal przy equal PTS. |
| `export-assets-bundle` | `86fda0c217e8430827d7dbba4e29f0d6b91e0b002bae4ffcbb00f391d9c3c5a8` | Native source-message-pointer→targetID, rehash/reopen, jawny partial restore; callback test nie dowodzi transakcji. |

`fleet-unknown-structure-20261101`: konkretny odczyt
`/var/lib/codex-team/worktrees/fleet-unknown-structure-4a82c0c-20261101/CHECKPOINT.md`
zwrocił **EACCES errno 13**. Bez kolejnych prób/obejścia, bez przyjęcia descriptora
lub legacy XLS. Stan completed sam nie zastępuje czytelnego przekazania.

Prywatny lokalny indeks dowodów: `.integration-fleet-batch-20261101/`:
`intake.json`, `module-status.json`, `runtime-binding.json`, `attempts.json`,
`native-tests.log`, `affected-results.json`, `http-result.json`,
`release-package.json`, `verify-package-result.json`, `package-helper-result.json`,
`final-result.json` i `manifest.json`. Nie importowano tych transportów do produktu.
