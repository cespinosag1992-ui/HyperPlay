// JNI de TurnipDriverInspector para com.murdoc820.hyperplay.engine.TurnipNative.
#include <jni.h>

#include <string>

#include "gpu/TurnipDriverInspector.h"

using namespace hyperplay;

namespace {

std::string toStd(JNIEnv* env, jstring value) {
    if (value == nullptr) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (chars == nullptr) return {};
    std::string out(chars);
    env->ReleaseStringUTFChars(value, chars);
    return out;
}

// NewStringUTF exige UTF-8 modificado válido: los mensajes de dlerror() pueden
// traer bytes arbitrarios de rutas, así que lo que no sea ASCII se sustituye.
jstring toJava(JNIEnv* env, const std::string& s) {
    std::string ascii(s);
    for (char& c : ascii) {
        if (static_cast<unsigned char>(c) >= 0x80) c = '?';
    }
    return env->NewStringUTF(ascii.c_str());
}

}  // namespace

#define TURNIP_JNI(ret, name) \
    extern "C" JNIEXPORT ret JNICALL Java_com_murdoc820_hyperplay_engine_TurnipNative_##name

// [validElf, aarch64, bionic, hasIcdEntry, hasNegotiate] como 0/1.
TURNIP_JNI(jintArray, nativeInspectDriver)(JNIEnv* env, jclass, jstring path) {
    const DriverInspection info = inspectDriverElf(toStd(env, path));
    const jint values[] = {
        info.validElf ? 1 : 0, info.aarch64 ? 1 : 0, info.bionic ? 1 : 0,
        info.hasIcdEntry ? 1 : 0, info.hasNegotiate ? 1 : 0,
    };
    constexpr jsize count = sizeof(values) / sizeof(values[0]);
    jintArray result = env->NewIntArray(count);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, count, values);
    return result;
}

// null = dlopen + dlsym correctos; si no, el texto de dlerror().
TURNIP_JNI(jstring, nativeDlopenCheck)(JNIEnv* env, jclass, jstring path) {
    std::string error;
    if (dlopenCheck(toStd(env, path), error)) return nullptr;
    return toJava(env, error);
}

TURNIP_JNI(jstring, nativeAdrenoModel)(JNIEnv* env, jclass) { return toJava(env, adrenoModel()); }

TURNIP_JNI(jboolean, nativeKgslAccessible)(JNIEnv*, jclass) {
    return kgslAccessible() ? JNI_TRUE : JNI_FALSE;
}
