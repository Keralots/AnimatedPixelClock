"""Audio spectrum streamer for the display's visualizer mode.

Captures what the PC is playing (WASAPI loopback on Windows, PulseAudio
monitor on Linux - both via the `soundcard` package), reduces each ~40ms
block to 32 log-spaced frequency bands, and sends them to the device as a
tiny binary UDP packet: b"FFT1" + 32 amplitude bytes, ~25 packets/s to the
same port the stats JSON uses. The device only shows them in its forced
visualizer mode (/api/mode/viz), so streaming is harmless otherwise.

With auto-start enabled the streamer also switches the display itself: it
watches block loudness and calls /api/mode/viz once sound has been playing
for the arming delay (so a notification pop cannot trigger it), then
/api/mode/auto after the configured quiet period.

Optional feature: `soundcard` and `numpy` may be missing, in which case
AVAILABLE is False and ensure() is a no-op (the web UI explains how to
install them). Everything is managed through ensure(config), which the
server calls after any config change and app_window calls at startup.
"""

import socket
import threading
import time
from urllib.request import urlopen

try:
    import numpy as np
    import soundcard as sc
    AVAILABLE = True
    _IMPORT_ERROR = ""
except Exception as e:  # missing package OR no audio backend on this system
    np = None
    sc = None
    AVAILABLE = False
    _IMPORT_ERROR = str(e)

RATE = 48000
FRAMES = 1920            # 40 ms blocks -> 25 packets/s
FFT_N = 2048
BANDS = 32
F_LO, F_HI = 50.0, 16000.0

# AGC: the reference level tracks the loudest recent band and decays slowly,
# so quiet and loud music both use the full bar height.
AGC_RANGE_DB = 38.0      # dB span mapped onto 0..255
AGC_DECAY_DB_PER_S = 1.5
AGC_FLOOR_DB = -55.0

# Auto-start defaults (dBFS / seconds).
AUTO_THRESHOLD_DB = -45.0
AUTO_START_DELAY = 3.0
AUTO_STOP_DELAY = 20.0
AUTO_GAP_S = 1.0         # quiet gaps shorter than this do not re-arm
SILENT_DB = -120.0


def clamp_auto(threshold_db, start_delay, stop_delay):
    """Clamp auto-start settings to the ranges the UI offers."""
    return (min(-10.0, max(-80.0, float(threshold_db))),
            min(60.0, max(0.0, float(start_delay))),
            min(3600.0, max(1.0, float(stop_delay))))


class VizAutoTrigger:
    """Turns a stream of block levels into force/release decisions.

    No numpy/soundcard use, so it stays testable on any machine."""

    def __init__(self):
        self.enabled = False
        self.threshold_db = AUTO_THRESHOLD_DB
        self.start_delay = AUTO_START_DELAY
        self.stop_delay = AUTO_STOP_DELAY
        self.forced = False      # True while we hold the display in viz mode
        self._loud_since = None
        self._last_loud = None

    def configure(self, enabled, threshold_db, start_delay, stop_delay):
        """Apply settings. True when the display must be handed back."""
        self.enabled = bool(enabled)
        self.threshold_db, self.start_delay, self.stop_delay = clamp_auto(
            threshold_db, start_delay, stop_delay)
        if not self.enabled:
            return self.release()
        return False

    def feed(self, level_db, now):
        """Consume one block level. Returns 'viz', 'auto' or None."""
        if not self.enabled:
            return None
        if level_db >= self.threshold_db:
            # A gap longer than AUTO_GAP_S counts as a new sound, so short
            # pops each restart the arming delay.
            if self._last_loud is None or (now - self._last_loud) > AUTO_GAP_S:
                self._loud_since = now
            self._last_loud = now
            if not self.forced and (now - self._loud_since) >= self.start_delay:
                self.forced = True
                return "viz"
            return None
        if self.forced and self._last_loud is not None and \
                (now - self._last_loud) >= self.stop_delay:
            self.forced = False
            self._loud_since = None
            return "auto"
        return None

    def release(self):
        """Forget armed/forced state. True when we still held the display."""
        was_forced = self.forced
        self.forced = False
        self._loud_since = None
        self._last_loud = None
        return was_forced


class SpectrumStreamer(threading.Thread):
    def __init__(self, ip, port):
        super().__init__(daemon=True, name="audio-spectrum")
        self._lock = threading.Lock()
        self._ip = ip
        self._port = int(port)
        self._stop = threading.Event()
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.last_sent = 0.0
        self.last_error = ""
        self.last_level_db = SILENT_DB
        self.auto = VizAutoTrigger()
        self.auto_error = ""

        # Precompute the window and the FFT-bin span of each log band.
        self._window = np.hanning(FRAMES).astype(np.float32)
        edges = F_LO * (F_HI / F_LO) ** (np.arange(BANDS + 1) / BANDS)
        bin_hz = RATE / float(FFT_N)
        self._band_bins = []
        for i in range(BANDS):
            lo = int(edges[i] / bin_hz)
            hi = max(lo + 1, int(edges[i + 1] / bin_hz))
            self._band_bins.append((lo, hi))
        self._agc_ref = AGC_FLOOR_DB

    def set_target(self, ip, port):
        with self._lock:
            self._ip = ip
            self._port = int(port)

    def set_auto(self, enabled, threshold_db, start_delay, stop_delay):
        with self._lock:
            release = self.auto.configure(enabled, threshold_db,
                                          start_delay, stop_delay)
        if release:
            self._send_mode("auto")

    def stop(self):
        self._stop.set()

    def _send_mode(self, mode, blocking=False):
        """Switch the device's display mode without blocking the capture loop."""
        with self._lock:
            ip = self._ip
        if not ip:
            return

        def call():
            try:
                with urlopen("http://%s/api/mode/%s" % (ip, mode), timeout=4) as r:
                    r.read(256)
                self.auto_error = ""
            except Exception as err:
                self.auto_error = "%s: %s" % (mode, err)

        if blocking:
            call()
        else:
            threading.Thread(target=call, daemon=True,
                             name="viz-mode-%s" % mode).start()

    def _process_block(self, mono):
        spec = np.abs(np.fft.rfft(mono * self._window, n=FFT_N))
        amps = np.empty(BANDS, dtype=np.float32)
        for i, (lo, hi) in enumerate(self._band_bins):
            amps[i] = np.sqrt(np.mean(spec[lo:hi] ** 2))
        db = 20.0 * np.log10(amps + 1e-7)

        peak = float(db.max())
        dt = FRAMES / float(RATE)
        self._agc_ref = max(self._agc_ref - AGC_DECAY_DB_PER_S * dt,
                            peak, AGC_FLOOR_DB)
        norm = (db - (self._agc_ref - AGC_RANGE_DB)) / AGC_RANGE_DB
        return np.clip(norm * 255.0, 0, 255).astype(np.uint8)

    def _block_level_db(self, mono):
        """Block loudness in dBFS. AGC-free, so the auto-start threshold
        means the same thing whatever the music's volume."""
        rms = float(np.sqrt(np.mean(mono ** 2)))
        if rms <= 0.0:
            return SILENT_DB
        return max(SILENT_DB, 20.0 * np.log10(rms))

    def run(self):
        while not self._stop.is_set():
            try:
                # Re-resolve the default output each (re)open, so switching
                # headphones/speakers is picked up on the next reconnect.
                spk = sc.default_speaker()
                mic = sc.get_microphone(id=str(spk.name), include_loopback=True)
                blocks_since_check = 0
                with mic.recorder(samplerate=RATE, blocksize=FRAMES) as rec:
                    while not self._stop.is_set():
                        data = rec.record(numframes=FRAMES)
                        mono = data.mean(axis=1) if data.ndim > 1 else data
                        mono = mono.astype(np.float32)
                        bands = self._process_block(mono)
                        self.last_level_db = self._block_level_db(mono)
                        with self._lock:
                            target = (self._ip, self._port)
                            action = self.auto.feed(self.last_level_db,
                                                    time.time())
                        if action:
                            self._send_mode(action)
                        try:
                            self._sock.sendto(b"FFT1" + bands.tobytes(), target)
                            self.last_sent = time.time()
                        except OSError:
                            pass
                        # Every ~10s, check whether the default output moved.
                        blocks_since_check += 1
                        if blocks_since_check >= 250:
                            blocks_since_check = 0
                            try:
                                if str(sc.default_speaker().name) != str(spk.name):
                                    break  # reopen on the new device
                            except Exception:
                                break
            except Exception as e:
                self.last_error = str(e)
                # Capture device busy/missing - retry without spinning.
                self._stop.wait(3.0)
        with self._lock:
            release = self.auto.release()
        if release:
            self._send_mode("auto", blocking=True)  # hand the display back
        try:
            self._sock.close()
        except Exception:
            pass


_lock = threading.Lock()
_streamer = None


def auto_settings(config):
    """Clamped auto-start settings from a config dict."""
    config = config or {}
    threshold, start, stop = clamp_auto(
        config.get("audio_viz_threshold", AUTO_THRESHOLD_DB),
        config.get("audio_viz_start_delay", AUTO_START_DELAY),
        config.get("audio_viz_stop_delay", AUTO_STOP_DELAY))
    return bool(config.get("audio_viz_auto")), threshold, start, stop


def ensure(config):
    """Start/stop/redirect the streamer to match the config. Safe to call often."""
    global _streamer
    if not AVAILABLE:
        return
    config = config or {}
    ip = (config.get("esp32_ip") or "").strip()
    port = int(config.get("udp_port") or 4210)
    want = bool(config.get("audio_viz")) and bool(ip)
    auto = auto_settings(config)
    with _lock:
        if _streamer is not None and not _streamer.is_alive():
            _streamer = None
        if want and _streamer is None:
            _streamer = SpectrumStreamer(ip, port)
            _streamer.set_auto(*auto)
            _streamer.start()
        elif want and _streamer is not None:
            _streamer.set_target(ip, port)
            _streamer.set_auto(*auto)
        elif not want and _streamer is not None:
            _streamer.stop()
            _streamer = None


def stop_all():
    global _streamer
    with _lock:
        if _streamer is not None:
            _streamer.stop()
            _streamer = None


def status():
    """Fields merged into /api/status for the web UI."""
    with _lock:
        running = _streamer is not None and _streamer.is_alive()
        sending = running and (time.time() - _streamer.last_sent) < 2.0
        err = _streamer.last_error if _streamer is not None else ""
        auto_on = running and _streamer.auto.enabled
        forced = running and _streamer.auto.forced
        level = _streamer.last_level_db if running else SILENT_DB
        auto_err = _streamer.auto_error if _streamer is not None else ""
    return {
        "audioVizAvailable": AVAILABLE,
        "audioVizReason": _IMPORT_ERROR,
        "audioVizEnabled": running,
        "audioVizSending": sending,
        "audioVizError": err,
        "audioVizAuto": auto_on,
        "audioVizForced": forced,
        "audioVizLevel": round(float(level), 1),
        "audioVizAutoError": auto_err,
    }
