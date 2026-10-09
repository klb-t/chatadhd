# Cache i kolejność — sprawdzenie oficjalnych źródeł 2026-10-09

Źródła sprawdzone tego dnia:

- https://openrouter.ai/docs/guides/best-practices/prompt-caching
- https://openrouter.ai/docs/guides/features/response-caching
- https://openrouter.ai/docs/guides/routing/provider-selection
- https://developers.openai.com/api/docs/guides/prompt-caching
- https://platform.claude.com/docs/en/build-with-claude/prompt-caching

OpenRouter rozdziela cache odpowiedzi od cache prefiksu dostawcy. Nagłówek
`X-OpenRouter-Cache: false` wyłącza ponowne użycie odpowiedzi, również dla
presetu z włączonym cache. HIT może dostać nowe generation ID: unikalne ID
nie dowodzi niezależnego inference. Gotowa odpowiedź z cache nie jest nową
próbą. Replay zapisanych odpowiedzi także pozostaje replay.

Sticky routing jest zależny od modelu i rozmowy; może przełączyć dostawcę
przy niedostępności. `session_id` pomaga kierowaniu, nie gwarantuje cache.
Nie zakładamy współdzielenia między modelami, endpointami ani dostawcami.
Zapisujemy faktyczne cache read/write z usage/generation; rabat może być ujemny
przy zapisie. Nie wyceniamy fazy z założenia, że każda próba trafi w cache.

`provider.require_parameters=true` ogranicza routing do obsługujących
parametry dostawców. Bez tego część parametrów może zostać zignorowana.
Nowy panel zachowuje dokładne odzyskane body; obecna dostępność/obsługa
endpointu wymaga świeżego preflight. Brak dowodu obsługi/pominięcia parametru
jest null, nie domniemaniem skuteczności. Seed nie gwarantuje identycznego inference.

Narzędzie nie zmienia układu badanego promptu dla cache. Grupowanie używa
hasha modelu, jawnego routing provider, endpointu, route i wybranych pełnych
wiadomości prefiksu. To planowana zgodność, nie obserwowany cache hit.
`balanced_blocks` leniwie rotuje pozycję wariantu między przypadkami i
powtórkami. To kontrola pozycji, nie pełna randomizacja/kontrobilansowanie
wszystkich efektów kolejności. `prefix_grouped` jest osobną jawną strategią;
globalne sortowanie materializuje wybrany zakres. Kolejność wykonania jest
zapisana i przy analizie należy raportować cold/warm i zmianę endpointu.

Pełne body pozostaje prywatne. Istniejący transport obsługuje
`headers_by_method.POST`; przy przyszłej fazie należy dodać wskazany wyżej
nagłówek do prywatnej polityki wykonawcy, zamrozić zmianę i zachować świadectwo
odpowiedzi. Nie zmieniamy historycznych requestów ani zapisanych polityk.
