# B-CKP-03 — IO Matrix: definicje ustawień jako dane, adresowalne deskryptory przepływów, stabilność przechwycenia

| Pole | Wartość |
|---|---|
| Repo / baza audytu | `klb-t/Custom-Keyboard-Pro` @ `192f820f`; potwierdzić na bieżącym `main` |
| Fala / priorytet | W3 / P3 |
| Ustalenia | CKP-A-004, CKP-A-006, CKP-A-011 (hipoteza), A4-CKP-001, A4-CKP-002 |
| Niezmienniki EITG | G1, G2, G6, G10 |
| Rozmiar / model | L / Sonnet 5.5 high |
| Zależności | B-CKP-01, B-CKP-02; wzór kanonizacji z B-CH-06 (ten sam błąd kolejności kluczy) |
| Worktree | `t9-ckp-03` |

## Zakres
- Zmienne domyślne i metadane prezentacji ustawień (etykiety, zakresy, grupy) w wersjonowanych
  danych; typy mechanizmu i klucze kontraktu zostają w Kotlinie; `SettingsSchema` jako konsument
  (CKP-A-004). Basic/Expert na tych samych wartościach efektywnych.
- Adresowalne deskryptory: ustawienie / zadanie / prompt / kontekst / wykonanie / dowód z wersją
  i pochodzeniem dla prześledzonych ścieżek; sekrety jako odniesienia do keystore urządzenia;
  UI i edycja półautomatyczna mają ten sam identyfikator danych (CKP-A-006). Nie ustanawia
  centralnej bazy (zastrzeżenie audytu).
- `profiles/*.json`: rozstrzygnąć rolę — operacyjny (wtedy zmiana pola zmienia żądanie) albo
  referencyjny (wtedy test nie reklamuje pokrycia runtime) (CKP-A-011).
- Przechwycenie: kanoniczna kolejność map semantycznych w `fingerprint` (A4-CKP-001); eksport
  zawiera efektywny `settleMillis` z receptury (A4-CKP-002).

## Pliki
- `app/src/main/java/com/example/core/config/SettingsSchema.kt:223–253` (`derive`);
  `SettingsStore.kt:29–38`, (`read/resetToDefaults`); `app/src/main/java/com/example/ui/settings/*`.
- `app/src/main/java/com/example/core/ai/AiClient.kt`; `core/assistant/GoalAgent.kt`.
- `profiles/openrouter_inference.json:1–26` i pozostałe `profiles/*.json`; `validate_profiles.py`.
- `app/src/main/java/com/example/core/capture/ConversationCapture.kt:6–24` (`CaptureOptions`),
  `:40–53` (`CaptureFrame.fingerprint`), `:163–196`; `CaptureSession.kt:40–104`.

## Mechanizm docelowy (istniejący)
Kaskada ustawień z pochodzeniem (CKP-A-010); `validate_profiles.py`; rdzeń przechwycenia
zachowujący nieznane fakty i granice `partial/failure/private` (A4-CKP-M001: mechanizm — nie
psuć); koperta `loom.ecosystem_ref/1` dla deskryptorów, które mają być czytelne z ChatADHD (most
M3 później).

## Kroki
**Rozważenie.** Potwierdzić lokatory; zdecydować rolę `profiles/*.json` na podstawie
rzeczywistego konsumenta (albo jego braku) — rekord decyzji; obecne defaulty = preset `legacy`.

**Implementacja.** (a) definicje ustawień + prezentacja do danych; test: zmiana etykiety
i defaultu bez kompilacji; Basic/Expert bez zmiany wartości efektywnych; wykluczenie nie wraca po
odświeżeniu packa; (b) deskryptory: dla kontrolowanej operacji odtworzyć z referencji efektywne
ustawienia, hash promptu, metodę kontekstu, wynik i pochodzenie; bajty sekretu nie trafiają do
przenośnego grafu; (c) `profiles/*.json` — zgodnie z decyzją; (d) kanonizacja map przed
`fingerprint`; `settleMillis` w eksporcie; testy host (6 PASS reprodukcji w pass4 jako punkt wyjścia).

**Optymalizacja.** Jeden schemat definicji ustawień dla UI i `SettingsStore`; brak drugich defaultów.

**Iteracja.** Deskryptory jako wejście mostu M3 (po W2 — tylko rozważenie).

## Kryterium zamknięcia (CKP-A-004, CKP-A-006, CKP-A-011, A4-CKP-001/002)
- Testy akceptacyjne z audytu PASS; pass4 IO Matrix host: przypadki A4-CKP-001/002 PASS.
- Rola `profiles/*.json` udokumentowana i zgodna z testem.
- `validate_profiles.py` PASS; JVM testy zielone albo jawnie niedostępne; retencja prywatnych
  tekstów zgodna z polityką (zastrzeżenie audytu).

## Czego nie robić
- Nie zamieniać istniejących wartości użytkownika na nowe defaulty podczas migracji.
- Nie wynosić typów mechanizmu i kluczy kontraktu z kodu (kategoria 1/3).
- Nie wprowadzać centralnej bazy ani zależności od Loom.

## Co zapisać
Rekord decyzji (rola profili, hash presetu); liczby deskryptorów per rodzaj.
