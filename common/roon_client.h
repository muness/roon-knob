#pragma once

#include "rk_cfg.h"
#include "ui.h"
#include <stdbool.h>
#include <stddef.h>

void roon_client_start(const rk_cfg_t *cfg);
void roon_client_handle_input(ui_input_event_t event);
void roon_client_set_network_ready(bool ready);
const char* roon_client_get_artwork_url(char *url_buf, size_t buf_len, int width, int height);
