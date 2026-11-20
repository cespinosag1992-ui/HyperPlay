#pragma once

// Registro de Windows en memoria con persistencia por juego.
//
// Unity guarda PlayerPrefs en HKCU\Software\<Company>\<Product> (RegCreateKeyExW +
// RegSetValueExW/RegQueryValueExW/RegEnumValueW). Este modelo es independiente del JIT y de
// los thunks para poder probarlo en la suite nativa: nombres UTF-16 sin distinguir mayúsculas
// (ASCII), handles opacos, códigos LSTATUS reales y volcado binario a un archivo tras cada cambio
// de HKCU/HKU. HKLM lleva unas pocas claves de solo lectura (CPU, versión de Windows) que no se
// persisten.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace win32reg {

constexpr uint32_t kSuccess = 0;
constexpr uint32_t kFileNotFound = 2;
constexpr uint32_t kInvalidHandle = 6;
constexpr uint32_t kInvalidParameter = 87;
constexpr uint32_t kMoreData = 234;
constexpr uint32_t kNoMoreItems = 259;

constexpr uint32_t kRegNone = 0;
constexpr uint32_t kRegSz = 1;
constexpr uint32_t kRegExpandSz = 2;
constexpr uint32_t kRegBinary = 3;
constexpr uint32_t kRegDword = 4;
constexpr uint32_t kRegMultiSz = 7;
constexpr uint32_t kRegQword = 11;

constexpr uint32_t kCreatedNewKey = 1;
constexpr uint32_t kOpenedExistingKey = 2;

// Handles devueltos al huésped: fuera del rango de las claves predefinidas (0x8000000x).
constexpr uint64_t kHandleBase = 0x52450000;

inline std::u16string lowerAscii(std::u16string s) {
    for (char16_t& c : s) {
        if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c - u'A' + u'a');
    }
    return s;
}

inline std::u16string fromUtf8(const char* s) {
    std::u16string out;
    if (s == nullptr) return out;
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    while (*p != 0) {
        uint32_t cp = *p++;
        if (cp >= 0xC0 && cp < 0xE0 && *p != 0) {
            cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
        } else if (cp >= 0xE0 && cp < 0xF0 && p[0] != 0 && p[1] != 0) {
            cp = ((cp & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        }
        out.push_back(static_cast<char16_t>(cp));
    }
    return out;
}

inline std::string toUtf8(const std::u16string& s) {
    std::string out;
    for (char16_t c : s) {
        const uint32_t cp = c;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

inline std::u16string fromGuestW(const void* p) {
    std::u16string out;
    if (p == nullptr) return out;
    const auto* w = static_cast<const char16_t*>(p);
    while (*w != 0 && out.size() < 32768) out.push_back(*w++);
    return out;
}

struct Value {
    std::u16string name;  // nombre original (con mayúsculas)
    uint32_t type = kRegNone;
    std::vector<uint8_t> data;
};

struct Key {
    std::u16string path;                       // ruta original "HKCU\Software\..."
    std::map<std::u16string, Value> values;    // clave: nombre en minúsculas
};

class Registry {
public:
    Registry() { seedMachine(); }

    // Archivo de persistencia (HKCU/HKU). Carga su contenido si existe.
    void setBackingFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_ = path;
        loadLocked();
    }

    // RegOpenKeyEx / RegCreateKeyEx. subKey vacío reabre la propia clave.
    uint32_t open(uint64_t hKey, const std::u16string& subKey, bool create, uint64_t* outHandle,
                  uint32_t* disposition = nullptr) {
        if (outHandle == nullptr) return kInvalidParameter;
        std::lock_guard<std::mutex> lock(mutex_);
        std::u16string base;
        if (!resolveLocked(hKey, base)) return kInvalidHandle;
        std::u16string path = base;
        std::u16string sub = trimSlashes(subKey);
        if (!sub.empty()) path += u"\\" + sub;
        const std::u16string lower = lowerAscii(path);
        auto it = keys_.find(lower);
        if (it == keys_.end()) {
            if (!create) return kFileNotFound;
            // Crea también los ancestros, como Windows.
            size_t pos = 0;
            while ((pos = path.find(u'\\', pos + 1)) != std::u16string::npos) {
                const std::u16string parent = path.substr(0, pos);
                keys_.try_emplace(lowerAscii(parent), Key{parent, {}});
            }
            keys_.emplace(lower, Key{path, {}});
            if (disposition != nullptr) *disposition = kCreatedNewKey;
            if (isPersistent(lower)) saveLocked();
        } else if (disposition != nullptr) {
            *disposition = kOpenedExistingKey;
        }
        const uint64_t h = kHandleBase + nextHandle_++;
        handles_[h] = lower;
        *outHandle = h;
        return kSuccess;
    }

    uint32_t close(uint64_t hKey) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (isPredefined(hKey)) return kSuccess;
        return handles_.erase(hKey) != 0 ? kSuccess : kInvalidHandle;
    }

    uint32_t setValue(uint64_t hKey, const std::u16string& name, uint32_t type, const uint8_t* data,
                      size_t size) {
        if (data == nullptr && size != 0) return kInvalidParameter;
        std::lock_guard<std::mutex> lock(mutex_);
        Key* key = keyLocked(hKey);
        if (key == nullptr) return kInvalidHandle;
        Value& v = key->values[lowerAscii(name)];
        v.name = name;
        v.type = type;
        v.data.assign(data, data + size);
        if (isPersistent(lowerAscii(key->path))) saveLocked();
        return kSuccess;
    }

    // Semántica de RegQueryValueEx: data NULL -> solo tamaño; búfer corto -> kMoreData + tamaño.
    uint32_t queryValue(uint64_t hKey, const std::u16string& name, uint32_t* type, uint8_t* data,
                        uint32_t* size) {
        std::lock_guard<std::mutex> lock(mutex_);
        Key* key = keyLocked(hKey);
        if (key == nullptr) return kInvalidHandle;
        auto it = key->values.find(lowerAscii(name));
        if (it == key->values.end()) return kFileNotFound;
        return copyOut(it->second, type, data, size);
    }

    uint32_t deleteValue(uint64_t hKey, const std::u16string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        Key* key = keyLocked(hKey);
        if (key == nullptr) return kInvalidHandle;
        if (key->values.erase(lowerAscii(name)) == 0) return kFileNotFound;
        if (isPersistent(lowerAscii(key->path))) saveLocked();
        return kSuccess;
    }

    // RegEnumValue: name recibe el nombre; nameCapacity en caracteres incluido el NUL.
    uint32_t enumValue(uint64_t hKey, uint32_t index, char16_t* name, uint32_t* nameChars,
                       uint32_t* type, uint8_t* data, uint32_t* size) {
        std::lock_guard<std::mutex> lock(mutex_);
        Key* key = keyLocked(hKey);
        if (key == nullptr) return kInvalidHandle;
        if (index >= key->values.size()) return kNoMoreItems;
        auto it = key->values.begin();
        std::advance(it, index);
        const Value& v = it->second;
        if (nameChars != nullptr) {
            if (name != nullptr) {
                if (*nameChars <= v.name.size()) return kMoreData;
                std::memcpy(name, v.name.data(), v.name.size() * sizeof(char16_t));
                name[v.name.size()] = 0;
            }
            *nameChars = static_cast<uint32_t>(v.name.size());
        }
        return copyOut(v, type, data, size);
    }

    // RegEnumKeyEx: hijos directos en orden alfabético.
    uint32_t enumKey(uint64_t hKey, uint32_t index, char16_t* name, uint32_t* nameChars) {
        std::lock_guard<std::mutex> lock(mutex_);
        Key* key = keyLocked(hKey);
        if (key == nullptr) return kInvalidHandle;
        const std::vector<std::u16string> kids = childrenLocked(*key);
        if (index >= kids.size()) return kNoMoreItems;
        const std::u16string& n = kids[index];
        if (nameChars == nullptr) return kInvalidParameter;
        if (name != nullptr) {
            if (*nameChars <= n.size()) return kMoreData;
            std::memcpy(name, n.data(), n.size() * sizeof(char16_t));
            name[n.size()] = 0;
        }
        *nameChars = static_cast<uint32_t>(n.size());
        return kSuccess;
    }

    uint32_t queryInfo(uint64_t hKey, uint32_t* subKeys, uint32_t* maxSubKeyLen, uint32_t* values,
                       uint32_t* maxValueNameLen, uint32_t* maxValueLen) {
        std::lock_guard<std::mutex> lock(mutex_);
        Key* key = keyLocked(hKey);
        if (key == nullptr) return kInvalidHandle;
        const std::vector<std::u16string> kids = childrenLocked(*key);
        uint32_t maxKid = 0, maxName = 0, maxData = 0;
        for (const auto& k : kids) maxKid = std::max<uint32_t>(maxKid, static_cast<uint32_t>(k.size()));
        for (const auto& [_, v] : key->values) {
            maxName = std::max<uint32_t>(maxName, static_cast<uint32_t>(v.name.size()));
            maxData = std::max<uint32_t>(maxData, static_cast<uint32_t>(v.data.size()));
        }
        if (subKeys != nullptr) *subKeys = static_cast<uint32_t>(kids.size());
        if (maxSubKeyLen != nullptr) *maxSubKeyLen = maxKid;
        if (values != nullptr) *values = static_cast<uint32_t>(key->values.size());
        if (maxValueNameLen != nullptr) *maxValueNameLen = maxName;
        if (maxValueLen != nullptr) *maxValueLen = maxData;
        return kSuccess;
    }

    // RegDeleteKey: solo claves sin subclaves, como Windows.
    uint32_t deleteKey(uint64_t hKey, const std::u16string& subKey) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::u16string base;
        if (!resolveLocked(hKey, base)) return kInvalidHandle;
        const std::u16string sub = trimSlashes(subKey);
        if (sub.empty()) return kInvalidParameter;
        const std::u16string lower = lowerAscii(base + u"\\" + sub);
        auto it = keys_.find(lower);
        if (it == keys_.end()) return kFileNotFound;
        if (!childrenLocked(it->second).empty()) return 5;  // ERROR_ACCESS_DENIED
        keys_.erase(it);
        if (isPersistent(lower)) saveLocked();
        return kSuccess;
    }

private:
    static bool isPredefined(uint64_t h) {
        const uint32_t low = static_cast<uint32_t>(h);
        const uint32_t high = static_cast<uint32_t>(h >> 32);
        return (high == 0 || high == 0xFFFFFFFFu) && low >= 0x80000000u && low <= 0x80000006u;
    }

    static std::u16string trimSlashes(std::u16string s) {
        while (!s.empty() && s.front() == u'\\') s.erase(s.begin());
        while (!s.empty() && s.back() == u'\\') s.pop_back();
        return s;
    }

    // Raíz o clave abierta -> ruta en minúsculas.
    bool resolveLocked(uint64_t hKey, std::u16string& out) const {
        if (isPredefined(hKey)) {
            switch (static_cast<uint32_t>(hKey)) {
                case 0x80000000u: out = u"hkcr"; return true;
                case 0x80000001u: out = u"hkcu"; return true;
                case 0x80000002u: out = u"hklm"; return true;
                case 0x80000003u: out = u"hku"; return true;
                case 0x80000005u: out = u"hkcc"; return true;
                default: return false;
            }
        }
        auto it = handles_.find(hKey);
        if (it == handles_.end()) return false;
        out = it->second;
        return true;
    }

    Key* keyLocked(uint64_t hKey) {
        std::u16string lower;
        if (!resolveLocked(hKey, lower)) return nullptr;
        auto it = keys_.find(lower);
        if (it == keys_.end()) {
            // Las raíces existen siempre aunque estén vacías.
            if (lower.find(u'\\') != std::u16string::npos) return nullptr;
            it = keys_.emplace(lower, Key{lower, {}}).first;
        }
        return &it->second;
    }

    std::vector<std::u16string> childrenLocked(const Key& key) const {
        std::vector<std::u16string> out;
        const std::u16string prefix = lowerAscii(key.path) + u"\\";
        for (const auto& [lower, k] : keys_) {
            if (lower.size() <= prefix.size() || lower.compare(0, prefix.size(), prefix) != 0) continue;
            if (lower.find(u'\\', prefix.size()) != std::u16string::npos) continue;
            out.push_back(k.path.substr(prefix.size()));
        }
        return out;
    }

    static uint32_t copyOut(const Value& v, uint32_t* type, uint8_t* data, uint32_t* size) {
        if (type != nullptr) *type = v.type;
        const auto need = static_cast<uint32_t>(v.data.size());
        if (data != nullptr) {
            if (size == nullptr) return kInvalidParameter;
            if (*size < need) {
                *size = need;
                return kMoreData;
            }
            if (need != 0) std::memcpy(data, v.data.data(), need);
        }
        if (size != nullptr) *size = need;
        return kSuccess;
    }

    static bool isPersistent(const std::u16string& lowerPath) {
        return lowerPath.compare(0, 4, u"hkcu") == 0 || lowerPath.compare(0, 3, u"hku") == 0;
    }

    void addMachine(const std::u16string& path, const std::u16string& name, uint32_t type,
                    const std::vector<uint8_t>& data) {
        size_t pos = 0;
        while ((pos = path.find(u'\\', pos + 1)) != std::u16string::npos) {
            const std::u16string parent = path.substr(0, pos);
            keys_.try_emplace(lowerAscii(parent), Key{parent, {}});
        }
        Key& k = keys_.try_emplace(lowerAscii(path), Key{path, {}}).first->second;
        k.values[lowerAscii(name)] = Value{name, type, data};
    }

    static std::vector<uint8_t> sz(const std::u16string& s) {
        std::vector<uint8_t> d((s.size() + 1) * sizeof(char16_t), 0);
        std::memcpy(d.data(), s.data(), s.size() * sizeof(char16_t));
        return d;
    }

    static std::vector<uint8_t> dword(uint32_t v) {
        std::vector<uint8_t> d(4);
        std::memcpy(d.data(), &v, 4);
        return d;
    }

    void seedMachine() {
        const std::u16string cpu = u"hklm\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
        addMachine(cpu, u"ProcessorNameString", kRegSz, sz(u"Qualcomm Oryon CPU"));
        addMachine(cpu, u"VendorIdentifier", kRegSz, sz(u"GenuineIntel"));
        addMachine(cpu, u"~MHz", kRegDword, dword(4320));
        const std::u16string nt = u"hklm\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
        addMachine(nt, u"ProductName", kRegSz, sz(u"Windows 10 Pro"));
        addMachine(nt, u"CurrentBuild", kRegSz, sz(u"19045"));
        addMachine(nt, u"CurrentBuildNumber", kRegSz, sz(u"19045"));
        addMachine(nt, u"CurrentVersion", kRegSz, sz(u"6.3"));
        addMachine(nt, u"CurrentMajorVersionNumber", kRegDword, dword(10));
        addMachine(nt, u"CurrentMinorVersionNumber", kRegDword, dword(0));
        addMachine(nt, u"ReleaseId", kRegSz, sz(u"2009"));
    }

    // Formato: "HPREG1\0" + por clave persistente: u32 len + ruta UTF-16, u32 nValores,
    // y por valor: u32 len + nombre, u32 tipo, u32 len + datos.
    void saveLocked() {
        if (file_.empty()) return;
        const std::string tmp = file_ + ".tmp";
        FILE* f = std::fopen(tmp.c_str(), "wb");
        if (f == nullptr) return;
        auto put32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto putStr = [&](const std::u16string& s) {
            put32(static_cast<uint32_t>(s.size()));
            if (!s.empty()) std::fwrite(s.data(), sizeof(char16_t), s.size(), f);
        };
        std::fwrite("HPREG1", 1, 7, f);
        for (const auto& [lower, k] : keys_) {
            if (!isPersistent(lower)) continue;
            putStr(k.path);
            put32(static_cast<uint32_t>(k.values.size()));
            for (const auto& [_, v] : k.values) {
                putStr(v.name);
                put32(v.type);
                put32(static_cast<uint32_t>(v.data.size()));
                if (!v.data.empty()) std::fwrite(v.data.data(), 1, v.data.size(), f);
            }
        }
        const bool ok = std::fflush(f) == 0;
        std::fclose(f);
        if (ok) std::rename(tmp.c_str(), file_.c_str());
    }

    void loadLocked() {
        FILE* f = std::fopen(file_.c_str(), "rb");
        if (f == nullptr) return;
        char magic[7] = {};
        if (std::fread(magic, 1, 7, f) != 7 || std::memcmp(magic, "HPREG1", 7) != 0) {
            std::fclose(f);
            return;
        }
        constexpr uint32_t kMaxLen = 1u << 20;
        auto get32 = [f](uint32_t& v) { return std::fread(&v, 4, 1, f) == 1; };
        auto getStr = [&](std::u16string& s) {
            uint32_t n = 0;
            if (!get32(n) || n > kMaxLen) return false;
            s.resize(n);
            return n == 0 || std::fread(s.data(), sizeof(char16_t), n, f) == n;
        };
        std::u16string path;
        while (getStr(path)) {
            uint32_t count = 0;
            if (!get32(count) || count > kMaxLen) break;
            Key k{path, {}};
            bool ok = true;
            for (uint32_t i = 0; i < count && ok; ++i) {
                Value v;
                uint32_t len = 0;
                ok = getStr(v.name) && get32(v.type) && get32(len) && len <= kMaxLen * 16;
                if (!ok) break;
                v.data.resize(len);
                ok = len == 0 || std::fread(v.data.data(), 1, len, f) == len;
                if (ok) k.values[lowerAscii(v.name)] = std::move(v);
            }
            if (!ok) break;
            keys_[lowerAscii(path)] = std::move(k);
        }
        std::fclose(f);
    }

    std::mutex mutex_;
    std::map<std::u16string, Key> keys_;
    std::unordered_map<uint64_t, std::u16string> handles_;
    uint64_t nextHandle_ = 1;
    std::string file_;
};

}  // namespace win32reg
