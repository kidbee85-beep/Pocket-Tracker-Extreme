// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/curated_radio.h"
#include "../linux/source-live-mp3.h"

#include <jni.h>
#include <array>
#include <iterator>
#include <new>

namespace {
using Decoder = pt::platform::source_live::Mp3Decoder;
struct PendingJavaException {};

void fail(JNIEnv* env, const char* text) {
    if (env->ExceptionCheck()) return;
    if (const auto cls = env->FindClass("java/lang/IllegalStateException"))
        env->ThrowNew(cls, text);
}
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_conanizer_pockettracker_AndroidRadioNative_catalog(JNIEnv* env, jobject) {
    const auto& stations = pt::sources::radio_presets;
    const auto strings = env->FindClass("java/lang/String");
    if (!strings) return nullptr;
    auto result = env->NewObjectArray(static_cast<jsize>(std::size(stations) * 7), strings, nullptr);
    if (!result) return nullptr;
    jsize index = 0;
    for (const auto& station : stations) {
        const char* fields[] = {station.id, station.title, station.creator, station.genres,
                                station.page, station.url, station.playlist ? "1" : "0"};
        for (const char* field : fields) {
            auto value = env->NewStringUTF(field);
            if (!value) return nullptr;
            env->SetObjectArrayElement(result, index++, value);
            env->DeleteLocalRef(value);
            if (env->ExceptionCheck()) return nullptr;
        }
    }
    return result;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_conanizer_pockettracker_AndroidRadioNative_create(JNIEnv* env, jobject) {
    auto* decoder = new (std::nothrow) Decoder();
    if (!decoder) fail(env, "Cannot allocate the live MP3 decoder");
    return reinterpret_cast<jlong>(decoder);
}

extern "C" JNIEXPORT void JNICALL
Java_com_conanizer_pockettracker_AndroidRadioNative_feed(
        JNIEnv* env, jobject, jlong handle, jbyteArray input, jint count, jshortArray output, jobject target) {
    constexpr int inputCapacity = 8192;
    constexpr int outputCapacity = 1152 * 2;
    if (!handle || !input || !output || !target || count < -1 || count > inputCapacity ||
        count > env->GetArrayLength(input) || env->GetArrayLength(output) < outputCapacity) {
        fail(env, "Invalid live decoder buffer");
        return;
    }
    const auto targetClass = env->GetObjectClass(target);
    if (!targetClass) return;
    const auto pcm = env->GetMethodID(targetClass, "pcm", "([SIII)V");
    if (!pcm) return;
    try {
        std::array<jshort, outputCapacity> samples{};
        auto deliver = [&](const float* values, size_t frames, int rate, int channels) {
            const auto sampleCount = static_cast<jsize>(frames * channels);
            for (jsize i = 0; i < sampleCount; ++i)
                samples[i] = static_cast<jshort>(values[i] * 32768.f);
            env->SetShortArrayRegion(output, 0, sampleCount, samples.data());
            if (!env->ExceptionCheck())
                env->CallVoidMethod(target, pcm, output, static_cast<jint>(frames), rate, channels);
            if (env->ExceptionCheck()) throw PendingJavaException{};
        };
        auto& decoder = *reinterpret_cast<Decoder*>(handle);
        if (count == -1) {
            decoder.finish(deliver);
        } else if (count > 0) {
            std::array<jbyte, inputCapacity> compressed{};
            env->GetByteArrayRegion(input, 0, count, compressed.data());
            if (env->ExceptionCheck()) return;
            decoder.feed(reinterpret_cast<const unsigned char*>(compressed.data()), static_cast<size_t>(count), deliver);
        }
    } catch (const PendingJavaException&) {
        // Keep the Kotlin worker's exception pending so it can close the stream and its take.
    } catch (const std::exception& error) {
        fail(env, error.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_conanizer_pockettracker_AndroidRadioNative_destroy(JNIEnv*, jobject, jlong handle) {
    delete reinterpret_cast<Decoder*>(handle);
}
