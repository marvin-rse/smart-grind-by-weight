# v1.6.3 – V2

Application image for the Waveshare 1.64" **V2** (SH8601) board, built by the
[v1.6.3 release](https://github.com/marvin-rse/smart-grind-by-weight/releases/tag/v1.6.3)
(build #9).

**Changes compared to v1.6.2:**
- Reliable updates while Bluetooth is active (one automatic restart without Bluetooth).
- GitHub downloads resume after a dropped connection.
- Upload shows each step and real progress.
- Diagnostic log survives restarts; Wi-Fi signal shown on the system page.

## Install

Grinder web page → *System & updates → Manual firmware file* →
`smart-grind-by-weight-v1.6.3-waveshare-164-v2.bin` → *Upload firmware*,
or *Install v1.6.3* under Automatic updates. Keep the page open until it reports
the new build.

## Revert

Upload the image from [`../v1.6.2-v2/`](../v1.6.2-v2/) the same way.
