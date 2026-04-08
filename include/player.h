#ifndef PLAYER_H
#define PLAYER_H

#include "app.h"

GstStateChangeReturn player_play(CustomData *data);
GstStateChangeReturn player_pause(CustomData *data);
GstStateChangeReturn player_stop(CustomData *data);
gboolean player_seek(CustomData *data, gint64 position);
gboolean player_query_position(CustomData *data, gint64 *position);
gboolean player_query_duration(CustomData *data, gint64 *duration);

#endif
