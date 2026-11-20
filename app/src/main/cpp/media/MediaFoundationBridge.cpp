#include "MediaFoundationBridge.h"

#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaDataSource.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#define MFLOGI(...) __android_log_print(ANDROID_LOG_INFO, "HyperPlay.MF", __VA_ARGS__)
#define MFLOGW(...) __android_log_print(ANDROID_LOG_WARN, "HyperPlay.MF", __VA_ARGS__)

namespace hyperplay::mf {
namespace {

RegisterFn g_register = nullptr;
CallGuestFn g_callGuest = nullptr;

// --- GUID y constantes -------------------------------------------------------------------------

struct Guid {
    uint32_t d1;
    uint16_t d2, d3;
    uint8_t d4[8];
    bool operator==(const Guid& o) const { return std::memcmp(this, &o, sizeof(Guid)) == 0; }
};
static_assert(sizeof(Guid) == 16);

constexpr Guid fourcc(uint32_t x) { return {x, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}}; }

constexpr Guid kMtMajorType{0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}};
constexpr Guid kMtSubtype{0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}};
constexpr Guid kMtFrameSize{0x1652c33d, 0xd6b2, 0x4012, {0xb8, 0x34, 0x72, 0x03, 0x08, 0x49, 0xa3, 0x7d}};
constexpr Guid kMtFrameRate{0xc459a2e8, 0x3d2c, 0x4e44, {0xb1, 0x32, 0xfe, 0xe5, 0x15, 0x6c, 0x7b, 0xb0}};
constexpr Guid kMtPixelAspect{0xc6376a1e, 0x8d0a, 0x4027, {0xbe, 0x45, 0x6d, 0x9a, 0x0a, 0xd3, 0x9b, 0xb6}};
constexpr Guid kMtDefaultStride{0x644b4e48, 0x1e02, 0x4516, {0xb0, 0xeb, 0xc0, 0x1c, 0xa9, 0xd4, 0x9a, 0xc6}};
constexpr Guid kMtInterlace{0xe2724bb8, 0xe676, 0x4806, {0xb4, 0xb2, 0xa8, 0xd6, 0xef, 0xb4, 0x4c, 0xcd}};
constexpr Guid kMtAllIndependent{0xc9173739, 0x5e56, 0x461c, {0xb7, 0x13, 0x46, 0xfb, 0x99, 0x5c, 0xb9, 0x5f}};
constexpr Guid kMtMpeg2Profile{0xad76a80b, 0x2d5c, 0x4e0b, {0xb3, 0x75, 0x64, 0xe5, 0x20, 0x13, 0x70, 0x36}};
constexpr Guid kMtSampleSize{0xdad3ab78, 0x1990, 0x408b, {0xbc, 0xe2, 0xeb, 0xa6, 0x73, 0xda, 0xcc, 0x10}};
constexpr Guid kMtAudioChannels{0x37e48bf5, 0x645e, 0x4c5b, {0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a}};
constexpr Guid kMtAudioRate{0x5faeeae7, 0x0290, 0x4c31, {0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba}};
constexpr Guid kMtAudioBits{0xf2deb57f, 0x40fa, 0x4764, {0xaa, 0x33, 0xed, 0x4f, 0x2d, 0x1f, 0xf6, 0x69}};
constexpr Guid kMtAudioBlockAlign{0x322de230, 0x9eeb, 0x43bd, {0xab, 0x7a, 0xff, 0x41, 0x22, 0x51, 0x54, 0x1d}};
constexpr Guid kMtAudioAvgBytes{0x1aab75c8, 0xcfef, 0x451c, {0xab, 0x95, 0xac, 0x03, 0x4b, 0x8e, 0x17, 0x31}};
constexpr Guid kPdDuration{0x6c990d33, 0xbb8e, 0x477a, {0x85, 0x98, 0x0d, 0x5d, 0x96, 0xfc, 0xd8, 0x8a}};
constexpr Guid kSrCharacteristics{0x6d23f5c8, 0xc5d7, 0x4a9b, {0x99, 0x71, 0x5d, 0x11, 0xf8, 0xbc, 0xa8, 0x80}};
constexpr Guid kSrAsyncCallback{0x1e3dbeac, 0xbb43, 0x4c35, {0xb5, 0x07, 0xcd, 0x64, 0x44, 0x64, 0xc9, 0x65}};

constexpr Guid kMediaVideo = fourcc(0x73646976);  // 'vids'
constexpr Guid kMediaAudio = fourcc(0x73647561);  // 'auds'
constexpr Guid kFmtH264 = fourcc(0x34363248);
constexpr Guid kFmtNV12 = fourcc(0x3231564E);
constexpr Guid kFmtRGB32 = fourcc(22);
constexpr Guid kFmtARGB32 = fourcc(21);
constexpr Guid kFmtYUY2 = fourcc(0x32595559);
constexpr Guid kFmtI420 = fourcc(0x30323449);
constexpr Guid kFmtIYUV = fourcc(0x56555949);
constexpr Guid kFmtAAC = fourcc(0x1610);
constexpr Guid kFmtPCM = fourcc(1);
constexpr Guid kFmtFloat = fourcc(3);

constexpr uint32_t S_OK_ = 0;
constexpr uint32_t E_NOTIMPL_ = 0x80004001u;
constexpr uint32_t E_NOINTERFACE_ = 0x80004002u;
constexpr uint32_t E_POINTER_ = 0x80004003u;
constexpr uint32_t E_FAIL_ = 0x80004005u;
constexpr uint32_t E_INVALIDARG_ = 0x80070057u;
constexpr uint32_t MF_E_INVALIDSTREAMNUMBER = 0xC00D36B3u;
constexpr uint32_t MF_E_INVALIDMEDIATYPE = 0xC00D36B4u;
constexpr uint32_t MF_E_NO_MORE_TYPES = 0xC00D36B9u;
constexpr uint32_t MF_E_INVALIDTYPE = 0xC00D36BDu;
constexpr uint32_t MF_E_ATTRIBUTENOTFOUND = 0xC00D36E6u;
constexpr uint32_t MF_E_UNSUPPORTED_BYTESTREAM_TYPE = 0xC00D36C4u;

constexpr uint32_t kStreamFirstVideo = 0xFFFFFFFCu;
constexpr uint32_t kStreamFirstAudio = 0xFFFFFFFDu;
constexpr uint32_t kStreamAll = 0xFFFFFFFEu;
constexpr uint32_t kReaderEndOfStream = 0x2u;

// MF_ATTRIBUTE_TYPE (= VARTYPE)
constexpr uint16_t kAtU32 = 19, kAtU64 = 21, kAtDouble = 5, kAtGuid = 72, kAtString = 31, kAtBlob = 0x1011,
                   kAtUnknown = 13;

template <typename T>
T* ptr(uint64_t a) { return reinterpret_cast<T*>(static_cast<uintptr_t>(a)); }

Guid readGuid(uint64_t a) {
    Guid g{};
    if (a != 0) std::memcpy(&g, ptr<void>(a), 16);
    return g;
}

std::string guidText(const Guid& g) {
    char b[48];
    std::snprintf(b, sizeof(b), "%08x-%04x-%04x-%02x%02x", g.d1, g.d2, g.d3, g.d4[0], g.d4[1]);
    return b;
}

// --- Llamadas a COM del huésped ----------------------------------------------------------------

uint64_t guestCall(uint64_t obj, int slot, std::initializer_list<uint64_t> rest) {
    if (obj == 0 || g_callGuest == nullptr) return E_POINTER_;
    const uint64_t vt = *ptr<uint64_t>(obj);
    const uint64_t fn = ptr<uint64_t>(vt)[slot];
    uint64_t args[8] = {obj};
    size_t n = 1;
    for (uint64_t v : rest) {
        if (n < 8) args[n++] = v;
    }
    return static_cast<uint64_t>(g_callGuest(static_cast<uintptr_t>(fn), args, n));
}

// --- Objetos COM host --------------------------------------------------------------------------

enum Kind : int { kAttr = 0, kMediaType, kSample, kBuffer, kResolver, kSource, kReader, kAsync, kKindCount };
constexpr size_t kSlots[kKindCount] = {33, 38, 47, 8, 10, 13, 13, 8};

uint64_t dispatch(int kind, int slot, const uint64_t* a);

template <int K, int S>
uint64_t slotFn(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
                uint64_t a7, uint64_t a8, uint64_t a9, uint64_t a10, uint64_t a11) {
    const uint64_t a[12] = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
    return dispatch(K, S, a);
}

template <int K, size_t... I>
std::array<void*, sizeof...(I)> makeVtable(std::index_sequence<I...>) {
    return {{reinterpret_cast<void*>(&slotFn<K, static_cast<int>(I)>)...}};
}

template <int K, size_t N>
struct Vt {
    static inline std::array<void*, N> slots = makeVtable<K>(std::make_index_sequence<N>{});
};

void** vtableFor(int kind) {
    switch (kind) {
        case kAttr: return Vt<kAttr, 33>::slots.data();
        case kMediaType: return Vt<kMediaType, 38>::slots.data();
        case kSample: return Vt<kSample, 47>::slots.data();
        case kBuffer: return Vt<kBuffer, 8>::slots.data();
        case kResolver: return Vt<kResolver, 10>::slots.data();
        case kSource: return Vt<kSource, 13>::slots.data();
        case kReader: return Vt<kReader, 13>::slots.data();
        default: return Vt<kAsync, 8>::slots.data();
    }
}

struct Obj {
    void** vtbl;
    std::atomic<uint32_t> refs{1};
    int kind;
    explicit Obj(int k) : vtbl(vtableFor(k)), kind(k) {}
};

int ourKind(uint64_t p) {
    if (p == 0) return -1;
    void** vt = *ptr<void**>(p);
    for (int k = 0; k < kKindCount; ++k) {
        if (vt == vtableFor(k)) return k;
    }
    return -1;
}

// Valor de atributo (IMFAttributes).
struct AttrValue {
    uint16_t type = 0;
    uint64_t u64 = 0;
    double dbl = 0;
    Guid guid{};
    std::u16string str;
    std::vector<uint8_t> blob;
};

struct AttrStore {
    std::recursive_mutex m;
    std::vector<std::pair<Guid, AttrValue>> items;
    AttrValue* find(const Guid& g) {
        for (auto& [k, v] : items) {
            if (k == g) return &v;
        }
        return nullptr;
    }
    AttrValue& set(const Guid& g) {
        if (AttrValue* v = find(g)) return *v;
        items.emplace_back(g, AttrValue{});
        return items.back().second;
    }
    void setU32(const Guid& g, uint32_t v) { auto& x = set(g); x = {}; x.type = kAtU32; x.u64 = v; }
    void setU64(const Guid& g, uint64_t v) { auto& x = set(g); x = {}; x.type = kAtU64; x.u64 = v; }
    void setGuid(const Guid& g, const Guid& v) { auto& x = set(g); x = {}; x.type = kAtGuid; x.guid = v; }
    bool getGuid(const Guid& g, Guid& out) {
        std::lock_guard<std::recursive_mutex> l(m);
        AttrValue* v = find(g);
        if (v == nullptr || v->type != kAtGuid) return false;
        out = v->guid;
        return true;
    }
    bool getU64(const Guid& g, uint64_t& out) {
        std::lock_guard<std::recursive_mutex> l(m);
        AttrValue* v = find(g);
        if (v == nullptr || (v->type != kAtU64 && v->type != kAtU32)) return false;
        out = v->u64;
        return true;
    }
    void copyFrom(AttrStore& o) {
        std::scoped_lock l(m, o.m);
        items = o.items;
    }
};

struct AttrObj : Obj {
    AttrStore attrs;
    explicit AttrObj(int k = kAttr) : Obj(k) {}
};

struct BufferObj : Obj {
    std::vector<uint8_t> data;
    uint32_t current = 0;
    BufferObj() : Obj(kBuffer) {}
};

struct SampleObj : AttrObj {
    int64_t time = 0, duration = 0;
    uint32_t flags = 0;
    std::vector<uint64_t> buffers;  // IMFMediaBuffer* (propios o del huésped), con referencia
    SampleObj() : AttrObj(kSample) {}
};

struct AsyncObj : Obj {
    uint64_t object = 0, callback = 0, state = 0;
    uint32_t status = 0;
    AsyncObj() : Obj(kAsync) {}
};

void addRef(uint64_t p) {
    if (p == 0) return;
    if (ourKind(p) >= 0) ptr<Obj>(p)->refs.fetch_add(1);
    else guestCall(p, 1, {});
}
void release(uint64_t p);

// --- Medio: datos en memoria + AMediaExtractor -------------------------------------------------

struct Track {
    size_t index = 0;
    bool video = false;
    std::string mime;
    int32_t width = 0, height = 0, rate = 0, channels = 0, profile = 0;
    double fps = 0;
    int64_t durationUs = 0;
    AMediaFormat* format = nullptr;
};

struct Media {
    std::vector<uint8_t> data;
    std::vector<Track> tracks;
    int64_t durationUs = 0;
    ~Media() {
        for (auto& t : tracks) {
            if (t.format) AMediaFormat_delete(t.format);
        }
    }
};

ssize_t memReadAt(void* ud, off64_t offset, void* buf, size_t size) {
    auto* m = static_cast<Media*>(ud);
    if (offset < 0 || static_cast<size_t>(offset) >= m->data.size()) return -1;  // EOS
    const size_t n = std::min(size, m->data.size() - static_cast<size_t>(offset));
    std::memcpy(buf, m->data.data() + offset, n);
    return static_cast<ssize_t>(n);
}
ssize_t memGetSize(void* ud) { return static_cast<ssize_t>(static_cast<Media*>(ud)->data.size()); }
void memClose(void*) {}

struct Extractor {
    AMediaDataSource* src = nullptr;
    AMediaExtractor* ex = nullptr;
    ~Extractor() {
        if (ex) AMediaExtractor_delete(ex);
        if (src) AMediaDataSource_delete(src);
    }
    bool open(Media& m) {
        src = AMediaDataSource_new();
        ex = AMediaExtractor_new();
        if (!src || !ex) return false;
        AMediaDataSource_setUserdata(src, &m);
        AMediaDataSource_setReadAt(src, &memReadAt);
        AMediaDataSource_setGetSize(src, &memGetSize);
        AMediaDataSource_setClose(src, &memClose);
        return AMediaExtractor_setDataSourceCustom(ex, src) == AMEDIA_OK;
    }
};

std::shared_ptr<Media> probeMedia(std::vector<uint8_t>&& bytes) {
    auto m = std::make_shared<Media>();
    m->data = std::move(bytes);
    Extractor e;
    if (!e.open(*m)) {
        MFLOGW("AMediaExtractor no abre el medio (%zu bytes)", m->data.size());
        return nullptr;
    }
    const size_t n = AMediaExtractor_getTrackCount(e.ex);
    for (size_t i = 0; i < n; ++i) {
        Track t;
        t.index = i;
        t.format = AMediaExtractor_getTrackFormat(e.ex, i);
        const char* mime = nullptr;
        AMediaFormat_getString(t.format, AMEDIAFORMAT_KEY_MIME, &mime);
        t.mime = mime ? mime : "";
        t.video = t.mime.rfind("video/", 0) == 0;
        if (!t.video && t.mime.rfind("audio/", 0) != 0) {
            AMediaFormat_delete(t.format);
            continue;
        }
        AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_WIDTH, &t.width);
        AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_HEIGHT, &t.height);
        AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &t.rate);
        AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &t.channels);
        AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_PROFILE, &t.profile);
        AMediaFormat_getInt64(t.format, AMEDIAFORMAT_KEY_DURATION, &t.durationUs);
        int32_t fr = 0;
        float frf = 0;
        if (AMediaFormat_getInt32(t.format, AMEDIAFORMAT_KEY_FRAME_RATE, &fr) && fr > 0) t.fps = fr;
        else if (AMediaFormat_getFloat(t.format, AMEDIAFORMAT_KEY_FRAME_RATE, &frf) && frf > 0) t.fps = frf;
        m->durationUs = std::max(m->durationUs, t.durationUs);
        MFLOGI("pista %zu: %s %dx%d fps=%.3f %d Hz x%d perfil=%d dur=%.2fs", i, t.mime.c_str(), t.width,
               t.height, t.fps, t.rate, t.channels, t.profile, t.durationUs / 1e6);
        m->tracks.push_back(std::move(t));
    }
    // fps ausente en el contenedor: contar muestras de la primera pista de vídeo.
    for (auto& t : m->tracks) {
        if (!t.video || t.fps > 0) continue;
        AMediaExtractor_selectTrack(e.ex, t.index);
        int frames = 0;
        int64_t last = 0;
        while (frames < 600) {
            if (AMediaExtractor_getSampleTrackIndex(e.ex) == static_cast<ssize_t>(t.index)) {
                ++frames;
                last = AMediaExtractor_getSampleTime(e.ex);
            }
            if (!AMediaExtractor_advance(e.ex)) break;
        }
        t.fps = (frames > 1 && last > 0) ? (frames - 1) * 1e6 / last : 30.0;
        MFLOGI("pista %zu: fps estimado %.3f", t.index, t.fps);
    }
    return m->tracks.empty() ? nullptr : m;
}

struct SourceObj : Obj {
    std::shared_ptr<Media> media;
    SourceObj() : Obj(kSource) {}
};

// --- Lector -------------------------------------------------------------------------------------

struct Packet {
    std::vector<uint8_t> bytes;
    int64_t pts = 0;
    uint32_t flags = 0;
};

struct Stream {
    Track* track = nullptr;
    bool selected = true;
    std::deque<Packet> queue;
    AMediaCodec* codec = nullptr;
    bool inputEos = false, outputEos = false;
    // Formato de salida pedido por el juego.
    Guid subtype{};
    bool haveCurrent = false;
    AttrStore current;
    int32_t colorFormat = 21, stride = 0, sliceHeight = 0, cropW = 0, cropH = 0;
    int32_t outRate = 0, outChannels = 0, pcmEncoding = 2;
    int64_t framesOut = 0;
};

struct ReaderObj : Obj {
    std::recursive_mutex m;
    std::shared_ptr<Media> media;
    std::unique_ptr<Extractor> ex;
    std::deque<Stream> streams;  // Stream no es movible (mutex del AttrStore)
    bool demuxEos = false;
    uint64_t callback = 0;
    std::deque<uint32_t> pending;  // ReadSample pedidos desde dentro del callback
    ReaderObj() : Obj(kReader) {}
    ~ReaderObj() {
        for (auto& s : streams) {
            if (s.codec) {
                AMediaCodec_stop(s.codec);
                AMediaCodec_delete(s.codec);
            }
        }
    }
};

thread_local int t_callbackDepth = 0;

int resolveStream(ReaderObj& r, uint32_t idx) {
    if (idx == kStreamFirstVideo || idx == kStreamFirstAudio) {
        for (size_t i = 0; i < r.streams.size(); ++i) {
            if (r.streams[i].track->video == (idx == kStreamFirstVideo)) return static_cast<int>(i);
        }
        return -1;
    }
    return idx < r.streams.size() ? static_cast<int>(idx) : -1;
}

void fillNativeType(AttrStore& a, const Track& t) {
    std::lock_guard<std::recursive_mutex> l(a.m);
    if (t.video) {
        a.setGuid(kMtMajorType, kMediaVideo);
        a.setGuid(kMtSubtype, kFmtH264);
        a.setU64(kMtFrameSize, (static_cast<uint64_t>(t.width) << 32) | static_cast<uint32_t>(t.height));
        const double fps = t.fps > 0 ? t.fps : 30.0;
        // 29.97/59.94 -> 30000/1001; el resto en milésimas.
        const double ntscBase = std::round(fps * 1.001);
        const bool ntsc = std::fabs(fps - std::round(fps)) > 0.01 && std::fabs(fps * 1.001 - ntscBase) < 0.01;
        const uint64_t num = ntsc ? static_cast<uint64_t>(ntscBase) * 1000 : static_cast<uint64_t>(std::llround(fps * 1000));
        const uint64_t den = ntsc ? 1001 : 1000;
        a.setU64(kMtFrameRate, (num << 32) | den);
        a.setU64(kMtPixelAspect, (1ull << 32) | 1);
        a.setU32(kMtInterlace, 2);  // MFVideoInterlace_Progressive
        // eAVEncH264VProfile: Base 66, Main 77, High 100 (Android: 1, 2, 8).
        const uint32_t prof = t.profile == 1 ? 66 : t.profile == 2 ? 77 : t.profile == 8 ? 100 : 77;
        a.setU32(kMtMpeg2Profile, prof);
    } else {
        a.setGuid(kMtMajorType, kMediaAudio);
        a.setGuid(kMtSubtype, kFmtAAC);
        a.setU32(kMtAudioChannels, static_cast<uint32_t>(t.channels));
        a.setU32(kMtAudioRate, static_cast<uint32_t>(t.rate));
        a.setU32(kMtAudioBits, 16);
    }
}

// Completa el tipo actual con lo que el juego no fijó (tamaño, stride, formato PCM).
void completeCurrentType(Stream& s) {
    AttrStore& a = s.current;
    std::lock_guard<std::recursive_mutex> l(a.m);
    const Track& t = *s.track;
    if (t.video) {
        a.setGuid(kMtMajorType, kMediaVideo);
        a.setU64(kMtFrameSize, (static_cast<uint64_t>(t.width) << 32) | static_cast<uint32_t>(t.height));
        if (a.find(kMtFrameRate) == nullptr) {
            AttrStore native;
            fillNativeType(native, t);
            uint64_t fr = 0;
            native.getU64(kMtFrameRate, fr);
            a.setU64(kMtFrameRate, fr);
        }
        a.setU64(kMtPixelAspect, (1ull << 32) | 1);
        a.setU32(kMtInterlace, 2);
        a.setU32(kMtAllIndependent, 1);
        const bool rgb = s.subtype == kFmtRGB32 || s.subtype == kFmtARGB32;
        const uint32_t stride = rgb ? t.width * 4u : (s.subtype == kFmtYUY2 ? t.width * 2u : static_cast<uint32_t>(t.width));
        a.setU32(kMtDefaultStride, stride);
        const uint32_t size = rgb ? stride * t.height : (s.subtype == kFmtYUY2 ? stride * t.height : t.width * t.height * 3u / 2u);
        a.setU32(kMtSampleSize, size);
    } else {
        a.setGuid(kMtMajorType, kMediaAudio);
        const bool flt = s.subtype == kFmtFloat;
        const uint32_t ch = static_cast<uint32_t>(t.channels > 0 ? t.channels : 2);
        const uint32_t rate = static_cast<uint32_t>(t.rate > 0 ? t.rate : 48000);
        const uint32_t bits = flt ? 32 : 16;
        a.setU32(kMtAudioChannels, ch);
        a.setU32(kMtAudioRate, rate);
        a.setU32(kMtAudioBits, bits);
        a.setU32(kMtAudioBlockAlign, ch * bits / 8);
        a.setU32(kMtAudioAvgBytes, rate * ch * bits / 8);
        a.setU32(kMtAllIndependent, 1);
    }
}

bool startCodec(Stream& s) {
    if (s.codec) return true;
    s.codec = AMediaCodec_createDecoderByType(s.track->mime.c_str());
    if (!s.codec) {
        MFLOGW("sin decodificador para %s", s.track->mime.c_str());
        return false;
    }
    AMediaFormat* f = s.track->format;
    if (s.track->video) AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, 21);  // NV12 en ByteBuffer
    if (AMediaCodec_configure(s.codec, f, nullptr, nullptr, 0) != AMEDIA_OK || AMediaCodec_start(s.codec) != AMEDIA_OK) {
        MFLOGW("no se pudo iniciar el decodificador %s", s.track->mime.c_str());
        AMediaCodec_delete(s.codec);
        s.codec = nullptr;
        return false;
    }
    MFLOGI("decodificador %s iniciado", s.track->mime.c_str());
    return true;
}

// Demultiplexa hasta tener un paquete para el stream `si` (o fin del archivo).
bool nextPacket(ReaderObj& r, size_t si, Packet& out) {
    Stream& s = r.streams[si];
    while (s.queue.empty()) {
        if (r.demuxEos) return false;
        const ssize_t ti = AMediaExtractor_getSampleTrackIndex(r.ex->ex);
        if (ti < 0) {
            r.demuxEos = true;
            return false;
        }
        for (auto& st : r.streams) {
            if (st.track->index != static_cast<size_t>(ti) || !st.selected) continue;
            Packet p;
            const ssize_t size = AMediaExtractor_getSampleSize(r.ex->ex);
            p.bytes.resize(size > 0 ? static_cast<size_t>(size) : 0);
            if (size > 0) AMediaExtractor_readSampleData(r.ex->ex, p.bytes.data(), p.bytes.size());
            p.pts = AMediaExtractor_getSampleTime(r.ex->ex);
            p.flags = AMediaExtractor_getSampleFlags(r.ex->ex);
            st.queue.push_back(std::move(p));
        }
        if (!AMediaExtractor_advance(r.ex->ex)) r.demuxEos = true;
    }
    out = std::move(s.queue.front());
    s.queue.pop_front();
    return true;
}

inline uint8_t clamp8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Convierte la salida NV12/I420 del decodificador al formato pedido.
void convertVideo(Stream& s, const uint8_t* src, size_t srcSize, std::vector<uint8_t>& dst) {
    const int w = s.track->width, h = s.track->height;
    const int stride = s.stride > 0 ? s.stride : w;
    const int slice = s.sliceHeight > 0 ? s.sliceHeight : h;
    const uint8_t* y = src;
    const bool planar = s.colorFormat == 19;  // COLOR_FormatYUV420Planar
    const uint8_t* uv = src + static_cast<size_t>(stride) * slice;
    const uint8_t* uP = uv;
    const uint8_t* vP = uv + static_cast<size_t>(stride / 2) * (slice / 2);
    const size_t needed = static_cast<size_t>(stride) * slice * 3 / 2;
    if (srcSize < needed - static_cast<size_t>(stride - w)) {
        dst.assign(static_cast<size_t>(w) * h * 4, 0);
        return;
    }
    const auto chroma = [&](int cx, int cy, int& u, int& v) {
        if (planar) {
            u = uP[cy * (stride / 2) + cx];
            v = vP[cy * (stride / 2) + cx];
        } else {
            u = uv[cy * stride + cx * 2];
            v = uv[cy * stride + cx * 2 + 1];
        }
    };
    if (s.subtype == kFmtNV12 || s.subtype == kFmtI420 || s.subtype == kFmtIYUV) {
        dst.resize(static_cast<size_t>(w) * h * 3 / 2);
        for (int r = 0; r < h; ++r) std::memcpy(dst.data() + static_cast<size_t>(r) * w, y + static_cast<size_t>(r) * stride, w);
        uint8_t* o = dst.data() + static_cast<size_t>(w) * h;
        for (int r = 0; r < h / 2; ++r) {
            for (int c = 0; c < w / 2; ++c) {
                int u, v;
                chroma(c, r, u, v);
                if (s.subtype == kFmtNV12) {
                    o[r * w + c * 2] = static_cast<uint8_t>(u);
                    o[r * w + c * 2 + 1] = static_cast<uint8_t>(v);
                } else {
                    o[r * (w / 2) + c] = static_cast<uint8_t>(u);
                    o[(w / 2) * (h / 2) + r * (w / 2) + c] = static_cast<uint8_t>(v);
                }
            }
        }
        return;
    }
    // RGB32/ARGB32 (BGRA en memoria) o YUY2. BT.709 rango limitado.
    const bool yuy2 = s.subtype == kFmtYUY2;
    dst.resize(static_cast<size_t>(w) * h * (yuy2 ? 2 : 4));
    for (int r = 0; r < h; ++r) {
        const uint8_t* yr = y + static_cast<size_t>(r) * stride;
        uint8_t* o = dst.data() + static_cast<size_t>(r) * w * (yuy2 ? 2 : 4);
        for (int c = 0; c < w; ++c) {
            int u, v;
            chroma(c / 2, r / 2, u, v);
            if (yuy2) {
                o[c * 2] = yr[c];
                o[c * 2 + 1] = static_cast<uint8_t>((c & 1) ? v : u);
                continue;
            }
            const int yy = (yr[c] - 16) * 298;
            const int d = u - 128, e = v - 128;
            o[c * 4 + 0] = clamp8((yy + 541 * d + 128) >> 8);            // B
            o[c * 4 + 1] = clamp8((yy - 55 * d - 136 * e + 128) >> 8);   // G
            o[c * 4 + 2] = clamp8((yy + 459 * e + 128) >> 8);            // R
            o[c * 4 + 3] = 255;
        }
    }
}

// Decodifica la siguiente salida del stream. Devuelve false en fin de stream.
bool decodeNext(ReaderObj& r, size_t si, std::vector<uint8_t>& out, int64_t& ptsUs, int64_t& durUs) {
    Stream& s = r.streams[si];
    if (s.outputEos || !startCodec(s)) return false;
    for (int iter = 0; iter < 4000; ++iter) {
        if (!s.inputEos) {
            const ssize_t ib = AMediaCodec_dequeueInputBuffer(s.codec, 0);
            if (ib >= 0) {
                Packet p;
                size_t cap = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(s.codec, static_cast<size_t>(ib), &cap);
                if (nextPacket(r, si, p) && buf != nullptr) {
                    const size_t n = std::min(cap, p.bytes.size());
                    std::memcpy(buf, p.bytes.data(), n);
                    AMediaCodec_queueInputBuffer(s.codec, static_cast<size_t>(ib), 0, n, static_cast<uint64_t>(p.pts), 0);
                } else {
                    AMediaCodec_queueInputBuffer(s.codec, static_cast<size_t>(ib), 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    s.inputEos = true;
                }
            }
        }
        AMediaCodecBufferInfo info{};
        const ssize_t ob = AMediaCodec_dequeueOutputBuffer(s.codec, &info, 2000);
        if (ob == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* f = AMediaCodec_getOutputFormat(s.codec);
            AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_COLOR_FORMAT, &s.colorFormat);
            AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_STRIDE, &s.stride);
            AMediaFormat_getInt32(f, "slice-height", &s.sliceHeight);
            AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, &s.outRate);
            AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &s.outChannels);
            AMediaFormat_getInt32(f, "pcm-encoding", &s.pcmEncoding);
            MFLOGI("salida %s: %s", s.track->mime.c_str(), AMediaFormat_toString(f));
            AMediaFormat_delete(f);
            continue;
        }
        if (ob < 0) continue;  // TRY_AGAIN / OUTPUT_BUFFERS_CHANGED
        size_t size = 0;
        uint8_t* data = AMediaCodec_getOutputBuffer(s.codec, static_cast<size_t>(ob), &size);
        const bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
        bool produced = false;
        if (data != nullptr && info.size > 0) {
            const uint8_t* p = data + info.offset;
            ptsUs = info.presentationTimeUs;
            if (s.track->video) {
                convertVideo(s, p, static_cast<size_t>(info.size), out);
                durUs = static_cast<int64_t>(1e6 / (s.track->fps > 0 ? s.track->fps : 30.0));
            } else {
                const int ch = s.outChannels > 0 ? s.outChannels : std::max(1, s.track->channels);
                const int rate = s.outRate > 0 ? s.outRate : std::max(1, s.track->rate);
                const bool srcFloat = s.pcmEncoding == 4;
                const size_t samples = static_cast<size_t>(info.size) / (srcFloat ? 4 : 2);
                if (s.subtype == kFmtFloat) {
                    out.resize(samples * 4);
                    auto* o = reinterpret_cast<float*>(out.data());
                    for (size_t i = 0; i < samples; ++i) {
                        if (srcFloat) std::memcpy(&o[i], p + i * 4, 4);
                        else {
                            int16_t v;
                            std::memcpy(&v, p + i * 2, 2);
                            o[i] = v / 32768.0f;
                        }
                    }
                } else {
                    out.resize(samples * 2);
                    auto* o = reinterpret_cast<int16_t*>(out.data());
                    for (size_t i = 0; i < samples; ++i) {
                        if (srcFloat) {
                            float v;
                            std::memcpy(&v, p + i * 4, 4);
                            o[i] = static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
                        } else {
                            std::memcpy(&o[i], p + i * 2, 2);
                        }
                    }
                }
                durUs = static_cast<int64_t>(samples / ch) * 1000000 / rate;
            }
            produced = true;
        }
        AMediaCodec_releaseOutputBuffer(s.codec, static_cast<size_t>(ob), false);
        if (eos) s.outputEos = true;
        if (produced) {
            ++s.framesOut;
            return true;
        }
        if (eos) return false;
    }
    MFLOGW("decodificador %s sin salida tras muchos intentos", s.track->mime.c_str());
    return false;
}

SampleObj* makeSample(std::vector<uint8_t>&& bytes, int64_t ptsUs, int64_t durUs) {
    auto* b = new BufferObj();
    b->data = std::move(bytes);
    b->current = static_cast<uint32_t>(b->data.size());
    auto* s = new SampleObj();
    s->buffers.push_back(reinterpret_cast<uint64_t>(b));
    s->time = ptsUs * 10;
    s->duration = durUs * 10;
    s->attrs.setU32(Guid{0x9cdf01d8, 0xa0f0, 0x43ba, {0xb0, 0x77, 0xea, 0xa0, 0x6c, 0xbd, 0x72, 0x8a}}, 1);  // CleanPoint
    return s;
}

std::atomic<int> s_logBudget{400};
bool logCall() { return s_logBudget.fetch_sub(1) > 0; }

// Una lectura: produce (stream real, flags, timestamp, sample).
void readOne(ReaderObj& r, uint32_t idx, uint32_t& actual, uint32_t& flags, int64_t& ts, SampleObj*& sample) {
    actual = idx;
    flags = 0;
    ts = 0;
    sample = nullptr;
    const int si = resolveStream(r, idx);
    if (si < 0) {
        flags = kReaderEndOfStream;
        return;
    }
    actual = static_cast<uint32_t>(si);
    std::vector<uint8_t> bytes;
    int64_t pts = 0, dur = 0;
    if (!decodeNext(r, static_cast<size_t>(si), bytes, pts, dur)) {
        flags = kReaderEndOfStream;
        MFLOGI("ReadSample(stream %d): fin de stream tras %lld muestras", si,
               static_cast<long long>(r.streams[si].framesOut));
        return;
    }
    ts = pts * 10;
    sample = makeSample(std::move(bytes), pts, dur);
}

uint64_t readerReadSample(ReaderObj& r, const uint64_t* a) {
    const auto idx = static_cast<uint32_t>(a[1]);
    if (r.callback == 0) {  // síncrono
        uint32_t actual, flags;
        int64_t ts;
        SampleObj* sample;
        {
            std::lock_guard<std::recursive_mutex> l(r.m);
            readOne(r, idx, actual, flags, ts, sample);
        }
        if (a[3]) *ptr<uint32_t>(a[3]) = actual;
        if (a[4]) *ptr<uint32_t>(a[4]) = flags;
        if (a[5]) *ptr<int64_t>(a[5]) = ts;
        if (a[6]) *ptr<uint64_t>(a[6]) = reinterpret_cast<uint64_t>(sample);
        else if (sample) release(reinterpret_cast<uint64_t>(sample));
        return S_OK_;
    }
    // Asíncrono: el callback se invoca en este hilo invitado. Si el juego vuelve a pedir desde
    // dentro de OnReadSample, se encola y se atiende al volver (sin recursión).
    {
        std::lock_guard<std::recursive_mutex> l(r.m);
        r.pending.push_back(idx);
        if (t_callbackDepth > 0) return S_OK_;
    }
    for (;;) {
        uint32_t req, actual, flags;
        int64_t ts;
        SampleObj* sample;
        {
            std::lock_guard<std::recursive_mutex> l(r.m);
            if (r.pending.empty()) break;
            req = r.pending.front();
            r.pending.pop_front();
            readOne(r, req, actual, flags, ts, sample);
        }
        ++t_callbackDepth;
        // IMFSourceReaderCallback::OnReadSample(hr, streamIndex, streamFlags, timestamp, sample)
        guestCall(r.callback, 3, {S_OK_, actual, flags, static_cast<uint64_t>(ts), reinterpret_cast<uint64_t>(sample)});
        --t_callbackDepth;
        if (sample) release(reinterpret_cast<uint64_t>(sample));
    }
    return S_OK_;
}

void flushReader(ReaderObj& r) {
    for (auto& s : r.streams) {
        s.queue.clear();
        if (s.codec) AMediaCodec_flush(s.codec);
        s.inputEos = s.outputEos = false;
    }
    r.demuxEos = false;
}

// --- IMFAttributes común -----------------------------------------------------------------------

uint64_t storeAttrOut(const AttrValue& v, uint64_t pv) {
    // PROPVARIANT: vt en +0, valor en +8.
    if (pv == 0) return S_OK_;
    auto* p = ptr<uint8_t>(pv);
    std::memset(p, 0, 24);
    uint16_t vt = v.type;
    if (v.type == kAtGuid) {
        auto* g = static_cast<Guid*>(std::malloc(16));  // CoTaskMem: el juego no la libera aquí
        *g = v.guid;
        std::memcpy(p + 8, &g, 8);
    } else {
        std::memcpy(p + 8, &v.u64, 8);
    }
    std::memcpy(p, &vt, 2);
    return S_OK_;
}

uint64_t attrMethod(AttrStore& st, int slot, const uint64_t* a) {
    std::lock_guard<std::recursive_mutex> l(st.m);
    // En GetCount/GetItemByIndex/CopyAllItems/Lock el primer argumento no es una clave.
    const bool keyed = slot >= 3 && slot <= 27;
    const Guid key = keyed ? readGuid(a[1]) : Guid{};
    AttrValue* v = (slot >= 3 && slot <= 17) ? st.find(key) : nullptr;
    switch (slot) {
        case 3:  // GetItem(key, pValue)
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            return storeAttrOut(*v, a[2]);
        case 4:  // GetItemType
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            *ptr<uint32_t>(a[2]) = v->type;
            return S_OK_;
        case 5:  // CompareItem(key, value, pbResult)
            if (a[3]) *ptr<int32_t>(a[3]) = 0;
            return S_OK_;
        case 6:  // Compare(pTheirs, type, pbResult)
            if (a[3]) *ptr<int32_t>(a[3]) = 0;
            return S_OK_;
        case 7:  // GetUINT32
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            if (v->type != kAtU32) return MF_E_INVALIDTYPE;
            *ptr<uint32_t>(a[2]) = static_cast<uint32_t>(v->u64);
            return S_OK_;
        case 8:  // GetUINT64
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            if (v->type != kAtU64) return MF_E_INVALIDTYPE;
            *ptr<uint64_t>(a[2]) = v->u64;
            return S_OK_;
        case 9:  // GetDouble
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            if (v->type != kAtDouble) return MF_E_INVALIDTYPE;
            *ptr<double>(a[2]) = v->dbl;
            return S_OK_;
        case 10:  // GetGUID
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            if (v->type != kAtGuid) return MF_E_INVALIDTYPE;
            std::memcpy(ptr<void>(a[2]), &v->guid, 16);
            return S_OK_;
        case 11:  // GetStringLength
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            *ptr<uint32_t>(a[2]) = static_cast<uint32_t>(v->str.size());
            return S_OK_;
        case 12: {  // GetString(key, pwsz, cch, pcch)
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            const size_t n = std::min<size_t>(v->str.size(), a[3] ? a[3] - 1 : 0);
            std::memcpy(ptr<void>(a[2]), v->str.data(), n * 2);
            ptr<char16_t>(a[2])[n] = 0;
            if (a[4]) *ptr<uint32_t>(a[4]) = static_cast<uint32_t>(n);
            return S_OK_;
        }
        case 13: {  // GetAllocatedString(key, ppwsz, pcch)
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            auto* s = static_cast<char16_t*>(std::malloc((v->str.size() + 1) * 2));
            std::memcpy(s, v->str.data(), v->str.size() * 2);
            s[v->str.size()] = 0;
            *ptr<uint64_t>(a[2]) = reinterpret_cast<uint64_t>(s);
            if (a[3]) *ptr<uint32_t>(a[3]) = static_cast<uint32_t>(v->str.size());
            return S_OK_;
        }
        case 14:  // GetBlobSize
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            *ptr<uint32_t>(a[2]) = static_cast<uint32_t>(v->blob.size());
            return S_OK_;
        case 15:  // GetBlob(key, pBuf, cbBufSize, pcbBlobSize)
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            std::memcpy(ptr<void>(a[2]), v->blob.data(), std::min<size_t>(v->blob.size(), a[3]));
            if (a[4]) *ptr<uint32_t>(a[4]) = static_cast<uint32_t>(v->blob.size());
            return S_OK_;
        case 16: {  // GetAllocatedBlob(key, ppBuf, pcbSize)
            if (!v) return MF_E_ATTRIBUTENOTFOUND;
            void* b = std::malloc(std::max<size_t>(1, v->blob.size()));
            std::memcpy(b, v->blob.data(), v->blob.size());
            *ptr<uint64_t>(a[2]) = reinterpret_cast<uint64_t>(b);
            if (a[3]) *ptr<uint32_t>(a[3]) = static_cast<uint32_t>(v->blob.size());
            return S_OK_;
        }
        case 17:  // GetUnknown(key, riid, ppv)
            if (!v || v->type != kAtUnknown) return MF_E_ATTRIBUTENOTFOUND;
            addRef(v->u64);
            *ptr<uint64_t>(a[3]) = v->u64;
            return S_OK_;
        case 18: {  // SetItem(key, PROPVARIANT*)
            const auto* p = ptr<uint8_t>(a[2]);
            uint16_t vt;
            std::memcpy(&vt, p, 2);
            AttrValue& x = st.set(key);
            x = {};
            x.type = vt;
            if (vt == kAtGuid) {
                uint64_t gp;
                std::memcpy(&gp, p + 8, 8);
                x.guid = readGuid(gp);
            } else {
                std::memcpy(&x.u64, p + 8, 8);
            }
            return S_OK_;
        }
        case 19:  // DeleteItem
            for (auto it = st.items.begin(); it != st.items.end(); ++it) {
                if (it->first == key) {
                    st.items.erase(it);
                    break;
                }
            }
            return S_OK_;
        case 20: st.items.clear(); return S_OK_;  // DeleteAllItems
        case 21: st.setU32(key, static_cast<uint32_t>(a[2])); return S_OK_;
        case 22: st.setU64(key, a[2]); return S_OK_;
        case 23: {
            AttrValue& x = st.set(key);
            x = {};
            x.type = kAtDouble;
            std::memcpy(&x.dbl, &a[2], 8);  // XMM2 no llega: el despachador pasa enteros; mejor esfuerzo
            return S_OK_;
        }
        case 24: st.setGuid(key, readGuid(a[2])); return S_OK_;
        case 25: {  // SetString
            AttrValue& x = st.set(key);
            x = {};
            x.type = kAtString;
            for (const char16_t* s = ptr<char16_t>(a[2]); s && *s; ++s) x.str.push_back(*s);
            return S_OK_;
        }
        case 26: {  // SetBlob(key, pBuf, cbBufSize)
            AttrValue& x = st.set(key);
            x = {};
            x.type = kAtBlob;
            x.blob.assign(ptr<uint8_t>(a[2]), ptr<uint8_t>(a[2]) + static_cast<uint32_t>(a[3]));
            return S_OK_;
        }
        case 27: {  // SetUnknown
            AttrValue* old = st.find(key);
            if (old && old->type == kAtUnknown) release(old->u64);
            AttrValue& x = st.set(key);
            x = {};
            x.type = kAtUnknown;
            x.u64 = a[2];
            addRef(a[2]);
            return S_OK_;
        }
        case 28: case 29: return S_OK_;  // LockStore / UnlockStore
        case 30: *ptr<uint32_t>(a[1]) = static_cast<uint32_t>(st.items.size()); return S_OK_;  // GetCount
        case 31: {  // GetItemByIndex(index, pguidKey, pValue)
            if (a[1] >= st.items.size()) return E_INVALIDARG_;
            const auto& [k, val] = st.items[a[1]];
            if (a[2]) std::memcpy(ptr<void>(a[2]), &k, 16);
            return storeAttrOut(val, a[3]);
        }
        case 32: {  // CopyAllItems(pDest)
            const int k = ourKind(a[1]);
            if (k == kAttr || k == kMediaType || k == kSample) ptr<AttrObj>(a[1])->attrs.copyFrom(st);
            return S_OK_;
        }
        default: return E_NOTIMPL_;
    }
}

// --- QI ----------------------------------------------------------------------------------------

bool kindSupports(int kind, uint32_t iid) {
    if (iid == 0x00000000u) return true;  // IUnknown
    switch (kind) {
        case kAttr: return iid == 0x2cd2d921u;
        case kMediaType: return iid == 0x2cd2d921u || iid == 0x44ae0fa8u || iid == 0xb99f381fu || iid == 0x26a0adc3u;
        case kSample: return iid == 0x2cd2d921u || iid == 0xc40a00f2u;
        case kBuffer: return iid == 0x045fa593u;
        case kResolver: return iid == 0xfbe5a32du;
        case kSource: return iid == 0x279a808du || iid == 0x2cd0bd52u;
        case kReader: return iid == 0x70ae66f2u;
        case kAsync: return iid == 0xac6b7889u;
        default: return false;
    }
}

void destroy(Obj* o) {
    switch (o->kind) {
        case kAttr: case kMediaType: delete static_cast<AttrObj*>(o); break;
        case kSample: {
            auto* s = static_cast<SampleObj*>(o);
            for (uint64_t b : s->buffers) release(b);
            delete s;
            break;
        }
        case kBuffer: delete static_cast<BufferObj*>(o); break;
        case kSource: delete static_cast<SourceObj*>(o); break;
        case kReader: {
            auto* r = static_cast<ReaderObj*>(o);
            if (r->callback) release(r->callback);
            delete r;
            break;
        }
        case kAsync: {
            auto* x = static_cast<AsyncObj*>(o);
            release(x->object);
            release(x->callback);
            release(x->state);
            delete x;
            break;
        }
        default: break;  // el resolver es estático
    }
}

void release(uint64_t p) {
    if (p == 0) return;
    if (ourKind(p) < 0) {
        guestCall(p, 2, {});
        return;
    }
    auto* o = ptr<Obj>(p);
    if (o->refs.fetch_sub(1) == 1 && o->kind != kResolver) destroy(o);
}

Obj s_resolver(kResolver);

// --- Despacho por objeto -----------------------------------------------------------------------

uint64_t bufferMethod(BufferObj& b, int slot, const uint64_t* a) {
    switch (slot) {
        case 3:  // Lock(ppbBuffer, pcbMaxLength, pcbCurrentLength)
            *ptr<uint64_t>(a[1]) = reinterpret_cast<uint64_t>(b.data.data());
            if (a[2]) *ptr<uint32_t>(a[2]) = static_cast<uint32_t>(b.data.size());
            if (a[3]) *ptr<uint32_t>(a[3]) = b.current;
            return S_OK_;
        case 4: return S_OK_;  // Unlock
        case 5: *ptr<uint32_t>(a[1]) = b.current; return S_OK_;
        case 6:
            if (a[1] > b.data.size()) return E_INVALIDARG_;
            b.current = static_cast<uint32_t>(a[1]);
            return S_OK_;
        case 7: *ptr<uint32_t>(a[1]) = static_cast<uint32_t>(b.data.size()); return S_OK_;
        default: return E_NOTIMPL_;
    }
}

uint64_t sampleMethod(SampleObj& s, int slot, const uint64_t* a) {
    switch (slot) {
        case 33: *ptr<uint32_t>(a[1]) = s.flags; return S_OK_;
        case 34: s.flags = static_cast<uint32_t>(a[1]); return S_OK_;
        case 35: *ptr<int64_t>(a[1]) = s.time; return S_OK_;
        case 36: s.time = static_cast<int64_t>(a[1]); return S_OK_;
        case 37: *ptr<int64_t>(a[1]) = s.duration; return S_OK_;
        case 38: s.duration = static_cast<int64_t>(a[1]); return S_OK_;
        case 39: *ptr<uint32_t>(a[1]) = static_cast<uint32_t>(s.buffers.size()); return S_OK_;
        case 40:  // GetBufferByIndex
            if (a[1] >= s.buffers.size()) return E_INVALIDARG_;
            addRef(s.buffers[a[1]]);
            *ptr<uint64_t>(a[2]) = s.buffers[a[1]];
            return S_OK_;
        case 41: {  // ConvertToContiguousBuffer
            if (s.buffers.empty()) return E_FAIL_;
            if (s.buffers.size() > 1 || ourKind(s.buffers[0]) != kBuffer) {
                auto* nb = new BufferObj();
                for (uint64_t bp : s.buffers) {
                    if (ourKind(bp) == kBuffer) {
                        auto* b = ptr<BufferObj>(bp);
                        nb->data.insert(nb->data.end(), b->data.begin(), b->data.begin() + b->current);
                    }
                }
                nb->current = static_cast<uint32_t>(nb->data.size());
                for (uint64_t bp : s.buffers) release(bp);
                s.buffers.assign(1, reinterpret_cast<uint64_t>(nb));
            }
            addRef(s.buffers[0]);
            *ptr<uint64_t>(a[1]) = s.buffers[0];
            return S_OK_;
        }
        case 42: addRef(a[1]); s.buffers.push_back(a[1]); return S_OK_;  // AddBuffer
        case 43:
            if (a[1] >= s.buffers.size()) return E_INVALIDARG_;
            release(s.buffers[a[1]]);
            s.buffers.erase(s.buffers.begin() + static_cast<long>(a[1]));
            return S_OK_;
        case 44: for (uint64_t b : s.buffers) release(b); s.buffers.clear(); return S_OK_;
        case 45: {
            uint32_t total = 0;
            for (uint64_t bp : s.buffers) {
                if (ourKind(bp) == kBuffer) total += ptr<BufferObj>(bp)->current;
            }
            *ptr<uint32_t>(a[1]) = total;
            return S_OK_;
        }
        case 46: {  // CopyToBuffer(pBuffer)
            if (ourKind(a[1]) != kBuffer) return E_NOTIMPL_;
            auto* dst = ptr<BufferObj>(a[1]);
            size_t off = 0;
            for (uint64_t bp : s.buffers) {
                if (ourKind(bp) != kBuffer) continue;
                auto* b = ptr<BufferObj>(bp);
                const size_t n = std::min<size_t>(b->current, dst->data.size() - off);
                std::memcpy(dst->data.data() + off, b->data.data(), n);
                off += n;
            }
            dst->current = static_cast<uint32_t>(off);
            return S_OK_;
        }
        default: return attrMethod(s.attrs, slot, a);
    }
}

uint64_t mediaTypeMethod(AttrObj& t, int slot, const uint64_t* a) {
    switch (slot) {
        case 33: {  // GetMajorType
            Guid g{};
            if (!t.attrs.getGuid(kMtMajorType, g)) return MF_E_ATTRIBUTENOTFOUND;
            std::memcpy(ptr<void>(a[1]), &g, 16);
            return S_OK_;
        }
        case 34: {  // IsCompressedFormat
            Guid g{};
            t.attrs.getGuid(kMtSubtype, g);
            *ptr<int32_t>(a[1]) = (g == kFmtH264 || g == kFmtAAC) ? 1 : 0;
            return S_OK_;
        }
        case 35: *ptr<uint32_t>(a[2]) = 0xF; return S_OK_;  // IsEqual: MF_MEDIATYPE_EQUAL_*
        case 36: case 37: return E_NOTIMPL_;
        default: return attrMethod(t.attrs, slot, a);
    }
}

// Lee MF_MT_SUBTYPE de un tipo (propio o del huésped).
bool typeSubtype(uint64_t type, Guid& out) {
    if (ourKind(type) == kMediaType || ourKind(type) == kAttr) return ptr<AttrObj>(type)->attrs.getGuid(kMtSubtype, out);
    Guid key = kMtSubtype;
    return guestCall(type, 10, {reinterpret_cast<uint64_t>(&key), reinterpret_cast<uint64_t>(&out)}) == S_OK_;
}

uint64_t readerMethod(ReaderObj& r, int slot, const uint64_t* a) {
    const auto idx = static_cast<uint32_t>(a[1]);
    switch (slot) {
        case 3: {  // GetStreamSelection(idx, pfSelected)
            std::lock_guard<std::recursive_mutex> l(r.m);
            const int si = resolveStream(r, idx);
            if (si < 0) return MF_E_INVALIDSTREAMNUMBER;
            *ptr<int32_t>(a[2]) = r.streams[si].selected ? 1 : 0;
            return S_OK_;
        }
        case 4: {  // SetStreamSelection(idx, fSelected)
            std::lock_guard<std::recursive_mutex> l(r.m);
            if (idx == kStreamAll) {
                for (auto& s : r.streams) s.selected = a[2] != 0;
                return S_OK_;
            }
            const int si = resolveStream(r, idx);
            if (si < 0) return MF_E_INVALIDSTREAMNUMBER;
            r.streams[si].selected = a[2] != 0;
            return S_OK_;
        }
        case 5: {  // GetNativeMediaType(idx, typeIndex, ppType)
            std::lock_guard<std::recursive_mutex> l(r.m);
            const int si = resolveStream(r, idx);
            if (si < 0) return MF_E_INVALIDSTREAMNUMBER;
            if (a[2] != 0) return MF_E_NO_MORE_TYPES;
            auto* t = new AttrObj(kMediaType);
            fillNativeType(t->attrs, *r.streams[si].track);
            *ptr<uint64_t>(a[3]) = reinterpret_cast<uint64_t>(t);
            return S_OK_;
        }
        case 6: {  // GetCurrentMediaType(idx, ppType)
            std::lock_guard<std::recursive_mutex> l(r.m);
            const int si = resolveStream(r, idx);
            if (si < 0) return MF_E_INVALIDSTREAMNUMBER;
            auto* t = new AttrObj(kMediaType);
            Stream& s = r.streams[si];
            if (s.haveCurrent) t->attrs.copyFrom(s.current);
            else fillNativeType(t->attrs, *s.track);
            *ptr<uint64_t>(a[2]) = reinterpret_cast<uint64_t>(t);
            return S_OK_;
        }
        case 7: {  // SetCurrentMediaType(idx, reserved, pType)
            std::lock_guard<std::recursive_mutex> l(r.m);
            const int si = resolveStream(r, idx);
            if (si < 0) return MF_E_INVALIDSTREAMNUMBER;
            Guid sub{};
            if (!typeSubtype(a[3], sub)) return MF_E_INVALIDMEDIATYPE;
            Stream& s = r.streams[si];
            const bool ok = s.track->video
                                ? (sub == kFmtNV12 || sub == kFmtRGB32 || sub == kFmtARGB32 || sub == kFmtI420 ||
                                   sub == kFmtIYUV || sub == kFmtYUY2)
                                : (sub == kFmtPCM || sub == kFmtFloat);
            MFLOGI("SetCurrentMediaType(stream %d, %s) -> %s", si, guidText(sub).c_str(), ok ? "aceptado" : "rechazado");
            if (!ok) return MF_E_INVALIDMEDIATYPE;
            s.subtype = sub;
            if (ourKind(a[3]) == kMediaType || ourKind(a[3]) == kAttr) s.current.copyFrom(ptr<AttrObj>(a[3])->attrs);
            else s.current.setGuid(kMtSubtype, sub);
            s.haveCurrent = true;
            completeCurrentType(s);
            return S_OK_;
        }
        case 8: {  // SetCurrentPosition(guidTimeFormat, varPosition)
            std::lock_guard<std::recursive_mutex> l(r.m);
            const auto* pv = ptr<uint8_t>(a[2]);
            int64_t pos = 0;
            if (pv) std::memcpy(&pos, pv + 8, 8);
            MFLOGI("SetCurrentPosition(%.3fs)", pos / 1e7);
            flushReader(r);
            AMediaExtractor_seekTo(r.ex->ex, pos / 10, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
            return S_OK_;
        }
        case 9: return readerReadSample(r, a);
        case 10: {  // Flush(idx)
            {
                std::lock_guard<std::recursive_mutex> l(r.m);
                const int64_t t = AMediaExtractor_getSampleTime(r.ex->ex);
                flushReader(r);
                if (t >= 0) AMediaExtractor_seekTo(r.ex->ex, t, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
                r.pending.clear();
            }
            if (r.callback) guestCall(r.callback, 4, {idx});  // OnFlush
            return S_OK_;
        }
        case 11:  // GetServiceForStream
            if (a[4]) *ptr<uint64_t>(a[4]) = 0;
            return E_NOINTERFACE_;
        case 12: {  // GetPresentationAttribute(idx, guidAttribute, pvarAttribute)
            const Guid key = readGuid(a[2]);
            auto* pv = ptr<uint8_t>(a[3]);
            std::memset(pv, 0, 24);
            if (key == kPdDuration) {
                const uint16_t vt = kAtU64;
                const uint64_t d = static_cast<uint64_t>(r.media->durationUs) * 10;
                std::memcpy(pv, &vt, 2);
                std::memcpy(pv + 8, &d, 8);
                return S_OK_;
            }
            if (key == kSrCharacteristics) {
                const uint16_t vt = kAtU32;
                const uint32_t c = 0x4 | 0x2;  // CAN_SEEK | CAN_PAUSE
                std::memcpy(pv, &vt, 2);
                std::memcpy(pv + 8, &c, 4);
                return S_OK_;
            }
            MFLOGW("GetPresentationAttribute(%s) no soportado", guidText(key).c_str());
            return MF_E_ATTRIBUTENOTFOUND;
        }
        default: return E_NOTIMPL_;
    }
}

uint64_t createReader(uint64_t sourceP, uint64_t attrsP, uint64_t ppReader) {
    if (ppReader == 0) return E_POINTER_;
    *ptr<uint64_t>(ppReader) = 0;
    if (ourKind(sourceP) != kSource) {
        MFLOGW("MFCreateSourceReaderFromMediaSource: fuente ajena");
        return E_INVALIDARG_;
    }
    auto* r = new ReaderObj();
    r->media = ptr<SourceObj>(sourceP)->media;
    r->ex = std::make_unique<Extractor>();
    if (!r->ex->open(*r->media)) {
        delete r;
        return E_FAIL_;
    }
    for (auto& t : r->media->tracks) {
        AMediaExtractor_selectTrack(r->ex->ex, t.index);
        r->streams.emplace_back();
        r->streams.back().track = &t;
    }
    if (ourKind(attrsP) == kAttr) {
        AttrStore& st = ptr<AttrObj>(attrsP)->attrs;
        std::lock_guard<std::recursive_mutex> l(st.m);
        if (AttrValue* v = st.find(kSrAsyncCallback); v && v->type == kAtUnknown) {
            r->callback = v->u64;
            addRef(r->callback);
        }
    }
    MFLOGI("SourceReader creado: %zu streams, %.2fs, modo %s", r->streams.size(), r->media->durationUs / 1e6,
           r->callback ? "asíncrono" : "síncrono");
    *ptr<uint64_t>(ppReader) = reinterpret_cast<uint64_t>(r);
    return S_OK_;
}

// IMFSourceResolver::CreateObjectFromByteStream(pByteStream, pwszURL, dwFlags, pProps, pObjectType, ppObject)
uint64_t resolverFromByteStream(const uint64_t* a) {
    const uint64_t bs = a[1];
    if (a[6]) *ptr<uint64_t>(a[6]) = 0;
    uint64_t length = 0;
    guestCall(bs, 4, {reinterpret_cast<uint64_t>(&length)});  // GetLength
    if (length == 0 || length > (1ull << 31)) {
        MFLOGW("CreateObjectFromByteStream: longitud %llu", static_cast<unsigned long long>(length));
        return MF_E_UNSUPPORTED_BYTESTREAM_TYPE;
    }
    uint64_t savedPos = 0;
    guestCall(bs, 6, {reinterpret_cast<uint64_t>(&savedPos)});  // GetCurrentPosition
    guestCall(bs, 7, {0});                                      // SetCurrentPosition(0)
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    size_t got = 0;
    while (got < bytes.size()) {
        uint32_t n = 0;
        const uint32_t want = static_cast<uint32_t>(std::min<size_t>(bytes.size() - got, 4u << 20));
        const auto hr = static_cast<uint32_t>(
            guestCall(bs, 9, {reinterpret_cast<uint64_t>(bytes.data() + got), want, reinterpret_cast<uint64_t>(&n)}));
        if (hr != S_OK_ || n == 0) break;
        got += n;
    }
    guestCall(bs, 7, {savedPos});
    bytes.resize(got);
    MFLOGI("CreateObjectFromByteStream: %zu/%llu bytes leídos", got, static_cast<unsigned long long>(length));
    auto media = probeMedia(std::move(bytes));
    if (!media) return MF_E_UNSUPPORTED_BYTESTREAM_TYPE;
    auto* src = new SourceObj();
    src->media = std::move(media);
    if (a[5]) *ptr<uint32_t>(a[5]) = 1;  // MF_OBJECT_MEDIASOURCE
    *ptr<uint64_t>(a[6]) = reinterpret_cast<uint64_t>(src);
    return S_OK_;
}

uint64_t dispatch(int kind, int slot, const uint64_t* a) {
    auto* self = ptr<Obj>(a[0]);
    if (self == nullptr) return E_POINTER_;
    if (slot == 0) {  // QueryInterface(riid, ppv)
        const Guid iid = readGuid(a[1]);
        if (!kindSupports(kind, iid.d1)) {
            if (logCall()) MFLOGW("QI kind=%d iid=%s -> E_NOINTERFACE", kind, guidText(iid).c_str());
            if (a[2]) *ptr<uint64_t>(a[2]) = 0;
            return E_NOINTERFACE_;
        }
        self->refs.fetch_add(1);
        *ptr<uint64_t>(a[2]) = a[0];
        return S_OK_;
    }
    if (slot == 1) return self->refs.fetch_add(1) + 1;
    if (slot == 2) {
        const uint32_t prev = self->refs.fetch_sub(1);
        if (prev == 1 && kind != kResolver) destroy(self);
        return prev - 1;
    }
    switch (kind) {
        case kAttr: return attrMethod(static_cast<AttrObj*>(self)->attrs, slot, a);
        case kMediaType: return mediaTypeMethod(*static_cast<AttrObj*>(self), slot, a);
        case kSample: return sampleMethod(*static_cast<SampleObj*>(self), slot, a);
        case kBuffer: return bufferMethod(*static_cast<BufferObj*>(self), slot, a);
        case kReader: {
            const uint64_t hr = readerMethod(*static_cast<ReaderObj*>(self), slot, a);
            if (slot != 9 && logCall()) MFLOGI("Reader slot %d -> 0x%08x", slot, static_cast<uint32_t>(hr));
            return hr;
        }
        case kResolver:
            if (slot == 4) return resolverFromByteStream(a);
            MFLOGW("SourceResolver slot %d no implementado", slot);
            return E_NOTIMPL_;
        case kSource:
            if (slot == 12) return S_OK_;  // Shutdown
            if (slot == 7) {               // GetCharacteristics
                *ptr<uint32_t>(a[1]) = 0x6;
                return S_OK_;
            }
            MFLOGW("MediaSource slot %d no implementado", slot);
            return E_NOTIMPL_;
        case kAsync: {
            auto* x = static_cast<AsyncObj*>(self);
            switch (slot) {
                case 3: addRef(x->state); *ptr<uint64_t>(a[1]) = x->state; return x->state ? S_OK_ : E_POINTER_;
                case 4: return x->status;
                case 5: x->status = static_cast<uint32_t>(a[1]); return S_OK_;
                case 6: addRef(x->object); *ptr<uint64_t>(a[1]) = x->object; return x->object ? S_OK_ : E_POINTER_;
                case 7: return x->state;
                default: return E_NOTIMPL_;
            }
        }
        default: return E_NOTIMPL_;
    }
}

// --- Exports -----------------------------------------------------------------------------------

uint64_t x_MFStartup(uint64_t, uint64_t) { MFLOGI("MFStartup"); return S_OK_; }
uint64_t x_ok() { return S_OK_; }
uint64_t x_MFCreateAttributes(uint64_t pp, uint64_t) {
    if (pp == 0) return E_POINTER_;
    *ptr<uint64_t>(pp) = reinterpret_cast<uint64_t>(new AttrObj(kAttr));
    return S_OK_;
}
uint64_t x_MFCreateMediaType(uint64_t pp) {
    if (pp == 0) return E_POINTER_;
    *ptr<uint64_t>(pp) = reinterpret_cast<uint64_t>(new AttrObj(kMediaType));
    return S_OK_;
}
uint64_t x_MFCreateSample(uint64_t pp) {
    if (pp == 0) return E_POINTER_;
    *ptr<uint64_t>(pp) = reinterpret_cast<uint64_t>(new SampleObj());
    return S_OK_;
}
uint64_t x_MFCreateMemoryBuffer(uint64_t cb, uint64_t pp) {
    if (pp == 0) return E_POINTER_;
    auto* b = new BufferObj();
    b->data.resize(static_cast<uint32_t>(cb));
    *ptr<uint64_t>(pp) = reinterpret_cast<uint64_t>(b);
    return S_OK_;
}
uint64_t x_MFCreateSourceResolver(uint64_t pp) {
    if (pp == 0) return E_POINTER_;
    s_resolver.refs.fetch_add(1);
    *ptr<uint64_t>(pp) = reinterpret_cast<uint64_t>(&s_resolver);
    return S_OK_;
}
uint64_t x_MFCreateSourceReaderFromMediaSource(uint64_t src, uint64_t attrs, uint64_t pp) {
    return createReader(src, attrs, pp);
}
uint64_t x_MFCreateAsyncResult(uint64_t obj, uint64_t cb, uint64_t state, uint64_t pp) {
    if (pp == 0) return E_POINTER_;
    auto* x = new AsyncObj();
    x->object = obj;
    x->callback = cb;
    x->state = state;
    addRef(obj);
    addRef(cb);
    addRef(state);
    *ptr<uint64_t>(pp) = reinterpret_cast<uint64_t>(x);
    return S_OK_;
}
uint64_t x_MFInvokeCallback(uint64_t result) {
    if (ourKind(result) != kAsync) return E_INVALIDARG_;
    auto* x = ptr<AsyncObj>(result);
    guestCall(x->callback, 4, {result});  // IMFAsyncCallback::Invoke
    return S_OK_;
}
uint64_t x_MFGetStrideForBitmapInfoHeader(uint64_t format, uint64_t width, uint64_t pStride) {
    const auto f = static_cast<uint32_t>(format), w = static_cast<uint32_t>(width);
    int32_t stride = static_cast<int32_t>(w);
    if (f == 21 || f == 22) stride = static_cast<int32_t>(w * 4);
    else if (f == 0x32595559u) stride = static_cast<int32_t>(w * 2);
    else if (f == 20) stride = static_cast<int32_t>((w * 3 + 3) & ~3u);
    if (pStride) *ptr<int32_t>(pStride) = stride;
    return S_OK_;
}
uint64_t x_MFTEnumEx(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t ppp, uint64_t pn) {
    if (ppp) *ptr<uint64_t>(ppp) = 0;
    if (pn) *ptr<uint32_t>(pn) = 0;
    return S_OK_;
}
uint64_t x_MFAverageTimePerFrameToFrameRate(uint64_t avg, uint64_t pNum, uint64_t pDen) {
    if (pNum) *ptr<uint32_t>(pNum) = 10000000;
    if (pDen) *ptr<uint32_t>(pDen) = static_cast<uint32_t>(avg ? avg : 333333);
    return S_OK_;
}
uint64_t x_notImpl() { return E_NOTIMPL_; }
uint64_t x_noInterface() { return E_NOINTERFACE_; }

}  // namespace

void init(RegisterFn registerFn, CallGuestFn callGuestFn) {
    static std::once_flag once;
    g_register = registerFn;
    g_callGuest = callGuestFn;
    std::call_once(once, [] {
        static const char* kNames[kKindCount] = {"IMFAttributes", "IMFMediaType", "IMFSample", "IMFMediaBuffer",
                                                 "IMFSourceResolver", "IMFMediaSource", "IMFSourceReader",
                                                 "IMFAsyncResult"};
        for (int k = 0; k < kKindCount; ++k) {
            void** vt = vtableFor(k);
            for (size_t i = 0; i < kSlots[k]; ++i) {
                const std::string name = std::string("mfplat!") + kNames[k] + "#" + std::to_string(i);
                g_register(vt[i], name.c_str());
            }
        }
    });
}

void* resolveExport(const char* name) {
    const std::string n = name ? name : "";
    void* p = nullptr;
    if (n == "MFStartup") p = reinterpret_cast<void*>(&x_MFStartup);
    else if (n == "MFShutdown" || n == "MFLockPlatform" || n == "MFUnlockPlatform") p = reinterpret_cast<void*>(&x_ok);
    else if (n == "MFCreateAttributes") p = reinterpret_cast<void*>(&x_MFCreateAttributes);
    else if (n == "MFCreateMediaType") p = reinterpret_cast<void*>(&x_MFCreateMediaType);
    else if (n == "MFCreateSample") p = reinterpret_cast<void*>(&x_MFCreateSample);
    else if (n == "MFCreateMemoryBuffer") p = reinterpret_cast<void*>(&x_MFCreateMemoryBuffer);
    else if (n == "MFCreateSourceResolver") p = reinterpret_cast<void*>(&x_MFCreateSourceResolver);
    else if (n == "MFCreateSourceReaderFromMediaSource") p = reinterpret_cast<void*>(&x_MFCreateSourceReaderFromMediaSource);
    else if (n == "MFCreateAsyncResult") p = reinterpret_cast<void*>(&x_MFCreateAsyncResult);
    else if (n == "MFInvokeCallback") p = reinterpret_cast<void*>(&x_MFInvokeCallback);
    else if (n == "MFGetStrideForBitmapInfoHeader") p = reinterpret_cast<void*>(&x_MFGetStrideForBitmapInfoHeader);
    else if (n == "MFTEnumEx") p = reinterpret_cast<void*>(&x_MFTEnumEx);
    else if (n == "MFAverageTimePerFrameToFrameRate") p = reinterpret_cast<void*>(&x_MFAverageTimePerFrameToFrameRate);
    else if (n == "MFGetService") p = reinterpret_cast<void*>(&x_noInterface);
    else if (n == "MFCreateDXGIDeviceManager" || n == "MFCreateVideoSampleAllocatorEx" || n == "MFCreateFile" ||
             n == "MFCreateSourceReaderFromURL" || n == "MFCreateSinkWriterFromURL") {
        p = reinterpret_cast<void*>(&x_notImpl);
    }
    if (p != nullptr) {
        MFLOGI("export %s", n.c_str());
        if (g_register) g_register(p, ("mfplat!" + n).c_str());
    } else {
        MFLOGW("export %s no implementado", n.c_str());
    }
    return p;
}

}  // namespace hyperplay::mf
