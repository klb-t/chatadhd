#pragma once
#include "loom/use_model.h"
namespace loom::llm {
// Reads explicit model declarations / cached catalogue. Never infers from id.
Result<std::optional<bool>> catalog_accepts_images(const Services& services, const ModelSpec& model);
// Checks the actual serialized message image shape against the adapter and
// model data before existing usage/BYOK admission. No transport here.
Status check_image_contract(const Services& services, const ModelSpec& model, const Adapter& adapter,
                             const CallSpec& spec);
}
