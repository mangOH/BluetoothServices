#include <stdio.h>
#include <glib.h>
#include <gio/gio.h>

// dbus-monitor --system

struct BSContext {
    GMainLoop *mainLoop;
};

static void HandleBusAcquiredForBatt(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    printf("BusAcquired\n");
}

static void HandleNameAcquiredForBatt(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    printf("NameAcquired\n");
}

static void HandleNameLostForBatt(
    GDBusConnection *conn, const gchar *name, gpointer userData)
{
    struct BSContext *ctx = userData;
    printf("NameLost\n");
}


int main(int argc, char **argv)
{
    printf("Starting fake battery service!\n");

    struct BSContext ctx;
    ctx.mainLoop = g_main_loop_new(NULL, FALSE);

    guint id = g_bus_own_name(
        G_BUS_TYPE_SYSTEM,
        "io.mangoh.BatteryService",
        G_BUS_NAME_OWNER_FLAGS_NONE,
        HandleBusAcquiredForBatt,
        HandleNameAcquiredForBatt,
        HandleNameLostForBatt,
        &ctx,
        NULL);

    g_main_loop_run(ctx.mainLoop);

    g_bus_unown_name(id);

    return 0;
}
