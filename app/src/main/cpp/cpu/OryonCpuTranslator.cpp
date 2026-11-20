#include "OryonCpuTranslator.h"
#include "OryonEmitterBackend.h"

#include <android/log.h>
#include <unordered_set>

#include "loader/BionicPeLoader.h"
#include "loader/NativeDllInterceptor.h"

#define LOG_TAG "HyperPlay.OryonTranslator"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {

OryonCpuTranslator::OryonCpuTranslator() {
    backend_ = std::make_unique<OryonEmitterBackend>();
    LOGI("OryonCpuTranslator inicializado con OryonEmitterBackend JIT nativo por defecto");
}

OryonCpuTranslator::~OryonCpuTranslator() {
    stop();
}

bool OryonCpuTranslator::setBackend(std::unique_ptr<CpuTranslationBackend> backend) {
    if (!backend) {
        LOGW("setBackend rechazado: puntero nulo");
        return false;
    }
    backend_ = std::move(backend);
    LOGI("Backend de traducción registrado");
    return true;
}

bool OryonCpuTranslator::init(LoadedPeImage* image) {
    if (image == nullptr || !image->isValid()) {
        LOGW("init rechazado: imagen PE inválida");
        return false;
    }
    if (!backend_) {
        LOGW("init rechazado: no hay backend de traducción registrado (OryonEmitterBackend ausente)");
        return false;
    }
    image_ = image;
    auto* oryon = dynamic_cast<OryonEmitterBackend*>(backend_.get());
    if (oryon != nullptr) {
        OryonEmitterBackend::setActiveBackend(oryon);
        EmitterConfig cfg;
        cfg.is32Bit = image->is32Bit;
        oryon->configure(cfg);
        oryon->setCodeRange(reinterpret_cast<uintptr_t>(image->baseAddress), image->totalMappedSize);
        std::unordered_set<void*> addedBases;
        for (const auto& kv : image->localDlls) {
            if (kv.second.base != nullptr && addedBases.insert(kv.second.base).second) {
                oryon->addCodeRange(reinterpret_cast<uintptr_t>(kv.second.base), kv.second.size);
            }
        }
        oryon->setNativeThunkHandler(&NativeDllInterceptor::dispatchThunk, oryon);
        oryon->setupImageTls(image->baseAddress, image->peInfo, image->localDlls, image->filePath);
    }
    LOGI("Traductor listo para imageBase=%p entry=%p", image->baseAddress, image->entryPoint);
    return true;
}

TranslatedBlock* OryonCpuTranslator::translateBlock(uintptr_t guestRip) {
    if (!backend_) return nullptr;
    return backend_->translateBlock(guestRip);
}

int64_t OryonCpuTranslator::execute(void* entryPoint) {
    if (entryPoint == nullptr) {
        LOGW("execute rechazado: entryPoint nulo");
        return -1;
    }
    if (!backend_) {
        LOGW("execute rechazado: no hay backend de traducción registrado");
        return -1;
    }
    LOGI("Transfiriendo control al entry point %p", entryPoint);
    return backend_->executeEntry(entryPoint);
}

int64_t OryonCpuTranslator::callDllEntry(void* entryPoint, void* dllBase) {
    if (entryPoint == nullptr || !backend_) return -1;
    return backend_->callDllEntry(entryPoint, dllBase);
}

int64_t OryonCpuTranslator::callDllEntryNested(void* entryPoint, void* dllBase) {
    if (entryPoint == nullptr || !backend_) return -1;
    return backend_->callDllEntryNested(entryPoint, dllBase);
}

void OryonCpuTranslator::stop() {
    if (backend_) backend_->stop();
    image_ = nullptr;
}

size_t OryonCpuTranslator::totalBlocksTranslated() const noexcept {
    return backend_ ? backend_->translatedBlockCount() : 0;
}

void OryonCpuTranslator::setAotCache(HyperAotCache* cache) {
    if (backend_) backend_->setAotCache(cache);
}

void OryonCpuTranslator::setCodeRange(uintptr_t base, size_t size) {
    if (backend_) backend_->setCodeRange(base, size);
}

void OryonCpuTranslator::addCodeRange(uintptr_t base, size_t size) {
    if (backend_) backend_->addCodeRange(base, size);
}

void OryonCpuTranslator::setGuestMode(bool is32Bit) {
    if (backend_) backend_->setGuestMode(is32Bit);
}

}  // namespace hyperplay
