#pragma once

#include <bblite/pal_audio.hpp>
#include <bblite/js_promise.hpp>
#include <bblite/pal_native_job.hpp>

namespace bbl::pal {

/**
 * `decodeAudioData`: the encoded bytes are copied on the realm, leaving the
 * ArrayBuffer attached, decoded in a native job, and the buffer is made on
 * the realm when the job settles.
 */
inline js::Promise<AudioBufferHandle> audio_decode_async(AudioContextHandle context,
                                                         js::ArrayBuffer encoded) {
    try {
        std::vector<std::uint8_t> bytes(encoded.data(), encoded.data() + encoded.byte_length());
        return run_native_job<AudioBufferHandle>(
            [bytes = std::move(bytes), rate = audio_sample_rate(context)] {
                return audio_decode_samples(bytes, rate);
            },
            [context](std::shared_ptr<const DecodedAudio> decoded) {
                const auto buffer = audio_buffer_from_decoded(context, decoded);
                if (buffer.value == 0)
                    throw std::runtime_error("Audio data could not be decoded.");
                return buffer;
            });
    } catch (const WorkerTerminated&) {
        throw;
    } catch (...) {
        return js::Promise<AudioBufferHandle>::rejected(std::current_exception());
    }
}

/**
 * `createAudioEngineAsync`'s context: the realm's step (`audio_begin_context`)
 * runs first, the playback device opens in a native job, and the context is
 * created on the realm, joining `session`, when it settles. A device that
 * cannot open rejects the promise.
 */
inline js::Promise<AudioContextHandle>
audio_create_context_async(std::shared_ptr<AudioSession>& session) {
    const bool capture = audio_begin_context();
    if (!session)
        session = std::make_shared<AudioSession>();
    return run_native_job<AudioContextHandle>(
        [capture] { return audio_open_device(capture); },
        [owner = session](std::shared_ptr<AudioPlaybackDevice> device) mutable {
            return audio_create_context(owner, std::move(device));
        });
}

enum class AudioContextAction { Resume, Suspend, Close };

/** Device transitions finish before a task settles the realm-owned promise. */
inline js::Promise<js::PromiseVoid> audio_context_transition(AudioContextHandle context,
                                                             AudioContextAction action) {
    js::Promise<js::PromiseVoid> promise;
    try {
        if (audio_state(context) == "closed")
            throw std::runtime_error("AudioContext is closed.");
        switch (action) {
        case AudioContextAction::Resume:
            audio_resume(context);
            break;
        case AudioContextAction::Suspend:
            audio_suspend(context);
            break;
        case AudioContextAction::Close:
            audio_close_context(context);
            break;
        }
        EventLoop::current().post([promise] { promise.resolve(js::PromiseVoid{}); });
    } catch (const WorkerTerminated&) {
        throw;
    } catch (...) {
        promise.reject(std::current_exception());
    }
    return promise;
}

} // namespace bbl::pal
