#pragma once

#include <chrono>
#include <stdexcept>

#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <jni.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bbl::pal {

/** Shared by preference consumers; refreshes without changing the user's setting. */
inline bool system_reduced_motion() {
#if defined(_WIN32) || defined(__ANDROID__)
    using Clock = std::chrono::steady_clock;
    static thread_local Clock::time_point checked{};
    static thread_local bool initialized = false;
    static thread_local bool reduced = false;
    const auto now = Clock::now();
    if (!initialized || now - checked >= std::chrono::seconds(1)) {
#if defined(__ANDROID__)
        auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
        if (!env)
            throw std::runtime_error("Android motion preference JNI unavailable.");
        const auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
        const auto type = activity ? env->GetObjectClass(activity) : nullptr;
        const auto method = type ? env->GetMethodID(type, "prefersReducedMotion", "()Z") : nullptr;
        const bool next = method && env->CallBooleanMethod(activity, method);
        const bool exception = env->ExceptionCheck();
        if (exception)
            env->ExceptionClear();
        if (type)
            env->DeleteLocalRef(type);
        if (activity)
            env->DeleteLocalRef(activity);
        if (!method || exception)
            throw std::runtime_error("Could not read the Android animation preference.");
        reduced = next;
#else
        BOOL animations = TRUE;
        if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
            throw std::runtime_error("Could not read the platform animation preference.");
        reduced = animations == FALSE;
#endif
        checked = now;
        initialized = true;
    }
    return reduced;
#else
    throw std::runtime_error("Platform motion preferences are not implemented on this platform.");
#endif
}

} // namespace bbl::pal
