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

static void app_watch_playback_progress(CustomData *data);

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

static void app_clear_string(gchar **value) {
  if (value != NULL && *value != NULL) {
    g_free(*value);
    *value = NULL;
  }
}

static gchar *app_dup_display_name_from_uri(const gchar *uri) {
  gchar *filename;
  gchar *basename;

  if (uri == NULL) {
    return g_strdup("未选择媒体");
  }

  filename = g_filename_from_uri(uri, NULL, NULL);
  if (filename == NULL) {
    return g_strdup(uri);
  }

  basename = g_path_get_basename(filename);
  g_free(filename);
  return basename;
}

static gchar *app_dup_directory_from_uri(const gchar *uri) {
  gchar *filename;
  gchar *directory;

  if (uri == NULL) {
    return NULL;
  }

  filename = g_filename_from_uri(uri, NULL, NULL);
  if (filename == NULL) {
    return NULL;
  }

  directory = g_path_get_dirname(filename);
  g_free(filename);
  return directory;
}

static gchar *app_format_duration_text(gint64 duration) {
  gint64 total_seconds;

  if (!GST_CLOCK_TIME_IS_VALID(duration) || duration <= 0) {
    return g_strdup("未知");
  }

  total_seconds = duration / GST_SECOND;
  return g_strdup_printf("%02lld:%02lld",
      (long long)(total_seconds / 60),
      (long long)(total_seconds % 60));
}

static const gchar *app_current_status_text(CustomData *data) {
  if (data == NULL || data->status_label == NULL) {
    return "未打开媒体";
  }

  return gtk_label_get_text(GTK_LABEL(data->status_label));
}

static void app_update_media_info_text(CustomData *data) {
  gchar *duration_text;
  gchar *text;
  const gchar *display_name;
  const gchar *status;

  display_name = data->current_display_name != NULL ? data->current_display_name : "未选择媒体";
  status = app_current_status_text(data);
  duration_text = app_format_duration_text(data->duration);
  text = g_strdup_printf("媒体：%s\n状态：%s\n总时长：%s",
      display_name,
      status,
      duration_text);

  app_clear_string(&data->media_info_text);
  data->media_info_text = text;
  g_free(duration_text);
}

static void app_reset_playlist(CustomData *data) {
  if (data->playlist_uris != NULL) {
    g_ptr_array_set_size(data->playlist_uris, 0);
  }

  if (data->playlist_names != NULL) {
    g_ptr_array_set_size(data->playlist_names, 0);
  }

  data->current_index = -1;
}

static gint app_compare_path_names(gconstpointer a, gconstpointer b) {
  const gchar *const *path_a;
  const gchar *const *path_b;

  path_a = (const gchar *const *)a;
  path_b = (const gchar *const *)b;
  return g_ascii_strcasecmp(*path_a, *path_b);
}

static gboolean app_is_supported_media_filename(const gchar *name) {
  gchar *lower_name;
  gboolean supported;

  if (name == NULL) {
    return FALSE;
  }

  lower_name = g_ascii_strdown(name, -1);
  supported = g_str_has_suffix(lower_name, ".mp3") ||
      g_str_has_suffix(lower_name, ".wav") ||
      g_str_has_suffix(lower_name, ".flac") ||
      g_str_has_suffix(lower_name, ".aac") ||
      g_str_has_suffix(lower_name, ".m4a") ||
      g_str_has_suffix(lower_name, ".ogg") ||
      g_str_has_suffix(lower_name, ".mp4") ||
      g_str_has_suffix(lower_name, ".mkv") ||
      g_str_has_suffix(lower_name, ".avi") ||
      g_str_has_suffix(lower_name, ".mov") ||
      g_str_has_suffix(lower_name, ".webm");
  g_free(lower_name);
  return supported;
}

static gboolean app_add_playlist_entry(CustomData *data, const gchar *uri, const gchar *display_name) {
  if (uri == NULL || display_name == NULL) {
    return FALSE;
  }

  g_ptr_array_add(data->playlist_uris, g_strdup(uri));
  g_ptr_array_add(data->playlist_names, g_strdup(display_name));
  return TRUE;
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

static gboolean app_switch_to_index(CustomData *data, gint index, gboolean autoplay, const gchar *loaded_status) {
  const gchar *uri;
  const gchar *display_name;
  GstStateChangeReturn ret;

  if (data->playlist_uris == NULL || index < 0 || index >= (gint)data->playlist_uris->len) {
    return FALSE;
  }

  if (data->switching_track) {
    return FALSE;
  }

  data->switching_track = TRUE;
  data->current_index = index;
  uri = g_ptr_array_index(data->playlist_uris, index);
  display_name = g_ptr_array_index(data->playlist_names, index);

  app_clear_string(&data->current_display_name);
  data->current_display_name = g_strdup(display_name);

  if (app_open_uri(data, uri) != 0) {
    data->switching_track = FALSE;
    app_set_status(data, "打开媒体失败");
    return FALSE;
  }

  if (loaded_status != NULL) {
    app_set_status(data, loaded_status);
  }

  if (autoplay) {
    ret = player_play(data);
    if (ret == GST_STATE_CHANGE_FAILURE) {
      data->switching_track = FALSE;
      app_set_status(data, "切换后播放失败");
      return FALSE;
    }
    app_set_status(data, "播放中");
  }

  app_request_ui_refresh(data);
  data->switching_track = FALSE;
  return TRUE;
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

static void app_watch_playback_progress(CustomData *data) {
  gint64 now_us;
  gint64 stagnant_us;
  gboolean recent_seek;
  GstStateChangeReturn pause_ret;
  GstStateChangeReturn play_ret;
  const gint64 progress_threshold = 100 * 1000 * 1000;
  const gint64 stall_log_threshold_us = 1500 * 1000;
  const gint64 stall_recovery_threshold_us = 2200 * 1000;
  const gint64 recovery_cooldown_us = 3000 * 1000;
  const gint64 recent_seek_window_us = 5000 * 1000;

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
    data->last_seek_request_time_us = 0;
    data->last_progress_position = data->position;
    data->last_progress_observed_time_us = now_us;
    data->playback_stalled = FALSE;
    return;
  }

  stagnant_us = now_us - data->last_progress_observed_time_us;
  recent_seek = data->last_seek_request_time_us != 0 &&
      now_us - data->last_seek_request_time_us < recent_seek_window_us;
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
  data->playback_stalled = FALSE;
  if (recent_seek) {
    app_debug_log(data, "WATCHDOG", "检测到 seek 后假播放，优先尝试原位 re-seek 恢复");
    if (player_recover_stalled_playback(data, data->position)) {
      return;
    }
    app_debug_log(data, "WATCHDOG", "原位 re-seek 恢复失败，回退到 PAUSED -> PLAYING");
  }

  app_debug_log(data, "WATCHDOG", "尝试轻量恢复：PAUSED -> PLAYING");
  pause_ret = gst_element_set_state(data->pipeline, GST_STATE_PAUSED);
  play_ret = gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
  app_debug_log(data, "WATCHDOG", "轻量恢复完成：pause_ret=%d play_ret=%d",
      pause_ret, play_ret);
}

void app_init(CustomData *data) {
  memset(data, 0, sizeof(*data));
  data->seek_done = TRUE;
  data->duration = GST_CLOCK_TIME_NONE;
  data->position = 0;
  data->current_index = -1;
  data->last_seek_request_time_us = 0;
  data->last_progress_position = 0;
  data->last_progress_observed_time_us = 0;
  data->last_stall_recovery_time_us = 0;
  data->playback_stalled = FALSE;
  data->audio_sink_name = g_strdup("autoaudiosink");
  data->playlist_uris = g_ptr_array_new_with_free_func(g_free);
  data->playlist_names = g_ptr_array_new_with_free_func(g_free);
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
  gchar *derived_name;
  gchar *derived_directory;

  if (uri == NULL) {
    return -1;
  }

  ret = gst_element_set_state(data->pipeline, GST_STATE_READY);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return -1;
  }

  app_clear_string(&data->current_uri);
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

  if (data->current_display_name == NULL) {
    derived_name = app_dup_display_name_from_uri(uri);
    data->current_display_name = derived_name;
  }

  derived_directory = app_dup_directory_from_uri(uri);
  if (derived_directory != NULL) {
    app_clear_string(&data->current_directory);
    data->current_directory = derived_directory;
  }

  app_reset_playback_view(data);
  app_set_status(data, "文件已加载");
  return 0;
}

int app_open_media_file(CustomData *data, const gchar *uri) {
  gchar *display_name;
  gchar *directory;

  if (uri == NULL) {
    app_set_status(data, "打开文件失败");
    return -1;
  }

  display_name = app_dup_display_name_from_uri(uri);
  directory = app_dup_directory_from_uri(uri);

  app_reset_playlist(data);
  app_add_playlist_entry(data, uri, display_name);
  app_clear_string(&data->current_directory);
  data->current_directory = directory;
  g_free(display_name);

  if (!app_switch_to_index(data, 0, FALSE, "文件已加载")) {
    return -1;
  }

  return 0;
}

int app_open_directory(CustomData *data, const gchar *directory) {
  GDir *dir;
  GError *error;
  const gchar *entry_name;
  GPtrArray *file_paths;
  guint i;
  gchar *status;

  if (directory == NULL || directory[0] == '\0') {
    app_set_status(data, "目录无效");
    return -1;
  }

  error = NULL;
  dir = g_dir_open(directory, 0, &error);
  if (dir == NULL) {
    app_set_status(data, "打开目录失败");
    if (error != NULL) {
      g_printerr("Failed to open directory '%s': %s\n", directory, error->message);
      g_error_free(error);
    }
    return -1;
  }

  file_paths = g_ptr_array_new_with_free_func(g_free);
  while ((entry_name = g_dir_read_name(dir)) != NULL) {
    gchar *full_path;

    if (!app_is_supported_media_filename(entry_name)) {
      continue;
    }

    full_path = g_build_filename(directory, entry_name, NULL);
    if (g_file_test(full_path, G_FILE_TEST_IS_REGULAR)) {
      g_ptr_array_add(file_paths, full_path);
    } else {
      g_free(full_path);
    }
  }
  g_dir_close(dir);

  if (file_paths->len == 0) {
    g_ptr_array_free(file_paths, TRUE);
    app_reset_playlist(data);
    app_set_status(data, "目录中没有可播放媒体");
    app_request_ui_refresh(data);
    return -1;
  }

  g_ptr_array_sort(file_paths, app_compare_path_names);
  app_reset_playlist(data);
  app_clear_string(&data->current_directory);
  data->current_directory = g_strdup(directory);

  for (i = 0; i < file_paths->len; ++i) {
    const gchar *full_path;
    gchar *uri;
    gchar *display_name;

    full_path = g_ptr_array_index(file_paths, i);
    uri = g_filename_to_uri(full_path, NULL, NULL);
    display_name = g_path_get_basename(full_path);
    if (uri != NULL) {
      app_add_playlist_entry(data, uri, display_name);
    }
    g_free(uri);
    g_free(display_name);
  }

  g_ptr_array_free(file_paths, TRUE);

  if (!app_switch_to_index(data, 0, FALSE, NULL)) {
    app_set_status(data, "目录加载失败");
    return -1;
  }

  status = g_strdup_printf("目录已加载，共 %u 个媒体", data->playlist_uris->len);
  app_set_status(data, status);
  g_free(status);
  return 0;
}

gboolean app_play_previous(CustomData *data) {
  if (data->playlist_uris == NULL || data->playlist_uris->len == 0) {
    app_set_status(data, "请先打开文件或目录");
    return FALSE;
  }

  if (data->current_index <= 0) {
    app_set_status(data, "已经是第一首");
    return FALSE;
  }

  return app_switch_to_index(data, data->current_index - 1, TRUE, NULL);
}

gboolean app_play_next(CustomData *data) {
  if (data->playlist_uris == NULL || data->playlist_uris->len == 0) {
    app_set_status(data, "请先打开文件或目录");
    return FALSE;
  }

  if (data->current_index < 0 || data->current_index + 1 >= (gint)data->playlist_uris->len) {
    app_set_status(data, "已经是最后一首");
    return FALSE;
  }

  return app_switch_to_index(data, data->current_index + 1, TRUE, NULL);
}

void app_handle_eos(CustomData *data) {
  if (data->switching_track) {
    return;
  }

  if (data->playlist_uris != NULL && data->current_index >= 0 &&
      data->current_index + 1 < (gint)data->playlist_uris->len) {
    app_debug_log(data, "APP", "EOS 后自动切换到下一首");
    if (app_switch_to_index(data, data->current_index + 1, TRUE, NULL)) {
      return;
    }
  }

  app_set_status(data, "播放结束");
}

void app_set_status(CustomData *data, const gchar *status) {
  ui_set_status_label(data, status);
  app_update_media_info_text(data);
  ui_set_media_info_label(data, data->media_info_text);
}

void app_request_ui_refresh(CustomData *data) {
  ui_set_time_label(data, data->position, data->duration);
  ui_set_scale_range(data, data->duration);
  ui_set_scale_value(data, data->position);
  app_update_media_info_text(data);
  ui_set_media_info_label(data, data->media_info_text);
  ui_update_transport_buttons(data);
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
  app_request_ui_refresh(data);

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

  app_clear_string(&data->current_directory);
  app_clear_string(&data->current_display_name);
  app_clear_string(&data->media_info_text);

  if (data->playlist_uris != NULL) {
    g_ptr_array_free(data->playlist_uris, TRUE);
    data->playlist_uris = NULL;
  }

  if (data->playlist_names != NULL) {
    g_ptr_array_free(data->playlist_names, TRUE);
    data->playlist_names = NULL;
  }

  if (data->audio_sink_name != NULL) {
    g_free(data->audio_sink_name);
    data->audio_sink_name = NULL;
  }

  if (data->audio_device != NULL) {
    g_free(data->audio_device);
    data->audio_device = NULL;
  }
}
