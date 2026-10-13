// Adapters of the speech / text-recognition API families (internal to the
// use-model action; registered next to the chat families in adapters.cpp).
#pragma once

#include <memory>

#include "loom/use_model.h"

namespace loom::llm {

// Multipart audio transcription in the OpenAI wire shape (Groq Whisper speaks it).
std::unique_ptr<Adapter> make_openai_transcriptions_adapter();
// Google Cloud Speech-to-Text v1 `speech:recognize` (JSON body, key in the query).
std::unique_ptr<Adapter> make_google_speech_adapter();
// OCR.space `parse/image` (urlencoded form, key as the first field).
std::unique_ptr<Adapter> make_ocr_space_form_adapter();

}  // namespace loom::llm
