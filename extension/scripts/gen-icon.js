// Regenerates icon.png from icon.svg (icon.png is a gitignored build artifact).
// Requires @resvg/resvg-js (prebuilt binaries, no system deps).
// Run from the plugin dir:  node scripts/gen-icon.js
const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');

const root = path.resolve(__dirname, '..');
const svg = fs.readFileSync(path.join(root, 'icon.svg'), 'utf8');
const resvg = new Resvg(svg, { fitTo: { mode: 'width', value: 128 } });
const png = resvg.render().asPng();
fs.writeFileSync(path.join(root, 'icon.png'), png);
console.log('Wrote icon.png (' + png.length + ' bytes)');
