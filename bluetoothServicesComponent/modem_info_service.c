// C standard library
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// GLib
#include <glib.h>
#include <gio/gio.h>

// Legato
#include "legato.h"
#include "interfaces.h"

// Local
#include "modem_info_service.h"
#include "org.bluez.GattCharacteristic1.h"
#include "org.bluez.GattService1.h"

#define MODEM_INFO_FSN_CHARACTERISTIC_UUID "2A25"
#define MODEM_INFO_IMEI_CHARACTERISTIC_UUID "2A27"

struct FSNContext {
    gchar fsn[32];
    BluezGattCharacteristic1 *bt_characteristic;
};

struct IMEIContext {
    gchar imei[32];
    BluezGattCharacteristic1 *bt_characteristic;
};

static gboolean handle_read_fsn_value(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    GVariant *options,
    gpointer user_data)
{
    struct FSNContext *ctx = user_data;
    memset(ctx->fsn, 0, 32);
    le_info_GetPlatformSerialNumber(ctx->fsn, 32);
    g_print("%s called with FSN: %s\n", __func__, ctx->fsn);
    GVariant *value = g_variant_new_bytestring((const gchar *)ctx->fsn);
    g_variant_ref_sink(value);
    bluez_gatt_characteristic1_set_value(interface, value);
    bluez_gatt_characteristic1_complete_read_value(interface, invocation, value);
    g_variant_unref(value);

    return TRUE;
}
/* FSN */

static gboolean handle_read_imei_value(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    GVariant *options,
    gpointer user_data)
{
    struct IMEIContext *ctx = user_data;
    // memset(ctx->imei, 0, 32);
    le_info_GetImei(ctx->imei, 32);
    g_print("%s called with IMEI: %s\n", __func__, ctx->imei);
    GVariant *value = g_variant_new_bytestring ((const gchar *)ctx->imei);
    g_variant_ref_sink(value);
    bluez_gatt_characteristic1_set_value(interface, value);
    bluez_gatt_characteristic1_complete_read_value(interface, invocation, value);
    g_variant_unref(value);

    return TRUE;
}
/* IMEI */

void modem_info_register_services(
    GDBusObjectManagerServer *services_om,
    size_t *num_services_registered)
{
    const gchar *om_path =
        g_dbus_object_manager_get_object_path(G_DBUS_OBJECT_MANAGER(services_om));

    gchar *service_path = g_strdup_printf("%s/service%zu", om_path, *num_services_registered);
    GDBusObjectSkeleton *bos = g_dbus_object_skeleton_new(service_path);
    BluezGattService1 *bgs = bluez_gatt_service1_skeleton_new();
    bluez_gatt_service1_set_uuid(bgs, MODEM_INFO_SERVICE_UUID);
    bluez_gatt_service1_set_primary(bgs, TRUE);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgs));
    g_object_unref(bgs);
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    // FSN
    gchar *fsn_characteristic_path = g_strconcat(service_path, "/fsn", NULL);
    bos = g_dbus_object_skeleton_new(fsn_characteristic_path);

    struct FSNContext *fsnctx = g_malloc0(sizeof(*fsnctx));
    BluezGattCharacteristic1 *bgc = bluez_gatt_characteristic1_skeleton_new();
    bluez_gatt_characteristic1_set_uuid(bgc, MODEM_INFO_FSN_CHARACTERISTIC_UUID);
    const gchar *modem_info_fsn_CharacteristicFlags[] = {
        "read",
        NULL
    };
    bluez_gatt_characteristic1_set_flags(bgc, modem_info_fsn_CharacteristicFlags);
    bluez_gatt_characteristic1_set_service(bgc, service_path);
    g_signal_connect(bgc, "handle-read-value", G_CALLBACK(handle_read_fsn_value), fsnctx);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgc));
    fsnctx->bt_characteristic = bgc;
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    // IMEI
    gchar *imei_characteristic_path = g_strconcat(service_path, "/imei", NULL);
    bos = g_dbus_object_skeleton_new(imei_characteristic_path);

    struct IMEIContext *imeictx = g_malloc0(sizeof(*imeictx));
    bgc = bluez_gatt_characteristic1_skeleton_new();
    bluez_gatt_characteristic1_set_uuid(bgc, MODEM_INFO_IMEI_CHARACTERISTIC_UUID);
    const gchar *modem_info_imei_CharacteristicFlags[] = {
        "read",
        NULL
    };
    bluez_gatt_characteristic1_set_flags(bgc, modem_info_imei_CharacteristicFlags);
    bluez_gatt_characteristic1_set_service(bgc, service_path);
    g_signal_connect(bgc, "handle-read-value", G_CALLBACK(handle_read_imei_value), imeictx);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgc));
    imeictx->bt_characteristic = bgc;
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    g_free(fsn_characteristic_path);
    g_free(imei_characteristic_path);
    g_free(service_path);

    *num_services_registered += 1;
}
