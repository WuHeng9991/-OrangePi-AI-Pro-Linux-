#include <gtk/gtk.h>
#include <gst/gst.h>

#include "app.h"

int main(int argc, char *argv[]) {
  CustomData data;

  gtk_init(&argc, &argv);
  gst_init(&argc, &argv);

  app_init(&data);

  if (app_setup_pipeline(&data, argc, argv) != 0) {
    app_cleanup(&data);
    return -1;
  }

  if (app_run(&data) != 0) {
    app_cleanup(&data);
    return -1;
  }

  app_cleanup(&data);
  return 0;
}
