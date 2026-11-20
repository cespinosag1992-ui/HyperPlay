#pragma once

// Estructuras Win64 de excepciones y contexto (layout exacto de Windows x64) a partir del
// CpuContext del JIT. Compartido por GetThreadContext/SetThreadContext y por la entrega de
// excepciones hardware al huésped (VEH / UnhandledExceptionFilter).

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "cpu/OryonEmitterBackend.h"

namespace hyperplay::win64ctx {

// CONTEXT (AMD64): sizeof = 0x4D0, alineado a 16.
inline constexpr size_t kContextSize = 0x4D0;
inline constexpr size_t kCtxFlags = 0x30, kCtxMxCsr = 0x34, kCtxSegCs = 0x38, kCtxSegSs = 0x42,
                        kCtxEFlags = 0x44, kCtxGpr = 0x78, kCtxRip = 0xF8, kCtxFltSave = 0x100,
                        kCtxXmm = 0x1A0;
inline constexpr uint32_t kContextAmd64 = 0x00100000u, kContextControl = 0x1, kContextInteger = 0x2,
                          kContextSegments = 0x4, kContextFloatingPoint = 0x8, kContextDebug = 0x10;
inline constexpr uint32_t kContextAll = kContextAmd64 | kContextControl | kContextInteger | kContextSegments |
                                        kContextFloatingPoint | kContextDebug;  // 0x0010001F

// EXCEPTION_RECORD (x64): sizeof = 0x98.
inline constexpr size_t kExceptionRecordSize = 0x98;
inline constexpr uint32_t kExceptionAccessViolation = 0xC0000005u;
inline constexpr uint32_t kExceptionIllegalInstruction = 0xC000001Du;
inline constexpr uint32_t kExceptionDatatypeMisalignment = 0x80000002u;
inline constexpr int32_t kExceptionContinueExecution = -1;
inline constexpr int32_t kExceptionContinueSearch = 0;

// CpuContext -> CONTEXT. flags == 0 usa el ContextFlags que ya trae `out` (GetThreadContext).
inline void toContext(const CpuContext& c, uint8_t* out, uint32_t flags) {
    if (flags == 0) std::memcpy(&flags, out + kCtxFlags, 4);
    if (flags == 0) flags = kContextAll;
    std::memcpy(out + kCtxFlags, &flags, 4);
    if ((flags & kContextInteger) != 0) std::memcpy(out + kCtxGpr, c.gpr, sizeof(c.gpr));
    if ((flags & kContextControl) != 0) {
        std::memcpy(out + kCtxRip, &c.rip, 8);
        std::memcpy(out + kCtxGpr + 4 * 8, &c.gpr[4], 8);  // Rsp
        std::memcpy(out + kCtxGpr + 5 * 8, &c.gpr[5], 8);  // Rbp
        // PF vive en pfByte (perezoso): se reconstruye el bit 2.
        const uint32_t eflags = (static_cast<uint32_t>(c.rflags) & ~0x4u) | (pfFromByte(c.pfByte) ? 0x4u : 0u) |
                                0x202u;  // bit 1 + IF
        std::memcpy(out + kCtxEFlags, &eflags, 4);
        const uint16_t cs = 0x33, ss = 0x2B;
        std::memcpy(out + kCtxSegCs, &cs, 2);
        std::memcpy(out + kCtxSegSs, &ss, 2);
    }
    if ((flags & kContextFloatingPoint) != 0) {
        const uint32_t mxcsr = 0x1F80;
        std::memcpy(out + kCtxMxCsr, &mxcsr, 4);
        std::memcpy(out + kCtxFltSave + 0x18, &mxcsr, 4);  // XMM_SAVE_AREA32.MxCsr
        std::memcpy(out + kCtxXmm, c.simd, sizeof(c.simd));
    }
}

// CONTEXT -> CpuContext según ContextFlags (forceAll: tras un VEH se aplica todo).
inline void fromContext(CpuContext& c, const uint8_t* in, bool forceAll) {
    uint32_t flags = 0;
    std::memcpy(&flags, in + kCtxFlags, 4);
    if (forceAll) flags = kContextAll;
    if ((flags & kContextInteger) != 0) std::memcpy(c.gpr, in + kCtxGpr, sizeof(c.gpr));
    if ((flags & kContextControl) != 0) {
        std::memcpy(&c.rip, in + kCtxRip, 8);
        std::memcpy(&c.gpr[4], in + kCtxGpr + 4 * 8, 8);
        std::memcpy(&c.gpr[5], in + kCtxGpr + 5 * 8, 8);
        uint32_t eflags = 0;
        std::memcpy(&eflags, in + kCtxEFlags, 4);
        c.rflags = eflags & 0x0CD5u;  // CF PF AF ZF SF DF OF
        c.pfByte = (eflags & 0x4u) ? 0u : 1u;
    }
    if ((flags & kContextFloatingPoint) != 0) std::memcpy(c.simd, in + kCtxXmm, sizeof(c.simd));
}

inline void fillExceptionRecord(uint8_t* rec, uint32_t code, uint64_t address, uint64_t info0, uint64_t info1,
                                uint32_t numParams) {
    std::memset(rec, 0, kExceptionRecordSize);
    std::memcpy(rec + 0x00, &code, 4);
    std::memcpy(rec + 0x10, &address, 8);  // ExceptionAddress
    std::memcpy(rec + 0x18, &numParams, 4);
    std::memcpy(rec + 0x20, &info0, 8);    // ExceptionInformation[0]: 0 lectura, 1 escritura, 8 ejecución
    std::memcpy(rec + 0x28, &info1, 8);    // ExceptionInformation[1]: dirección del fallo
}

}  // namespace hyperplay::win64ctx
