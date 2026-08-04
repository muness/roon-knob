#/**/inc\
lude/**/"../idf_app/main/new_target_ui.h"

void controller_boundary_negative_fixture(void) {
    /* Forward declaration tricks must not bypass owner-private storage checks. */
    platform_storage_write();
}
