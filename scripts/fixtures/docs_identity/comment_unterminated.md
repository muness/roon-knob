# An unterminated HTML comment hides the rest of the file

The comment opener below is never closed. Everything after it is invisible to the scan,
including the contradiction on the last line, so this file must be reported as a structure
failure naming the opening line -- it must never be able to produce RK-IDENT-OK.

<!-- an opener with no closing marker anywhere in this file

The panel is a 360x360 round IPS LCD.

The display is a round AMOLED with 16.7 million colours and no backlight control.
