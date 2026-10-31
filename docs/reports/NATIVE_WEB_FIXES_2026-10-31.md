# ChatADHD — poprawki jądra i interfejsu, 2026-10-31

Baza: `pkg-webfix`, commit `910dca317e1f35a01102edb95c372bc85a6cd9aa`. Zmiany są przygotowane do przeglądu; nie są potwierdzonym wdrożeniem.

Naprawiono wybór języka etykiet i menu okien, formatowanie komunikatów we wszystkich językach katalogu, sortowanie zgodne z językiem oraz nazwy/aliasy pulpitu z literami Unicode. Uzupełniono 69 tekstów nl w słownikach stylów, układów, sprzężeń i struktury projektu. Nowe teksty nl są tłumaczeniem maszynowym Codexa, bez przeglądu człowieka. Testy sprawdzają także parametry tekstów, nowe katalogi i zachowanie danych wejściowych.

Geneza tekstu obsługuje starszą wiadomość bez grupy wersji i zachowuje wszystkie zdarzenia na wspólnej krawędzi w `metadata.events`, także przy powtórzonym fragmencie lub współdzieleniu fragmentów między rozmowami. Źródłowe wiadomości nie są zmieniane. Struktura projektu zachowuje poprawne wyniki obok błędnych metadanych i pokazuje częściowy odczyt. Naprawiono kolizję znanej frazy pulpitu z czasownikiem innego polecenia.

Operacje kosza wymagają statusu `completed`, zanim interfejs pokaże sukces. Panel logów i ustawienia zgłaszają błędy odczytu/zapisu zamiast milczeć. Nieznany tryb ponawiania z nowszego hosta pozostaje widoczny; opcje pochodzą z danych workera. Niejednoznaczne ID dostawcy nie może przejść kontroli jako darmowe, a zostać wybrane jako płatne; jest pomijane z przyczyną, z zachowaniem poprawnych źródeł zastępczych. Regresje tego przypadku używają atrap, bez prawdziwych poświadczeń i modeli.

Weryfikacja:

- Kompilacja C++ z ustawieniami dev i kompilacja TypeScript/Vite: zielone.
- 72/72 samodzielnych zestawów logiki webu: zielone. To nie test GUI.
- Szeroka seria 219 celów C++ unit: początkowo 218 zaliczonych i jeden błąd wzorca `settings_fields`. Porównanie obu dokumentów wykazało wyłącznie dwie odziedziczone różnice `human.flows` w 138 polach. Po potwierdzeniu czytelników i korekcie tych dwóch wpisów osobne powtórzenie `unit.test_action_rules` jest zielone. Pierwotnego wyniku nie zastąpiono zielonym logiem.
- Kontrole generatorów, tekstów UI i wcześniejsze zakresowe kontrole ABI/ROM: zielone. W kontrolowanych 18 katalogach brakujące en/pl/nl = 0; nie jest to deklaracja kompletnego tłumaczenia całego projektu.
- `unit.test_storage` (lokalny serwer) i opt-in `catalog_scale` nie wchodzą do 219. Nie wykonano pełnej bramki 281 celów ani testów HTTP/GUI: środowisko odmawia lokalnych socketów i IPC przeglądarki.

Przed wydaniem potrzebna jest pełna bramka w środowisku pozwalającym na lokalny serwer/przeglądarkę, w tym kontrola przepływu zmiany źródła i BYOK oraz weryfikacja na telefonie. Otwarte pakiety funkcjonalne, w tym W11a-2, D7–D10 i R102 G3–G4, pozostają otwarte. Ta partia nie oznacza realizacji całego planu.
