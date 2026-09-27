#include "pal_system_preferences.hpp"

#include <dbus/dbus.h>

#include <memory>
#include <optional>
#include <string>

namespace bbl::pal {
namespace {

struct BusError {
    DBusError value = DBUS_ERROR_INIT;
    ~BusError() { dbus_error_free(&value); }
};

struct ConnectionDeleter {
    void operator()(DBusConnection* connection) const {
        dbus_connection_close(connection);
        dbus_connection_unref(connection);
    }
};

using Connection = std::unique_ptr<DBusConnection, ConnectionDeleter>;
using Message = std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)>;

Connection connect_settings() {
    static const bool initialized = dbus_threads_init_default();
    if (!initialized)
        throw std::runtime_error("Could not initialize D-Bus threading for motion preferences.");
    BusError error;
    Connection connection(dbus_bus_get_private(DBUS_BUS_SESSION, &error.value));
    if (!connection)
        throw std::runtime_error(
            std::string("Could not connect to the motion preference session bus: ") +
            (error.value.message ? error.value.message : "unknown error"));
    dbus_connection_set_exit_on_disconnect(connection.get(), false);
    return connection;
}

std::optional<bool> read_motion_setting(DBusConnection* connection, const char* name_space,
                                        const char* key, bool standard) {
    Message request(dbus_message_new_method_call("org.freedesktop.portal.Desktop",
                                                 "/org/freedesktop/portal/desktop",
                                                 "org.freedesktop.portal.Settings", "ReadOne"),
                    dbus_message_unref);
    if (!request || !dbus_message_append_args(request.get(), DBUS_TYPE_STRING, &name_space,
                                              DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID))
        throw std::runtime_error("Could not allocate the desktop motion preference request.");
    BusError error;
    Message reply(
        dbus_connection_send_with_reply_and_block(connection, request.get(), 500, &error.value),
        dbus_message_unref);
    if (!reply) {
        if (dbus_error_has_name(&error.value, "org.freedesktop.portal.Error.NotFound"))
            return std::nullopt;
        throw std::runtime_error(std::string("Could not read the desktop motion preference: ") +
                                 (error.value.message ? error.value.message : "unknown error"));
    }
    if (!dbus_message_has_signature(reply.get(), "v"))
        throw std::runtime_error("Desktop motion preference reply must contain one variant.");
    DBusMessageIter outer{}, value{};
    dbus_message_iter_init(reply.get(), &outer);
    dbus_message_iter_recurse(&outer, &value);
    if (standard) {
        if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_UINT32)
            throw std::runtime_error("Desktop reduced-motion preference must be uint32.");
        dbus_uint32_t reduced = 0;
        dbus_message_iter_get_basic(&value, &reduced);
        // The standardized portal contract maps unknown values to no preference.
        return reduced == 1;
    }
    if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_BOOLEAN)
        throw std::runtime_error("Desktop enable-animations preference must be boolean.");
    dbus_bool_t animations = false;
    dbus_message_iter_get_basic(&value, &animations);
    return !animations;
}

} // namespace

bool linux_reduced_motion() {
    static thread_local Connection connection;
    if (!connection || !dbus_connection_get_is_connected(connection.get()))
        connection = connect_settings();
    if (const auto value = read_motion_setting(connection.get(), "org.freedesktop.appearance",
                                               "reduced-motion", true))
        return *value;
    // Older GNOME portals expose the effective animation setting under this namespace.
    if (const auto value = read_motion_setting(connection.get(), "org.gnome.desktop.interface",
                                               "enable-animations", false))
        return *value;
    throw std::runtime_error(
        "Desktop portal exposes neither reduced-motion nor enable-animations preferences.");
}

} // namespace bbl::pal
