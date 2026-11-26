#pragma once

#include <aaudio/AAudio.h>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace hyperplay {

class DirectAAudioEngine {
public:
    DirectAAudioEngine();
    ~DirectAAudioEngine();

    DirectAAudioEngine(const DirectAAudioEngine&) = delete;
    DirectAAudioEngine& operator=(const DirectAAudioEngine&) = delete;

    /// Inicia el stream directo de hardware AAudio en modo baja latencia.
    /// minBufferMs > 0: búfer de AAudio de al menos ese tiempo (más margen contra underruns cuando el
    /// productor entrega bloques grandes); 0 = el mínimo de baja latencia (2 ráfagas).
    bool start(int32_t sampleRate = 48000, int32_t channelCount = 2, int32_t minBufferMs = 0);
    void stop();

    /// Escribe muestras de audio PCM directamente al hardware con mínima latencia.
    int32_t writePcm(const int16_t* pcmData, int32_t numFrames);

    [[nodiscard]] bool isRunning() const noexcept { return isRunning_.load(std::memory_order_relaxed); }
    [[nodiscard]] int32_t underrunCount() const;
    [[nodiscard]] uint64_t framesWritten() const noexcept {
        return framesWritten_.load(std::memory_order_relaxed);
    }

private:
    AAudioStream* stream_ = nullptr;
    std::atomic<bool> isRunning_{false};
    std::atomic<uint64_t> framesWritten_{0};
    mutable std::mutex mutex_;
};

}  // namespace hyperplay
