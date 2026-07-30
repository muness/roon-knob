# One marker cannot cover two lines

The allowance below is well formed and covers the line immediately after it. The line after
*that* is not covered, so this file must still produce a violation.

<!-- rk-ident-allow-next-line: PANEL_AMOLED target="ESP32-S3-AMOLED-1.91" reason="comparison table entry for a different Waveshare product, which really does have that panel technology" -->
The ESP32-S3-AMOLED-1.91 is a different Waveshare product with an AMOLED panel.
And the round knob board has an AMOLED panel too, which is the claim being smuggled in.
