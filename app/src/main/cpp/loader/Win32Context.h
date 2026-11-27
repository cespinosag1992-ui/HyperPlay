#pragma once

// Estructuras de excepciones de Windows x86 (32 bits, WOW64) a partir del CpuContext del JIT.
// Las usa el SEH de 32 bits (cadena fs:[0]); el de x64 está en Win64Context.h y no cambia.

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "cpu/OryonEmitterBackend.h"

namespace hyperplay::win32ctx {

// CONTEXT (i386): sizeof = 0x2CC.
inline constexpr size_t kContextSize = 0x2CC;
inline constexpr size_t kCtxFlags = 0x00, kCtxSegGs = 0x8C, kCtxSegFs = 0x90, kCtxSegEs = 0x94,
                        kCtxSegDs = 0x98, kCtxEdi = 0x9C, kCtxEsi = 0xA0, kCtxEbx = 0xA4, kCtxEdx = 0xA8,
                        kCtxEcx = 0xAC, kCtxEax = 0xB0, kCtxEbp = 0xB4, kCtxEip = 0xB8, kCtxSegCs = 0xBC,
                        kCtxEFlags = 0xC0, kCtxEsp = 0xC4, kCtxSegSs = 0xC8, kCtxExtended = 0xCC;
inline constexpr uint32_t kContextI386 = 0x00010000u;
inline constexpr uint32_t kContextFull = kContextI386 | 0x1 /*CONTROL*/ | 0x2 /*INTEGER*/ | 0x4 /*SEGMENTS*/;
inline constexpr uint32_t kContextExtendedRegisters = kContextI386 | 0x20;

// EXCEPTION_RECORD (x86): sizeof = 0x50.
inline constexpr size_t kExceptionRecordSize = 0x50;
inline constexpr uint32_t kExceptionNoncontinuable = 0x1, kExceptionUnwinding = 0x2, kExceptionExitUnwind = 0x4;
// EXCEPTION_DISPOSITION (x86, valores de los manejadores de marco).
inline constexpr uint32_t kDispositionContinueExecution = 0, kDispositionContinueSearch = 1;

inline void put32(uint8_t* p, size_t off, uint32_t v) { std::memcpy(p + off, &v, 4); }
inline uint32_t get32(const uint8_t* p, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, p + off, 4);
    return v;
}

// CpuContext -> CONTEXT con EIP/ESP dados (los del punto donde se reanudaría).
inline void toContext(const CpuContext& c, uint32_t eip, uint32_t esp, uint8_t* out) {
    std::memset(out, 0, kContextSize);
    put32(out, kCtxFlags, kContextFull | kContextExtendedRegisters);
    put32(out, kCtxSegGs, 0x2B);
    put32(out, kCtxSegFs, 0x53);
    put32(out, kCtxSegEs, 0x2B);
    put32(out, kCtxSegDs, 0x2B);
    put32(out, kCtxEax, static_cast<uint32_t>(c.gpr[0]));
    put32(out, kCtxEcx, static_cast<uint32_t>(c.gpr[1]));
    put32(out, kCtxEdx, static_cast<uint32_t>(c.gpr[2]));
    put32(out, kCtxEbx, static_cast<uint32_t>(c.gpr[3]));
    put32(out, kCtxEbp, static_cast<uint32_t>(c.gpr[5]));
    put32(out, kCtxEsi, static_cast<uint32_t>(c.gpr[6]));
    put32(out, kCtxEdi, static_cast<uint32_t>(c.gpr[7]));
    put32(out, kCtxEip, eip);
    put32(out, kCtxEsp, esp);
    put32(out, kCtxSegCs, 0x23);
    put32(out, kCtxSegSs, 0x2B);
    const uint32_t eflags = (static_cast<uint32_t>(c.rflags) & ~0x4u) | (pfFromByte(c.pfByte) ? 0x4u : 0u) | 0x202u;
    put32(out, kCtxEFlags, eflags);
    // ExtendedRegisters = FXSAVE: MXCSR en +24, XMM0..7 en +160.
    put32(out, kCtxExtended + 24, 0x1F80);
    std::memcpy(out + kCtxExtended + 160, c.simd, 8 * 16);
}

// CONTEXT -> CpuContext (registros enteros, control y XMM0..7).
inline void fromContext(CpuContext& c, const uint8_t* in) {
    c.gpr[0] = get32(in, kCtxEax);
    c.gpr[1] = get32(in, kCtxEcx);
    c.gpr[2] = get32(in, kCtxEdx);
    c.gpr[3] = get32(in, kCtxEbx);
    c.gpr[4] = get32(in, kCtxEsp);
    c.gpr[5] = get32(in, kCtxEbp);
    c.gpr[6] = get32(in, kCtxEsi);
    c.gpr[7] = get32(in, kCtxEdi);
    c.rip = get32(in, kCtxEip);
    const uint32_t eflags = get32(in, kCtxEFlags);
    c.rflags = eflags & 0x0CD5u;
    c.pfByte = (eflags & 0x4u) ? 0u : 1u;
    if ((get32(in, kCtxFlags) & 0x20u) != 0) std::memcpy(c.simd, in + kCtxExtended + 160, 8 * 16);
}

}  // namespace hyperplay::win32ctx
