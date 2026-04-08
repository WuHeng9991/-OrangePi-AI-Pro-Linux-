#ifndef UI_H
#define UI_H

#include "app.h"

void create_ui(CustomData *data);
void ui_set_time_label(CustomData *data, gint64 position, gint64 duration);
void ui_set_status_label(CustomData *data, const gchar *status);
void ui_set_scale_range(CustomData *data, gint64 duration);
void ui_set_scale_value(CustomData *data, gint64 position);
void *gtk_main_loop(void *data);

#endif
