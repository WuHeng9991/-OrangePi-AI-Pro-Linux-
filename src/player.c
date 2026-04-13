#include "player.h"

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
  gboolean seek_requested;

  if (!data->has_media || !data->seek_enabled || !data->seek_done) {
    app_debug_log(data, "PLAYER", "player_seek 被拒绝：target=%" GST_TIME_FORMAT,
        GST_TIME_ARGS((GstClockTime)(position >= 0 ? position : 0)));
    return FALSE;
  }

  data->seek_done = FALSE;
  app_debug_log(data, "PLAYER", "发送 seek，请求位置=%" GST_TIME_FORMAT,
      GST_TIME_ARGS((GstClockTime)(position >= 0 ? position : 0)));
  seek_requested = gst_element_seek_simple(
      data->pipeline,
      GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      position);

  if (!seek_requested) {
    data->seek_done = TRUE;
    app_debug_log(data, "PLAYER", "seek 请求失败");
    return FALSE;
  }

  data->position = position;
  app_debug_log(data, "PLAYER", "seek 请求已发送成功");
  return TRUE;
}
