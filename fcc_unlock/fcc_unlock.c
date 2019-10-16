#include <glib.h>
#include <gio/gio.h>

#include <libmbim-glib.h>

static GMainLoop *loop;
static GCancellable *cancellable;
static MbimDevice *device;


static void
device_close_ready (MbimDevice   *dev,
                    GAsyncResult *res)
{
    GError *error = NULL;

    if (!mbim_device_close_finish (dev, res, &error)) {
        g_printerr ("error: couldn't close device: %s", error->message);
        g_error_free (error);
    } else
        g_debug ("Device closed");

    g_main_loop_quit (loop);
}


void
async_operation_done (gboolean reported_operation_status)
{
    /* Cleanup cancellation */
    g_clear_object (&cancellable);

    /* Close the device */
    mbim_device_close (device,
                       15,
                       cancellable,
                       (GAsyncReadyCallback) device_close_ready,
                       NULL);
}

static void
device_open_ready (MbimDevice   *dev,
                   GAsyncResult *res)
{
    GError *error = NULL;

    if (!mbim_device_open_finish (dev, res, &error)) {
        g_printerr ("error: couldn't open the MbimDevice: %s\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    g_debug ("MBIM Device at '%s' ready",
             mbim_device_get_path_display (dev));

    return;
}

static void
device_new_ready (GObject      *unused,
                  GAsyncResult *res)
{
    GError *error = NULL;
    MbimDeviceOpenFlags open_flags = MBIM_DEVICE_OPEN_FLAGS_NONE;

    device = mbim_device_new_finish (res, &error);
    if (!device) {
        g_printerr ("error: couldn't create MbimDevice: %s\n",
                    error->message);
        exit (EXIT_FAILURE);
    }

    /* Open the device */
    mbim_device_open_full (device,
                           open_flags,
                           30,
                           cancellable,
                           (GAsyncReadyCallback) device_open_ready,
                           NULL);
}


int main(int argc, char **argv) {
    GFile *file;
    cancellable = g_cancellable_new ();
    loop = g_main_loop_new (NULL, FALSE);
    file = g_file_new_for_path ("/dev/cdc-wdm0");
    mbim_device_new (file,
                     cancellable,
                     (GAsyncReadyCallback)device_new_ready,
                     NULL);
    g_main_loop_run (loop);

    if (cancellable)
        g_object_unref (cancellable);
    if (device)
        g_object_unref (device);
    g_main_loop_unref (loop);
    g_object_unref (file);
    return 0;
}
