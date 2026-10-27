const assert = require('node:assert/strict');
const fs = require('node:fs');
const G = require('../grammar.js');
const {chromium} = require('playwright');
(async () => {
  const source = JSON.parse(fs.readFileSync('docs/exploration/authoring/builtins.json', 'utf8')).builtins.find(b => b.id === 2).source;
  const cpp = fs.readFileSync('src/trance/visual/builtin_patterns_v3.cpp', 'utf8');
  assert.equal(cpp.match(/kSlowFlash = R"\(([\s\S]*?)\)";/)[1].replaceAll('\r', ''), source);
  const doc = G.parse(source);
  assert.deepEqual(doc.errors, []);
  assert.equal(G.serialize(doc), source);
  const r = doc.children[0], [slow, fast] = r.children;
  assert.deepEqual([r.kind, slow.kind, fast.kind], ['pattern', 'pattern', 'pattern']);
  const fields = n => Object.fromEntries(G.fields(n).map(f => [f.key, f.value]));
  assert.deepEqual(fields(r), {name:'slow_flash', length:'1536f', sequence:'true', loop:'2'});
  assert.equal(fields(slow).length, '1024f'); assert.equal(fields(fast).length, '512f');
  assert.deepEqual(slow.children.map(n=>n.kind), ['every','every','spiral']);
  assert.deepEqual(fast.children.map(n=>n.kind), ['every','every','spiral']);
  assert.deepEqual(slow.children.slice(0,2).map(n=>fields(n).length), ['64f','64f']);
  assert.deepEqual(fast.children.slice(0,2).map(n=>fields(n).length), ['8f','16f']);
  assert.equal(fields(slow.children[0].children[0]).zoom, '(curve 0 -> 0.5)');
  assert.equal(fields(fast.children[0].children[0]).zoom, '(curve 0 -> 0.8 over fast)');
  const browser = await chromium.launch({channel:'msedge', headless:true});
  try {
    const page = await browser.newPage(); const errors=[]; page.on('pageerror', e=>errors.push(e.message));
    await page.goto('http://127.0.0.1:8766'); await page.waitForFunction(()=>window.TranceComposer);
    await page.selectOption('#builtin-picker', '2');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()), source);
    assert.equal(await page.locator('#source').inputValue(), source);
    const nodes = await page.evaluate(()=>{const out=[]; function walk(n){out.push({id:n.id,kind:n.kind,header:n.header});(n.children||[]).forEach(walk)} TranceComposer.getDocument().children.forEach(walk);return out;});
    assert.equal(nodes.length, 13);
    const inspected=[];
    for (const n of nodes) {
      await page.locator('[data-node-id="'+n.id+'"] > .block-head').click();
      const visible=await page.locator('#inspector [data-field]').evaluateAll(es=>Object.fromEntries(es.map(e=>[e.dataset.field,e.type==='checkbox'?String(e.checked):e.value])));
      assert.ok(Object.keys(visible).length, n.header);
      inspected.push({kind:n.kind,header:n.header,fields:visible});
    }
    async function validate() {
      await page.click('#validate');
      await page.waitForFunction(()=>document.querySelector('#validation-summary').textContent.startsWith('Compiler accepted'), {timeout:30000});
      const result={summary:await page.locator('#validation-summary').textContent(),diagnostics:await page.locator('#diagnostics').textContent()};
      assert.match(result.diagnostics,/LINT OK/); return result;
    }
    const baseline=await validate();
    async function edit(id,key,value){await page.locator('[data-node-id="'+id+'"] > .block-head').click(); const field=page.locator('#inspector [data-field="'+key+'"]'); await field.fill(value);await field.dispatchEvent('change');}
    await edit(nodes[0].id,'loop','3');
    await edit(nodes[2].id,'length','32f');
    await edit(nodes[9].id,'zoom','(curve 0 -> 0.6 over fast)');
    const expected=source.replace('loop 2','loop 3').replace('every 64f { image','every 32f { image').replace('0.8 over fast','0.6 over fast');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()), expected);
    const edited=await validate();
    await page.locator('[data-node-id="'+nodes[7].id+'"] > .block-head').click();
    await page.click('#move-up');
    const moved=await page.evaluate(()=>({source:TranceComposer.getSource(),order:TranceComposer.getDocument().children[0].children.map(n=>n.header.trim())}));
    assert.deepEqual(moved.order, ['pattern fast for 512f','pattern slow for 1024f']);
    assert.ok(moved.source.indexOf('pattern fast') < moved.source.indexOf('pattern slow for'));
    assert.equal(await page.locator('#source').inputValue(), moved.source);
    assert.deepEqual(await page.locator('[data-node-id="'+nodes[0].id+'"] > .block-children > .block > .block-head .block-code').allTextContents(), moved.order);
    const reordered=await validate();
    await page.click('#move-down');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()), expected);
    assert.deepEqual(errors, []);
    console.log(JSON.stringify({inspected,baseline,edited,reordered,movedOrder:moved.order,restored:true,pageErrors:errors},null,2));
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
