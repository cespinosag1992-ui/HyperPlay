#pragma once

// SEH x64 del huésped: tablas de funciones (.pdata de las imágenes y tablas dinámicas de los
// JIT), RtlLookupFunctionEntry y RtlVirtualUnwind sobre un CONTEXT de Windows (layout AMD64).
// El despacho de excepciones (manejadores de lenguaje, RtlUnwindEx, RtlRestoreContext) vive en
// NativeDllInterceptor.cpp, que tiene los VEH y las llamadas al código invitado.
//
// Referencia de formato: "x64 exception handling" (Microsoft Learn) y la implementación de Wine
// (dlls/ntdll/signal_x86_64.c). Código propio.

#include <cstddef>
#include <cstdint>

namespace hyperplay::seh {

struct RuntimeFunction {
    uint32_t begin;
    uint32_t end;
    uint32_t unwindData;
};

// Flags de UNWIND_INFO / HandlerType de RtlVirtualUnwind.
inline constexpr uint32_t kUnwFlagNHandler = 0, kUnwFlagEHandler = 1, kUnwFlagUHandler = 2, kUnwFlagChainInfo = 4;

// Flags de EXCEPTION_RECORD.
inline constexpr uint32_t kEhNonContinuable = 0x01, kEhUnwinding = 0x02, kEhExitUnwind = 0x04, kEhStackInvalid = 0x08,
                          kEhNestedCall = 0x10, kEhTargetUnwind = 0x20, kEhCollidedUnwind = 0x40;

// EXCEPTION_DISPOSITION de un manejador de lenguaje.
inline constexpr int32_t kDispContinueExecution = 0, kDispContinueSearch = 1, kDispNested = 2, kDispCollided = 3;

inline constexpr uint32_t kStatusUnwind = 0xC0000027u, kStatusUnwindConsolidate = 0x80000029u,
                          kStatusLongjump = 0x80000026u;

/// Busca el módulo PE mapeado que contiene `pc` (lo da NativeDllInterceptor).
using ModuleFinder = bool (*)(uint64_t pc, uint64_t* base, uint64_t* size);
void setModuleFinder(ModuleFinder finder);

/// Llama a una función invitada (callback de RtlInstallFunctionTableCallback).
using GuestCaller = uint64_t (*)(uint64_t fn, const uint64_t* args, size_t argc);
void setGuestCaller(GuestCaller caller);

// Tablas dinámicas (JIT de Mono/CoreCLR).
uint32_t addGrowableTable(void** handle, const RuntimeFunction* table, uint32_t count, uint32_t max,
                          uint64_t rangeBase, uint64_t rangeEnd);
void growTable(void* handle, uint32_t count);
void deleteGrowableTable(void* handle);
bool installCallback(uint64_t tableId, uint64_t base, uint32_t length, uint64_t callback, uint64_t context);
bool addTable(const RuntimeFunction* table, uint32_t count, uint64_t base);
bool deleteTable(const void* tableOrId);

/// RtlLookupFunctionEntry: entrada de la función que contiene `pc` (o nullptr: función hoja).
const RuntimeFunction* lookup(uint64_t pc, uint64_t* imageBase);

/// RtlPcToFileHeader: base del módulo que contiene `pc` (0 si ninguno).
uint64_t moduleBaseOf(uint64_t pc);

/// RtlVirtualUnwind sobre un CONTEXT AMD64 (`ctx`, 0x4D0 bytes). Devuelve el manejador de
/// lenguaje (si el marco tiene uno del tipo pedido y el PC no está en el prólogo/epílogo).
uint64_t virtualUnwind(uint32_t handlerType, uint64_t imageBase, uint64_t pc, const RuntimeFunction* fn,
                       uint8_t* ctx, uint64_t* handlerData, uint64_t* establisherFrame,
                       uint64_t* contextPointers = nullptr);

/// Un paso de desenrollado completo (como virtual_unwind de Wine): con entrada de función usa
/// virtualUnwind; sin ella (hoja) saca RIP de [RSP]. Rellena la información del marco.
struct FrameInfo {
    uint64_t controlPc = 0;
    uint64_t imageBase = 0;
    const RuntimeFunction* function = nullptr;
    uint64_t establisherFrame = 0;
    uint64_t handler = 0;
    uint64_t handlerData = 0;
};
bool unwindStep(uint32_t handlerType, uint8_t* ctx, FrameInfo& out);

// Acceso a campos del CONTEXT AMD64.
uint64_t ctxRip(const uint8_t* ctx);
uint64_t ctxRsp(const uint8_t* ctx);
void setCtxRip(uint8_t* ctx, uint64_t v);
void setCtxRsp(uint8_t* ctx, uint64_t v);
void setCtxRax(uint8_t* ctx, uint64_t v);

}  // namespace hyperplay::seh
