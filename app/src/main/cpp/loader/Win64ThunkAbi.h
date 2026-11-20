#pragma once

// Puente de llamada Win64 (Microsoft x64) -> AAPCS64 para los trampolines nativos.
// Header-only para que la suite nativa (OryonEmitterBackendTest) ejercite el mismo código
// que NativeDllInterceptor::dispatchThunk.

#include <array>
#include <cstdint>
#include <cstring>

#include "cpu/OryonEmitterBackend.h"

namespace hyperplay::win64abi {

// Máximo de argumentos que se transfieren a un trampolín (CreateWindowExW y
// D3D11CreateDeviceAndSwapChain usan 12).
inline constexpr size_t kMaxThunkArgs = 12;

using ThunkArgs = std::array<uint64_t, kMaxThunkArgs>;

// Lee los argumentos Win64 de una llamada cuya dirección de retorno YA fue retirada
// (RSP apunta al shadow space de 32 bytes):
//   arg0..arg3 = RCX, RDX, R8, R9
//   arg4..arg11 = [RSP + 0x20] .. [RSP + 0x58]
// Cada lectura de pila se valida con isValidStackAddress; si no es válida queda en 0.
inline ThunkArgs readThunkArgs(const CpuContext& ctx, const OryonEmitterBackend* oryon) noexcept {
    ThunkArgs args{};
    args[0] = ctx.gpr[1];
    args[1] = ctx.gpr[2];
    args[2] = ctx.gpr[8];
    args[3] = ctx.gpr[9];
    const uintptr_t rsp = static_cast<uintptr_t>(ctx.gpr[4]);
    for (size_t i = 4; i < kMaxThunkArgs; ++i) {
        const uintptr_t addr = rsp + 0x20 + (i - 4) * 8;
        if (oryon != nullptr && !oryon->isValidStackAddress(addr, 8)) continue;
        std::memcpy(&args[i], reinterpret_cast<const void*>(addr), sizeof(uint64_t));
    }
    return args;
}

// Invoca el trampolín con 12 argumentos enteros. En AAPCS64 los 8 primeros van en X0..X7 y
// el resto en la pila del llamador (slots de 8 bytes), por lo que un callee con menos
// parámetros es compatible.
inline uint64_t invokeThunk(uintptr_t target, const ThunkArgs& a) noexcept {
    using Fn = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    auto fn = reinterpret_cast<Fn>(target);
    return fn(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
}

}  // namespace hyperplay::win64abi
