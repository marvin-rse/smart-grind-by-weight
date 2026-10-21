# v1.6.0 – V2

Application image for the Waveshare 1.64" **V2** (SH8601) board, built by the
[v1.6.0 release](https://github.com/marvin-rse/smart-grind-by-weight/releases/tag/v1.6.0)
(build #6).

**Changes compared to v1.5.9:**
- No green flash on the display at power-on.
- Delete a single grind from the web interface history (also removed from lifetime statistics).
- Wi-Fi power saving setting (Settings → Connectivity) for a faster web interface.
- Web interface served compressed and cached by the browser.
- Manual uploads refuse firmware for the wrong board generation and confirm the restart.

## Install

Grinder web page → *System & updates → Manual firmware file* →
`smart-grind-by-weight-v1.6.0-waveshare-164-v2.bin` → *Upload firmware*,
or select v1.6.0 in the [web flasher](https://marvin-rse.github.io/smart-grind-by-weight/).

## Revert

Upload the image from [`../v1.5.9-v2/`](../v1.5.9-v2/) the same way.
