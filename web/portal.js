(function () {
'use strict';
var $  = function (s, r) { return (r || document).querySelector(s); };
var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
var navToggle = $('#navToggle'), navScrim = $('#navScrim');
function setNav(open) {
document.documentElement.classList.toggle('nav-open', open);
if (navToggle) navToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
}
function closeNav() { setNav(false); }
if (navToggle) navToggle.addEventListener('click', function () { setNav(!document.documentElement.classList.contains('nav-open')); });
if (navScrim) navScrim.addEventListener('click', closeNav);
document.addEventListener('keydown', function (e) { if (e.key === 'Escape') closeNav(); });
var navItems = $$('.nav-item');
var pages = $$('.page');
var crumb = $('#crumb');
function showPage(key) {
pages.forEach(function (p) { p.classList.toggle('active', p.dataset.page === key); });
navItems.forEach(function (n) { n.classList.toggle('active', n.dataset.nav === key); });
var active = navItems.filter(function (n) { return n.dataset.nav === key; })[0];
if (active && crumb) crumb.textContent = (active.firstChild ? active.firstChild.textContent : active.textContent).trim();
window.scrollTo(0, 0);
closeNav();
try { localStorage.setItem('soled_section', key); } catch (e) {}
}
navItems.forEach(function (n) { n.addEventListener('click', function () { showPage(n.dataset.nav); }); });
try { var s = localStorage.getItem('soled_section'); if (s && $('[data-page="' + s + '"]')) showPage(s); } catch (e) {}
var accSw = $$('.acc-sw');
function setAccent(acc) {
document.documentElement.setAttribute('data-accent', acc);
accSw.forEach(function (b) { b.classList.toggle('on', b.dataset.acc === acc); });
try { localStorage.setItem('soled_accent', acc); } catch (e) {}
}
accSw.forEach(function (b) { b.addEventListener('click', function () { setAccent(b.dataset.acc); }); });
try { var a = localStorage.getItem('soled_accent'); if (a) setAccent(a); } catch (e) {}
var modeBtns = $$('.mode-toggle button');
function setMode(mode) {
document.documentElement.setAttribute('data-mode', mode);
modeBtns.forEach(function (b) { b.classList.toggle('on', b.dataset.mode === mode); });
var meta = $('meta[name="theme-color"]'); if (meta) meta.setAttribute('content', mode === 'dark' ? '#161512' : '#f4f0e7');
try { localStorage.setItem('soled_mode', mode); } catch (e) {}
}
modeBtns.forEach(function (b) { b.addEventListener('click', function () { setMode(b.dataset.mode); }); });
try { var m = localStorage.getItem('soled_mode'); if (m) setMode(m); } catch (e) {}
var saveMeta = $('#saveMeta'), saveBtn = $('#saveBtn');
var loaded = false;   // true once /api/portal has filled the form
function markDirty() { if (loaded && saveMeta) { saveMeta.classList.remove('clean'); $('.txt', saveMeta).textContent = 'Unsaved changes'; } }
function markClean(txt) { if (saveMeta) { saveMeta.classList.add('clean'); $('.txt', saveMeta).textContent = txt || 'All saved'; } }
var form = $('#cfgForm');
form.addEventListener('input', markDirty);
form.addEventListener('change', markDirty);
function fmtRange(inp) {
var span = $('.range-val[data-for="' + inp.id + '"]');
if (!span) return;
var suf = inp.dataset.suffix || '';
if (inp.dataset.pct) { span.textContent = Math.round((inp.value / 255) * 100) + '%'; return; }
var div = parseFloat(inp.dataset.div || '1');
var fixed = parseInt(inp.dataset.fixed || '0', 10);
span.textContent = (inp.value / div).toFixed(fixed) + suf;
}
$$('input[type="range"]').forEach(function (inp) { fmtRange(inp); inp.addEventListener('input', function () { fmtRange(inp); }); });
function toggle(el, on) { if (el) el.style.display = on ? '' : 'none'; }
var syncs = [];   // what shows or hides with a value: run again once the values are in
var nightChk = $('#enableScheduledDimming');
if (nightChk) { var fn = function () { toggle($('#nightFields'), nightChk.checked); }; nightChk.addEventListener('change', fn); syncs.push(fn); }
var offChk = $('#enableScheduledOff');
if (offChk) { var fo = function () { toggle($('#offFields'), offChk.checked); }; offChk.addEventListener('change', fo); syncs.push(fo); }
var ledChk = $('#ledEnabled'), ledFx = $('#ledEffect'), ledIdle = $('#ledVuIdle'), ledSlots = [-1, -1];
function syncLed() {
 toggle($('#ledFields'), !ledChk || ledChk.checked);
 toggle($('#ledLightCard'), !ledChk || ledChk.checked);
 var fx = ledFx ? ledFx.value : '0';
 var shown = fx === '3' && ledIdle ? ledIdle.value : fx;
 toggle($('#ledSpeedField'), ['2', '5', '6', '7', '8', '9'].indexOf(shown) >= 0);
 toggle($('#ledMirrorField'), fx === '3' || ['4', '6', '7', '8', '9'].indexOf(shown) >= 0);
 toggle($('#ledVuFields'), fx === '3');
 // Each colour row only where the effect on show uses it.
 var slotRow = function (slot) { var c = form.elements.namedItem('color_' + slot); return c ? c.parentNode : null; };
 toggle(slotRow(ledSlots[0]), ['0', '2', '4', '8', '9'].indexOf(shown) >= 0);
 toggle(slotRow(ledSlots[1]), shown === '4');
 var fxHint = $('#ledEffectHint');
 if (fxHint) fxHint.textContent = {
  '0': 'Holds the strip colour steady.',
  '2': 'A soft bright band drifts slowly along the strip.',
  '6': 'The colour wheel scrolls along the strip.',
  '7': 'A flickering flame burns from one end, or from the middle outwards.',
  '8': 'A bright head with a fading tail runs along the strip, again and again.',
  '9': 'A bright dot with a short tail bounces between the ends.',
  '4': 'Fills the strip over each hour, with a seconds dot running its length once a minute.',
  '5': 'Outside temperature as colour with clouds, rain, snow or lightning on top; set up weather under the Weather clock style first.',
  '3': 'A loudness meter fed by the companion app, while the panel keeps showing the clock.'
 }[fx] || '';
 var inten = {
  '6': ['Rainbow size', '0 turns the whole strip through the colours together, 50 lays one rainbow along it, 100 two.'],
  '7': ['Sparking', 'How often new sparks feed the flame. Low is a quiet ember, high a roaring fire.'],
  '8': ['Tail length', 'How far the tail trails behind the head, as a share of the strip.'],
  '9': ['Tail length', 'How far the tail trails behind the head, as a share of the strip.']
 }[shown];
 toggle($('#ledIntensityField'), !!inten);
 if (inten) { $('#ledIntensityLabel').textContent = inten[0]; $('#ledIntensityHint').textContent = inten[1]; }
}
if (ledChk) ledChk.addEventListener('change', syncLed);
if (ledFx) ledFx.addEventListener('change', syncLed);
if (ledIdle) ledIdle.addEventListener('change', syncLed);
if (ledChk || ledFx) syncs.push(syncLed);
var ledCnt = $('#ledCount'), ledMaxCount = 300, ledMaPerLed = 50;
function syncLedCount() {
 var hint = $('#ledCountHint');
 if (!hint || !ledCnt) return;
 var n = parseInt(ledCnt.value || '0', 10) || 0;
 hint.textContent = 'Up to ' + ledMaxCount + '. At full white ' + n + ' LEDs draw about ' +
  (n * ledMaPerLed) + ' mA, measured on a reference strip.';
}
if (ledCnt) { ledCnt.addEventListener('input', syncLedCount); syncs.push(syncLedCount); }
var ledPinEl = $('#ledPin'), ledPins = null;
function ledPinRanges() {
 var out = [], i = 0;
 while (i < ledPins.length) {
  var j = i;
  while (j + 1 < ledPins.length && ledPins[j + 1] === ledPins[j] + 1) j++;
  out.push(j > i ? ledPins[i] + '-' + ledPins[j] : String(ledPins[i]));
  i = j + 1;
 }
 return out.join(', ');
}
function syncLedPin() {
 if (!ledPinEl || !ledPins) return;
 var n = parseInt(ledPinEl.value, 10);
 var bad = ledChk && ledChk.checked && ledPinEl.value !== '' && ledPins.indexOf(n) < 0;
 var msg = bad ? 'GPIO' + n + ' is not free on this board. Free: ' + ledPinRanges() + '.' : '';
 ledPinEl.setCustomValidity(msg);
 var err = $('#ledPinErr');
 if (err) { err.textContent = msg; toggle(err, bad); }
}
if (ledPinEl) { ledPinEl.addEventListener('input', syncLedPin); syncs.push(syncLedPin); }
if (ledChk) ledChk.addEventListener('change', syncLedPin);
var ambChk = $('#ambientEnabled');
if (ambChk) { var fa = function () { toggle($('#ambientFields'), ambChk.checked); }; ambChk.addEventListener('change', fa); syncs.push(fa); }
function ambCall(path, okMsg) {
fetch(path).then(function (r) { return r.json(); })
.then(function () { var s = $('#ambRunStatus'); if (s) s.textContent = okMsg; })
.catch(function () { var s = $('#ambRunStatus'); if (s) s.textContent = 'Request failed - is the device reachable?'; });
}
var ambStart = $('#ambStartBtn');
if (ambStart) ambStart.addEventListener('click', function () { ambCall('/api/mode/ambient', 'Ambient running. Stop returns to the normal display.'); });
var ambStop = $('#ambStopBtn');
if (ambStop) ambStop.addEventListener('click', function () { ambCall('/api/mode/auto', 'Back to normal mode.'); });
var animSel = $('#ambientCustomFile'), animField = $('#animCustomField'), ambStyleSel = $('#ambientStyle');
var animUsable = false;
function syncAnimField() { if (animField) animField.style.display = (ambStyleSel && ambStyleSel.value === '6') ? '' : 'none'; }
function animStatus(t) { var s = $('#animStatus'); if (s) s.textContent = t; }
function animRefresh() {
if (!animSel) return;
fetch('/api/anim/list').then(function (r) { return r.json(); }).then(function (d) {
animUsable = !!d.usable;
if (!d.usable) { animStatus(d.reason || 'Storage unavailable'); syncAnimField(); return; }
var cur = animSel.dataset.cur || '';
animSel.innerHTML = '';
var none = document.createElement('option'); none.value = ''; none.textContent = '- none -'; animSel.appendChild(none);
(d.anims || []).forEach(function (a) {
var o = document.createElement('option'); o.value = a.name;
o.textContent = a.name + ' (' + a.frames + ' frames, ' + Math.round(a.bytes / 1024) + ' KiB)';
if (a.name === cur) o.selected = true;
animSel.appendChild(o);
});
animStatus(Math.round(d.free / 1024) + ' KiB free. Upload budget: ' + Math.floor(d.maxUploadBytes / 1024) + ' KiB, up to ' + Math.min(360, d.maxFrames) + ' frames. Use the companion Animations page to convert and preview GIFs.');
syncAnimField();
}).catch(function () { animStatus('Cannot read animation storage. Retry after checking the connection.'); });
}
if (ambStyleSel) ambStyleSel.addEventListener('change', syncAnimField);
var vizStyleSel = $('#vizStyle'), vizClassicC = $('#vizClassicColors'), vizScopeO = $('#vizScopeOptions'), vizOptCard = $('#vizOptionsCard');
function syncVizPanels() {
var v = vizStyleSel ? vizStyleSel.value : '0';
if (vizClassicC) vizClassicC.style.display = (v === '0') ? '' : 'none';
if (vizScopeO) vizScopeO.style.display = (v === '6') ? '' : 'none';
if (vizOptCard) vizOptCard.style.display = (v === '0' || v === '6') ? '' : 'none';
}
if (vizStyleSel) vizStyleSel.addEventListener('change', syncVizPanels);
syncs.push(syncVizPanels);
var animUp = $('#animUploadBtn');
if (animUp) animUp.addEventListener('click', function () {
var fi = $('#animFile');
if (!fi || !fi.files.length) { animStatus('Choose a .pca file first (made by tools/gif2pca.py).'); return; }
var fd = new FormData(); fd.append('anim', fi.files[0]);
animStatus('Uploading...');
fetch('/api/anim/upload', { method: 'POST', body: fd }).then(function (r) { return r.json(); }).then(function (d) {
if (d.success) { if (animSel) animSel.dataset.cur = d.name; animStatus('Uploaded ' + d.name + '. Select it and Save.'); animRefresh(); }
else animStatus('Upload failed: ' + (d.error || 'unknown'));
}).catch(function () { animStatus('Upload failed - device unreachable?'); });
});
var animDel = $('#animDeleteBtn');
if (animDel) animDel.addEventListener('click', function () {
if (!animSel || !animSel.value) { animStatus('Select an animation to delete.'); return; }
fetch('/api/anim/delete?name=' + encodeURIComponent(animSel.value), { method: 'POST' }).then(function (r) { return r.json(); })
.then(function () { animStatus('Deleted.'); animRefresh(); }).catch(function () {});
});
function vizCall(path, okMsg) {
fetch(path).then(function (r) { return r.json(); })
.then(function () { var s = $('#vizRunStatus'); if (s) s.textContent = okMsg; })
.catch(function () { var s = $('#vizRunStatus'); if (s) s.textContent = 'Request failed - is the device reachable?'; });
}
var vizStart = $('#vizStartBtn');
if (vizStart) vizStart.addEventListener('click', function () { vizCall('/api/mode/viz', 'Visualizer running. Start the companion audio stream if the panel says no audio data.'); });
var vizStop = $('#vizStopBtn');
if (vizStop) vizStop.addEventListener('click', function () { vizCall('/api/mode/auto', 'Back to normal mode.'); });
var gameStatusEl = $('#gameStatus'), gamePage = $('[data-page="game"]');
function pollGameStatus() {
if (!gameStatusEl || !gamePage || !gamePage.classList.contains('active')) return;
fetch('/api/game/status').then(function (r) { return r.json(); }).then(function (d) {
var t = 'Pad status: ' + d.link + (d.link === 'connected' ? ' (battery ' + d.battery + '%)' : '');
gameStatusEl.textContent = t + ' - ' + (d.paired ? 'a pad is paired' : 'no pad paired') + ' - game mode ' + (d.active ? 'on' : 'off');
if (d.hiscores) renderHiscores(d.hiscores);
}).catch(function () {});
}
var gameHiEl = $('#gameHiscores'), gameHiKey = '';
function renderHiscores(list) {
var key = JSON.stringify(list);
if (!gameHiEl || key === gameHiKey) return;
gameHiKey = key;
gameHiEl.textContent = '';
list.forEach(function (g) {
var row = document.createElement('div');
row.style.cssText = 'display:flex;align-items:center;gap:8px;padding:4px 0';
var name = document.createElement('span');
name.style.flex = '1';
name.textContent = g.name;
var hi = document.createElement('strong');
hi.textContent = g.hi ? g.hi : '-';
var btn = document.createElement('button');
btn.type = 'button';
btn.className = 'btn';
btn.textContent = 'Reset';
btn.disabled = !g.hi;
btn.addEventListener('click', function () { resetHiscore(g.id, 'Reset the ' + g.name + ' high score?'); });
row.appendChild(name); row.appendChild(hi); row.appendChild(btn);
gameHiEl.appendChild(row);
});
}
function resetHiscore(id, msg) {
if (confirm(msg)) fetch('/api/game/hiscores?reset=' + encodeURIComponent(id), { method: 'POST' }).then(function () { gameHiKey = ''; pollGameStatus(); });
}
var gameHiResetAll = $('#gameHiResetAll');
if (gameHiResetAll) gameHiResetAll.addEventListener('click', function () { resetHiscore('all', 'Reset every high score?'); });
var gameStartBtn = $('#gameStart'), gameStopBtn = $('#gameStop'), gameForgetBtn = $('#gameForget');
if (gameStartBtn) gameStartBtn.addEventListener('click', function () { fetch('/api/game/start').then(pollGameStatus); });
if (gameStopBtn) gameStopBtn.addEventListener('click', function () { fetch('/api/game/stop').then(pollGameStatus); });
if (gameForgetBtn) gameForgetBtn.addEventListener('click', function () {
if (confirm('Forget the paired pad? It will need its pair button held again.')) fetch('/api/game/forget', { method: 'POST' }).then(function (r) { if (r.ok) pollGameStatus(); else if (gameStatusEl) gameStatusEl.textContent = 'Bluetooth is still shutting down - try again in a moment.'; });
});
setInterval(pollGameStatus, 2000);
fetch('/api/game/status').then(function (r) { if (r.status === 404) { var n = $('[data-nav="game"]'); if (n) n.style.display = 'none'; } }).catch(function () {});
var staticSel = $('#useStaticIP');
if (staticSel) { var fs = function () { toggle($('#staticFields'), staticSel.value === '1'); }; staticSel.addEventListener('change', fs); syncs.push(fs); }
var marioEnc = $('#marioIdleEncounters');
if (marioEnc) { var fe = function () { toggle($('#marioEncFields'), marioEnc.checked); }; marioEnc.addEventListener('change', fe); syncs.push(fe); }
var tetSmallClk = $('#tetrisSmallClock');
if (tetSmallClk) { var ftsc = function () { toggle($('#tetrisSmallClockField'), tetSmallClk.checked); }; tetSmallClk.addEventListener('change', ftsc); syncs.push(ftsc); }
var STYLE_PANELS = { '0':'marioSettings','3':'spaceSettings','4':'spaceSettings','5':'pongSettings','6':'pacmanSettings','7':'snakeSettings','8':'tetrisSettings','10':'asteroidsSettings','11':'dinoSettings','12':'matrixSettings','14':'weatherSettings','16':'tronSettings','17':'doomSettings' };
var ALL_PANELS = Object.keys(STYLE_PANELS).map(function (k) { return STYLE_PANELS[k]; });
var clockStyle = $('#clockStyle');
function syncClockPanels() {
ALL_PANELS.forEach(function (id) {
var el = document.getElementById(id); if (el) el.style.display = 'none';
var c = document.getElementById(id + 'Colors'); if (c) c.style.display = 'none';
});
var dcs = document.querySelectorAll('.digitc');
for (var i = 0; i < dcs.length; i++) dcs[i].style.display = 'none';
var show = STYLE_PANELS[clockStyle.value];
if (show) {
var e = document.getElementById(show); if (e) e.style.display = '';
var c = document.getElementById(show + 'Colors'); if (c) c.style.display = '';
}
var dc = document.querySelector('.digitc[data-ds="' + clockStyle.value + '"]');
if (dc) dc.style.display = '';
}
if (clockStyle) { clockStyle.addEventListener('change', syncClockPanels); syncs.push(syncClockPanels); }
var wgBtn = $('#weatherGeoBtn');
if (wgBtn) wgBtn.addEventListener('click', function () {
var q = $('#weatherCity').value.trim(); if (!q) return;
var st = $('#weatherGeoStatus'); st.textContent = 'Searching...';
fetch('https://geocoding-api.open-meteo.com/v1/search?count=1&name=' + encodeURIComponent(q))
.then(function (r) { return r.json(); })
.then(function (d) {
if (d.results && d.results.length) {
var g = d.results[0];
$('#weatherLat').value = g.latitude.toFixed(4);
$('#weatherLon').value = g.longitude.toFixed(4);
st.textContent = 'Found: ' + g.name + (g.admin1 ? ', ' + g.admin1 : '') + (g.country ? ', ' + g.country : '');
} else { st.textContent = 'No match found. Try a bigger nearby city.'; }
})
.catch(function () { st.textContent = 'Lookup failed (no internet?). Enter coordinates manually.'; });
});
var dn = $('#deviceName');
if (dn) dn.addEventListener('input', function () {
var v = dn.value.toLowerCase() || 'pixelclock';
var hp = $('#hostPreview'); if (hp) hp.textContent = v;
var sh = $('#srHost'); if (sh) sh.textContent = v;
});
var metricsData = [];
var DEVTIME = '12:34';
var MAX_ROWS = 5;
var IS_LARGE = false;
function rowGeom() {
var rm = parseInt($('#rowMode').value, 10);
IS_LARGE = (rm >= 2);
MAX_ROWS = (rm === 0) ? 5 : (rm === 1) ? 6 : (rm === 2) ? 2 : 3;
return { rows: MAX_ROWS, cols: IS_LARGE ? 1 : 2, large: IS_LARGE };
}
function byId(id) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].id === id) return metricsData[i]; return null; }
var FONT = (function () {
var h = "00000000003e5b4f5b3e3e6b4f6b3e1c3e7c3e1c183c7e3c181c577d571c1c5e7f5e1c00183c1800ffe7c3e7ff0018241800ffe7dbe7ff30483a060e2629792926407f050507407f05253f5a3ce73c5a7f3e1c1c08081c1c3e7f14227f22145f5f005f5f06097f017f006689956a606060606094a2ffa29408047e040810207e201008082a1c08081c2a08081e101010100c1e0c1e0c30383e3830060e3e0e06000000000000005f00000007000700147f147f14242a7f2a12231308646236495620500008070300001c2241000041221c002a1c7f1c2a08083e080800807030000808080808000060600020100804023e5149453e00427f400072494949462141494d331814127f1027454545393c4a49493141211109073649494936464949291e0000140000004034000000081422411414141414004122140802015909063e415d594e7c1211127c7f494949363e414141227f4141413e7f494949417f090909013e414151737f0808087f00417f41002040413f017f081422417f404040407f021c027f7f0408107f3e4141413e7f090909063e4151215e7f09192946264949493203017f01033f4040403f1f2040201f3f4038403f631408146303047804036159494d43007f4141410204081020004141417f04020102044040404040000307080020545478407f284444383844444428384444287f385454541800087e090218a4a49c787f0804047800447d40002040403d007f1028440000417f40007c047804787c080404783844444438fc1824241818242418fc7c08040408485454542404043f44243c4040207c1c2040201c3c4030403c44281028444c9090907c4464544c4400083641000000770000004136080002010204023c2623263c1ea1a161123a4040207a385454555921555579412254547842215554784020545579400c1e5272123955555559395454545939555454580000457c410002457d420001457c407d1211127df0282528f07c545545002054547c547c0a097f4932494949323a4444443a324a4848303a4141217a3a42402078009da0a07d3d4242423d3d4040403d3c24ff2424487e4943662b2ffc2f2bff0929f620c0887e090320545479410000447d413048484a32384040227a007a0a0a727d0d19317d2629292f28262929292630484d4020380808080808080808382f10c8acba2f102834fa00007b000008142a142222142a14085500550055aa55aa55aaff55ff55ff000000ff00101010ff00141414ff001010ff00ff1010f010f0141414fc001414f700ff0000ff00ff1414f404fc141417101f10101f101f1414141f00101010f0000000001f101010101f10101010f010000000ff101010101010101010ff10000000ff140000ff00ff00001f10170000fc04f414141710171414f404f40000ff00f714141414141414f700f7141414171410101f101f141414f4141010f010f000001f101f0000001f14000000fc140000f010f01010ff10ff141414ff141010101f00000000f010fffffffffff0f0f0f0f0ffffff0000000000ffff0f0f0f0f0f3844443844fc4a4a4a347e02020606027e027e0263554941633844443c04407e201e2006027e020299a5e7a5991c2a492a1c4c7201724c304a4d4d303048784830bc625a463d3e494949007e0101017e2a2a2a2a2a44445f444440514a444040444a51400000ff0103e080ff000008086b6b083612362436060f090f06000018180000001010003040ff0101001f01011e00191d1712003c3c3c3c0000000000";
var a = new Uint8Array(1280);
for (var i = 0; i < 1280; i++) a[i] = parseInt(h.substr(i * 2, 2), 16);
return a;
})();
function FB() { this.buf = new Uint8Array(128 * 64); }
FB.prototype.px = function (x, y, v) { x |= 0; y |= 0; if (x >= 0 && x < 128 && y >= 0 && y < 64) this.buf[y * 128 + x] = v; };
FB.prototype.fillRect = function (x, y, w, h, v) { for (var yy = y; yy < y + h; yy++) for (var xx = x; xx < x + w; xx++) this.px(xx, yy, v); };
FB.prototype.drawRect = function (x, y, w, h, v) { var xx, yy; for (xx = x; xx < x + w; xx++) { this.px(xx, y, v); this.px(xx, y + h - 1, v); } for (yy = y; yy < y + h; yy++) { this.px(x, yy, v); this.px(x + w - 1, yy, v); } };
function drawChar(fb, x, y, ch, size) {
var o = ch.charCodeAt(0); if (o > 255) o = 63;
if (x >= 128 || y >= 64 || (x + 6 * size - 1) < 0 || (y + 8 * size - 1) < 0) return;
var base = o * 5;
for (var col = 0; col < 5; col++) {
var line = FONT[base + col];
for (var row = 0; row < 8; row++) {
if ((line >> row) & 1) {
if (size === 1) fb.px(x + col, y + row, 255);
else fb.fillRect(x + col * size, y + row * size, size, size, 255);
}
}
}
}
function write(fb, x, y, text, size, wrap) {
var cx = x, cy = y;
for (var i = 0; i < text.length; i++) {
var ch = text.charAt(i);
if (ch === '\n') { cx = 0; cy += size * 8; continue; }
if (ch === '\r') continue;
if (wrap && (cx + size * 6) > 128) { cx = 0; cy += size * 8; }
drawChar(fb, cx, cy, ch, size); cx += size * 6;
}
return [cx, cy];
}
function buildMetricText(label, unit, value, rpmK, netMB) {
var dl = (label || '').replace(/\^/g, ' ');
var stripped = dl.replace(/ +$/, '');
var trailing = dl.length - stripped.length; dl = stripped;
if (dl.charAt(dl.length - 1) === '%') dl = dl.slice(0, -1);
var spaces = new Array(Math.min(trailing, 10) + 1).join(' ');
if (rpmK && unit === 'RPM' && value >= 1000) return dl + ':' + spaces + (value / 1000).toFixed(1) + 'K';
if (unit === 'KB/s') { var a = value / 10; if (netMB) return dl + ':' + spaces + (a / 1000).toFixed(1) + 'M'; return dl + ':' + spaces + a.toFixed(1) + unit; }
return dl + ':' + spaces + value + unit;
}
function buildCompanionText(unit, value, netMB) {
if (unit === 'KB/s') { var cv = value / 10; if (netMB) return ' ' + (cv / 1000).toFixed(1) + 'M'; return ' ' + cv.toFixed(1) + unit; }
return ' ' + value + unit;
}
function renderFrame() {
saveFormState();
var fb = new FB();
var rm = parseInt($('#rowMode').value, 10);
var showClock = $('#showClock').checked;
var clockPos = parseInt(($('#clockPosition') || {}).value || '0', 10);
var clockOffset = parseInt(($('#clockOffset') || {}).value || '0', 10) || 0;
var rpmK = $('#rpmKFormat') ? $('#rpmKFormat').checked : false;
var netMB = $('#netMBFormat') ? $('#netMBFormat').checked : false;
var ts = DEVTIME || '12:34';
var isLarge = rm >= 2, textH = isLarge ? 16 : 8;
function slotText(pos) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].position === pos) return metricsData[i]; return null; }
function slotBar(pos) { for (var i = 0; i < metricsData.length; i++) if (metricsData[i].barPosition === pos) return metricsData[i]; return null; }
function lblOf(mt) { return (mt.label && mt.label.length) ? mt.label : mt.name; }
function drawBar(x, y, mt, large) {
var ax = x + (mt.barOffsetX || 0), aw = (mt.barWidth || 60);
if (ax >= 128 || ax < 0) return;
if (ax + aw > 128) aw = 128 - ax;
if (aw <= 0) return;
var bmin = mt.barMin | 0, bmax = (mt.barMax == null ? 100 : mt.barMax);
var rng = bmax - bmin; if (rng <= 0) rng = 100;
var dv = (mt.unit === 'KB/s') ? Math.floor(mt.value / 10) : mt.value;
var vir = Math.max(bmin, Math.min(dv, bmax)) - bmin;
var fillW = Math.floor(vir * (aw - 2) / rng);
var barH = large ? 16 : 8;
fb.drawRect(ax, y, aw, barH, 255);
if (fillW > 0) fb.fillRect(ax + 1, y + 1, fillW, barH - 2, 255);
}
function drawText(x, y, mt, size, wrap, large) {
var text = buildMetricText(lblOf(mt), mt.unit, mt.value | 0, rpmK, netMB);
var comp = (mt.companionId > 0) ? byId(mt.companionId) : null;
if (comp && !large) text += buildCompanionText(comp.unit, comp.value | 0, netMB);
var cc = write(fb, x, y, text, size, wrap);
if (comp && large) {
var ct = buildCompanionText(comp.unit, comp.value | 0, netMB).slice(1);
var cxp = 128 - ct.length * 12; if (cxp < cc[0] + 4) cxp = cc[0] + 4;
write(fb, cxp, y, ct, size, wrap);
}
}
if (isLarge) {
var maxRows = (rm === 2) ? 2 : 3;
var startY = (rm === 2) ? 8 : 4;
if (showClock) { write(fb, 48 + clockOffset, 0, ts, 1, false); startY = (rm === 2) ? 12 : 10; }
var rowH = (rm === 2) ? 32 : (showClock ? 18 : 20);
for (var r = 0; r < maxRows; r++) {
var y = startY + r * rowH; if (y + textH > 64) break;
var b = slotBar(r); if (b) { drawBar(0, y, b, true); continue; }
var e = slotText(r); if (e) drawText(0, y, e, 2, false, true);
}
} else {
var COL1 = 0, COL2 = 62, maxRowsN = (rm === 0) ? 5 : 6, sY, rH;
if (rm === 0) { sY = 0; rH = (showClock && clockPos === 0) ? 11 : 13; } else { sY = 2; rH = 10; }
if (showClock) {
if (clockPos === 0) { write(fb, 48 + clockOffset, sY, ts, 1, true); sY += 10; }
else if (clockPos === 1) write(fb, COL1 + clockOffset, sY, ts, 1, true);
else if (clockPos === 2) write(fb, COL2 + clockOffset, sY, ts, 1, true);
}
for (var rr = 0; rr < maxRowsN; rr++) {
var yy = sY + rr * rH; if (yy + 8 > 64) break;
var cols = [[COL1, rr * 2, showClock && clockPos === 1 && rr === 0], [COL2, rr * 2 + 1, showClock && clockPos === 2 && rr === 0]];
for (var ci = 0; ci < 2; ci++) {
var cx = cols[ci][0], pos = cols[ci][1]; if (cols[ci][2]) continue;
var bb = slotBar(pos); if (bb) { drawBar(cx, yy, bb, false); continue; }
var ee = slotText(pos); if (ee) drawText(cx, yy, ee, 1, true, false);
}
}
}
blit(fb);
var g = rowGeom();
$('#oledMeta').textContent = '128x64 - ' + g.rows + ' rows - ' + (g.cols === 1 ? 'single column' : '2 columns');
}
function blit(fb) {
var canvas = $('#oledCanvas'); if (!canvas) return;
var ctx = canvas.getContext('2d');
var amber = document.documentElement.getAttribute('data-accent') === 'amber';
var on = amber ? [255, 207, 122] : [132, 243, 173];
var off = amber ? [12, 8, 3] : [6, 13, 9];
var img = ctx.createImageData(128, 64), d = img.data;
for (var i = 0; i < fb.buf.length; i++) {
var c = fb.buf[i] ? on : off, o = i * 4;
d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2]; d[o + 3] = 255;
}
ctx.putImageData(img, 0, 0);
}
function rowMaxSlots(rm) { return rm === 0 ? 10 : rm === 1 ? 12 : rm === 2 ? 2 : 3; }
function slotGeom(rm, slot, showClock, clockPos) {
if (slot < 0 || slot >= rowMaxSlots(rm)) return null;
if (rm >= 2) {
var sy, rh;
if (showClock) { if (rm === 2) { sy = 12; rh = 32; } else { sy = 10; rh = 18; } }
else { if (rm === 2) { sy = 8; rh = 32; } else { sy = 4; rh = 20; } }
return { x: 0, y: sy + slot * rh, w: 128, h: rh };
}
var sy2, rh2;
if (rm === 0) { sy2 = 0; rh2 = (showClock && clockPos === 0) ? 11 : 13; } else { sy2 = 2; rh2 = 10; }
if (showClock && clockPos === 0) sy2 += 10;
var row = Math.floor(slot / 2), col = slot % 2;
return { x: col === 0 ? 0 : 62, y: sy2 + row * rh2, w: col === 0 ? 60 : 64, h: rh2 };
}
function clockBlockedSlot(rm, clockPos, showClock) { if (!showClock || rm >= 2 || clockPos === 0) return -1; return clockPos === 1 ? 0 : 1; }
function buildDropCells() {
var host = $('#dropCells'); if (!host) return;
var rm = parseInt($('#rowMode').value, 10);
var showClock = $('#showClock').checked;
var clockPos = parseInt(($('#clockPosition') || {}).value || '0', 10);
host.innerHTML = '';
var n = rowMaxSlots(rm), blocked = clockBlockedSlot(rm, clockPos, showClock);
for (var s = 0; s < n; s++) {
if (s === blocked) continue;
var g = slotGeom(rm, s, showClock, clockPos); if (!g) continue;
var h = Math.min(g.h, 64 - g.y); if (h <= 0) continue;
var cell = document.createElement('div');
var occ = metricsData.filter(function (m) { return m.position === s; })[0];
cell.className = 'drop-cell' + (occ ? ' filled' : ''); cell.dataset.slot = s;
if (occ) cell.title = 'Click to remove ' + occ.name + ' from the screen';
cell.style.left = (g.x / 128 * 100) + '%'; cell.style.top = (g.y / 64 * 100) + '%';
cell.style.width = (g.w / 128 * 100) + '%'; cell.style.height = (h / 64 * 100) + '%';
attachDrop(cell, s);
host.appendChild(cell);
}
}
var DRAG_ID = null;
function setPositions(targetSlot) {
saveFormState();
if (targetSlot !== 255) metricsData.forEach(function (x) { if (x.id !== DRAG_ID && x.position === targetSlot) x.position = 255; });
var mt = byId(DRAG_ID); if (mt) { mt.position = targetSlot; if (targetSlot === 255) mt.barPosition = 255; }
renderMetrics(); renderFrame(); buildDropCells(); buildChipTray(); markDirty();
}
function attachDrop(cell, slot) {
cell.addEventListener('dragover', function (e) { if (DRAG_ID != null) { e.preventDefault(); cell.classList.add('over'); } });
cell.addEventListener('dragleave', function () { cell.classList.remove('over'); });
cell.addEventListener('drop', function (e) { e.preventDefault(); cell.classList.remove('over'); if (DRAG_ID != null) setPositions(slot); });
cell.addEventListener('click', function () {
if (SEL_ID != null) { DRAG_ID = SEL_ID; setSel(null); setPositions(slot); DRAG_ID = null; return; }
var occ = metricsData.filter(function (m) { return m.position === slot; })[0];
if (occ) { DRAG_ID = occ.id; setPositions(255); DRAG_ID = null; }
});
}
var SEL_ID = null;
function setSel(id) {
SEL_ID = id;
var st = $('#oledStage'); if (st) st.classList.toggle('placing', id != null);
var tray = $('#chipTray'); if (tray) tray.classList.toggle('placing', id != null);
buildChipTray();
}
function slotLabel(pos, g) {
if (pos === 255 || pos == null) return 'Hidden';
if (g.large) return 'R' + (pos + 1);
return 'R' + (Math.floor(pos / 2) + 1) + (pos % 2 === 0 ? '·L' : '·R');
}
function buildChipTray() {
var host = $('#chipTray'); if (!host) return;
host.innerHTML = '';
if (!metricsData.length) { host.innerHTML = '<span class="chip-empty">No metrics yet - start the companion app on your PC.</span>'; return; }
var g = rowGeom();
var sorted = metricsData.slice().sort(function (a, b) { return a.displayOrder - b.displayOrder; });
sorted.forEach(function (mt) {
var placed = mt.position !== 255 && mt.position != null;
var chip = document.createElement('div');
chip.className = 'chip' + (placed ? ' placed' : '') + (SEL_ID === mt.id ? ' sel' : '');
chip.innerHTML = '<span class="cn">' + esc(mt.name) + '</span><span class="cb">' + slotLabel(mt.position, g) + '</span>';
makeRowDraggable(chip, mt.id);
chip.addEventListener('click', function () { setSel(SEL_ID === mt.id ? null : mt.id); });
host.appendChild(chip);
});
}
function makeRowDraggable(row, id) {
row.setAttribute('draggable', 'true');
row.addEventListener('dragstart', function (e) {
DRAG_ID = id; row.classList.add('drag-src');
var st = $('#oledStage'); if (st) st.classList.add('dragging');
try { e.dataTransfer.setData('text/plain', String(id)); e.dataTransfer.effectAllowed = 'move'; } catch (_) {}
});
row.addEventListener('dragend', function () {
DRAG_ID = null; row.classList.remove('drag-src');
var st = $('#oledStage'); if (st) st.classList.remove('dragging');
});
}
function setupListDrop() {
var tray = $('#chipTray');
if (tray) {
tray.addEventListener('dragover', function (e) { if (DRAG_ID != null) e.preventDefault(); });
tray.addEventListener('drop', function (e) { if (DRAG_ID != null) { e.preventDefault(); setPositions(255); } });
tray.addEventListener('click', function (e) { if (e.target === tray && SEL_ID != null) setSel(null); });
}
document.addEventListener('keydown', function (e) { if (e.key === 'Escape' && SEL_ID != null) setSel(null); });
var list = $('#metricsList'); if (!list) return;
list.addEventListener('dragover', function (e) { if (DRAG_ID != null) e.preventDefault(); });
list.addEventListener('drop', function (e) { if (DRAG_ID != null) { e.preventDefault(); setPositions(255); } });
}
function saveFormState() {
metricsData.forEach(function (mt) {
var lbl = document.querySelector('input[name="label_' + mt.id + '"]'); if (lbl) mt.label = lbl.value;
var comp = document.getElementById('comp_' + mt.id); if (comp) mt.companionId = parseInt(comp.value, 10);
var bp = document.getElementById('barPos_' + mt.id); if (bp) mt.barPosition = parseInt(bp.value, 10);
var bmin = document.querySelector('input[name="barMin_' + mt.id + '"]'); if (bmin) mt.barMin = parseInt(bmin.value, 10) || 0;
var bmax = document.querySelector('input[name="barMax_' + mt.id + '"]'); if (bmax) mt.barMax = parseInt(bmax.value, 10) || 100;
var bw = document.querySelector('input[name="barWidth_' + mt.id + '"]'); if (bw) mt.barWidth = parseInt(bw.value, 10) || 60;
var bo = document.querySelector('input[name="barOffset_' + mt.id + '"]'); if (bo) mt.barOffsetX = parseInt(bo.value, 10) || 0;
});
}
function onRowMode() {
saveFormState();
var g = rowGeom();
var maxPos = g.large ? g.rows : g.rows * 2;
var hidden = metricsData.filter(function (mt) {
return (mt.position !== 255 && mt.position >= maxPos) || (mt.barPosition !== 255 && mt.barPosition >= maxPos);
});
if (hidden.length > 0) {
var names = hidden.map(function (mt) { return mt.name; }).join(', ');
if (!confirm('Warning: ' + hidden.length + ' metric(s) (' + names + ') will be hidden in this row mode. Continue?')) { return; }
metricsData.forEach(function (mt) {
if (mt.position !== 255 && mt.position >= maxPos) mt.position = 255;
if (mt.barPosition !== 255 && mt.barPosition >= maxPos) mt.barPosition = 255;
});
}
renderMetrics(); buildDropCells(); buildChipTray(); renderFrame();
}
function posOptionsHtml(cur, g, includeNoneLabel) {
var html = '<option value="255">' + (includeNoneLabel || 'None (hidden)') + '</option>';
for (var r = 0; r < g.rows; r++) {
if (g.large) {
html += '<option value="' + r + '"' + (cur === r ? ' selected' : '') + '>Row ' + (r + 1) + '</option>';
} else {
var lp = r * 2, rp = r * 2 + 1;
html += '<option value="' + lp + '"' + (cur === lp ? ' selected' : '') + '>Row ' + (r + 1) + ' &middot; Left</option>';
html += '<option value="' + rp + '"' + (cur === rp ? ' selected' : '') + '>Row ' + (r + 1) + ' &middot; Right</option>';
}
}
return html;
}
function esc(s) { return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }
function renderMetrics() {
var g = rowGeom();
var list = $('#metricsList');
var openIds = {}; Array.prototype.forEach.call(list.querySelectorAll('details.metric-row[open]'), function (d) { openIds[d.dataset.id] = 1; });
list.innerHTML = '';
if (!metricsData.length) { list.innerHTML = '<p class="field-hint">No metrics received yet. Start the companion app on your PC.</p>'; return; }
var sorted = metricsData.slice().sort(function (a, b) { return a.displayOrder - b.displayOrder; });
sorted.forEach(function (mt) {
var compName = mt.companionId > 0 ? (metricsData.filter(function (x) { return x.id === mt.companionId; })[0] || {}).name : null;
var compOpts = '<option value="0">None</option>';
metricsData.forEach(function (x) {
if (x.id !== mt.id) compOpts += '<option value="' + x.id + '"' + (mt.companionId === x.id ? ' selected' : '') + '>' + esc(x.name) + ' (' + esc(x.unit) + ')</option>';
});
var placed = mt.position !== 255 && mt.position != null;
var row = document.createElement('details');
row.className = 'metric-row';
row.dataset.id = mt.id;
row.dataset.placed = placed ? '1' : '0';
if (openIds[mt.id]) row.open = true;
row.innerHTML =
'<summary class="metric-sum"><span class="ms-main"><span class="ms-nm">' + esc(mt.label || mt.name) + '</span>' +
'<span class="ms-sub">' + esc(mt.unit) + (compName ? ' · + ' + esc(compName) : '') + '</span></span>' +
'<span class="ms-badge">' + slotLabel(mt.position, g) + '</span><span class="ms-chev"></span></summary>' +
'<div class="metric-body"><div class="metric-adv">' +
'<div><label class="field-label">Custom label (10 max)</label><input type="text" name="label_' + mt.id + '" value="' + esc(mt.label) + '" maxlength="10" placeholder="' + esc(mt.name) + '"></div>' +
'<div><label class="field-label">Pair with</label><div class="select-wrap"><select id="comp_' + mt.id + '" name="companion_' + mt.id + '">' + compOpts + '</select></div></div>' +
'<div class="full"><label class="field-label">Progress bar position</label><div class="select-wrap"><select id="barPos_' + mt.id + '" name="barPosition_' + mt.id + '">' + posOptionsHtml(mt.barPosition, g, 'None') + '</select></div></div>' +
'<div><label class="field-label">Bar min</label><input type="number" name="barMin_' + mt.id + '" value="' + (mt.barMin || 0) + '"></div>' +
'<div><label class="field-label">Bar max</label><input type="number" name="barMax_' + mt.id + '" value="' + (mt.barMax || 100) + '"></div>' +
'<div><label class="field-label">Bar width (px)</label><input type="number" name="barWidth_' + mt.id + '" value="' + (mt.barWidth || 60) + '" min="10" max="64"></div>' +
'<div><label class="field-label">Bar offset X (px)</label><input type="number" name="barOffset_' + mt.id + '" value="' + (mt.barOffsetX || 0) + '" min="0" max="54"></div>' +
'</div></div>' +
'<input type="hidden" name="order_' + mt.id + '" value="' + mt.displayOrder + '">' +
'<input type="hidden" name="position_' + mt.id + '" value="' + mt.position + '">';
list.appendChild(row);
$('#comp_' + mt.id, row).addEventListener('change', function () { saveFormState(); renderMetrics(); renderFrame(); markDirty(); });
$('#barPos_' + mt.id, row).addEventListener('change', function () { saveFormState(); renderFrame(); markDirty(); });
row.addEventListener('input', function () { saveFormState(); renderFrame(); var nm = $('.ms-nm', row), li = document.querySelector('input[name="label_' + mt.id + '"]'); if (nm && li) nm.textContent = li.value || mt.name; });
});
}
var rowModeSel = $('#rowMode');
if (rowModeSel) rowModeSel.addEventListener('change', onRowMode);
var showClockChk = $('#showClock');
if (showClockChk) showClockChk.addEventListener('change', function () { buildDropCells(); renderFrame(); });
var clockPosSel = $('#clockPosition');
if (clockPosSel) clockPosSel.addEventListener('change', function () { buildDropCells(); renderFrame(); });
['clockOffset', 'rpmKFormat', 'netMBFormat'].forEach(function (id) {
var el = document.getElementById(id); if (!el) return;
el.addEventListener('input', renderFrame); el.addEventListener('change', renderFrame);
});
setupListDrop();
function pollMetrics() {
fetch('/metrics').then(function (r) { return r.json(); }).then(function (data) {
if (data.time) DEVTIME = data.time;
if (data.metrics) data.metrics.forEach(function (d) { var m = byId(d.id); if (m) m.value = d.value; });
renderFrame();
}).catch(function () {});
}
// The layout editor is built from the device's row mode, so it waits for the
// values: the page is static now, /metrics can answer before /api/portal does,
// and a metric placed outside row mode 0's grid would fall back to "None".
function loadMetrics() {
fetch('/metrics').then(function (r) { return r.json(); }).then(function (data) {
if (data.time) DEVTIME = data.time;
if (data.metrics && data.metrics.length) { metricsData = data.metrics; renderMetrics(); buildDropCells(); buildChipTray(); renderFrame(); }
else { $('#metricsList').innerHTML = '<p class="field-hint">No metrics received yet. Start the companion app on your PC.</p>'; buildDropCells(); buildChipTray(); renderFrame(); }
setInterval(pollMetrics, 1500);
}).catch(function () { $('#metricsList').innerHTML = '<p class="field-hint">Could not load metrics from the device.</p>'; });
}
form.addEventListener('submit', function (e) {
e.preventDefault();
saveFormState();
var btn = $('#saveBtn'); var orig = btn.textContent;
btn.disabled = true; btn.textContent = 'Saving...';
var body = new URLSearchParams(new FormData(form));
fetch('/save', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
.then(function (r) { return r.json(); })
.then(function (d) {
btn.disabled = false; btn.textContent = orig;
if (d.success) {
markClean('Saved');
if (d.networkChanged) {
alert('Network settings changed. The device is restarting - you may need to reconnect at the new IP address.');
setTimeout(function () { window.location.href = '/'; }, 3000);
}
} else { alert('Error saving settings.'); }
})
.catch(function (err) { btn.disabled = false; btn.textContent = orig; alert('Error saving settings: ' + err); });
});
$('#resetBtn').addEventListener('click', function () {
if (!confirm('Have you exported a backup of your settings?\n\nUse "Export config" first if not.\n\nOK to continue with factory reset, Cancel to go back.')) return;
if (!confirm('ARE YOU SURE?\n\nThis permanently erases ALL settings:\n- WiFi credentials\n- Display & clock config\n- Metric labels & layout\n- Network settings\n\nThe device restarts into AP setup mode. This cannot be undone.')) return;
var f = document.createElement('form'); f.method = 'POST'; f.action = '/reset'; document.body.appendChild(f); f.submit();
});
$('#exportBtn').addEventListener('click', function () {
fetch('/api/export').then(function (r) { return r.json(); }).then(function (data) {
var blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
var url = URL.createObjectURL(blob);
var a = document.createElement('a'); a.href = url; a.download = 'animatedpixelclock-config.json';
document.body.appendChild(a); a.click(); document.body.removeChild(a); URL.revokeObjectURL(url);
}).catch(function (err) { alert('Error exporting configuration: ' + err); });
});
var ntpBtn = $('#ntpTestBtn');
if (ntpBtn) ntpBtn.addEventListener('click', function () {
var res = $('#ntpTestResult');
function esc(x) { return String(x).replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; }); }
var jobs = [{ label: 'Primary', srv: ($('#ntpServer1').value || '').trim() || 'pool.ntp.org' }];
var sec = ($('#ntpServer2').value || '').trim();
if (sec) jobs.push({ label: 'Secondary', srv: sec });
var lines = [];
ntpBtn.disabled = true; res.style.color = ''; res.textContent = 'Testing...';
function run(i) {
if (i >= jobs.length) { res.innerHTML = lines.join('<br>'); ntpBtn.disabled = false; return; }
var j = jobs[i];
fetch('/api/ntptest?server=' + encodeURIComponent(j.srv)).then(function (r) { return r.json(); }).then(function (d) {
if (d.success) lines.push('<span style="color:#3fb950">OK</span> ' + j.label + ' (' + esc(j.srv) + ') &middot; ' + esc(d.time) + ' UTC');
else lines.push('<span style="color:#f85149">no response</span> ' + j.label + ' (' + esc(j.srv) + ')');
}).catch(function () { lines.push('<span style="color:#f85149">test failed</span> ' + j.label + ' (' + esc(j.srv) + ')'); })
.then(function () { run(i + 1); });
}
run(0);
});
var panelCard = $('#panelCard');
var PANEL_DEFAULTS = { driver: 2, clockMHz: 8, latchBlanking: 2, clkPhase: false, colorDepth: 8, minRefresh: 60 };
function panelFill(p) {
$('#panelDriver').value = p.driver; $('#panelClock').value = p.clockMHz; $('#panelLatch').value = p.latchBlanking;
$('#panelDepth').value = p.colorDepth; $('#panelMinRefresh').value = p.minRefresh; $('#panelClkPhase').checked = !!p.clkPhase;
}
function panelLoad() {
fetch('/api/panel', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(function (p) {
panelFill(p); $('#panelTest').value = p.testPattern || 0;
$('#panelHz').textContent = p.refreshHz + ' Hz';
}).catch(function () {});
}
if (panelCard) {
// Panel options save on their own button, never through the main form.
['input', 'change'].forEach(function (t) { panelCard.addEventListener(t, function (ev) { ev.stopPropagation(); }); });
$('#panelTest').addEventListener('change', function () { fetch('/api/panel/test?pattern=' + this.value).catch(function () {}); });
$('#panelDefaultsBtn').addEventListener('click', function () { panelFill(PANEL_DEFAULTS); });
$('#panelApplyBtn').addEventListener('click', function () {
var mr = parseInt($('#panelMinRefresh').value, 10);
if (!(mr >= 30 && mr <= 200)) { alert('Minimum refresh must be 30-200 Hz.'); return; }
if (!confirm('Apply the panel options? The device reboots.')) return;
var body = new URLSearchParams({ driver: $('#panelDriver').value, clockMHz: $('#panelClock').value,
latchBlanking: $('#panelLatch').value, clkPhase: $('#panelClkPhase').checked ? 1 : 0,
colorDepth: $('#panelDepth').value, minRefresh: mr });
var btn = this; btn.disabled = true;
fetch('/api/panel', { method: 'POST', body: body }).then(function (r) { return r.json(); }).then(function (d) {
if (!d.success) { btn.disabled = false; alert('Could not save panel options.'); return; }
markClean('Rebooting...');
setTimeout(function () { location.reload(); }, 10000);
}).catch(function (err) { btn.disabled = false; alert('Error saving panel options: ' + err); });
});
}
var wifiTxCard = $('#wifiTxCard');
function wifiTxShow(d) { $('#wifiTxPower').value = String(d.dbm); $('#wifiTxRssi').textContent = 'signal ' + d.rssi + ' dBm'; }
if (wifiTxCard) {
['input', 'change'].forEach(function (t) { wifiTxCard.addEventListener(t, function (ev) { ev.stopPropagation(); }); });
fetch('/api/wifi/txpower', { cache: 'no-store' }).then(function (r) { return r.json(); }).then(wifiTxShow).catch(function () {});
$('#wifiTxPower').addEventListener('change', function () {
fetch('/api/wifi/txpower?dbm=' + this.value, { method: 'POST' }).then(function (r) { return r.json(); }).then(wifiTxShow)
.catch(function (err) { alert('Could not set the transmit power: ' + err); });
});
}
$('#importBtn').addEventListener('click', function () { $('#importFile').click(); });
$('#importFile').addEventListener('change', function (ev) {
var file = ev.target.files[0]; if (!file) return;
var reader = new FileReader();
reader.onload = function (e) {
var cfg;
try { cfg = JSON.parse(e.target.result); } catch (err) { alert('Invalid configuration file: ' + err); return; }
fetch('/api/import', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(cfg) })
.then(function (r) { return r.json(); })
.then(function (d) {
if (d.success) { alert('Configuration imported.' + (d.panelChanged ? ' Panel options apply after the next reboot.' : '') + ' Reloading...'); location.reload(); }
else { alert('Error importing configuration: ' + d.message); }
})
.catch(function (err) { alert('Error importing configuration: ' + err); });
};
reader.readAsText(file);
});
var drop = $('#otaDrop'), otaFile = $('#otaFile');
$('#otaBrowse').addEventListener('click', function () { otaFile.click(); });
otaFile.addEventListener('change', function () { if (otaFile.files[0]) doUpload(otaFile.files[0]); });
['dragenter', 'dragover'].forEach(function (ev) { drop.addEventListener(ev, function (e) { e.preventDefault(); drop.classList.add('drag'); }); });
['dragleave', 'drop'].forEach(function (ev) { drop.addEventListener(ev, function (e) { e.preventDefault(); drop.classList.remove('drag'); }); });
drop.addEventListener('drop', function (e) { var f = e.dataTransfer.files[0]; if (f) doUpload(f); });
function doUpload(file) {
if (!file.name || file.name.slice(-4) !== '.bin') { alert('Please select a valid .bin firmware file.'); return; }
var fileGames = /-games\.bin$/i.test(file.name);
if (fwGames !== null && fileGames !== fwGames && !confirm('This clock runs the ' + (fwGames ? 'clock + games' : 'clock only') +
 ' firmware, but ' + file.name + ' looks like the ' + (fileGames ? 'clock + games' : 'clock only') + ' one. Install it anyway?')) return;
var prog = $('#otaProgress'), fill = $('#otaFill'), pct = $('#otaPct');
prog.classList.add('show'); fill.style.width = '0%'; pct.textContent = 'Uploading ' + file.name + '... 0%';
var xhr = new XMLHttpRequest();
xhr.upload.addEventListener('progress', function (e) {
if (e.lengthComputable) { var p = Math.round((e.loaded / e.total) * 100); fill.style.width = p + '%'; pct.textContent = 'Uploading ' + file.name + '... ' + p + '%'; }
});
xhr.addEventListener('load', function () {
if (xhr.status === 200) { fill.style.width = '100%'; pct.textContent = '✓ Written - rebooting device...'; setTimeout(function () { window.location.href = '/'; }, 8000); }
else { pct.textContent = (xhr.responseText || 'Upload failed - please try again.'); }
});
xhr.addEventListener('error', function () { pct.textContent = 'Upload error - please try again.'; });
var fd = new FormData(); fd.append('firmware', file);
xhr.open('POST', '/update'); xhr.send(fd);
}
function fmtUptime(sec) {
var d = Math.floor(sec / 86400), h = Math.floor((sec % 86400) / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
function p2(n) { return (n < 10 ? '0' : '') + n; }
return (d > 0 ? d + 'd ' : '') + p2(h) + ':' + p2(m) + ':' + p2(s);
}

var cycleInput = $('#cycleConfig'), cycleRows = $('#cycleRows');
var cycleNames = {0:'Mario',1:'Standard',2:'Large',3:'Space Invaders',5:'Arkanoid',6:'Pac-Man',7:'Snake',8:'Tetris',10:'Asteroids',11:'Dino Runner',12:'Matrix Rain',14:'Weather',15:'Bomberman',16:'TRON',17:'Doom Fire'};
var cycleItems = [];
function initCycle() {
 cycleItems = cycleInput.value.split(',').map(function(v) { var p=v.split(':'); return {id:Number(p[0]),seconds:Number(p[1]),enabled:Number(p[1])>0}; });
 if (!cycleItems.some(function(v){return v.id===15;})) cycleItems.push({id:15,seconds:300,enabled:false});
 if (!cycleItems.some(function(v){return v.id===16;})) cycleItems.push({id:16,seconds:300,enabled:false});
 if (!cycleItems.some(function(v){return v.id===17;})) cycleItems.push({id:17,seconds:300,enabled:false});
 drawCycle();
}
function saveCycle() { cycleInput.value=cycleItems.map(function(v){return v.id+':'+(v.enabled?v.seconds:0);}).join(','); cycleInput.dispatchEvent(new Event('change',{bubbles:true})); }
function drawCycle() {
 cycleRows.innerHTML='';
 cycleItems.forEach(function(v,i) {
  var row=document.createElement('div'); row.style.cssText='display:flex;align-items:center;gap:8px;margin:8px 0;flex-wrap:wrap';
  var check=document.createElement('input'); check.type='checkbox'; check.checked=v.enabled; check.setAttribute('aria-label','Include '+cycleNames[v.id]);
  check.onchange=function(){v.enabled=check.checked;if(!v.seconds)v.seconds=300;saveCycle();}; row.appendChild(check);
  var label=document.createElement('span');label.textContent=cycleNames[v.id];label.style.minWidth='125px';row.appendChild(label);
  var duration=document.createElement('input');duration.type='number';duration.min=5;duration.max=3600;duration.value=v.seconds||300;duration.style.width='88px';duration.setAttribute('aria-label',cycleNames[v.id]+' seconds');
  duration.onchange=function(){v.seconds=Math.max(5,Math.min(3600,Number(duration.value)||300));duration.value=v.seconds;saveCycle();};row.appendChild(duration);
  var unit=document.createElement('span');unit.textContent='seconds';row.appendChild(unit);
  [-1,1].forEach(function(direction){var b=document.createElement('button');b.type='button';b.className='btn';b.textContent=direction<0?'Up':'Down';b.disabled=i+direction<0||i+direction>=cycleItems.length;b.onclick=function(){var other=cycleItems[i+direction];cycleItems[i+direction]=v;cycleItems[i]=other;saveCycle();drawCycle();};row.appendChild(b);});
  cycleRows.appendChild(row);
 });
}
function showCycle(){ $('#cycleSettings').style.display=$('#clockStyle').value==='9'?'':'none'; }
$('#clockStyle').addEventListener('change',showCycle);syncs.push(showCycle);
function updateDiagnostics(d) {
 var reset={1:'Power on',3:'Software restart',4:'Panic',5:'Interrupt watchdog',6:'Task watchdog',7:'Watchdog',9:'Brownout'};
 var lines=['Firmware: '+d.version+' ('+d.build+')','Chip: '+d.chip,'Flash: '+(d.flashBytes/1048576).toFixed(0)+' MiB','Firmware size: '+Math.round(d.firmwareBytes/1024)+' KiB','Free heap: '+Math.round(d.freeHeap/1024)+' KiB','Lowest heap: '+Math.round(d.minFreeHeap/1024)+' KiB','Largest block: '+Math.round(d.largestHeapBlock/1024)+' KiB','Storage: '+Math.round(d.animationFreeBytes/1024)+' / '+Math.round(d.animationStorageBytes/1024)+' KiB free','Reset: '+(reset[d.resetReason]||d.resetReason),'Time synced: '+(d.ntpSynced?'yes':'no'),'Animation: '+(d.animationPlaying?'playing':'idle')];
 if(d.animationFailureCode)lines.push('Playback error code: '+d.animationFailureCode);
 if(d.lastAnimationError)lines.push('Last upload error: '+d.lastAnimationError);
 if(d.weatherValid)lines.push('Weather age: '+d.weatherAgeSeconds+'s');
 $('#diagnosticsText').textContent=lines.join('\n');
}

var fwGames = null;
function refreshStatus() {
fetch('/api/info').then(function (r) { return r.json(); }).then(function (d) {
updateDiagnostics(d);
if (d.ip) { var e = $('#srIp'); if (e) e.textContent = d.ip; }
if (d.hostname) { var h = $('#srHost'); if (h) h.textContent = String(d.hostname).replace(/\.local$/, ''); }
if (typeof d.uptime === 'number') { var u = $('#srUptime'); if (u) u.textContent = fmtUptime(d.uptime); }
if (typeof d.rssi === 'number') { var rs = $('#srRssi'); if (rs) rs.textContent = d.rssi + ' dBm'; }
if (typeof d.freeHeap === 'number') { var fh = $('#fwHeap'); if (fh) fh.textContent = (d.freeHeap / 1024).toFixed(1) + ' KB'; }
if (typeof d.gameMode === 'boolean') { fwGames = d.gameMode; var fv = $('#fwVariant'); if (fv) fv.textContent = d.gameMode ? 'clock + games' : 'clock only'; }
}).catch(function () {});
fetch('/api/status').then(function (r) { return r.json(); }).then(function (d) {
var led = $('#srLed'), title = $('#srTitle');
if (led) { led.classList.toggle('online', !!d.pcOnline); led.classList.toggle('offline', !d.pcOnline); }
if (title) title.textContent = (d.pcOnline ? 'PC online' : 'PC offline') + ' · ' + (d.mode || 'clock');
}).catch(function () {});
}
function colorRow(slot, label, hex) {
 return '<label style="display:flex;align-items:center;justify-content:space-between;gap:12px;padding:5px 0"><span>' + esc(label) +
  '</span><input type="color" name="color_' + slot + '" value="' + hex + '"></label>';
}
function colorRows(d, style) {
 return d.colorRows.filter(function (r) { return r[1] === style; })
  .map(function (r) { return colorRow(r[0], r[2], d.spriteColors[r[0]]); }).join('');
}
function place(id, html) { var el = $('#' + id); if (el) el.innerHTML = html; }
function buildColors(d) {
 // The Clock page's card: each style's sprite rows and its time-digit row, both
 // revealed by syncClockPanels(), as buildColorsCard() used to emit them.
 var html = '<div class="card"><h2 class="card-title">Colors</h2>';
 d.styleCards.forEach(function (c) {
  var rows = colorRows(d, c[0]);
  if (rows) html += '<div id="' + c[1] + 'Colors" style="display:none">' + rows + '</div>';
 });
 d.digitRows.forEach(function (r) {
  html += '<div class="digitc" data-ds="' + r[0] + '" style="display:none">' +
   colorRow(r[1], 'Time digits + colon', d.spriteColors[r[1]]) + '</div>';
 });
 html += '<label style="display:flex;align-items:center;gap:8px;margin-top:12px">' +
  '<input type="checkbox" name="resetSpriteColors" value="1"> Reset all sprite colors to defaults</label></div>';
 place('colorsClock', html);
 place('colorsDate', colorRows(d, -1));
 var pc = colorRows(d, -2);
 place('colorsPc', pc ? '<div class="card"><h2 class="card-title">Colors</h2>' + pc + '</div>' : '');
 place('colorsViz', colorRows(d, -3));
 place('colorsScope', colorRows(d, -4));
 place('colorsLed', colorRows(d, -5));
}
function fillOptions(sel, list) {
 if (!sel) return;
 sel.innerHTML = '';
 list.forEach(function (o) { var e = document.createElement('option'); e.value = o[0]; e.textContent = o[1]; sel.appendChild(e); });
}
function applyValues(d) {
 var el = form.elements, v = d.form, i, hours = [], trail = [];
 // The minimum first: a range clamps a value below it.
 if (el.namedItem('displayBrightness')) el.namedItem('displayBrightness').min = d.minBright;
 if (typeof d.ledMaxCount === 'number') {
  ledMaxCount = d.ledMaxCount;
  if (el.namedItem('ledCount')) el.namedItem('ledCount').max = d.ledMaxCount;
 }
 if (typeof d.ledMaxMa === 'number' && el.namedItem('ledMaxMilliamps')) el.namedItem('ledMaxMilliamps').max = d.ledMaxMa;
 if (typeof d.ledMaPerLed === 'number') ledMaPerLed = d.ledMaPerLed;
 if (Array.isArray(d.ledPins)) ledPins = d.ledPins;
 if (typeof d.ledStripSlot === 'number') ledSlots = [d.ledStripSlot, d.ledSecondsSlot];
 if (typeof d.ledVuGainMin === 'number' && el.namedItem('ledVuGain')) {
  el.namedItem('ledVuGain').min = d.ledVuGainMin;
  el.namedItem('ledVuGain').max = d.ledVuGainMax;
 }
 if (typeof d.ledVuStartMax === 'number' && el.namedItem('ledVuStartS')) el.namedItem('ledVuStartS').max = d.ledVuStartMax;
 if (typeof d.ledVuStopMax === 'number' && el.namedItem('ledVuStopS')) {
  el.namedItem('ledVuStopS').min = d.ledVuStopMin;
  el.namedItem('ledVuStopS').max = d.ledVuStopMax;
 }
 if (el.namedItem('dimBrightness')) el.namedItem('dimBrightness').min = d.minBright;
 for (i = 0; i < 24; i++) hours.push([i, i + ':00']);
 fillOptions(el.namedItem('ambientStartHour'), hours);
 fillOptions(el.namedItem('ambientEndHour'), hours);
 for (i = 0; i <= d.scopeTrailMax; i++) trail.push([i, i + (i === d.scopeTrailDefault ? ' (default)' : '')]);
 fillOptions(el.namedItem('scopeTrail'), trail);
 fillOptions(el.namedItem('timezoneRegion'),
  [['', '-- Select Region --']].concat(d.timezones.map(function (n, k) { return [k, n]; })));
 buildColors(d);
 Object.keys(v).forEach(function (n) {
  var c = el.namedItem(n);
  if (!c) return;
  if (n === 'ambientCustomFile') c.dataset.cur = v[n];   // its options come from /api/anim/list
  else if (c.type === 'checkbox') c.checked = !!v[n];
  else {
   c.value = v[n];
   // No option matches: the first, as a select with none marked selected showed.
   if (c.tagName === 'SELECT' && c.selectedIndex < 0) c.selectedIndex = 0;
  }
 });
 document.title = 'AnimatedPixelClock - Config Portal v' + d.ver;
 ['tbVer', 'aboutVer', 'fwVer'].forEach(function (id) { var e = $('#' + id); if (e) e.textContent = 'v' + d.ver; });
 var e2 = $('#fwBuilt'); if (e2) e2.textContent = d.built;
 var e3 = $('#fwBoard'); if (e3) e3.textContent = d.board + ' \u00b7 ' + d.displayModel;
 var e4 = $('#srHost'); if (e4) e4.textContent = v.deviceName;
 var e5 = $('#hostPreview'); if (e5) e5.textContent = String(v.deviceName || 'pixelclock').toLowerCase();
 syncs.forEach(function (fn) { fn(); });
 $$('input[type="range"]').forEach(function (inp) { fmtRange(inp); });
 initCycle();
}
function loadValues() {
 if (saveBtn) saveBtn.disabled = true;
 markClean('Loading settings...');
 fetch('/api/portal', { cache: 'no-store' }).then(function (r) {
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
 }).then(function (d) {
  applyValues(d);
  loaded = true;
  if (saveBtn) saveBtn.disabled = false;
  markClean();
  animRefresh();
  loadMetrics();
  panelLoad();
 }).catch(function (err) {
  saveMeta.classList.remove('clean');
  $('.txt', saveMeta).textContent = 'Settings did not load (' + (err && err.message ? err.message : err) + ') - retrying';
  setTimeout(loadValues, 3000);
 });
}
loadValues();

refreshStatus();
setInterval(refreshStatus, 5000);
})();
