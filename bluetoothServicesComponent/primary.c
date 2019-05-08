// C standard library
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// GLib
#include <glib.h>
#include <gio/gio.h>

// Local
#include "battery_service.h"
#include "immediate_alert.h"
#include "bluez_dbus.h"

struct State
{
    GDBusObjectManagerServer *services_om;
    GDBusObjectManagerClient *bluez_om;
    gulong interface_added_handler_id;
    gulong interface_removed_handler_id;
    bool application_registered;
};

static BluezGattManager1 *create_gatt_manager_proxy(GDBusObjectManagerClient *bluez_om, GDBusObject *obj)
{
    BluezGattManager1 *result = NULL;
    const gchar *name = g_dbus_object_manager_client_get_name(bluez_om);
    const gchar *path = g_dbus_object_get_object_path(obj);
    GDBusConnection *conn = g_dbus_object_manager_client_get_connection(bluez_om);
    GError *error = NULL;
    result = bluez_gatt_manager1_proxy_new_sync(
        conn,
        G_DBUS_PROXY_FLAGS_NONE,
        name,
        path,
        NULL,
        &error);
    if (error)
        g_error("Failed to create gatt manager: %s\n", error->message);

    return result;
}

static BluezGattManager1 *search_for_gatt_manager1_interface(GDBusObjectManagerClient *bluez_om)
{
    BluezGattManager1 *result = NULL;
    GList *objIt = g_dbus_object_manager_get_objects(
        G_DBUS_OBJECT_MANAGER(bluez_om));
    while (objIt != NULL) {
        GDBusObject *obj = objIt->data;

        if (result == NULL) {
            GDBusInterface *gattManager1Interface =
                g_dbus_object_get_interface(obj, "org.bluez.GattManager1");
            if (gattManager1Interface != NULL) {
                result = create_gatt_manager_proxy(bluez_om, obj);
            }
        }

        g_object_unref(obj);
        objIt = objIt->next;
    }
    g_list_free(objIt);

    return result;
}

static void application_registered_callback(
    GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    // struct State *state = user_data;
    GError *error = NULL;
    bluez_gatt_manager1_call_register_application_finish(
        BLUEZ_GATT_MANAGER1(source_object), res, &error);
    if (error != NULL) {
        g_print("Error registering BS application: %s\n", error->message);
        exit(1);
    }
    g_print("Registered BS application\n");
}

static void register_application(BluezGattManager1 *gatt_manager, struct State *state)
{
    GVariantBuilder *options_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    GVariant *options = g_variant_builder_end(options_builder);
    g_variant_builder_unref(options_builder);
    bluez_gatt_manager1_call_register_application(
        gatt_manager,
        "/io/mangoh",
        options,
        NULL,
        application_registered_callback,
        state);
    state->application_registered = true;
}

static void handle_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data)
{
    g_print("bus acquired\n");
    struct State *state = user_data;

    g_dbus_object_manager_server_set_connection(state->services_om, conn);
    BluezGattManager1 *gatt_manager = search_for_gatt_manager1_interface(state->bluez_om);
    if (gatt_manager != NULL)
    {
        register_application(gatt_manager, state);
        g_object_unref(gatt_manager);
    }
}

static void handle_name_acquired(
    GDBusConnection *conn, const gchar *name, gpointer user_data)
{
    g_print("name acquired: %s\n", name);
}

static void handle_name_lost(
    GDBusConnection *conn, const gchar *name, gpointer user_data)
{
    g_print("name lost: %s\n", name);
}

static void bluez_interface_added_handler(
    GDBusObjectManager *manager,
    GDBusObject *object,
    GDBusInterface *interface,
    gpointer context)
{
    struct State *state = context;
    if (!state->application_registered)
    {
        GDBusProxy *interface_proxy = G_DBUS_PROXY(interface);
        const gchar *interface_name = g_dbus_proxy_get_interface_name(interface_proxy);
        g_print("signal interface-added for interface %s\n", interface_name);
        if (g_strcmp0(interface_name, "org.bluez.GattManager1") == 0) {
            BluezGattManager1 *gatt_manager = create_gatt_manager_proxy(
                G_DBUS_OBJECT_MANAGER_CLIENT(manager), object);
            register_application(gatt_manager, state);
            g_object_unref(gatt_manager);
        }
    }
}

static void bluez_interface_removed_handler(
    GDBusObjectManager *manager,
    GDBusObject *object,
    GDBusInterface *interface,
    gpointer context)
{
    // struct State *state = context;
    GDBusProxy *interface_proxy = G_DBUS_PROXY(interface);
    const gchar *interface_name = g_dbus_proxy_get_interface_name(interface_proxy);
    g_print("signal interface-removed for interface %s\n", interface_name);
    if (g_strcmp0(interface_name, "org.bluez.GattManager1") == 0) {
        /*
         * TODO: In the case that a GattManager1 interface is removed, I think we need to check and
         * see if it is *the* interface that the application was registered on. If it is, then I
         * think the application needs to be deactivated and reset so that it is waiting for a
         * GattManager1 interface to be added again.
         */
        // state->application_registered = false;
    }
}

void run_bluetooth_services(void)
{
    struct State *state = g_malloc0(sizeof(*state));
    size_t num_services_registered = 0;
    state->services_om = g_dbus_object_manager_server_new("/io/mangoh");

    battery_register_services(state->services_om, &num_services_registered);
    alert_register_services(state->services_om, &num_services_registered);

    GError *error = NULL;
    GDBusObjectManager *bluez_om = g_dbus_object_manager_client_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
        "org.bluez",
        "/",
        NULL,
        NULL,
        NULL,
        NULL,
        &error);
    if (error != NULL) {
        g_print("Error creating bluez object manager client: %s\n", error->message);
        exit(1);
    }
    state->bluez_om = G_DBUS_OBJECT_MANAGER_CLIENT(bluez_om);
    state->interface_added_handler_id = g_signal_connect(
        bluez_om,
        "interface-added",
        G_CALLBACK(bluez_interface_added_handler),
        state);
    state->interface_removed_handler_id = g_signal_connect(
        bluez_om,
        "interface-removed",
        G_CALLBACK(bluez_interface_removed_handler),
        state);

    guint id = g_bus_own_name(
        G_BUS_TYPE_SYSTEM,
        "io.mangoh",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        handle_bus_acquired,
        handle_name_acquired,
        handle_name_lost,
        state,
        NULL);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    g_bus_unown_name(id);
}
