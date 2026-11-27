// DirectInput 8 mínimo para juegos de 32 bits (x86). Unreal Engine 3 crea con él el teclado y el ratón
// y no comprueba el resultado de DirectInput8Create. En el teléfono no hay teclado ni ratón: los
// dispositivos existen, se adquieren y no devuelven datos (estado a cero, cola vacía). EnumDevices no
// enumera joysticks: los mandos llegan por XInput. Los juegos de 64 bits no pasan por aquí.
//
// Objetos COM en memoria baja con vtables de 32 bits (stdcall, `this` incluido, aridad exacta).

#include "DirectInput32.h"

#include <android/log.h>
#include <sys/mman.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "LowMem.h"
#include "Win32Arity.h"

#define LOG_TAG "HyperPlay.DInput32"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace w32alias {
uintptr_t lowFor(void* host, const std::string& name);  // NativeDllInterceptor.cpp
}

namespace dinput32 {
namespace {

using u32 = uint32_t;
constexpr u32 kOk = 0;
constexpr u32 kUnsupported = 0x80004001u;      // E_NOTIMPL
constexpr u32 kDeviceNotReg = 0x80040154u;     // DIERR_DEVICENOTREG
constexpr u32 kNotFound = 0x80070002u;         // DIERR_NOTFOUND

template <typename T = uint8_t>
T* G(u32 a) { return reinterpret_cast<T*>(static_cast<uintptr_t>(a)); }
u32 L(const void* p) { return static_cast<u32>(reinterpret_cast<uintptr_t>(p)); }
void put32(u32 a, u32 v) { if (a != 0) std::memcpy(G<void>(a), &v, 4); }
u32 rd32(u32 a) { u32 v = 0; if (a != 0) std::memcpy(&v, G<void>(a), 4); return v; }

void* lowAlloc(size_t n) {
    static std::mutex m;
    static uint8_t* cur = nullptr;
    static size_t left = 0;
    std::lock_guard<std::mutex> lk(m);
    n = (n + 15) & ~size_t{15};
    if (cur == nullptr || left < n) {
        void* p = lowmem::mmapBelow4G(0x10000);
        if (p == MAP_FAILED) return nullptr;
        cur = static_cast<uint8_t*>(p);
        left = 0x10000;
    }
    uint8_t* r = cur;
    cur += n;
    left -= n;
    return r;
}

struct Obj {
    u32 vtbl;
    std::atomic<u32> refs{1};
    u32 kind;  // 0 = IDirectInput8, 1 = teclado, 2 = ratón
};

u32 g_vtDI = 0, g_vtDev = 0;

u32 makeVtable(const std::vector<void*>& m, const char* name) {
    auto* vt = static_cast<u32*>(lowAlloc(4 * m.size()));
    if (vt == nullptr) return 0;
    for (size_t i = 0; i < m.size(); ++i)
        vt[i] = static_cast<u32>(w32alias::lowFor(m[i], std::string("dinput8.dll!") + name + "#" + std::to_string(i)));
    return L(vt);
}

u32 newObj(u32 vt, u32 kind) {
    auto* mem = lowAlloc(sizeof(Obj));
    if (mem == nullptr) return 0;
    auto* o = new (mem) Obj();
    o->vtbl = vt;
    o->kind = kind;
    return L(o);
}

// --- IUnknown común ---
u32 QI(u32 t, u32, u32 ppv) {
    // Cualquier IID de DirectInput8 / DirectInputDevice8 (W/A): el mismo objeto.
    G<Obj>(t)->refs.fetch_add(1);
    put32(ppv, t);
    return kOk;
}
u32 AddRef(u32 t) { return G<Obj>(t)->refs.fetch_add(1) + 1; }
u32 Release(u32 t) { const u32 p = G<Obj>(t)->refs.fetch_sub(1); return p > 0 ? p - 1 : 0; }

// --- IDirectInput8W ---
u32 DI_CreateDevice(u32, u32 rguid, u32 ppDev, u32) {
    const u32 d1 = rd32(rguid);
    // GUID_SysKeyboard {6F1D2B61-...}, GUID_SysMouse {6F1D2B60-...}
    u32 kind = 0;
    if (d1 == 0x6F1D2B61u) kind = 1;
    else if (d1 == 0x6F1D2B60u) kind = 2;
    if (kind == 0) { put32(ppDev, 0); return kDeviceNotReg; }
    const u32 dev = newObj(g_vtDev, kind);
    put32(ppDev, dev);
    LOGI("CreateDevice(%s) -> 0x%x (sin datos)", kind == 1 ? "teclado" : "ratón", dev);
    return dev != 0 ? kOk : 0x8007000Eu;
}
u32 DI_EnumDevices(u32, u32, u32, u32, u32) { return kOk; }  // sin joysticks (XInput)
u32 DI_GetDeviceStatus(u32, u32) { return kOk; }
u32 DI_RunControlPanel(u32, u32, u32) { return kOk; }
u32 DI_Initialize(u32, u32, u32) { return kOk; }
u32 DI_FindDevice(u32, u32, u32, u32) { return kNotFound; }
u32 DI_EnumBySemantics(u32, u32, u32, u32, u32, u32) { return kOk; }
u32 DI_ConfigureDevices(u32, u32, u32, u32, u32) { return kUnsupported; }

// --- IDirectInputDevice8W ---
u32 D_GetCapabilities(u32 t, u32 caps) {
    if (caps == 0) return 0x80004003u;
    const u32 size = rd32(caps);
    if (size >= 44) std::memset(G<void>(caps + 4), 0, size - 4);
    put32(caps + 4, 0x1);  // DIDC_ATTACHED
    put32(caps + 8, G<Obj>(t)->kind == 1 ? 0x13 : 0x12);  // DI8DEVTYPE_KEYBOARD / MOUSE
    put32(caps + 12, G<Obj>(t)->kind == 1 ? 0 : 3);       // dwAxes
    put32(caps + 16, G<Obj>(t)->kind == 1 ? 128 : 4);     // dwButtons
    return kOk;
}
u32 D_EnumObjects(u32, u32, u32, u32) { return kOk; }
u32 D_GetProperty(u32, u32, u32) { return kUnsupported; }
u32 D_SetProperty(u32, u32, u32) { return kOk; }
u32 D_Acquire(u32) { return kOk; }
u32 D_Unacquire(u32) { return kOk; }
u32 D_GetDeviceState(u32, u32 cb, u32 data) {
    if (data != 0 && cb != 0 && cb <= 4096) std::memset(G<void>(data), 0, cb);
    return kOk;
}
u32 D_GetDeviceData(u32, u32, u32, u32 inOut, u32) { put32(inOut, 0); return kOk; }
u32 D_SetDataFormat(u32, u32) { return kOk; }
u32 D_SetEventNotification(u32, u32) { return kOk; }
u32 D_SetCooperativeLevel(u32, u32, u32) { return kOk; }
u32 D_GetObjectInfo(u32, u32, u32, u32) { return kNotFound; }
u32 D_GetDeviceInfo(u32, u32) { return kUnsupported; }
u32 D_RunControlPanel(u32, u32, u32) { return kOk; }
u32 D_Initialize(u32, u32, u32, u32) { return kOk; }
u32 D_CreateEffect(u32, u32, u32, u32 pp, u32) { put32(pp, 0); return kUnsupported; }
u32 D_EnumEffects(u32, u32, u32, u32) { return kOk; }
u32 D_GetEffectInfo(u32, u32, u32) { return kUnsupported; }
u32 D_GetForceFeedbackState(u32, u32 p) { put32(p, 0); return kUnsupported; }
u32 D_SendForceFeedbackCommand(u32, u32) { return kUnsupported; }
u32 D_EnumCreatedEffectObjects(u32, u32, u32, u32) { return kOk; }
u32 D_Escape(u32, u32) { return kUnsupported; }
u32 D_Poll(u32) { return 1; }  // DI_NOEFFECT
u32 D_SendDeviceData(u32, u32, u32, u32 inOut, u32) { put32(inOut, 0); return kOk; }
u32 D_EnumEffectsInFile(u32, u32, u32, u32, u32) { return kOk; }
u32 D_WriteEffectToFile(u32, u32, u32, u32, u32) { return kUnsupported; }
u32 D_BuildActionMap(u32, u32, u32, u32) { return kUnsupported; }
u32 D_SetActionMap(u32, u32, u32, u32) { return kUnsupported; }
u32 D_GetImageInfo(u32, u32) { return kUnsupported; }

void ensureVtables() {
    static std::once_flag once;
    std::call_once(once, [] {
        g_vtDI = makeVtable({
            w32arity::hpFn(&QI), w32arity::hpFn(&AddRef), w32arity::hpFn(&Release),
            w32arity::hpFn(&DI_CreateDevice), w32arity::hpFn(&DI_EnumDevices), w32arity::hpFn(&DI_GetDeviceStatus),
            w32arity::hpFn(&DI_RunControlPanel), w32arity::hpFn(&DI_Initialize), w32arity::hpFn(&DI_FindDevice),
            w32arity::hpFn(&DI_EnumBySemantics), w32arity::hpFn(&DI_ConfigureDevices),
        }, "IDirectInput8");
        g_vtDev = makeVtable({
            w32arity::hpFn(&QI), w32arity::hpFn(&AddRef), w32arity::hpFn(&Release),
            w32arity::hpFn(&D_GetCapabilities), w32arity::hpFn(&D_EnumObjects), w32arity::hpFn(&D_GetProperty),
            w32arity::hpFn(&D_SetProperty), w32arity::hpFn(&D_Acquire), w32arity::hpFn(&D_Unacquire),
            w32arity::hpFn(&D_GetDeviceState), w32arity::hpFn(&D_GetDeviceData), w32arity::hpFn(&D_SetDataFormat),
            w32arity::hpFn(&D_SetEventNotification), w32arity::hpFn(&D_SetCooperativeLevel),
            w32arity::hpFn(&D_GetObjectInfo), w32arity::hpFn(&D_GetDeviceInfo), w32arity::hpFn(&D_RunControlPanel),
            w32arity::hpFn(&D_Initialize), w32arity::hpFn(&D_CreateEffect), w32arity::hpFn(&D_EnumEffects),
            w32arity::hpFn(&D_GetEffectInfo), w32arity::hpFn(&D_GetForceFeedbackState),
            w32arity::hpFn(&D_SendForceFeedbackCommand), w32arity::hpFn(&D_EnumCreatedEffectObjects),
            w32arity::hpFn(&D_Escape), w32arity::hpFn(&D_Poll), w32arity::hpFn(&D_SendDeviceData),
            w32arity::hpFn(&D_EnumEffectsInFile), w32arity::hpFn(&D_WriteEffectToFile),
            w32arity::hpFn(&D_BuildActionMap), w32arity::hpFn(&D_SetActionMap), w32arity::hpFn(&D_GetImageInfo),
        }, "IDirectInputDevice8");
    });
}

// DirectInput8Create(hinst, version, riid, ppvOut, punkOuter) (stdcall)
u32 DirectInput8Create(u32, u32 version, u32, u32 ppv, u32) {
    ensureVtables();
    const u32 o = newObj(g_vtDI, 0);
    put32(ppv, o);
    LOGI("DirectInput8Create(0x%x) -> 0x%x", version, o);
    return o != 0 ? kOk : 0x8007000Eu;
}

}  // namespace

void* resolve(const std::string& name) {
    if (name == "DirectInput8Create") return w32arity::hpFn(&DirectInput8Create);
    return nullptr;
}

}  // namespace dinput32
}  // namespace hyperplay
