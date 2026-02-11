"""
ChatADHD v0.07.00 - Provider Abstraction Layer
Unified interface for external services:
- OCR: ocr.space
- ASR: Groq (Whisper), Google Speech (with alternatives)
- LLM: OpenRouter (existing)
"""
import os
import json
import base64
import logging
import requests
from pathlib import Path
from typing import Optional, List, Dict, Any
from dataclasses import dataclass

log = logging.getLogger('providers')


@dataclass
class ASRResult:
    """Speech recognition result with alternatives."""
    text: str
    confidence: float
    alternatives: List[Dict[str, Any]]  # [{text, confidence}, ...]
    language: Optional[str] = None
    duration: Optional[float] = None


@dataclass
class OCRResult:
    """OCR result with parsed text."""
    text: str
    confidence: float
    lines: List[str]
    language: Optional[str] = None
    

class ProviderError(Exception):
    """Base exception for provider errors."""
    pass


# === ASR PROVIDERS ===

class ASRProvider:
    """Base class for speech recognition providers."""
    
    name: str = "base"
    
    def __init__(self, api_key: str):
        self.api_key = api_key
    
    def transcribe(self, audio_path: str, language: str = None) -> ASRResult:
        raise NotImplementedError
    
    def transcribe_bytes(self, audio_data: bytes, format: str = 'wav', language: str = None) -> ASRResult:
        raise NotImplementedError


class GroqASR(ASRProvider):
    """Groq Whisper API - fast, cheap, good quality."""
    
    name = "groq"
    BASE_URL = "https://api.groq.com/openai/v1/audio/transcriptions"
    
    # Available models
    MODELS = {
        'whisper-large-v3': 'Best quality',
        'whisper-large-v3-turbo': 'Fast, good quality',
        'distil-whisper-large-v3-en': 'English only, fastest',
    }
    
    def __init__(self, api_key: str, model: str = 'whisper-large-v3-turbo'):
        super().__init__(api_key)
        self.model = model
    
    def transcribe(self, audio_path: str, language: str = None) -> ASRResult:
        with open(audio_path, 'rb') as f:
            audio_data = f.read()
        
        ext = Path(audio_path).suffix.lower().lstrip('.')
        format_map = {'mp3': 'mp3', 'wav': 'wav', 'm4a': 'mp4', 'ogg': 'ogg', 'webm': 'webm'}
        fmt = format_map.get(ext, 'wav')
        
        return self.transcribe_bytes(audio_data, fmt, language)
    
    def transcribe_bytes(self, audio_data: bytes, format: str = 'wav', language: str = None) -> ASRResult:
        files = {
            'file': (f'audio.{format}', audio_data, f'audio/{format}'),
        }
        
        data = {
            'model': self.model,
            'response_format': 'verbose_json',  # Get segments with confidence
        }
        
        if language:
            data['language'] = language
        
        headers = {
            'Authorization': f'Bearer {self.api_key}',
        }
        
        try:
            resp = requests.post(self.BASE_URL, headers=headers, files=files, data=data, timeout=60)
            resp.raise_for_status()
            result = resp.json()
            
            # Extract alternatives from segments if available
            alternatives = []
            segments = result.get('segments', [])
            
            # Groq doesn't provide alternatives per se, but we can use segment info
            for seg in segments[:5]:
                alternatives.append({
                    'text': seg.get('text', '').strip(),
                    'confidence': seg.get('avg_logprob', 0),
                    'start': seg.get('start'),
                    'end': seg.get('end'),
                })
            
            return ASRResult(
                text=result.get('text', '').strip(),
                confidence=0.9,  # Groq doesn't provide overall confidence
                alternatives=alternatives,
                language=result.get('language'),
                duration=result.get('duration'),
            )
            
        except requests.exceptions.RequestException as e:
            raise ProviderError(f"Groq ASR error: {e}")


class GoogleSpeechASR(ASRProvider):
    """Google Cloud Speech-to-Text - provides alternatives with confidence scores."""
    
    name = "google"
    BASE_URL = "https://speech.googleapis.com/v1/speech:recognize"
    
    def __init__(self, api_key: str):
        super().__init__(api_key)
    
    def transcribe(self, audio_path: str, language: str = None) -> ASRResult:
        with open(audio_path, 'rb') as f:
            audio_data = f.read()
        
        ext = Path(audio_path).suffix.lower().lstrip('.')
        return self.transcribe_bytes(audio_data, ext, language)
    
    def transcribe_bytes(self, audio_data: bytes, format: str = 'wav', language: str = None) -> ASRResult:
        # Encode audio as base64
        audio_b64 = base64.b64encode(audio_data).decode('utf-8')
        
        # Determine encoding
        encoding_map = {
            'wav': 'LINEAR16',
            'mp3': 'MP3',
            'ogg': 'OGG_OPUS',
            'flac': 'FLAC',
            'webm': 'WEBM_OPUS',
        }
        encoding = encoding_map.get(format, 'LINEAR16')
        
        payload = {
            'config': {
                'encoding': encoding,
                'languageCode': language or 'en-US',
                'maxAlternatives': 5,  # Key feature: get alternatives!
                'enableWordConfidence': True,
                'enableWordTimeOffsets': True,
            },
            'audio': {
                'content': audio_b64,
            }
        }
        
        try:
            resp = requests.post(
                f"{self.BASE_URL}?key={self.api_key}",
                json=payload,
                timeout=60
            )
            resp.raise_for_status()
            result = resp.json()
            
            # Parse results
            alternatives = []
            best_text = ""
            best_confidence = 0.0
            
            results = result.get('results', [])
            if results:
                alts = results[0].get('alternatives', [])
                
                for i, alt in enumerate(alts):
                    text = alt.get('transcript', '')
                    conf = alt.get('confidence', 0)
                    
                    alternatives.append({
                        'text': text,
                        'confidence': conf,
                        'rank': i + 1,
                    })
                    
                    if i == 0:
                        best_text = text
                        best_confidence = conf
            
            return ASRResult(
                text=best_text,
                confidence=best_confidence,
                alternatives=alternatives,
                language=language,
            )
            
        except requests.exceptions.RequestException as e:
            raise ProviderError(f"Google Speech error: {e}")


# === OCR PROVIDERS ===

class OCRProvider:
    """Base class for OCR providers."""
    
    name: str = "base"
    
    def __init__(self, api_key: str):
        self.api_key = api_key
    
    def recognize(self, image_path: str, language: str = None) -> OCRResult:
        raise NotImplementedError
    
    def recognize_bytes(self, image_data: bytes, format: str = 'png', language: str = None) -> OCRResult:
        raise NotImplementedError


class OCRSpaceProvider(OCRProvider):
    """ocr.space API - free tier available, good accuracy."""
    
    name = "ocr.space"
    BASE_URL = "https://api.ocr.space/parse/image"
    
    # Language codes
    LANGUAGES = {
        'en': 'eng', 'pl': 'pol', 'de': 'ger', 'fr': 'fre', 
        'es': 'spa', 'it': 'ita', 'nl': 'dut', 'pt': 'por',
        'ru': 'rus', 'zh': 'chs', 'ja': 'jpn', 'ko': 'kor',
    }
    
    def __init__(self, api_key: str, engine: int = 2):
        """
        engine: 1 = default, 2 = better for screenshots/documents, 3 = newest
        """
        super().__init__(api_key)
        self.engine = engine
    
    def recognize(self, image_path: str, language: str = None) -> OCRResult:
        with open(image_path, 'rb') as f:
            image_data = f.read()
        
        ext = Path(image_path).suffix.lower().lstrip('.')
        return self.recognize_bytes(image_data, ext, language)
    
    def recognize_bytes(self, image_data: bytes, format: str = 'png', language: str = None) -> OCRResult:
        # Encode as base64
        image_b64 = base64.b64encode(image_data).decode('utf-8')
        
        # Map language code
        lang_code = self.LANGUAGES.get(language, 'eng') if language else 'eng'
        
        payload = {
            'apikey': self.api_key,
            'base64Image': f'data:image/{format};base64,{image_b64}',
            'language': lang_code,
            'OCREngine': self.engine,
            'isTable': True,  # Better for structured content
            'scale': True,  # Auto-scale for better recognition
        }
        
        try:
            resp = requests.post(self.BASE_URL, data=payload, timeout=60)
            resp.raise_for_status()
            result = resp.json()
            
            if result.get('IsErroredOnProcessing'):
                error_msg = result.get('ErrorMessage', ['Unknown error'])
                raise ProviderError(f"OCR.space error: {error_msg}")
            
            # Parse results
            parsed_results = result.get('ParsedResults', [])
            
            if not parsed_results:
                return OCRResult(text='', confidence=0, lines=[], language=language)
            
            first_result = parsed_results[0]
            text = first_result.get('ParsedText', '')
            
            # Clean up text
            lines = [line.strip() for line in text.split('\n') if line.strip()]
            
            # Get confidence from TextOverlay if available
            confidence = 0.0
            text_overlay = first_result.get('TextOverlay', {})
            if text_overlay:
                words = text_overlay.get('Lines', [])
                if words:
                    # Average confidence across words
                    confs = []
                    for line in words:
                        for word in line.get('Words', []):
                            if 'WordText' in word:
                                confs.append(0.9)  # ocr.space doesn't give per-word confidence
                    confidence = sum(confs) / len(confs) if confs else 0.9
            else:
                confidence = 0.85 if text else 0
            
            return OCRResult(
                text='\n'.join(lines),
                confidence=confidence,
                lines=lines,
                language=language,
            )
            
        except requests.exceptions.RequestException as e:
            raise ProviderError(f"OCR.space request error: {e}")


# === PROVIDER MANAGER ===

class ProviderManager:
    """Manages all external service providers."""
    
    def __init__(self, secrets):
        self.secrets = secrets
        self._asr_providers = {}
        self._ocr_providers = {}
        self._init_providers()
    
    def _init_providers(self):
        """Initialize available providers based on configured API keys."""
        
        # ASR providers
        groq_key = self.secrets.get('groq_api_key')
        if groq_key:
            self._asr_providers['groq'] = GroqASR(groq_key)
            log.info("Groq ASR initialized")
        
        google_key = self.secrets.get('google_speech_api_key')
        if google_key:
            self._asr_providers['google'] = GoogleSpeechASR(google_key)
            log.info("Google Speech ASR initialized")
        
        # OCR providers
        ocr_space_key = self.secrets.get('ocr_space_api_key')
        if ocr_space_key:
            self._ocr_providers['ocr.space'] = OCRSpaceProvider(ocr_space_key)
            log.info("OCR.space initialized")
    
    # === ASR ===
    
    def get_asr_providers(self) -> List[str]:
        """Get list of available ASR providers."""
        return list(self._asr_providers.keys())
    
    def transcribe(self, audio_path: str, provider: str = None, language: str = None) -> ASRResult:
        """Transcribe audio file."""
        if not self._asr_providers:
            raise ProviderError("No ASR providers configured. Add groq_api_key or google_speech_api_key to secrets.")
        
        if provider:
            if provider not in self._asr_providers:
                raise ProviderError(f"ASR provider '{provider}' not available. Available: {self.get_asr_providers()}")
            return self._asr_providers[provider].transcribe(audio_path, language)
        
        # Try providers in order of preference
        for name in ['groq', 'google']:
            if name in self._asr_providers:
                try:
                    return self._asr_providers[name].transcribe(audio_path, language)
                except ProviderError as e:
                    log.warning(f"ASR provider {name} failed: {e}")
                    continue
        
        raise ProviderError("All ASR providers failed")
    
    def transcribe_bytes(self, audio_data: bytes, format: str = 'wav', 
                        provider: str = None, language: str = None) -> ASRResult:
        """Transcribe audio from bytes."""
        if not self._asr_providers:
            raise ProviderError("No ASR providers configured")
        
        if provider:
            if provider not in self._asr_providers:
                raise ProviderError(f"ASR provider '{provider}' not available")
            return self._asr_providers[provider].transcribe_bytes(audio_data, format, language)
        
        for name in ['groq', 'google']:
            if name in self._asr_providers:
                try:
                    return self._asr_providers[name].transcribe_bytes(audio_data, format, language)
                except ProviderError as e:
                    log.warning(f"ASR provider {name} failed: {e}")
                    continue
        
        raise ProviderError("All ASR providers failed")
    
    # === OCR ===
    
    def get_ocr_providers(self) -> List[str]:
        """Get list of available OCR providers."""
        return list(self._ocr_providers.keys())
    
    def ocr(self, image_path: str, provider: str = None, language: str = None) -> OCRResult:
        """Perform OCR on image file."""
        if not self._ocr_providers:
            raise ProviderError("No OCR providers configured. Add ocr_space_api_key to secrets.")
        
        if provider:
            if provider not in self._ocr_providers:
                raise ProviderError(f"OCR provider '{provider}' not available")
            return self._ocr_providers[provider].recognize(image_path, language)
        
        # Try first available
        for name, prov in self._ocr_providers.items():
            try:
                return prov.recognize(image_path, language)
            except ProviderError as e:
                log.warning(f"OCR provider {name} failed: {e}")
                continue
        
        raise ProviderError("All OCR providers failed")
    
    def ocr_bytes(self, image_data: bytes, format: str = 'png',
                  provider: str = None, language: str = None) -> OCRResult:
        """Perform OCR on image bytes."""
        if not self._ocr_providers:
            raise ProviderError("No OCR providers configured")
        
        if provider:
            if provider not in self._ocr_providers:
                raise ProviderError(f"OCR provider '{provider}' not available")
            return self._ocr_providers[provider].recognize_bytes(image_data, format, language)
        
        for name, prov in self._ocr_providers.items():
            try:
                return prov.recognize_bytes(image_data, format, language)
            except ProviderError as e:
                log.warning(f"OCR provider {name} failed: {e}")
                continue
        
        raise ProviderError("All OCR providers failed")
    
    # === Status ===
    
    def status(self) -> Dict[str, Any]:
        """Get status of all providers."""
        return {
            'asr': {
                'available': self.get_asr_providers(),
                'configured': bool(self._asr_providers),
            },
            'ocr': {
                'available': self.get_ocr_providers(),
                'configured': bool(self._ocr_providers),
            },
        }
