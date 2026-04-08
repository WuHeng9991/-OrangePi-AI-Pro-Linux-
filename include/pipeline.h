#ifndef PIPELINE_H
#define PIPELINE_H

#include "app.h"

int pipeline_create(CustomData *data);
void pipeline_set_source_uri(CustomData *data, const gchar *uri);
int pipeline_setup_video_sink(CustomData *data);
int pipeline_build(CustomData *data);
GstBus *pipeline_get_bus(CustomData *data);
void pipeline_handle_message(CustomData *data, GstMessage *msg);
void pipeline_cleanup(CustomData *data);

#endif
