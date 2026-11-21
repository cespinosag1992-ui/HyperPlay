#include "GuestOpenGl.h"

#include "DirectVulkanBridge.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define LOG_TAG "HyperPlay.GL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay::guestgl {
namespace {

RegisterFn s_register = nullptr;
XmmFn s_xmm = nullptr;
std::atomic<uint64_t> s_presents{0};

float xmmF(int i) {
    float v = 0;
    if (const uint8_t* p = s_xmm != nullptr ? s_xmm(i) : nullptr) std::memcpy(&v, p, 4);
    return v;
}
double xmmD(int i) {
    double v = 0;
    if (const uint8_t* p = s_xmm != nullptr ? s_xmm(i) : nullptr) std::memcpy(&v, p, 8);
    return v;
}

// --- EGL ---------------------------------------------------------------------------------------
struct Context {
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface pbuffer = EGL_NO_SURFACE;
};

std::mutex s_mutex;
EGLDisplay s_dpy = EGL_NO_DISPLAY;
EGLConfig s_config = nullptr;
EGLSurface s_window = EGL_NO_SURFACE;
int s_windowOwner = 0;  // tid con la superficie de ventana actual
std::vector<Context> s_contexts;  // HGLRC = índice + 1 (desplazado)
int s_swapInterval = 1;
thread_local uintptr_t t_current = 0;
thread_local uintptr_t t_currentDc = 0;

constexpr uintptr_t kRcBase = 0x6C000000;

bool ensureDisplay() {
    if (s_dpy != EGL_NO_DISPLAY) return true;
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, nullptr, nullptr)) {
        LOGW("eglInitialize falló");
        return false;
    }
    const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                            EGL_RED_SIZE,        8,
                            EGL_GREEN_SIZE,      8,
                            EGL_BLUE_SIZE,       8,
                            EGL_ALPHA_SIZE,      8,
                            EGL_DEPTH_SIZE,      24,
                            EGL_STENCIL_SIZE,    8,
                            EGL_NONE};
    EGLint n = 0;
    if (!eglChooseConfig(d, attrs, &s_config, 1, &n) || n == 0) {
        LOGW("eglChooseConfig sin configuración RGBA8 D24S8");
        return false;
    }
    s_dpy = d;
    LOGI("EGL listo: %s / %s", eglQueryString(d, EGL_VENDOR), eglQueryString(d, EGL_VERSION));
    return true;
}

EGLSurface windowSurface() {
    if (s_window != EGL_NO_SURFACE) return s_window;
    ANativeWindow* win = DirectVulkanBridge::shared().releaseWindowForGuest();
    if (win == nullptr) return EGL_NO_SURFACE;
    s_window = eglCreateWindowSurface(s_dpy, s_config, win, nullptr);
    if (s_window == EGL_NO_SURFACE) {
        LOGW("eglCreateWindowSurface falló: 0x%x", eglGetError());
    } else {
        LOGI("Superficie GL de la ventana: %dx%d", ANativeWindow_getWidth(win), ANativeWindow_getHeight(win));
    }
    return s_window;
}

uintptr_t createContext(uintptr_t share, int major, int minor) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!ensureDisplay()) return 0;
    // Todos los contextos comparten objetos con el primero (wglShareLists / contextos de carga).
    EGLContext shareCtx = EGL_NO_CONTEXT;
    if (share >= kRcBase && share - kRcBase < s_contexts.size()) shareCtx = s_contexts[share - kRcBase].ctx;
    if (shareCtx == EGL_NO_CONTEXT) {
        for (const auto& c : s_contexts) {
            if (c.ctx != EGL_NO_CONTEXT) {
                shareCtx = c.ctx;
                break;
            }
        }
    }
    const EGLint attrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE};
    Context c;
    c.ctx = eglCreateContext(s_dpy, s_config, shareCtx, attrs);
    if (c.ctx == EGL_NO_CONTEXT) {
        LOGW("eglCreateContext falló: 0x%x", eglGetError());
        return 0;
    }
    const EGLint pb[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    c.pbuffer = eglCreatePbufferSurface(s_dpy, s_config, pb);
    s_contexts.push_back(c);
    LOGI("Contexto GL %zu creado (pedido %d.%d, GLES 3.2)", s_contexts.size(), major, minor);
    return kRcBase + s_contexts.size() - 1;
}

bool makeCurrent(uintptr_t dc, uintptr_t rc) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_dpy == EGL_NO_DISPLAY) return rc == 0;
    const int tid = gettid();
    if (rc == 0) {
        eglMakeCurrent(s_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_windowOwner == tid) s_windowOwner = 0;
        t_current = 0;
        t_currentDc = 0;
        return true;
    }
    if (rc < kRcBase || rc - kRcBase >= s_contexts.size()) return false;
    const Context& c = s_contexts[rc - kRcBase];
    // La ventana solo puede estar actual en un hilo; los demás (carga en segundo plano) usan
    // su pbuffer de 1x1.
    EGLSurface surf = c.pbuffer;
    if (s_windowOwner == 0 || s_windowOwner == tid) {
        if (EGLSurface w = windowSurface(); w != EGL_NO_SURFACE) {
            surf = w;
            s_windowOwner = tid;
        }
    }
    if (!eglMakeCurrent(s_dpy, surf, surf, c.ctx)) {
        LOGW("eglMakeCurrent falló: 0x%x", eglGetError());
        return false;
    }
    if (surf == s_window) eglSwapInterval(s_dpy, s_swapInterval);
    t_current = rc;
    t_currentDc = dc;
    return true;
}

// --- Funciones reales de GLES ------------------------------------------------------------------
void* s_gles = nullptr;
void* realGl(const char* name) {
    if (s_gles == nullptr) s_gles = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
    void* f = s_gles != nullptr ? dlsym(s_gles, name) : nullptr;
    if (f == nullptr) f = reinterpret_cast<void*>(eglGetProcAddress(name));
    return f;
}

// --- Envoltorios: float/double (XMM) -------------------------------------------------------------
void w_glClearColor() { glClearColor(xmmF(0), xmmF(1), xmmF(2), xmmF(3)); }
void w_glBlendColor() { glBlendColor(xmmF(0), xmmF(1), xmmF(2), xmmF(3)); }
void w_glClearDepth() { glClearDepthf(static_cast<float>(xmmD(0))); }
void w_glClearDepthf() { glClearDepthf(xmmF(0)); }
void w_glDepthRange() { glDepthRangef(static_cast<float>(xmmD(0)), static_cast<float>(xmmD(1))); }
void w_glDepthRangef() { glDepthRangef(xmmF(0), xmmF(1)); }
void w_glPolygonOffset() { glPolygonOffset(xmmF(0), xmmF(1)); }
void w_glTexParameterf(GLenum target, GLenum pname) { glTexParameterf(target, pname, xmmF(2)); }
void w_glUniform1f(GLint loc) { glUniform1f(loc, xmmF(1)); }
void w_glLineWidth() { glLineWidth(xmmF(0)); }
void w_glSampleCoverage(GLenum, GLboolean invert) { glSampleCoverage(xmmF(0), invert); }

// --- Envoltorios: diferencias escritorio / GLES ---------------------------------------------------
bool desktopOnlyCap(GLenum cap) {
    switch (cap) {
        case 0x0DE1:  // GL_TEXTURE_2D (fijo)
        case 0x8861:  // GL_POINT_SPRITE
        case 0x8642:  // GL_VERTEX_PROGRAM_POINT_SIZE
        case 0x809D:  // GL_MULTISAMPLE
        case 0x8DB9:  // GL_FRAMEBUFFER_SRGB
        case 0x0B20:  // GL_LINE_SMOOTH
        case 0x0B41:  // GL_POLYGON_SMOOTH
        case 0x2A02:  // GL_POLYGON_OFFSET_LINE
        case 0x884F:  // GL_TEXTURE_CUBE_MAP_SEAMLESS (siempre activo en ES3)
            return true;
        default:
            return false;
    }
}
void w_glEnable(GLenum cap) {
    if (!desktopOnlyCap(cap)) glEnable(cap);
}
void w_glDisable(GLenum cap) {
    if (!desktopOnlyCap(cap)) glDisable(cap);
}
void w_glPolygonMode(GLenum, GLenum) {}
void w_glDrawBuffer(GLenum mode) {
    GLint fb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
    GLenum b = mode;
    if (fb == 0) b = (mode == GL_NONE) ? GL_NONE : GL_BACK;
    else if (mode == GL_BACK || mode == 0x0404 /*FRONT*/ || mode == 0x0408 /*FRONT_AND_BACK*/) b = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &b);
}
void w_glReadBuffer(GLenum mode) {
    GLint fb = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &fb);
    if (fb == 0) mode = (mode == GL_NONE) ? GL_NONE : GL_BACK;
    else if (mode == GL_BACK || mode == 0x0404 || mode == 0x0408) mode = GL_COLOR_ATTACHMENT0;
    glReadBuffer(mode);
}
void* w_glMapBuffer(GLenum target, GLenum access) {
    GLint size = 0;
    glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    GLbitfield bits = 0;
    if (access == 0x88B8 /*READ_ONLY*/) bits = GL_MAP_READ_BIT;
    else if (access == 0x88B9 /*WRITE_ONLY*/) bits = GL_MAP_WRITE_BIT;
    else bits = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    return size > 0 ? glMapBufferRange(target, 0, size, bits) : nullptr;
}
void w_glGetTexImage(GLenum, GLint, GLenum, GLenum, void*) {
    static std::atomic<bool> once{false};
    if (!once.exchange(true)) LOGW("glGetTexImage no existe en GLES: se ignora");
}
void w_glGetQueryObjectiv(GLuint id, GLenum pname, GLint* params) {
    GLuint v = 0;
    glGetQueryObjectuiv(id, pname, &v);
    if (params != nullptr) *params = static_cast<GLint>(v);
}
void w_glBeginQuery(GLenum target, GLuint id) {
    glBeginQuery(target == 0x8914 /*SAMPLES_PASSED*/ ? GL_ANY_SAMPLES_PASSED : target, id);
}
void w_glEndQuery(GLenum target) { glEndQuery(target == 0x8914 ? GL_ANY_SAMPLES_PASSED : target); }
void w_glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const void* idx, GLsizei inst,
                                           GLuint) {
    glDrawElementsInstanced(mode, count, type, idx, inst);
}

// Formatos internos: GLES 3 exige formato con tamaño para float/half float.
GLint sizedInternal(GLint internal, GLenum format, GLenum type) {
    if (type == GL_FLOAT) {
        if (internal == GL_RGBA || internal == GL_RGBA32F) return GL_RGBA32F;
        if (internal == GL_RGB) return GL_RGB32F;
        if (internal == GL_RG) return GL_RG32F;
        if (internal == GL_RED || internal == 0x1903) return GL_R32F;
    } else if (type == GL_HALF_FLOAT || type == 0x8D61 /*HALF_FLOAT_OES*/) {
        if (internal == GL_RGBA) return GL_RGBA16F;
        if (internal == GL_RGB) return GL_RGB16F;
        if (internal == GL_RG) return GL_RG16F;
        if (internal == GL_RED) return GL_R16F;
    } else if (type == GL_UNSIGNED_BYTE && format == GL_RGBA && internal == 0x8058 /*RGBA8*/) {
        return GL_RGBA8;
    }
    (void)format;
    return internal;
}
void w_glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format,
                    GLenum type, const void* pixels) {
    if (type == 0x8D61) type = GL_HALF_FLOAT;
    glTexImage2D(target, level, sizedInternal(internal, format, type), w, h, border, format, type, pixels);
}
void w_glRenderbufferStorage(GLenum target, GLenum internal, GLsizei w, GLsizei h) {
    if (internal == GL_DEPTH_COMPONENT) internal = GL_DEPTH_COMPONENT16;
    glRenderbufferStorage(target, internal, w, h);
}

// GLSL de escritorio -> GLSL ES: sin #version de escritorio (los shaders de MonoGame ya están
// escritos para GL_ES); precisión por defecto si falta.
void w_glShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string src;
    for (GLsizei i = 0; i < count; ++i) {
        if (strings == nullptr || strings[i] == nullptr) continue;
        if (lengths != nullptr && lengths[i] >= 0) src.append(strings[i], static_cast<size_t>(lengths[i]));
        else src.append(strings[i]);
    }
    std::string out;
    size_t pos = 0;
    while (pos < src.size()) {
        size_t eol = src.find('\n', pos);
        if (eol == std::string::npos) eol = src.size();
        std::string line = src.substr(pos, eol - pos);
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 8, "#version") == 0 &&
            line.find(" es") == std::string::npos) {
            line = "// " + line;  // GLSL 1.10/1.20/1.30 de escritorio: GLSL ES 1.00 por defecto
        }
        out += line;
        out += '\n';
        pos = eol + 1;
    }
    GLint type = 0;
    glGetShaderiv(shader, GL_SHADER_TYPE, &type);
    if (type == GL_FRAGMENT_SHADER && out.find("precision ") == std::string::npos) {
        out = "precision mediump float;\n" + out;
    }
    const char* p = out.c_str();
    glShaderSource(shader, 1, &p, nullptr);
}
void w_glCompileShader(GLuint shader) {
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        LOGW("Shader %u no compila: %s", shader, log);
    }
}

std::string s_extensions;
const GLubyte* w_glGetString(GLenum name) {
    switch (name) {
        case GL_VENDOR:
            return reinterpret_cast<const GLubyte*>("HyperPlay");
        case GL_RENDERER: {
            static std::string r = std::string("HyperPlay GL sobre ") +
                                   reinterpret_cast<const char*>(glGetString(GL_RENDERER));
            return reinterpret_cast<const GLubyte*>(r.c_str());
        }
        case GL_VERSION:
            return reinterpret_cast<const GLubyte*>("3.0.0 HyperPlay (OpenGL ES 3.2)");
        case GL_SHADING_LANGUAGE_VERSION:
            return reinterpret_cast<const GLubyte*>("1.30 HyperPlay");
        case GL_EXTENSIONS: {
            if (s_extensions.empty()) {
                const char* es = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
                const std::string real = es != nullptr ? es : "";
                s_extensions =
                    "GL_ARB_framebuffer_object GL_EXT_framebuffer_object GL_EXT_framebuffer_blit "
                    "GL_EXT_framebuffer_multisample GL_ARB_texture_non_power_of_two GL_ARB_instanced_arrays "
                    "GL_ARB_draw_instanced GL_ARB_vertex_buffer_object GL_ARB_texture_float GL_ARB_half_float_pixel "
                    "GL_ARB_depth_texture GL_EXT_packed_depth_stencil GL_ARB_occlusion_query GL_EXT_blend_minmax ";
                if (real.find("GL_EXT_texture_filter_anisotropic") != std::string::npos)
                    s_extensions += "GL_EXT_texture_filter_anisotropic ";
                if (real.find("GL_EXT_texture_compression_s3tc") != std::string::npos)
                    s_extensions += "GL_EXT_texture_compression_s3tc ";
                if (real.find("GL_EXT_discard_framebuffer") != std::string::npos)
                    s_extensions += "GL_EXT_discard_framebuffer ";
            }
            return reinterpret_cast<const GLubyte*>(s_extensions.c_str());
        }
        default:
            return glGetString(name);
    }
}

// --- WGL ---------------------------------------------------------------------------------------------
void* w_wglGetProcAddress(const char* name);

uintptr_t w_wglCreateContext(uintptr_t) { return createContext(0, 2, 1); }
int32_t w_wglDeleteContext(uintptr_t rc) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (rc < kRcBase || rc - kRcBase >= s_contexts.size()) return 0;
    Context& c = s_contexts[rc - kRcBase];
    if (c.ctx != EGL_NO_CONTEXT) eglDestroyContext(s_dpy, c.ctx);
    if (c.pbuffer != EGL_NO_SURFACE) eglDestroySurface(s_dpy, c.pbuffer);
    c = Context{};
    return 1;
}
int32_t w_wglMakeCurrent(uintptr_t dc, uintptr_t rc) { return makeCurrent(dc, rc) ? 1 : 0; }
uintptr_t w_wglGetCurrentContext() { return t_current; }
uintptr_t w_wglGetCurrentDC() { return t_currentDc; }
int32_t w_wglShareLists(uintptr_t, uintptr_t) { return 1; }  // todos comparten con el primero
uintptr_t w_wglCreateContextAttribsARB(uintptr_t, uintptr_t share, const int32_t* attribs) {
    int major = 2, minor = 1;
    for (int i = 0; attribs != nullptr && attribs[i] != 0 && i < 64; i += 2) {
        if (attribs[i] == 0x2091) major = attribs[i + 1];       // WGL_CONTEXT_MAJOR_VERSION_ARB
        else if (attribs[i] == 0x2092) minor = attribs[i + 1];  // WGL_CONTEXT_MINOR_VERSION_ARB
    }
    return createContext(share, major, minor);
}
const char* kWglExtensions =
    "WGL_ARB_extensions_string WGL_EXT_extensions_string WGL_ARB_pixel_format WGL_ARB_create_context "
    "WGL_ARB_create_context_profile WGL_EXT_swap_control";
const char* w_wglGetExtensionsStringARB(uintptr_t) { return kWglExtensions; }
const char* w_wglGetExtensionsStringEXT() { return kWglExtensions; }
int32_t w_wglSwapIntervalEXT(int32_t interval) {
    s_swapInterval = interval;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_dpy != EGL_NO_DISPLAY && s_windowOwner == gettid()) eglSwapInterval(s_dpy, interval);
    return 1;
}
int32_t w_wglGetSwapIntervalEXT() { return s_swapInterval; }
int32_t w_wglChoosePixelFormatARB(uintptr_t, const int32_t*, const float*, uint32_t maxFormats, int32_t* formats,
                                  uint32_t* numFormats) {
    if (formats != nullptr && maxFormats > 0) formats[0] = 1;
    if (numFormats != nullptr) *numFormats = 1;
    return 1;
}
int32_t pixelAttrib(int32_t a) {
    switch (a) {
        case 0x2000: return 1;       // NUMBER_PIXEL_FORMATS
        case 0x2001:                 // DRAW_TO_WINDOW
        case 0x2010:                 // SUPPORT_OPENGL
        case 0x2011: return 1;       // DOUBLE_BUFFER
        case 0x2003: return 0x2027;  // ACCELERATION = FULL
        case 0x2013: return 0x202B;  // PIXEL_TYPE = RGBA
        case 0x2014: return 32;      // COLOR_BITS
        case 0x2015: case 0x2017: case 0x2019: case 0x201B: return 8;  // R G B A
        case 0x2016: return 16;      // RED_SHIFT
        case 0x2018: return 8;       // GREEN_SHIFT
        case 0x201C: return 24;      // ALPHA_SHIFT
        case 0x2022: return 24;      // DEPTH_BITS
        case 0x2023: return 8;       // STENCIL_BITS
        default: return 0;
    }
}
int32_t w_wglGetPixelFormatAttribivARB(uintptr_t, int32_t, int32_t, uint32_t n, const int32_t* attribs,
                                       int32_t* values) {
    for (uint32_t i = 0; i < n && attribs != nullptr && values != nullptr; ++i) values[i] = pixelAttrib(attribs[i]);
    return 1;
}
int32_t w_wglGetPixelFormatAttribfvARB(uintptr_t, int32_t, int32_t, uint32_t n, const int32_t* attribs, float* values) {
    for (uint32_t i = 0; i < n && attribs != nullptr && values != nullptr; ++i)
        values[i] = static_cast<float>(pixelAttrib(attribs[i]));
    return 1;
}

int32_t w_SwapBuffers(uintptr_t) {
    bool mine = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        mine = s_window != EGL_NO_SURFACE && s_windowOwner == gettid();
    }
    if (!mine) return 1;
    if (!eglSwapBuffers(s_dpy, s_window)) {
        static std::atomic<int> errs{0};
        if (errs.fetch_add(1) < 5) LOGW("eglSwapBuffers falló: 0x%x", eglGetError());
        return 0;
    }
    if (s_presents.fetch_add(1) == 0) LOGI("Primer fotograma OpenGL presentado");
    DirectVulkanBridge::shared().countGuestPresent();
    return 1;
}

// --- GDI ---------------------------------------------------------------------------------------------
int32_t w_ChoosePixelFormat(uintptr_t, const void*) { return 1; }
int32_t w_SetPixelFormat(uintptr_t, int32_t, const void*) { return 1; }
int32_t w_GetPixelFormat(uintptr_t) { return 1; }
int32_t w_DescribePixelFormat(uintptr_t, int32_t, uint32_t bytes, uint8_t* pfd) {
    if (pfd != nullptr && bytes >= 40) {
        std::memset(pfd, 0, 40);
        const uint16_t size = 40, version = 1;
        const uint32_t flags = 0x4 | 0x20 | 0x1;  // DRAW_TO_WINDOW | SUPPORT_OPENGL | DOUBLEBUFFER
        std::memcpy(pfd, &size, 2);
        std::memcpy(pfd + 2, &version, 2);
        std::memcpy(pfd + 4, &flags, 4);
        pfd[8] = 0;    // PFD_TYPE_RGBA
        pfd[9] = 32;   // cColorBits
        pfd[10] = 8;   pfd[11] = 16;  // rojo
        pfd[12] = 8;   pfd[13] = 8;   // verde
        pfd[14] = 8;   pfd[15] = 0;   // azul
        pfd[16] = 8;   pfd[17] = 24;  // alfa
        pfd[23] = 24;  // cDepthBits
        pfd[24] = 8;   // cStencilBits
    }
    return 1;  // un formato
}

std::mutex s_gdiMutex;
std::unordered_map<uintptr_t, void*> s_bitmaps;
std::atomic<uintptr_t> s_nextGdi{0xCAFE5000};

uintptr_t w_CreateDC() { return 0xCAFE0030; }
int32_t w_DeleteDC(uintptr_t) { return 1; }
uintptr_t w_CreateCompatibleDC(uintptr_t) { return 0xCAFE0031; }
uintptr_t newBitmap(size_t bytes, void** bits) {
    void* mem = std::calloc(1, bytes > 0 ? bytes : 4);
    if (bits != nullptr) *bits = mem;
    const uintptr_t h = s_nextGdi.fetch_add(4);
    std::lock_guard<std::mutex> lock(s_gdiMutex);
    s_bitmaps[h] = mem;
    return h;
}
uintptr_t w_CreateDIBSection(uintptr_t, const uint8_t* bmi, uint32_t, void** bits, uintptr_t, uint32_t) {
    int32_t w = 0, h = 0;
    uint16_t bpp = 32;
    if (bmi != nullptr) {
        std::memcpy(&w, bmi + 4, 4);
        std::memcpy(&h, bmi + 8, 4);
        std::memcpy(&bpp, bmi + 14, 2);
    }
    const size_t stride = ((static_cast<size_t>(std::abs(w)) * bpp + 31) / 32) * 4;
    return newBitmap(stride * static_cast<size_t>(std::abs(h)), bits);
}
uintptr_t w_CreateBitmap(int32_t w, int32_t h, uint32_t, uint32_t bpp, const void* src) {
    const size_t stride = ((static_cast<size_t>(std::abs(w)) * bpp + 15) / 16) * 2;
    void* bits = nullptr;
    const uintptr_t handle = newBitmap(stride * static_cast<size_t>(std::abs(h)), &bits);
    if (src != nullptr && bits != nullptr) std::memcpy(bits, src, stride * static_cast<size_t>(std::abs(h)));
    return handle;
}
int32_t w_DeleteObject(uintptr_t h) {
    std::lock_guard<std::mutex> lock(s_gdiMutex);
    auto it = s_bitmaps.find(h);
    if (it != s_bitmaps.end()) {
        std::free(it->second);
        s_bitmaps.erase(it);
    }
    return 1;
}
uintptr_t w_SelectObject(uintptr_t, uintptr_t) { return 0xCAFE0040; }
int32_t w_GetObjectW(uintptr_t, int32_t, void*) { return 0; }

uintptr_t w_CreateIconIndirect(const void*) { return s_nextGdi.fetch_add(4); }
uintptr_t w_CreateCursor(uintptr_t, int32_t, int32_t, int32_t, int32_t, const void*, const void*) {
    return s_nextGdi.fetch_add(4);
}
int32_t w_DestroyCursor(uintptr_t) { return 1; }

// --- Tabla de nombres ----------------------------------------------------------------------------
std::mutex s_tableMutex;
std::unordered_map<std::string, void*> s_cache;

void* expose(void* fn, const char* dll, const char* name) {
    if (fn != nullptr && s_register != nullptr) s_register(fn, (std::string(dll) + "!" + name).c_str());
    return fn;
}

void* wrapperFor(const std::string& n) {
    static const std::unordered_map<std::string, void*> kWrappers = {
        {"glClearColor", reinterpret_cast<void*>(&w_glClearColor)},
        {"glBlendColor", reinterpret_cast<void*>(&w_glBlendColor)},
        {"glClearDepth", reinterpret_cast<void*>(&w_glClearDepth)},
        {"glClearDepthf", reinterpret_cast<void*>(&w_glClearDepthf)},
        {"glDepthRange", reinterpret_cast<void*>(&w_glDepthRange)},
        {"glDepthRangef", reinterpret_cast<void*>(&w_glDepthRangef)},
        {"glPolygonOffset", reinterpret_cast<void*>(&w_glPolygonOffset)},
        {"glTexParameterf", reinterpret_cast<void*>(&w_glTexParameterf)},
        {"glUniform1f", reinterpret_cast<void*>(&w_glUniform1f)},
        {"glLineWidth", reinterpret_cast<void*>(&w_glLineWidth)},
        {"glSampleCoverage", reinterpret_cast<void*>(&w_glSampleCoverage)},
        {"glEnable", reinterpret_cast<void*>(&w_glEnable)},
        {"glDisable", reinterpret_cast<void*>(&w_glDisable)},
        {"glPolygonMode", reinterpret_cast<void*>(&w_glPolygonMode)},
        {"glDrawBuffer", reinterpret_cast<void*>(&w_glDrawBuffer)},
        {"glReadBuffer", reinterpret_cast<void*>(&w_glReadBuffer)},
        {"glMapBuffer", reinterpret_cast<void*>(&w_glMapBuffer)},
        {"glMapBufferARB", reinterpret_cast<void*>(&w_glMapBuffer)},
        {"glGetTexImage", reinterpret_cast<void*>(&w_glGetTexImage)},
        {"glGetQueryObjectiv", reinterpret_cast<void*>(&w_glGetQueryObjectiv)},
        {"glGetQueryObjectivARB", reinterpret_cast<void*>(&w_glGetQueryObjectiv)},
        {"glBeginQuery", reinterpret_cast<void*>(&w_glBeginQuery)},
        {"glBeginQueryARB", reinterpret_cast<void*>(&w_glBeginQuery)},
        {"glEndQuery", reinterpret_cast<void*>(&w_glEndQuery)},
        {"glEndQueryARB", reinterpret_cast<void*>(&w_glEndQuery)},
        {"glDrawElementsInstancedBaseInstance", reinterpret_cast<void*>(&w_glDrawElementsInstancedBaseInstance)},
        {"glTexImage2D", reinterpret_cast<void*>(&w_glTexImage2D)},
        {"glRenderbufferStorage", reinterpret_cast<void*>(&w_glRenderbufferStorage)},
        {"glRenderbufferStorageEXT", reinterpret_cast<void*>(&w_glRenderbufferStorage)},
        {"glShaderSource", reinterpret_cast<void*>(&w_glShaderSource)},
        {"glCompileShader", reinterpret_cast<void*>(&w_glCompileShader)},
        {"glGetString", reinterpret_cast<void*>(&w_glGetString)},
        {"wglGetProcAddress", reinterpret_cast<void*>(&w_wglGetProcAddress)},
        {"wglCreateContext", reinterpret_cast<void*>(&w_wglCreateContext)},
        {"wglDeleteContext", reinterpret_cast<void*>(&w_wglDeleteContext)},
        {"wglMakeCurrent", reinterpret_cast<void*>(&w_wglMakeCurrent)},
        {"wglGetCurrentContext", reinterpret_cast<void*>(&w_wglGetCurrentContext)},
        {"wglGetCurrentDC", reinterpret_cast<void*>(&w_wglGetCurrentDC)},
        {"wglShareLists", reinterpret_cast<void*>(&w_wglShareLists)},
        {"wglSwapBuffers", reinterpret_cast<void*>(&w_SwapBuffers)},
        {"wglCreateContextAttribsARB", reinterpret_cast<void*>(&w_wglCreateContextAttribsARB)},
        {"wglGetExtensionsStringARB", reinterpret_cast<void*>(&w_wglGetExtensionsStringARB)},
        {"wglGetExtensionsStringEXT", reinterpret_cast<void*>(&w_wglGetExtensionsStringEXT)},
        {"wglSwapIntervalEXT", reinterpret_cast<void*>(&w_wglSwapIntervalEXT)},
        {"wglGetSwapIntervalEXT", reinterpret_cast<void*>(&w_wglGetSwapIntervalEXT)},
        {"wglChoosePixelFormatARB", reinterpret_cast<void*>(&w_wglChoosePixelFormatARB)},
        {"wglGetPixelFormatAttribivARB", reinterpret_cast<void*>(&w_wglGetPixelFormatAttribivARB)},
        {"wglGetPixelFormatAttribfvARB", reinterpret_cast<void*>(&w_wglGetPixelFormatAttribfvARB)},
    };
    auto it = kWrappers.find(n);
    return it != kWrappers.end() ? it->second : nullptr;
}

// Nombres de escritorio con sufijo EXT/ARB -> función núcleo de GLES 3 (misma firma).
std::string coreName(const std::string& n) {
    for (const char* suffix : {"EXT", "ARB", "OES"}) {
        const size_t len = std::strlen(suffix);
        if (n.size() > len && n.compare(n.size() - len, len, suffix) == 0) return n.substr(0, n.size() - len);
    }
    return n;
}

void* w_wglGetProcAddress(const char* name) { return name != nullptr ? glProcAddr(name) : nullptr; }

}  // namespace

void init(RegisterFn reg, XmmFn xmm) {
    s_register = reg;
    s_xmm = xmm;
}

void* glProcAddr(const char* name) {
    if (name == nullptr) return nullptr;
    const std::string n = name;
    {
        std::lock_guard<std::mutex> lock(s_tableMutex);
        auto it = s_cache.find(n);
        if (it != s_cache.end()) return it->second;
    }
    void* fn = wrapperFor(n);
    if (fn == nullptr && n.compare(0, 2, "gl") == 0) {
        fn = realGl(n.c_str());
        if (fn == nullptr) {
            const std::string core = coreName(n);
            if (core != n) fn = wrapperFor(core) != nullptr ? wrapperFor(core) : realGl(core.c_str());
        }
        if (fn == nullptr && n == "glDiscardFramebufferEXT") fn = realGl("glInvalidateFramebuffer");
    }
    if (fn == nullptr) {
        static std::atomic<int> misses{0};
        if (misses.fetch_add(1) < 60) LOGW("opengl32: '%s' no disponible", name);
    }
    expose(fn, "opengl32.dll", name);
    std::lock_guard<std::mutex> lock(s_tableMutex);
    s_cache[n] = fn;
    return fn;
}

void* gdiProcAddr(const char* name) {
    if (name == nullptr) return nullptr;
    static const std::unordered_map<std::string, void*> kGdi = {
        {"ChoosePixelFormat", reinterpret_cast<void*>(&w_ChoosePixelFormat)},
        {"SetPixelFormat", reinterpret_cast<void*>(&w_SetPixelFormat)},
        {"GetPixelFormat", reinterpret_cast<void*>(&w_GetPixelFormat)},
        {"DescribePixelFormat", reinterpret_cast<void*>(&w_DescribePixelFormat)},
        {"SwapBuffers", reinterpret_cast<void*>(&w_SwapBuffers)},
        {"CreateDCW", reinterpret_cast<void*>(&w_CreateDC)},
        {"CreateDCA", reinterpret_cast<void*>(&w_CreateDC)},
        {"DeleteDC", reinterpret_cast<void*>(&w_DeleteDC)},
        {"CreateCompatibleDC", reinterpret_cast<void*>(&w_CreateCompatibleDC)},
        {"CreateDIBSection", reinterpret_cast<void*>(&w_CreateDIBSection)},
        {"CreateBitmap", reinterpret_cast<void*>(&w_CreateBitmap)},
        {"DeleteObject", reinterpret_cast<void*>(&w_DeleteObject)},
        {"SelectObject", reinterpret_cast<void*>(&w_SelectObject)},
        {"GetObjectW", reinterpret_cast<void*>(&w_GetObjectW)},
    };
    auto it = kGdi.find(name);
    return it != kGdi.end() ? expose(it->second, "gdi32.dll", name) : nullptr;
}

void* user32ProcAddr(const char* name) {
    if (name == nullptr) return nullptr;
    static const std::unordered_map<std::string, void*> kUser = {
        {"CreateIconIndirect", reinterpret_cast<void*>(&w_CreateIconIndirect)},
        {"CreateCursor", reinterpret_cast<void*>(&w_CreateCursor)},
        {"DestroyCursor", reinterpret_cast<void*>(&w_DestroyCursor)},
    };
    auto it = kUser.find(name);
    return it != kUser.end() ? expose(it->second, "user32.dll", name) : nullptr;
}

uint64_t presentCount() { return s_presents.load(std::memory_order_relaxed); }

}  // namespace hyperplay::guestgl
