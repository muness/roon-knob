# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Arduino 3.3.11 declares an unconditional S3 ESP-SR manifest dependency.
# Its selective ESP_SR library is disabled in our board profiles. Register an
# empty interface for that unused edge: no source, headers, runtime or models.
# The parent project adds this directory only with HIPHI_KIZZ_WAKE_WORD=OFF.
idf_build_get_property(kizz_wake_word_enabled HIPHI_KIZZ_WAKE_WORD)
if(kizz_wake_word_enabled)
    message(FATAL_ERROR "The disabled ESP-SR adapter must not be used with wake ON")
endif()
idf_component_register()
