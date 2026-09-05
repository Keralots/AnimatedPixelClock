// Run with Node.js; add --png to also render using the optional sharp package.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const out = path.join(root, 'docs', 'img', 'hub75_connection_diagram');
const colors = {R1:'#d44252', R2:'#d44252', G1:'#15865c', G2:'#15865c', B1:'#3277cf', B2:'#3277cf', A:'#a86912', B:'#a86912', C:'#a86912', D:'#a86912', E:'#a86912', CLK:'#7951b0', LAT:'#7951b0', OE:'#7951b0', GND:'#344255'};
const pins = [['R1',1],['G1',2],['B1',4],['GND',null],['R2',5],['G2',6],['B2',7],['E',12],['A',8],['B',9],['C',10],['D',11],['CLK',13],['LAT',14],['OE',38],['GND',null]];
// Refuse to generate a stale diagram if the firmware or documentation changes.
const header = fs.readFileSync(path.join(root,'src/display/matrix_display.h'),'utf8');
const actual = header.match(/i2s_pins pins = \{([\s\S]*?)\}/)[1].replace(/\/\/[^\n]*/g,'').match(/\d+/g).map(Number);
const order = ['R1','G1','B1','R2','G2','B2','A','B','C','D','E','LAT','OE','CLK'];
if (JSON.stringify(actual)!==JSON.stringify(order.map(s=>pins.find(p=>p[0]===s)[1]))) throw Error('Firmware pin map differs');
const guide = fs.readFileSync(path.join(root,'docs/HUB75_WIRING.md'),'utf8');
pins.forEach(([s,g],i)=>{if(g!==null && !new RegExp(`\\| ${s}\\s*\\| ${g}\\s*\\| ${i+1}\\s*\\|`).test(guide)) throw Error(`Guide differs: ${s}`);});
const a = ['<svg xmlns="http://www.w3.org/2000/svg" width="1600" height="1550" viewBox="0 0 1600 1550" role="img" aria-labelledby="title desc"><title id="title">AnimatedPixelClock connection diagram</title><desc id="desc">A phone charger feeds a separate USB-C breakout. Its 5V and ground rails power the ESP32-S3 and both HUB75E panels. A 2200 microfarad capacitor is across the rails; a two-pole output connector feeds the panels. All sixteen input connector pins are labeled with their GPIO connections.</desc><defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0 0 L8 4 L0 8" fill="#167b87"/></marker></defs>'];
const esc=s=>String(s).replaceAll('&','&amp;').replaceAll('<','&lt;');
function rect(x,y,w,h,fill='#fff',stroke='none',r=14){a.push(`<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}" stroke="${stroke}"/>`);}
function text(x,y,s,size=20,fill='#1c3043',weight=400,anchor='start'){a.push(`<text x="${x}" y="${y}" font-family="Segoe UI,Arial,sans-serif" font-size="${size}" fill="${fill}" font-weight="${weight}" text-anchor="${anchor}">${esc(s)}</text>`);}
function line(d,c='#344255',w=4,arrow=false){a.push(`<path d="${d}" fill="none" stroke="${c}" stroke-width="${w}" stroke-linejoin="round" stroke-linecap="round"${arrow?' marker-end="url(#arrow)"':''}/>`);}
function dot(x,y,c,r=6){a.push(`<circle cx="${x}" cy="${y}" r="${r}" fill="${c}"/>`);}
rect(0,0,1600,1550,'#edf2f5','none',0);
rect(0,0,1600,151,'#142c3e','none',0);
text(56,49,'ANIMATED PIXEL CLOCK',17,'#75d3cc',700);
text(56,101,'Connection diagram',42,'#fff',700);
text(1544,65,'ESP32-S3  /  HUB75E',22,'#fff',600,'end');
text(1544,103,'2 × 64 × 64 panels  •  128 × 64 canvas',19,'#c4d4de',400,'end');
rect(40,175,1520,585);
text(64,213,'01   PANEL CHAIN & POWER',21,'#1c3043',700);
text(64,243,'Your prototype • phone charger → separate USB-C breakout → shared 5V / GND distribution',18,'#52677a');
rect(80,286,275,165,'#e8f2f5','#bdd3df');
text(217,323,'ESP32-S3',27,'#1c3043',700,'middle');
text(217,355,'WROOM / Zero / compatible Mini',16,'#52677a',400,'middle');
text(217,395,'Onboard USB: programming',16,'#52677a',400,'middle');
text(220,436,'5V',16,'#cf3d43',700,'middle');
text(300,436,'GND',16,'#344255',600,'middle');
for (const [x,n,range] of [[580,1,'x = 0-63 • left half'],[1120,2,'x = 64-127 • right half']]) {
  rect(x,286,390,165,'#e8f4ef','#bddbce');
  text(x+195,324,`PANEL ${n}`,25,'#1c3043',700,'middle');
  text(x+195,354,'Waveshare P2.5 • 64 × 64',18,'#52677a',400,'middle');
  text(x+195,384,range,17,'#52677a',400,'middle');
  text(x+15,419,'JIN',17,'#167b87',700);text(x+330,419,'JOUT',17,'#167b87',700);
  text(x+135,438,'+5V',16,'#cf3d43',700,'middle');text(x+215,438,'GND',16,'#344255',700,'middle');
}
line('M355 411 H575','#167b87',4,true);text(466,361,'GPIO signals',18,'#167b87',600,'middle');text(466,386,'see detail below',16,'#52677a',400,'middle');
line('M970 411 H1115','#167b87',4,true);text(1045,361,'16-pin ribbon',17,'#167b87',600,'middle');text(1045,386,'JOUT → JIN',16,'#52677a',400,'middle');
text(1510,475,'Panel 2 JOUT: unused',15,'#52677a',400,'end');
rect(80,490,275,145,'#fff2e9','#edcebb');
text(102,521,'USB-C BREAKOUT',21,'#1c3043',700);
text(102,551,'Separate power input',17,'#52677a');
text(102,583,'↑ Phone charger • 5V',18,'#1c3043',600);
text(102,613,'D+ / D− unused',16,'#52677a');
rect(655,520,45,103,'#ffe09a','#caa851',6);
line('M355 535 H1235 V451','#cf3d43',5);
line('M715 535 V451','#cf3d43',5);dot(715,535,'#cf3d43');
line('M220 451 V466 H370 V535','#cf3d43',4);dot(370,535,'#cf3d43');
line('M355 607 H1335 V451','#344255',5);
line('M795 607 V451','#344255',5);dot(795,607,'#344255');
// A white gap makes the ground / +5V crossing unambiguously unconnected.
line('M795 527 V543','#fff',10);line('M795 527 V543','#344255',5);
line('M395 527 V543','#fff',10);
line('M362 477 H378','#fff',10);
line('M300 451 V477 H395 V607','#344255',4);dot(395,607,'#344255');
// Polarized reservoir capacitor in parallel with the distribution rails.
line('M470 535 V565','#cf3d43',3);dot(470,535,'#cf3d43');
line('M450 565 H490','#344255',3);line('M450 579 H490','#344255',3);
line('M470 579 V607','#344255',3);dot(470,607,'#344255');
text(443,559,'+',17,'#cf3d43',700);text(502,574,'2200µF',19,'#1c3043',600);text(502,596,'25V rated',15,'#52677a');
text(678,654,'2-pole panel power output',17,'#52677a',600,'middle');
text(678,678,'Yellow connector on prototype',15,'#52677a',400,'middle');
text(1050,654,'Dedicated power feed to each panel',17,'#52677a',600,'middle');
text(1050,678,'HUB75 ribbon carries signals, not panel power',16,'#52677a',400,'middle');
text(80,729,'Functional wiring layout • capacitor + to 5V, − to GND • connector contact positions are schematic',18,'#52677a');
a.push('<g transform="translate(0 150)">');
rect(40,632,1520,590);
text(64,673,'02   EXACT PIN CONNECTIONS',21,'#1c3043',700);
text(64,703,'GPIO numbers are chip numbers, not physical board-header positions. Both side columns belong to the same ESP32-S3.',18,'#52677a');
text(290,750,'ESP32-S3',19,'#52677a',700,'middle');text(1310,750,'ESP32-S3',19,'#52677a',700,'middle');
text(800,750,'PANEL 1 • JIN',20,'#1c3043',700,'middle');
rect(703,767,194,376,'#142c3e','none',10);
for(let i=0;i<16;i++){
  const [s,g] = pins[i], left=i%2===0, y=792+Math.floor(i/2)*46, c=colors[s];
  const label=g===null?'GND (common)':`GPIO ${g}`;
  rect(left?180:1195,y-19,225,37,'#f3f6f8','none',8);
  text(left?292:1307,y+7,label,21,c,600,'middle');
  line(left?`M405 ${y} H730`:`M870 ${y} H1195`,c,3);
  dot(left?730:870,y,c,8);
  text(left?555:1045,y-9,s==='LAT'?'LAT / STB':s,19,c,700,'middle');
  text(left?752:848,y+6,i+1,17,'#fff',600,left?'start':'end');
}
text(695,781,'1 ▸',16,'#1c3043',700,'end');
text(800,1198,'Panel header view • locate the PCB pin-1 mark / notch before wiring; verify the panel silkscreen.',17,'#52677a',400,'middle');
rect(70,1143,565,44,'#fff3dc','none',9);text(88,1171,'HUB75E pin 8 is E → GPIO12, NOT ground.',20,'#855c11',700);
rect(983,1143,537,44,'#eaf0f5','none',9);text(1000,1171,'Pins 4 + 16 → shared power / ESP ground.',18,'#344255',600);
text(64,1260,'POWER ON',17,'#167b87',700);text(205,1260,'Complete wiring with power off → plug the charger into the separate USB-C input.',20,'#1c3043',600);
text(64,1296,'Keep signal wires short. This is the direct 3.3V setup; optional buffers are described in the wiring guide, section 6.',18,'#52677a');
text(64,1330,'Owner-reported use: around 10W, observed below 30W; not a measured full-brightness maximum.',18,'#52677a');
text(64,1373,'Sources: project wiring guide + firmware pin map + owner’s prototype photos and power observations',15,'#52677a');
text(1536,1373,'AnimatedPixelClock  /  2026-09-05',15,'#52677a',400,'end');
a.push('</g></svg>');fs.mkdirSync(path.dirname(out),{recursive:true});fs.writeFileSync(out+'.svg',a.join('\n'));
console.log('Verified 14 GPIO signals against firmware and wiring guide; wrote '+out+'.svg');
if(process.argv.includes('--png')) require('sharp')(Buffer.from(a.join('\n'))).png().toFile(out+'.png').then(()=>console.log('Wrote '+out+'.png'));
