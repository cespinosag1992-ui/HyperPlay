#pragma once

#include <cstddef>
#include <cstdint>

namespace hyperplay {

// Bytecode SPIR-V compilado a partir de fsr_cas.comp mediante glslc oficial del NDK 28.2.13676358.
// La primera palabra es 0x07230203 (SPIR-V Magic Number).
alignas(uint32_t) inline constexpr uint32_t kFsrCasSpv[] = 
#include "shaders/fsr_cas.inc"
;

inline constexpr size_t kFsrCasSpvSizeBytes = sizeof(kFsrCasSpv);
inline constexpr size_t kFsrCasSpvWordCount = sizeof(kFsrCasSpv) / sizeof(uint32_t);

}  // namespace hyperplay
