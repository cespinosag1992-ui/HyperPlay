#include "FsbVorbis.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hyperplay::fmodlite {
namespace {

template <typename T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

constexpr uint32_t kFreq[] = {4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000};

}  // namespace

uint32_t crc32(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool parseFsb5(const uint8_t* fsb, size_t size, size_t baseOffset, Fsb5& out) {
    if (size < 0x3C || std::memcmp(fsb, "FSB5", 4) != 0) return false;
    const uint32_t version = rd<uint32_t>(fsb + 4);
    const uint32_t num = rd<uint32_t>(fsb + 8);
    const uint32_t shs = rd<uint32_t>(fsb + 12);
    const uint32_t nts = rd<uint32_t>(fsb + 16);
    const uint32_t ds = rd<uint32_t>(fsb + 20);
    out.codec = rd<uint32_t>(fsb + 24);
    const size_t hdr = version == 0 ? 0x40 : 0x3C;
    const size_t dataStart = hdr + shs + nts;
    if (dataStart + ds > size) return false;
    out.samples.clear();
    out.samples.reserve(num);
    size_t p = hdr;
    std::vector<size_t> offsets;
    for (uint32_t i = 0; i < num; ++i) {
        if (p + 8 > hdr + shs) return false;
        const uint64_t v = rd<uint64_t>(fsb + p);
        p += 8;
        FsbSample s;
        s.rate = kFreq[std::min<uint32_t>((v >> 1) & 0xF, 10)];
        const uint32_t ch = (v >> 5) & 0x3;
        s.channels = ch == 0 ? 1 : (ch == 1 ? 2 : (ch == 2 ? 6 : 8));
        const size_t off = static_cast<size_t>((v >> 7) & 0x7FFFFFFu) << 5;
        s.numSamples = static_cast<uint32_t>((v >> 34) & 0x3FFFFFFFu);
        bool more = (v & 1) != 0;
        while (more) {
            const uint32_t c = rd<uint32_t>(fsb + p);
            p += 4;
            more = (c & 1) != 0;
            const uint32_t csize = (c >> 1) & 0xFFFFFF;
            const uint32_t type = (c >> 25) & 0x7F;
            if (type == 1) s.channels = fsb[p];
            else if (type == 2) s.rate = rd<uint32_t>(fsb + p);
            else if (type == 3 && csize >= 8) { s.loopStart = rd<uint32_t>(fsb + p); s.loopEnd = rd<uint32_t>(fsb + p + 4); s.hasLoop = true; }
            else if (type == 11 && csize >= 4) s.vorbisCrc = rd<uint32_t>(fsb + p);
            p += csize;
        }
        offsets.push_back(off);
        s.dataOffset = baseOffset + dataStart + off;
        out.samples.push_back(s);
    }
    for (size_t i = 0; i < out.samples.size(); ++i) {
        const size_t end = i + 1 < offsets.size() ? offsets[i + 1] : ds;
        out.samples[i].dataSize = end > offsets[i] ? end - offsets[i] : 0;
    }
    // Tabla de nombres: uint32 offsets relativos al inicio de la tabla.
    if (nts >= 4 * num) {
        const uint8_t* names = fsb + hdr + shs;
        for (uint32_t i = 0; i < num; ++i) {
            const uint32_t o = rd<uint32_t>(names + 4 * i);
            if (o < nts) out.samples[i].name = reinterpret_cast<const char*>(names + o);
        }
    }
    return true;
}

size_t VorbisSetupTable::loadFromFmodDll(const std::vector<uint8_t>& dll, const std::vector<uint32_t>& crcs) {
    if (dll.size() < 0x200) return 0;
    const uint32_t pe = rd<uint32_t>(dll.data() + 0x3C);
    if (pe + 0x100 > dll.size()) return 0;
    const bool is64 = rd<uint16_t>(dll.data() + pe + 24) == 0x20B;
    const uint64_t base = is64 ? rd<uint64_t>(dll.data() + pe + 24 + 24) : rd<uint32_t>(dll.data() + pe + 24 + 28);
    const uint16_t nsec = rd<uint16_t>(dll.data() + pe + 6);
    const uint16_t opt = rd<uint16_t>(dll.data() + pe + 20);
    auto vaToOff = [&](uint64_t va) -> size_t {
        const uint64_t rva = va - base;
        for (uint16_t i = 0; i < nsec; ++i) {
            const uint8_t* s = dll.data() + pe + 24 + opt + i * 40;
            const uint32_t vsz = rd<uint32_t>(s + 8), v = rd<uint32_t>(s + 12), rsz = rd<uint32_t>(s + 16), raw = rd<uint32_t>(s + 20);
            if (rva >= v && rva < v + std::max(vsz, rsz)) return static_cast<size_t>(rva - v + raw);
        }
        return SIZE_MAX;
    };
    // Entradas {puntero, tamaño, crc32}: se busca el CRC y se lee lo anterior; se valida con el
    // CRC32 real de la cabecera (así no importa el layout exacto ni la versión de FMOD).
    for (uint32_t crc : crcs) {
        if (headers_.count(crc) != 0) continue;
        for (size_t p = 16; p + 4 <= dll.size(); p += 4) {
            if (rd<uint32_t>(dll.data() + p) != crc) continue;
            const uint32_t size = rd<uint32_t>(dll.data() + p - (is64 ? 4 : 4));
            const uint64_t ptr = is64 ? rd<uint64_t>(dll.data() + p - 16) : rd<uint32_t>(dll.data() + p - 8);
            const size_t off = vaToOff(ptr);
            if (off == SIZE_MAX || size == 0 || off + size > dll.size()) continue;
            if (dll[off] != 0x05 || crc32(dll.data() + off, size) != crc) continue;
            headers_[crc].assign(dll.begin() + static_cast<long>(off), dll.begin() + static_cast<long>(off + size));
            break;
        }
    }
    return headers_.size();
}

const std::vector<uint8_t>* VorbisSetupTable::find(uint32_t crc) const {
    auto it = headers_.find(crc);
    return it == headers_.end() ? nullptr : &it->second;
}

bool decodeVorbisSample(const uint8_t* bank, const FsbSample& s, const VorbisSetupTable& setups,
                        std::vector<int16_t>& pcm, std::string* error) {
    auto fail = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    const std::vector<uint8_t>* setup = setups.find(s.vorbisCrc);
    if (setup == nullptr) return fail("sin cabecera setup para el CRC");
    // Cabecera de identificación (30 bytes): bloques 256/2048 (0xB8), como FMOD.
    uint8_t ident[30] = {0x01, 'v', 'o', 'r', 'b', 'i', 's'};
    ident[11] = static_cast<uint8_t>(s.channels);
    std::memcpy(ident + 12, &s.rate, 4);
    ident[28] = 0xB8;
    ident[29] = 0x01;

    AMediaCodec* codec = AMediaCodec_createDecoderByType("audio/vorbis");
    if (codec == nullptr) return fail("sin decodificador Vorbis");
    AMediaFormat* fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "audio/vorbis");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, static_cast<int32_t>(s.rate));
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, static_cast<int32_t>(s.channels));
    AMediaFormat_setBuffer(fmt, "csd-0", ident, sizeof(ident));
    AMediaFormat_setBuffer(fmt, "csd-1", const_cast<uint8_t*>(setup->data()), setup->size());
    const media_status_t cfg = AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0);
    const media_status_t st = cfg == AMEDIA_OK ? AMediaCodec_start(codec) : cfg;
    const bool configured = cfg == AMEDIA_OK && st == AMEDIA_OK;
    AMediaFormat_delete(fmt);
    if (!configured) {
        AMediaCodec_delete(codec);
        return fail("AMediaCodec no aceptó el formato");
    }

    pcm.clear();
    pcm.reserve(static_cast<size_t>(s.numSamples) * s.channels);
    const uint8_t* p = bank + s.dataOffset;
    const uint8_t* end = p + s.dataSize;
    bool inputDone = false;
    int idle = 0;
    int64_t pts = 0;
    // Tope de vueltas: un decodificador que no avanza no debe colgar al llamador.
    for (int guard = 0; guard < 200000; ++guard) {
        if (!inputDone) {
            const ssize_t ib = AMediaCodec_dequeueInputBuffer(codec, 2000);
            if (ib >= 0) {
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(ib), &cap);
                uint16_t len = 0;
                if (p + 2 <= end) len = rd<uint16_t>(p);
                if (len == 0 || p + 2 + len > end || len + 4u > cap) {
                    AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(ib), 0, 0, pts, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputDone = true;
                } else {
                    // Paquete + int32 "fotogramas de la página" (-1 = desconocido), como lo deja el
                    // extractor Ogg de Android para su decodificador Vorbis.
                    std::memcpy(buf, p + 2, len);
                    const int32_t unknown = -1;  // sin el entero, los estéreo no producen audio
                    std::memcpy(buf + len, &unknown, 4);
                    AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(ib), 0, len + 4u, pts, 0);
                    p += 2 + len;
                    pts += 1000;
                }
            }
        }
        AMediaCodecBufferInfo info;
        const ssize_t ob = AMediaCodec_dequeueOutputBuffer(codec, &info, 2000);
        if (ob >= 0) {
            idle = 0;
            size_t cap = 0;
            const uint8_t* buf = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(ob), &cap);
            if (buf != nullptr && info.size > 0) {
                const auto* s16 = reinterpret_cast<const int16_t*>(buf + info.offset);
                pcm.insert(pcm.end(), s16, s16 + info.size / 2);
            }
            const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(ob), false);
            if (eos) break;
        } else if (ob == AMEDIACODEC_INFO_TRY_AGAIN_LATER && inputDone && ++idle > 200) {
            break;  // el decodificador no marcó fin: nada más que sacar
        } else if (ob == AMEDIACODEC_INFO_TRY_AGAIN_LATER && !inputDone && ++idle > 5000) {
            break;  // ni acepta entrada ni produce salida: atascado
        }
    }
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    // FMOD indica el número exacto de muestras: se recorta el relleno del final.
    const size_t want = static_cast<size_t>(s.numSamples) * s.channels;
    if (want > 0 && pcm.size() > want) pcm.resize(want);
    if (pcm.empty()) return fail("el decodificador no produjo audio");
    return true;
}

// ------------------------------------------------------------------ decodificación por bloques

VorbisStream::~VorbisStream() { close(); }

void VorbisStream::close() {
    if (codec_ != nullptr) {
        auto* codec = static_cast<AMediaCodec*>(codec_);
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        codec_ = nullptr;
    }
}

bool VorbisStream::open(const uint8_t* bank, const FsbSample& s, const VorbisSetupTable& setups, std::string* error) {
    close();
    bank_ = bank;
    sample_ = s;
    setup_ = setups.find(s.vorbisCrc);
    if (setup_ == nullptr) {
        if (error) *error = "sin cabecera setup para el CRC";
        return false;
    }
    if (!start()) {
        if (error) *error = "AMediaCodec no aceptó el formato";
        return false;
    }
    return true;
}

bool VorbisStream::start() {
    uint8_t ident[30] = {0x01, 'v', 'o', 'r', 'b', 'i', 's'};
    ident[11] = static_cast<uint8_t>(sample_.channels);
    std::memcpy(ident + 12, &sample_.rate, 4);
    ident[28] = 0xB8;
    ident[29] = 0x01;
    AMediaCodec* codec = AMediaCodec_createDecoderByType("audio/vorbis");
    if (codec == nullptr) return false;
    AMediaFormat* fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, "audio/vorbis");
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, static_cast<int32_t>(sample_.rate));
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, static_cast<int32_t>(sample_.channels));
    AMediaFormat_setBuffer(fmt, "csd-0", ident, sizeof(ident));
    AMediaFormat_setBuffer(fmt, "csd-1", const_cast<uint8_t*>(setup_->data()), setup_->size());
    const bool ok = AMediaCodec_configure(codec, fmt, nullptr, nullptr, 0) == AMEDIA_OK && AMediaCodec_start(codec) == AMEDIA_OK;
    AMediaFormat_delete(fmt);
    if (!ok) {
        AMediaCodec_delete(codec);
        return false;
    }
    codec_ = codec;
    p_ = bank_ + sample_.dataOffset;
    end_ = p_ + sample_.dataSize;
    inputDone_ = false;
    finished_ = false;
    pts_ = 0;
    produced_ = 0;
    return true;
}

bool VorbisStream::restart() {
    close();
    return setup_ != nullptr && start();
}

size_t VorbisStream::read(std::vector<int16_t>& out, size_t frames) {
    if (codec_ == nullptr || finished_) return 0;
    auto* codec = static_cast<AMediaCodec*>(codec_);
    const size_t ch = sample_.channels;
    const uint64_t total = sample_.numSamples;
    size_t got = 0;
    int idle = 0;
    while (got < frames && !finished_) {
        if (!inputDone_) {
            const ssize_t ib = AMediaCodec_dequeueInputBuffer(codec, 0);
            if (ib >= 0) {
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(codec, static_cast<size_t>(ib), &cap);
                uint16_t len = 0;
                if (p_ + 2 <= end_) len = rd<uint16_t>(p_);
                if (len == 0 || p_ + 2 + len > end_ || len + 4u > cap) {
                    AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(ib), 0, 0, pts_, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputDone_ = true;
                } else {
                    std::memcpy(buf, p_ + 2, len);
                    const int32_t unknown = -1;
                    std::memcpy(buf + len, &unknown, 4);
                    AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(ib), 0, len + 4u, pts_, 0);
                    p_ += 2 + len;
                    pts_ += 1000;
                }
            }
        }
        AMediaCodecBufferInfo info;
        const ssize_t ob = AMediaCodec_dequeueOutputBuffer(codec, &info, inputDone_ ? 2000 : 500);
        if (ob >= 0) {
            idle = 0;
            size_t cap = 0;
            const uint8_t* buf = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(ob), &cap);
            if (buf != nullptr && info.size > 0) {
                const auto* s16 = reinterpret_cast<const int16_t*>(buf + info.offset);
                size_t n = static_cast<size_t>(info.size) / 2 / ch;
                if (total > 0 && produced_ + n > total) n = produced_ < total ? static_cast<size_t>(total - produced_) : 0;
                out.insert(out.end(), s16, s16 + n * ch);
                produced_ += n;
                got += n;
            }
            const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(ob), false);
            if (eos || (total > 0 && produced_ >= total)) finished_ = true;
        } else if (ob == AMEDIACODEC_INFO_TRY_AGAIN_LATER && ++idle > (inputDone_ ? 200 : 4000)) {
            finished_ = inputDone_;
            break;
        }
    }
    return got;
}

}  // namespace hyperplay::fmodlite
