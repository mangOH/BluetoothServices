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
#include "battery_service.h"
#include "immediate_alert.h"
#include "org.bluez.Adapter1.h"
#include "org.bluez.Device1.h"
#include "org.bluez.GattCharacteristic1.h"
#include "org.bluez.GattDescriptor1.h"
#include "org.bluez.GattManager1.h"
#include "org.bluez.GattService1.h"
#include "org.bluez.LEAdvertisement1.h"
#include "org.bluez.LEAdvertisingManager1.h"

#define BLUEZ_INTF_ADAPTER "org.bluez.Adapter1"
#define BLUEZ_INTF_GATT_MANAGER "org.bluez.GattManager1"
#define BLUEZ_INTF_LE_ADVERTISING_MANAGER "org.bluez.LEAdvertisingManager1"


enum BluezState
{
    BLUEZ_STATE_WAITING_FOR_NAME,
    BLUEZ_STATE_CREATING_OBJECT_MANAGER,
    BLUEZ_STATE_SEARCHING_FOR_ADAPTER,
    BLUEZ_STATE_POWERING_ON_ADAPTER,
    BLUEZ_STATE_ADAPTER_POWERED_ON,
};

enum ServicesState
{
    SERVICES_STATE_INIT,
    SERVICES_STATE_DEFINED_IN_OM,
    SERVICES_STATE_EXPORTED_AT_NAME,
    SERVICES_STATE_REGISTERING_APPLICAITON, // Depends on BLUEZ_STATE_ADAPTER_POWERED_ON
    SERVICES_STATE_REGISTERING_ADVERTISEMENT,
    SERVICES_STATE_RUNNING,
};

struct State
{
    enum BluezState bluezState;
    enum ServicesState servicesState;
    GDBusObjectManagerServer *servicesObjectManager;
    guint bluezWatchHandle;
    guint mangohOwnHandle;
    GDBusObjectManager *bluezObjectManager;
    BluezAdapter1 *adapter;
};


static void TryCreateBluezObjectManager(struct State *state);

static GType BluezProxyTypeFunc
(
    GDBusObjectManagerClient *manager,
    const gchar *objectPath,
    const gchar *interfaceName,
    gpointer userData
)
{
    LE_DEBUG("Handling request for objectPath=%s, interfaceName=%s", objectPath, interfaceName);
    if (interfaceName == NULL)
    {
        return g_dbus_object_proxy_get_type();
    }

    if (g_strcmp0(interfaceName, "org.bluez.Adapter1") == 0)
    {
        return BLUEZ_TYPE_ADAPTER1_PROXY;
    }
    else if (g_strcmp0(interfaceName, "org.bluez.Device1") == 0)
    {
        return BLUEZ_TYPE_DEVICE1_PROXY;
    }
    else if (g_strcmp0(interfaceName, "org.bluez.GattService1") == 0)
    {
        return BLUEZ_TYPE_GATT_SERVICE1_PROXY;
    }
    else if (g_strcmp0(interfaceName, "org.bluez.GattCharacteristic1") == 0)
    {
        return BLUEZ_TYPE_GATT_CHARACTERISTIC1_PROXY;
    }
    else if (g_strcmp0(interfaceName, "org.bluez.GattDescriptor1") == 0)
    {
        return BLUEZ_TYPE_GATT_DESCRIPTOR1_PROXY;
    }

    return g_dbus_proxy_get_type();
}

static void CreateAdvertisementObject(struct State *state)
{
    GDBusObjectSkeleton *obj_skel = g_dbus_object_skeleton_new("/io/mangoh/advertisement");
    BluezLEAdvertisement1 *adv_skel = bluez_leadvertisement1_skeleton_new();
    bluez_leadvertisement1_set_type_(adv_skel, "peripheral");
    bluez_leadvertisement1_set_local_name(adv_skel, "mangOH");
    const uint16_t no_timeout = 0; // Never timeout
    bluez_leadvertisement1_set_timeout(adv_skel, no_timeout);

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

    g_dbus_object_manager_server_export(
        state->servicesObjectManager, G_DBUS_OBJECT_SKELETON(obj_skel));
    g_object_unref(obj_skel);
}

static void AdvertisementRegisteredCallback(
    GObject *sourceObject, GAsyncResult *res, gpointer userData)
{
    struct State *state = userData;
    GError *error = NULL;
    bluez_leadvertising_manager1_call_register_advertisement_finish(
        BLUEZ_LEADVERTISING_MANAGER1(sourceObject), res, &error);
    LE_FATAL_IF(error, "Error registering bluetooth application: %s", error->message);

    LE_INFO("Advertising object registered");
    state->servicesState = SERVICES_STATE_RUNNING;
}

static void RegisterAdvertisement(struct State *state)
{
    state->servicesState = SERVICES_STATE_REGISTERING_ADVERTISEMENT;
    const char *adapterPath = g_dbus_proxy_get_object_path(G_DBUS_PROXY(state->adapter));
    GError *error = NULL;
    BluezLEAdvertisingManager1 *advMgr = bluez_leadvertising_manager1_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.bluez",
        adapterPath,
        NULL,
        &error);
    LE_FATAL_IF(error, "Couldn't access LE Advertising Manager: %s", error->message);

    GVariant *options = g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
    bluez_leadvertising_manager1_call_register_advertisement(
        advMgr, "/io/mangoh/advertisement", options, NULL, AdvertisementRegisteredCallback, state);

    g_object_unref(advMgr);
    g_print("Registered advertising object\n");
}

static void ApplicationRegisteredCallback(
    GObject *sourceObject, GAsyncResult *res, gpointer userData)
{
    struct State *state = userData;
    GError *error = NULL;
    bluez_gatt_manager1_call_register_application_finish(
        BLUEZ_GATT_MANAGER1(sourceObject), res, &error);

    LE_FATAL_IF(error, "Error registering bluetooth application: %s", error->message);
    LE_INFO("Registered bluetooth application");

    RegisterAdvertisement(state);
}

static void TryRegisterWithBluez(struct State *state)
{
    if (state->servicesState != SERVICES_STATE_EXPORTED_AT_NAME)
    {
        LE_INFO("Not registering with BlueZ because app is not yet on dbus");
        return;
    }

    if (state->bluezState != BLUEZ_STATE_ADAPTER_POWERED_ON)
    {
        LE_INFO("Not registering with BlueZ because the adapter is not powered on yet");
        return;
    }

    state->servicesState = SERVICES_STATE_REGISTERING_APPLICAITON;
    const char *adapterPath = g_dbus_proxy_get_object_path(G_DBUS_PROXY(state->adapter));
    GError *error = NULL;
    BluezGattManager1 *gattManager = bluez_gatt_manager1_proxy_new_for_bus_sync(
        G_BUS_TYPE_SYSTEM,
        G_DBUS_PROXY_FLAGS_NONE,
        "org.bluez",
        adapterPath,
        NULL,
        &error);
    LE_FATAL_IF(error, "Couldn't create GattManager1 - %s", error->message);
    bluez_gatt_manager1_call_register_application(
        gattManager,
        "/io/mangoh",
        g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0),
        NULL,
        ApplicationRegisteredCallback,
        state);

    g_object_unref(gattManager);
}

static void AdapterPoweredOnHandler(struct State *state)
{
    state->bluezState = BLUEZ_STATE_ADAPTER_POWERED_ON;
    TryRegisterWithBluez(state);
}

static void AdapterPropertiesChangedHandler(
    GDBusProxy *proxy, GVariant *changedProperties, GStrv invalidatedProperties, gpointer userData)
{
    struct State *state = userData;
    if (state->bluezState == BLUEZ_STATE_POWERING_ON_ADAPTER)
    {
        GVariant *poweredVal =
            g_variant_lookup_value(changedProperties, "Powered", G_VARIANT_TYPE_BOOLEAN);
        if (poweredVal != NULL)
        {
            gboolean powered = g_variant_get_boolean(poweredVal);
            g_variant_unref(poweredVal);
            LE_DEBUG("Adapter Powered property = %d", powered);
            if (powered)
            {
                AdapterPoweredOnHandler(state);
            }
        }
    }
}

static void AdapterFoundHandler(struct State *state)
{
    // Ensure the adapter is powered on
    if (!bluez_adapter1_get_powered(state->adapter))
    {
        state->bluezState = BLUEZ_STATE_POWERING_ON_ADAPTER;
        LE_DEBUG("Adapter not powered - powering on");
        g_signal_connect(
            state->adapter,
            "g-properties-changed",
            G_CALLBACK(AdapterPropertiesChangedHandler),
            state);
        bluez_adapter1_set_powered(state->adapter, TRUE);
    }
    else
    {
        AdapterPoweredOnHandler(state);
    }
}

static void SearchForAdapter(struct State *state)
{
    LE_DEBUG("Searching for adapter");
    GList *bluezObjects = g_dbus_object_manager_get_objects(state->bluezObjectManager);
    for (GList *node = bluezObjects; node != NULL && state->adapter == NULL; node = node->next)
    {
        GDBusObject *obj = node->data;
        state->adapter = BLUEZ_ADAPTER1(g_dbus_object_get_interface(obj, "org.bluez.Adapter1"));
    }
    g_list_free_full(bluezObjects, g_object_unref);

    if (state->adapter != NULL)
    {
        AdapterFoundHandler(state);
    }
}

static void BluezObjectAddedHandler
(
    GDBusObjectManager *manager,
    GDBusObject *object,
    gpointer userData
)
{
    LE_DEBUG(
        "Received \"object-added\" signal - object_path=%s", g_dbus_object_get_object_path(object));
    struct State *state = userData;

    if (state->bluezState == BLUEZ_STATE_SEARCHING_FOR_ADAPTER)
    {
        state->adapter = BLUEZ_ADAPTER1(g_dbus_object_get_interface(object, "org.bluez.Adapter1"));
        if (state->adapter != NULL)
        {
            AdapterFoundHandler(state);
        }
    }
}

static void BluezObjectRemovedHandler
(
    GDBusObjectManager *manager,
    GDBusObject *object,
    gpointer userData
)
{
    LE_DEBUG(
        "Received \"object-removed\" signal - object_path=%s",
        g_dbus_object_get_object_path(object));
}


static void MangohBusAcquiredCallback(GDBusConnection *conn, const gchar *name, gpointer userData)
{
    LE_DEBUG("io.mangoh bus acquired");
    struct State *state = userData;

    g_dbus_object_manager_server_set_connection(state->servicesObjectManager, conn);
    state->servicesState = SERVICES_STATE_EXPORTED_AT_NAME;

    TryRegisterWithBluez(state);
}

static void MangohNameAcquiredCallback(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    LE_DEBUG("io.mangoh name acquired: %s", name);
}

static void MangohNameLostCallback(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    LE_DEBUG("io.mangoh name lost: %s", name);
}

static void BluezObjectManagerCreateCallback(
    GObject *sourceObject,
    GAsyncResult *res,
    gpointer userData)
{
    GError *error = NULL;
    struct State *state = userData;
    state->bluezObjectManager = g_dbus_object_manager_client_new_for_bus_finish(res, &error);
    if (error != NULL)
    {
        LE_ERROR("Couldn't create Bluez object manager - %s", error->message);
        TryCreateBluezObjectManager(state);
    }
    else
    {
        state->bluezState = BLUEZ_STATE_SEARCHING_FOR_ADAPTER;
        g_signal_connect(
            state->bluezObjectManager, "object-added", G_CALLBACK(BluezObjectAddedHandler), state);
        g_signal_connect(
            state->bluezObjectManager,
            "object-removed",
            G_CALLBACK(BluezObjectRemovedHandler),
            state);

        SearchForAdapter(state);
    }

}

static void TryCreateBluezObjectManager(struct State *state)
{
    if (state->bluezState == BLUEZ_STATE_CREATING_OBJECT_MANAGER)
    {
        g_dbus_object_manager_client_new_for_bus(
            G_BUS_TYPE_SYSTEM,
            G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
            "org.bluez",
            "/",
            BluezProxyTypeFunc,
            NULL,
            NULL,
            NULL,
            BluezObjectManagerCreateCallback,
            state);
    }
    else
    {
        LE_WARN("Called %s while in unexpected state %d", __func__, state->bluezState);
    }
}


void BluezNameAppearedCallback
(
    GDBusConnection *connection,
    const gchar *name,
    const gchar *nameOwner,
    gpointer userData
)
{
    struct State *state = userData;

    LE_DEBUG("Received NameAppeared for name=%s, nameOwner=%s", name, nameOwner);
    LE_ASSERT(strcmp(name, "org.bluez") == 0);

    if (state->bluezState == BLUEZ_STATE_WAITING_FOR_NAME)
    {
        state->bluezState = BLUEZ_STATE_CREATING_OBJECT_MANAGER;
        TryCreateBluezObjectManager(state);
    }
    else
    {
        LE_WARN("org.bluez appeared while in unexpected state (%d)", state->bluezState);
    }
}


void BluezNameVanishedCallback
(
    GDBusConnection *connection,
    const gchar *name,
    gpointer userData
)
{
    LE_DEBUG("Received NameVanished for name=%s", name);
}


// Runs once immediately before the event glib event loop is run
void InitializeBluetoothServices(void)
{
    struct State *state = g_malloc0(sizeof(*state));
    state->bluezState = BLUEZ_STATE_WAITING_FOR_NAME;
    state->servicesState = SERVICES_STATE_INIT;

    size_t numServicesRegistered = 0;
    state->servicesObjectManager = g_dbus_object_manager_server_new("/io/mangoh");

    battery_register_services(state->servicesObjectManager, &numServicesRegistered);
    alert_register_services(state->servicesObjectManager, &numServicesRegistered);
    CreateAdvertisementObject(state);
    state->servicesState = SERVICES_STATE_DEFINED_IN_OM;

    state->mangohOwnHandle = g_bus_own_name(
        G_BUS_TYPE_SYSTEM,
        "io.mangoh",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        MangohBusAcquiredCallback,
        MangohNameAcquiredCallback,
        MangohNameLostCallback,
        state,
        NULL);

    state->bluezWatchHandle = g_bus_watch_name(
        G_BUS_TYPE_SYSTEM,
        "org.bluez",
        G_BUS_NAME_WATCHER_FLAGS_AUTO_START,
        BluezNameAppearedCallback,
        BluezNameVanishedCallback,
        state,
        NULL);
}
