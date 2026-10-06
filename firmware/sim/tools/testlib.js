// a small made-up library for headless tests
const sharp = require(require('child_process').execSync('npm root -g').toString().trim() + '/sharp');
const GAMES = ['Neon Run', 'Castle Quest', 'Deep Dive', 'Robo Blast', 'Star Raider', 'Turbo Kart', 'Bubble Town', 'Pixel Pirates', 'Moon Miner', 'Zap Zone', 'Frost Byte', 'Gold Rush'];
function adf(name) {                       // an 880 KB AmigaDOS-looking image (boot block + a name), enough for the GTi
  const b = new Uint8Array(901120);
  b.set([0x44, 0x4F, 0x53, 0x00], 0);
  const t = new TextEncoder().encode(name); b.set(t, 880 * 512 + 432);
  return b;
}
async function cover(name, i) {
  const hue = (i * 47) % 360;
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="300" height="400"><defs><linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="hsl(${hue},70%,25%)"/><stop offset="1" stop-color="hsl(${(hue + 60) % 360},80%,55%)"/></linearGradient></defs><rect width="300" height="400" fill="url(#g)"/><text x="150" y="80" font-family="sans-serif" font-size="34" font-weight="bold" fill="#fff" text-anchor="middle">${name}</text><circle cx="150" cy="240" r="${60 + i * 3}" fill="rgba(255,255,255,0.25)"/></svg>`;
  return new Uint8Array(await sharp(Buffer.from(svg)).jpeg({ quality: 80, progressive: false }).toBuffer());
}
exports.build = async function (kind) {
  const files = []; const now = Date.now();
  const add = (path, data) => files.push({ path, size: data.length, mtime: now, src: data });
  const n = kind === 'tiny' ? 3 : GAMES.length;
  for (let i = 0; i < n; i++) {
    const g = GAMES[i];
    if (i % 3 === 1) { add(`ADF/${g}/${g} (Disk 1 of 2).adf`, adf(g)); add(`ADF/${g}/${g} (Disk 2 of 2).adf`, adf(g + ' 2')); }
    else add(`ADF/${g}/${g}.adf`, adf(g));
    add(`ADF/${g}/${g}.jpg`, await cover(g, i));
    add(`ADF/${g}/${g}.nfo`, new TextEncoder().encode(`${g}\n\nA made-up game for the GTi simulator. ${g} has you racing, jumping and saving the day across twelve levels of pure Amiga joy.\n\nYear: 199${i % 10}\nGenre: Arcade\n`));
  }
  return files;
};
