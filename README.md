# ChatADHD

Multi-model AI chat client for minds that branch, jump, and never quite finish the previous thought.

## Features (Faza 1 - MVP)

- ✅ Multi-model chat (Claude, GPT, Gemini, DeepSeek, Grok, Mistral, Llama...)
- ✅ SQLite local storage
- ✅ Conversation history
- ✅ Model switching w trakcie rozmowy
- ✅ Temperature control
- ✅ Streaming responses
- ✅ **Branching**: edycja tworzy gałąź, nie kasuje
- ✅ **Exclude/Include**: odznaczanie wiadomości z kontekstu
- ✅ **Tagging**: tagowanie wiadomości
- ✅ **Tree view**: widok drzewa konwersacji

## Planned Features (Future Phases)

- 🔲 Dołączanie wiadomości z innych rozmów
- 🔲 Context bundles (zapisane zestawy kontekstu)
- 🔲 Attachment handling (images, files)
- 🔲 Import Claude/ChatGPT takeout (JSON, ZIP)
- 🔲 GUI (Kivy for mobile)
- 🔲 AWS backend sync

## Setup (Pydroid 3)

### 1. Install dependencies

```bash
pip install requests openai rich prompt_toolkit
```

### 2. Get OpenRouter API key

1. Go to https://openrouter.ai/keys
2. Create account / login
3. Add credits ($10 minimum recommended)
4. Create API key

### 3. Run

```bash
python main.py
```

### 4. Set API key

```
/key sk-or-v1-xxxxxxxxxxxxx
```

## Commands

| Command | Description |
|---------|-------------|
| `/help` | Show help |
| `/key <api_key>` | Set OpenRouter API key |
| `/model <model_id>` | Change model |
| `/models` | List available models |
| `/temp <0.0-2.0>` | Set temperature |
| `/new [title]` | Start new conversation |
| `/list` | List conversations |
| `/load <id>` | Load conversation (supports partial ID) |
| `/history` | Show current conversation |
| `/tree` | Show conversation tree structure |
| `/exclude <msg_id>` | Exclude message from context |
| `/include <msg_id>` | Include message back in context |
| `/tag <msg_id> <tag>` | Tag a message |
| `/system <prompt>` | Set system prompt |
| `/config` | Show configuration |
| `/quit` | Exit |

## Available Models

### Anthropic
- `anthropic/claude-sonnet-4` - Claude Sonnet 4
- `anthropic/claude-haiku` - Claude Haiku  
- `anthropic/claude-opus-4` - Claude Opus 4

### OpenAI
- `openai/gpt-4o` - GPT-4o
- `openai/gpt-4o-mini` - GPT-4o Mini
- `openai/o3-mini` - o3 Mini

### Google
- `google/gemini-2.5-pro-preview` - Gemini 2.5 Pro
- `google/gemini-2.5-flash-preview` - Gemini 2.5 Flash

### DeepSeek
- `deepseek/deepseek-chat` - DeepSeek V3
- `deepseek/deepseek-reasoner` - DeepSeek R1

### Mistral
- `mistralai/mistral-large` - Mistral Large
- `mistralai/mistral-small` - Mistral Small

### xAI
- `x-ai/grok-2` - Grok 2

### Meta (often free!)
- `meta-llama/llama-3.3-70b-instruct` - Llama 3.3 70B

### Qwen
- `qwen/qwen-2.5-72b-instruct` - Qwen 2.5 72B

## Example Session

```
🤖 OpenRouter Multi-Model Chat Client
==================================================

📝 You: /key sk-or-v1-xxxx
✅ API key set successfully!

📝 You: /new Legal Analysis
✅ New conversation: Legal Analysis

📝 You: Analyze this situation from Dutch administrative law perspective...

[Claude Sonnet 4]: Based on Dutch administrative law (Algemene wet bestuursrecht)...

📝 You: /model deepseek/deepseek-reasoner
✅ Model changed to: DeepSeek R1

📝 You: Now give me a different perspective on the same issue

[DeepSeek R1]: <thinking>Let me analyze this from a different angle...</thinking>
...

📝 You: /history
📜 Legal Analysis
----------------------------------------
👤 [a1b2c3d4] 
Analyze this situation...
🤖 [e5f6g7h8]
Based on Dutch administrative law...
👤 [i9j0k1l2]
Now give me a different perspective...
🤖 [m3n4o5p6]
<thinking>...

📝 You: /exclude e5f6
✅ Message e5f6g7h8 excluded from context

📝 You: /tag m3n4 important
✅ Tagged message m3n4o5p6 with 'important'
```

## Database

SQLite database: `openrouter_client.db`

Tables:
- `conversations` - Conversation metadata
- `messages` - Messages with tree structure (parent_id for branching)
- `attachments` - File attachments
- `tags` - Tag definitions
- `bundles` - Saved context bundles
- `config` - App configuration

## Architecture

```
┌─────────────────────────────────────────────┐
│           OPENROUTER MULTI-CLIENT           │
├─────────────────────────────────────────────┤
│  CLI Interface (main.py)                    │
├─────────────────────────────────────────────┤
│  ChatApp                                    │
│  ├── Configuration management               │
│  ├── Conversation management                │
│  ├── Context building (with exclusions)     │
│  └── Branching logic                        │
├─────────────────────────────────────────────┤
│  OpenRouterClient                           │
│  ├── OpenAI-compatible API                  │
│  └── Streaming support                      │
├─────────────────────────────────────────────┤
│  Database (SQLite)                          │
│  └── Tree-structured messages               │
└─────────────────────────────────────────────┘
```

## Cost Estimation

With $10 OpenRouter credits:
- ~500k tokens Claude Sonnet
- ~2M tokens GPT-4o-mini  
- ~10M tokens DeepSeek
- Unlimited Llama 3.3 (free model)

