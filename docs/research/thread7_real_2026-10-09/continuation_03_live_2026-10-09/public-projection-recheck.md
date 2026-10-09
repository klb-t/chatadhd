# A2-C-001 / A2-C-004 — ponowny test i naprawa granicy publicznej

Baza C: `cb16b336ee0c59a1ff80b4899dadebce33aa7610`. Audyt A:
`9f931da3bb1d1001f9b7d865914ae195d9255b7e`. Nie zakładano, że ustalenie
z wcześniejszego C4 automatycznie dotyczy każdego obecnego wejścia.

Odtworzono oba błędy na własnej bazie. `build_final_handoff.py` oraz
`evaluation-build.py` kopiowały niezatwierdzone dodatkowe pola protokołu
oraz projekcji do odzyskiwalnych źródeł grafu. Odrzucona prezentacja
ujawniała syntetyczny marker przez rzeczywiste `sys.excepthook` błędu
`jsonschema`. Starszy `build_public_artifact.py` przyjmował także obiekt
zamiast hasha w znanym polu receiptu. To kontrolowane reprodukcje; nie
ustanawiają wycieku rzeczywistych rozmów ani klucza z opublikowanych plików.

Aktualny `continuation_02/delivery-build.py` **już odrzucał** podmianę
zamrożonych wejść dzięki zgodności hashów. Nie przypisujemy mu tego samego
błędu dla niezmienionej polityki. Pozostawało inne konkretne wejście:
caller mógł podać zmienioną politykę z własnym tekstem Basic i nadać mu
pozór zaakceptowanej projekcji. Odtworzono to osobno; teraz również zostaje
odrzucone.

## Zmiana

W badawczym callerze istniejącego eksportera dodano osobny, wersjonowany
`public-input-review-v1.json`: zatwierdzone hashe dla 33 ról danych.
To zamknięty przegląd konkretnych publicznych snapshotów z przypiętej bazy,
a nie samopotwierdzenie hashem przesłanym razem z dowolnym wejściem.
Deskryptor jest zaufaną konfiguracją producenta; jego własne pola, formaty
hashów i zagnieżdżona struktura podlegają ścisłej walidacji. Nowe publiczne
wyniki wymagają osobno przejrzanej rewizji deskryptora. Wpisanie hasha nie
jest zgodą właściciela, niezależną adjudykacją ani dowodem jakości modelu.

Sprawdzane są prezentacja, protokół, profil projekcji oraz rzeczywiste
`ProjectionProfile.source_bytes`, które generic codec zachowuje oddzielnie
od obiektu JSON. Pozostałe historyczne buildery sprawdzają także dane
wejściowe/receipty, oryginalny program i protokół tekstowy; najnowszy builder
sprawdza politykę i każde przypięte wejście. Nieobsługiwane dane są odrzucane,
bez usuwania pól, zamieniania treści na `unknown` czy relabelowania dowodu.

Warstwa publikacji zwraca stały kod `public_projection_rejected`, bez
serializacji wyjątku, instancji wejścia ani ukrytego łańcucha exception
context. Odrzucenie pomocniczych danych następuje przed zapisaniem nowego
artefaktu/prezentacji. Pełne surowe logi reprodukcji i stare źródła
producentów pozostają prywatne.

Generic `method_graph.py`, wspólne schematy i produkcyjny runtime są bez
zmian. Zatwierdzone źródła nadal odzyskuje się bajtowo; test bieżącego
handoffu zachowuje identyczne wartości prezentacji i kontraktu. Zmiana kodu
producenta poprawnie zmienia jego hash i tożsamość nowego artefaktu. Nie
przepisano starych artefaktów, freeze, kosztów ani wyników.

Przegląd drugiego agenta wykrył następnie TOCTOU w najnowszym builderze:
po sprawdzeniu wejść ponowne czytanie kontraktu mogło dołączyć nowe pole
`$comment` do publicznego kontraktu. Builder teraz konsumuje zachowane
bajty, które rzeczywiście przeszły zatwierdzenie. Kontrolowana podmiana
pliku po walidacji nie zmienia kontraktu ani artefaktu. Zachowano dowód
przed/po i dodano jedną regresję. To przegląd współpracownika tej samej
sesji, nie nowa niezależna adjudykacja wyników.

## Weryfikacja

- **60/60 testów, 0 skip**: 18 nowych testów publicznej granicy i 42 istniejące
  testy analizy. To testy mechaniki na kontrolowanych danych.
- Niezmienione AST funkcji `projection_tests`, `add`, `err_summary` z A:
  przed zmianą kontrola PASS, 3 reprodukcje PASS i 3 acceptance FAIL;
  po zmianie kontrola PASS i wszystkie 3 acceptance PASS. Trzy stare
  asercje reprodukcji zwracają teraz FAIL, ponieważ wyciek nie występuje;
  nie należy liczyć tego jako regresji produktu.
- Dodatkowa macierz czterech entrypointów: po zmianie 4 prawidłowe kontrole
  produkują artefakt, a wszystkie 12 mutacji są odrzucone bez markera w
  artefakcie, odzyskanych źródłach i diagnostyce. Przed zmianą 9 mutacji
  reprodukowało wadę, a 3 kontrole mutacji aktualnego delivery prawidłowo
  blokował już stary kod.
- Oddzielna reprodukcja modyfikacji polityki aktualnego delivery: przed
  zmianą własny tekst callera trafiał do wyników; po zmianie odrzucenie.

Polecenie ponownej weryfikacji ze środowiskiem zawierającym zależności:

```bash
TMPDIR=/var/tmp PYTHONPATH=/tmp/thread7-python-deps:. PYTHONDONTWRITEBYTECODE=1 \
python3 -m unittest loom.tools.structure.test_experiment_public_projection_v1 \
loom.tools.structure.test_experiment_analysis_v1 -v > public-projection-tests.log 2>&1
```

`public-projection-recheck-v2.json` wiąże końcowe logi, kod przed/po zmianie
i wersję deskryptora; poprzedni receipt zachowano. Prywatny manifest
`audit-projection/MANIFEST-v2.json`
obejmuje pełne dowody i pliki odtworzenia, również stare producenty.
Nie wykonywano ponownie rankingów, preparacji, inference ani API.
Koszt tego sprawdzenia: **0 USD**.

Granica zaufania jest jawna: osoba zmieniająca zaufany kod i deskryptor
może zatwierdzić inne dane. Mechanizm chroni przed przypadkowym przyjęciem
nieprzejrzanych danych przez publiczne entrypointy; nie jest ogólnym
klasyfikatorem PII ani zabezpieczeniem przed złośliwym administratorem repo.
