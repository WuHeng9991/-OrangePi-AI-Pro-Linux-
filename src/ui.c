#include <glib.h>

#include "ui.h"
#include "player.h"

static gchar *format_time_text(gint64 position, gint64 duration) {
  gint64 pos_total_seconds;
  gint64 dur_total_seconds;

  if (!GST_CLOCK_TIME_IS_VALID(position) || position < 0) {
    position = 0;
  }

  if (!GST_CLOCK_TIME_IS_VALID(duration) || duration < 0) {
    duration = 0;
  }

  pos_total_seconds = position / GST_SECOND;
  dur_total_seconds = duration / GST_SECOND;

  return g_strdup_printf(
      "%02lld:%02lld / %02lld:%02lld",
      (long long)(pos_total_seconds / 60),
      (long long)(pos_total_seconds % 60),
      (long long)(dur_total_seconds / 60),
      (long long)(dur_total_seconds % 60));
}

static gboolean playback_active_for_seek(CustomData *data) {
  return data->playing || data->seek_resume_playback;
}

static void play_cb(GtkButton *button, CustomData *data) {
  GstStateChangeReturn ret;

  (void)button;

  app_debug_log(data, "UI", "点击 Play");
  ret = player_play(data);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    app_set_status(data, data->has_media ? "播放失败" : "请先打开文件");
    ui_update_transport_buttons(data);
    return;
  }

  app_set_status(data, "播放中");
  ui_update_transport_buttons(data);
}

static void pause_cb(GtkButton *button, CustomData *data) {
  GstStateChangeReturn ret;

  (void)button;

  app_debug_log(data, "UI", "点击 Pause");
  ret = player_pause(data);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    app_set_status(data, data->has_media ? "暂停失败" : "请先打开文件");
    ui_update_transport_buttons(data);
    return;
  }

  app_set_status(data, "已暂停");
  ui_update_transport_buttons(data);
}

static void stop_cb(GtkButton *button, CustomData *data) {
  GstStateChangeReturn ret;

  (void)button;

  app_debug_log(data, "UI", "点击 Stop");
  ret = player_stop(data);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    app_set_status(data, data->has_media ? "停止失败" : "请先打开文件");
    ui_update_transport_buttons(data);
    return;
  }

  app_reset_playback_view(data);
  app_set_status(data, data->has_media ? "已停止" : "未打开媒体");
  app_request_ui_refresh(data);
}

static void prev_cb(GtkButton *button, CustomData *data) {
  (void)button;

  app_debug_log(data, "UI", "点击 Prev");
  app_play_previous(data);
  app_request_ui_refresh(data);
}

static void next_cb(GtkButton *button, CustomData *data) {
  (void)button;

  app_debug_log(data, "UI", "点击 Next");
  app_play_next(data);
  app_request_ui_refresh(data);
}

static void open_cb(GtkButton *button, CustomData *data) {
  GtkWidget *dialog;
  gint response;

  (void)button;

  dialog = gtk_file_chooser_dialog_new("打开媒体文件",
      GTK_WINDOW(data->window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      "取消", GTK_RESPONSE_CANCEL,
      "打开", GTK_RESPONSE_ACCEPT,
      NULL);

  response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (response == GTK_RESPONSE_ACCEPT) {
    gchar *filename;
    gchar *uri;

    filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    uri = g_filename_to_uri(filename, NULL, NULL);
    if (uri == NULL || app_open_media_file(data, uri) != 0) {
      app_set_status(data, "打开文件失败");
      app_request_ui_refresh(data);
    }
    g_free(uri);
    g_free(filename);
  }

  gtk_widget_destroy(dialog);
}

static void open_dir_cb(GtkButton *button, CustomData *data) {
  GtkWidget *dialog;
  gint response;

  (void)button;

  dialog = gtk_file_chooser_dialog_new("打开媒体目录",
      GTK_WINDOW(data->window),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      "取消", GTK_RESPONSE_CANCEL,
      "打开", GTK_RESPONSE_ACCEPT,
      NULL);

  response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (response == GTK_RESPONSE_ACCEPT) {
    gchar *directory;

    directory = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    if (app_open_directory(data, directory) != 0) {
      app_request_ui_refresh(data);
    }
    g_free(directory);
  }

  gtk_widget_destroy(dialog);
}

static gboolean scale_button_press_cb(GtkWidget *widget, GdkEventButton *event, CustomData *data) {
  (void)widget;
  (void)event;

  app_debug_log(data, "UI", "按下进度条");

  if (!data->has_media) {
    app_debug_log(data, "UI", "忽略按下进度条：未加载媒体");
    return FALSE;
  }

  if (!data->seek_done) {
    app_debug_log(data, "UI", "忽略按下进度条：上一次 seek 尚未完成");
    app_set_status(data, "正在跳转中，请稍候");
    return FALSE;
  }

  if (!playback_active_for_seek(data)) {
    data->is_seeking = FALSE;
    app_debug_log(data, "UI", "忽略按下进度条：当前未处于可继续播放状态");
    return FALSE;
  }

  data->is_seeking = TRUE;
  app_debug_log(data, "UI", "进入拖动状态");
  return FALSE;
}

static gboolean scale_button_release_cb(GtkWidget *widget, GdkEventButton *event, CustomData *data) {
  gdouble value;

  (void)widget;
  (void)event;

  value = gtk_range_get_value(GTK_RANGE(data->position_scale));
  app_debug_log(data, "UI", "释放进度条，目标位置=%" GST_TIME_FORMAT,
      GST_TIME_ARGS((GstClockTime)(value >= 0.0 ? (gint64)value : 0)));

  if (!data->has_media) {
    app_debug_log(data, "UI", "忽略释放进度条：未加载媒体");
    app_set_status(data, "请先打开文件");
    return FALSE;
  }

  if (!playback_active_for_seek(data)) {
    data->is_seeking = FALSE;
    app_debug_log(data, "UI", "忽略释放进度条：当前未处于可继续播放状态");
    app_set_status(data, "请先播放媒体");
    return FALSE;
  }

  if (!data->seek_done) {
    data->is_seeking = FALSE;
    app_debug_log(data, "UI", "忽略释放进度条：上一次 seek 尚未完成");
    app_set_status(data, "正在跳转中，请稍候");
    return FALSE;
  }

  if (!data->seek_enabled) {
    data->is_seeking = FALSE;
    app_debug_log(data, "UI", "忽略释放进度条：当前媒体不支持拖动");
    app_set_status(data, "当前媒体不支持拖动");
    return FALSE;
  }

  data->position = (gint64)value;
  ui_set_time_label(data, data->position, data->duration);

  if (!player_seek(data, (gint64)value)) {
    data->is_seeking = FALSE;
    data->seek_done = TRUE;
    app_debug_log(data, "UI", "释放进度条后 seek 失败");
    app_set_status(data, "拖动失败");
    return FALSE;
  }

  app_debug_log(data, "UI", "释放进度条后 seek 已提交");
  app_set_status(data, "跳转中");
  return FALSE;
}

void ui_set_time_label(CustomData *data, gint64 position, gint64 duration) {
  gchar *text;

  if (data->time_label == NULL) {
    return;
  }

  text = format_time_text(position, duration);
  gtk_label_set_text(GTK_LABEL(data->time_label), text);
  g_free(text);
}

void ui_set_status_label(CustomData *data, const gchar *status) {
  if (data->status_label == NULL) {
    return;
  }

  gtk_label_set_text(GTK_LABEL(data->status_label), status);
}

void ui_set_scale_range(CustomData *data, gint64 duration) {
  gdouble upper;

  if (data->position_scale == NULL) {
    return;
  }

  upper = GST_CLOCK_TIME_IS_VALID(duration) && duration > 0 ? (gdouble)duration : 100.0;
  gtk_range_set_range(GTK_RANGE(data->position_scale), 0.0, upper);
}

void ui_set_scale_value(CustomData *data, gint64 position) {
  if (data->position_scale == NULL || data->is_seeking) {
    return;
  }

  gtk_range_set_value(GTK_RANGE(data->position_scale), (gdouble)position);
}

void ui_set_media_info_label(CustomData *data, const gchar *text) {
  if (data->media_info_label == NULL) {
    return;
  }

  gtk_label_set_text(GTK_LABEL(data->media_info_label), text != NULL ? text : "媒体：未选择媒体\n状态：未打开媒体\n总时长：未知");
}

void ui_update_transport_buttons(CustomData *data) {
  gboolean has_playlist;
  gboolean has_previous;
  gboolean has_next;
  gboolean can_play;
  gboolean can_pause_stop;

  has_playlist = data->playlist_uris != NULL && data->playlist_uris->len > 0;
  has_previous = has_playlist && data->current_index > 0;
  has_next = has_playlist && data->current_index >= 0 &&
      data->current_index + 1 < (gint)data->playlist_uris->len;
  can_play = data->has_media;
  can_pause_stop = data->has_media;

  if (data->open_button != NULL) {
    gtk_widget_set_sensitive(data->open_button, TRUE);
  }
  if (data->open_dir_button != NULL) {
    gtk_widget_set_sensitive(data->open_dir_button, TRUE);
  }
  if (data->prev_button != NULL) {
    gtk_widget_set_sensitive(data->prev_button, has_previous);
  }
  if (data->next_button != NULL) {
    gtk_widget_set_sensitive(data->next_button, has_next);
  }
  if (data->play_button != NULL) {
    gtk_widget_set_sensitive(data->play_button, can_play);
  }
  if (data->pause_button != NULL) {
    gtk_widget_set_sensitive(data->pause_button, can_pause_stop);
  }
  if (data->stop_button != NULL) {
    gtk_widget_set_sensitive(data->stop_button, can_pause_stop);
  }
  if (data->position_scale != NULL) {
    gtk_widget_set_sensitive(data->position_scale, data->has_media);
  }
}

void create_ui(CustomData *data) {
  GtkWidget *main_view;
  GtkWidget *content_box;
  GtkWidget *control_box;
  GtkWidget *buttons_box;
  GtkWidget *progress_frame;
  GtkWidget *info_frame;
  GtkWidget *info_box;

  content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  buttons_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  control_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  progress_frame = gtk_frame_new("播放进度");
  info_frame = gtk_frame_new("媒体信息");
  info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

  data->open_button = gtk_button_new_with_label("Open");
  data->open_dir_button = gtk_button_new_with_label("Open Dir");
  data->prev_button = gtk_button_new_with_label("Prev");
  data->next_button = gtk_button_new_with_label("Next");
  data->play_button = gtk_button_new_with_label("Play");
  data->pause_button = gtk_button_new_with_label("Pause");
  data->stop_button = gtk_button_new_with_label("Stop");
  data->position_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  data->time_label = gtk_label_new("00:00 / 00:00");
  data->status_label = gtk_label_new("未打开媒体");
  data->media_info_label = gtk_label_new("媒体：未选择媒体\n状态：未打开媒体\n总时长：未知");

  gtk_widget_set_hexpand(data->position_scale, TRUE);
  gtk_widget_set_size_request(data->position_scale, 640, 36);
  gtk_scale_set_draw_value(GTK_SCALE(data->position_scale), FALSE);
  gtk_range_set_value(GTK_RANGE(data->position_scale), 0.0);
  gtk_container_add(GTK_CONTAINER(progress_frame), data->position_scale);

  gtk_label_set_xalign(GTK_LABEL(data->time_label), 0.0f);
  gtk_label_set_xalign(GTK_LABEL(data->status_label), 0.0f);
  gtk_label_set_xalign(GTK_LABEL(data->media_info_label), 0.0f);
  gtk_label_set_line_wrap(GTK_LABEL(data->media_info_label), TRUE);
  gtk_box_pack_start(GTK_BOX(info_box), data->media_info_label, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(info_frame), info_box);

  gtk_box_pack_start(GTK_BOX(buttons_box), data->open_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->open_dir_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->prev_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->next_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->play_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->pause_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->stop_button, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(control_box), buttons_box, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(control_box), progress_frame, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(control_box), data->time_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(control_box), data->status_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(control_box), info_frame, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(content_box), data->sink_widget, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(content_box), control_box, FALSE, FALSE, 0);

  main_view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(main_view), content_box, TRUE, TRUE, 0);

  data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(data->window), 960, 640);

  g_signal_connect(data->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(G_OBJECT(data->open_button), "clicked", G_CALLBACK(open_cb), data);
  g_signal_connect(G_OBJECT(data->open_dir_button), "clicked", G_CALLBACK(open_dir_cb), data);
  g_signal_connect(G_OBJECT(data->prev_button), "clicked", G_CALLBACK(prev_cb), data);
  g_signal_connect(G_OBJECT(data->next_button), "clicked", G_CALLBACK(next_cb), data);
  g_signal_connect(G_OBJECT(data->play_button), "clicked", G_CALLBACK(play_cb), data);
  g_signal_connect(G_OBJECT(data->pause_button), "clicked", G_CALLBACK(pause_cb), data);
  g_signal_connect(G_OBJECT(data->stop_button), "clicked", G_CALLBACK(stop_cb), data);
  g_signal_connect(G_OBJECT(data->position_scale), "button-press-event", G_CALLBACK(scale_button_press_cb), data);
  g_signal_connect(G_OBJECT(data->position_scale), "button-release-event", G_CALLBACK(scale_button_release_cb), data);

  gtk_container_add(GTK_CONTAINER(data->window), main_view);
  ui_update_transport_buttons(data);
  gtk_widget_show_all(data->window);
}
