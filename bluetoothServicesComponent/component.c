#include "legato.h"
#include "interfaces.h"
#include "primary.h"
#include <glib.h>


static gboolean LegatoFdHandler(GIOChannel *source, GIOCondition condition, gpointer data)
{
    while (true)
    {
        le_result_t r = le_event_ServiceLoop();
        if (r == LE_WOULD_BLOCK)
        {
            // All of the work is done, so break out
            break;
        }
        LE_ASSERT_OK(r);
    }

    return TRUE;
}


static void GlibInit(void *deferredArg1, void *deferredArg2)
{
    GMainLoop *glibMainLoop = g_main_loop_new(NULL, FALSE);

    int legatoEventLoopFd = le_event_GetFd();
    GIOChannel *channel = g_io_channel_unix_new(legatoEventLoopFd);
    gpointer userData = NULL;
    g_io_add_watch(channel, G_IO_IN, LegatoFdHandler, userData);

    InitializeBluetoothServices();

    g_main_loop_run(glibMainLoop);

    LE_ERROR("GLib main loop has returned");
    exit(1);
}


COMPONENT_INIT
{
    le_event_QueueFunction(GlibInit, NULL, NULL);
}
