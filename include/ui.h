#ifndef UI_H
#define UI_H

#include "app.h"

void create_ui(CustomData *data);
void ui_set_time_label(CustomData *data, gint64 position, gint64 duration);
void ui_set_status_label(CustomData *data, const gchar *status);
void ui_set_scale_range(CustomData *data, gint64 duration);
void ui_set_scale_value(CustomData *data, gint64 position);
void ui_set_media_info_label(CustomData *data, const gchar *text);
void ui_update_transport_buttons(CustomData *data);

#endif
