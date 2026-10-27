// Standalone prototype QA. Requires Playwright and an installed Edge browser.
// NODE_PATH may point to a shared Playwright installation. No media is played.
const { chromium } = require('playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { spawnSync } = require('node:child_process');

async function main() {
  const root = path.resolve(__dirname, '../..');
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'trance-authoring-'));
  const browser = await chromium.launch({ headless: true, channel: 'msedge' });
  try {
    const page = await browser.newPage({ viewport: { width: 1360, height: 1000 } });
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(pathToFileURL(path.join(root, 'docs/exploration/visual-authoring-prototype.html')).href);
    const source = () => page.locator('#source').inputValue();
    const initial = await source();
    assert(initial.includes('\n'), 'source contains real line breaks');
    assert(!initial.includes('\\n'), 'source does not contain escaped line breaks');
    assert.match(await page.locator('#traceDetail').innerText(), /4 foreground selections \+ 1 backdrop selection.*16 visible windows.*64 visible frames/);
    await page.locator('#import').click();
    assert.equal(await source(), initial, 'supported source round-trips');
    await page.locator('#frame').evaluate(el => { el.value = '4'; el.dispatchEvent(new Event('input')); });
    assert.match(await page.locator('#sample').innerText(), /hidden.*backdrop remains/);
    await page.locator('#frame').evaluate(el => { el.value = '48'; el.dispatchEvent(new Event('input')); });
    assert.match(await page.locator('#detail').innerText(), /zoom 0\.000/);
    await page.locator('#clock').selectOption('scene');
    assert.match(await page.locator('#detail').innerText(), /zoom 0\.125/);
    const draft = (await source()) + ' unsupported trailing input';
    await page.locator('#source').fill(draft);
    assert.equal(await page.locator('#cut').isDisabled(), true, 'text draft pauses block edits');
    await page.locator('#import').click();
    assert.equal(await source(), draft, 'unsupported draft is preserved');
    assert.equal(await page.locator('#status').getAttribute('data-error'), 'true');
    await page.locator('#reset').click();
    assert.equal(await page.locator('#cut').isDisabled(), false, 'reset resumes block edits');
    await page.locator('#from').fill('0.0000001');
    assert.equal(await page.locator('#status').getAttribute('data-error'), 'true', 'unsupported numeric precision rejected');
    await page.locator('#reset').click();
    await page.locator('#on').fill('13');
    assert.equal(await page.locator('#status').getAttribute('data-error'), 'true');
    assert.equal(await source(), initial, 'invalid knobs preserve last candidate');
    await page.locator('#reset').click();
    const executable = process.argv[2] && path.resolve(process.argv[2]);
    let linted = 0;
    for (const clock of ['cut', 'scene', 'gate']) {
      for (const theme of ['primary', 'secondary', 'runtime']) {
        for (const backdrop of [true, false]) {
          await page.locator('#clock').selectOption(clock);
          await page.locator('#theme').selectOption(theme);
          await page.locator('#backdrop').setChecked(backdrop);
          const candidate = await source();
          await page.locator('#import').click();
          assert.equal(await source(), candidate, 'all selector variants round-trip');
          if (executable) {
            const file = path.join(temporary, 'candidate.pattern');
            fs.writeFileSync(file, candidate);
            const result = spawnSync(executable, ['--lint', '--pattern=' + file], { encoding: 'utf8', cwd: temporary });
            assert.equal(result.status, 0, result.stdout + result.stderr);
            linted++;
          }
        }
      }
    }
    await page.locator('#reset').click();
    await page.locator('#duration').fill('101');
    await page.locator('#cut').fill('29');
    await page.locator('#period').fill('11');
    await page.locator('#on').fill('3');
    assert.match(await page.locator('#traceDetail').innerText(), /4 foreground selections.*11 visible windows.*33 visible frames/);
    await page.setViewportSize({ width: 390, height: 844 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, 'mobile has no horizontal overflow');
    assert.deepEqual(errors, [], 'no browser runtime errors');
    console.log(JSON.stringify({ result: 'pass', lintedVariants: linted, checks: ['source roundtrip', 'visibility', 'clock binding', 'reject/preserve draft', 'invalid controls', 'clipped timeline', 'mobile layout', 'browser errors'] }));
  } finally {
    await browser.close();
    fs.rmSync(temporary, { recursive: true, force: true });
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
