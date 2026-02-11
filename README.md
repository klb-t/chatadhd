# ChatADHD v0.5.2

## What's New

### 🎤 Voice Input (Android)
- Tap microphone button to speak
- Requires RECORD_AUDIO permission
- Button shows 🎤, turns orange when listening

### ⌨️ Keyboard Handling
- `Window.softinput_mode = 'below_target'`
- Input field stays visible above keyboard
- No more hidden text while typing

### Previous Features (v0.5.1)
- 🌐 Web Search toggle
- 🔬 Deep Research mode
- 🧠 Reasoning effort (Auto/Low/Med/High/MAX)
- 📊 Graph Explorer with big readable nodes
- 📝 Message versioning
- 📁 Memory with file/dir attachments

## Installation

```bash
pip install kivy requests
```

Copy to `/storage/emulated/0/Download/`
Run `main.py` in Pydroid 3

## UI

### Input Area
```
[Message input............] [Send]
[+ Attach                   ] [🎤]
```

### Feature Toggles
```
[Web] [Deep] [Auto] [💭]
```

## Voice Input Usage

1. Tap 🎤 button
2. Grant microphone permission (first time)
3. Speak your message
4. Text appears in input field
5. Tap Send

Note: Requires Android with Google Speech Recognition

## Keyboard Tips

- Input auto-scrolls above keyboard
- Back button closes keyboard (not app)
- Landscape mode supported

---
ChatADHD v0.5.2 - Your voice, your thoughts, your control.
