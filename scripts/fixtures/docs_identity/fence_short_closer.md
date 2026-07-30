# A short run cannot close a longer fence

The block below opens with four backticks and the author closed it with three. A closing fence
must be at least as long as the opener, so Markdown keeps the four-backtick fence open and the
contradiction on the last line is inside it. The checker must report an unterminated fence
rather than accepting the file.

````text
Nested example: ```c is the shape being documented here.
```

The display is a round AMOLED with 16.7 million colours and no backlight control.
