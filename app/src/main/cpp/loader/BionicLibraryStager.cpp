#include "BionicLibraryStager.h"

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define LOG_TAG "HyperPlay.LibStager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace BionicLibraryStager {
namespace {

constexpr size_t kCopyBufferSize = 64 * 1024;

bool startsWith(const std::string& value, const char* prefix) noexcept {
    return value.rfind(prefix, 0) == 0;
}

bool isPrivatePath(const std::string& path) noexcept {
    return startsWith(path, "/data/user/") || startsWith(path, "/data/data/") ||
           startsWith(path, "/data/app/") || startsWith(path, "/data/local/tmp/");
}

std::string packageFromCmdline() {
    const int fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    char buf[256];
    const ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return {};
    buf[n] = '\0';
    std::string name(buf);
    const size_t colon = name.find(':');
    if (colon != std::string::npos) name.resize(colon);
    return name;
}

std::string deriveFromReference(const std::string& referencePath) {
    const size_t files = referencePath.find("/files/");
    if (files != std::string::npos) return referencePath.substr(0, files + 6) + "/libstage";
    const size_t codeCache = referencePath.find("/code_cache/");
    if (codeCache != std::string::npos) return referencePath.substr(0, codeCache) + "/code_cache/libstage";
    return {};
}

bool ensureDir(const std::string& dir) {
    if (dir.empty()) return false;
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    if (mkdir(dir.c_str(), 0700) != 0) {
        LOGW("No se pudo crear %s: errno=%d (%s)", dir.c_str(), errno, std::strerror(errno));
        return false;
    }
    if (chmod(dir.c_str(), 0700) != 0) {
        LOGW("chmod 0700 falló en %s: errno=%d (%s)", dir.c_str(), errno, std::strerror(errno));
    }
    return true;
}

uint64_t hashName(const std::string& path, int64_t size, int64_t mtimeSec, int64_t mtimeNsec) noexcept {
    uint64_t hash = 1469598103934665603ULL;  // FNV-1a
    for (const char c : path) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<uint64_t>(size);
    hash *= 1099511628211ULL;
    hash ^= static_cast<uint64_t>(mtimeSec);
    hash *= 1099511628211ULL;
    hash ^= static_cast<uint64_t>(mtimeNsec);
    return hash;
}

std::string sanitizeBaseName(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    if (base.empty()) return "staged.so";
    return base;
}

bool writeAll(int fd, const char* data, size_t length) noexcept {
    size_t written = 0;
    while (written < length) {
        const ssize_t n = write(fd, data + written, length - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

bool copyFile(const std::string& src, const std::string& dst, mode_t mode) {
    const int in = open(src.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) {
        LOGW("No se pudo abrir %s: errno=%d (%s)", src.c_str(), errno, std::strerror(errno));
        return false;
    }
    // Una copia previa en 0500 no se puede truncar con O_WRONLY: EACCES. Se elimina antes de
    // crear el nuevo fichero, que se escribe en 0600 y se sella a `mode` tras el fsync.
    if (unlink(dst.c_str()) != 0 && errno != ENOENT) {
        LOGW("No se pudo reemplazar %s: errno=%d (%s)", dst.c_str(), errno, std::strerror(errno));
        close(in);
        return false;
    }
    const int out = open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0) {
        LOGW("No se pudo crear %s: errno=%d (%s)", dst.c_str(), errno, std::strerror(errno));
        close(in);
        return false;
    }

    std::vector<char> buffer(kCopyBufferSize);
    bool ok = true;
    for (;;) {
        const ssize_t n = read(in, buffer.data(), buffer.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            LOGW("Lectura falló en %s: errno=%d (%s)", src.c_str(), errno, std::strerror(errno));
            ok = false;
            break;
        }
        if (n == 0) break;
        if (!writeAll(out, buffer.data(), static_cast<size_t>(n))) {
            LOGW("Escritura falló en %s: errno=%d (%s)", dst.c_str(), errno, std::strerror(errno));
            ok = false;
            break;
        }
    }

    if (ok && fchmod(out, mode) != 0) {
        LOGW("fchmod 0%o falló en %s: errno=%d (%s)", mode, dst.c_str(), errno, std::strerror(errno));
        ok = false;
    }
    if (ok && fsync(out) != 0) {
        LOGW("fsync falló en %s: errno=%d (%s)", dst.c_str(), errno, std::strerror(errno));
        ok = false;
    }
    if (close(out) != 0) ok = false;
    close(in);
    if (!ok) unlink(dst.c_str());
    return ok;
}

}  // namespace

std::string stagingDir(const std::string& referencePath) {
    const char* env = std::getenv("HYPERPLAY_STAGING_DIR");
    if (env != nullptr && env[0] != '\0') return env;

    const std::string fromReference = deriveFromReference(referencePath);
    if (!fromReference.empty()) return fromReference;

    const std::string pkg = packageFromCmdline();
    if (pkg.empty()) return {};

    // /data/data es un symlink a /data/user/<user>; se usa la ruta canónica.
    return "/data/user/0/" + pkg + "/files/libstage";
}

bool needsStaging(const std::string& path) noexcept {
    if (path.empty()) return false;
    if (isPrivatePath(path)) return false;
    // Los nombres sin '/' los resuelve el linker de Android desde sus rutas de búsqueda.
    if (path.find('/') == std::string::npos) return false;
    return true;
}

std::string stageLibrary(const std::string& srcPath, const std::string& referencePath) {
    if (srcPath.empty()) return {};
    if (!needsStaging(srcPath)) return srcPath;

    const std::string dir = stagingDir(referencePath.empty() ? srcPath : referencePath);
    if (dir.empty() || !ensureDir(dir)) {
        LOGW("Staging imposible para %s: sin directorio privado utilizable", srcPath.c_str());
        return {};
    }

    struct stat st;
    if (stat(srcPath.c_str(), &st) != 0) {
        LOGW("stat(%s) falló: errno=%d (%s)", srcPath.c_str(), errno, std::strerror(errno));
        return {};
    }
    if (!S_ISREG(st.st_mode)) {
        LOGW("Staging rechazado: %s no es un fichero regular", srcPath.c_str());
        return {};
    }

    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "-%016llx.so",
                  static_cast<unsigned long long>(hashName(srcPath, st.st_size, st.st_mtime, st.st_mtim.tv_nsec)));
    const std::string dest = dir + "/" + sanitizeBaseName(srcPath) + suffix;

    struct stat dstStat;
    if (stat(dest.c_str(), &dstStat) == 0 && dstStat.st_size == st.st_size) {
        if (chmod(dest.c_str(), 0500) != 0) {
            LOGW("chmod 0500 falló en %s: errno=%d (%s)", dest.c_str(), errno, std::strerror(errno));
        }
        return dest;
    }

    if (!copyFile(srcPath, dest, 0500)) return {};
    LOGI("Librería preparada en %s (%lld bytes)", dest.c_str(), static_cast<long long>(st.st_size));
    return dest;
}

void logDlError(const char* context, const std::string& path) {
    const char* err = dlerror();
    LOGW("%s: %s (%s)", context, path.c_str(), err != nullptr ? err : "dlerror vacío");
}

void* dlopenStaged(const std::string& path, int flags, std::string* stagedPath) {
    if (path.empty()) return nullptr;

    if (!needsStaging(path)) {
        dlerror();
        void* handle = dlopen(path.c_str(), flags);
        if (handle != nullptr) {
            if (stagedPath != nullptr) *stagedPath = path;
            return handle;
        }
        logDlError("dlopen directo falló", path);
        // Se intenta el staging como último recurso: algunas ROMs marcan como noexec rutas que
        // parecen privadas.
    }

    const std::string staged = stageLibrary(path, path);
    if (staged.empty()) {
        LOGW("dlopen abortado: no se pudo preparar %s", path.c_str());
        return nullptr;
    }

    dlerror();
    void* handle = dlopen(staged.c_str(), flags);
    if (handle == nullptr) {
        logDlError("dlopen (staged) falló", staged);
        return nullptr;
    }
    if (stagedPath != nullptr) *stagedPath = staged;
    return handle;
}

}  // namespace BionicLibraryStager
}  // namespace hyperplay
