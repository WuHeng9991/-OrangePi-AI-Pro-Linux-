#include <pthread.h>
#include <string.h>

#include "app.h"
#include "player.h"
#include "pipeline.h"
#include "ui.h"

typedef struct _StatusUpdateData {
  CustomData *data;
  gchar *status;
} StatusUpdateData;

static gboolean refresh_ui_on_main_thread(gpointer user_data) {
  CustomData *data;

  data = (CustomData *)user_data;
  ui_set_time_label(data, data->position, data->duration);
  ui_set_scale_range(data, data->duration);
  ui_set_scale_value(data, data->position);

  return G_SOURCE_REMOVE;
}

static gboolean update_status_on_main_thread(gpointer user_data) {
  StatusUpdateData *payload;

  payload = (StatusUpdateData *)user_data;
  ui_set_status_label(payload->data, payload->status);
  g_free(payload->status);
  g_free(payload);

  return G_SOURCE_REMOVE;
}

void app_init(CustomData *data) {
  memset(data, 0, sizeof(*data));
  data->seek_done = TRUE;
  data->duration = GST_CLOCK_TIME_NONE;
  data->position = 0;
}

int app_setup_pipeline(CustomData *data, int argc, char *argv[]) {
  (void)argc;
  (void)argv;

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
  data->position = 0;
  data->duration = GST_CLOCK_TIME_NONE;

  app_reset_playback_view(data);
  app_set_status(data, "文件已加载");
  return 0;
}

void app_set_status(CustomData *data, const gchar *status) {
  StatusUpdateData *payload;

  payload = g_new0(StatusUpdateData, 1);
  payload->data = data;
  payload->status = g_strdup(status);
  g_idle_add(update_status_on_main_thread, payload);
}

void app_request_ui_refresh(CustomData *data) {
  g_idle_add(refresh_ui_on_main_thread, data);
}

void app_reset_playback_view(CustomData *data) {
  ui_set_time_label(data, 0, GST_CLOCK_TIME_NONE);
  ui_set_scale_range(data, GST_CLOCK_TIME_NONE);
  ui_set_scale_value(data, 0);
}

int app_run(CustomData *data) {
  GstBus *bus;
  GstMessage *msg;
  pthread_t ui_loop_thread;

  create_ui(data);
  app_reset_playback_view(data);
  app_set_status(data, "未打开媒体");

  pthread_create(&ui_loop_thread, NULL, &gtk_main_loop, (void *)data);

  bus = pipeline_get_bus(data);

  do {
    msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_DURATION);

    if (msg != NULL) {
      pipeline_handle_message(data, msg);
      app_request_ui_refresh(data);
    } else if (data->has_media) {
      gint64 current;

      current = -1;
      if (player_query_position(data, &current)) {
        data->position = current;
      }

      if (!GST_CLOCK_TIME_IS_VALID(data->duration)) {
        gint64 duration;

        duration = GST_CLOCK_TIME_NONE;
        if (player_query_duration(data, &duration)) {
          data->duration = duration;
        }
      }

      app_request_ui_refresh(data);
    }
  } while (!data->terminate);

  pthread_join(ui_loop_thread, NULL);
  gst_object_unref(bus);
  return 0;
}

void app_cleanup(CustomData *data) {
  pipeline_cleanup(data);
}
