// Run against tools/authoring_server.py; requires Playwright and installed Edge.
'use strict';
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({headless:true,channel:'msedge'});
 try {
  const page=await browser.newPage({viewport:{width:1600,height:1100}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(process.env.AUTHORING_URL||'http://127.0.0.1:8766/');
  await page.waitForFunction(()=>window.TranceComposer);
  assert.equal(await page.locator('#leapfrog-example').count(),0);
  for(const kind of ['draw','copy','enter'])assert.equal(await page.locator(`.palette-piece[data-piece="${kind}"]`).count(),0);
  const source='pattern p for 128f { pattern sequence for 64f seq {every 8f {image primary}} burst period 8f {base {every 8f {image secondary}} burst {image secondary anim}} }';
  await page.locator('#source').fill(source);await page.locator('#apply-source').click();
  assert.equal(await page.locator('.block[data-kind="burst"] > .block-head > .block-title').innerText(),'Burst controller');
  assert.equal(await page.locator('.block[data-kind="burst-phase"] > .block-head > .block-title').innerText(),'Interrupt phase');
  assert.equal(await page.locator('.block[data-kind="base"] > .block-head > .block-title').innerText(),'Default phase');
  assert.equal(await page.locator('.block[data-kind="pattern"]').nth(1).locator(':scope > .block-head > .block-role').innerText(),'sequence');
  const colors=await page.locator('.block').evaluateAll(nodes=>nodes.filter(n=>['pattern','every','burst'].includes(n.dataset.kind)).map(n=>getComputedStyle(n).borderLeftColor));
  assert(new Set(colors).size>=4,'parallel, sequence, repeat and exclusive composition have distinct colors');
  const before=await page.locator('#source').inputValue();
  await page.locator('.palette-piece[data-piece="word"]').dragTo(page.locator('.block[data-kind="burst"]').locator(':scope > .block-children > .drop-slot').last());
  assert.equal(await page.locator('#source').inputValue(),before,'controller rejects direct layer');
  await page.locator('.block[data-kind="burst"] > .block-head').click();
  await page.locator('.palette-piece[data-piece="base"] button').click();
  assert.equal(await page.locator('#source').inputValue(),before,'duplicate phase rejected');
  await page.locator('.block[data-kind="base"] > .block-head').click();
  await page.locator('#duplicate-block').click();
  assert.equal(await page.locator('#source').inputValue(),before,'phase duplication also guarded');
  await page.locator('.block[data-kind="burst-phase"] > .block-head').click();
  await page.locator('.palette-piece[data-piece="every"] button').click();
  assert.notEqual(await page.locator('#source').inputValue(),before,'phase accepts repeating schedule');
  assert.equal(await page.locator('.palette-piece[data-piece="sequential-pattern"] strong').innerText(),'Sequential pattern');
  await page.locator('.palette-piece[data-piece="sequential-pattern"]').dragTo(page.locator('.block[data-kind="pattern"]').first().locator(':scope > .block-children > .drop-slot').last());
  assert.equal(await page.locator('.block-title').filter({hasText:'Sequential pattern'}).count(),2);
  await page.locator('#builtin-picker').selectOption('8');
  assert(!/-> rapid/.test(await page.locator('#source').inputValue()));
  await page.locator('.block[data-kind="burst"] > .block-head').click();
  assert.equal(await page.locator('#inspector [data-field="target"]').count(),0);
  await page.locator('.block[data-kind="image"] > .block-head').first().click();
  assert.equal(await page.locator('#inspector [data-field="content"]').inputValue(),'either');
  assert.match(await page.locator('#inspector [data-field="content"] option:checked').innerText(),/Random theme/);
  assert.match(await page.locator('#inspector').innerText(),/unrelated to clocks/);
  assert.deepEqual(errors,[]);
  console.log('PASS composition role labels/colors, no redundant button or register palette, controller drop guards, exclusive phase uniqueness, schedules within phases');
 } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
