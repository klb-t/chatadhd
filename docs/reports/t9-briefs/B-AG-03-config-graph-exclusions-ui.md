# B-AG-03 — AGEDS: konfiguracja adresowalna w grafie dowodów, trwałe `never`, teksty i endpoint Androida jako dane

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/AGEDS` @ `9c1d513b`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W3 / P3 |
| Ustalenia | EA-AGEDS-006, EA-AGEDS-007, EA-AGEDS-008 |
| Niezmienniki EITG | G1, G5, G6, G10 |
| Rozmiar / model | M / Sonnet 5.5 high |
| Zależności | B-AG-01, B-AG-02 (profile istnieją — tu są mapowane) |
| Worktree | `t9-ag-03` |

## Zakres
- Dla każdego efektywnego parametru z `flow-map.json` audytu runtime zwraca lokator grafowy,
  wersję i źródło warstwy; eksport konfiguracji zachowuje odniesienia bez sekretów; nie narzuca
  konkretnego backendu ani ontologii ChatADHD (EA-AGEDS-006).
- Wykluczenia wyboru grup: trwały stan `never` osobny od chwilowego odznaczenia; przeżywa restart
  i nowy seed (EA-AGEDS-007; R39 trzy stany `unknown`/`declined`/`never`).
- Polskie napisy UI i fallback endpointu w Kotlinie → katalog komunikatów + ustawienie endpointu;
  komunikat świadomego wysłania i walidacja adresu zachowane (EA-AGEDS-008; EA-AGEDS-012 to
  mechanizm — nie psuć).

## Pliki
- `server/app/db.py:111–145`; `server/app/packages.py` (`export_metadata_package`);
  `server/app/config.py` (`Settings`); `androidApp/src/main/java/dev/klbt/ageds/MainActivity.kt`,
  `CorpusStore.kt`.
- `androidApp/src/main/java/dev/klbt/ageds/CorpusVm.kt:40–55` (`install`, `selectPriority`,
  `togglePhone`, `selectedPhones`); `CorpusStore.kt`.
- `androidApp/src/main/java/dev/klbt/ageds/MainActivity.kt:43–46` i dalsze lokatory z audytu
  (`EvidenceApp`, `PendingPane`, `EvidenceVm.api`).

## Mechanizm docelowy (istniejący)
Graf dowodów (relacje dowodowe, runs, jobs — istnieje) rozszerzony o lokalne encje ustawień/
profili/metod/zamiarów zadań z jawnym mapowaniem; `CorpusStore` jako miejsce trwałego `never`;
katalog komunikatów w zasobach Androida (wzór: Watchdog ma PL/EN dla nawigacji; ChatADHD
`ui.pack#/locales`); koperta `loom.ecosystem_ref/1` dla eksportu, który ma być czytelny z ChatADHD
(most M1 później).

## Kroki
**Rozważenie.** Potwierdzić lokatory; przejrzeć `flow-map.json` audytu dla AGEDS (lista
efektywnych parametrów); rekord decyzji: mapowanie typów jawne, nieznane pola zachowane; nie
mieszać konfiguracji z oryginalnym materiałem dowodowym (ryzyko audytu).

**Implementacja.** (a) test: dla każdego parametru z flow-map runtime zwraca lokator/wersję/warstwę;
eksport zachowuje odniesienia bez sekretów; (b) `never` w `CorpusStore`: test: po wykluczeniu
`forever` restart i nowy seed nie przywracają; chwilowe odznaczenie zachowuje odrębną semantykę;
istniejące chwilowe wybory migrują jako `unknown`, nie jako zgoda/odmowa (ryzyko audytu);
(c) katalog komunikatów + ustawienie endpointu: test: zmiana języka/presetu endpointu z danych
aktualizuje UI i żądania bez buildu; klucze kontraktu stabilne; komunikat świadomego wysłania
zachowany.

**Optymalizacja.** Jeden eksport konfiguracji używany przez pakiet metadanych i widok „co
aplikacja o mnie wie” (gdy powstanie).

**Iteracja.** Wpis `SharedPreferences`/Room/plików AGEDS do `storages.pack` repo (B-ECO-02);
wejście mostu M1.

## Kryterium zamknięcia (EA-AGEDS-006/007/008)
- Trzy testy akceptacyjne z audytu PASS; `pytest` zielony; Android nieuruchamiany — jawnie.
- Eksport nie zawiera sekretów ani identyfikatorów dostawców poza zakresem (ryzyko audytu).

## Czego nie robić
- Nie narzucać ontologii ChatADHD; mapować typy jawnie, zachować nieznane pola.
- Nie uznawać `HTTP/HTTPS` za zakazane dane; nie usuwać walidacji adresu.

## Co zapisać
Rekord decyzji; liczba parametrów z lokatorem / bez; liczba komunikatów per język.
