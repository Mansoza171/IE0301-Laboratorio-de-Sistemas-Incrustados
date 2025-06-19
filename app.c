#include <gst/gst.h>
#include <glib.h>

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *) data;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End of stream\n");
            g_main_loop_quit(loop);
            break;

        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;

            gst_message_parse_error(msg, &error, &debug);
            g_free(debug);

            g_printerr("Error: %s\n", error->message);
            g_error_free(error);

            g_main_loop_quit(loop);
            break;
        }
        default:
            break;
    }

    return TRUE;
}

int main(int argc, char *argv[]) {
    GMainLoop *loop;

    GstElement *pipeline, *source, *capsfilter, *encoder, *parser, *payloader, *sink;
    GstBus *bus;
    GstCaps *caps;
    guint bus_watch_id;

    /* Inicialización */
    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    /* Crear elementos del pipeline */
    pipeline   = gst_pipeline_new("video-streamer");
    source     = gst_element_factory_make("nvarguscamerasrc", "camera-source");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    encoder    = gst_element_factory_make("nvv4l2h264enc", "h264-encoder");
    parser     = gst_element_factory_make("h264parse", "h264-parser");
    payloader  = gst_element_factory_make("rtph264pay", "rtp-payloader");
    sink       = gst_element_factory_make("udpsink", "udp-sink");

    if (!pipeline || !source || !capsfilter || !encoder || !parser || !payloader || !sink) {
        g_printerr("No se pudo crear uno o más elementos.\n");
        return -1;
    }

    /* Configurar elementos */
    g_object_set(encoder, "insert-sps-pps", TRUE, NULL);
    g_object_set(payloader, "pt", 96, NULL);
    g_object_set(sink, "host", "127.0.0.1", "port", 8001, "sync", FALSE, NULL);

    /* Establecer capacidades */
    caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12, width=1920, height=1080");
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Agregar elementos al pipeline */
    gst_bin_add_many(GST_BIN(pipeline), source, capsfilter, encoder, parser, payloader, sink, NULL);

    if (!gst_element_link_many(source, capsfilter, encoder, parser, payloader, sink, NULL)) {
        g_printerr("Error al enlazar los elementos del pipeline.\n");
        gst_object_unref(pipeline);
        return -1;
    }

    /* Configurar manejo de mensajes del bus */
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    /* Iniciar reproducción */
    g_print("Iniciando streaming de video...\n");
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    /* Ejecutar bucle principal */
    g_main_loop_run(loop);

    /* Finalizar y liberar recursos */
    g_print("Deteniendo...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}
