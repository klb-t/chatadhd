# 7A / 7B / 7C — wspólne rzeczywiste źródła

Stan 2026-10-05. Obowiązuje wyłącznie eksperymentowanie na rzeczywistych eksportach. Jedyny płatny wykonawca i prywatny rejestr pozostają w 7A. Ten dokument nie uruchamia żadnych żądań.

Paczka `Thread7_REAL_SOURCE_FREEZE_20261005.zip` jest dostępna w prywatnej Library właściciela. Dokładny hash, status i odnośniki są w [COORDINATION.json](COORDINATION.json). Nie umieszczać paczki ani rozmów na GitHubie.

Domknięto odbiór źródeł: 3 pełne wybrane rozmowy, 44 wiadomości; 13 plików starego freeze zgodnych. Dodano [zweryfikowany importer i grafową projekcję](real-source-recovery-20261005/README.md), 37/37 testów. Usunięto zależność od utraconych ścieżek `/workspace` starego preparera.

**Ważne:** w 2/3 rozmów stara lista tur nie była chronologiczna: razem 20 odwróconych krawędzi rodzic–dziecko. Nowa pochodna respektuje natywne relacje, role i czas. Zachowuje wszystkie wiadomości i dawne identyfikatory tur. Oryginały oraz osiem wcześniej przygotowanych żądań są nienaruszone. Nie podmieniać body pod historycznymi hashami; nowe żądania muszą jawnie wskazywać nową projekcję.

7B/7C nie muszą już czekać na znalezienie źródła. Pozostają: recenzowany gold z rzeczywistych wypowiedzi, nowe wersje manifestów/żądań i preflight kosztu w 7A. Trzy rozmowy nie spełniają starego planu 24 rodzin / 12 PL + 12 EN; nie fabrykować brakujących rodzin ani nie dziedziczyć syntetycznych etykiet. Zachować null dla niezmierzonej jakości.

Publicznie publikować tylko jawną projekcję hashy/liczności/stanu. Pełne źródło, żądania, odpowiedzi i locatory pozostają prywatne. W tym przyroście: 0 wywołań, 0 USD, bez zmian C++, UI, STATE i głównego README.
