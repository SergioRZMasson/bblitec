#include "pal_system_preferences.hpp"
#include <cassert>
#include <cstring>
#include <thread>

namespace {
int fault = 0;
bool pending = false;
bool preference = false;
int calls = 0;
int local_refs = 0;
_jobject activity;
_jclass activity_type;

jclass JNICALL get_class(JNIEnv*, jobject object) {
    assert(object == &activity);
    if (fault == 3) {
        pending = true;
        return nullptr;
    }
    ++local_refs;
    return &activity_type;
}
jmethodID JNICALL get_method(JNIEnv*, jclass type, const char* name, const char* signature) {
    assert(type == &activity_type);
    assert(std::strcmp(name, "prefersReducedMotion") == 0);
    assert(std::strcmp(signature, "()Z") == 0);
    if (fault == 4) {
        pending = true;
        return nullptr;
    }
    return reinterpret_cast<jmethodID>(&calls);
}
jboolean JNICALL call_boolean(JNIEnv*, jobject object, jmethodID method, va_list) {
    assert(object == &activity && method == reinterpret_cast<jmethodID>(&calls));
    ++calls;
    if (fault == 5)
        pending = true;
    return preference ? JNI_TRUE : JNI_FALSE;
}
jboolean JNICALL exception_check(JNIEnv*) { return pending ? JNI_TRUE : JNI_FALSE; }
void JNICALL exception_clear(JNIEnv*) { pending = false; }
void JNICALL delete_ref(JNIEnv*, jobject ref) {
    assert(ref == &activity || ref == &activity_type);
    --local_refs;
}
JNINativeInterface_ functions = [] {
    JNINativeInterface_ table{};
    table.GetObjectClass = get_class;
    table.GetMethodID = get_method;
    table.CallBooleanMethodV = call_boolean;
    table.ExceptionCheck = exception_check;
    table.ExceptionClear = exception_clear;
    table.DeleteLocalRef = delete_ref;
    return table;
}();
JNIEnv environment{&functions};
} // namespace

void* SDL_GetAndroidJNIEnv() { return fault == 1 ? nullptr : &environment; }
void* SDL_GetAndroidActivity() {
    if (fault == 2)
        return nullptr;
    ++local_refs;
    return &activity;
}

int main() {
    const auto refused = [] {
        bool threw = false;
        try {
            (void)bbl::pal::system_reduced_motion();
        } catch (const std::runtime_error&) {
            threw = true;
        }
        assert(threw && !pending && local_refs == 0);
    };
    for (fault = 1; fault <= 5; ++fault)
        refused();
    fault = 0;
    assert(!bbl::pal::system_reduced_motion());
    const int initial_calls = calls;
    preference = true;
    assert(!bbl::pal::system_reduced_motion() && calls == initial_calls);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(bbl::pal::system_reduced_motion() && calls == initial_calls + 1);
    assert(local_refs == 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    fault = 5;
    refused();
    fault = 0;
    preference = false;
    assert(!bbl::pal::system_reduced_motion() && local_refs == 0);
}
