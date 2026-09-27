#include "pal_system_preferences.hpp"

#include <dbus/dbus.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
enum class Reply {
    Zero,
    One,
    Unknown,
    Missing,
    BooleanTrue,
    BooleanFalse,
    WrongType,
    WrongSignature,
    Failure,
    Timeout
};
std::atomic<Reply> standard = Reply::Zero, legacy = Reply::BooleanTrue;
std::atomic<int> legacy_calls = 0;

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void respond(DBusConnection* connection, DBusMessage* request, Reply mode) {
    if (mode == Reply::Timeout)
        return;
    using Message = std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)>;
    Message reply(nullptr, dbus_message_unref);
    if (mode == Reply::Missing || mode == Reply::Failure) {
        reply.reset(dbus_message_new_error(request,
                                           mode == Reply::Missing
                                               ? "org.freedesktop.portal.Error.NotFound"
                                               : "org.freedesktop.portal.Error.Failed",
                                           "Fixture response"));
    } else {
        reply.reset(dbus_message_new_method_return(request));
        DBusMessageIter root{}, variant{};
        dbus_message_iter_init_append(reply.get(), &root);
        const bool boolean = mode == Reply::BooleanTrue || mode == Reply::BooleanFalse;
        const char* signature = mode == Reply::WrongType ? "s" : boolean ? "b" : "u";
        auto* destination = &root;
        if (mode != Reply::WrongSignature) {
            require(dbus_message_iter_open_container(&root, DBUS_TYPE_VARIANT, signature, &variant),
                    "Open variant");
            destination = &variant;
        }
        const char* text = "bad";
        dbus_uint32_t number = mode == Reply::One ? 1 : mode == Reply::Unknown ? 99 : 0;
        dbus_bool_t enabled = mode == Reply::BooleanTrue;
        require(dbus_message_iter_append_basic(destination, signature[0],
                                               mode == Reply::WrongType ? static_cast<void*>(&text)
                                               : boolean ? static_cast<void*>(&enabled)
                                                         : static_cast<void*>(&number)),
                "Append fixture reply");
        if (mode != Reply::WrongSignature)
            require(dbus_message_iter_close_container(&root, &variant), "Close variant");
    }
    require(reply && dbus_connection_send(connection, reply.get(), nullptr), "Send fixture reply");
    dbus_connection_flush(connection);
}

void refuses(Reply mode, const char* expected) {
    standard = mode;
    try {
        (void)bbl::pal::linux_reduced_motion();
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()).find(expected) != std::string_view::npos,
                "Unexpected refusal");
        return;
    }
    throw std::runtime_error("Invalid preference was accepted");
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--live") {
        std::cout << "live reduced-motion=" << bbl::pal::system_reduced_motion() << '\n';
        return 0;
    }
    require(dbus_threads_init_default(), "D-Bus threading");
    DBusError error = DBUS_ERROR_INIT;
    auto* service = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    require(service, "Private test session bus");
    dbus_connection_set_exit_on_disconnect(service, false);
    require(dbus_bus_request_name(service, "org.freedesktop.portal.Desktop",
                                  DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                  &error) == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER,
            "Private test portal name");
    std::atomic<bool> finished = false;
    std::thread server([&] {
        while (!finished) {
            dbus_connection_read_write(service, 20);
            std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)> request(
                dbus_connection_pop_message(service), dbus_message_unref);
            if (!request || !dbus_message_is_method_call(
                                request.get(), "org.freedesktop.portal.Settings", "ReadOne"))
                continue;
            const char* name_space = nullptr;
            const char* key = nullptr;
            require(dbus_message_get_args(request.get(), nullptr, DBUS_TYPE_STRING, &name_space,
                                          DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID),
                    "ReadOne arguments");
            const bool old = std::strcmp(name_space, "org.gnome.desktop.interface") == 0;
            require(std::strcmp(key, old ? "enable-animations" : "reduced-motion") == 0,
                    "ReadOne key");
            if (old)
                ++legacy_calls;
            respond(service, request.get(), old ? legacy.load() : standard.load());
        }
    });
    int result = 0;
    try {
        require(!bbl::pal::linux_reduced_motion(), "Standard zero");
        standard = Reply::One;
        require(bbl::pal::linux_reduced_motion(), "Standard one");
        standard = Reply::Unknown;
        require(!bbl::pal::linux_reduced_motion() && legacy_calls == 0,
                "Standard unknown contract");
        standard = Reply::Missing;
        require(!bbl::pal::linux_reduced_motion(), "GNOME animations enabled");
        legacy = Reply::BooleanFalse;
        require(bbl::pal::linux_reduced_motion(), "GNOME animations disabled");
        legacy = Reply::Missing;
        refuses(Reply::Missing, "neither");
        legacy = Reply::WrongType;
        refuses(Reply::Missing, "boolean");
        const int old_calls = legacy_calls;
        refuses(Reply::WrongType, "uint32");
        refuses(Reply::WrongSignature, "variant");
        refuses(Reply::Failure, "Fixture response");
        const auto began = std::chrono::steady_clock::now();
        refuses(Reply::Timeout, "Could not read");
        require(std::chrono::steady_clock::now() - began < std::chrono::seconds(2),
                "Bounded portal timeout");
        require(legacy_calls == old_calls, "Only missing standard keys may use the legacy setting");
        standard = Reply::Zero;
        require(!bbl::pal::system_reduced_motion(), "Initial cache value");
        standard = Reply::One;
        require(!bbl::pal::system_reduced_motion(), "Cache remains valid for one second");
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        require(bbl::pal::system_reduced_motion(), "Changed preference refreshes");
        std::cout << "portal values, strict errors, timeout and cache refresh passed\n";
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        result = 1;
    }
    finished = true;
    server.join();
    dbus_connection_close(service);
    dbus_connection_unref(service);
    dbus_error_free(&error);
    return result;
}
