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
  if (!data->has_media) {
    return GST_STATE_CHANGE_FAILURE;
  }

  return gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
}

GstStateChangeReturn player_pause(CustomData *data) {
  if (!data->has_media) {
    return GST_STATE_CHANGE_FAILURE;
  }

  return gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
}

GstStateChangeReturn player_stop(CustomData *data) {
  GstStateChangeReturn ret;

  ret = gst_element_set_state(data->pipeline, GST_STATE_READY);
  data->playing = FALSE;
  data->seek_done = TRUE;
  data->position = 0;
  data->duration = GST_CLOCK_TIME_NONE;

  return ret;
}

gboolean player_seek(CustomData *data, gint64 position) {
  if (!data->has_media || !data->seek_enabled) {
    return FALSE;
  }

  data->seek_done = gst_element_seek_simple(
      data->pipeline,
      GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
      position);

  if (data->seek_done) {
    data->position = position;
  }

  return data->seek_done;
}
