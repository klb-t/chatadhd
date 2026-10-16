# Ekonomia tokenów — pierwszy punkt pomiarowy (2026-10-16, ~16:30 UTC)

Właściciel (2026-10-16): „pytałem czy przebiegi agentyczne też są w archiwach, to by się zrobiło ilościową analizę ekonomii tokenów”; „API żeby te transkrypty pobrać”. Odpowiedź: żadnego dodatkowego API — liczniki per sesja daje API sesji Claude Code (`list_sessions`, pole `usage`), liczniki per tura leżą w transkrypcie JSONL każdej sesji w jej kontenerze (format jak lokalne Claude Code: `message.usage` przy każdej odpowiedzi asystenta). Koszt to równowartość wg cennika API; właściciel płaci abonament Max, więc jest to miara zużycia, nie rachunek.

## Sesje na koncie (zmierzone, API sesji)

| Sesja | Okres | Cache read | Cache write | Wejście | Wyjście | Koszt wg cennika API | Commity (stopka sesji) |
|---|---|---|---|---|---|---|---|
| S1 „ChatADHD C++ Loom core” (session_015yMVYJqXDd8sNSZdYq3gu5) | 12–16.10 | 3 298 114 597 | 95 182 321 | 170 173 | 12 668 627 | 1 102,73 USD | 386 (375 na main) |
| S2 „równoległe wątki” (session_01Armc9PkDiTDzjFBgXAMCjR) | 13–16.10 | 1 102 736 487 | 70 633 929 | 76 162 | 2 391 699 | 407,97 USD | 138 (138 na main) |
| Port ChatADHD → Loom (session_01WQHsQsjm2Ncn3EeV7fZet1) | 20.02–11.10 | 1 425 413 641 | 45 525 643 | 280 672 | 6 610 510 | 644,77 USD | 220 (18 bezpośrednio na main; reszta przez scalenia gałęzi) |
| T9 synteza ekosystemu | 12.10 | 9 839 363 | 801 194 | 1 001 | 133 254 | 25,16 USD | — |
| T10/T14 edge cases | 12.10 | 8 210 382 | 556 724 | 1 006 | 105 006 | 18,45 USD | — |
| 4 krótkie sesje S2 (smoke R19 ×3, klucz) | 15–16.10 | ~7,1 mln | ~0,37 mln | ~7 tys. | ~26 tys. | 4,91 USD | — |
| Custom-Keyboard-Pro | 11–25.09 | 1 851 358 434 | 29 756 561 | 152 974 | 5 300 682 | 1 233,69 USD | (inne repo) |
| Watchdog-JH16 | 16.08–7.10 | 381 556 767 | 11 770 225 | 44 438 | 1 163 935 | 288,02 USD | (inne repo) |

Razem ChatADHD/Loom: ~2 204 USD; pozostałe projekty: ~1 522 USD.

## Co z tego wynika (pierwsze obserwacje, do sprawdzenia na danych per tura)

1. **Cache read to 95–97 % wolumenu tokenów** w każdej sesji roboczej. To ten strumień wyczerpuje okna limitów (5-godzinne i tygodniowe), nie wyjście modelu. Każdy obrót pętli głównej S1 czyta ~350 tys. tokenów kontekstu z cache; każdy agent — swój kontekst od nowa.
2. **Koszt na commit na main jest niemal identyczny w S1 i S2**: 1 102,73 / 375 = **2,94 USD**; 407,97 / 138 = **2,96 USD** — mimo że S1 prowadzi panele i workflow, a S2 pojedynczych agentów. Hipoteza: różnią się wielkością commitów, nie sprawnością; do zmierzenia liniami i pozycjami pracy, nie commitami.
3. **Pętla główna S1 to tylko ~30 % zużycia tej sesji**: transkrypt główny (9 771 zdarzeń, 2 518 tur asystenta) ma 976 mln cache read i 1,96 mln wyjścia wobec 3,30 mld i 12,7 mln dla całej sesji — ~70 % cache read i ~85 % wyjścia przypada na subagentów i workflow (transkrypty w `subagents/`). Optymalizacja tempa = optymalizacja agentów (mniejsze konteksty, mniej równoległych od zera), nie rozmowy.
4. Model w pętli głównej S1: 2 173 tur Opus 5.5, 333 tur Fable 5.1 (po przełączeniu przez właściciela 16.10).

## Czego tu nie ma

- GPT (ChatGPT Work, czat, Codex): eksport ChatGPT nie zawiera liczników ani ukrytego rozumowania; Codex chmurowy nie jest w eksporcie. Tam zostaje szacunek tokenizerem + cennik z datą, z etykietą „oszacowane”.
- Koszt na wynik (commit, plik, decyzja) per tura: wymaga przejścia transkryptów JSONL — plaster 4 X-Port (P38 nr 37). S2 przeliczy swój transkrypt u siebie tym samym narzędziem i odda liczby.

## Metoda i powtarzalność

Źródła: `mcp__claude-code-remote__list_sessions` (usage per sesja, stan 2026-10-16 16:25 UTC); `git log --all --format=%b | grep "Claude-Session: …<id>"` (commity per sesja); transkrypt `/root/.claude/projects/-home-user-chatadhd/<session>.jsonl` (usage per tura). Liczby cache/wyjście dla krótkich sesji S2 zsumowane z czterech wpisów. Następny punkt pomiarowy: po Bramie 0 M1.
