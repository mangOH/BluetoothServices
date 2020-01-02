// C standard library
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// GLib
#include <glib.h>
#include <gio/gio.h>

// Local
#include "battery_service.h"
#include "org.bluez.GattCharacteristic1.h"
#include "org.bluez.GattService1.h"

#define BLE_BATTERY_LEVEL_CHARACTERISTIC_UUID "2a19"
#define BLUE_CCCD_UUID "2902"

struct BSContext2 {
    // TODO: This goes away when connected to a real battery
    guint8 batt_percent;
    gint8 batt_delta;
    bool notifying;
    BluezGattCharacteristic1 *battery_characteristic;
};

static void notify_battery_level(
    BluezGattCharacteristic1 *gatt_characteristic_object,
    guint8 battery_percent)
{
    guint8 value_array[] = {battery_percent};
    GVariant *value = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE, value_array, G_N_ELEMENTS(value_array), sizeof(value_array[0]));

    bluez_gatt_characteristic1_set_value(gatt_characteristic_object, value);
}

static gboolean adjust_battery_level(gpointer user_data)
{
    struct BSContext2 *ctx = user_data;
    if (ctx->batt_percent == 0 && ctx->batt_delta == -1)
        ctx->batt_delta = 1;
    else if (ctx->batt_percent == 100 && ctx->batt_delta == 1)
        ctx->batt_delta = -1;

    ctx->batt_percent = ctx->batt_percent + ctx->batt_delta;

    g_print("Adjusted battery level to %u\n", ctx->batt_percent);

    if (ctx->notifying) {
        notify_battery_level(ctx->battery_characteristic, ctx->batt_percent);
    }

    return TRUE;
}

static gboolean handle_start_notify(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    struct BSContext2 *ctx = user_data;
    if (!ctx->notifying)
    {
        ctx->notifying = true;
        notify_battery_level(interface, ctx->batt_percent);
    }

    bluez_gatt_characteristic1_complete_start_notify(interface, invocation);
    return TRUE;
}

static gboolean handle_stop_notify(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    struct BSContext2 *ctx = user_data;
    ctx->notifying = false;

    bluez_gatt_characteristic1_complete_stop_notify(interface, invocation);
    return TRUE;
}

static gboolean handle_read_value(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    GVariant *options,
    gpointer user_data)
{
    struct BSContext2 *ctx = user_data;
    g_print("%s called\n", __func__);
    guint8 valueArray[] = {ctx->batt_percent};
    GVariant *value = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE, valueArray, G_N_ELEMENTS(valueArray), sizeof(valueArray[0]));
    /*
     * It seems that it is necessary to sink the reference because if something else sinks the
     * reference and then frees it, then the variant might be freed while it is still needed later
     * in this function.
     */
    g_variant_ref_sink(value);

    bluez_gatt_characteristic1_set_value(interface, value);
    bluez_gatt_characteristic1_complete_read_value(interface, invocation, value);
    g_variant_unref(value);

    return TRUE;
}

void battery_register_services(
    GDBusObjectManagerServer *services_om,
    size_t *num_services_registered)
{
    struct BSContext2 *ctx = g_malloc0(sizeof(*ctx));
    ctx->batt_percent = 50;
    ctx->batt_delta = -1;
    const gchar *om_path =
        g_dbus_object_manager_get_object_path(G_DBUS_OBJECT_MANAGER(services_om));

    gchar *service_path = g_strdup_printf("%s/service%zu", om_path, *num_services_registered);
    GDBusObjectSkeleton *bos = g_dbus_object_skeleton_new(service_path);
    BluezGattService1 *bgs = bluez_gatt_service1_skeleton_new();
    bluez_gatt_service1_set_uuid(bgs, BLE_BATTERY_SERVICE_UUID);
    bluez_gatt_service1_set_primary(bgs, TRUE);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgs));
    g_object_unref(bgs);
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    gchar *characteristic_path = g_strconcat(service_path, "/char0", NULL);
    bos = g_dbus_object_skeleton_new(characteristic_path);
    BluezGattCharacteristic1 *bgc = bluez_gatt_characteristic1_skeleton_new();
    bluez_gatt_characteristic1_set_uuid(bgc, BLE_BATTERY_LEVEL_CHARACTERISTIC_UUID);
    const gchar *batteryLevelCharacteristicFlags[] = {
        "read",
        "notify",
        NULL
    };
    bluez_gatt_characteristic1_set_flags(bgc, batteryLevelCharacteristicFlags);
    bluez_gatt_characteristic1_set_service(bgc, service_path);
    g_signal_connect(bgc, "handle-read-value", G_CALLBACK(handle_read_value), ctx);
    g_signal_connect(bgc, "handle-start-notify", G_CALLBACK(handle_start_notify), ctx);
    g_signal_connect(bgc, "handle-stop-notify", G_CALLBACK(handle_stop_notify), ctx);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgc));
    ctx->battery_characteristic = bgc;
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    g_free(characteristic_path);
    g_free(service_path);

    *num_services_registered += 1;

    g_timeout_add(10000, adjust_battery_level, ctx);
}
