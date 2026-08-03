/* Frame's mature portal is shared directly while RLCD owns presentation. */
#include "rlcd_ui.h"

void eink_ui_post_network_status(const char *status) {
    rlcd_ui_set_network_status(status);
}
