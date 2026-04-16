#include "player.h"

static gboolean player_submit_seek(CustomData *data,
    gint64 position,
    GstSeekFlags flags,
    const gchar *reason) {
  gboolean seek_requested;
  gint64 target_position;

  if (!data->has_media || data->pipeline == NULL || !data->seek_enabled) {
    app_debug_log(data, "PLAYER", "%s被拒绝：target=%" GST_TIME_FORMAT,
        reason != NULL ? reason : "seek",
        GST_TIME_ARGS((GstClockTime)(position >= 0 ? position : 0)));
    return FALSE;
  }

  target_position = position >= 0 ? position : 0;
  data->is_seeking = TRUE;
  data->seek_done = FALSE;
  data->last_seek_request_time_us = g_get_monotonic_time();
  app_debug_log(data, "PLAYER", "%s，请求位置=%" GST_TIME_FORMAT " flags=0x%x",
      reason != NULL ? reason : "发送 seek",
      GST_TIME_ARGS((GstClockTime)target_position),
      (guint)flags);
  seek_requested = gst_element_seek_simple(
      data->pipeline,
      GST_FORMAT_TIME,
      flags,
      target_position);

  if (!seek_requested) {
    data->is_seeking = FALSE;
    data->seek_done = TRUE;
    app_debug_log(data, "PLAYER", "%s失败", reason != NULL ? reason : "seek 请求");
    return FALSE;
  }

  data->position = target_position;
  app_debug_log(data, "PLAYER", "%s已发送成功", reason != NULL ? reason : "seek 请求");
  return TRUE;
}

gboolean player_query_position(CustomData *data, gint64 *position) {
  if (!data->has_media || data->pipeline == NULL) {
    return FALSE;
  }

  return gst_element_query_position(data->pipeline, GST_FORMAT_TIME, position);
}

gboolean player_query_duration(CustomData *data, gint64 *duration) {
  if (!data->has_media || data->pipeline == NULL) {
    return FALSE;
  }

  return gst_element_query_duration(data->pipeline, GST_FORMAT_TIME, duration);
}

GstStateChangeReturn player_play(CustomData *data) {
  GstStateChangeReturn ret;

  if (!data->has_media) {
    app_debug_log(data, "PLAYER", "player_play 被拒绝：未加载媒体");
    return GST_STATE_CHANGE_FAILURE;
  }

  data->seek_resume_playback = TRUE;
  app_debug_log(data, "PLAYER", "调用 player_play，请求切到 PLAYING");
  ret = gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
  if (ret != GST_STATE_CHANGE_FAILURE) {
    data->playing = TRUE;
  }
  app_debug_log(data, "PLAYER", "player_play 返回=%d", ret);
  return ret;
}

GstStateChangeReturn player_pause(CustomData *data) {
  GstStateChangeReturn ret;

  if (!data->has_media) {
    app_debug_log(data, "PLAYER", "player_pause 被拒绝：未加载媒体");
    return GST_STATE_CHANGE_FAILURE;
  }

  data->seek_resume_playback = FALSE;
  app_debug_log(data, "PLAYER", "调用 player_pause，请求切到 PAUSED");
  ret = gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
  if (ret != GST_STATE_CHANGE_FAILURE) {
    data->playing = FALSE;
  }
  app_debug_log(data, "PLAYER", "player_pause 返回=%d", ret);
  return ret;
}

GstStateChangeReturn player_stop(CustomData *data) {
  GstStateChangeReturn ret;

  app_debug_log(data, "PLAYER", "调用 player_stop，请求切到 READY");
  ret = gst_element_set_state(data->pipeline, GST_STATE_READY);
  data->playing = FALSE;
  data->is_seeking = FALSE;
  data->seek_done = TRUE;
  data->seek_resume_playback = FALSE;
  data->last_seek_request_time_us = 0;
  data->position = 0;
  data->duration = GST_CLOCK_TIME_NONE;
  data->last_progress_position = 0;
  data->last_progress_observed_time_us = 0;
  data->last_stall_recovery_time_us = 0;
  data->playback_stalled = FALSE;
  app_debug_log(data, "PLAYER", "player_stop 返回=%d", ret);

  return ret;
}

gboolean player_seek(CustomData *data, gint64 position) {
  if (!data->has_media || !data->seek_enabled || !data->seek_done) {
    app_debug_log(data, "PLAYER", "player_seek 被拒绝：target=%" GST_TIME_FORMAT,
        GST_TIME_ARGS((GstClockTime)(position >= 0 ? position : 0)));
    return FALSE;
  }

  return player_submit_seek(
      data,
      position,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      "发送 seek");
}

gboolean player_recover_stalled_playback(CustomData *data, gint64 position) {
  if (!data->has_media || data->pipeline == NULL || !data->seek_enabled) {
    app_debug_log(data, "PLAYER", "watchdog 原位恢复被拒绝：target=%" GST_TIME_FORMAT,
        GST_TIME_ARGS((GstClockTime)(position >= 0 ? position : 0)));
    return FALSE;
  }

  return player_submit_seek(
      data,
      position,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
      "watchdog 触发原位恢复 seek");
}
