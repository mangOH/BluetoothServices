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

#define BLUEZ_INTF_ADAPTER "org.bluez.Adapter1"
#define BLUEZ_INTF_GATT_MANAGER "org.bluez.GattManager1"
#define BLUEZ_INTF_LE_ADVERTISING_MANAGER "org.bluez.LEAdvertisingManager1"

struct State
{
    GDBusObjectManagerServer *services_om;
};

static void advertisement_registered_callback(
    GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GError *error = NULL;
    bluez_leadvertising_manager1_call_register_advertisement_finish(
        BLUEZ_LEADVERTISING_MANAGER1(source_object), res, &error);
    if (error != NULL) {
        g_print("Error registering bluetooth application: %s\n", error->message);
        exit(1);
    }

    g_print("Advertising object registered\n");
}

static void create_and_populate_advertisement(struct State *state)
{
    GDBusObjectSkeleton *obj_skel = g_dbus_object_skeleton_new("/io/mangoh/advertisement");
    BluezLEAdvertisement1 *adv_skel = bluez_leadvertisement1_skeleton_new();
    // TODO: "broadcast" is also valid.  Not sure which to use.
    bluez_leadvertisement1_set_type_(adv_skel, "peripheral");
    bluez_leadvertisement1_set_local_name(adv_skel, "mangOH");
    // TODO: should this be infinite?  Does 0 mean infinite?
    bluez_leadvertisement1_set_timeout(adv_skel, 500);

    const gchar* service_uuids[] = {
        BLE_BATTERY_SERVICE_UUID,
        IMMEDIATE_ALERT_SERVICE_UUID,
        NULL,
    };
    bluez_leadvertisement1_set_service_uuids(adv_skel, service_uuids);

    /*
     * Refer to:
     * https://www.bluetooth.com/wp-content/uploads/Sitecore-Media-Library/Gatt/Xml/Characteristics/org.bluetooth.characteristic.gap.appearance.xml
     */
    const guint16 appearance_generic_computer = 128;
    bluez_leadvertisement1_set_appearance(adv_skel, appearance_generic_computer);

    g_dbus_object_skeleton_add_interface(obj_skel, G_DBUS_INTERFACE_SKELETON(adv_skel));
    g_object_unref(adv_skel);

    g_dbus_object_manager_server_export(state->services_om, G_DBUS_OBJECT_SKELETON(obj_skel));
    g_object_unref(obj_skel);

    // Advertisement object is created, now register it with Bluez
    GError *error = NULL;
    BluezLEAdvertisingManager1 *adv_manager = bluez_leadvertising_manager1_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.bluez",
        "/org/bluez/hci0",
        NULL,
        &error);
    if (error)
        g_error("Failed to create proxy for LE advertising manager on hci0: %s\n", error->message);

    GVariant *options = g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
    bluez_leadvertising_manager1_call_register_advertisement(
        adv_manager, "/io/mangoh/advertisement", options, NULL, advertisement_registered_callback, NULL);

    g_object_unref(adv_manager);
    g_print("Registered advertising object\n");
}


static void application_registered_callback(
    GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    struct State *state = user_data;
    GError *error = NULL;
    bluez_gatt_manager1_call_register_application_finish(
        BLUEZ_GATT_MANAGER1(source_object), res, &error);
    if (error != NULL) {
        g_print("Error registering bluetooth application: %s\n", error->message);
        exit(1);
    }
    g_print("Registered bluetooth application\n");

    create_and_populate_advertisement(state);
}

static void handle_bus_acquired(GDBusConnection *conn, const gchar *name, gpointer user_data)
{
    g_print("bus acquired\n");
    struct State *state = user_data;
    GError *error = NULL;

    g_dbus_object_manager_server_set_connection(state->services_om, conn);
    BluezGattManager1 *gatt_manager = bluez_gatt_manager1_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.bluez",
        "/org/bluez/hci0",
        NULL,
        &error);
    if (error)
        g_error("Failed to create proxy for GATT manager on hci0: %s\n", error->message);

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

    g_object_unref(gatt_manager);
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

static void initialize_adapter(void)
{
    GError *error = NULL;
    BluezAdapter1 *adapter = bluez_adapter1_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.bluez",
        "/org/bluez/hci0",
        NULL,
        &error);
    if (error)
        g_error("Failed to create proxy for hci0 adapter: %s\n", error->message);

    bluez_adapter1_set_powered(adapter, TRUE);
    g_object_unref(adapter);
}


void run_bluetooth_services(void)
{
    struct State *state = g_malloc0(sizeof(*state));
    size_t num_services_registered = 0;
    state->services_om = g_dbus_object_manager_server_new("/io/mangoh");

    battery_register_services(state->services_om, &num_services_registered);
    alert_register_services(state->services_om, &num_services_registered);

    initialize_adapter();

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

