# Continuation 01 — nowy panel rzeczywistych źródeł

Przygotowano 12 dodatkowych rodzin: 6 OpenAI i 6 Anthropic, 2079 wiadomości,
2085 węzłów natywnych. Zachowano całe rozmowy, również gałęzie, pozatematyczne
fragmenty, wiadomości narzędziowe, metadane i osadzone rekordy załączników.
Okres wiadomości: 2026-01-23–2026-09-09 UTC. Dawne 3 rodziny pozostały odrębnym
panelem i nie weszły do nowej selekcji. Nie wykonano inference: nowy koszt 0 USD.

Źródłem są rzeczywiste bajty `ChatADHD_Loom_LEM_takeout_test_2026-10-01.zip`
(SHA256 w `corpus-receipt.json`) oraz odpowiadającego audytu. Wykorzystano jego
`selected_ids`, selektor pełnych rozmów, kanoniczne hashe obiektów, manifest
pakowania i mapowanie załączników. Nie przeszukiwano ponownie pełnych eksportów
4545 rozmów ani nie uruchamiano historycznych 46 wywołań Jev. Ten wcześniejszy
program jest odrębny od rachunku wątku 7: 720 prób / 0,873216500 USD.

Odzyskany wycinek obejmuje 179 całych rozmów: 65 OpenAI i 114 Anthropic,
14652 wiadomości, okres 2025-10-08–2026-09-12 UTC. Był rozwojowym wycinkiem
ChatADHD/Loom/LEM, nie kompletnym ani losowo reprezentatywnym archiwum.
Zweryfikowano 146 członków źródłowego ZIP-a i 103 członków audytu. Dodatkowa
kontrola potwierdza zgodność 179 kanonicznych obiektów z dawnym selektorem,
12/12 pełnych obiektów, 2079/2079 wiadomości i 2085/2085 węzłów nowego panelu
oraz 103/103 binariów źródłowych. Wyniki tej kontroli są oddzielone od 20 testów
mechaniki programu, które korzystają z jawnych syntetycznych fixtures.

Dobór jest danymi w `corpus-policy.json`. W obrębie dostawcy wyznaczono tercyle
długości tekstu; z każdego wybrano dwie całe rodziny. Deterministyczny wybór
zwiększa pokrycie dostępnych struktur i leksykalnych wskazówek. W panelu występuje
6 rozmów z rozgałęzieniami i 6 z natywnymi oznakami narzędzi. Kandydaci wskazówek:
korekta 7, zmiana preferencji 11, zmiana tematu 8, zadanie wieloetapowe 10,
checkpoint/wznowienie 4. Są to **trafienia leksykalne w wypowiedziach użytkownika**,
nie potwierdzone etykiety semantyczne, deklaracje preferencji ani cztery uznane
checkpointy. Nowego streszczenia asystenta nie utworzono.

Rodziny są składowymi po identyfikatorze rozmowy, dokładnym obiekcie, wspólnym
natywnym identyfikatorze węzła/wiadomości lub identycznej wypowiedzi tego samego
nadawcy o długości przynajmniej 200 znaków. Ostatnia reguła jest konserwatywna:
może połączyć rozmowy zawierające wspólny szablon. Parafrazowanych duplikatów
nie uznano za wykryte. Kontrola objęła oba eksporty i stary panel; wykryła jego
3 rodzinne nakładania. Całe składowe są przydzielane razem, nigdy pojedyncze
wiadomości czy gałęzie. Prywatny indeks przechowuje przypisania i hashe.

Osiem rodzin ma status tuning, cztery `provisional_validation`. Audyt dawnych
przypadków modelowych/retrieval oznaczył 16 źródłowych rozmów jako znane wcześniej
użyte; ich rodzin nie przydzielano do proponowanej walidacji. Cały wycinek jest
jednak materiałem rozwojowym wcześniej przechowywanym i przetwarzanym. Nie ma
pełnego dowodu braku wcześniejszej ekspozycji. Te cztery rodziny są **kandydatem
partycji walidacyjnej, nie niezależnym/blind holdoutem**. Nie czytano ani nie
wykorzystywano `eval/real-holdout-key`.

Oryginalny ZIP pozostał niezmieniony. Prywatne rekordy normalizacji zawierają
`native_conversation`, każdy `native_message`, `native_node`, wszystkie węzły
oraz dokładny wskaźnik do elementu źródłowego shardu i jego hash. Pole `text`
jest jawnie stratną projekcją tekstowych części; nie zastępuje źródła. Anthropic
`human` mapuje się na `user`; brak relacji rodzica pozostaje brakiem. Nie
wymyślono liniowej kolejności rodziców na podstawie indeksu tablicy.

Dostępne 103 binaria pozostają w źródłowym ZIP-ie, 11 binariów powiązanych z nowym
panelem dodatkowo skopiowano do prywatnego freeze i sprawdzono SHA. Panel ma
75 rekordów nierozwiązanych referencji; osadzone treści pozostały bez zmian.
Dawny audyt wymienia 168 nieodnalezionych kandydatów OpenAI i jeden pominięty
ucięty ZIP. Anthropic ma 727 wystąpień referencji do 692 unikalnych plików bez
osobnych binariów w sprawdzonym źródle. Pełność załączników nie jest dowiedziona;
nieodnalezienie nie oznacza usunięcia. Szczegóły identyfikatorów i błędów wejścia
pozostają w prywatnym artefakcie; publiczne rachunki zawierają tylko agregaty.

Program CLI: `loom/tools/structure/experiment_corpus_v1.py`; argumenty
`--source --audit --historical --policy --output --receipt`. `--output` musi być
nowym katalogiem poza repozytorium: poprzedniego freeze nie nadpisuje. Prywatny
`panel.json` wskazuje pełne rekordy, a `FREEZE.json` wiąże pliki, wersję kodu,
politykę i poprzedni checkpoint. `corpus-verify.py` odtwarza kontrolę zgodności
ze źródłami. Publiczny receipt nie jest źródłem rozmów ani dowodem jakości modeli.
