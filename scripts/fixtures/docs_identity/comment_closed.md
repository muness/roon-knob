# A closed HTML comment is still an exemption

Commenting a superseded sentence out is a legitimate edit, and the checker reads prose rather
than comments. Both a one-line comment and a comment that spans lines must stay exempt, and the
prose after the closing marker must still be scanned. This file must produce zero violations.

<!-- The panel is a round AMOLED with 16.7 million colours and no backlight. -->

<!--
An earlier draft of this page said the display controller IC is the SH8601 and that the
ESP32-S3-WROOM-1 module carries it. Press the knob to open the zone picker. Backlight starts
at 50 % duty.
-->

Text and <!-- a guarded AMOLED aside --> more text on one line is exempt in the middle only.

The panel is a 360x360 round IPS LCD; the vendor declares an ST77916 driver IC and 262K
colours, and the firmware drives an LEDC PWM backlight on GPIO 47.
