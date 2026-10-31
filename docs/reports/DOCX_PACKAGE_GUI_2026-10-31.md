# Domyślny pakiet DOCX i nowe GUI OCR/DOCX — 2026-10-31

## Odbiór

Przyjęty kod: **`daf4f48412924737f260a42499818ea6bbeba513`**.
Baza przyrostu: `d0af04a2f5a24205618ec827bec02d0765887af9`; P0/M1/M2/DOCX
pozostają przodkami. Własny przyrost tej samej sesji integratora
`01a192f5-2814-7e52-bf0c-32f3428151ed`, bez ponownego importu delt wykonawców.

- Pakiet: `ed3b8cbc48e7f74a9c3803306fdebd32cef8ec7d`, 12 plików;
  SHA256 delty `19c3c01e6af602baf80e45c49fb76ed6a9811becf5da60ac5ccdeb08163e359a`.
- GUI/testy: `daf4f48412924737f260a42499818ea6bbeba513`, 5 plików;
  SHA256 delty `c1e9420192542cc74c63eb9b2cdcb15a605c2d967c0f15ddafaa2925c67fbea5`.
- 17/17 zmienionych plików zgodnych bajtowo z commitem i zachowanym manifestem
  testowanych źródeł. Nie zmieniono genesis/client_blobs ani worktree G1.

Implemented/tested: domyślny pakiet na testowanym Linux, pobieranie oryginału
w GUI oraz opisane scenariusze. GUI funkcjonalnie **7/7**, lecz **pełna akceptacja
prezentacji importu PNG pozostaje otwarta** z powodu dwóch błędów opisanych niżej.
Deployed: **NIE**. To nie jest pełna gotowość wydania.

## Dostarczenie reader.py i interpretera

CMake komponent `runtime` dostarcza `bin/loom-server`, `lib/libloom.so*`,
`share/loom/docx_basic/reader.py` i `runtime.json`. Czytnik pochodzi z kanonicznego
parsera; nie zmieniono parsera. Domyślne dane `python` i `script` wskazują
`package:docx_basic`. Resolver znajduje zasoby względem własnego modułu,
nie względem checkoutu, CWD, HOME, nazwy uploadu ani PATH. Względne uruchomienie
programu sprawdza jego rzeczywiste mapowanie i `/proc/self/exe`; biblioteka
wymaga absolutnej ścieżki ładowania albo absolutnej ścieżki loadera.
Jawne absolutne override w zaufanym profilu zachowano; brak override nie
uruchamia innego przypadkowego helpera.

Python jest **jawną systemową zależnością wykonawczą, nie bundlowanym runtime**.
Konfiguracja sprawdza wybrany interpreter >=3.10 oraz stdlib zipfile/zlib/pyexpat
w trybie izolowanym; manifest rejestruje obserwowaną wersję i hash readera.
Testowano `/usr/bin/python3` 3.14.4, bez instalacji do `/usr` lub pobierania
zależności. Domyślny pakiet nie jest samowystarczalny na hoście bez tej
zależności. Manifest nie jest podpisem, a resolver nie stanowi sandboxa OS.
Instrukcja lokalnego staging: [README](../../loom/tools/docx_basic/README.md).

Własny przyrostowy build `-j2` użył zachowanego
`.integration-p0-20261031/build` (Debug/WERROR). Własny staging przeniesiono
z `stage2` do `stage2-relocated`. Landlock faktycznie odmówił odczytu
kanonicznego readera, adaptera i binarium z oryginalnego checkoutu/builda.
Runtime mógł czytać wyłącznie zainstalowane zasoby staging i jawne zależności
systemowe; kopie harnessu/fixtures były tylko wsparciem testów, bez drugiej
kopii reader.py w strukturze źródeł. Serwer uruchomiono jako `bin/loom-server`,
a bibliotekę z absolutnej ścieżki staging. Oryginalny checkout nie był potrzebny.

## Własne kontrole

| Kontrola | Wynik i zakres |
|---|---|
| Build native | PASS, przyrostowo `-j2`; finalny log `build-relative-package.log` |
| Domyślny relokowany pakiet | **5/5**, 0 skip, 34,795 s; HTTP PL/treść/kontekst/warnings/reopen/restart/original, brak helpera, brak interpretera, upload nie wybiera runtime, shared-library C API |
| Web build | PASS, **22,83 s**; istniejący własny npm/Vite/toolchain; ostrzeżenie o dużym chunku pozostaje |
| Nowe rzeczywiste GUI | **7/7** grup; natywny własny serwer, Chromium, ImportPanel/ChatView; brak pageErrors |
| Pobieranie oryginału — komponent | **4/4**; zgodne bajty/nazwa, odmowa złego SHA, odmowa 404, brak akcji dla ścieżki niebędącej blob:hash |
| ABI | **3/3**, 2,236 s; eksporty, sygnatury i ctypes smoke na końcowej własnej bibliotece |
| Runtime i Git | generator `--check`, diff `--check`, hash źródeł i zgodność staging/build PASS |

Nie sumowano ponownych prób. Stage1 5/5 jest wcześniejszym wynikiem przed poprawką
względnego uruchomienia, nie odbiorem końcowego kodu. GUI1 zatrzymało się na
otwartym panelu; GUI2 ukończyło pierwsze 5 grup, po czym następny niezależny
przypadek dostał pozostawiony w composerze uszkodzony DOCX. Harness poprawiono,
zamykając panel i usuwając stare załączniki przez rzeczywiste przyciski UI.
GUI3 wykonało wyłącznie brakujące 2/2. Po dodaniu pobierania oryginału nowy
web build i końcowy GUI4 wykonały całe 7/7 na nowych źródłach. Negatywne logi
zachowano; nie zmniejszono asercji ani nie obchodzono bramki sekretów.
Komponentowy bundle testowy zgłasza ostrzeżenie import.meta w nieużywanym tam
workerze PoW; nie jest to błąd produkcyjnego buildu Vite.

## Rzeczywiste GUI i pochodzenie danych

1. ImportPanel przyjmuje syntetyczny polski PNG; rzeczywisty lokalny Tesseract
   odczytuje tekst, zapisuje rozmowę; zero wywołań dostawcy podczas importu.
2. ChatView otwiera tę rozmowę; faktyczna treść OCR trafia do kontekstu.
   Lokalny provider wyprowadza odpowiedź z odebranej treści, a nie zwraca jej
   bezwarunkowo. Dla celu tekstowego nie wysłano image_url.
3. ChatView wysyła syntetyczny DOCX z polskimi akapitami/listą/tabelą; tekst
   trafia do kontekstu, częściowość i warnings są widoczne. Przycisk
   „Pobierz oryginał” rzeczywiście pobiera ten sam DOCX z właściwą nazwą.
4. Po restarcie natywnego serwera i reopen rozmowy zachowano kontekst,
   ostrzeżenia oraz pobranie oryginału z identycznym SHA256.
5. Uszkodzony DOCX daje widoczny komunikat odmowy i zero wywołań dostawcy.
6. Testowy wzorzec sekretu pasuje do niesekretnego tekstu OCR. Rzeczywisty
   secret_pending wstrzymuje dispatch; kliknięcie potwierdzenia przekazuje
   dokładny image_payload_sha256, pytanie i załączniki, powodując jeden dispatch.
7. Dla trybu never brak przycisku potwierdzenia; odrzucenie nie wywołuje dostawcy.

Nowy komponent używa istniejącego `clientBlobFetch`, dopuszcza tylko adres
blob:SHA256, sprawdza SHA256 pobranych bajtów przed zapisem i pokazuje błąd
zamiast tworzyć plik przy niezgodności. Nie wykonuje ani nie podgląda dokumentu.
Teksty PL/EN są danymi; nazwę pobrania ogranicza do nazwy pliku.

Dane: istniejący syntetyczny PNG lokalnego OCR oraz `docx_basic/tests/fixtures.py`.
PNG SHA256: `05bb01e18f58fefb8fc984d14771d7c831268684ef6279089bdec0572d54bde1`.
DOCX pobrany dwukrotnie: `4bb0a25a9f0312be31c6d5cf42da4d12bc5c14de8f57562ab6604bfe199807f6`.
Dostawca to własny loopback, bez poświadczeń/modelu; odpowiedzi oznaczone
SYNTHETIC_OCR/SYNTHETIC_DOCX. Testy nie dowodzą jakości rzeczywistego modelu.
Chromium miał viewport 420×900, co nie jest testem fizycznego mobile.

## Odłożone wspólne hooki i błędy produktu

Po udanym imporcie PNG UI nadal błędnie podaje brak załączników z JSON bez ZIP
(`attachments.json_without_archive`), mimo zachowanego źródła i OCR.
Tytuł rozmowy zawiera techniczne `req_…png` zamiast nazwy uploadu.
Są to błędy produktu, nie środowiska ani brak zależności.

[Jawny patch do późniejszej integracji](IMPORT_PRESENTATION_FOLLOWUP_2026-10-31.patch)
ma bazę `ed3b8cbc48e7f74a9c3803306fdebd32cef8ec7d` (te cztery pliki są identyczne
na końcowym code_head); SHA256
`4b2852dbe889c47853500edd958a9d1d375f61ce8acbcdfcb1f86d1d0cabcc72`.
**Nienałożony, tylko review i `git apply --check` PASS; bez kompilacji/testu native.**
Obejmuje importer_image, import_truth, dane chat_ops oraz proponowany test:
nazwa z display_name, dane warunkowe per format oraz licznik zachowanego
źródła oparty na source_id/blob_hash, bez udawania liczby załączników.
Wymaga uzgodnienia z deltą G1, regeneracji runtime (nie kopiowania starego inc),
regresji zwykłego JSON oraz GUI. Test w patchu jest propozycją, nie wynikiem PASS.
Nie rozszerza się nim zaakceptowanego kodu ani zakresu tej tury.

Zachowane starsze luki: role labels/tytuł New…, raw503, komunikat o kluczu
w testowym lokalnym trybie auth-free. Nie deklarujemy pełnej polonizacji.
DOCX pozostaje częściowym odczytem tekstu, bez odtworzenia pełnego układu,
paginacji, osadzonych obrazów, DOC/XLSX/PPTX/ODT. Nie powtarzano przyjętych
pełnych backendowych M1/M2/DOCX/P0/BYOK; nie ma nowej pełnej bramki repo,
Release/sanitizer, zewnętrznego share, Anthropic vision ani produkcyjnego testu.

## Dowody, granice i dalszy krok

Własne niecommitowane dowody: `.integration-package-gui-20261031/`.
Manifest `accepted-evidence.json` SHA256:
`6d9eed5053b45d58bb8383569fa1cad95f0be0b22c47faed020facffc41ba249`.
Wskazuje 17 źródeł, logi, web/dist, runtime, 7 screenshotów, receipt i dwa DOCX.
GUI4 receipt SHA256 `c03a13c1902d6622e86439b6d80023eef3f4fd01648fb9d3453dffa164b68836`.
Serwer staging/build SHA256 `080b73e0afee27b00483e2ba6c96dc5866060509dcd4bb26b5393fe8e830b18e`;
biblioteka SHA256 `baca64e66a4d74b5055515551cfd97767cd54b04fcad5af71151b6d5e01235bc`.
Binaria staging i własnego builda są bajtowo zgodne. Surowych receiptów,
screenshotów, katalogów build, prywatnych checkpointów ani danych administratora
nie włączono do commita produktu.

Kanon §0 rewizja 2: konfigurowalne ścieżki i teksty w danych, integralność
adresu źródła i oddzielenie uploadu od runtime egzekwowane w kodzie.
Hierarchia logowania i jeden wyłącznik przy deploy nadal nieudowodnione;
bez niezwiązanego refaktoru. Graf części jest osobnym zadaniem G1.

Odwracalność: dwa małe lokalne commity kodu bez migracji danych; nowy zasób
instaluje się tylko w wybranym lokalnym prefixie. Cofnięcie pakietowania
przywraca historyczne wymaganie jawnego helpera; nie usuwa źródeł ani historii.
Nie wykonywano rollback/deploy. Następny odbiór i zastosowanie wspólnego patcha
uruchamia administrator. Bez push/CI/PR/publikacji, modeli, wydatków i zmian hosta.
