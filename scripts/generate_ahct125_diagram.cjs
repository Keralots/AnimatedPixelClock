// DIY S3-Zero board: 14 HUB75 signals buffered by 4x SN74AHCT125N at 5V.
// Run with Node.js from the project root or anywhere: node generate_ahct125_diagram.cjs
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const out = path.join(root, 'docs', 'img', 's3zero_ahct125_wiring');

const hdr = fs.readFileSync(path.join(root, 'src/display/hub75_pins.h'), 'utf8').split('#else')[1];
const gpio = {};
for (const m of hdr.matchAll(/#define HUB75_PIN_(\w+)\s+(\d+)/g)) gpio[m[1]] = Number(m[2]);
const hub = {R1:1, G1:2, B1:3, R2:5, G2:6, B2:7, E:8, A:9, B:10, C:11, D:12, CLK:13, LAT:14, OE:15};
// GPIO17: no strap or USB/UART duty, not the onboard RGB LED (21), not a HUB75 line.
const LED_PIN = 17;
if (Object.keys(gpio).length !== 14 || Object.keys(hub).some(s => gpio[s] === undefined)) throw Error('Firmware pin map differs');

const col = {R1:'#d44252', R2:'#d44252', G1:'#15865c', G2:'#15865c', B1:'#3277cf', B2:'#3277cf',
  A:'#a86912', B:'#a86912', C:'#a86912', D:'#a86912', E:'#a86912', CLK:'#7951b0', LAT:'#7951b0', OE:'#7951b0', LED:'#c2410c'};
const V5 = '#cf3d43', GND = '#344255', MUTED = '#52677a', INK = '#1c3043', TEAL = '#167b87';

// Channel n: [OE pin, A pin, Y pin] on the SN74AHCT125 (DIP-14 / SOIC-14).
const ch = {1:[1,2,3], 2:[4,5,6], 3:[10,9,8], 4:[13,12,11]};
const chips = [
  ['U1', ['R1', 'G1', 'B1', 'R2']],
  ['U2', ['G2', 'B2', 'A', 'B']],
  ['U3', ['C', 'D', 'E', 'CLK']],
  ['U4', ['LAT', 'OE', 'LED', null]],
];
const src = {};
chips.forEach(([u, sigs]) => sigs.forEach((s, i) => { if (s) src[s] = [u, i + 1]; }));
if (Object.values(gpio).includes(LED_PIN)) throw Error('LED pin collides with HUB75');
gpio.LED = LED_PIN;

const a = [];
const esc = s => String(s).replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
const rect = (x, y, w, h, fill = '#fff', stroke = 'none', r = 14) => a.push(`<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}" stroke="${stroke}" stroke-width="2"/>`);
const text = (x, y, s, size = 20, fill = INK, weight = 400, anchor = 'start') => a.push(`<text x="${x}" y="${y}" font-family="Segoe UI,Arial,sans-serif" font-size="${size}" fill="${fill}" font-weight="${weight}" text-anchor="${anchor}">${esc(s)}</text>`);
const line = (d, c = GND, w = 4) => a.push(`<path d="${d}" fill="none" stroke="${c}" stroke-width="${w}" stroke-linejoin="round" stroke-linecap="round"/>`);
const dot = (x, y, c, r = 6) => a.push(`<circle cx="${x}" cy="${y}" r="${r}" fill="${c}"/>`);

const W = 1600, H = 2400;
a.push(`<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" role="img" aria-labelledby="title desc"><title id="title">S3-Zero HUB75 level shifter wiring</title><desc id="desc">Waveshare ESP32-S3-Zero driving a HUB75E panel through four SN74AHCT125N buffers powered from 5V. All fourteen panel signals and the WS2812B strip data line are buffered.</desc>`);
rect(0, 0, W, H, '#edf2f5', 'none', 0);
rect(0, 0, W, 151, '#142c3e', 'none', 0);
text(56, 49, 'ANIMATED PIXEL CLOCK  •  DIY S3-ZERO BOARD', 17, '#75d3cc', 700);
text(56, 101, 'Level shifter wiring', 42, '#fff', 700);
text(1544, 65, '4 × SN74AHCT125N  •  3.3V → 5V', 22, '#fff', 600, 'end');
text(1544, 103, '14 HUB75 signals + WS2812B data  •  env matrix-s3', 19, '#c4d4de', 400, 'end');

// 01 power
rect(40, 175, 1520, 420);
text(64, 213, '01   POWER & SIGNAL FLOW', 21, INK, 700);
text(64, 243, 'One 5V supply feeds the ESP, the buffer chips and the panel. All grounds are common.', 18, MUTED);
rect(80, 285, 250, 150, '#fff2e9', '#edcebb');
text(205, 330, '5V SUPPLY', 24, INK, 700, 'middle');
text(205, 362, 'USB-C breakout / PSU', 17, MUTED, 400, 'middle');
text(205, 390, '+ 2200µF across rails', 16, MUTED, 400, 'middle');
rect(470, 285, 250, 150, '#e8f2f5', '#bdd3df');
text(595, 330, 'ESP32-S3-Zero', 24, INK, 700, 'middle');
text(595, 362, '3.3V GPIO outputs', 17, MUTED, 400, 'middle');
text(595, 390, 'powered on 5V pin', 16, MUTED, 400, 'middle');
rect(860, 285, 280, 150, '#f1ecf8', '#d3c6e6');
text(1000, 330, 'U1 - U4', 24, INK, 700, 'middle');
text(1000, 362, 'SN74AHCT125N @ 5V', 17, MUTED, 400, 'middle');
text(1000, 390, '100nF at each VCC pin', 16, MUTED, 400, 'middle');
rect(1280, 285, 240, 150, '#e8f4ef', '#bddbce');
text(1400, 330, 'PANEL JIN', 24, INK, 700, 'middle');
text(1400, 362, 'HUB75E, 5V logic', 17, MUTED, 400, 'middle');
text(1400, 390, 'own power socket(s)', 16, MUTED, 400, 'middle');
line('M720 360 H855', TEAL, 5); text(788, 350, '14 × 3.3V', 16, TEAL, 700, 'middle'); text(788, 385, 'into A', 15, MUTED, 400, 'middle');
line('M1140 360 H1275', TEAL, 5); text(1208, 350, '14 × 5V', 16, TEAL, 700, 'middle'); text(1208, 385, 'from Y', 15, MUTED, 400, 'middle');
line('M1270 360 l-10 -8 M1270 360 l-10 8', TEAL, 4); line('M850 360 l-10 -8 M850 360 l-10 8', TEAL, 4);
// rails
line(`M205 435 V480 H1470 V435`, V5, 5); dot(595, 480, V5); dot(1000, 480, V5);
line('M595 480 V435', V5, 4); line('M1000 480 V435', V5, 4);
line(`M150 435 V530 H1340 V435`, GND, 5); dot(560, 530, GND); dot(960, 530, GND);
line('M560 530 V435', GND, 4); line('M960 530 V435', GND, 4);
// White gaps: the ground drops cross the +5V rail without connecting.
for (const x of [560, 960, 1340]) { line(`M${x} 470 V490`, '#fff', 12); line(`M${x} 468 V492`, GND, 4); }
text(1480, 470, '+5V', 17, V5, 700); text(1350, 555, 'GND (also HUB75 pins 4 + 16)', 16, GND, 700, 'end');
text(80, 575, 'Panel and LED strip power go by their own cables straight from the supply, never through the ESP, the chips or the ribbon.', 17, MUTED);

// 02 chips
const CY = 620;
rect(40, CY, 1520, 970);
text(64, CY + 38, '02   BUFFER CHIPS  (top view, notch up, pin 1 top-left)', 21, INK, 700);
text(64, CY + 68, 'A = input from ESP (3.3V)  •  Y = output to panel (5V)  •  every ~OE LOW = always enabled', 18, MUTED);
const pinName = {1:'1OE', 2:'1A', 3:'1Y', 4:'2OE', 5:'2A', 6:'2Y', 7:'GND', 8:'3Y', 9:'3A', 10:'3OE', 11:'4Y', 12:'4A', 13:'4OE', 14:'VCC'};
function net(p, sigs) {
  if (p === 14) return ['+5V', V5];
  if (p === 7) return ['GND', GND];
  for (const [n, [oe, A, Y]] of Object.entries(ch)) {
    const s = sigs[n - 1];
    if (p === oe) return ['GND', GND];
    if (p === A) return s ? [`GPIO ${gpio[s]}  (${s})`, col[s]] : ['GND  (spare)', GND];
    if (p === Y && s === 'LED') return ['330Ω → strip DIN', col.LED];
    if (p === Y) return s ? [`${s}  →  JIN ${hub[s]}`, col[s]] : ['n/c', '#9aa7b3'];
  }
}
chips.forEach(([u, sigs], k) => {
  const x0 = 70 + (k % 2) * 740, y0 = CY + 100 + Math.floor(k / 2) * 430;
  rect(x0, y0, 720, 410, '#f6f8fa', '#d6dee5');
  text(x0 + 24, y0 + 38, u, 26, INK, 700);
  text(x0 + 70, y0 + 38, sigs.filter(Boolean).join(' • ') + (sigs.includes(null) ? '  (+1 spare)' : ''), 18, MUTED, 600);
  const bx = x0 + 285, bw = 150, by = y0 + 62, pitch = 42;
  rect(bx, by, bw, 7 * pitch + 12, '#142c3e', 'none', 8);
  a.push(`<path d="M${bx + bw / 2 - 16} ${by} a16 16 0 0 0 32 0" fill="#f6f8fa"/>`);
  dot(bx + 18, by + 22, '#6b7f90', 5);
  for (let i = 0; i < 7; i++) {
    const y = by + 27 + i * pitch;
    for (const side of ['L', 'R']) {
      const p = side === 'L' ? i + 1 : 14 - i;
      const [label, c] = net(p, sigs);
      const px = side === 'L' ? bx : bx + bw;
      rect(side === 'L' ? px - 22 : px, y - 7, 22, 14, '#b8c3cc', 'none', 2);
      text(side === 'L' ? px + 8 : px - 8, y + 6, pinName[p], 14, '#dfe8ef', 600, side === 'L' ? 'start' : 'end');
      text(side === 'L' ? px - 28 : px + 28, y + 6, p, 15, INK, 700, side === 'L' ? 'end' : 'start');
      const lx = side === 'L' ? px - 52 : px + 52, ex = side === 'L' ? x0 + 20 : x0 + 700;
      line(`M${lx} ${y} H${side === 'L' ? ex + 185 : ex - 185}`, c, 3);
      text(side === 'L' ? ex : ex, y + 6, label, 16, c, 700, side === 'L' ? 'start' : 'end');
    }
  }
  text(x0 + 360, y0 + 392, 'Pin 14 → +5V, pin 7 → GND, 100nF from 14 to 7 close to the chip', 15, MUTED, 400, 'middle');
});

// 03 panel header
const PY = CY + 1000;
rect(40, PY, 1520, 500);
text(64, PY + 38, '03   PANEL JIN  (HUB75E, 2 × 8)', 21, INK, 700);
text(64, PY + 68, 'Every signal pin is fed from a buffer Y output. Check the pin-1 mark on the panel PCB before wiring.', 18, MUTED);
const order = ['R1', 'G1', 'B1', null, 'R2', 'G2', 'B2', 'E', 'A', 'B', 'C', 'D', 'CLK', 'LAT', 'OE', null];
rect(703, PY + 95, 194, 376 - 10, '#142c3e', 'none', 10);
for (let i = 0; i < 16; i++) {
  const s = order[i], left = i % 2 === 0, y = PY + 120 + Math.floor(i / 2) * 44, c = s ? col[s] : GND;
  const label = s ? `${src[s][0]} pin ${ch[src[s][1]][2]}  (${src[s][1]}Y)` : 'GND (common)';
  rect(left ? 180 : 1195, y - 18, 225, 36, '#f3f6f8', 'none', 8);
  text(left ? 292 : 1307, y + 7, label, 19, c, 600, 'middle');
  line(left ? `M405 ${y} H730` : `M870 ${y} H1195`, c, 3);
  dot(left ? 730 : 870, y, c, 8);
  text(left ? 555 : 1045, y - 8, s ? (s === 'LAT' ? 'LAT / STB' : s) : 'GND', 18, c, 700, 'middle');
  text(left ? 752 : 848, y + 6, i + 1, 17, '#fff', 600, left ? 'start' : 'end');
}

// notes
const NY = PY + 545;
text(64, NY, 'NOTES', 17, TEAL, 700);
text(160, NY, 'AHCT inputs switch at TTL levels (VIH 2.0V), so 3.3V GPIO drives them cleanly and the outputs swing to 5V.', 18, INK, 600);
text(64, NY + 34, 'Use AHCT or HCT only; plain HC/AC at 5V do not reliably accept 3.3V. Unused A inputs to GND, unused Y outputs open.', 17, MUTED);
text(64, NY + 64, 'Keep ESP → chip and chip → panel wires short, CLK shortest. Never power the chips from 3.3V.', 17, MUTED);
text(64, NY + 124, `WS2812B: U4 3A ← GPIO ${gpio.LED} (set LED pin to ${gpio.LED} in the portal), 3Y → 330Ω at the strip end → DIN. Strip +5V/GND from the supply, GND common.`, 17, MUTED);
text(64, NY + 154, `GPIO ${gpio.LED} chosen over the default 21 (S3-Zero onboard RGB LED) and 46 (boot strap). Firmware default stays 21, so change it once in the portal.`, 17, MUTED);
text(64, NY + 94, 'Firmware for the NONDK panel after the buffer: driver 0 (generic), color depth 8, min refresh 60 (Display panel card).', 17, MUTED);
text(64, NY + 196, 'Sources: src/display/hub75_pins.h default map, SN74AHCT125 datasheet pinout, HUB75E pinout', 15, MUTED);
text(1536, NY + 196, 'AnimatedPixelClock  /  2026-09-29', 15, MUTED, 400, 'end');

a.push('</svg>');
fs.writeFileSync(out + '.svg', a.join('\n'));
console.log('Wrote ' + out + '.svg');
