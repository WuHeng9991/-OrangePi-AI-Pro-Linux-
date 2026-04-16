#include <glib.h>

#include "pipeline.h"
#include "player.h"

#ifdef HWACC_ENABLED
static gboolean prefer_gtksink_video_output(void) {
  const gchar *value;

  value = g_getenv("OPMP_PREFER_GTKSINK");
  if (value == NULL || value[0] == '\0') {
    return FALSE;
  }

  return g_ascii_strcasecmp(value, "1") == 0 ||
      g_ascii_strcasecmp(value, "true") == 0 ||
      g_ascii_strcasecmp(value, "yes") == 0 ||
      g_ascii_strcasecmp(value, "on") == 0;
}
#endif

static const gchar *friendly_error_status(const gchar *source_name, const GError *err) {
  if (err == NULL || err->message == NULL) {
    return "播放失败";
  }

  if (source_name != NULL && g_strcmp0(source_name, "source") == 0) {
    return "媒体无法播放";
  }

  if (g_strrstr(err->message, "No such file") != NULL ||
      g_strrstr(err->message, "not found") != NULL) {
    return "找不到媒体文件";
  }

  if (g_strrstr(err->message, "Permission denied") != NULL) {
    return "没有权限访问该媒体";
  }

  if (g_strrstr(err->message, "Could not open") != NULL ||
      g_strrstr(err->message, "Failed to open") != NULL) {
    return "打开媒体失败";
  }

  return "播放失败";
}

static GstElement *create_audio_sink(CustomData *data) {
  GstElement *audio_sink;

  if (data->audio_sink_name == NULL || data->audio_sink_name[0] == '\0') {
    audio_sink = gst_element_factory_make("autoaudiosink", "audio-sink");
    if (audio_sink == NULL) {
      g_printerr("Failed to create default autoaudiosink.\n");
    }
    return audio_sink;
  }

  audio_sink = gst_element_factory_make(data->audio_sink_name, "audio-sink");
  if (audio_sink == NULL) {
    g_printerr("Failed to create audio sink '%s', falling back to autoaudiosink.\n",
        data->audio_sink_name);
    audio_sink = gst_element_factory_make("autoaudiosink", "audio-sink");
    if (audio_sink == NULL) {
      g_printerr("Failed to create fallback autoaudiosink.\n");
    }
    return audio_sink;
  }

  if (data->audio_device != NULL && data->audio_device[0] != '\0') {
    GObjectClass *klass;

    klass = G_OBJECT_GET_CLASS(audio_sink);
    if (g_object_class_find_property(klass, "device") != NULL) {
      g_object_set(audio_sink, "device", data->audio_device, NULL);
      g_print("Using audio sink '%s' with device '%s'.\n",
          data->audio_sink_name, data->audio_device);
    } else {
      g_printerr("Audio sink '%s' does not support device property, ignoring '%s'.\n",
          data->audio_sink_name, data->audio_device);
    }
  } else {
    g_print("Using audio sink '%s'.\n", data->audio_sink_name);
  }

  return audio_sink;
}

static int setup_gtksink_fallback(CustomData *data, const gchar *reason) {
  if (reason != NULL && reason[0] != '\0') {
    g_printerr("%s\n", reason);
  }

  if (data->videosink != NULL) {
    gst_object_unref(data->videosink);
    data->videosink = NULL;
  }

  if (data->gtkglsink != NULL) {
    gst_object_unref(data->gtkglsink);
    data->gtkglsink = NULL;
  }

  data->videosink = gst_element_factory_make("gtksink", "gtksink");
  if (data->videosink == NULL) {
    g_printerr("Failed to create gtksink fallback.\n");
    return -1;
  }

  g_object_get(data->videosink, "widget", &data->sink_widget, NULL);
  if (data->sink_widget == NULL) {
    g_printerr("Failed to get widget from gtksink.\n");
    return -1;
  }

  g_print("Using gtksink video output.\n");
  return 0;
}

int pipeline_create(CustomData *data) {
  data->pipeline = gst_element_factory_make("playbin", "player");
  data->asink = create_audio_sink(data);
  data->videosink = gst_element_factory_make("glsinkbin", "glsinkbin");
  data->gtkglsink = gst_element_factory_make("gtkglsink", "gtkglsink");

  if (!data->pipeline || !data->asink) {
    g_printerr("Not all elements could be created.\n");
    return -1;
  }

  app_debug_log(data, "PIPELINE", "使用 playbin 作为底层播放管线");

  return 0;
}

void pipeline_set_source_uri(CustomData *data, const gchar *uri) {
  g_object_set(data->pipeline, "uri", uri, NULL);
}

int pipeline_setup_video_sink(CustomData *data) {
#ifdef HWACC_ENABLED
  if (prefer_gtksink_video_output()) {
    return setup_gtksink_fallback(data,
        "OPMP_PREFER_GTKSINK enabled, using gtksink video output.");
  }

  if (data->gtkglsink == NULL || data->videosink == NULL) {
    return setup_gtksink_fallback(data,
        "Failed to create gtkglsink/glsinkbin, falling back to gtksink.");
  }

  g_object_set(data->videosink, "sink", data->gtkglsink, NULL);
  g_object_get(data->gtkglsink, "widget", &data->sink_widget, NULL);
  if (data->sink_widget == NULL) {
    return setup_gtksink_fallback(data,
        "Failed to get widget from gtkglsink, falling back to gtksink.");
  }

  g_print("Using gtkglsink + glsinkbin video output.\n");
  return 0;
#else
  return setup_gtksink_fallback(data,
      "HWACC disabled, using gtksink video output.");
#endif
}

int pipeline_build(CustomData *data) {
  g_object_set(data->pipeline,
      "audio-sink", data->asink,
      "video-sink", data->videosink,
      NULL);
  app_debug_log(data, "PIPELINE", "playbin 已绑定音视频输出");
  return 0;
}

GstBus *pipeline_get_bus(CustomData *data) {
  return gst_element_get_bus(data->pipeline);
}

void pipeline_handle_message(CustomData *data, GstMessage *msg) {
  GError *err;
  gchar *debug_info;

  err = NULL;
  debug_info = NULL;

  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
      const gchar *src_name;
      const gchar *status;

      gst_message_parse_error(msg, &err, &debug_info);
      src_name = GST_OBJECT_NAME(msg->src);
      app_debug_log(data, "BUS", "收到 ERROR：src=%s err=%s debug=%s",
          src_name,
          err != NULL ? err->message : "none",
          debug_info != NULL ? debug_info : "none");
      g_printerr("Error received from element %s: %s\n", src_name, err->message);
      g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
      data->playing = FALSE;
      data->is_seeking = FALSE;
      data->seek_done = TRUE;
      data->seek_resume_playback = FALSE;
      data->last_seek_request_time_us = 0;
      status = friendly_error_status(src_name, err);
      app_set_status(data, status);
      g_clear_error(&err);
      g_free(debug_info);
      break;
    }
    case GST_MESSAGE_EOS:
      app_debug_log(data, "BUS", "收到 EOS");
      g_print("\nEnd-Of-Stream reached.\n");
      data->playing = FALSE;
      data->is_seeking = FALSE;
      data->seek_done = TRUE;
      data->seek_resume_playback = FALSE;
      data->last_seek_request_time_us = 0;
      data->position = data->duration;
      app_handle_eos(data);
      break;
    case GST_MESSAGE_DURATION_CHANGED:
      app_debug_log(data, "BUS", "收到 DURATION_CHANGED");
      data->duration = GST_CLOCK_TIME_NONE;
      break;
    case GST_MESSAGE_CLOCK_LOST: {
      GstStateChangeReturn ret;

      app_debug_log(data, "BUS", "收到 CLOCK_LOST，准备重新获取时钟");
      ret = gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
      app_debug_log(data, "BUS", "CLOCK_LOST 时切到 PAUSED，返回=%d", ret);

      if (data->seek_resume_playback) {
        ret = gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
        app_debug_log(data, "BUS", "CLOCK_LOST 后恢复到 PLAYING，返回=%d", ret);
      } else {
        app_debug_log(data, "BUS", "CLOCK_LOST 后保持非播放状态");
      }
      break;
    }
    case GST_MESSAGE_NEW_CLOCK: {
      GstClock *clock;

      clock = NULL;
      gst_message_parse_new_clock(msg, &clock);
      app_debug_log(data, "BUS", "收到 NEW_CLOCK，clock=%s",
          clock != NULL ? GST_OBJECT_NAME(clock) : "none");
      if (clock != NULL) {
        gst_object_unref(clock);
      }
      break;
    }
    case GST_MESSAGE_LATENCY: {
      gboolean latency_ok;

      latency_ok = gst_bin_recalculate_latency(GST_BIN(data->pipeline));
      app_debug_log(data, "BUS", "收到 LATENCY，重新计算延迟结果=%d", latency_ok);
      break;
    }
    case GST_MESSAGE_ASYNC_DONE: {
      gint64 current;
      gint64 duration;

      if (GST_MESSAGE_SRC(msg) != GST_OBJECT(data->pipeline)) {
        break;
      }

      current = -1;
      duration = GST_CLOCK_TIME_NONE;
      if (player_query_position(data, &current)) {
        data->position = current;
      }
      if (player_query_duration(data, &duration) && GST_CLOCK_TIME_IS_VALID(duration)) {
        data->duration = duration;
      }
      data->is_seeking = FALSE;
      data->seek_done = TRUE;
      data->last_progress_position = data->position;
      data->last_progress_observed_time_us = g_get_monotonic_time();
      data->playback_stalled = FALSE;
      app_debug_log(data, "BUS", "收到 ASYNC_DONE，当前查询位置=%" GST_TIME_FORMAT " 时长=%" GST_TIME_FORMAT,
          GST_TIME_ARGS((GstClockTime)((GST_CLOCK_TIME_IS_VALID(data->position) && data->position >= 0) ? data->position : 0)),
          GST_TIME_ARGS((GstClockTime)((GST_CLOCK_TIME_IS_VALID(data->duration) && data->duration >= 0) ? data->duration : 0)));
      if (data->seek_resume_playback) {
        app_set_status(data, "播放中");
      } else if (data->has_media) {
        app_set_status(data, "已暂停");
      }
      break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state;
      GstState new_state;
      GstState pending_state;
      GstQuery *query;
      gint64 start;
      gint64 end;

      if (GST_MESSAGE_SRC(msg) != GST_OBJECT(data->pipeline)) {
        break;
      }

      gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
      g_print("Pipeline state changed from %s to %s:\n",
          gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));

      if (new_state == GST_STATE_PLAYING) {
        data->playing = TRUE;
      } else if (new_state == GST_STATE_PAUSED) {
        data->playing = (pending_state == GST_STATE_PLAYING) ||
            (data->is_seeking && data->seek_resume_playback);
      } else {
        data->playing = FALSE;
      }

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
        g_printerr("Seeking query failed.\n");
      }
      gst_query_unref(query);

      if (new_state == GST_STATE_READY) {
        data->is_seeking = FALSE;
        data->seek_done = TRUE;
        data->seek_resume_playback = FALSE;
        data->last_seek_request_time_us = 0;
      }

      app_debug_log(data, "BUS", "收到 STATE_CHANGED old=%s new=%s pending=%s",
          gst_element_state_get_name(old_state),
          gst_element_state_get_name(new_state),
          gst_element_state_get_name(pending_state));

      if (new_state == GST_STATE_PLAYING) {
        app_set_status(data, "播放中");
      } else if (new_state == GST_STATE_PAUSED && data->has_media) {
        if (data->is_seeking && data->seek_resume_playback) {
          app_set_status(data, "跳转中");
        } else {
          app_set_status(data, "已暂停");
        }
      } else if (new_state == GST_STATE_READY && data->has_media) {
        app_set_status(data, "已停止");
      }
      break;
    }
    default:
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

  if (data->asink != NULL) {
    gst_object_unref(data->asink);
    data->asink = NULL;
  }

  if (data->videosink != NULL) {
    gst_object_unref(data->videosink);
    data->videosink = NULL;
  }

  if (data->gtkglsink != NULL) {
    gst_object_unref(data->gtkglsink);
    data->gtkglsink = NULL;
  }
}
