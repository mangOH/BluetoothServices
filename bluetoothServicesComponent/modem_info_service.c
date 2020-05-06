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
#include "org.bluez.GattDescriptor1.h"

#define MODEM_INFO_FSN_CHARACTERISTIC_UUID "2A25"
#define MODEM_INFO_IMEI_CHARACTERISTIC_UUID "fb22d0b6-7c72-4e29-a156-df6518f69ec4"
#define CHARACTERISTIC_PRESENTATION_FORMAT_UUID "2904"

static gboolean handle_read_fsn_value(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    GVariant *options,
    gpointer user_data)
{
    gchar fsn[32];
    le_info_GetPlatformSerialNumber(fsn, 32);
    g_print("%s called with FSN: %s\n", __func__, fsn);
    GVariant *value = g_variant_new_bytestring((const gchar *)fsn);
    g_variant_ref_sink(value);
    bluez_gatt_characteristic1_set_value(interface, value);
    bluez_gatt_characteristic1_complete_read_value(interface, invocation, value);
    g_variant_unref(value);

    return TRUE;
}

static gboolean handle_read_imei_value(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    GVariant *options,
    gpointer user_data)
{
    gchar imei[32];
    le_info_GetImei(imei, 32);
    g_print("%s called with IMEI: %s\n", __func__, imei);
    GVariant *value = g_variant_new_bytestring ((const gchar *)imei);
    g_variant_ref_sink(value);
    bluez_gatt_characteristic1_set_value(interface, value);
    bluez_gatt_characteristic1_complete_read_value(interface, invocation, value);
    g_variant_unref(value);

    return TRUE;
}

static gboolean handle_read_cpf_value(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    GVariant *options,
    gpointer user_data)
{
    /**
     * Characteristic Presentation Format for IMEI
     * - Format:        0x19    (UTF-8 string)
     * - Exponent:      0x00    (No change)
     * - Unit:          0x2700  (Unitless)
     * - Namespace:     0x01    (Bluetooth SIG Assigned Numbers)
     * - Description:   0x0000  (Unknown)
     */
    guint8 custom_format[] = { 0x19, 0x00, 0x00, 0x27, 0x01, 0x00, 0x00 };

    g_print("%s called\n", __func__);

    GVariant *value = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE, custom_format, G_N_ELEMENTS(custom_format), sizeof(custom_format[0]));
    g_variant_ref_sink(value);
    bluez_gatt_characteristic1_set_value(interface, value);
    bluez_gatt_characteristic1_complete_read_value(interface, invocation, value);
    g_variant_unref(value);

    return TRUE;
}


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

    BluezGattCharacteristic1 *bgc = bluez_gatt_characteristic1_skeleton_new();
    bluez_gatt_characteristic1_set_uuid(bgc, MODEM_INFO_FSN_CHARACTERISTIC_UUID);
    const gchar *modem_info_fsn_CharacteristicFlags[] = {
        "read",
        NULL
    };
    bluez_gatt_characteristic1_set_flags(bgc, modem_info_fsn_CharacteristicFlags);
    bluez_gatt_characteristic1_set_service(bgc, service_path);
    g_signal_connect(bgc, "handle-read-value", G_CALLBACK(handle_read_fsn_value), NULL);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgc));
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    // IMEI
    gchar *imei_characteristic_path = g_strconcat(service_path, "/imei", NULL);
    bos = g_dbus_object_skeleton_new(imei_characteristic_path);

    bgc = bluez_gatt_characteristic1_skeleton_new();
    bluez_gatt_characteristic1_set_uuid(bgc, MODEM_INFO_IMEI_CHARACTERISTIC_UUID);
    const gchar *modem_info_imei_CharacteristicFlags[] = {
        "read",
        NULL
    };
    bluez_gatt_characteristic1_set_flags(bgc, modem_info_imei_CharacteristicFlags);
    bluez_gatt_characteristic1_set_service(bgc, service_path);
    g_signal_connect(bgc, "handle-read-value", G_CALLBACK(handle_read_imei_value), NULL);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgc));
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    // Characteristic Presentation Format
    gchar *cpf_characteristic_path = g_strconcat(imei_characteristic_path, "/imei_cpf", NULL);
    bos = g_dbus_object_skeleton_new(cpf_characteristic_path);

    BluezGattDescriptor1 *bgd = bluez_gatt_descriptor1_skeleton_new();
    bluez_gatt_descriptor1_set_uuid(bgd, CHARACTERISTIC_PRESENTATION_FORMAT_UUID);
    const gchar *cpf_CharacteristicFlags[] = {
        "read",
        NULL
    };
    bluez_gatt_descriptor1_set_flags(bgd, cpf_CharacteristicFlags);
    bluez_gatt_descriptor1_set_characteristic(bgd, imei_characteristic_path);
    g_signal_connect(bgd, "handle-read-value", G_CALLBACK(handle_read_cpf_value), NULL);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgd));
    g_dbus_object_manager_server_export(services_om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    /* cleanup */
    g_free(fsn_characteristic_path);
    g_free(imei_characteristic_path);
    g_free(cpf_characteristic_path);
    g_free(service_path);

    *num_services_registered += 1;
}
