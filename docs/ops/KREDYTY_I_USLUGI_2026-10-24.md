# Kredyty i usługi: na co je wykorzystać i gdzie jeszcze mogą być (2026-10-24)

Pytanie właściciela: „na co możemy wykorzystać kredyty, które mam, i gdzie jeszcze mogę mieć kredyty i nawet o tym nie wiedzieć”.

**Zakres tej wersji (korekta właściciela, 2026-10-24):** tylko Google (oba konta GCP, korzyści Google Developer Program, AI Studio/Gemini free tier) i GitHub. Pozostali dostawcy: krótka lista z linkami do salda (sekcja 2), głębsze badanie dopiero po zalogowaniu właściciela.

Zasady czytania (CLAUDE.md): wszystko tu to **fakty z daty 2026-10-24 z oceną źródła**, nigdy ograniczenia. Zastosowania to **przykłady** (silnik ma obsłużyć każdą konfigurację), kwoty i limity dostawców to dane z okna ważności, nie reguły. Ocena: `[oficjalne]` = dokumentacja/strona dostawcy, `[forum]` = użytkownicy/blogi, `[niezweryfikowane]` = nie potwierdzone (z pamięci modelu, niespójne źródła). Badanie: wyszukiwarka i pobrania stron, bez zalogowania, nic nie zmieniało kont ani nie kosztowało. Strony Google pokazują różne wersje planów (45 USD, 10 USD, 550 USD rocznie): kwoty sprawdź na żywo w konsoli.

---

## 0. Wstępny plan wykorzystania według terminów ważności

To **opcje (przykłady), nie decyzje**: nie wiemy jeszcze, jakie potrzeby się pojawią. Kolejność wynika wyłącznie z dat.

| # | Termin | Kredyt / zasób | Źródło terminu | Opcje użycia przed terminem (przykłady) |
|---|---|---|---|---|
| 1 | **Co miesiąc (limity się odnawiają)** | GitHub: Actions na repozytoriach publicznych, Codespaces (120 h Free / 180 h Pro), Copilot (limit miesięczny) | `[oficjalne]` sekcja 2.2 | Wykorzystać darmowe standardowe runnery w publicznych repozytoriach tam, gdzie to wygodne (opcja, nie cel oszczędnościowy)|
| 2 | **Nieznany, najwcześniej ok. 2027-02 (do potwierdzenia)** | Anthropic API ok. 100 USD (placeholder; pamięć właściciela) | Typowo: kredyty zakupione wygasają **rok po zakupie** `[oficjalne]` https://support.anthropic.com/en/articles/8977456-how-do-i-pay-for-my-api-usage ; Usage Credits rok od wydania, promocyjne w terminie podanym przy wydaniu, bez terminu rok `[oficjalne]` https://www.anthropic.com/legal/credit-terms . **Gdzie potwierdzić:** Console, Plans & Billing | Batch (taniej o 50 %) analizy semantycznej zaległych wiadomości; ewaluacje; zadania agentów badawczych. Zakładamy najgorszy przypadek (12 miesięcy od zakupu) |
| 3 | **2027-03-24** | GCP konto A: pierwsza z 7 pozycji „Google Developer Program premium benefit” (łącznie 59,97 EUR) | Konsola Billing, Credits (dane właściciela); zasada: wygasają rok po przyznaniu `[oficjalne]` https://developers.google.com/profile/help/benefits | Koszt VM `devbox-20260923` (jeśli zakres obejmuje Compute Engine), Cloud Run/Workflows (spot-guard), Vertex AI embeddingi/OCR |
| 4 | **od 2027-03-24 do 2027-05-23** | pozostałe pozycje konta A | jw., każda pozycja ma własną datę (spisać z Billing) | jak w pkt 3 |
| 5 | **2027-05-23** | GCP konto B: „Trial credit for GenAI App Builder”, 855,05 EUR; ostatnie pozycje konta A | Konsola (dane właściciela) | **Najpierw ustalić zakres** (zob. 1.2). Opcje: Vertex AI Search/Agent Search po korpusie; jeśli zakres szeroki: Gemini na Vertex (batch), embeddingi, OCR |
| 6 | Okresowo | AI Studio / Gemini API free tier | sekcja 1.3 | Używać tam, gdzie jakość wystarcza (opcja) |

**Reguła przypomnień (preset, odwracalny):** koordynator podnosi temat **90, 60, 30, 14 i 7 dni** przed każdym terminem (dla kredytów miesięcznych: 5 dni przed końcem miesiąca rozliczeniowego, jeśli są wolne limity). Kredyt B ma najdłuższą drogę (ustalenie zakresu, ew. przepięcie projektu), dlatego zaczynamy od 90 dni. Dla kredytu o niepotwierdzonej dacie liczymy od najwcześniejszej możliwej i przy każdym przypomnieniu prosimy o potwierdzenie z Console. Wartości są presetem; właściciel zmienia je lub wyłącza (miejsce zapisu do decyzji koordynatora, np. plik presetów obok `tools/pace_plan.json`).

Zasada ogólna: „nie zdążymy wydać” nie uzasadnia marnowania. Gdy nie ma pożytecznego zadania w zakresie kredytu, zgłaszamy to właścicielowi jako pytanie tak/nie, zamiast sztucznie generować obciążenie.

---

## 1. Google

### 1.1 Konto A: Google Developer Program, „premium benefit” (7 pozycji, 59,97 EUR)

Fakty:
- Oficjalny FAQ korzyści GDP: „Cloud credits may be spent on any Google Cloud Platform or Google Maps Platform product”, wygasają rok po przyznaniu, bez przenoszenia, **każdy kredyt można zastosować tylko do jednego konta rozliczeniowego; zastosowanego nie da się przenieść** `[oficjalne]` https://developers.google.com/profile/help/benefits .
- Osobny „GenAI developer credit” (50 USD rocznie) działa **tylko** w AI Studio i Vertex AI i tylko dla modeli Google GenAI (Gemini 2.0+, Imagen, Veo) `[oficjalne]` tamże.
- Strona planów (2026-10-24): Premium 19,99 USD/mies., „10 USD GenAI and Cloud credit per month”, 35 kredytów Google Skills/mies. (GEAR), Antigravity, wliczone w Google AI Pro `[oficjalne]` https://developers.google.com/program/plans-and-pricing . Starsze wersje: 24,99 USD/mies. i 45 USD `[oficjalne, historyczne]` https://developers.googleblog.com/google-developer-program-join-connect-code/ .
- Dopasowanie: 7 pozycji i 59,97 EUR to ok. 7 × 10 USD `[niezweryfikowane: arytmetyka]`.
- Zgłoszenia, że „Developer Program credit” widać w Billing, a AI Studio mimo to prosi o doładowanie przedpłaty `[forum]` https://discuss.ai.google.dev/ : nie każde użycie Gemini API zużywa ten kredyt.

**Nieznane:** czy konkretne pozycje obejmują Compute Engine, Cloud Run, Workflows, Gemini API. Oficjalny tekst jest szeroki, ale pozycja „GenAI and Cloud” może mieć węższe warunki. **Rozstrzygnięcie bez kosztu:** Konsola, Billing, Credits, kolumny usług/warunków przy każdej pozycji; po pierwszym rachunku w Billing, Reports, widać pozycję „Promotions and others”.

Zastosowania (przykłady):
- Pokrycie kosztów konta A (Compute Engine, Cloud Run, Workflows, Vertex AI), o ile zakres to obejmuje; koszt VM Spot jest niewielki, więc nie jest to cel optymalizacji.
- Embeddingi/OCR/ASR na Vertex AI, Cloud Run dla serwera Loom przy sporadycznym ruchu.

Korzyści GDP Premium poza kredytami `[oficjalne]` (ten sam FAQ i strona planów): voucher certyfikacyjny Google Cloud (1 rocznie; miesięczny abonent po 6 mies.), po zdaniu bonus 500 USD kredytu, konsultacje ekspertów (1 h albo 2 × 30 min rocznie, po angielsku), Firebase Studio 30 przestrzeni, Google Skills, Antigravity, prywatne podglądy. **Uwaga o zmianie planów:** dla kont osobistych z samodzielnym Premium automatyczne odnowienie kończy się 2026-06-30 (miesięczne) lub 2026-03-30 (roczne); Google AI Pro/Ultra wliczają 10/40/100 USD miesięcznego kredytu Cloud, ale bez vouchera i konsultacji. Warto sprawdzić, w którym trybie jest konto, bo od tego zależy, czy pozycje nadal przybywają. Kontakt: gdp-premium-support@google.com.

### 1.2 Konto B: „Trial credit for GenAI App Builder”, 855,05 EUR, do 2027-05-23, „Not applicable to Gemini API”

Żadna oficjalna strona nie opisuje tego kredytu. Stan wiedzy:
- Wsparcie Google (relacja z forum): kredyt „zostanie zastosowany tylko do usług Vertex AI Search”, z odnośnikiem do grupy SKU `vertex-genai-offer-2025`; inny użytkownik dostał grupę „Vertex AI Search and Conversation” `[forum]` https://discuss.ai.google.dev/t/no-one-is-talking-about-how-to-use-the-credits-of-trial-credit-for-genai-app-builder/101423 . Użycie z Gemini API / AI Studio kończyło się obciążeniem karty `[forum]`, co zgadza się z adnotacją w Konsoli.
- **Oficjalna** grupa SKU `vertex-genai-offer-2025` ("[Deprecate] Vertex GenAI Offer 2025") obejmuje: Custom Search, Programmable Search Element, Recommendations AI, Retail API Search oraz **szeroko Vertex AI: Gemini 2.0 Flash/Flash Lite, 2.5 Flash/Flash Lite/Pro (wejście/wyjście, batch, cache), embeddingi tekstowe i multimodalne, Provisioned Throughput** `[oficjalne]` https://cloud.google.com/skus/sku-groups/vertex-genai-offer-2025 . Strona zastrzega, że lista może być przywoływana w umowach i nie mówi, które kredyty z niej korzystają.
- Wniosek roboczy: zakres to (a) wąski: Vertex AI Search / Conversation / Discovery Engine / Agent Search, albo (b) szeroki: grupa powyżej z **Gemini na Vertex AI** (co innego niż Gemini API z AI Studio, więc adnotacja „Not applicable to Gemini API” nie wyklucza Gemini na Vertex). Rozstrzygnięcie: kolumna „Eligible products/SKUs” przy pozycji w Billing, Credits (odczyt, 0 zł); w razie braku jedno testowe wywołanie Vertex AI na koncie B za kilka centów i sprawdzenie w Reports (pytanie 1).
- **Użycie z projektu na innym koncie rozliczeniowym:** kredyt pokrywa koszty tylko **na koncie B**. Dla GDP oficjalnie „jeden kredyt, jedno konto, bez przenoszenia” `[oficjalne]`; dla trial credit zasada ogólna GCP jest taka sama `[niezweryfikowane dla tej pozycji]`. Czyli: albo przepiąć **projekt** na konto B (Billing, Account management, Change billing; role Billing Account User na B i Project Billing Manager na projekcie), albo założyć projekt na B i uruchomić usługę tam. Przepięcie odwraca się tą samą operacją; ryzyko: usługi projektu na chwilę bez rozliczenia.

Zastosowania (przykłady): indeks Vertex AI Search po eksportach rozmów lub dokumentach WatchDoga (dane do Google, pytanie 3); jeśli szeroki zakres: batch semantic analysis (Gemini 2.5 Flash batch), embeddingi całego korpusu, OCR obrazów; jednorazowe duże zadania (re-analiza korpusu, ewaluacje).

**Burn rate (szacunek; założenia jawne; ceny `[niezweryfikowane]`, sprawdź https://cloud.google.com/vertex-ai/generative-ai/pricing):** Gemini 2.5 Flash na Vertex ok. 0,30 USD/M wejścia i ok. 2,50 USD/M wyjścia, batch ok. połowa. Zadanie: 100 000 wiadomości × 800 tokenów wejścia + 200 wyjścia = 80 M + 20 M tokenów, w batchu ok. 12 + 25 = ok. 37 USD. 855 EUR to więc ok. 20 pełnych przejść korpusu; bez celowego dużego zadania kredyt nie zostanie „przepalony”. Embeddingi: rząd kilku USD na milion dokumentów.

### 1.3 AI Studio / Gemini API (free tier)

Darmowy poziom Gemini API ma limity zależne od modelu (zapytania/min, zapytania/dzień, tokeny) i dane z różnych źródeł są niespójne (nazwy modeli, 60 vs 10 zapytań/min) `[forum]` https://blogs.novita.ai/free-llm-api-comparison-2026/ . Limity widać na żywo w AI Studio (Usage / rate limits) i w błędzie 429. Rozliczanie projektu AI Studio na koncie z kredytem: kredyt GDP „GenAI” ma w FAQ działać w AI Studio `[oficjalne]`, a trial B nie; zgłoszenia o „No available credits” mimo kredytu `[forum]`. Dla ChatADHD to opcja darmowego modelu semantycznego (preset, nie wymóg), z uwagą, że darmowy poziom może dopuszczać użycie danych do ulepszania produktu `[niezweryfikowane: sprawdzić warunki, to pytanie o prywatność]`.

### 1.4 Anthropic API (placeholder, ok. 100 USD)

Saldo i data: Console, Plans & Billing `[oficjalne]` https://www.anthropic.com/legal/credit-terms . Typowy termin: rok od zakupu/wydania (sekcja 0). API salda: nie znaleziono `[niezweryfikowane]`; jest raport kosztów `GET https://api.anthropic.com/v1/organizations/cost_report` z kluczem Admin (organizacja wymagana), parametry `starting_at`, `ending_at`, `bucket_width=1d`, `group_by[]=description|workspace_id`, `limit` 1–31, `page`; odpowiedź `data[].results[]{amount (string, najmniejsze jednostki), currency "USD", model, token_type, cost_type, ...}`, `has_more`, `next_page` `[oficjalne]` https://platform.claude.com/docs/en/api/admin/cost_report . Saldo = kwota z Console minus suma kosztów. Głębsze badanie (programy badawcze/startupowe) po zalogowaniu właściciela.

---

## 2. GitHub (konto `klb-t`) i krótka lista pozostałych

### 2.1 Stan konta (dane koordynatora, 2026-10-24)

Repozytoria **publiczne**: chatadhd, AGEDS, Watchdog-JH16, loom, Custom-Keyboard-Pro, LEM-Workbench. **Prywatne**: ar-drafty. Plan konta i zużycie Actions nieznane: token `gh` nie ma zakresu `user`, więc `/users/klb-t/settings/billing/*` zwraca 404 (zasób ukryty). Odczyt: https://github.com/settings/billing (po zalogowaniu) albo `gh auth refresh -s user`, które poszerza zakres tokenu (zmiana uprawnień: do decyzji właściciela, pytanie 5).

### 2.2 Co dają limity (fakty `[oficjalne]` z dokumentacji, 2026-10-24)

| Obszar | Fakt | Źródło |
|---|---|---|
| Actions, repozytoria **publiczne** | Standardowe runnery hostowane przez GitHub są **bezpłatne i bez limitu minut** | https://docs.github.com/en/billing/managing-billing-for-your-products/about-billing-for-github-actions |
| Actions, repozytoria **prywatne** | Free 2 000 min/mies., Pro 3 000, Team 3 000, Enterprise 50 000; ponad: Linux 2-core x64 0,006 USD/min, macOS 0,062 USD/min | tamże |
| Larger runners | **Zawsze płatne, także w repozytoriach publicznych**; dostępne tylko w organizacjach z planem Team/Enterprise | tamże; https://docs.github.com/en/actions/reference/runners/larger-runners |
| Runnery GPU | Larger runners GPU też płatne w publicznych repozytoriach: Linux 4-core GPU 0,052 USD/min, Windows 0,102 USD/min (wg strony cenowej) `[oficjalne, wersja z wyszukiwania]`; tylko Team/Enterprise | https://docs.github.com/en/billing/reference/actions-runner-pricing |
| Cache | **10 GB na repozytorium**, wszystkie plany, osobno od artefaktów; ponad: 0,07 USD/GB-mies.; wpisy nieużywane >7 dni są usuwane; po przepełnieniu eviction od najstarszego; cache gałęzi dziecka niedostępny dla rodzeństwa; limity operacji 200 upload/1 500 download na min | https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching ; https://docs.github.com/en/actions/reference/limits |
| Artefakty i Packages | Wspólna pula: Free 500 MB, Pro 1 GB (Team 2 GB); dotyczy prywatnych; **Packages publiczne są bezpłatne**; ponad: 0,25 USD/GB-mies.; transfer Packages Free 1 GB, Pro 10 GB (prywatne) | https://docs.github.com/en/billing/managing-billing-for-your-products/about-billing-for-github-packages |
| Limity jobów | Job: **6 h** na runnerze hostowanym (self-hosted 5 dni), workflow run: 35 dni; domyślnego `timeout-minutes` strona nie podaje (praktycznie 360 min). Równoległe joby na standardowych runnerach: Free 20, Pro 40; macOS max 5 | https://docs.github.com/en/actions/reference/limits |
| Codespaces | Konto osobiste: **Free 120 godzin obliczeń i 15 GB-mies. dysku; Pro 180 h i 20 GB-mies.**; czas liczony razy liczba rdzeni; bez karty zużycie blokuje się po wyczerpaniu | https://docs.github.com/en/billing/managing-billing-for-your-products/managing-billing-for-github-codespaces/about-billing-for-github-codespaces |
| Copilot | Free: 2 000 uzupełnień/mies., tylko auto-wybór modelu, część agentów niedostępna, dodatek „AI credits” o niepodanej wielkości; Pro 10 USD (1 500 kredytów), Pro+ 39 USD (7 000), Max 100 USD (20 000); Student bezpłatny dla zweryfikowanych; nauczyciele i opiekunowie popularnych projektów OSS mogą dostać Pro za darmo | https://docs.github.com/en/copilot/get-started/plans ; https://docs.github.com/en/copilot/how-tos/copilot-on-github/set-up-copilot/enable-copilot/set-up-for-teachers-and-os-maintainers (wstrzymanie nowych rejestracji od 2026-04-20 `[oficjalne w docs, nie zweryfikowane na stronie planów]`) |

Wniosek: dla repozytoriów publicznych praktyczne ograniczenia to cache 10 GB, 6 h na job, równoległość 20 jobów i brak GPU w darmowej puli, nie liczba minut. W repozytorium prywatnym (ar-drafty) obowiązuje pula minut.

Dodatkowe fakty `[oficjalne]`, odczytane 2026-10-24 (lista otwarta, bez wyceny oszczędności na VM, bo Spot kosztuje niewiele):
- Runnery standardowe w repozytoriach publicznych: 4 vCPU, 16 GB RAM, 14 GB SSD (x64 i arm64), prywatne 2 vCPU/8 GB: https://docs.github.com/en/actions/reference/runners/github-hosted-runners
- **GitHub Models (darmowe API LLM) wyłączone 2026-07-30**: nie planować na nim analizy semantycznej ani embeddingów (okno ważności: sprawdzić ponownie przy planowaniu): https://docs.github.com/en/github-models/use-github-models/prototyping-with-ai-models
- Pages: strona do 1 GB, miękko 100 GB transferu/mies., 10 buildów/h, nie jako hosting biznesu/SaaS ani na dane wrażliwe: https://docs.github.com/en/pages/getting-started-with-github-pages/github-pages-limits
- Releases: plik poniżej 2 GiB, do 1 000 plików, bez limitu sumy i transferu (APK, binarki): https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases
- GHCR/Packages: pakiety publiczne bezpłatne (wyżej).
- Dependabot, CodeQL, secret scanning i push protection: bezpłatne w repozytoriach publicznych, w prywatnych (ar-drafty) poza Dependabotem wymagają planów płatnych: https://docs.github.com/en/code-security/getting-started/github-security-features
- Copilot coding agent: dokumentacja mówi o „kwalifikujących się prywatnych i wewnętrznych repozytoriach”; plany i zużycie niezweryfikowane: https://docs.github.com/en/copilot/concepts/agents/coding-agent/about-coding-agent
- Dla OSS: Secure Open Source Fund (10 000 USD przez Sponsors, sesja 5 miała termin przed 2026-08-24, bieżący harmonogram nieznany; składnik kredytów Azure sporny) https://github.blog/news-insights/company-news/announcing-github-secure-open-source-fund/ ; Copilot Pro dla opiekunów popularnych projektów OSS (warunki do sprawdzenia). Student/Education nie dotyczy. Gists, Discussions, Projects: bezpłatne, limity niezbadane (Projects może służyć jako kolejka).

### 2.3 Jak używać Actions oszczędnie w naszym układzie

Stan obecny w repozytorium (odczyt `.github/workflows/`): `concurrency` z `cancel-in-progress: true` jest już w `python.yml`, `package.yml`, `android-smoke.yml`, `loom.yml`; `timeout-minutes` ustawione na jobach (python 10, xport 15, loom 45 / asan 150, package 120/60/30, android-smoke 120); ccache przez `actions/cache@v4` w `package.yml` i `android-smoke.yml`. Do rozważenia (przykłady, nie wymogi):

2. **Timeouty.** Utrzymać `timeout-minutes` na każdym jobie; ustawić go blisko mierzonego czasu z zapasem (ostatnio podnoszone ręcznie: asan 150→240 min wg commita `51315dd6`). Limit systemowy to 6 h/job, więc sufit jest poza zasięgiem; nie musi być sztywną regułą (preset).
3. **Concurrency.** `cancel-in-progress` jest włączone; osobna grupa na gałąź/PR (`group: ${{ github.workflow }}-${{ github.ref }}`) zapobiega zużyciu równoległości (20 jobów) przez nieaktualne commity. Uwaga: nie anulować przebiegów na `main`, jeśli mają publikować artefakty.
4. **Cache.** Jedna rodzina kluczy na (system, preset), `restore-keys` po prefiksie; sprawdzać statystyki ccache (są w workflow). Kluczem `${{ github.sha }}` powstaje nowy wpis przy każdym commicie; przy 10 GB i eviction od najstarszego warto ograniczyć zapis cache do `main` (cache z gałęzi dziecka i tak nie służy rodzeństwu) `[wniosek z reguł zasięgu, oficjalne]`.
5. **Artefakty.** Dla publicznych repozytoriów wspólna pula 500 MB dotyczy prywatnych; w publicznych retencję i rozmiar i tak warto skracać (`retention-days`), żeby nie trzymać zbędnych (decyzja w danych presetu).
6. **GPU / duże obliczenia:** w darmowej puli brak; ASR/embeddingi lokalnie na VM albo kredyty Google (sekcja 1).
7. **Codespaces** jako alternatywna maszyna dla agentów: 120 h Free, czas razy rdzenie, więc 8 rdzeni = ok. 15 h wallclock `[wniosek z reguły mnożenia]`.

### 2.4 Pozostali dostawcy: krótka lista (otwarta; głębsze badanie po zalogowaniu)

| Dostawca | Gdzie sprawdzić saldo | API salda |
|---|---|---|
| Anthropic | https://console.anthropic.com/settings/billing | cost_report (wydatki), salda brak |
| OpenAI | https://platform.openai.com/settings/organization/billing | brak salda `[forum]` |
| OpenRouter | https://openrouter.ai/settings/credits | tak: `GET /api/v1/credits` (klucz zarządzający), `GET /api/v1/key` `[oficjalne]` https://openrouter.ai/docs/api/api-reference/credits/get-credits |
| DeepSeek | https://platform.deepseek.com/usage | `GET /user/balance` `[forum]` |
| AWS | https://console.aws.amazon.com/billing/home#/credits | brak salda; Cost Explorer koszty |
| Azure | https://portal.azure.com (Cost Management) | brak salda |
| Hugging Face (`klb-t`) | https://huggingface.co/settings/billing | `[niezweryfikowane]` |
| W&B | https://wandb.ai/ | `[niezweryfikowane]` |
| Lambda | https://cloud.lambda.ai/ | `[niezweryfikowane]` |
| Modal | https://modal.com/settings | `[niezweryfikowane]` |
| RunPod | https://www.runpod.io/console/user/billing | GraphQL `myself { clientBalance }` `[oficjalne pole, kształt niezweryfikowany]` |
| Together, Replicate, Groq, Cloudflare, Mistral, Fireworks, Cerebras, NVIDIA, Novita, Colab/Kaggle | konsole dostawców | brak potwierdzenia |

Ukryte miejsca do odczytu bez kosztu od razu: Billing, Credits w obu kontach GCP i w każdym innym projekcie Google; „My Benefits” w profilu GDP; Console Anthropic; https://github.com/settings/billing; (za zgodą) poczta ze słowami „credit/promo/voucher”.

---

## 3. Propozycja rozszerzenia `tools/cloud/login_all.sh` (specyfikacja, bez kodu)

**Cel:** po zalogowaniu (oraz przy `--status`) odczytać saldo/zużycie tam, gdzie API istnieje; gdzie nie, wypisać link do ręcznego odczytu. Wynik: jedna linia na dostawcę w podsumowaniu (✔/·/✘ jak dziś) z dopiskiem salda i terminu. Odczyt tylko GET do własnego dostawcy, brak wysyłania danych użytkownika. Flaga `--balance` (preset: włączona przy `--status`; `--no-balance` wyłącza). Brak API albo błąd to **advisory**, nie stop. Do `PROVIDERS` dodać 7. pole `balance_url`.

| Dostawca | Wywołanie | Odpowiedź do wyciągnięcia | Uwagi |
|---|---|---|---|
| GitHub | `gh api /user` (login, plan wymaga zakresu `user`); `gh api /users/klb-t/settings/billing/actions` (po `gh auth refresh -s user`); `gh api /rate_limit` | `plan.name`; `total_minutes_used`, `included_minutes`, `minutes_used_breakdown` `[niezweryfikowane: kształt]` | Bez zakresu `user` zwraca 404: advisory „odśwież token: gh auth refresh -s user” (z pytaniem), zamiast cichego ominięcia. Publiczne repozytoria: minuty nie są liczone |
| GCP | `bq query` na tabeli eksportu `gcp_billing_export_v1_*`: suma `credits[].amount` po `credits[].name` w okresie | zużycie kredytów po nazwie | Wymaga włączonego eksportu Billing→BigQuery (pytanie 6). Bez: `gcloud billing accounts list` + link do Billing, Credits; saldo tylko w Console `[niezweryfikowane: brak endpointu]` |
| OpenRouter | `GET https://openrouter.ai/api/v1/key` (Bearer) | `data.limit_remaining`, `data.usage`, `data.is_free_tier`, `data.expires_at`; `GET /credits` (klucz zarządzający): `data.total_credits − data.total_usage` | 403 dla zwykłego klucza w `/credits`: advisory |
| Anthropic | `GET https://api.anthropic.com/v1/organizations/cost_report?starting_at=…&bucket_width=1d&limit=31`, nagłówki `x-api-key: $ANTHROPIC_ADMIN_KEY`, `anthropic-version: 2023-06-01` | suma `data[].results[].amount` | Klucz Admin osobny i opcjonalny; saldo = kwota kredytu z pliku danych minus wydatek |
| DeepSeek | `GET https://api.deepseek.com/user/balance` | `is_available`, `balance_infos[].total_balance/granted_balance/topped_up_balance` (stringi) | Ścieżkę z `/v1` i bez sprawdzić |
| RunPod | `POST https://api.runpod.io/graphql`, `{"query":"query{myself{clientBalance currentSpendPerHr}}"}` | `data.myself.clientBalance` | Kształt do potwierdzenia |
| Pozostali | – | – | „saldo: sprawdź ręcznie” + `balance_url` |

Plik danych terminów (źródło dla przypomnień z sekcji 0), np. `~/.config/chatadhd/credits.json`: pozycje `{provider, amount, currency, expires[], source, rating}`; przykład: `{"provider":"gcp-A","amount":"59.97","currency":"EUR","expires":["2027-03-24","2027-05-23"],"source":"console","rating":"owner"}`. Klucze nie są logowane; brak `curl`/`jq`/`bq` to pominięcie z komunikatem w podsumowaniu.

---

## 4. Pytania tak/nie do właściciela

(Tylko pieniądze, nowi odbiorcy prywatnych danych, zmiany uprawnień. Przeszły test filozofii: reszta to decyzje techniczne koordynatora.)

1. **Pieniądze, konto B:** czy wolno wykonać jedno testowe wywołanie Vertex AI na koncie B za kilka centów, żeby ustalić, czy kredyt 855 EUR pokrywa Gemini na Vertex? (pomija się, jeśli kolumna zakresu w Billing, Credits to rozstrzygnie)
2. **Przepięcie projektu:** czy wolno przepiąć wskazany projekt (np. nowy, nie VM) na konto rozliczeniowe B, jeśli test z p. 1 wypadnie pomyślnie? (odwracalne)
3. **Dane do Google:** czy kredyt B może być użyty z danymi z Twoich rozmów (Vertex AI Search / Gemini na Vertex, czyli Google jako odbiorca danych, których dziś tam nie wysyłamy)? Domyślnie: tylko dane testowe lub publiczne.
5. **Uprawnienia GitHub:** czy rozszerzyć zakres tokenu `gh` o `user` (`gh auth refresh -s user`), żeby odczytać plan i zużycie Actions? (odczyt, ale szerszy token)
6. **Eksport Billing do BigQuery:** czy włączyć, żeby `login_all.sh` pokazywał zużycie kredytów GCP? (niewielki koszt przechowywania `[niezweryfikowane]`)

Domyślnie bez odpowiedzi koordynator nic z tego nie robi; wykonuje tylko odczyty w Konsoli wskazane przez właściciela i trzyma przypomnienia z sekcji 0.

---

## Źródła (dostęp 2026-10-24)

- https://developers.google.com/profile/help/benefits, https://developers.google.com/program/plans-and-pricing, https://developers.googleblog.com/google-developer-program-join-connect-code/, https://cloud.google.com/skus/sku-groups/vertex-genai-offer-2025 `[oficjalne]`
- https://discuss.ai.google.dev/t/no-one-is-talking-about-how-to-use-the-credits-of-trial-credit-for-genai-app-builder/101423, https://blogs.novita.ai/free-llm-api-comparison-2026/ `[forum]`
- https://docs.github.com/en/billing/managing-billing-for-your-products/about-billing-for-github-actions, https://docs.github.com/en/actions/reference/limits, https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching, https://docs.github.com/en/billing/reference/actions-runner-pricing, https://docs.github.com/en/actions/reference/runners/larger-runners, https://docs.github.com/en/billing/managing-billing-for-your-products/about-billing-for-github-packages, https://docs.github.com/en/billing/managing-billing-for-your-products/managing-billing-for-github-codespaces/about-billing-for-github-codespaces, https://docs.github.com/en/copilot/get-started/plans `[oficjalne]`
- https://www.anthropic.com/legal/credit-terms, https://support.anthropic.com/en/articles/8977456-how-do-i-pay-for-my-api-usage, https://platform.claude.com/docs/en/api/admin/cost_report `[oficjalne]`
- https://openrouter.ai/docs/api/api-reference/credits/get-credits, https://openrouter.ai/docs/api/api-reference/api-keys/get-current-key `[oficjalne]`
