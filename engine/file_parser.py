import os, logging
log = logging.getLogger('parser')

def ocr_image(path):
    try:
        from PIL import Image
        import pytesseract
        return {'text': pytesseract.image_to_string(Image.open(path)).strip(), 'confidence': 0.9}
    except Exception as e:
        return {'text': '', 'confidence': 0, 'note': str(e)}

def transcribe_audio(path):
    try:
        import whisper
        return {'text': whisper.load_model('small').transcribe(path).get('text','').strip(), 'confidence': 0.85}
    except Exception as e:
        return {'text': '', 'confidence': 0, 'note': str(e)}

def parse_file(path):
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.png','.jpg','.jpeg','.bmp','.webp'): return ocr_image(path)
    if ext in ('.wav','.mp3','.m4a','.flac'): return transcribe_audio(path)
    if ext in ('.txt','.md'):
        try: return {'text': open(path).read(), 'confidence': 1.0}
        except: pass
    return {'text': '', 'confidence': 0, 'note': 'unsupported'}
