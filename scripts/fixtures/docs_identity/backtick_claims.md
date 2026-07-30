# Wrong claims wearing backticks

Every claim below sits inside a backtick span, which used to be enough to silence the guard.
None of these spans carries a code signal, so each one is still an assertion and must be
reported.

The panel is an `AMOLED` module. Colour capability is `16.7 million` colours.
The display controller is the `SH8601`. The compute module is an `ESP32-S3-WROOM-1`.
The product is the `Waveshare ESP32-S3 Touch AMOLED 1.8"` board.
A stale note in another tree read `Display controller | SH8601` and another read
`no backlight required`.
