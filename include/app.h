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
  gint64 duration;
  gint64 position;
  gchar *current_uri;
  GtkWidget *window;
  GtkWidget *sink_widget;
  GtkWidget *open_button;
  GtkWidget *play_button;
  GtkWidget *pause_button;
  GtkWidget *stop_button;
  GtkWidget *position_scale;
  GtkWidget *time_label;
  GtkWidget *status_label;
} CustomData;

void app_init(CustomData *data);
int app_setup_pipeline(CustomData *data, int argc, char *argv[]);
int app_run(CustomData *data);
int app_open_uri(CustomData *data, const gchar *uri);
void app_set_status(CustomData *data, const gchar *status);
void app_request_ui_refresh(CustomData *data);
void app_reset_playback_view(CustomData *data);
void app_cleanup(CustomData *data);

#endif
