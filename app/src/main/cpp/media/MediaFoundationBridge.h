#pragma once

#include <cstddef>
#include <cstdint>

// Media Foundation (mfplat / mfreadwrite / mf) implementado sobre Android NDK Media:
// AMediaExtractor demultiplexa el MP4 que el juego entrega por su IMFByteStream y AMediaCodec
// decodifica H.264/AAC por hardware. Cubre el camino que usa el VideoPlayer de Unity en Windows
// (MFCreateSourceResolver -> CreateObjectFromByteStream -> MFCreateSourceReaderFromMediaSource ->
// ReadSample síncrono o con IMFSourceReaderCallback).
namespace hyperplay::mf {

// Registra una función host como destino nativo para el JIT (vtables COM).
using RegisterFn = void (*)(void* fn, const char* name);
// Llama a una función invitada (métodos COM del juego) con hasta 8 argumentos Win64.
using CallGuestFn = int64_t (*)(uintptr_t fn, const uint64_t* args, size_t argCount);

void init(RegisterFn registerFn, CallGuestFn callGuestFn);

// Export de mfplat.dll / mfreadwrite.dll / mf.dll por nombre, o nullptr.
void* resolveExport(const char* name);

}  // namespace hyperplay::mf
