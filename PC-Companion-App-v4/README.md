# PC Companion App v4

The v4 desktop companion for AnimatedPixelClock. It brings the device's own
config portal to the PC: a web-style window (1:1 OLED preview, drag-and-drop
layout, sensor picker, number formats, quick templates, pull/push, backup),
hosted in a native window + system tray, sending sensor readings to the device
over UDP.

## Layout

| Folder | What it is |
|--------|------------|
| [`win-companion/`](win-companion/) | **Windows** app (core + PyInstaller build). Ships a prebuilt `dist/pc_stats_monitor_v4.exe`. |
| [`linux-companion/`](linux-companion/) | **Linux** app - run from source (`python3 pc_stats_monitor_v4_linux.py`), no build step. |
| [`companion-common/`](companion-common/) | Shared, OS-neutral code used by both: the localhost web server, the pywebview window host, the `webui/` (HTML/CSS/JS extracted from the firmware portal), the layout engine and the device renderer. |

The two platform folders contain only the OS-specific core (sensor discovery,
autostart, packaging); everything else is shared from `companion-common/`.

## Quick start
- **Windows:** double-click `win-companion/dist/pc_stats_monitor_v4.exe` (needs the
  WebView2 runtime - preinstalled on Win11). See [`win-companion/README.md`](win-companion/README.md).
- **Linux:** `cd linux-companion && pip install -r requirements.txt && python3 pc_stats_monitor_v4_linux.py`.
  See [`linux-companion/README.md`](linux-companion/README.md).

The UDP wire-protocol version stays `2.2` (the contract the firmware speaks); the
product/config-file version is `4.0`.

## Reachability probe

The status readout's "device online" check issues a real `GET /api/status`
every 10s. It must not be a bare TCP connect: the firmware serves HTTP from the
same single-threaded loop that renders frames, and a socket that opens without
sending a request leaves that loop waiting ~150ms - measured on the device as a
visible stutter in the clock animation, roughly one connect in four. A complete
request costs about 7ms instead.

## Turning the stats stream off

**Send PC stats to the display** (Connection page, saved as `send_pc_stats`,
default on) gates the metric packets. With it off the monitor loop skips the
sensor sweep and the UDP send entirely, so the device sees no PC and shows its
clock or scheduled ambient screen; the reachability probe still runs, and the
sensor preview in this app freezes because nothing is read. The visualizer
stream is independent, so "equalizer while music plays, clock otherwise" is
this checkbox off plus auto-start on.

## Audio visualizer stream (optional)

The Connection page has an **Audio visualizer stream** checkbox: when enabled, the
companion captures whatever the PC is playing (WASAPI loopback on Windows,
PulseAudio monitor on Linux), reduces it to a 32-band spectrum and streams it to
the device as tiny binary UDP packets (`"FFT1"` + 32 bytes, ~25/s) alongside the
stats JSON on the same port. The display shows it in its visualizer mode
(device web UI -> Display -> Audio visualizer -> Start, or `GET /api/mode/viz`).

It needs two extra packages: `pip install soundcard numpy`. Without them the
checkbox explains what to install and the rest of the app is unaffected.

Packets go out from the capture thread, so the audio device sets the cadence:
one block per 40ms, measured at 40.0ms mean and 41ms p95. Do not pace this on a
timer instead - Windows waits round up to the ~15.6ms scheduler tick, giving
~46ms periods that fall behind capture and drop frames (visible as stuttering
bars). A watchdog thread only fills gaps: with no packet for 120ms it repeats
the last frame, fades it after half a second, and gives up after six, so a
starved capture thread costs smoothness rather than blanking the display to
"No audio". `/api/status` counts those gaps as `audioVizStalls`.

The capture and watchdog threads register with MMCSS ("Pro Audio"), run at
highest thread priority, and the process goes to ABOVE_NORMAL while streaming.
Without that, a busy PC pushed p95 to 50ms and gaps to 64ms; with it, 28 pegged
cores leave the stream indistinguishable from idle. Note that the Windows API
handles here need explicit ctypes `argtypes`/`restype`: with the defaults the
64-bit pseudo-handles overflow and the calls silently do nothing.

**Start the visualizer when music plays** (same card) removes the manual step:
the streamer measures each 40ms block in dBFS and, once sound stays above the
**sound threshold** (default -45 dB) for the **start delay** (default 3s), calls
`GET /api/mode/viz` on the device. After the **stop delay** of quiet (default
20s) it calls `/api/mode/auto`. The start delay is what keeps notification pops
and other short sounds from switching the display; gaps shorter than a second do
not restart it, and the companion only releases the display if it was the one
that took it. Settings live in the config file as `audio_viz_auto`,
`audio_viz_threshold`, `audio_viz_start_delay` and `audio_viz_stop_delay`; the
live sound level is shown on the Connection page for setting the threshold.
