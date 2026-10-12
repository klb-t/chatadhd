# Projekt obszaru „Magazyn i bezpieczeństwo” (T5a, 2026-10-12)

Realizuje R51 (`OWNER_REQUIREMENTS_2026-09-26.md`), na zasadach R47 (ustawienia w zakresach),
R42 (dane poza kodem) i reguły właściciela z `CLAUDE.md`: **nie decydujemy za użytkownika,
wszystko jest ustawieniem, wartości domyślne to presety, nie reguły**. Jedyna bramka: spodziewany
wzrost zużycia ×10 względem presetu potwierdza użytkownik (jednorazowo; patrz §9, to interpretacja
do zweryfikowania przez właściciela). Wejście: `STORAGE_SECURITY_INVENTORY_2026-10-12.md` (dalej INW).
Dane tego projektu: `loom/data/policy/storage_security.pack` → profil `storage_security`.

## 1. Co użytkownik widzi od razu

Obszar `storage_security` widoku wejściowego (`loom.graph_entry/1`, `loom/data/graph/entry.pack`,
osie: o czym = aplikacja, jaki byt = dane, czas = stan). Pierwszy ekran to jedna tabela, wiersz na kategorię danych:

| Kolumna | Skąd | Dziś (INW) |
|---|---|---|
| Kategoria | `categories.*` | 17 kategorii technicznych + kategorie użytkownika (zdrowie, praca, rodzina…) |
| Gdzie leży | `placement` → węzły magazynów | prawie wszystko: `local_dir` |
| Kto dosięgnie | `storages.*.access` + uprawnienia plików (sonda) | np. `blobs` 444 = każdy lokalny użytkownik; Android: aplikacje z „All files” |
| Czym zaszyfrowane | `encrypt_at_rest` + sonda sygnatury bloba | nic (F1) |
| Kto trzyma klucz | `key_custody` | n/d (hasło tylko w RAM po `unlock`) |
| Rotacja | `rotation` + zdarzenia rotacji | brak |
| Repliki | stan dostępności magazynów | pojedyncza kopia |
| Co wychodzi do dostawców | `egress` | jawny tekst (F9) |

Drugi poziom: karta magazynu, karta klucza, **mapa zagrożeń** (§6) dla bieżącej konfiguracji, dziennik zmian ustawień.
Wartości nieznane wyświetlamy jako `nieznane`, nigdy jako zero (kontrakt `graph_entry`: `unknown`).
Zakres dostarczany w T5a: obszar z czterema licznikami (kategorie, magazyny, zestawy szyfrów, zagrożenia)
przez istniejący dostawca `profile.items` — czysto danowo. Reszta wymaga dostawców z §8.

## 2. Model: kategorie × magazyny (schemat węzła z `EVERYTHING_IN_THE_GRAPH` §6)

**Magazyn** (węzeł): `class` ∈ {`app_server`, `user_cloud`, `local`, `device`, `provider_side`, `repo`, `archive_media`, + `volatile` (RAM)},
`access` (kto), `encryption` {algorytm, odniesienie do opieki nad kluczem, rotacja}, `sync` {reguła, konflikt},
`jurisdiction`, `privacy_profile` (R19), `capabilities` (odczyt/zapis/lista/obserwacja nowych plików, R20), `available_on`.
Klasy to propozycja, nie rejestr zamknięty (nowe klasy = nowe napisy).

**Kategoria danych** (węzeł): `locators` (pliki/klucze z INW), `sensitivity`, `placement` = lista wpisów
`{storage, role: primary|replica|cache|egress, encrypt_at_rest: <suite|none|inherit>}`, `allowed_storages`,
`sync`, `egress`, `key_scope` (klucz per kategoria: pozwala zniszczyć klucz kategorii = crypto-shredding, EC-E10).
Kategorie użytkownika (np. `health`) dodaje się jak każde dane (bez zmiany kodu); wnioskowana kategoria o niskiej pewności dostaje
regułę najbardziej restrykcyjną (EC-E01), pochodne dziedziczą kategorię z domknięcia przesłanek (EC-E06).

**Macierz kategoria × magazyn** jest ustawieniem `/placement/<kategoria>` w zakresach R47. Lokator może wskazywać tylko magazyn
z `allowed_storages` kategorii. Wysyłka do dostawcy to ta sama macierz z magazynem `provider_side`; każde wysłanie jest węzłem
(dostawca, czas, identyfikator klucza, kategorie, hash payloadu), więc pytanie „klucz wyciekł, co poszło?” (EC-E05) jest zapytaniem.

**Domyślne rozmieszczenie = rzeczywistość dziś** (INW §1): każda kategoria techniczna `primary` w `local_dir`, brak replik, `encrypt_at_rest: none`
(`suite: legacy` dostępny, wyłączony), `egress` bez zmian. Po wdrożeniu danych nic się nie zmienia w zachowaniu aplikacji.

## 3. Ustawienia, zakresy i reguły konfliktu (R47, GEP §6)

Łańcuch zakresów z `graph/scopes.pack`: `engine → app_preset → installation → organization → group_of_groups → team → person → role_in_context → project → conversation → turn`.
Ustawienia tego obszaru używają: `app_preset` (domyślne), `installation` (które magazyny istnieją na tym urządzeniu), `organization` (polityka firmy),
`person` (wybór użytkownika), `project`/`conversation` (np. „ta rozmowa nie wychodzi do dostawcy”).

| Ustawienie | Reguła konfliktu | Uzasadnienie |
|---|---|---|
| `/placement/<kat>` (gdzie leży) | `most_specific_wins` | osoba/projekt przenosi swoje dane |
| `/allowed_storages/<kat>` (gdzie wolno) | `intersection` | ograniczają i organizacja, i osoba (GEP §6) |
| `/allowed_storages` ustawione przez organizację z przypięciem | `locked_by` (wymaga uprawnienia, R43; widoczne w wyjaśnieniu) | polityka firmy |
| `/egress/<kat>` (co wolno wysłać) | `intersection`; nadpisanie ponad `locked_by` tylko z inicjatorem `user` i zapisem zdarzenia | EC-E01, EC-E06 |
| `/crypto/selected_preset` | `most_specific_wins` | wybór sposobu dla nowych zapisów |
| `/crypto/params/*` (Argon2id, rotacja) | `max` dla minimów bezpieczeństwa ustawionych przez organizację, inaczej `most_specific_wins` | nie osłabiać polityki firmy po cichu |
| `/sync/<kat>/direction` | `most_specific_wins` | |
| `/sync/<kat>/conflict` | `ask` (preset), alternatywy: `first_defined`, `by_context`, `merge`, `weighted_vote` (kolektyw) | decyzja zapisana jako nadpisanie |
| `/key_custody/<klucz>` | `locked_by` albo `ask` | zmiana opieki to zdarzenie z inicjatorem |
| `/rotation/*` | `min` (okres) | krótszy z zakresów wygrywa |

Reguły są w danych: `settings_conflict_rules` (wzorzec ścieżki → reguła z `scopes.pack`).
Każda wartość efektywna ma wyjaśnienie (zakresy, reguła, inicjator, czas). Operacje R40 (override/disable/exclude/reenable) działają w każdym zakresie.
Konflikt **danych** między replikami (nie ustawień): reguły w `sync_rules` (preset `ask`; `keep_both`, `last_writer_wins`, `vector_clock`, `merge_by_kind`). Brak magazynu
to degradacja, nie blokada: zapis się udaje, jeśli przyjął go co najmniej `write_quorum` magazynów (preset 1), widok pokazuje opóźnione repliki (EC-E07).
Przenoszenie kategorii: kopiuj → weryfikuj hash → dopiero usuń; w każdej chwili ≥ 1 kompletna kopia; brak miejsca = `invariant:external` z liczbą bajtów (EC-E04).

## 4. Kto może czytać co: pokrycie przypadków EC-E

| Przypadek | Jak projekt go obsługuje |
|---|---|
| EC-E01 trzy kategorie, trzy miejsca | trzy osie `placement`, `allowed_storages`, `egress` per kategoria; `intersection` |
| EC-E02 jedna wiadomość, dwie kategorie | kategoria per obserwacja; wiadomość = suma; wysyłka liczona z dozwolonych fragmentów (T5b+T1) |
| EC-E03 podpis na 20 lat | `signatures`: Ed25519 + ML-DSA-65; format niezależny od aplikacji; łańcuch kluczy jako dane; rotacja nie unieważnia starych podpisów |
| EC-E04 przeniesienie | §3 |
| EC-E05 wyciek klucza | węzeł klucza `status: compromised`, rejestr egressu; wymaga dostawcy `egress.ledger` (§8) |
| EC-E07 jeden magazyn offline | §3 kworum |
| EC-E08 tryb paranoiczny | `threat_model` + mapa trzech etykiet (§6) |
| EC-E09 zgubione hasło | tryb `recovery: none` dozwolony; wybór poprzedzony ostrzeżeniem zapisanym jako zdarzenie (treść i data); brak tylnej furtki |
| EC-E10 zapomnienie | klucz kategorii zniszczony (crypto-shredding), hashe zostają |
| „dane w trzech miejscach naraz” | jedna kategoria ma wiele wpisów `placement`; każdy z własnym `encrypt_at_rest`, własnym kluczem (KEK per magazyn), reguły §3 |

## 5. Zestaw kryptograficzny jako ustawienia

Każdy algorytm i parametr jest wpisem rejestru `crypto.algorithms` (id, rodzaj, standard, parametry, status `implemented|planned|legacy_readable`, biblioteka).
Wybór to `crypto.selected_preset` (zakresowe, §3). **Preset domyślny: `legacy`** (dzisiejsze PBKDF2 + AES-GCM) do czasu, aż T5b zaimplementuje i przetestuje prymitywy; `pq_hybrid` jest opisany i zapisany w danych, ale `status: planned`.
Niczego nie wymuszamy: każda pozycja ma `enabled`, a presety użytkownika dodaje się bez zmiany kodu.

| Funkcja | Preset `legacy` (dziś) | Preset `pq_hybrid` (cel) | Standard |
|---|---|---|---|
| Dane (AEAD) | AES-256-GCM, nonce 96 b, bez AAD | AES-256-GCM, klucz obiektu z HKDF-SHA256(DEK kategorii, id obiektu), AAD = (kategoria, id, wersja, suite) | NIST SP 800-38D |
| Hasło → klucz | PBKDF2-HMAC-SHA256, 600 000 iteracji | Argon2id (parametry w presetach poniżej) | RFC 9106 |
| Wymiana kluczy | RSA-OAEP-3072 (`credential_handoff_v2`) | hybryda **X25519 + ML-KEM-768**, kombinator X-Wing (SHA3-256 nad ss_M ‖ ss_X ‖ ct_X ‖ pk_X ‖ etykieta) | FIPS 203; RFC 7748; draft-connolly-cfrg-xwing-kem |
| Podpisy | brak | hybryda **Ed25519 + ML-DSA-65**, oba muszą się zweryfikować (kontener `loom.sig_hybrid/1`; zgodność z draft-ietf-lamps-pq-composite-sigs do sprawdzenia w T5b) | FIPS 204; RFC 8032 |
| Skrót / KDF | SHA-256, HKDF-SHA256 | + SHA3-256 (X-Wing, ML-KEM/ML-DSA) | FIPS 180-4, 202 |

**Dlaczego tak:** AES-256 jest odporny na Grovera (R51); łamliwe są RSA/EC przy wymianie kluczy i podpisach, więc tam hybryda (pewność klasyczna + postkwantowa;
złamanie jednej połówki nie psuje całości). Dla danych w spoczynku postkwantowość zapewnia klucz symetryczny 256 b; ML-KEM dotyczy współdzielenia kluczy między
urządzeniami, serwerem i innymi użytkownikami oraz przekazania poświadczeń (harvest-now-decrypt-later).

**Presety Argon2id** (wszystkie wartości edytowalne; nie wolno ich cicho obniżać przy braku pamięci, tylko zgłosić błąd):

| Preset | t | m | p | Źródło |
|---|---|---|---|---|
| `rfc9106_second` (domyślny w `pq_hybrid`) | 3 | 64 MiB | 4 | RFC 9106 §4, drugi zalecany |
| `rfc9106_first` | 1 | 2 GiB | 4 | RFC 9106 §4, pierwszy zalecany |
| `owasp_min` (urządzenia słabe) | 2 | 19 MiB | 1 | OWASP Password Storage Cheat Sheet |
| `calibrated` | wybierane | wybierane | wybierane | pomiar na urządzeniu do zadanego czasu (ustawienie, bez z góry narzuconego czasu) |

**Koperta v3 (wymiana poświadczeń):** `loom.credential_envelope/3`, algorytm `XWING-X25519-MLKEM768+HKDF-SHA256+A256GCM`.
Pola jak v2 (`schema, algorithm, session_id, purpose, programme_id, expires_at, iv_b64, ciphertext_b64`) z `wrapped_key_b64` zastąpionym przez `kem_ct_b64`
(1120 B = 1088 ML-KEM-768 + 32 X25519) oraz opcjonalnym `signature` (`loom.sig_hybrid/1`). Klucz AES = HKDF-SHA256(ss, info = `loom.credential_envelope/3` ‖ binding), AAD = ten sam
`binding(metadata)` co w v2 (`credential_handoff_v2.py:29-31`). Sesja zawiera klucz publiczny hybrydowy (X25519 32 B + ML-KEM-768 1184 B). **v2 pozostaje czytelne** (dispatch po
`schema`); generacja nowych v2 zależy od ustawienia `handoff.allow_v2` (preset: tak).
Uwaga o przeglądarce: formularz HTML używa WebCrypto, który nie zakłada ML-KEM; T5b musi osadzić w HTML zweryfikowaną implementację (WASM z mlkem-native albo `@noble/post-quantum`, MIT) przy zachowaniu CSP `connect-src 'none'` i bez zależności zewnętrznych.

## 6. Model zagrożeń jako dane

`threat_model.<id>` = {atakujący, zdolności, aktywa, ścieżki (kategorie × magazyny), kontrole, status per ścieżka ∈ `controlled|not_controlled|unknown`, ocena wysiłku}. Mapa ryzyka to przebieg: konfiguracja → lista ścieżek
danych z trzema etykietami (kontrolujemy / nie kontrolujemy / nie wiemy); zmiana jednego ustawienia zmienia tylko ścieżki, przez które przechodzi (EC-E08, MR-6). Wpisy startowe (kalibracja, edytowalne):
utrata urządzenia, inna aplikacja na współdzielonym storage (Android), złośliwe oprogramowanie tego samego użytkownika, operator serwera aplikacji, dostawca chmury, dostawca LLM (egress), podsłuch sieci,
czytelnik zdalnego repo GitHub, wyciek przez logi, klient synchronizacji plików, „zapisz teraz, odszyfruj później” (kwantowy), przyszłe fałszerstwo podpisów, zgubione hasło, wyciek klucza dostawcy, kompromitacja OS (Pegasus: `unknown`).
Stan „dziś” każdego wpisu pochodzi z INW (np. Android: `not_controlled`; logi: `not_controlled`; OS: `unknown`).

## 7. Migracja

| Zasada | Szczegóły |
|---|---|
| Odczyt zawsze po bloku | blob niesie `v`/`suite`; odszyfrowanie dobiera algorytm z bloba, nie z ustawień. `v:1` = dzisiejsze `{v,ct,n,s,i}` (PBKDF2 + AES-GCM) jest czytelne bez końca |
| Zapis wg ustawień | nowe zapisy używają `crypto.selected_preset` w zakresie danych; `legacy` zostaje domyślny do ukończenia T5b |
| Przeszyfrowanie opcjonalne | tryby: `never` (preset), `on_touch`, `background`, `now`; każdy jawny, z podglądem kosztu; ×10 względem presetu = potwierdzenie raz |
| Nie ruszamy formatu DB | `schema_version` zostaje `"4"` (niezmiennik kompatybilności); szyfrowanie dotyczy wartości/blobów, metadane (czy szyfrować) to osobne ustawienie (EC-E09) |
| Koperty | v2 czytelne zawsze; v3 nowe; tabela zgodności w danych |
| Android | dziś bez OpenSSL (F2): T5b dostarcza prymitywy wendorowane (bez OpenSSL) albo wstrzykuje AEAD z Kotlina; bez tego preset `pq_hybrid` na Androidzie ma `available:false` i działa `legacy: unavailable`, bez blokowania aplikacji |

## 8. Dostawcy potrzebni obszarowi (mechanizm w kodzie, nazwy w danych)

`loom.graph_entry/1` pozwala na czysto danowe wiązanie tylko dostawcami istniejącymi (`profile.items`, `data.items`, `db.*`). Pełny widok wymaga nowych dostawców (T5b/T8):

| Dostawca | Zwraca | Dlaczego danowo się nie da |
|---|---|---|
| `storage.probe` | per kategoria: istnieje?, rozmiar, tryb pliku, magazyn faktyczny | trzeba czytać system plików |
| `storage.placement` | efektywne `placement` po złożeniu zakresów + wyjaśnienie | wymaga silnika zakresów (WP-?) |
| `crypto.status` | zestaw aktywny, dostępność prymitywów, wersje bloków w użyciu | wymaga ABI `loom_crypto_status` rozszerzonego o suite |
| `keys.custody` | węzły kluczy, opieka, rotacje, status | magazyn kluczy nie istnieje |
| `egress.ledger` | wysyłki per dostawca/kategoria/czas/hash | rejestr nie istnieje (INW F9) |
| `threats.map` | mapa trzech etykiet z `threat_model` × konfiguracja | przebieg obliczeniowy |

Powiązanie danowe T5a (zrobione): obszar `storage_security` + powiązania `security.categories|storages|suites|threats` → `profile.items` (domena `storage_security`).

## 9. Jak to wyłączyć

| Co | Jak (każdy krok odwracalny) |
|---|---|
| Cały obszar z widoku | nakładka `dir/profiles/graph_entry.pack`: `{"schema":"loom.runtime_profile_overlay/1","domain":"graph_entry","overrides":{"areas":{"storage_security":{"hidden":true}}}}` |
| Wszystkie wartości obszaru do ustawień domyślnych | usuń `dir/profiles/storage_security.pack` |
| Nowa kryptografia | `crypto.selected_preset = "legacy"` (domyślne); pojedynczy algorytm: `crypto.algorithms.<id>.enabled = false` |
| Hybrydę zamiast klasyki lub odwrotnie | `crypto.suite_presets.<nazwa>.kem` / `.signature` ustawione na dowolną kombinację z rejestru (także klasyczną samą) |
| Koperty v3 | `handoff.envelope = "v2"` (v2 zawsze czytelne i zapisywalne) |
| Szyfrowanie danej kategorii | `placement.<kat>[i].encrypt_at_rest = "none"`; blokady czytają stare bloki dalej |
| Replikację / sync | `sync.<kat>.direction = "off"` albo usuń wpis `replica` z `placement` |
| Reguły „co wolno wysłać” | `egress.<kat>.state = "as_today"` (brak ograniczeń, preset) |
| Przeszyfrowanie | `migration.reencrypt = "never"` (domyślne) |
| Bramka ×10 | `guard.x10.mode = "off"` (ustawienie właściciela; domyślnie „zapytaj raz”) |
| Brak odzyskiwania hasła | świadomy wybór `recovery = "none"` po ostrzeżeniu; powrót do `recovery = "escrow"`/`"hint"` dla nowych kluczy |

Wyłączenie nigdy nie usuwa danych ani nie przeszyfrowuje wstecz bez osobnego, jawnego polecenia.

## 10. Wybór bibliotek dla T5b (dlaczego ta, a nie inna)

System ma OpenSSL **3.0.13**: brak ML-KEM, ML-DSA, Argon2 (pojawiają się od 3.5). Android buduje się bez OpenSSL. Zatem wendorujemy zrecenzowane implementacje referencyjne
do `loom/third_party/` (przypięty tag/commit i SHA-256 archiwum zapisane w `README.md` katalogu; licencje i wersje do **potwierdzenia przy pobraniu**, nie zweryfikowano offline):

| Prymityw | Wybór | Licencja (wg stanu wiedzy) | Dlaczego | Alternatywa |
|---|---|---|---|---|
| ML-KEM-768 | **mlkem-native** (pq-code-package) | Apache-2.0 / ISC / MIT | FIPS 203 final, C, szybkie dowody pamięci/typów (CBMC), zestawy KAT/ACVP, używana w produkcji (AWS-LC), prosta integracja, bez zależności | PQClean `mlkem768` (CC0/MIT/Apache; bez dowodów, mniej utrzymywana) |
| ML-DSA-65 | **mldsa-native** (pq-code-package) | Apache-2.0 / ISC / MIT | FIPS 204 final, ten sam projekt i konwencje co powyżej (jedna ścieżka audytu, wspólny Keccak) | PQClean `ml-dsa-65`; liboqs (większa, MIT; ciągnie wiele niepotrzebnych algorytmów) |
| Argon2id | **phc-winner-argon2** (implementacja referencyjna, `ref.c` + Blake2b) | CC0-1.0 / Apache-2.0 | to kod autorów RFC 9106, ma wektory z RFC i `test.c`; bez zależności | Monocypher `crypto_argon2` (CC0/BSD-2; mniejsze, nie referencyjne) |
| X25519, Ed25519 | **Monocypher** | CC0-1.0 / BSD-2-Clause | mały (~2 kLOC), audyt Quarkslab, jedna ścieżka kodu na wszystkich platformach (nie zależy od OpenSSL), wektory RFC 7748/8032 | OpenSSL EVP (desktop; Android niedostępny) |
| AES-256-GCM, SHA-2, HKDF | OpenSSL gdy `LOOM_HAVE_OPENSSL`; inaczej wstrzyknięcie z platformy (Kotlin `javax.crypto`) przez interfejs `CryptoBackend` | OpenSSL: Apache-2.0 | istniejąca ścieżka (`crypto.cpp`); T5b nie wymienia AES-GCM, tylko ukrywa za interfejsem | wendorowany AES-GCM stałoczasowy (osobna decyzja, gdy Android ma potrzebować) |
| SHA3 / Keccak | dostarczany z mlkem-native/mldsa-native (`fips202`) | j.w. | jedna kopia | |

Wymóg wspólny: kod wendorowany bez modyfikacji (łatki tylko jako pliki `.patch` obok), brak `-ffast-math`, budowa w presetach `dev`, `asan`, `vendored`; testy KAT przypięte hashami.

## 11. Brief dla T5b

**Cel:** zaimplementować i przetestować prymitywy oraz zestaw `pq_hybrid`, dodać dostawców §8, zmienić preset domyślny na `pq_hybrid` dopiero po zielonych testach i decyzji właściciela (nic nie jest wymuszone).

**Pliki (nowe):** `loom/third_party/{mlkem-native,mldsa-native,argon2,monocypher}/` + `README.md` (wersja, licencja, SHA-256); `loom/include/loom/crypto_suite.h`;
`loom/src/crypto/{backend.cpp,kdf_argon2.cpp,kem_xwing.cpp,sig_hybrid.cpp,sealed.cpp,envelope.cpp}`; `loom/src/capi/capi_crypto_suite.cpp` (+ wpisy w `loom.h`; test `test_abi_compat.py`);
`loom/tests/test_crypto_{kat,kdf,kem,sig,sealed,envelope}.cpp`; `loom/tests/data/kat/*.json`; `loom/tools/structure/credential_handoff_v3.py` (+ test).
**Pliki (zmiana):** `loom/src/crypto/crypto.cpp` (dispatch po `v`, bez zmiany formatu `v:1`), `loom/data/policy/storage_security.pack` (status `planned` → `implemented`), `loom/data/runtime/storage_security.pack` (regenerowane), dostawcy w `src/graph/graph_entry.cpp`, `docs/STATE.md`.

**Kroki:**
1. Wendoruj i przypnij biblioteki (§10), dodaj do CMake (glob), sprawdź presety `dev|asan|vendored|tsan`.
2. `CryptoBackend` (AES-GCM/SHA-2/HKDF z OpenSSL albo wstrzyknięty); `available()` per prymityw.
3. KAT: ML-KEM-768 (NIST ACVP: keyGen, encapDecap), ML-DSA-65 (ACVP: keyGen, sigGen, sigVer), X25519 (RFC 7748 §5.2, §6.1), Ed25519 (RFC 8032 §7.1), Argon2id (RFC 9106 §5.3), HKDF (RFC 5869), X-Wing (wektory z draftu), AES-256-GCM (SP 800-38D / Wycheproof).
4. Hybrydowe KEM (X-Wing): round-trip, zmiana jednego bajtu `ct` → inny sekret (niejawne odrzucenie ML-KEM), niezgodny klucz → błąd.
5. Podpis hybrydowy: weryfikacja; odrzucenie podrobionej połówki Ed25519 i podrobionej połówki ML-DSA osobno; odrzucenie zmienionej wiadomości i kontekstu.
6. `loom.sealed/2` (AEAD z kluczem obiektu z HKDF, AAD wiązane z kategorią/id) + dispatch odczytu; **stare szyfrogramy `v:1` odczytywane** (fixture z `core/crypto.py`).
7. Koperta v3 (Py+HTML+C++); test round-trip; **v2 nadal deszyfruje** (istniejące `test_credential_handoff_v2.py` zielone); odrzucenie koperty z niezgodnym `binding`.
8. Dostawcy `crypto.status`, `keys.custody` (szkielet), `storage.probe`; wpis zdarzeń rotacji.
9. Ustawienia: wszystkie parametry czytane z profilu `storage_security` (R42, brak stałych w kodzie poza fałszywym fallbackiem testowym); test, że zmiana presetu zmienia tylko nowe zapisy.
10. Aktualizacja `STATE.md`; `gen_runtime_profiles.py --check`.

**Testy zbiorczo:** KAT per prymityw (powyżej) · round-trip hybrydowego KEM · podpis: weryfikacja, odrzucenie podróbki każdej połówki · Argon2id wektory RFC 9106 · koperta v3 round-trip · v2 nadal działa · szyfrogramy legacy nadal działają · ASan/UBSan zielone · dwa przebiegi na różnych platformach dają te same bajty KAT · test kompatybilności `schema_version == "4"` nietknięty.
**Kryteria akceptacji:** (a) wszystkie KAT przechodzą na `dev`, `asan`, `vendored`; (b) żaden istniejący szyfrogram ani koperta v2 nie przestaje się odszyfrowywać; (c) domyślnie nic nie jest zmieniane ani szyfrowane, dopóki użytkownik nie wybierze presetu; (d) każdy parametr da się ustawić nakładką bez rekompilacji; (e) brak sekretów w logach nowego kodu (test grep na wzorcach + test sinka logów); (f) dokument „Jak to wyłączyć” zgodny z faktycznym zachowaniem (test: nakładka `hidden:true` ukrywa obszar); (g) naprawione F3 (kolejność chmod w Py) i F4 (klucz poza URL) albo zgłoszone właścicielowi z powodem.
**Poza zakresem T5b bez decyzji właściciela:** domyślne szyfrowanie `chatadhd.db`, zmiana domyślnych wzorców sync GitHub (F7), zmiana modelu współdzielonego storage Androida (F2).
