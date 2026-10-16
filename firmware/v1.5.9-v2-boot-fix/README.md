# v1.5.9 V2 – boot fix (untested)

Custom build of v1.5.9 for the Waveshare 1.64" **V2** (SH8601) board.

**Change:** The display no longer flashes green for about half a second at power-on.
The backlight now stays off until the first UI frame has been drawn (with a 3 s fallback),
instead of turning on while the panel memory still contains random data.

**Status:** Built successfully, **not yet tested on hardware**.

## Install

Grinder web page → *System & updates → Manual firmware file* → select
`smart-grind-by-weight-v1.5.9-v2-boot-fix.bin` → *Upload firmware*.

## Revert

Upload the stock image from [`../v1.5.9-v2/`](../v1.5.9-v2/) the same way.
