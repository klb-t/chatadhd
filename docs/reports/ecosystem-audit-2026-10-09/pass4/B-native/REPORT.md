# B4: rzeczywisty odbiór native, 2026-10-09

Przypięty `klb-t/chatadhd` B4 `387afe08587f179d47c013a2ea518ff4b68e36bc`, poprzedni B2 `384c5e1686cd3a58a7d89a8a6813a18c764f697d`. Przeczytano diff, kontrakt B, AGENTS/CLAUDE, STATE i R15/R20/R21/R39–42. Raport autora był wskazówką do testów, nie dowodem odbioru. Produkt i historyczne receipts niezmienione.

Wykonano **23 kryteria w 45 osobnych procesach rzeczywistego Runtime: 18 PASS, 3 FAIL, 2 BLOCKED**. Rozdział: naprawy 2 PASS/2 FAIL; integracja 3 PASS; kontrakt 10 PASS/1 FAIL/2 BLOCKED; reprodukcje 3 PASS. Dodatkowo mała reprodukcja relokacji: 7 wywołań, reprodukcja PASS, akceptacja FAIL. PASS reprodukcji nie jest PASS produktu. Po uszczelnieniu CLI minimalnej reprodukcji powtórzono jej 7 wywołań: domyślne `--phase both` teraz wychodzi 1 przy FAIL akceptacji, mimo PASS reprodukcji (`relocation-gate.json`). Nie dodano nowego kryterium produktu. Transport ScriptedTransport odrzuca wysyłkę, workery wyłączone, zero prób HTTP. Fixture jest syntetyczny; nie mierzy jakości modeli.

## Odbiór zmienionych konsumentów

| Zakres | Wynik | Niezależny dowód i granica |
|---|---|---|
| CH-RES-N001 | częściowo | `read_resource` udostępnia trzy wiadomości i dwie relacje rodzic–dziecko; zwykły `Database.get_msgs` po link-import nadal zwraca placeholder. Zachowana dawna akceptacja FAIL. |
| CH-RES-N002 | częściowo | Nowa projekcja copy zachowuje relacje, ale stary `Catalog.import_selected(copy)` nadal zapisuje wiadomości z pustymi parent_id. Zachowana dawna akceptacja FAIL. |
| CH-RES-G001: native preview/zadanie | naprawione w tym zakresie | `Catalog.preview.resource`, zarejestrowany TaskEngine `catalog.read_resource` i bezpośredni headless odczyt dają ten sam snapshot. Graf odczytano z rzeczywistych `nodes/links` po zamknięciu i ponownym otwarciu Runtime. Przeglądarki nie wykonywano. |
| CH-RES-G002: profil → runtime | częściowo | Dwa jawne zewnętrzne bindingi symlink dają różne pola grafu i rzeczywiste wyniki SemanticAnalyzer, z zgodnym profile_hash. To ten sam plik i istniejący RuntimeProfile, nie dowód edycji grafowego pola aktywującej runtime. Ta druga bramka pozostaje BLOCKED. |
| CH-RES-G003: niezależne polityki | niezweryfikowane / brak kontraktu | Obecna ścieżka jest on-demand, lecz zawsze utrwala pełną pochodną projekcję. Nie ma zbadanej selekcji cache/index/eager/live/write-back. Wymaganie właściciela dopuszcza referencję z projekcją na żądanie; nie żądamy obowiązkowej materializacji. |
| A3-DISC-001/002 | nadal otwarte | MethodRegistry DTO oraz export_unknown są bajtowo niezmienione względem B2; nie powtórzono starego przebiegu bez zmiany. Nowy pakiet C4 jest osobną hipotezą integracyjną w C-boundary. |

Kod nowego odczytu wykorzystuje istniejący parser export-3, Database, RuntimeProfile i TaskEngine. Nie znaleziono drugiego magazynu/resolvera/silnika workflow ani parsera tylko rendererowego. Rodzaje/predykaty są danymi `resource_projection.pack`; `gen_runtime_profiles.py --check` wykonano: exit 0. Klucze kontraktu, stany lifecycle, nazwa operacji i diagnostyka mają odpowiednio wyjątki R42 1/3/6; nie jest to zwolnienie całego pliku z R42.

## Niezmienność i trwałość

Testy zachowują kolejność tablic `children` i wynikową kolejność wiadomości. Odwrócenie **wyłącznie kluczy obiektów**, inne wcięcia/whitespace i dostęp plain JSON zamiast członka ZIP nie zmieniają domenowego zestawu `(source_key, role, text, parent, status)`. Oracle jest jawnie zapisany na podstawie fixture; nie pochodzi z aktualnej odpowiedzi parsera. Hash/locator/version ID mogą się zmieniać wraz z bajtami źródła. Nie założono ogólnej zamienności 1 i 1.0.

Ponowny odczyt z ciepłą utrwaloną projekcją zachowuje snapshot i istniejący graf bez duplikatów. To idempotencja odczytu, nie certyfikacja polityki cache. Niedostępne źródło daje current=false i zachowuje poprzednią projekcję także po reopen. Copy działa bez źródła. Przywrócenie źródła pozwala ponowić odczyt tego samego snapshotu. Wykonano pauzę/wznowienie oczekującego zadania, nie przerwanie procesu w środku parsera ani wznowienie częściowego indeksowania.

Nieznany JSON można skatalogować i odczytać: mapping=uncertain, coverage=not_implemented, bytes nadal dostępne. Odrzucony profil zachowuje nieznane pola jako węzły składni z przypiętym value_ref; nie jest aktywowany jako profil wykonawczy. Nie jest to dowód generycznego discovery czy wyczerpującej interpretacji wszystkich struktur.

## Nowe ustalenie A4-B-RES001

Przeniesienie **tych samych 7 bajtów** `{"x":1}` z old.json do new.json, po czym jawny scan nowej ścieżki, pozostawia stare powiązanie źródła. Zwykły scan kończy się bez ostrzeżeń; `force=true` też nie naprawia dostępu. `read_unit` nadal próbuje nieistniejącego old.json i zwraca Io. Pełny test ZIP wykazuje to samo, zachowując last_successful.

Przyczyna: źródło identyfikuje hash, `register_source` robi INSERT OR IGNORE, checkpoint/dedup ponownie widzianych bajtów nie aktualizuje lokalizacji, a read_unit czyta pojedynczą zapamiętaną ścieżkę. Nie jest to regresja wprowadzona przez nowy B4: ścieżka scan była niezmieniona. Jest to nowo wykonana bramka kontraktu relokacji. Nie stwierdzono kasowania poprzednich danych.

Pakiet odbioru: `handoff-B.json`; dokładne zakresy i wymaganie: `findings.jsonl`; mała reprodukcja: `relocation.py` i `relocation.json`. Naprawa powinna zachować wersję i selektor, a rozróżniać lokalizacje z ich uprawnieniami. Alternatywy: jawne relocate albo lista zweryfikowanych lokatorów; nie automatyczny nieuprawniony transport.

## Pochodzenie i ograniczenia wykonania

Nowe 3 jednostki C++ B4 (`catalog/query`, `runtime`, `model/runtime_profile`) oraz 6 wcześniej zweryfikowanych B2 obiektów połączono przed archiwum B `ddcaeaf…`. Manifest sprawdza SHA źródeł i obiektów; mapa linkera wyklucza pobranie zastąpionych członów archiwum. Shared library SHA-256 `2578229aa002627317a68ffd7215f92d294df3e12b8972c77f514e37c4c0f6f4`; 110/110 nazw C ABI zgodnych z bazą. To selektywny rzeczywisty build, **nie** pełny CMake/CTest ani sanitizer B4. Build odmawia niepokrytych zmian/dependency drift; rozszerzona poprawka potrzebuje nowego pełnego buildu lub zweryfikowanego domknięcia zależności.

Nie wykonano tutaj nested ZIP, remote transport, wyścigu równoczesnej mutacji, twardego crash w połowie transakcji, pełnego scenariusza UI ani permission-aware context/analysis. `TaskRecord.status=done` przy current=false nie został błędnie zaklasyfikowany jako udana analiza: kontrakt B mówi o zakończeniu inspekcji, a stan źródła jest oddzielny. Nie dowodzi to poprawności każdego późniejszego konsumenta tych dwóch pól.

Dalszy konkretny odbiór: naprawa A4-B-RES001; połączenie zwykłego konsumenta rozmowy z istniejącą projekcją dla N001/N002; publiczny kontrakt grafowe pole→resolver; prawdziwe przerwanie/wznowienie indeksowania. Pozostałe osie polityk nie są przesłanką do odrzucenia już działającej projekcji na żądanie.
