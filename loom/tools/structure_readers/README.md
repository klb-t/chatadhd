# Zaufane lokalne readery struktury

Host obsługuje jawne `csv` i `python_ast` przez istniejące
`client.blob_text` / `client.blob_text_read`. Generuje wynik z bloba samodzielnie;
JSON klienta nie poświadcza struktury. Shared `read_local_json` rozszerza
istniejący subprocess DOCX, zachowując jego kontrakt i obsługę błędów.

CMake komponent `runtime` instaluje `share/loom/structure_readers/`:
`worker.py`, profil, obydwa readery, profil AST i `runtime.json` z hashami
wszystkich pięciu plików oraz absolutnego interpretera. Interpreter jest
zależnością systemową, nie dołączoną kopią. Linux/glibc >= 2.34 oraz Python
>= 3.11 z modułem `resource`; Android/JNI nieodebrane. Resolver opiera się
na lokalizacji załadowanego executable/lib, nigdy na cwd, PATH lub uploadzie.

Profil jest zaufanym elementem instalacji. Obecny preset CSV to średnik,
cudzysłów, nagłówek i język polski. Można zmienić go przy świadomym przygotowaniu
nowego pakietu i jego manifestu; nie przez endpoint konfiguracji konta lub
żądanie importu. Nie ma automatycznej detekcji dialektu ani nowego UI ustawień.
Domyślne limity: 1 MiB wejścia, 32 MiB wyjścia, 3 s CPU, 5 s wall time,
256 MiB przestrzeni adresowej subprocessu. Zaufany profil może ustalić inne
**dodatnie** granice; kod nie pozwala ominąć ograniczonego transportu.

Uruchomienie: argv bez shella, Python `-I -B -S`, wyczyszczone środowisko
(`LANG=C.UTF-8`), stdin otwarty na oryginalnym blobie, stdout ograniczony,
stderr `/dev/null`, zamknięte odziedziczone deskryptory. Worker ustala RLIMIT_CPU,
RLIMIT_AS i wyłącza core dump przed ładowaniem readera. Kod dokumentu nie jest
wykonywany. Native watchdog zabija i zbiera wyłącznie własny proces; nie jest
to sieciowy sandbox ani gwarancja przeciw złośliwej instalacji. Testy wykonano
z Landlock, który odmawiał readerom dostępu do checkoutu.

Host sprawdza wejście i pakiet przed/po parsowaniu. Binding metody zawiera
oryginalny SHA, profil, wersję/hash interpretera, hashe helperów i hash zapisanej
pochodnej odpowiedzi. Materiał oryginalny pozostaje w blob store. Graf korzysta
z dotychczasowego genesis/fingerprint/provenance; zagnieżdżone części świadomie
nakładają się, nie są partycją bajtów ani wyrównaniem między wersjami.

`read` ponownie parsuje i weryfikuje zapisany receipt oraz konkretny Unit,
wystąpienie i odwołania do obserwacji. Kompilator kontekstu wymaga dla tych
wystąpień callbacku walidującego oryginał i host receipt; sam widok DB nie
poświadcza integralności bloba. Wymazanie powiązania hosta nie zmienia starego
wystąpienia w zaufany tekst ogólny. Pozostała ogólna obsługa tekstu i DOCX
zachowuje swoje reprezentacje/współrzędne.

Zmiana przypiętej instalacji (w tym aktualizacja systemowego interpretera)
odmawia replay starej metody. Zachować poprzedni pakiet; migracja projekcji i
współistnienie wielu instalacji parsera nie są zaimplementowane w tym przyroście.
Sprawdzenie cache nie jest audytem ani naprawą całego podgrafu. SQL rollback
nie obiecuje usuwania każdego wcześniej zapisanego content-addressed bloba.

Własne bramki: 9 grup native CSV/AST, osobna odmowa uszkodzonego oryginału,
2 scenariusze HTTP ze syntetycznym providerem i restartem, 8 odmów helpera/
limitów, G1 11 + DOCX 9 sentineli oraz domyślny reader pakietowy DOCX.
Brak GUI dla CSV/Python, realnego modelu, mobilnej akceptacji, pełnej bramki
repo, aktualnej kompatybilności rollback tych nowych wystąpień lub deploy.
Dokładne komendy, źródła i hashe: raport odbioru HOST_READERS_CSV_2026-11-01.
