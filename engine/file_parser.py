
import logging, os
log = logging.getLogger('file_parser')

def ocr_image(path: str):
    try:
        from PIL import Image
        import pytesseract
        img = Image.open(path)
        txt = pytesseract.image_to_string(img)
        return {'text': txt.strip(), 'confidence': 0.9 if txt else 0.0}
    except Exception as e:
        log.info('OCR not available: %s', e)
        return {'text':'', 'confidence':0.0, 'note':'ocr-missing'}

def transcribe_audio(path: str):
    try:
        import whisper
        model = whisper.load_model('small')
        res = model.transcribe(path)
        return {'text': res.get('text','').strip(), 'confidence': 0.85 if res.get('text') else 0.0}
    except Exception as e:
        log.info('ASR not available: %s', e)
        return {'text':'', 'confidence':0.0, 'note':'asr-missing'}

def parse_file(path: str):
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.png','.jpg','.jpeg','.tiff','.bmp'):
        return ocr_image(path)
    if ext in ('.wav','.mp3','.m4a','.flac','.ogg'):
        return transcribe_audio(path)
    return {'text':'', 'confidence':0.0, 'note':'unsupported'}
