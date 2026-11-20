#pragma once

// Semántica de rutas SHLWAPI (PathCanonicalize/Combine/Append/RemoveFileSpec/FindFileName/
// FindExtension/IsRelative) sobre cadenas huésped: char16_t para las variantes W (WCHAR de
// Windows es de 16 bits, nunca el wchar_t de 32 bits de Bionic) y char para las variantes A.
// Header-only para que la suite nativa valide el mismo código que usan los trampolines.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hyperplay::win32path {

inline constexpr size_t kMaxPath = 260;  // MAX_PATH, incluye el terminador

template <typename C>
inline bool isSep(C c) noexcept {
    return c == static_cast<C>('\\') || c == static_cast<C>('/');
}

template <typename C>
inline size_t length(const C* s) noexcept {
    size_t n = 0;
    while (s[n] != 0) ++n;
    return n;
}

template <typename C>
inline bool hasDrive(const std::basic_string<C>& s) noexcept {
    if (s.size() < 2 || s[1] != static_cast<C>(':')) return false;
    const C d = s[0];
    return (d >= static_cast<C>('A') && d <= static_cast<C>('Z')) ||
           (d >= static_cast<C>('a') && d <= static_cast<C>('z'));
}

template <typename C>
inline bool isRelative(const C* path) noexcept {
    if (path == nullptr || path[0] == 0) return true;
    if (isSep(path[0])) return false;
    return !(path[1] == static_cast<C>(':'));
}

// Normaliza separadores a '\' y resuelve "." y "..". Nunca sube por encima de la raíz.
template <typename C>
inline std::basic_string<C> canonicalize(const std::basic_string<C>& in) {
    const C bs = static_cast<C>('\\');
    std::basic_string<C> root;
    size_t pos = 0;
    if (hasDrive(in)) {
        root = in.substr(0, 2);
        pos = 2;
        if (pos < in.size() && isSep(in[pos])) {
            root.push_back(bs);
            ++pos;
        }
    } else if (in.size() >= 2 && isSep(in[0]) && isSep(in[1])) {
        root.assign(2, bs);  // UNC
        pos = 2;
    } else if (!in.empty() && isSep(in[0])) {
        root.assign(1, bs);
        pos = 1;
    }

    std::vector<std::basic_string<C>> parts;
    std::basic_string<C> seg;
    auto flush = [&]() {
        if (seg.empty() || (seg.size() == 1 && seg[0] == static_cast<C>('.'))) {
            // segmento vacío o "." -> se descarta
        } else if (seg.size() == 2 && seg[0] == static_cast<C>('.') && seg[1] == static_cast<C>('.')) {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(seg);
        }
        seg.clear();
    };
    for (; pos < in.size(); ++pos) {
        if (isSep(in[pos])) flush();
        else seg.push_back(in[pos]);
    }
    flush();

    std::basic_string<C> out = root;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out.push_back(bs);
        out += parts[i];
    }
    const bool trailingSep = !in.empty() && isSep(in.back());
    if (trailingSep && !parts.empty()) out.push_back(bs);
    if (out.empty()) out.push_back(bs);
    return out;
}

// Copia con terminador; falla si no cabe en cap (incluido el terminador).
template <typename C>
inline bool store(C* dst, size_t cap, const std::basic_string<C>& s) noexcept {
    if (dst == nullptr || s.size() + 1 > cap) return false;
    for (size_t i = 0; i < s.size(); ++i) dst[i] = s[i];
    dst[s.size()] = 0;
    return true;
}

template <typename C>
inline bool canonicalizeInto(C* dst, const C* src, size_t cap = kMaxPath) {
    if (dst == nullptr || src == nullptr) return false;
    return store(dst, cap, canonicalize(std::basic_string<C>(src, length(src))));
}

// PathCombine: une dir + file y canonicaliza. Un file absoluto sustituye a dir
// (con '\' inicial conserva la unidad de dir).
template <typename C>
inline bool combineInto(C* dst, const C* dir, const C* file, size_t cap = kMaxPath) {
    if (dst == nullptr || (dir == nullptr && file == nullptr)) return false;
    const std::basic_string<C> d = dir != nullptr ? std::basic_string<C>(dir, length(dir)) : std::basic_string<C>();
    const std::basic_string<C> f = file != nullptr ? std::basic_string<C>(file, length(file)) : std::basic_string<C>();
    std::basic_string<C> joined;
    if (f.empty()) {
        joined = d;
    } else if (hasDrive(f) || (f.size() >= 2 && isSep(f[0]) && isSep(f[1]))) {
        joined = f;
    } else if (isSep(f[0])) {
        joined = hasDrive(d) ? d.substr(0, 2) + f : f;
    } else if (d.empty()) {
        joined = f;
    } else {
        joined = d;
        if (!isSep(joined.back())) joined.push_back(static_cast<C>('\\'));
        joined += f;
    }
    return store(dst, cap, canonicalize(joined));
}

// PathAppend: añade more a path (in situ); los separadores iniciales de more se ignoran.
template <typename C>
inline bool appendInto(C* path, const C* more, size_t cap = kMaxPath) {
    if (path == nullptr || more == nullptr) return false;
    while (*more != 0 && isSep(*more)) ++more;
    std::basic_string<C> p(path, length(path));
    if (*more == 0) return store(path, cap, canonicalize(p.empty() ? std::basic_string<C>(1, static_cast<C>('\\')) : p));
    if (!p.empty() && !isSep(p.back())) p.push_back(static_cast<C>('\\'));
    p.append(more, length(more));
    return store(path, cap, canonicalize(p));
}

// PathFindFileName: puntero al último componente (o al inicio si no hay separadores).
template <typename C>
inline C* findFileName(C* path) noexcept {
    if (path == nullptr) return nullptr;
    C* name = path;
    for (C* p = path; *p != 0; ++p) {
        if ((isSep(*p) || *p == static_cast<C>(':')) && p[1] != 0 && !isSep(p[1])) name = p + 1;
    }
    return name;
}

// PathFindExtension: puntero al '.' de la extensión o al terminador si no hay.
template <typename C>
inline C* findExtension(C* path) noexcept {
    if (path == nullptr) return nullptr;
    C* dot = nullptr;
    C* p = path;
    for (; *p != 0; ++p) {
        if (isSep(*p) || *p == static_cast<C>(' ')) dot = nullptr;
        else if (*p == static_cast<C>('.')) dot = p;
    }
    return dot != nullptr ? dot : p;
}

// PathRemoveFileSpec: elimina el último componente; devuelve true si cambió algo.
template <typename C>
inline bool removeFileSpec(C* path) noexcept {
    if (path == nullptr) return false;
    const size_t len = length(path);
    if (len == 0) return false;
    size_t rootLen = 0;
    if (len >= 2 && path[1] == static_cast<C>(':')) rootLen = (len >= 3 && isSep(path[2])) ? 3 : 2;
    else if (isSep(path[0])) rootLen = (len >= 2 && isSep(path[1])) ? 2 : 1;
    size_t cut = len;
    size_t lastSep = SIZE_MAX;
    for (size_t i = rootLen; i < len; ++i) {
        if (isSep(path[i])) lastSep = i;
    }
    if (lastSep == SIZE_MAX) cut = rootLen;
    else cut = lastSep;
    if (cut < rootLen) cut = rootLen;
    if (cut == len) return false;
    path[cut] = 0;
    return true;
}

}  // namespace hyperplay::win32path
