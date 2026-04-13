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
    ui_set_status_label(data, data->has_media ? "播放失败" : "请先打开文件");
    return;
  }

  ui_set_status_label(data, "播放中");
}

static void pause_cb(GtkButton *button, CustomData *data) {
  GstStateChangeReturn ret;

  (void)button;

  app_debug_log(data, "UI", "点击 Pause");
  ret = player_pause(data);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    ui_set_status_label(data, data->has_media ? "暂停失败" : "请先打开文件");
    return;
  }

  ui_set_status_label(data, "已暂停");
}

static void stop_cb(GtkButton *button, CustomData *data) {
  GstStateChangeReturn ret;

  (void)button;

  app_debug_log(data, "UI", "点击 Stop");
  ret = player_stop(data);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    ui_set_status_label(data, data->has_media ? "停止失败" : "请先打开文件");
    return;
  }

  ui_set_time_label(data, 0, GST_CLOCK_TIME_NONE);
  ui_set_scale_range(data, GST_CLOCK_TIME_NONE);
  ui_set_scale_value(data, 0);
  ui_set_status_label(data, data->has_media ? "已停止" : "未打开媒体");
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

    filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));//取出文件路径
    uri = g_filename_to_uri(filename, NULL, NULL);//转为uri
    if (uri == NULL || app_open_uri(data, uri) != 0) {
      ui_set_status_label(data, "打开文件失败");
    }
    g_free(uri);
    g_free(filename);
  }

  gtk_widget_destroy(dialog);//销毁
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
    ui_set_status_label(data, "正在跳转中，请稍候");
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
    ui_set_status_label(data, "请先打开文件");
    return FALSE;
  }

  if (!playback_active_for_seek(data)) {
    data->is_seeking = FALSE;
    app_debug_log(data, "UI", "忽略释放进度条：当前未处于可继续播放状态");
    ui_set_status_label(data, "请先播放媒体");
    return FALSE;
  }

  if (!data->seek_done) {
    data->is_seeking = FALSE;
    app_debug_log(data, "UI", "忽略释放进度条：上一次 seek 尚未完成");
    ui_set_status_label(data, "正在跳转中，请稍候");
    return FALSE;
  }

  if (!data->seek_enabled) {
    data->is_seeking = FALSE;
    app_debug_log(data, "UI", "忽略释放进度条：当前媒体不支持拖动");
    ui_set_status_label(data, "当前媒体不支持拖动");
    return FALSE;
  }

  data->position = (gint64)value;
  ui_set_time_label(data, data->position, data->duration);

  if (!player_seek(data, (gint64)value)) {
    data->is_seeking = FALSE;
    data->seek_done = TRUE;
    app_debug_log(data, "UI", "释放进度条后 seek 失败");
    ui_set_status_label(data, "拖动失败");
    return FALSE;
  }

  app_debug_log(data, "UI", "释放进度条后 seek 已提交");
  ui_set_status_label(data, "跳转中");
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

void create_ui(CustomData *data) {
  GtkWidget *main_view;
  GtkWidget *content_box;
  GtkWidget *control_box;
  GtkWidget *buttons_box;
  GtkWidget *progress_frame;

  content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  buttons_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  control_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  progress_frame = gtk_frame_new("播放进度");

  data->open_button = gtk_button_new_with_label("Open");
  data->play_button = gtk_button_new_with_label("Play");
  data->pause_button = gtk_button_new_with_label("Pause");
  data->stop_button = gtk_button_new_with_label("Stop");
  data->position_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.0, 100.0, 1.0);
  data->time_label = gtk_label_new("00:00 / 00:00");
  data->status_label = gtk_label_new("未打开媒体");

  gtk_widget_set_hexpand(data->position_scale, TRUE);//让进度条尽可能在水平方向延深
  gtk_widget_set_size_request(data->position_scale, 640, 36);//设置一个进度条的尺寸 宽:高
  gtk_scale_set_draw_value(GTK_SCALE(data->position_scale), FALSE);//不要在进度条上显示当前播放的时间
  gtk_range_set_value(GTK_RANGE(data->position_scale), 0.0);//设置进度条初始值
  gtk_container_add(GTK_CONTAINER(progress_frame), data->position_scale);//添加到容器中

  gtk_box_pack_start(GTK_BOX(buttons_box), data->open_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->play_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->pause_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(buttons_box), data->stop_button, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(control_box), buttons_box, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(control_box), progress_frame, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(control_box), data->time_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(control_box), data->status_label, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(content_box), data->sink_widget, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(content_box), control_box, FALSE, FALSE, 0);

  main_view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(main_view), content_box, TRUE, TRUE, 0);

  data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  //gtk_window_set_title(GTK_WINDOW(data->window), "Open pipe media player");
  gtk_window_set_default_size(GTK_WINDOW(data->window), 960, 640);

  g_signal_connect(data->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(G_OBJECT(data->open_button), "clicked", G_CALLBACK(open_cb), data);
  g_signal_connect(G_OBJECT(data->play_button), "clicked", G_CALLBACK(play_cb), data);
  g_signal_connect(G_OBJECT(data->pause_button), "clicked", G_CALLBACK(pause_cb), data);
  g_signal_connect(G_OBJECT(data->stop_button), "clicked", G_CALLBACK(stop_cb), data);
  g_signal_connect(G_OBJECT(data->position_scale), "button-press-event", G_CALLBACK(scale_button_press_cb), data);
  g_signal_connect(G_OBJECT(data->position_scale), "button-release-event", G_CALLBACK(scale_button_release_cb), data);

  gtk_container_add(GTK_CONTAINER(data->window), main_view);
  gtk_widget_show_all(data->window);
}
