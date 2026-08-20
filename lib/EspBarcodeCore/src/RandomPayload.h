#pragma once

#include <functional>
#include <string>

#include "EspBarcodeCore.h"

namespace espbarcode {

// Produces a payload that satisfies the given symbology's encoder outright
// (correct digit counts for check-digit symbologies, allowed character sets
// for alphanumeric ones), so passing it straight to encode() always succeeds.
// `nextRandom` supplies raw 32-bit random values from whatever source the
// caller has available (esp_random() on device, a seeded PRNG in tests).
std::string randomValidPayload(Symbology type, const std::function<uint32_t()>& nextRandom);

}  // namespace espbarcode
