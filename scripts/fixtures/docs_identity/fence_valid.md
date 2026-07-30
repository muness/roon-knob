# Valid Markdown fences stay exempt

Every fence below is well formed: an info string on the opener, a tilde fence containing a
backtick fence, a four-backtick fence closed by five backticks, and an indented fence. Quoting
the guarded wordings as source is exactly what the exemption is for, so this file must produce
zero violations.

```c
// Panel: 360x360 round AMOLED, 16.7 million colors, no backlight, ESP32-S3-WROOM-1
// Press the knob to open the zone picker; backlight starts at 50% duty
```

~~~markdown
An inner backtick fence, which is why the outer fence uses tildes:

```text
The display controller IC is the SH8601, per an earlier draft.
```
~~~

````text
A four-backtick fence closed by five, so the three-backtick line below is content:
```
The fitted touch controller is a CST816D and the panel is an AMOLED.
`````

   ```text
   Indented three spaces, which is still a fence: no dimming control is possible.
   ```

The panel is a 360x360 round IPS LCD; the vendor declares an ST77916 driver IC and 262K
colours, and the firmware drives an LEDC PWM backlight on GPIO 47.
