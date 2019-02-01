// C standard library
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// GLib
#include <glib.h>
#include <gio/gio.h>

// Local
#include "gen/bluez_dbus.h"

#define BLE_BATTERY_SERVICE_UUID "180f"
#define BLE_BATTERY_LEVEL_CHARACTERISTIC_UUID "2a19"

struct BSContext {
    GMainLoop *mainLoop;
    GDBusObjectManagerServer *bsObjectManager;
    GDBusObjectManager *bluezObjectManager;
    struct {
        gulong interfaceAdded;
        gulong interfaceRemoved;
    } handlerIds;
    bool appRegistered;
    bool appCreated;
};

static GDBusProxy* SearchForGattManager1Interface(struct BSContext *ctx)
{
    GDBusProxy *result = NULL;
    GList *objIt = g_dbus_object_manager_get_objects(ctx->bluezObjectManager);
    while (objIt != NULL) {
        GDBusObject *obj = objIt->data;

        if (result == NULL) {
            GDBusInterface *gattManager1Interface =
                g_dbus_object_get_interface(obj, "org.bluez.GattManager1");
            if (gattManager1Interface != NULL) {
                printf(
                    "Found object: %s which implements org.bluez.GattManager1\n",
                    g_dbus_object_get_object_path(obj));
                result = G_DBUS_PROXY(gattManager1Interface);
            }
        }

        g_object_unref(obj);
        objIt = objIt->next;
    }
    g_list_free(objIt);

    return result;
}

static void HandleBusAcquiredForBatt(GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    printf("BusAcquired\n");
}

static void RegisterBSApplication(GDBusProxy *gattManager1Proxy, struct BSContext *ctx)
{
    GError *error = NULL;
    const gchar *application = "/io/mangoh/BatteryService";
    GVariantBuilder *optionsBuilder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
    // TODO: Options required?
    // g_variant_builder_add(optionsBuilder, "{sv}", "name", g_variant_new_string("Bob"));
    GVariant *options = g_variant_builder_end(optionsBuilder);
    GVariant *res = g_dbus_proxy_call_sync(
        gattManager1Proxy,
        "RegisterApplication",
        g_variant_new("(o@a{sv})", application, options),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);
    if (error != NULL) {
        printf("Error registering BS application: %s\n", error->message);
        exit(1);
    }
    printf("Registered BS application\n");
    ctx->appRegistered = true;
}

static void HandleNameAcquiredForBatt(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    printf("NameAcquired\n");

    g_dbus_object_manager_server_set_connection(ctx->bsObjectManager, conn);
    ctx->appCreated = true;
    GDBusProxy *gattManager1InterfaceProxy = SearchForGattManager1Interface(ctx);
    if (gattManager1InterfaceProxy != NULL)
    {
        RegisterBSApplication(gattManager1InterfaceProxy, ctx);
    }
    g_object_unref(gattManager1InterfaceProxy);
}

static void HandleNameLostForBatt(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    printf("NameLost\n");
}

static gboolean HandleReadValueForBattLevel(
    BluezGattCharacteristic1 *interface,
    GDBusMethodInvocation *invocation,
    const GVariant *options,
    GVariant **value,
    gpointer user_data)
{
    // Send 53% battery for now
    const guint8 valueArray[] = {53};
    *value = g_variant_new_fixed_array(
        G_VARIANT_TYPE_BYTE, valueArray, G_N_ELEMENTS(valueArray), sizeof(valueArray[0]));

    g_variant_ref_sink(*value);

    return TRUE;
}

GDBusObjectManagerServer *CreateBSObjectManager(void)
{
    GDBusObjectManagerServer *om = g_dbus_object_manager_server_new("/io/mangoh/BatteryService");

    BluezObjectSkeleton *bos = bluez_object_skeleton_new("/io/mangoh/BatteryService/service0");
    BluezGattService1 *bgs = bluez_gatt_service1_skeleton_new();
    bluez_object_skeleton_set_gatt_service1(bos, bgs);
    bluez_gatt_service1_set_uuid(bgs, BLE_BATTERY_SERVICE_UUID);
    bluez_gatt_service1_set_primary(bgs, TRUE);
    g_object_unref(bgs);
    g_dbus_object_manager_server_export(om, G_DBUS_OBJECT_SKELETON(bos));
    g_object_unref(bos);

    bos = bluez_object_skeleton_new("/io/mangoh/BatteryService/service0/char0");
    BluezGattCharacteristic1 *bgc = bluez_gatt_characteristic1_skeleton_new();
    bluez_object_skeleton_set_gatt_characteristic1(bos, bgc);
    bluez_gatt_characteristic1_set_uuid(bgc, BLE_BATTERY_LEVEL_CHARACTERISTIC_UUID);
    const gchar *batteryLevelCharacteristicFlags[] = {
        "read",
        /*
         * TODO: Other flags required? It seems like notify would be useful, but
         * adds some work in terms of required methods.
         */
        NULL
    };
    bluez_gatt_characteristic1_set_flags(bgc, batteryLevelCharacteristicFlags);
    bluez_gatt_characteristic1_set_service(bgc, "/service0");
    g_signal_connect(bgc, "handle-read-value", G_CALLBACK(HandleReadValueForBattLevel), NULL);
    g_object_unref(bgc);
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
        printf("signal interface-added for interface %s\n", interfaceName);
        if (strcmp(interfaceName, "org.bluez.GattManager1") == 0) {
            RegisterBSApplication(interfaceProxy, ctx);
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
    printf(
        "signal interface-removed for interface %s\n",
        g_dbus_proxy_get_interface_name(interfaceProxy));
}

int main(int argc, char **argv)
{
    printf("Starting fake battery service!\n");

    struct BSContext ctx;
    ctx.mainLoop = g_main_loop_new(NULL, FALSE);
    GError *error = NULL;
    ctx.bluezObjectManager = bluez_object_manager_client_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
        "org.bluez",
        "/",
        NULL,
        &error);
    if (error != NULL) {
        printf("Error creating bluez object manager client: %s\n", error->message);
        exit(1);
    }
    ctx.bsObjectManager = CreateBSObjectManager();

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

    g_main_loop_run(ctx.mainLoop);

    g_bus_unown_name(id);

    return 0;
}
