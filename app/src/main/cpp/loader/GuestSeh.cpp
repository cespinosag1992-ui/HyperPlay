#include "GuestSeh.h"

#include <android/log.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#define LOG_TAG "HyperPlay.Seh"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay::seh {
namespace {

ModuleFinder s_finder = nullptr;
GuestCaller s_caller = nullptr;

// --- CONTEXT AMD64 ---------------------------------------------------------------------------
constexpr size_t kGpr = 0x78, kRip = 0xF8, kXmm = 0x1A0;

uint64_t reg(const uint8_t* ctx, unsigned r) {
    uint64_t v;
    std::memcpy(&v, ctx + kGpr + 8 * (r & 15), 8);
    return v;
}
void setReg(uint8_t* ctx, unsigned r, uint64_t v) { std::memcpy(ctx + kGpr + 8 * (r & 15), &v, 8); }
// Lectura que no puede tumbar el proceso: un marco corrupto (pila pisada, PC basura) se lee
// con process_vm_readv sobre el propio proceso; si falla, el recorrido se corta.
bool safeRead(uint64_t addr, void* out, size_t n) {
    if (addr < 0x10000) return false;
    struct iovec local{out, n};
    struct iovec remote{reinterpret_cast<void*>(addr), n};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(n);
}
uint64_t rd64(uint64_t addr) {
    uint64_t v = 0;
    if (!safeRead(addr, &v, 8)) return 0;  // RIP 0 termina el recorrido
    return v;
}

// --- Tablas dinámicas ----------------------------------------------------------------------------
struct DynTable {
    enum Kind { kGrowable, kStatic, kCallback } kind;
    uint64_t id = 0;          // handle / puntero a la tabla / identificador
    uint64_t rangeBase = 0;   // base de los RVA
    uint64_t rangeLo = 0, rangeHi = 0;
    const RuntimeFunction* table = nullptr;
    uint32_t count = 0;
    uint64_t callback = 0, context = 0;
};
std::shared_mutex s_dynMutex;
std::vector<DynTable> s_dyn;
uint64_t s_nextHandle = 0x7AB1E000;

// --- .pdata de los módulos -----------------------------------------------------------------------
struct ModuleTable {
    const RuntimeFunction* table = nullptr;
    uint32_t count = 0;
};
std::mutex s_modMutex;
std::unordered_map<uint64_t, ModuleTable> s_modTables;

ModuleTable moduleTable(uint64_t base) {
    {
        std::lock_guard<std::mutex> lock(s_modMutex);
        auto it = s_modTables.find(base);
        if (it != s_modTables.end()) return it->second;
    }
    ModuleTable t;
    const auto* b = reinterpret_cast<const uint8_t*>(base);
    uint32_t lfanew = 0;
    std::memcpy(&lfanew, b + 0x3C, 4);
    if (b[0] == 'M' && b[1] == 'Z' && lfanew < 0x1000 && std::memcmp(b + lfanew, "PE\0\0", 4) == 0) {
        const uint8_t* opt = b + lfanew + 24;
        uint16_t magic = 0;
        std::memcpy(&magic, opt, 2);
        if (magic == 0x20B) {  // PE32+
            uint32_t numDirs = 0, rva = 0, size = 0;
            std::memcpy(&numDirs, opt + 108, 4);
            if (numDirs > 3) {
                std::memcpy(&rva, opt + 112 + 3 * 8, 4);
                std::memcpy(&size, opt + 112 + 3 * 8 + 4, 4);
                if (rva != 0 && size >= sizeof(RuntimeFunction)) {
                    t.table = reinterpret_cast<const RuntimeFunction*>(base + rva);
                    t.count = size / sizeof(RuntimeFunction);
                }
            }
        }
    }
    std::lock_guard<std::mutex> lock(s_modMutex);
    s_modTables[base] = t;
    return t;
}

const RuntimeFunction* searchSorted(const RuntimeFunction* table, uint32_t count, uint32_t rva) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (rva < table[mid].begin) hi = mid;
        else if (rva >= table[mid].end) lo = mid + 1;
        else return &table[mid];
    }
    return nullptr;
}

const RuntimeFunction* resolveIndirect(const RuntimeFunction* f, uint64_t base) {
    // RUNTIME_FUNCTION_INDIRECT: UnwindData con el bit 0 apunta a otra RUNTIME_FUNCTION.
    for (int i = 0; f != nullptr && i < 4; ++i) {
        RuntimeFunction copy{};
        if (!safeRead(reinterpret_cast<uint64_t>(f), &copy, sizeof(copy))) return nullptr;
        if ((copy.unwindData & 1u) == 0) {
            uint32_t head = 0;  // UNWIND_INFO legible
            return safeRead(base + copy.unwindData, &head, 4) ? f : nullptr;
        }
        f = reinterpret_cast<const RuntimeFunction*>(base + (copy.unwindData & ~1u));
    }
    return nullptr;
}

// --- Epílogos (como Wine: is_inside_epilog / interpret_epilog) -----------------------------------
bool insideEpilog(const uint8_t* pc, uint64_t base, const RuntimeFunction* fn) {
    if ((pc[0] & 0xF8) == 0x48) {
        switch (pc[1]) {
            case 0x81:  // add rsp, imm32
                if (pc[0] == 0x48 && pc[2] == 0xC4) { pc += 7; break; }
                return false;
            case 0x83:  // add rsp, imm8
                if (pc[0] == 0x48 && pc[2] == 0xC4) { pc += 4; break; }
                return false;
            case 0x8D:  // lea rsp, [reg+disp]
                if ((pc[0] & 0x06) != 0 || ((pc[2] >> 3) & 7) != 4 || (pc[2] & 7) == 4) return false;
                if ((pc[2] >> 6) == 1) { pc += 4; break; }
                if ((pc[2] >> 6) == 2) { pc += 7; break; }
                return false;
            default:
                break;
        }
    }
    for (;;) {
        if ((*pc & 0xF0) == 0x40) ++pc;  // REX
        if (*pc >= 0x58 && *pc <= 0x5F) { ++pc; continue; }  // pop
        switch (*pc) {
            case 0xC2:
            case 0xC3:
                return true;
            case 0xE9: {
                int32_t rel;
                std::memcpy(&rel, pc + 1, 4);
                const uint64_t target = reinterpret_cast<uint64_t>(pc) + 5 + rel - base;
                return !(target >= fn->begin && target < fn->end);
            }
            case 0xEB: {
                const uint64_t target = reinterpret_cast<uint64_t>(pc) + 2 + static_cast<int8_t>(pc[1]) - base;
                return !(target >= fn->begin && target < fn->end);
            }
            case 0xF3:
                return pc[1] == 0xC3;
            default:
                return false;
        }
    }
}

void interpretEpilog(const uint8_t* pc, uint8_t* ctx, uint64_t* ctxPtrs) {
    for (;;) {
        uint8_t rex = 0;
        if ((*pc & 0xF0) == 0x40) rex = *pc++ & 0x0F;
        if (*pc >= 0x58 && *pc <= 0x5F) {
            const uint64_t rsp = reg(ctx, 4);
            setReg(ctx, (*pc - 0x58) + (rex & 1) * 8, rd64(rsp));
            if (ctxPtrs != nullptr) ctxPtrs[16 + (((*pc - 0x58) + (rex & 1) * 8) & 15)] = rsp;
            setReg(ctx, 4, rsp + 8);
            ++pc;
            continue;
        }
        switch (*pc) {
            case 0x81: {
                int32_t v;
                std::memcpy(&v, pc + 2, 4);
                setReg(ctx, 4, reg(ctx, 4) + v);
                pc += 6;
                continue;
            }
            case 0x83:
                setReg(ctx, 4, reg(ctx, 4) + static_cast<int8_t>(pc[2]));
                pc += 3;
                continue;
            case 0x8D:
                if ((pc[1] >> 6) == 1) {
                    setReg(ctx, 4, reg(ctx, (pc[1] & 7) + (rex & 1) * 8) + static_cast<int8_t>(pc[2]));
                    pc += 3;
                } else {
                    int32_t v;
                    std::memcpy(&v, pc + 2, 4);
                    setReg(ctx, 4, reg(ctx, (pc[1] & 7) + (rex & 1) * 8) + v);
                    pc += 6;
                }
                continue;
            case 0xC2: {
                uint16_t n;
                std::memcpy(&n, pc + 1, 2);
                const uint64_t rsp = reg(ctx, 4);
                setCtxRip(ctx, rd64(rsp));
                setReg(ctx, 4, rsp + 8 + n);
                return;
            }
            case 0xC3:
            case 0xE9:
            case 0xEB:
            case 0xF3: {
                const uint64_t rsp = reg(ctx, 4);
                setCtxRip(ctx, rd64(rsp));
                setReg(ctx, 4, rsp + 8);
                return;
            }
            default:
                return;
        }
    }
}

int opcodeSlots(uint8_t op, uint8_t info) {
    switch (op) {
        case 1: return info != 0 ? 3 : 2;  // ALLOC_LARGE
        case 4:                             // SAVE_NONVOL
        case 6:                             // EPILOG (v2)
        case 8: return 2;                   // SAVE_XMM128
        case 5:                             // SAVE_NONVOL_FAR
        case 7:                             // SPARE
        case 9: return 3;                   // SAVE_XMM128_FAR
        default: return 1;
    }
}

}  // namespace

void setModuleFinder(ModuleFinder finder) { s_finder = finder; }
void setGuestCaller(GuestCaller caller) { s_caller = caller; }

uint64_t ctxRip(const uint8_t* ctx) {
    uint64_t v;
    std::memcpy(&v, ctx + kRip, 8);
    return v;
}
uint64_t ctxRsp(const uint8_t* ctx) { return reg(ctx, 4); }
void setCtxRip(uint8_t* ctx, uint64_t v) { std::memcpy(ctx + kRip, &v, 8); }
void setCtxRsp(uint8_t* ctx, uint64_t v) { setReg(ctx, 4, v); }
void setCtxRax(uint8_t* ctx, uint64_t v) { setReg(ctx, 0, v); }

uint32_t addGrowableTable(void** handle, const RuntimeFunction* table, uint32_t count, uint32_t, uint64_t rangeBase,
                          uint64_t rangeEnd) {
    std::unique_lock<std::shared_mutex> lock(s_dynMutex);
    DynTable t{DynTable::kGrowable};
    t.id = s_nextHandle;
    s_nextHandle += 0x10;
    t.rangeBase = rangeBase;
    t.rangeLo = rangeBase;
    t.rangeHi = rangeEnd;
    t.table = table;
    t.count = count;
    s_dyn.push_back(t);
    if (handle != nullptr) *handle = reinterpret_cast<void*>(t.id);
    return 0;
}

void growTable(void* handle, uint32_t count) {
    std::unique_lock<std::shared_mutex> lock(s_dynMutex);
    for (auto& t : s_dyn) {
        if (t.kind == DynTable::kGrowable && t.id == reinterpret_cast<uint64_t>(handle)) t.count = count;
    }
}

void deleteGrowableTable(void* handle) { deleteTable(handle); }

bool installCallback(uint64_t tableId, uint64_t base, uint32_t length, uint64_t callback, uint64_t context) {
    if ((tableId & 3) != 3) return false;  // el identificador debe tener los 2 bits bajos a 1
    std::unique_lock<std::shared_mutex> lock(s_dynMutex);
    DynTable t{DynTable::kCallback};
    t.id = tableId;
    t.rangeBase = base;
    t.rangeLo = base;
    t.rangeHi = base + length;
    t.callback = callback;
    t.context = context;
    s_dyn.push_back(t);
    return true;
}

bool addTable(const RuntimeFunction* table, uint32_t count, uint64_t base) {
    if (table == nullptr || count == 0) return false;
    uint32_t lo = UINT32_MAX, hi = 0;
    for (uint32_t i = 0; i < count; ++i) {
        lo = std::min(lo, table[i].begin);
        hi = std::max(hi, table[i].end);
    }
    std::unique_lock<std::shared_mutex> lock(s_dynMutex);
    DynTable t{DynTable::kStatic};
    t.id = reinterpret_cast<uint64_t>(table);
    t.rangeBase = base;
    t.rangeLo = base + lo;
    t.rangeHi = base + hi;
    t.table = table;
    t.count = count;
    s_dyn.push_back(t);
    return true;
}

bool deleteTable(const void* tableOrId) {
    std::unique_lock<std::shared_mutex> lock(s_dynMutex);
    const auto id = reinterpret_cast<uint64_t>(tableOrId);
    const auto before = s_dyn.size();
    s_dyn.erase(std::remove_if(s_dyn.begin(), s_dyn.end(), [id](const DynTable& t) { return t.id == id; }), s_dyn.end());
    return s_dyn.size() != before;
}

uint64_t moduleBaseOf(uint64_t pc) {
    uint64_t base = 0, size = 0;
    if (s_finder != nullptr && s_finder(pc, &base, &size)) return base;
    return 0;
}

const RuntimeFunction* lookup(uint64_t pc, uint64_t* imageBase) {
    uint64_t base = 0, size = 0;
    if (s_finder != nullptr && s_finder(pc, &base, &size)) {
        const ModuleTable t = moduleTable(base);
        if (t.table != nullptr) {
            const RuntimeFunction* f = searchSorted(t.table, t.count, static_cast<uint32_t>(pc - base));
            if (f != nullptr) {
                if (imageBase != nullptr) *imageBase = base;
                return resolveIndirect(f, base);
            }
        }
        return nullptr;  // función hoja de un módulo
    }
    DynTable hit{};
    bool found = false;
    {
        std::shared_lock<std::shared_mutex> lock(s_dynMutex);
        for (const auto& t : s_dyn) {
            if (pc >= t.rangeLo && pc < t.rangeHi) {
                hit = t;
                found = true;
                break;
            }
        }
    }
    if (!found) return nullptr;
    if (hit.kind == DynTable::kCallback) {
        if (s_caller == nullptr) return nullptr;
        const uint64_t args[2] = {pc, hit.context};
        const auto* f = reinterpret_cast<const RuntimeFunction*>(s_caller(hit.callback, args, 2));
        if (f != nullptr && imageBase != nullptr) *imageBase = hit.rangeBase;
        return f != nullptr ? resolveIndirect(f, hit.rangeBase) : nullptr;
    }
    const uint32_t rva = static_cast<uint32_t>(pc - hit.rangeBase);
    const RuntimeFunction* f = searchSorted(hit.table, hit.count, rva);
    if (f == nullptr) {
        for (uint32_t i = 0; i < hit.count; ++i) {  // tablas sin ordenar
            if (rva >= hit.table[i].begin && rva < hit.table[i].end) {
                f = &hit.table[i];
                break;
            }
        }
    }
    if (f != nullptr && imageBase != nullptr) *imageBase = hit.rangeBase;
    return f != nullptr ? resolveIndirect(f, hit.rangeBase) : nullptr;
}

uint64_t virtualUnwind(uint32_t handlerType, uint64_t base, uint64_t pc, const RuntimeFunction* fn, uint8_t* ctx,
                       uint64_t* handlerData, uint64_t* establisherFrame, uint64_t* ctxPtrs) {
    // KNONVOLATILE_CONTEXT_POINTERS: FloatingContext[16] y después IntegerContext[16] (punteros a
    // donde quedó guardado cada registro). El GC de CoreCLR los usa para actualizar referencias
    // guardadas en la pila cuando mueve objetos.
    auto noteInt = [ctxPtrs](unsigned r, uint64_t addr) {
        if (ctxPtrs != nullptr) ctxPtrs[16 + (r & 15)] = addr;
    };
    auto noteXmm = [ctxPtrs](unsigned r, uint64_t addr) {
        if (ctxPtrs != nullptr) ctxPtrs[r & 15] = addr;
    };
    uint64_t handler = 0;
    if (handlerData != nullptr) *handlerData = 0;
    uint32_t prologOffset = static_cast<uint32_t>(pc - base - fn->begin);
    bool primary = true;
    for (int chain = 0; chain < 32; ++chain) {
        const auto* info = reinterpret_cast<const uint8_t*>(base + fn->unwindData);
        const uint8_t version = info[0] & 7, flags = info[0] >> 3, prologSize = info[1], count = info[2];
        const uint8_t frameReg = info[3] & 0x0F, frameOff = info[3] >> 4;
        const uint8_t* codes = info + 4;
        if (version != 1 && version != 2) {
            LOGW("UNWIND_INFO versión %u no soportada en %p", version, reinterpret_cast<void*>(pc));
            return 0;
        }
        const bool inProlog = primary && prologOffset < prologSize;
        uint8_t probe[16];
        const bool pcReadable = safeRead(pc, probe, sizeof(probe));
        if (primary && !inProlog && pcReadable && insideEpilog(reinterpret_cast<const uint8_t*>(pc), base, fn)) {
            if (establisherFrame != nullptr) *establisherFrame = reg(ctx, 4);
            interpretEpilog(reinterpret_cast<const uint8_t*>(pc), ctx, ctxPtrs);
            return 0;
        }
        const uint32_t limit = primary ? prologOffset : 0xFFFFFFFFu;
        // Marco: registro de marco si SET_FPREG ya se ejecutó; si no, RSP.
        uint64_t frame = reg(ctx, 4);
        if (frameReg != 0) {
            bool fpSet = !inProlog;
            for (int i = 0; i < count && !fpSet;) {
                const uint8_t off = codes[i * 2], op = codes[i * 2 + 1] & 0x0F, opInfo = codes[i * 2 + 1] >> 4;
                if (op == 3 && off <= limit) fpSet = true;
                i += opcodeSlots(op, opInfo);
            }
            if (fpSet) frame = reg(ctx, frameReg) - static_cast<uint64_t>(frameOff) * 16;
        }
        if (primary && establisherFrame != nullptr) *establisherFrame = frame;

        for (int i = 0; i < count;) {
            const uint8_t off = codes[i * 2], op = codes[i * 2 + 1] & 0x0F, opInfo = codes[i * 2 + 1] >> 4;
            const int slots = opcodeSlots(op, opInfo);
            if (off > limit && op != 6) {  // aún no ejecutado en el prólogo
                i += slots;
                continue;
            }
            auto slot16 = [&](int k) {
                uint16_t v;
                std::memcpy(&v, codes + (i + k) * 2, 2);
                return v;
            };
            auto slot32 = [&](int k) {
                uint32_t v;
                std::memcpy(&v, codes + (i + k) * 2, 4);
                return v;
            };
            switch (op) {
                case 0: {  // PUSH_NONVOL
                    const uint64_t rsp = reg(ctx, 4);
                    setReg(ctx, opInfo, rd64(rsp));
                    noteInt(opInfo, rsp);
                    setReg(ctx, 4, rsp + 8);
                    break;
                }
                case 1:  // ALLOC_LARGE
                    setReg(ctx, 4, reg(ctx, 4) + (opInfo != 0 ? slot32(1) : static_cast<uint64_t>(slot16(1)) * 8));
                    break;
                case 2:  // ALLOC_SMALL
                    setReg(ctx, 4, reg(ctx, 4) + (static_cast<uint64_t>(opInfo) + 1) * 8);
                    break;
                case 3:  // SET_FPREG
                    setReg(ctx, 4, reg(ctx, frameReg) - static_cast<uint64_t>(frameOff) * 16);
                    break;
                case 4: {  // SAVE_NONVOL
                    const uint64_t a = frame + static_cast<uint64_t>(slot16(1)) * 8;
                    setReg(ctx, opInfo, rd64(a));
                    noteInt(opInfo, a);
                    break;
                }
                case 5: {  // SAVE_NONVOL_FAR
                    const uint64_t a = frame + slot32(1);
                    setReg(ctx, opInfo, rd64(a));
                    noteInt(opInfo, a);
                    break;
                }
                case 8: {  // SAVE_XMM128
                    const uint64_t a = frame + static_cast<uint64_t>(slot16(1)) * 16;
                    safeRead(a, ctx + kXmm + 16 * opInfo, 16);
                    noteXmm(opInfo, a);
                    break;
                }
                case 9: {  // SAVE_XMM128_FAR
                    const uint64_t a = frame + slot32(1);
                    safeRead(a, ctx + kXmm + 16 * opInfo, 16);
                    noteXmm(opInfo, a);
                    break;
                }
                case 10: {  // PUSH_MACHFRAME: RIP, CS, EFLAGS, RSP, SS (más código de error si opInfo=1)
                    const uint64_t rsp = reg(ctx, 4) + static_cast<uint64_t>(opInfo) * 8;
                    setCtxRip(ctx, rd64(rsp));
                    setReg(ctx, 4, rd64(rsp + 24));
                    return handler;  // sin pop final de dirección de retorno
                }
                default:  // EPILOG (v2), SPARE: nada que deshacer
                    break;
            }
            i += slots;
        }

        if ((flags & kUnwFlagChainInfo) == 0) {
            if (primary && !inProlog && (flags & handlerType) != 0) {
                const uint8_t* hd = codes + ((count + 1) & ~1) * 2;
                uint32_t rva;
                std::memcpy(&rva, hd, 4);
                handler = base + rva;
                if (handlerData != nullptr) *handlerData = reinterpret_cast<uint64_t>(hd + 4);
            }
            break;
        }
        // Información encadenada: se deshace el resto del prólogo del padre (completo).
        if (primary && !inProlog && (flags & handlerType) != 0) {
            // (un manejador con CHAININFO no existe: los flags son excluyentes)
        }
        fn = reinterpret_cast<const RuntimeFunction*>(codes + ((count + 1) & ~1) * 2);
        fn = resolveIndirect(fn, base);
        primary = false;
    }
    const uint64_t rsp = reg(ctx, 4);
    setCtxRip(ctx, rd64(rsp));
    setReg(ctx, 4, rsp + 8);
    return handler;
}

bool unwindStep(uint32_t handlerType, uint8_t* ctx, FrameInfo& out) {
    out = FrameInfo{};
    out.controlPc = ctxRip(ctx);
    if (out.controlPc < 0x10000) {  // PC basura: fin del recorrido
        setCtxRip(ctx, 0);
        return false;
    }
    out.function = lookup(out.controlPc, &out.imageBase);
    if (out.function != nullptr) {
        out.handler = virtualUnwind(handlerType, out.imageBase, out.controlPc, out.function, ctx, &out.handlerData,
                                    &out.establisherFrame, nullptr);
        return true;
    }
    // Hoja: sin prólogo; la dirección de retorno está en [RSP].
    const uint64_t rsp = ctxRsp(ctx);
    out.establisherFrame = rsp;
    setCtxRip(ctx, rd64(rsp));
    setCtxRsp(ctx, rsp + 8);
    return true;
}

}  // namespace hyperplay::seh
