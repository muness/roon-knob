#pragma once

#include "wifi_manager.h"

void ui_network_register_menu(void); // adds Settings→Network screen
void ui_network_on_event(rk_net_evt_t evt, const char *ip_opt);
