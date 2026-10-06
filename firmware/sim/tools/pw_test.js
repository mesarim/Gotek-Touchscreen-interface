// drives the web page in headless Chromium: node tools/pw_test.js <steps.json>
const { chromium } = require('playwright');
const steps = JSON.parse(require('fs').readFileSync(process.argv[2], 'utf8'));
(async () => {
  const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1400, height: 950 } });
  p.on('console', m => console.log('console:', m.text().slice(0, 300))); p.on('pageerror', e => console.log('PAGEERR', e.message));
  await p.goto('http://localhost:8766/index.html?' + Date.now()); await p.waitForTimeout(800);
  let box = null;
  const tap = async (x, y, hold) => { box = await p.locator('#scr').boundingBox(); const W = (await p.evaluate(() => document.getElementById('scr').width)), H = (await p.evaluate(() => document.getElementById('scr').height));
    await p.mouse.move(box.x + x * box.width / W, box.y + y * box.height / H); await p.mouse.down(); await p.waitForTimeout(hold || 120); await p.mouse.up(); await p.waitForTimeout(400); };
  for (const s of steps) {
    if (s.click) await p.click(s.click);
    if (s.wait) await p.waitForTimeout(s.wait);
    if (s.tap) await tap(s.tap[0], s.tap[1], s.hold);
    if (s.drag) { box = await p.locator('#scr').boundingBox(); const [x0, y0, x1, y1] = s.drag; const W = 480, H = 320;
      await p.mouse.move(box.x + x0 * box.width / W, box.y + y0 * box.height / H); await p.mouse.down();
      for (let i = 1; i <= 12; i++) { await p.mouse.move(box.x + (x0 + (x1 - x0) * i / 12) * box.width / W, box.y + (y0 + (y1 - y0) * i / 12) * box.height / H); await p.waitForTimeout(30); }
      await p.mouse.up(); await p.waitForTimeout(400); }
    if (s.shot) await p.screenshot({ path: '/tmp/gti_shots/' + s.shot + '.png', fullPage: !!s.full });
    if (s.screen) await p.locator('.case').screenshot({ path: '/tmp/gti_shots/' + s.screen + '.png' });
    if (s.eval) console.log('eval:', JSON.stringify(await p.evaluate(s.eval)).slice(0, 2000));
  }
  await b.close();
})();
