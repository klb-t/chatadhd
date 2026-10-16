# Model lokalny (offline) — R37

ChatADHD może rozmawiać z modelem uruchomionym na tym samym urządzeniu: telefonie, laptopie albo serwerze.
Aplikacja nie ma wbudowanego silnika. Łączy się z dowolnym serwerem zgodnym z API OpenAI
(`/v1/chat/completions`, `/v1/models`) i nie wysyła mu żadnego klucza.

## Ustawienia

| Ustawienie | Wartość |
|---|---|
| `llm_provider` | `local` (preset: `openrouter`) |
| `local_model_base_url` | adres serwera, preset `http://127.0.0.1:8080/v1` (llama.cpp); Ollama: `http://127.0.0.1:11434/v1` |
| `default_model` | nazwa modelu, którą zna serwer (Ollama jej wymaga, np. `qwen2.5:3b`; llama.cpp ją ignoruje) |

Gdy serwer nie odpowiada, czat wyświetla komunikat „The local model at … does not answer…”, który mówi, co zrobić.
Powrót do dostawcy w chmurze: `llm_provider = openrouter`.

## Android: jak postawić serwer (krótko)

1. **Termux** (z F-Droid): `pkg install llama-cpp` (albo zbuduj llama.cpp ze źródeł), pobierz model GGUF
   (np. mały model 1–4B, kwantyzacja Q4) i uruchom:
   `llama-server -m model.gguf --host 127.0.0.1 --port 8080`
2. **Ollama** w Termux (`pkg install ollama`): `ollama serve`, w drugiej sesji `ollama pull qwen2.5:3b`.
   Potem `local_model_base_url = http://127.0.0.1:11434/v1` i `default_model = qwen2.5:3b`.
3. W ChatADHD ustaw `llm_provider = local`.

Wydajność zależy od telefonu. Model 1–3B na nowszym telefonie odpowiada płynnie, większe są wolne.
Wbudowanie silnika w APK to osobna decyzja i nie jest częścią R37.
