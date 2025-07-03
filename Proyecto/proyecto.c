#include <gst/gst.h>
#include <glib.h>

static void on_pad_added(GstElement *src, GstPad *new_pad, gpointer data) {
    GstElement *parser = (GstElement *)data;
    GstPad *sink_pad = gst_element_get_static_pad(parser, "sink");

    if (!gst_pad_is_linked(sink_pad)) {
        if (gst_pad_link(new_pad, sink_pad) != GST_PAD_LINK_OK) {
            g_printerr("No se pudo enlazar el pad del demuxer al parser\n");
        }
    }

    gst_object_unref(sink_pad);
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("Fin del stream\n");
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
    GstElement *pipeline, *source, *demuxer, *parser, *decoder, *queue1;
    GstElement *streammux, *queue2, *pgie, *queue3, *tracker, *queue4, *sgie, *queue5;
    GstElement *nvvidconv1, *nvdsosd, *nvvidconv2, *capsfilter;
    GstElement *encoder, *h264parse, *rtppay, *udpsink;
    GstCaps *caps;
    GstBus *bus;
    guint bus_watch_id;

    gst_init(&argc, &argv);
    loop = g_main_loop_new(NULL, FALSE);

    pipeline = gst_pipeline_new("deepstream-pipeline");

    source      = gst_element_factory_make("filesrc", "file-source");
    demuxer     = gst_element_factory_make("qtdemux", "qt-demuxer");
    parser      = gst_element_factory_make("h264parse", "h264-parser");
    decoder     = gst_element_factory_make("nvv4l2decoder", "h264-decoder");
    queue1      = gst_element_factory_make("queue", "queue1");

    streammux   = gst_element_factory_make("nvstreammux", "stream-muxer");
    queue2      = gst_element_factory_make("queue", "queue2");
    pgie        = gst_element_factory_make("nvinfer", "primary-infer");
    queue3      = gst_element_factory_make("queue", "queue3");
    tracker     = gst_element_factory_make("nvtracker", "tracker");
    queue4      = gst_element_factory_make("queue", "queue4");
    sgie        = gst_element_factory_make("nvinfer", "secondary-infer");
    queue5      = gst_element_factory_make("queue", "queue5");
    nvvidconv1  = gst_element_factory_make("nvvideoconvert", "nvvidconv1");
    nvdsosd     = gst_element_factory_make("nvdsosd", "onscreendisplay");
    nvvidconv2  = gst_element_factory_make("nvvideoconvert", "nvvidconv2");
    capsfilter  = gst_element_factory_make("capsfilter", "capsfilter");
    encoder     = gst_element_factory_make("nvv4l2h264enc", "h264-encoder");
    h264parse   = gst_element_factory_make("h264parse", "h264-parser2");
    rtppay      = gst_element_factory_make("rtph264pay", "rtp-payload");
    udpsink     = gst_element_factory_make("udpsink", "udp-sink");

    if (!pipeline || !source || !demuxer || !parser || !decoder || !queue1 || !streammux ||
        !pgie || !tracker || !sgie || !nvvidconv1 || !nvdsosd || !nvvidconv2 || !capsfilter ||
        !encoder || !h264parse || !rtppay || !udpsink || !queue2 || !queue3 || !queue4 || !queue5) {
        g_printerr("No se pudieron crear todos los elementos del pipeline.\n");
        return -1;
    }

    /* Configuraciones */
    g_object_set(G_OBJECT(source), "location", "/opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4", NULL);
    g_object_set(G_OBJECT(streammux), "width", 1920, "height", 1080, "batch-size", 1, "batched-push-timeout", 4000000, NULL);
    g_object_set(G_OBJECT(pgie),
                 "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_primary.txt",
                 "model-engine-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Primary_Detector/resnet10.caffemodel_b1_gpu0_fp16.engine",
                 NULL);
    g_object_set(G_OBJECT(tracker),
                 "tracker-width", 640, "tracker-height", 368,
                 "ll-lib-file", "/opt/nvidia/deepstream/deepstream-6.0/lib/libnvds_nvmultiobjecttracker.so",
                 "ll-config-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_tracker_IOU.yml",
                 "enable-batch-process", 1, NULL);
    g_object_set(G_OBJECT(sgie),
                 "config-file-path", "/opt/nvidia/deepstream/deepstream-6.0/samples/configs/deepstream-app/config_infer_secondary_carmake.txt",
                 "model-engine-file", "/opt/nvidia/deepstream/deepstream-6.0/samples/models/Secondary_CarMake/resnet18.caffemodel_b16_gpu0_fp16.engine",
                 NULL);
    g_object_set(G_OBJECT(nvdsosd), "process-mode", 0, NULL); // HW_MODE
    g_object_set(G_OBJECT(encoder), "insert-sps-pps", TRUE, NULL);
    g_object_set(G_OBJECT(rtppay), "pt", 96, NULL);
    g_object_set(G_OBJECT(udpsink), "host", "192.168.1.147", "port", 5001, "sync", FALSE, NULL);
    caps = gst_caps_from_string("video/x-raw(memory:NVMM), format=NV12");
    g_object_set(G_OBJECT(capsfilter), "caps", caps, NULL);
    gst_caps_unref(caps);

    /* Construcción del pipeline */
    gst_bin_add_many(GST_BIN(pipeline),
        source, demuxer, parser, decoder, queue1,
        streammux, queue2, pgie, queue3, tracker, queue4, sgie, queue5,
        nvvidconv1, nvdsosd, nvvidconv2, capsfilter,
        encoder, h264parse, rtppay, udpsink, NULL);

    gst_element_link(source, demuxer);
    g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_pad_added), parser);
    gst_element_link_many(parser, decoder, queue1, NULL);

    /* Vincular decoder -> streammux */
    GstPad *sinkpad, *srcpad;
    sinkpad = gst_element_get_request_pad(streammux, "sink_0");
    srcpad = gst_element_get_static_pad(queue1, "src");
    if (gst_pad_link(srcpad, sinkpad) != GST_PAD_LINK_OK) {
        g_printerr("Error al enlazar decoder con streammux\n");
        return -1;
    }
    gst_object_unref(sinkpad);
    gst_object_unref(srcpad);

    if (!gst_element_link_many(streammux, queue2, pgie, queue3, tracker, queue4, sgie, queue5, nvvidconv1,
                               nvdsosd, nvvidconv2, capsfilter, encoder, h264parse, rtppay, udpsink, NULL)) {
        g_printerr("Error al enlazar el resto del pipeline\n");
        return -1;
    }

    /* Bus */
    bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    /* Ejecutar */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Pipeline ejecutándose...\n");
    g_main_loop_run(loop);

    /* Finalizar */
    g_print("Finalizando...\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(GST_OBJECT(pipeline));
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);

    return 0;
}

