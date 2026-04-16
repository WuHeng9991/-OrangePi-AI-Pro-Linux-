#ifndef APP_H
#define APP_H

#include <gtk/gtk.h>
#include <gst/gst.h>

typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *aconvert;
  GstElement *vconvert;
  GstElement *resample;
  GstElement *audio_queue;
  GstElement *video_queue;
  GstElement *asink;
  GstElement *vsink;
  GstElement *videosink;
  GstElement *gtkglsink;
  gboolean playing;
  gboolean terminate;
  gboolean seek_enabled;
  gboolean seek_done;
  gboolean has_media;
  gboolean is_seeking;
  gboolean seek_resume_playback;
  gboolean switching_track;
  gint64 duration;
  gint64 position;
  gint64 last_seek_request_time_us;
  gint64 last_progress_position;
  gint64 last_progress_observed_time_us;
  gint64 last_stall_recovery_time_us;
  gboolean playback_stalled;
  gint current_index;
  gchar *current_uri;
  gchar *current_directory;
  gchar *current_display_name;
  gchar *media_info_text;
  gchar *audio_sink_name;
  gchar *audio_device;
  GPtrArray *playlist_uris;
  GPtrArray *playlist_names;
  GtkWidget *window;
  GtkWidget *sink_widget;
  GtkWidget *open_button;
  GtkWidget *open_dir_button;
  GtkWidget *prev_button;
  GtkWidget *next_button;
  GtkWidget *play_button;
  GtkWidget *pause_button;
  GtkWidget *stop_button;
  GtkWidget *position_scale;
  GtkWidget *time_label;
  GtkWidget *status_label;
  GtkWidget *media_info_label;
} CustomData;

void app_init(CustomData *data);
int app_setup_pipeline(CustomData *data, int argc, char *argv[]);
int app_run(CustomData *data);
int app_open_uri(CustomData *data, const gchar *uri);
int app_open_media_file(CustomData *data, const gchar *uri);
int app_open_directory(CustomData *data, const gchar *directory);
gboolean app_play_previous(CustomData *data);
gboolean app_play_next(CustomData *data);
void app_handle_eos(CustomData *data);
void app_set_status(CustomData *data, const gchar *status);
void app_request_ui_refresh(CustomData *data);
void app_reset_playback_view(CustomData *data);
void app_debug_log(CustomData *data, const gchar *scope, const gchar *fmt, ...);
void app_cleanup(CustomData *data);

#endif
