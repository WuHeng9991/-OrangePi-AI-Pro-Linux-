#include <glib.h>

#include "pipeline.h"

static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

  new_pad_caps = gst_pad_get_current_caps(new_pad);
  new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
  new_pad_type = gst_structure_get_name(new_pad_struct);

  GstPad *vsink_pad = gst_element_get_static_pad(data->video_queue, "sink");
  if (g_str_has_prefix(new_pad_type, "video/x-raw")) {
    ret = gst_pad_link(new_pad, vsink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
      g_print("Type is '%s' but link failed.\n", new_pad_type);
      if (new_pad_caps != NULL) {
        gst_caps_unref(new_pad_caps);
      }
      gst_object_unref(vsink_pad);
    } else {
      g_print("Link succeeded (type '%s').\n", new_pad_type);
    }
  }

  GstPad *asink_pad = gst_element_get_static_pad(data->audio_queue, "sink");
  if (g_str_has_prefix(new_pad_type, "audio/x-raw")) {
    ret = gst_pad_link(new_pad, asink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
      g_print("Type is '%s' but link failed.\n", new_pad_type);
      if (new_pad_caps != NULL) {
        gst_caps_unref(new_pad_caps);
      }
      gst_object_unref(asink_pad);
    } else {
      g_print("Link succeeded (type '%s').\n", new_pad_type);
    }
  }
}

int pipeline_create(CustomData *data) {
  data->source = gst_element_factory_make("uridecodebin", "source");
  data->aconvert = gst_element_factory_make("audioconvert", "audio-convert");
  data->resample = gst_element_factory_make("audioresample", "resample");
  data->audio_queue = gst_element_factory_make("queue", "audio_queue");
  data->video_queue = gst_element_factory_make("queue", "video_queue");
  data->asink = gst_element_factory_make("autoaudiosink", "audio-sink");
  data->vconvert = gst_element_factory_make("videoconvert", "video-convert");
  data->videosink = gst_element_factory_make("glsinkbin", "glsinkbin");
  data->gtkglsink = gst_element_factory_make("gtkglsink", "gtkglsink");
  data->pipeline = gst_pipeline_new("open-audio-video-pipeline");

  if (!data->pipeline || !data->videosink || !data->source || !data->aconvert ||
      !data->audio_queue || !data->video_queue || !data->vconvert || !data->resample ||
      !data->asink || !data->gtkglsink) {
    g_printerr("Not all elements could be created.\n");
    return -1;
  }

  return 0;
}

void pipeline_set_source_uri(CustomData *data, const gchar *uri) {
  g_object_set(data->source, "uri", uri, NULL);
}

int pipeline_setup_video_sink(CustomData *data) {
#ifdef HWACC_ENABLED
  if (data->gtkglsink != NULL && data->videosink != NULL) {
    g_printerr("Successfully created GTK GL Sink \n");
    g_object_set(data->videosink, "sink", data->gtkglsink, NULL);
    g_object_get(data->gtkglsink, "widget", &data->sink_widget, NULL);
  } else {
    return -1;
  }
#else
  g_printerr("Could not create gtkglsink, falling back to gtksink.\n");
  data->videosink = gst_element_factory_make("gtksink", "gtksink");
  g_object_get(data->videosink, "widget", &data->sink_widget, NULL);
#endif

  return 0;
}

int pipeline_build(CustomData *data) {
  gst_bin_add_many(GST_BIN(data->pipeline), data->source, data->audio_queue, data->video_queue,
      data->aconvert, data->resample, data->asink, data->vconvert, data->videosink, NULL);

  if (!gst_element_link_many(data->audio_queue, data->aconvert, data->resample, data->asink, NULL)) {
    g_printerr("Elements could not be linked on audio brach.\n");
    return -1;
  }

  if (!gst_element_link_many(data->video_queue, data->vconvert, data->videosink, NULL)) {
    g_printerr("Elements could not be linked on video brach.\n");
    return -1;
  }

  g_signal_connect(data->source, "pad-added", G_CALLBACK(pad_added_handler), data);
  return 0;
}

GstBus *pipeline_get_bus(CustomData *data) {
  return gst_element_get_bus(data->pipeline);
}

void pipeline_handle_message(CustomData *data, GstMessage *msg) {
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error(msg, &err, &debug_info);
      g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
      g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
      app_set_status(data, err->message);
      data->playing = FALSE;
      g_clear_error(&err);
      g_free(debug_info);
      break;
    case GST_MESSAGE_EOS:
      g_print("\nEnd-Of-Stream reached.\n");
      data->playing = FALSE;
      data->position = data->duration;
      app_set_status(data, "播放结束");
      break;
    case GST_MESSAGE_DURATION:
      data->duration = GST_CLOCK_TIME_NONE;
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipeline)) {
        GstQuery *query;
        gint64 start;
        gint64 end;

        g_print("Pipeline state changed from %s to %s:\n",
            gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

        data->playing = (new_state == GST_STATE_PLAYING);

        query = gst_query_new_seeking(GST_FORMAT_TIME);
        if (gst_element_query(data->pipeline, query)) {
          gst_query_parse_seeking(query, NULL, &data->seek_enabled, &start, &end);
          if (data->seek_enabled) {
            g_print("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
                GST_TIME_ARGS(start), GST_TIME_ARGS(end));
          } else {
            g_print("Seeking is DISABLED for this stream.\n");
          }
        } else {
          g_printerr("Seeking query failed.");
        }
        gst_query_unref(query);

        if (new_state == GST_STATE_PLAYING) {
          app_set_status(data, "播放中");
        } else if (new_state == GST_STATE_PAUSED && data->has_media) {
          app_set_status(data, "已暂停");
        } else if (new_state == GST_STATE_READY && data->has_media) {
          app_set_status(data, "已停止");
        }
      }
    } break;
    default:
      g_printerr("Unexpected message received.\n");
      break;
  }
  gst_message_unref(msg);
}

void pipeline_cleanup(CustomData *data) {
  if (data->current_uri != NULL) {
    g_free(data->current_uri);
    data->current_uri = NULL;
  }

  if (data->pipeline != NULL) {
    gst_element_set_state(data->pipeline, GST_STATE_NULL);
    gst_object_unref(data->pipeline);
    data->pipeline = NULL;
  }
}
