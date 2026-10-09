# Thread 7 — credential odebrany, rzeczywisty preflight zatrzymany

2026-10-09. Kontynuacja C od `e18822487e991ba921d47b561eb87c92adf92e38`,
po zamkniętym pakiecie offline i naprawach granic opisanych w
`thread7-real-live-unblock-2026-10-09.md`. Nowy, osobny zapis:
`docs/research/thread7_real_2026-10-09/continuation_04_preflight_2026-10-09/`.
Przypięto A `9f931da3bb1d1001f9b7d865914ae195d9255b7e` oraz B
`ed4bd2fc444ff0c71148e1fdef6f747a1093edf4`. Brak nowego ustalenia A względem
poprzednio odtworzonych i naprawionych A4-C-001/002 oraz A2-C-001/004.

## Dostęp i rzeczywista blokada

Załącznik użytkownika odszyfrowano istniejącym `credential_handoff_v2.py`.
Jednorazowy odbiorca został zużyty prawidłowo; sekret jest dostępny tej sesji
przez prywatny plik. Fingerprint odpowiada dotychczasowej kampanii. Nie
utworzono nowego programu, nie odnowiono limitu ani zgody EUR5. Wartość
klucza, fingerprint i prywatne ścieżki nie należą do publicznej projekcji.

Dwa uwierzytelnione GET metadanych klucza zwróciły ten sam wynik. Ostatni
odczyt: `2026-10-09T11:33:13.492450+00:00`.

| Wielkość | USD |
|---|---:|
| Nieodnawialny limit tego klucza | 5,000000000 |
| Bieżące zużycie dostawcy | 1,450420500 |
| Pozostałość raportowana przez dostawcę | 3,549579500 |
| Zweryfikowane 720 historycznych prób | 0,873216500 |
| Nieprzypisane dodatkowe zużycie | **0,577204000** |

Jedyny istniejący payer odrzucił preflight:
`budget_evidence_gate_blocked:provider_usage_reconciles_with_cumulative_actuals`.
15 z 16 kontroli budżetowych przeszło; kontrola zgodności usage nie przeszła.
Późniejszej kontroli eskalacji nie osiągnięto. Stan wszystkich bieżących
rezerwacji poza odzyskanym checkpointem pozostaje **unknown**. Pozostałość
dostawcy nie jest jeszcze wiarygodnym budżetem do dispatchu.

Cały istniejący ledger wraz z pierwszymi dowodami i 714 późniejszymi
rozstrzygnięciami przeniesiono bajtowo do prywatnego katalogu roboczego.
Archiwalny oryginał pozostał niezmieniony; aktywna kopia jest kontynuacją
tego samego ledgera, nie drugim rachunkiem pieniędzy. Walidacja wszystkich
720 prób potwierdziła koszt 0,873216500 USD i zero **archiwalnych** otwartych
rezerwacji. Nie dodano sztucznego offsetu ani prób wyrównujących saldo.

Siedem wybranych przed sprawdzeniem rachunków (pierwszy, ostatni i
najdroższy na model, po deduplikacji) odczytano GET-em ponownie: 7/7 HTTP200,
7/7 zgodnych identyfikatorów i kosztów. Nie jest to odświeżenie wszystkich
720 rachunków. Pierwszy lokalny checker użył błędnej nazwy funkcji;
poprawny wynik v2 wyliczono z tych samych zapisanych odpowiedzi, bez
ponownego HTTP. Zachowano oba dowody. Dostępne późniejsze metadane
checkpointów nie wyjaśniły różnicy; checkpoint real-prompts zawierał
preparacje i zero nowych płatnych wywołań.

## Zamrożony mały panel

Wybrano cztery już istniejące requesty: dwie rodziny źródeł (OpenAI i
Anthropic), dla każdej graf bezpośredni oraz tekst ze strukturą w jednym
wywołaniu. Model: `openai/gpt-4.1-mini`, żądany provider OpenAI bez fallbacku,
temperature 0, max_tokens 768, pełny dokładny tekst, bez personalizacji.
To małe porównanie form odpowiedzi jednym modelem na materiale tuningowym
już znanym badaczowi, nie porównanie wielu modeli ani niezależny holdout.

Connector v2 sprawdził rzeczywiste bajty źródeł 4/4. Kontekst i zadanie
wewnątrz par pozostają identyczne. Łączny rozmiar requestów: 495462 bajty;
przecięcie hashy z 720 historycznymi próbami: 0. Nie renderowano nowych
treści, nie powtarzano preparacji ani rankingu. Kolejka ma cztery pozycje
`prepared`, zero wysłanych i zero zarezerwowanych.

Odświeżono ceny rzeczywistego endpointu i kurs referencyjny ECB
(2026-10-08: 1 EUR = 1,1186 USD). Konserwatywny pułap payera dla całej
fazy wynosi **0,2546942 USD**. Jest to prognoza, nie naliczony koszt ani
zaksięgowana rezerwacja. Sprawdzono aktualną oficjalną dokumentację
[prompt cache](https://openrouter.ai/docs/guides/best-practices/prompt-caching)
i [response cache](https://openrouter.ai/docs/guides/features/response-caching).
Dispatch nadal wymaga świeżego preflight; gotowa odpowiedź nie może być
ponownie użyta jako niezależne wykonanie.

## Wykonanie, dowody i wznowienie

**Nowe płatne wywołania: 0; nowy koszt API: 0 USD.** Nie ma nowych odpowiedzi
modeli, pomiarów ich jakości ani zwycięzcy. W tej kontynuacji nie zmieniono
kodu; wykonano odbiór credential, rzeczywiste odczyty kontrolne, walidację
historii, podłączenie gotowych requestów i preflight. Poprzednia bramka
1597/1597 PASS pozostaje dowodem z poprzedniego etapu, nie nowym testem.

Do wznowienia potrzebna jest historia użycia **tego klucza**, przede wszystkim
brakujące generation IDs i rachunki lub nowszy ledger wraz z otwartymi
rezerwacjami. [Logs](https://openrouter.ai/logs) pozwala filtrować po kluczu
i przeglądać poszczególne generacje; [Activity](https://openrouter.ai/activity)
udostępnia eksport zagregowanych kosztów. Sam agregat nie zastępuje
przypisania poszczególnych prób. Dokumentacja:
[Logs](https://openrouter.ai/docs/guides/features/logs),
[eksport](https://openrouter.ai/docs/cookbook/administration/activity-export).
Podany klucz nie jest kluczem administracyjnym; wymagające takiego klucza
API historii nie było wywoływane ani obchodzone.

Po uzyskaniu dowodów: rozliczyć różnicę i otwarte rezerwacje w istniejącym
mechanizmie, zachowując pierwsze świadectwa; ponownie odświeżyć klucz, ceny
i preflight. Nie zerować unknown, nie przypisywać różnicy do domniemanych
prób, nie zmieniać polityki na podstawie samego salda. Dopiero pozytywny
pełny preflight pozwala wysłać cztery zamrożone requesty przez istniejący
payer i granicę kolejki. Nie uruchamiać całej macierzy.

Surowe metadane, historyczne odpowiedzi, requesty panelu i kolejka pozostają
w prywatnym checkpoincie. Publiczny `PREFLIGHT.json` zawiera wyłącznie
sprawdzoną projekcję liczb, statusów i hashy. Prywatna paczka nie zawiera
credential ani materiału odbiorcy. STATE/INDEX, runtime, UI i cudze gałęzie
pozostały niezmienione.
