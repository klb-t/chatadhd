# Ekosystem — koncepcje współpracy

> 2026-09-30 · Notatka z brainstormu. Możliwe kierunki, nie opis gotowych integracji ani zlecenie ich wdrożenia.

## 1. Wspólny opis ekosystemu

Projekty rozwijamy jako ekosystem wzajemnie użytecznych zdolności, a nie sztywny łańcuch aplikacji o rozłącznych rolach. Każdy może udostępniać innym wiedzę, narzędzia, sposoby interakcji i doświadczenia, a także z nich korzystać. Współpraca nie wymaga rezygnacji z samodzielnej użyteczności projektów.

**Najbardziej bezpośredni kierunek to ChatADHD jako interfejs do AGEDS oraz do części lub całości WatchDoga.** Nie tylko do zadawania pytań o wyniki, ale również do pracy z materiałem i kierowania zadaniami. Dalej można rozważać ChatADHD jako interfejs do pozostałych projektów. Szersza filozofia dopuszcza wszelkie możliwe powiązania interfejsów z danymi i sterowaniem: rozmowę, głos, graf, widok przestrzenny, gesty czy urządzenia. To horyzont koncepcyjny, nie obecny plan implementacji; czat nie musi zastępować innych interfejsów.

**Przepływ jest dwukierunkowy.** iOmatrix nie jest wyłącznie wejściem i wyjściem: może korzystać ze struktur wiedzy i pamięci rozwijanych w ekosystemie, np. do zapisywania nauczonych rzeczy o użytkowniku, jego słowniku, preferencjach, otoczeniu i sposobach działania. Wiedza ta nie musi należeć do interfejsu ChatADHD. Analogicznie inne projekty mogą wzajemnie korzystać ze swoich metod i doświadczeń.

Wspólne obszary do rozważenia to pamięć i kontekst pracy, schowek dla grafów i innych struktur, zaznaczanie nieostre, przechodzenie między reprezentacjami, pamięć korekt i niedokończonych zadań, pochodzenie informacji oraz przenoszenie nauczonych procedur. Obserwacja, wypowiedź użytkownika, hipoteza modelu i wynik działania pozostają rozróżnialne. Współdzielenie nie oznacza automatycznego dostępu do wszystkich danych ani uprawnień do działania.

Mapa obejmuje ChatADHD, iOmatrix (repozytorium Custom-Keyboard-Pro), Loom, AGEDS, WatchDog, program LEM i jego Workbench oraz PixelSpace AR. Otwarta pozostaje także na książkę/meta-książkę, wątki Legal Flow, narzędzia multimedialne i rekonstrukcję scen, agentów i avatar, DevBox i środowiska pracy, analizę archiwów oraz programy badawcze takie jak RCH. Dawny projekt może wrócić jako samodzielny produkt, współdzielona zdolność albo źródło metod; nie oznacza to automatycznego wznowienia wszystkich prac.

Nie rozstrzygamy tutaj jednej aplikacji, bazy, technologii, podziału repozytoriów ani ostatecznego modelu danych. Różne grafy nie muszą mieć tej samej semantyki. Zachowujemy alternatywy i szukamy rzeczywistych korzyści współpracy zamiast łączyć wszystko na siłę. Ta notatka nie zmienia bieżących priorytetów, kontraktów ani kryteriów gotowości funkcji.

## 2. Znaczenie dla ChatADHD

Punkt wyjścia: rozmowa, pamięć i eksploracja powiązanej wiedzy. Poniższe rozszerzenia są kierunkami współpracy, a nie deklaracją obecnych możliwości.

- **Interfejs do AGEDS:** rozmowa nad materiałem dowodowym, wyszukiwanie i porównywanie źródeł, chronologie, adnotacje oraz kierowanie pracą bez mieszania oryginału z interpretacją. To szczególnie wyraźny punkt integracji.
- **Interfejs do WatchDoga:** formułowanie pytań badawczych, praca ze źródłami, kierowanie analizą i omawianie wyników. Zakres może objąć wybrane części albo cały system, z zachowaniem jego specjalistycznych widoków i reguł badawczych.
- **Wspólny przedmiot pracy:** zaznaczenie nieostre, schowek grafowy i kontekst mogą przechodzić między rozmową, iOmatrix, materiałem źródłowym i innymi widokami bez utraty powiązań.
- **Pamięć bez monopolu czata:** ChatADHD może pomagać przeglądać i korygować wiedzę o użytkowniku, projektach i procedurach, ale pozostałe aplikacje mogą z niej korzystać również bez otwierania rozmowy. Loom i badania LEM są potencjalnymi partnerami tego kierunku.
- **Odzyskiwanie i rozwijanie pomysłów:** archiwa, książka/meta-książka, agent, avatar i pozostałe programy mogą korzystać ze wspólnej pamięci intencji, alternatyw i powodów zawieszenia prac; ich wyniki wracają do rozmowy.

ChatADHD jako interfejs do całego ekosystemu pozostaje możliwością, nie obowiązkowym pośrednikiem każdej operacji. Zmiana interfejsu nie przenosi odpowiedzialności za poprawność danych i wykonania z projektu dziedzinowego na model językowy.
