#include "DirectAAudioEngine.h"

#include <android/log.h>
#include <unistd.h>

#define LOG_TAG "HyperPlay.DirectAudio"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace {

// Espera activa acotada: AAudio no ofrece un wait con timeout portable para el estado del stream.
constexpr int kStopWaitIterations = 50;
constexpr useconds_t kStopWaitSleepUs = 4000;

}  // namespace

DirectAAudioEngine::DirectAAudioEngine() = default;

DirectAAudioEngine::~DirectAAudioEngine() {
    stop();
}

bool DirectAAudioEngine::start(int32_t sampleRate, int32_t channelCount, int32_t minBufferMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (isRunning_.load(std::memory_order_relaxed) && stream_ != nullptr) return true;
    if (sampleRate <= 0 || channelCount <= 0 || channelCount > 8) {
        LOGW("Parámetros AAudio inválidos: %d Hz, %d canales", sampleRate, channelCount);
        return false;
    }

    // Un stream previo (parado o desconectado) debe cerrarse antes de abrir otro.
    if (stream_ != nullptr) {
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }

    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr) {
        LOGW("Fallo al crear AAudioStreamBuilder");
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(builder, sampleRate);
    AAudioStreamBuilder_setChannelCount(builder, channelCount);

    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream_);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK || stream_ == nullptr) {
        LOGW("Fallo al abrir stream AAudio: %s", AAudio_convertResultToText(result));
        stream_ = nullptr;
        return false;
    }

    const int32_t capacity = AAudioStream_getBufferCapacityInFrames(stream_);
    const int32_t burst = AAudioStream_getFramesPerBurst(stream_);
    if (capacity > 0 && burst > 0) {
        int32_t want = burst * 2;
        if (minBufferMs > 0) {
            const int32_t rate = AAudioStream_getSampleRate(stream_) > 0 ? AAudioStream_getSampleRate(stream_) : sampleRate;
            const int32_t frames = rate * minBufferMs / 1000;
            want = ((frames + burst - 1) / burst) * burst;  // múltiplo de ráfaga
        }
        const int32_t target = (want < capacity) ? want : capacity;
        const aaudio_result_t bufferResult = AAudioStream_setBufferSizeInFrames(stream_, target);
        if (bufferResult < 0) {
            LOGW("AAudioStream_setBufferSizeInFrames(%d) falló: %s", target,
                 AAudio_convertResultToText(bufferResult));
        }
    }

    result = AAudioStream_requestStart(stream_);
    if (result != AAUDIO_OK) {
        LOGW("AAudioStream_requestStart falló: %s", AAudio_convertResultToText(result));
        AAudioStream_close(stream_);
        stream_ = nullptr;
        return false;
    }

    framesWritten_.store(0, std::memory_order_relaxed);
    isRunning_.store(true, std::memory_order_relaxed);
    LOGI("DirectAAudioEngine iniciado: %d Hz, %d canales, buffer %d frames",
         AAudioStream_getSampleRate(stream_), AAudioStream_getChannelCount(stream_),
         AAudioStream_getBufferSizeInFrames(stream_));
    return true;
}

void DirectAAudioEngine::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_ == nullptr) {
        isRunning_.store(false, std::memory_order_relaxed);
        return;
    }

    if (isRunning_.load(std::memory_order_relaxed)) {
        const aaudio_result_t result = AAudioStream_requestStop(stream_);
        if (result != AAUDIO_OK) {
            LOGW("AAudioStream_requestStop falló: %s", AAudio_convertResultToText(result));
        }
    }

    // Cerrar mientras el stream sigue en STARTED/STOPPING provoca USE_AFTER_FREE en el servicio
    // de audio; se espera a STOPPED (o DISCONNECTED/CLOSED) con un límite de tiempo.
    for (int i = 0; i < kStopWaitIterations; ++i) {
        const aaudio_stream_state_t state = AAudioStream_getState(stream_);
        if (state == AAUDIO_STREAM_STATE_STOPPED || state == AAUDIO_STREAM_STATE_CLOSED ||
            state == AAUDIO_STREAM_STATE_DISCONNECTED) {
            break;
        }
        usleep(kStopWaitSleepUs);
    }

    const aaudio_result_t closeResult = AAudioStream_close(stream_);
    if (closeResult != AAUDIO_OK) {
        LOGW("AAudioStream_close falló: %s", AAudio_convertResultToText(closeResult));
    }
    stream_ = nullptr;
    isRunning_.store(false, std::memory_order_relaxed);
    LOGI("DirectAAudioEngine detenido");
}

int32_t DirectAAudioEngine::writePcm(const int16_t* pcmData, int32_t numFrames) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isRunning_.load(std::memory_order_relaxed) || stream_ == nullptr ||
        pcmData == nullptr || numFrames <= 0) {
        return 0;
    }

    constexpr int64_t kWriteTimeoutNs = 10000000;  // 10 ms
    const aaudio_result_t result =
        AAudioStream_write(stream_, pcmData, numFrames, kWriteTimeoutNs);

    if (result == AAUDIO_ERROR_DISCONNECTED) {
        LOGW("Stream AAudio desconectado; se detiene la reproducción");
        isRunning_.store(false, std::memory_order_relaxed);
        return 0;
    }
    if (result == AAUDIO_ERROR_TIMEOUT) return 0;  // buffer lleno: no es un fallo
    if (result < 0) {
        LOGW("AAudioStream_write falló: %s", AAudio_convertResultToText(result));
        return 0;
    }

    framesWritten_.fetch_add(static_cast<uint64_t>(result), std::memory_order_relaxed);
    return result;
}

int32_t DirectAAudioEngine::underrunCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_ == nullptr) return 0;
    const int32_t xruns = AAudioStream_getXRunCount(stream_);
    return xruns > 0 ? xruns : 0;
}

}  // namespace hyperplay
