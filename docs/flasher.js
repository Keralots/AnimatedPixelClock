// AnimatedPixelClock Web Flasher - client logic.
// Builds an ESP Web Tools manifest on the fly for the chosen board and keeps the
// install button, specs and board photo in sync. Each board in BOARDS below has a
// clock-only image and a "-games" image with Bluetooth game mode (VARIANTS), all
// driving the 128x64 HUB75 matrix. The picker lists every board once per variant,
// one section each, with "<board>/<variant>" option values.

const BOARDS = {
  supermini: {
    label: 'ESP32-S3-Zero / Super Mini (4MB, USB-C)',
    chipFamily: 'ESP32-S3',
    firmware: 'supermini',              // AnimatedPixelClock-supermini-<ver>-Full.bin (shared 4MB image)
    board: 'ESP32-S3-Zero / Super Mini',
    note: 'The compact 4MB build - the same image runs on the Waveshare ESP32-S3-Zero and an ESP32-S3 Super Mini. One USB-C charger powers the board and both panels. Native USB: if the serial port does not appear, hold BOOT while plugging in.',
  },
  wroom: {
    label: 'ESP32-S3-WROOM devkit (16MB)',
    chipFamily: 'ESP32-S3',
    firmware: 'wroom',                  // AnimatedPixelClock-wroom-<ver>-Full.bin
    board: 'ESP32-S3-WROOM-1 (N16R8)',
    note: 'The full-size 16MB devkit has more storage for custom GIF animations. Follow the wiring guide for the panel power connections.',
  },
  waveshare: {
    label: 'Waveshare ESP32-S3-RGB-Matrix (32MB)',
    chipFamily: 'ESP32-S3',
    firmware: 'waveshare',              // AnimatedPixelClock-waveshare-<ver>-Full.bin
    board: 'Waveshare ESP32-S3-RGB-Matrix',
    note: 'The purpose-built HUB75 driver board: the panel header and output buffers are onboard, so no per-GPIO wiring is needed - you still connect the ribbon cables and panel power. Its 32MB flash leaves 23MB for custom GIF animations. Two USB-C ports, one for programming and one for power. Native USB: if the serial port does not appear, hold BOOT while plugging in. Follow Waveshare\'s own connection guide for this board.',
  },
};

// Bluetooth reserves internal memory for good once it is in the firmware, so the
// clock-only image is the default.
const VARIANTS = {
  clock: { label: 'Clock only (recommended)', suffix: '', spec: 'clock only' },
  games: { label: 'Clock + game mode (Bluetooth Xbox pad, about 29KB less free memory)', suffix: '-games', spec: 'clock + games' },
};

const DEFAULT_BOARD = 'supermini';
const DEFAULT_VARIANT = 'clock';
const DISPLAY = 'HUB75 · 128×64 RGB';
const PROBE_TIMEOUT_MS = 4000;

let _version = null;
let _currentManifestUrl = null;
const _imageState = {};  // "<board>/<variant>" -> published | missing | unknown

async function loadVersion() {
  const r = await fetch('firmware/latest/VERSION', { cache: 'no-cache' });
  if (!r.ok) throw new Error(`firmware/latest/VERSION returned HTTP ${r.status}`);
  const text = (await r.text()).trim();
  if (!text) throw new Error('VERSION file is empty');
  return text;
}

function imageUrl(boardId, variantId, version) {
  const fid = BOARDS[boardId].firmware + VARIANTS[variantId].suffix;
  return new URL(`firmware/latest/AnimatedPixelClock-${fid}-${version}-Full.bin`, location.href).href;
}

function buildManifest(boardId, variantId, version) {
  const board = BOARDS[boardId];
  const binUrl = imageUrl(boardId, variantId, version);
  return {
    name: 'AnimatedPixelClock',
    version,
    // No new_install_prompt_erase: without it ESP Web Tools always erases the
    // whole flash. Keeping data is pointless here - the merged image fills the
    // NVS gap with 0xFF anyway - and existing clocks update via OTA instead.
    // After flashing, wait up to 15s for the device to boot, then probe for
    // Improv-Serial. The firmware exposes Improv only on first boot (no stored
    // WiFi credentials), so this kicks in for fresh installs and lets ESP Web
    // Tools show its in-browser "Configure WiFi" dialog (section 02). The
    // WiFiManager AP portal stays up in parallel as a fallback.
    new_install_improv_wait_time: 15,
    builds: [{
      chipFamily: board.chipFamily,
      parts: [{ path: binUrl, offset: 0 }],
    }],
  };
}

function manifestBlobUrl(boardId, variantId, version) {
  if (_currentManifestUrl) {
    URL.revokeObjectURL(_currentManifestUrl);
    _currentManifestUrl = null;
  }
  const blob = new Blob([JSON.stringify(buildManifest(boardId, variantId, version))], { type: 'application/json' });
  _currentManifestUrl = URL.createObjectURL(blob);
  return _currentManifestUrl;
}

function populateBoardSelect() {
  const sel = document.getElementById('board-select');
  if (!sel) return;
  for (const [variantId, variant] of Object.entries(VARIANTS)) {
    const group = document.createElement('optgroup');
    group.label = variant.label;
    for (const [boardId, info] of Object.entries(BOARDS)) {
      const opt = document.createElement('option');
      opt.value = `${boardId}/${variantId}`;
      opt.textContent = info.label;
      group.appendChild(opt);
    }
    sel.appendChild(group);
  }
  sel.value = `${DEFAULT_BOARD}/${DEFAULT_VARIANT}`;
}

function parsePick(value) {
  const [boardId, variantId] = (value || '').split('/');
  return BOARDS[boardId] && VARIANTS[variantId] ? [boardId, variantId] : [DEFAULT_BOARD, DEFAULT_VARIANT];
}

function renderVariant(variantId) {
  const el = document.getElementById('spec-variant');
  if (el) el.textContent = VARIANTS[variantId].spec;
}

function renderSpecs(boardId) {
  const info = BOARDS[boardId];
  document.getElementById('spec-chip').textContent = info.chipFamily;
  const boardEl = document.getElementById('spec-board');
  if (boardEl) boardEl.textContent = info.board;
  document.getElementById('spec-display').textContent = DISPLAY;
  const img = document.getElementById('board-img');
  if (img) {
    // Eager: a lazy image in a collapsed step never loads, so its onerror never
    // fires and a board without a photo would show a broken-image icon.
    img.loading = 'eager';
    img.onerror = () => { img.hidden = true; };
    img.hidden = false;
    img.src = `img/boards/${boardId}.jpg`;
    img.alt = info.board;
  }
  const note = document.getElementById('board-note-text');
  if (note) note.textContent = info.note;
}

function renderInstallButton(boardId, variantId, version) {
  // ESP Web Tools caches the manifest on first render - recreate the element on
  // every board switch so the new board's manifest is picked up.
  const slot = document.getElementById('install-slot');
  slot.innerHTML = '';
  const btn = document.createElement('esp-web-install-button');
  btn.setAttribute('manifest', manifestBlobUrl(boardId, variantId, version));

  const fallback = document.createElement('span');
  fallback.setAttribute('slot', 'unsupported');
  fallback.className = 'unsupported';
  fallback.textContent = 'Your browser does not support Web Serial. Use Chrome or Edge on desktop.';
  btn.appendChild(fallback);

  const notAllowed = document.createElement('span');
  notAllowed.setAttribute('slot', 'not-allowed');
  notAllowed.className = 'unsupported';
  notAllowed.textContent = 'Web Serial requires a secure context (HTTPS). Open this page from https://.';
  btn.appendChild(notAllowed);

  slot.appendChild(btn);
}

function showStatus(message, kind) {
  const line = document.getElementById('status-line');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function showVersion(version) {
  document.getElementById('spec-version').textContent = version;
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = version;
  const releaseUrl = 'https://github.com/Keralots/AnimatedPixelClock/releases';
  const download = document.getElementById('companion-download');
  if (download) download.href = `${releaseUrl}/download/${encodeURIComponent(version)}/pc_stats_monitor_v4.exe`;
  const notes = document.getElementById('release-downloads');
  if (notes) notes.href = `${releaseUrl}/tag/${encodeURIComponent(version)}`;
  const label = document.getElementById('companion-release');
  if (label) label.textContent = `Included in ${version}`;
}

function showVersionError(err) {
  document.getElementById('spec-version').textContent = 'unavailable';
  const rail = document.getElementById('rail-version');
  if (rail) rail.textContent = 'unavailable';
  showStatus(
    `Could not load firmware version (${err.message}). The site may be mid-deploy, try again in a minute.`,
    'error',
  );
  document.getElementById('install-slot').innerHTML = '';
}

// Only a definite 404/410 counts as "not published yet". A blocked HEAD, a 405,
// a 5xx or a timeout is inconclusive, and dropping a board on those grounds would
// hide a perfectly good image.
async function probeImage(url) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), PROBE_TIMEOUT_MS);
  try {
    const r = await fetch(url, { method: 'HEAD', cache: 'no-cache', signal: ctrl.signal });
    if (r.ok) return 'published';
    return (r.status === 404 || r.status === 410) ? 'missing' : 'unknown';
  } catch {
    return 'unknown';
  } finally {
    clearTimeout(timer);
  }
}

// A board or variant is only offered once its Full.bin exists for the published
// version - otherwise picking it hands ESP Web Tools a 404.
async function pruneUnpublishedBoards(version) {
  const sel = document.getElementById('board-select');
  if (!sel) return;
  const keys = Object.keys(BOARDS).flatMap((b) => Object.keys(VARIANTS).map((v) => [b, v]));
  await Promise.all(keys.map(async ([b, v]) => {
    _imageState[`${b}/${v}`] = await probeImage(imageUrl(b, v, version));
  }));
  // Every image missing means the VERSION file and the published binaries
  // disagree. Say so rather than silently emptying the picker.
  if (Object.values(_imageState).every((state) => state === 'missing')) {
    showStatus(`No firmware images published for ${version} yet. The site may be mid-deploy, try again in a minute.`, 'error');
    return;
  }
  for (const [key, state] of Object.entries(_imageState)) {
    if (state === 'missing') sel.querySelector(`option[value="${key}"]`)?.remove();
  }
  for (const group of sel.querySelectorAll('optgroup')) {
    if (!group.children.length) group.remove();
  }
  if (!sel.value) sel.selectedIndex = 0;
}

function checkBrowserSupport() {
  if (!('serial' in navigator)) {
    document.getElementById('browser-callout').classList.add('show');
  }
}

async function init() {
  checkBrowserSupport();
  populateBoardSelect();
  renderSpecs(DEFAULT_BOARD);
  wireMonitor();

  try {
    _version = await loadVersion();
  } catch (err) {
    showVersionError(err);
    return;
  }

  showVersion(_version);
  await pruneUnpublishedBoards(_version);

  const sel = document.getElementById('board-select');
  const refresh = () => {
    const [boardId, variantId] = parsePick(sel && sel.value);
    renderSpecs(boardId);
    renderVariant(variantId);
    renderInstallButton(boardId, variantId, _version);
  };
  refresh();
  if (sel) sel.addEventListener('change', refresh);
}

// ────────── 04 serial monitor ──────────
// Reads the device's serial stream at 115200 baud and appends decoded text to
// <pre id="monitor-output">. Independent of the install button — only one
// program can hold the port at a time, so don't click Install while connected.

let _monitorPort = null;
let _monitorReader = null;
let _monitorReadLoopRunning = false;

async function monitorConnect() {
  if (_monitorPort) return;
  let port;
  try {
    port = await navigator.serial.requestPort();
  } catch (err) {
    if (err && err.name === 'NotFoundError') return; // user cancelled picker
    setMonitorStatus(`Could not pick a port: ${err.message}`, 'error');
    return;
  }
  try {
    await port.open({ baudRate: 115200 });
  } catch (err) {
    setMonitorStatus(`Could not open the port: ${err.message}. Close other monitors and try again.`, 'error');
    return;
  }
  _monitorPort = port;
  toggleMonitorButtons(true);
  setMonitorStatus('Connected. Reading from device…', 'ok');
  monitorReadLoop().catch((err) => setMonitorStatus(`Read error: ${err.message}`, 'error'));
}

async function monitorDisconnect() {
  if (!_monitorPort) return;
  setMonitorStatus('Disconnecting…');
  try { if (_monitorReader) await _monitorReader.cancel(); } catch (_) {}
  const startedAt = Date.now();
  while (_monitorReadLoopRunning && Date.now() - startedAt < 1000) {
    await new Promise((r) => setTimeout(r, 20));
  }
  try { await _monitorPort.close(); } catch (_) {}
  _monitorPort = null;
  _monitorReader = null;
  toggleMonitorButtons(false);
  setMonitorStatus('Disconnected.');
}

async function monitorReadLoop() {
  _monitorReadLoopRunning = true;
  const decoder = new TextDecoder();
  try {
    if (!_monitorPort || !_monitorPort.readable) return;
    const reader = _monitorPort.readable.getReader();
    _monitorReader = reader;
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value && value.byteLength) appendMonitorOutput(decoder.decode(value, { stream: true }));
      }
    } finally {
      try { reader.releaseLock(); } catch (_) {}
      _monitorReader = null;
    }
  } finally {
    _monitorReadLoopRunning = false;
  }
}

function appendMonitorOutput(text) {
  const out = document.getElementById('monitor-output');
  const wasEmpty = out.textContent.length === 0;
  const atBottom = out.scrollHeight - out.clientHeight - out.scrollTop < 4;
  out.appendChild(document.createTextNode(text));
  if (out.textContent.length > 200000) out.textContent = out.textContent.slice(-150000);
  if (atBottom) out.scrollTop = out.scrollHeight;
  if (wasEmpty) setMonitorBufferButtons(true);
}

function monitorExport() {
  const out = document.getElementById('monitor-output');
  const text = out.textContent;
  if (!text) return;
  const ts = new Date().toISOString().replace(/[:.]/g, '-').replace('Z', '');
  const blob = new Blob([text], { type: 'text/plain;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `animatedpixelclock-serial-${ts}.txt`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function monitorClear() {
  document.getElementById('monitor-output').textContent = '';
  setMonitorBufferButtons(false);
}

function setMonitorBufferButtons(hasContent) {
  document.getElementById('monitor-export').disabled = !hasContent;
  document.getElementById('monitor-clear').disabled = !hasContent;
}

function setMonitorStatus(message, kind) {
  const line = document.getElementById('monitor-status');
  line.textContent = message || '';
  line.className = 'status-line' + (kind ? ' ' + kind : '');
}

function toggleMonitorButtons(connected) {
  document.getElementById('monitor-connect').disabled = connected;
  document.getElementById('monitor-disconnect').disabled = !connected;
}

function wireMonitor() {
  const connectBtn = document.getElementById('monitor-connect');
  if (!('serial' in navigator)) {
    connectBtn.disabled = true;
    setMonitorStatus('Web Serial is unavailable in this browser — use desktop Chrome or Edge.', 'warn');
    return;
  }
  connectBtn.addEventListener('click', monitorConnect);
  document.getElementById('monitor-disconnect').addEventListener('click', monitorDisconnect);
  document.getElementById('monitor-export').addEventListener('click', monitorExport);
  document.getElementById('monitor-clear').addEventListener('click', monitorClear);
}

init();
