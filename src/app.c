#include <stdarg.h>
#include <string.h>

#include "app.h"
#include "player.h"
#include "pipeline.h"
#include "ui.h"

typedef struct _AppLoopData {
  CustomData *data;
  GstBus *bus;
} AppLoopData;

static GstClockTime app_debug_log_time_value(gint64 value) {
  if (!GST_CLOCK_TIME_IS_VALID(value) || value < 0) {
    return 0;
  }

  return (GstClockTime)value;
}

static const gchar *app_debug_state_change_return_name(GstStateChangeReturn ret) {
  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      return "FAILURE";
    case GST_STATE_CHANGE_SUCCESS:
      return "SUCCESS";
    case GST_STATE_CHANGE_ASYNC:
      return "ASYNC";
    case GST_STATE_CHANGE_NO_PREROLL:
      return "NO_PREROLL";
    default:
      return "UNKNOWN";
  }
}

static void app_load_audio_output_config(CustomData *data) {
  const gchar *audio_sink_name;
  const gchar *audio_device;

  audio_sink_name = g_getenv("OPMP_AUDIO_SINK");
  audio_device = g_getenv("OPMP_AUDIO_DEVICE");

  if (audio_sink_name != NULL && audio_sink_name[0] != '\0') {
    g_free(data->audio_sink_name);
    data->audio_sink_name = g_strdup(audio_sink_name);//g_strdup 将字符串的首地址存在申请的另一块内存中
  }

  if (audio_device != NULL && audio_device[0] != '\0') {
    g_free(data->audio_device);
    data->audio_device = g_strdup(audio_device);
  }
}

static void app_update_playback_metrics(CustomData *data) {
  gint64 current;
  gint64 duration;

  if (!data->has_media) {
    return;
  }

  if (!data->is_seeking || data->seek_done) {
    current = -1;
    if (player_query_position(data, &current)) {
      data->position = current;
    }
  }

  duration = GST_CLOCK_TIME_NONE;
  if (player_query_duration(data, &duration) && GST_CLOCK_TIME_IS_VALID(duration)) {
    data->duration = duration;
  }
}

static void app_reset_progress_watchdog(CustomData *data) {
  data->last_progress_position = data->position;
  data->last_progress_observed_time_us = g_get_monotonic_time();
  data->playback_stalled = FALSE;
}

static void app_watch_playback_progress(CustomData *data) {
  gint64 now_us;
  gint64 stagnant_us;
  GstStateChangeReturn pause_ret;
  GstStateChangeReturn play_ret;
  const gint64 progress_threshold = 100 * 1000 * 1000;
  const gint64 stall_log_threshold_us = 1500 * 1000;
  const gint64 stall_recovery_threshold_us = 2200 * 1000;
  const gint64 recovery_cooldown_us = 3000 * 1000;

  if (!data->has_media || !data->playing || !data->seek_resume_playback ||
      data->is_seeking || !data->seek_done) {
    app_reset_progress_watchdog(data);
    return;
  }

  if (GST_CLOCK_TIME_IS_VALID(data->duration) && data->duration > 0 &&
      data->position >= data->duration - (200 * GST_MSECOND)) {
    app_reset_progress_watchdog(data);
    return;
  }

  now_us = g_get_monotonic_time();
  if (data->last_progress_observed_time_us == 0) {
    app_reset_progress_watchdog(data);
    return;
  }

  if (data->position >= data->last_progress_position + progress_threshold) {
    if (data->playback_stalled) {
      app_debug_log(data, "WATCHDOG", "检测到播放进度已恢复");
    }
    data->last_progress_position = data->position;
    data->last_progress_observed_time_us = now_us;
    data->playback_stalled = FALSE;
    return;
  }

  stagnant_us = now_us - data->last_progress_observed_time_us;
  if (stagnant_us >= stall_log_threshold_us && !data->playback_stalled) {
    data->playback_stalled = TRUE;
    app_debug_log(data, "WATCHDOG", "检测到播放卡住，位置已 %.3f 秒未前进",
        stagnant_us / 1000000.0);
  }

  if (stagnant_us < stall_recovery_threshold_us) {
    return;
  }

  if (data->last_stall_recovery_time_us != 0 &&
      now_us - data->last_stall_recovery_time_us < recovery_cooldown_us) {
    return;
  }

  data->last_stall_recovery_time_us = now_us;
  data->last_progress_observed_time_us = now_us;
  data->last_progress_position = data->position;
  app_debug_log(data, "WATCHDOG", "尝试轻量恢复：PAUSED -> PLAYING");
  pause_ret = gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
  play_ret = gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
  app_debug_log(data, "WATCHDOG", "轻量恢复完成：pause_ret=%d play_ret=%d",
      pause_ret, play_ret);
}

static gboolean app_poll_pipeline(gpointer user_data) {
  AppLoopData *loop_data;
  CustomData *data;
  GstMessage *msg;
  gboolean handled_message;

  loop_data = (AppLoopData *)user_data;
  data = loop_data->data;
  handled_message = FALSE;

  while ((msg = gst_bus_pop_filtered(loop_data->bus,
      GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
      GST_MESSAGE_DURATION_CHANGED | GST_MESSAGE_ASYNC_DONE |
      GST_MESSAGE_CLOCK_LOST | GST_MESSAGE_NEW_CLOCK |
      GST_MESSAGE_LATENCY)) != NULL) {
    handled_message = TRUE;
    pipeline_handle_message(data, msg);
  }

  if (data->has_media) {
    app_update_playback_metrics(data);
    app_watch_playback_progress(data);
    app_request_ui_refresh(data);
  } else if (handled_message) {
    app_request_ui_refresh(data);
  }

  return data->terminate ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}

void app_init(CustomData *data) {
  memset(data, 0, sizeof(*data));
  data->seek_done = TRUE;
  data->duration = GST_CLOCK_TIME_NONE;
  data->position = 0;
  data->last_seek_request_time_us = 0;
  data->last_progress_position = 0;
  data->last_progress_observed_time_us = 0;
  data->last_stall_recovery_time_us = 0;
  data->playback_stalled = FALSE;
  data->audio_sink_name = g_strdup("autoaudiosink");
}

int app_setup_pipeline(CustomData *data, int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  app_load_audio_output_config(data);

  if (pipeline_create(data) != 0) {
    return -1;
  }

  if (pipeline_setup_video_sink(data) != 0) {
    pipeline_cleanup(data);
    return -1;
  }

  if (pipeline_build(data) != 0) {
    pipeline_cleanup(data);
    return -1;
  }

  return 0;
}

int app_open_uri(CustomData *data, const gchar *uri) {
  GstStateChangeReturn ret;

  if (uri == NULL) {
    return -1;
  }

  ret = gst_element_set_state(data->pipeline, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return -1;
  }

  if (data->current_uri != NULL) {
    g_free(data->current_uri);
  }

  data->current_uri = g_strdup(uri);
  pipeline_set_source_uri(data, data->current_uri);
  data->has_media = TRUE;
  data->playing = FALSE;
  data->seek_enabled = FALSE;
  data->seek_done = TRUE;
  data->seek_resume_playback = FALSE;
  data->last_seek_request_time_us = 0;
  data->position = 0;
  data->duration = GST_CLOCK_TIME_NONE;
  data->last_progress_position = 0;
  data->last_progress_observed_time_us = 0;
  data->last_stall_recovery_time_us = 0;
  data->playback_stalled = FALSE;

  app_reset_playback_view(data);
  app_set_status(data, "文件已加载");
  return 0;
}

void app_set_status(CustomData *data, const gchar *status) {
  ui_set_status_label(data, status);
}

void app_request_ui_refresh(CustomData *data) {
  ui_set_time_label(data, data->position, data->duration);
  ui_set_scale_range(data, data->duration);
  ui_set_scale_value(data, data->position);
}

void app_reset_playback_view(CustomData *data) {
  ui_set_time_label(data, 0, GST_CLOCK_TIME_NONE);
  ui_set_scale_range(data, GST_CLOCK_TIME_NONE);
  ui_set_scale_value(data, 0);
}

void app_debug_log(CustomData *data, const gchar *scope, const gchar *fmt, ...) {
  va_list args;
  gchar *message;
  gint64 now_us;
  GstState current_state;
  GstState pending_state;
  GstStateChangeReturn ret;

  va_start(args, fmt);
  message = g_strdup_vprintf(fmt, args);
  va_end(args);

  now_us = g_get_real_time();
  current_state = GST_STATE_VOID_PENDING;
  pending_state = GST_STATE_VOID_PENDING;
  ret = GST_STATE_CHANGE_FAILURE;

  if (data != NULL && data->pipeline != NULL) {
    ret = gst_element_get_state(data->pipeline, &current_state, &pending_state, 0);
  }

  g_print("[OPMP][%lld.%03lld][%s] %s | state=%s pending=%s get_state=%s play=%d has_media=%d seek_enabled=%d seek_done=%d is_seeking=%d seek_resume=%d pos=%" GST_TIME_FORMAT " dur=%" GST_TIME_FORMAT "\n",
      (long long)(now_us / G_USEC_PER_SEC),
      (long long)((now_us % G_USEC_PER_SEC) / 1000),
      scope != NULL ? scope : "APP",
      message != NULL ? message : "",
      gst_element_state_get_name(current_state),
      gst_element_state_get_name(pending_state),
      app_debug_state_change_return_name(ret),
      data != NULL ? data->playing : FALSE,
      data != NULL ? data->has_media : FALSE,
      data != NULL ? data->seek_enabled : FALSE,
      data != NULL ? data->seek_done : FALSE,
      data != NULL ? data->is_seeking : FALSE,
      data != NULL ? data->seek_resume_playback : FALSE,
      GST_TIME_ARGS(data != NULL ? app_debug_log_time_value(data->position) : 0),
      GST_TIME_ARGS(data != NULL ? app_debug_log_time_value(data->duration) : 0));

  g_free(message);
}

int app_run(CustomData *data) {
  GstBus *bus;
  AppLoopData loop_data;

  create_ui(data);
  app_reset_playback_view(data);
  app_set_status(data, "未打开媒体");

  bus = pipeline_get_bus(data);
  loop_data.data = data;
  loop_data.bus = bus;
  g_timeout_add(100, app_poll_pipeline, &loop_data);

  gtk_main();

  data->terminate = TRUE;
  gst_object_unref(bus);
  return 0;
}

void app_cleanup(CustomData *data) {
  pipeline_cleanup(data);

  if (data->audio_sink_name != NULL) {
    g_free(data->audio_sink_name);
    data->audio_sink_name = NULL;
  }

  if (data->audio_device != NULL) {
    g_free(data->audio_device);
    data->audio_device = NULL;
  }
}
