// C standard library
#include <stdint.h>
#include <stdbool.h>

// GLib
#include <glib.h>
#include <gio/gio.h>

// Local
#include "bluez_dbus.h"
#include "freedesktop_dbus.h"

#define BLE_BATTERY_SERVICE_UUID "180f"
#define BLE_BATTERY_LEVEL_CHARACTERISTIC_UUID "2a19"
#define BLUE_CCCD_UUID "2902"

struct BSContext {
    guint8 batt_percent;
    bool notifying;
    GMainLoop *mainLoop;
    GDBusObjectManagerServer *bsObjectManager;
    GDBusObjectManagerClient *bluezObjectManager;
    BluezGattCharacteristic1 *battery_characteristic;
    struct {
        gulong interfaceAdded;
        gulong interfaceRemoved;
    } handlerIds;
    bool appRegistered;
    bool appCreated;
};

static BluezGattManager1 *CreateGattManager(GDBusObjectManagerClient *manager, GDBusObject *obj)
{
    BluezGattManager1 *result = NULL;
    const gchar *name =  g_dbus_object_manager_client_get_name(manager);
    const gchar *path = g_dbus_object_get_object_path(obj);
    GDBusConnection *conn = g_dbus_object_manager_client_get_connection(manager);
    GError *error = NULL;
    result = bluez_gatt_manager1_proxy_new_sync(
        g_dbus_object_manager_client_get_connection(manager),
        G_DBUS_PROXY_FLAGS_NONE,
        name,
        path,
        NULL,
        &error);
    if (error)
        g_error("Failed to create gatt manager: %s\n", error->message);

    return result;
}

static BluezGattManager1 *SearchForGattManager1Interface(struct BSContext *ctx)
{
    BluezGattManager1 *result = NULL;
    GList *objIt = g_dbus_object_manager_get_objects(
        G_DBUS_OBJECT_MANAGER(ctx->bluezObjectManager));
    while (objIt != NULL) {
        GDBusObject *obj = objIt->data;

        if (result == NULL) {
            GDBusInterface *gattManager1Interface =
                g_dbus_object_get_interface(obj, "org.bluez.GattManager1");
            if (gattManager1Interface != NULL) {
                result = CreateGattManager(ctx->bluezObjectManager, obj);
            }
        }

        g_object_unref(obj);
        objIt = objIt->next;
    }
    g_list_free(objIt);

    return result;
}

void ApplicationRegisteredCallback(
    GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    struct BSContext *ctx = user_data;
    GError *error = NULL;
    bluez_gatt_manager1_call_register_application_finish(
        BLUEZ_GATT_MANAGER1(source_object), res, &error);
    if (error != NULL) {
        g_print("Error registering BS application: %s\n", error->message);
        exit(1);
    }
    g_print("Registered BS application\n");
    ctx->appRegistered = true;
}

static void RegisterBSApplication(BluezGattManager1 *gattManager1, struct BSContext *ctx)
{
    GVariantBuilder *optionsBuilder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    GVariant *options = g_variant_builder_end(optionsBuilder);
    g_variant_builder_unref(optionsBuilder);
    bluez_gatt_manager1_call_register_application(
        gattManager1,
        "/io/mangoh/BatteryService",
        options,
        NULL,
        ApplicationRegisteredCallback,
        ctx);
}


static void HandleBusAcquiredForBatt(GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    g_print("BusAcquired\n");

    g_dbus_object_manager_server_set_connection(ctx->bsObjectManager, conn);
    ctx->appCreated = true;
    BluezGattManager1 *gattManager1 = SearchForGattManager1Interface(ctx);
    if (gattManager1 != NULL)
    {
        RegisterBSApplication(gattManager1, ctx);
    }
    g_object_unref(gattManager1);
}

static void HandleNameAcquiredForBatt(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    g_print("NameAcquired\n");
}

static void HandleNameLostForBatt(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    g_print("NameLost\n");
}

static void NotifyBatteryLevel(
    BluezGattCharacteristic1 *gattCharacteristicObject,
    guint8 battery_percent)
{
    const gchar *interface_changed = "org.bluez.GattCharacteristic1";
    GVariantDict *changed_properties;
    g_variant_dict_init(changed_properties, NULL);

    guint8 valueArray[] = {battery_percent};
    GVariant *value = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE, valueArray, G_N_ELEMENTS(valueArray), sizeof(valueArray[0]));
    g_variant_dict_insert_value(changed_properties, "Value", value);

    GVariant *invalidated_properties = g_variant_new_array(G_VARIANT_TYPE_STRING, NULL, 0);

    bluez_gatt_characteristic1_set_value(gattCharacteristicObject, value);
    bluez_gatt_characteristic1_emit_properties_changed(
        gattCharacteristicObject,
        interface_changed,
        g_variant_dict_end(changed_properties),
        invalidated_properties);
}

static gboolean AdjustBatteryLevel(gpointer user_data)
{
    struct BSContext *ctx = user_data;
    static gint8 delta = -1;
    if (ctx->batt_percent == 0 && delta == -1)
        delta = 1;
    else if (ctx->batt_percent == 100 && delta == 1)
        delta = -1;

    ctx->batt_percent = ctx->batt_percent + delta;

    if (ctx->notifying) {
        NotifyBatteryLevel(ctx->battery_characteristic, ctx->batt_percent);
    }
}

static gboolean HandleStartNotifyForBattLevel(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    struct BSContext *ctx = user_data;
    if (!ctx->notifying)
    {
        ctx->notifying = true;
        NotifyBatteryLevel(interface, ctx->batt_percent);
    }

    bluez_gatt_characteristic1_complete_start_notify(interface, invocation);
    return TRUE;
}

static gboolean HandleStopNotifyForBattLevel(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    struct BSContext *ctx = user_data;
    ctx->notifying = false;

    bluez_gatt_characteristic1_complete_stop_notify(interface, invocation);
    return TRUE;
}

static gboolean HandleReadValueForBattLevel(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    const GVariant *options,
    gpointer user_data)
{
    struct BSContext *ctx = user_data;
    g_print("HandleReadValueForBattLevel called\n");
    guint8 valueArray[] = {ctx->batt_percent};
    GVariant *value = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE, valueArray, G_N_ELEMENTS(valueArray), sizeof(valueArray[0]));

    bluez_gatt_characteristic1_set_value(interface, value);
    g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&value, 1));

    return TRUE;
}

GDBusObjectManagerServer *CreateBSObjectManager(struct BSContext *ctx)
{
    GDBusObjectManagerServer *om = g_dbus_object_manager_server_new("/io/mangoh/BatteryService");

    GDBusObjectSkeleton *bos = g_dbus_object_skeleton_new("/io/mangoh/BatteryService/service0");
    BluezGattService1 *bgs = bluez_gatt_service1_skeleton_new();
    bluez_gatt_service1_set_uuid(bgs, BLE_BATTERY_SERVICE_UUID);
    bluez_gatt_service1_set_primary(bgs, TRUE);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgs));
    g_object_unref(bgs);
    g_dbus_object_manager_server_export(om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    bos = g_dbus_object_skeleton_new("/io/mangoh/BatteryService/service0/char0");
    BluezGattCharacteristic1 *bgc = bluez_gatt_characteristic1_skeleton_new();
    bluez_gatt_characteristic1_set_uuid(bgc, BLE_BATTERY_LEVEL_CHARACTERISTIC_UUID);
    const gchar *batteryLevelCharacteristicFlags[] = {
        "read",
        "notify",
        NULL
    };
    bluez_gatt_characteristic1_set_flags(bgc, batteryLevelCharacteristicFlags);
    bluez_gatt_characteristic1_set_service(bgc, "/io/mangoh/BatteryService/service0");
    g_signal_connect(bgc, "handle-read-value", G_CALLBACK(HandleReadValueForBattLevel), ctx);
    g_signal_connect(bgc, "handle-start-notify", G_CALLBACK(HandleStartNotifyForBattLevel), ctx);
    g_signal_connect(bgc, "handle-stop-notify", G_CALLBACK(HandleStopNotifyForBattLevel), ctx);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgc));
    ctx->battery_characteristic = bgc;
    g_dbus_object_manager_server_export(om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);


    bos = g_dbus_object_skeleton_new("/io/mangoh/BatteryService/service0/char0/desc0");
    BluezGattDescriptor1 *bgd = bluez_gatt_descriptor1_skeleton_new();
    bluez_gatt_descriptor1_set_uuid(bgd, BLUE_CCCD_UUID);
    bluez_gatt_descriptor1_set_characteristic(bgd, "/io/mangoh/BatteryService/service0/char0");
    const gchar *batteryLevelDescriptorFlags[] = {
        "read",
        NULL
    };
    bluez_gatt_descriptor1_set_flags(bgd, batteryLevelDescriptorFlags);
    g_dbus_object_skeleton_add_interface(bos, G_DBUS_INTERFACE_SKELETON(bgd));
    g_object_unref(bgd);
    g_dbus_object_manager_server_export(om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    return om;
}

static void BluezInterfaceAddedHandler(
    GDBusObjectManager *manager,
    GDBusObject *object,
    GDBusInterface *interface,
    gpointer context)
{
    struct BSContext *ctx = context;
    if (ctx->appCreated && !ctx->appRegistered)
    {
        GDBusProxy *interfaceProxy = G_DBUS_PROXY(interface);
        const gchar *interfaceName = g_dbus_proxy_get_interface_name(interfaceProxy);
        g_print("signal interface-added for interface %s\n", interfaceName);
        if (strcmp(interfaceName, "org.bluez.GattManager1") == 0) {

            BluezGattManager1 *gattManager1 = CreateGattManager(
                G_DBUS_OBJECT_MANAGER_CLIENT(manager), object);
            RegisterBSApplication(gattManager1, ctx);
            g_object_unref(gattManager1);
        }
    }
}

static void BluezInterfaceRemovedHandler(
    GDBusObjectManager *manager,
    GDBusObject *object,
    GDBusInterface *interface,
    gpointer context)
{
    GDBusProxy *interfaceProxy = G_DBUS_PROXY(interface);
    g_print(
        "signal interface-removed for interface %s\n",
        g_dbus_proxy_get_interface_name(interfaceProxy));
}

int main(int argc, char **argv)
{
    g_print("Starting fake battery service!\n");

    struct BSContext ctx = { .batt_percent=50, .notifying=false };
    ctx.mainLoop = g_main_loop_new(NULL, FALSE);
    GError *error = NULL;
    GDBusObjectManager *bluezObjectManager = g_dbus_object_manager_client_new_for_bus_sync(
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
    ctx.bluezObjectManager = G_DBUS_OBJECT_MANAGER_CLIENT(bluezObjectManager);
    ctx.bsObjectManager = CreateBSObjectManager(&ctx);

    guint id = g_bus_own_name(
        G_BUS_TYPE_SYSTEM,
        "io.mangoh.BatteryService",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        HandleBusAcquiredForBatt,
        HandleNameAcquiredForBatt,
        HandleNameLostForBatt,
        &ctx,
        NULL);


    ctx.handlerIds.interfaceAdded = g_signal_connect(
        ctx.bluezObjectManager,
        "interface-added",
        G_CALLBACK(BluezInterfaceAddedHandler),
        &ctx);
    ctx.handlerIds.interfaceRemoved = g_signal_connect(
        ctx.bluezObjectManager,
        "interface-removed",
        G_CALLBACK(BluezInterfaceRemovedHandler),
        &ctx);

    g_timeout_add(10000, AdjustBatteryLevel, &ctx);
    g_main_loop_run(ctx.mainLoop);

    g_bus_unown_name(id);

    return 0;
}
