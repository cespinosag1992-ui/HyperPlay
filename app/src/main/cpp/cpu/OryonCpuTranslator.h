#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hyperplay {

struct LoadedPeImage;

struct TranslatedBlock {
    uintptr_t guestRip = 0;   // Dirección de origen x86/x86_64
    void* hostCode = nullptr; // Código nativo aarch64 emitido por el backend
    size_t guestSize = 0;
    size_t hostSize = 0;
    bool endsBlock = false;   // El bloque termina en salto/ret y fija RIP explícitamente
    // Inicio de cada instrucción x86: {offset en hostCode, offset desde guestRip}. Permite
    // traducir el PC host de un fallo a la RIP exacta de la instrucción invitada.
    std::vector<std::pair<uint32_t, uint32_t>> instrMap;
};

class HyperAotCache;

/// Contrato que debe cumplir un motor de traducción binaria real.
/// OryonCpuTranslator no sintetiza instrucciones por sí mismo: sin backend no hay ejecución.
class CpuTranslationBackend {
public:
    virtual ~CpuTranslationBackend() = default;

    virtual TranslatedBlock* translateBlock(uintptr_t guestRip) = 0;
    virtual int64_t executeEntry(void* entryPoint) = 0;
    virtual int64_t callDllEntry(void* entryPoint, void* dllBase) {
        (void)entryPoint; (void)dllBase; return 0;
    }
    virtual int64_t callDllEntryNested(void* entryPoint, void* dllBase) {
        (void)entryPoint; (void)dllBase; return 0;
    }
    virtual void stop() = 0;
    [[nodiscard]] virtual size_t translatedBlockCount() const noexcept = 0;

    virtual void setAotCache(HyperAotCache*) {}
    virtual void setCodeRange(uintptr_t, size_t) {}
    virtual void addCodeRange(uintptr_t, size_t) {}
    virtual void setGuestMode(bool) {}
    [[nodiscard]] virtual const std::string& lastError() const noexcept { static const std::string empty; return empty; }
};

class OryonCpuTranslator {
public:
    OryonCpuTranslator();
    ~OryonCpuTranslator();

    OryonCpuTranslator(const OryonCpuTranslator&) = delete;
    OryonCpuTranslator& operator=(const OryonCpuTranslator&) = delete;

    /// Adjunta la imagen PE ya cargada. Falla si no hay backend registrado: no existe traducción
    /// implícita en este componente.
    bool init(LoadedPeImage* image);

    /// Registra el motor de traducción real. Sin backend, init() y execute() fallan.
    bool setBackend(std::unique_ptr<CpuTranslationBackend> backend);

    [[nodiscard]] bool hasBackend() const noexcept { return backend_ != nullptr; }
    [[nodiscard]] std::string lastError() const { return backend_ ? backend_->lastError() : ""; }

    /// Traduce (o recupera de caché) el bloque que contiene `guestRip`.
    [[nodiscard]] TranslatedBlock* translateBlock(uintptr_t guestRip);

    /// Transfiere el control al punto de entrada mediante el backend registrado.
    int64_t execute(void* entryPoint);
    int64_t callDllEntry(void* entryPoint, void* dllBase);
    int64_t callDllEntryNested(void* entryPoint, void* dllBase);

    void stop();

    [[nodiscard]] size_t totalBlocksTranslated() const noexcept;

    void setAotCache(HyperAotCache* cache);
    void setCodeRange(uintptr_t base, size_t size);
    void addCodeRange(uintptr_t base, size_t size);
    void setGuestMode(bool is32Bit);

private:
    LoadedPeImage* image_ = nullptr;
    std::unique_ptr<CpuTranslationBackend> backend_;
};

}  // namespace hyperplay
