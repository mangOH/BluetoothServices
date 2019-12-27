// C standard library
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// GLib
#include <glib.h>
#include <gio/gio.h>

// Local
#include "immediate_alert.h"
#include "org.bluez.GattCharacteristic1.h"
#include "org.bluez.GattService1.h"

#define ALERT_LEVEL_CHARACTERISTIC_UUID "2a06"


#define NO_ALERT 0
#define MILD_ALERT 1
#define HIGH_ALERT 2

static gboolean handle_write_value(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    GVariant *value,
    GVariant *options,
    gpointer user_data)
{
    // TODO: check options

    if (!g_variant_is_of_type(value, G_VARIANT_TYPE_BYTESTRING)) {
        g_print(
            "%s received value of unexpected type: \"%s\"\n",
            __func__,
            g_variant_get_type_string(value));
        goto done;
    }

    gsize n_elements;
    const guint8 *value_array = g_variant_get_fixed_array(value, &n_elements, sizeof(guint8));
    if (n_elements !=  1) {
        g_print("%s received value of unexpected length: %zu\n", __func__, n_elements);
        goto done;
    }

    const guint8 alert_level = value_array[0];

    switch (alert_level) {
    case NO_ALERT:
        g_print("Received request for no alert\n");
        break;

    case MILD_ALERT:
        g_print("Received request for mild alert\n");
        break;

    case HIGH_ALERT:
        g_print("Received request for high alert\n");
        break;
    }

done:
    bluez_gatt_characteristic1_complete_write_value(interface, invocation);

    return TRUE;
}

void alert_register_services(
    GDBusObjectManagerServer *services_om,
    size_t *num_services_registered)
{
    const gchar *om_path =
        g_dbus_object_manager_get_object_path(G_DBUS_OBJECT_MANAGER(services_om));

    gchar *service_path = g_strdup_printf("%s/service%zu", om_path, *num_services_registered);
    GDBusObjectSkeleton *service_object = g_dbus_object_skeleton_new(service_path);
    BluezGattService1 *service_interface = bluez_gatt_service1_skeleton_new();
    bluez_gatt_service1_set_uuid(service_interface, IMMEDIATE_ALERT_SERVICE_UUID);
    bluez_gatt_service1_set_primary(service_interface, TRUE);
    //g_print("About to set includes for immediate alert service\n");
    ////const gchar *const includes[] = { NULL };
    //bluez_gatt_service1_set_includes(service_interface, NULL);
    g_dbus_object_skeleton_add_interface(
        service_object, G_DBUS_INTERFACE_SKELETON(service_interface));
    g_object_unref(service_interface);
    g_dbus_object_manager_server_export(services_om, service_object);
    g_object_unref(service_object);

    gchar *characteristic_path = g_strconcat(service_path, "/char0", NULL);
    GDBusObjectSkeleton *characteristic_object = g_dbus_object_skeleton_new(characteristic_path);
    BluezGattCharacteristic1 *characteristic_interface = bluez_gatt_characteristic1_skeleton_new();
    bluez_gatt_characteristic1_set_uuid(characteristic_interface, ALERT_LEVEL_CHARACTERISTIC_UUID);
    const gchar *characteristic_flags[] = {
        "write",
        NULL
    };
    bluez_gatt_characteristic1_set_flags(characteristic_interface, characteristic_flags);
    bluez_gatt_characteristic1_set_service(characteristic_interface, service_path);
    g_signal_connect(
        characteristic_interface, "handle-write-value", G_CALLBACK(handle_write_value), NULL);
    g_dbus_object_skeleton_add_interface(
        characteristic_object, G_DBUS_INTERFACE_SKELETON(characteristic_interface));
    g_object_unref(characteristic_interface);
    g_dbus_object_manager_server_export(services_om, characteristic_object);
    g_object_unref(characteristic_object);

    g_free(characteristic_path);
    g_free(service_path);

    *num_services_registered += 1;
}
