/* HALO uses its full-screen navigation settings. */
#include "ui.h"
#include "halo_navigation.h"

void ui_show_settings(void) { halo_navigation_settings(); }
void ui_hide_settings(void) { halo_navigation_close_settings(); }
bool ui_is_settings_visible(void) { return halo_navigation_settings_visible(); }
