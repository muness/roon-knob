#pragma once

#include <stdbool.h>

#include "config_store.h"

void mdns_client_init(const char *hostname);           // e.g. "roon-knob-ABCD"
bool mdns_client_discover_bridge(rk_cfg_t *cfg);       // query _roonknob._tcp; if TXT "base", set cfg->bridge_base
