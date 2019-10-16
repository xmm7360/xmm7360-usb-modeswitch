#include <glib.h>
#include <gio/gio.h>

#include <libmbim-glib.h>
#include "sha-256.c"

static GMainLoop *loop;
static GCancellable *cancellable;
static MbimDevice *device;
static void
device_open_ready (MbimDevice   *dev,
                   GAsyncResult *res);

// XXX TODO: send the QUERY to get lock state/mode

MbimMessage *
mbim_message_fcc_unlock_new (MbimDevice *dev, int type, guint32 is_response, guint32 response_value)
{
    MbimMessage *msg = mbim_message_command_new(
                                mbim_device_get_next_transaction_id(dev),
                                MBIM_SERVICE_DSS,   // we will clobber this later
                                1,
                                type);

    // there is no way to set custom UUIDs through the libmbim-glib API?
    void *uuid = (void*)mbim_message_command_get_service_id(msg);
    memcpy(uuid,"\xf8\x5d\x46\xef\xab\x26\x40\x81\x98\x68\x4d\x18\x3c\x0a\x3a\xec", 16);

    mbim_message_command_append(msg, &is_response, 4);
    mbim_message_command_append(msg, &response_value, 4);
    return msg;
}

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
radio_query (MbimDevice   *device,
                         GAsyncResult *res)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimDeviceType device_type;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_printerr ("error: operation failed: %s\n", error->message);
        g_error_free (error);
        if (response)
            mbim_message_unref (response);
        async_operation_done (FALSE);
        return;
    }

    MbimRadioSwitchState hw, sw;
    mbim_message_radio_state_response_parse(response, &hw, &sw, NULL);
    g_printf("switch states: hw %d, sw %d\n", hw, sw);
}
static void
radio_on (MbimDevice   *device,
                         GAsyncResult *res)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimDeviceType device_type;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_printerr ("error: operation failed: %s\n", error->message);
        g_error_free (error);
        if (response)
            mbim_message_unref (response);
        async_operation_done (FALSE);
        return;
    }
    g_printf("successss\n");
    mbim_message_unref (response);

    MbimMessage *request = mbim_message_radio_state_query_new (NULL);
    mbim_device_command (device,
                         request,
                         10,
                         cancellable,
                         (GAsyncReadyCallback)radio_query,
                         NULL);
    mbim_message_unref (request);
}

static void
got_response (MbimDevice   *device,
                         GAsyncResult *res)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimDeviceType device_type;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_printerr ("error: operation failed: %s\n", error->message);
        g_error_free (error);
        if (response)
            mbim_message_unref (response);
        async_operation_done (FALSE);
        return;
    }
    guint32 length;
    const guint8 *buf = mbim_message_command_done_get_raw_information_buffer(response, &length);
    if (buf) {
        for (int i=0; i<length; i++)
            g_printf(" %02x", buf[i]);
        g_printf("\n");
    }
    g_printf("success\n");

    sleep(2);

    MbimMessage *request = mbim_message_radio_state_set_new (MBIM_RADIO_SWITCH_STATE_ON, NULL);
    mbim_device_command (device,
                         request,
                         10,
                         cancellable,
                         (GAsyncReadyCallback)radio_on,
                         NULL);
    mbim_message_unref (request);

    mbim_message_unref (response);
}

static void
got_challenge (MbimDevice   *device,
                         GAsyncResult *res)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimDeviceType device_type;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_printerr ("error: operation failed: %s\n", error->message);
        g_error_free (error);
        if (response)
            mbim_message_unref (response);
        async_operation_done (FALSE);
        return;
    }

    guint32 length;
    const guint8 *buf = mbim_message_command_done_get_raw_information_buffer(response, &length);
    if (!buf || length != 8) {
        g_printerr ("error: got no challenge from device\n");
        async_operation_done (FALSE);
        return;
    }

    guint32 state = *(guint32*)buf;
    guint32 challenge = *(guint32*)(buf+4);

    g_printf("lock challenge present: %x challenge: %x\n", state, challenge);
    if (state != 1)
        return;

    guint8 resp[8];

    guint8 hash[32];

    calc_sha_256(hash, "KHOIHGIUCCHHII", 14);
    *(guint32*)(resp) = challenge;
    memcpy(resp+4, hash, 4);
    g_printf("resp: ");
    for (int i=0; i<8; i++)
        g_printf("%02x ", resp[i]);
    g_printf("\n");
    calc_sha_256(hash, resp, 8);

    MbimMessage *msg = mbim_message_fcc_unlock_new(device, MBIM_MESSAGE_COMMAND_TYPE_SET, 1, *(guint32*)hash);
    mbim_device_command(device, msg, 10, cancellable, (GAsyncReadyCallback)got_response, NULL);
    mbim_message_unref(msg);

    mbim_message_unref (response);
}

static void request_challenge(MbimDevice *device) {
}

static void
got_lock_state (MbimDevice   *device,
                         GAsyncResult *res)
{
    MbimMessage *response;
    GError *error = NULL;
    MbimDeviceType device_type;

    response = mbim_device_command_finish (device, res, &error);
    if (!response || !mbim_message_response_get_result (response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error)) {
        g_printerr ("error: operation failed: %s\n", error->message);
        g_error_free (error);
        if (response)
            mbim_message_unref (response);
        async_operation_done (FALSE);
        return;
    }

    guint32 length;
    const guint8 *buf = mbim_message_command_done_get_raw_information_buffer(response, &length);
    if (!buf || length != 8) {
        g_printerr ("error: got no lock status from device\n");
        async_operation_done (FALSE);
        return;
    }

    guint32 mode = *(guint32*)buf;
    guint32 state = *(guint32*)(buf+4);

    g_printf("lock mode: %x state: %x\n", mode, state);
    /* if (state){ */
    /*     g_printf("Unlocked, doing nothing\n"); */
    /*     return ; */
    /* } */

    MbimMessage *msg = mbim_message_fcc_unlock_new(device, MBIM_MESSAGE_COMMAND_TYPE_SET, 0, 0);
    mbim_device_command(device, msg, 10, cancellable, (GAsyncReadyCallback)got_challenge, NULL);
    mbim_message_unref(msg);

    mbim_message_unref (response);
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

    MbimMessage *msg = mbim_message_fcc_unlock_new(dev, MBIM_MESSAGE_COMMAND_TYPE_QUERY, 0, 0);
    mbim_device_command(dev, msg, 10, cancellable, (GAsyncReadyCallback)got_lock_state, NULL);
    mbim_message_unref(msg);
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
    mbim_utils_set_traces_enabled(TRUE);
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
