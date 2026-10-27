const assert = require('node:assert/strict');
const fs = require('node:fs');
const G = require('../grammar.js');
const {chromium} = require('playwright');
(async () => {
  const source = JSON.parse(fs.readFileSync('docs/exploration/authoring/builtins.json', 'utf8')).builtins.find(b => b.id === 4).source;
  assert.equal(fs.readFileSync('src/trance/visual/builtin_patterns_v3.cpp', 'utf8').match(/kFlashText = R"\(([\s\S]*?)\)";/)[1].replaceAll('\r', ''), source);
  const api = await (await fetch('http://127.0.0.1:8766/api/builtins')).json();
  assert.equal(api.builtins.find(b => b.id === 4).source, source);
  const doc = G.parse(source), fields = n => Object.fromEntries(G.fields(n).map(f => [f.key,f.value]));
  assert.deepEqual(doc.errors, []); assert.equal(G.serialize(doc), source);
  const root = doc.children[0], life = root.children[0], beat = life.children[0];
  assert.deepEqual(fields(root), {name:'flash_text',length:'1024f',sequence:'false',loop:''});
  assert.deepEqual(fields(life), {name:'life',length:'128f',sequence:'false',loop:'8'});
  assert.deepEqual(fields(beat), {length:'64f',target:'beat',offset:''});
  assert.deepEqual(beat.children.map(n=>n.kind), ['copy','draw','image']);
  assert.match(beat.children[0].header, /copy cur -> prev/);
  assert.match(beat.children[1].header, /draw prev\s+zoom \(curve 0.4 -> 0.8\)/);
  assert.match(beat.children[2].header, /image secondary -> cur fade in zoom \(curve 0 -> 0.4\)/);
  assert.deepEqual(life.children[1].children.map(n=>n.header.trim()), ['word secondary']);
  assert.equal(fields(life.children[1]).length,'64f');
  assert.equal(fields(root.children[1]).length,'32f');
  assert.equal(root.children[1].children[0].header.trim(),'caption primary');
  assert.equal(root.children[2].header.trim(),'spiral speed 2');
  const browser = await chromium.launch({channel:'msedge',headless:true});
  try {
    const page = await browser.newPage(), errors=[]; page.on('pageerror',e=>errors.push(e.message));
    await page.goto('http://127.0.0.1:8766'); await page.waitForFunction(()=>window.TranceComposer);
    await page.selectOption('#builtin-picker','4'); assert.equal(await page.evaluate(()=>TranceComposer.getSource()),source);
    const nodes = await page.evaluate(()=>{const a=[];function walk(n){a.push({id:n.id,kind:n.kind,header:n.header});(n.children||[]).forEach(walk)}TranceComposer.getDocument().children.forEach(walk);return a;});
    assert.deepEqual(nodes.map(n=>n.kind),['pattern','pattern','every','copy','draw','image','every','word','every','caption','spiral']);
    const inspected=[];
    for(const n of nodes){await page.locator(`[data-node-id="${n.id}"] > .block-head`).click(); const shown=await page.locator('#inspector [data-field]').evaluateAll(es=>Object.fromEntries(es.map(e=>[e.dataset.field,e.type==='checkbox'?String(e.checked):e.value])));assert.ok(Object.keys(shown).length,n.header);inspected.push({kind:n.kind,fields:shown});}
    async function validate(){await page.click('#validate');await page.waitForFunction(()=>!document.querySelector('#validate').disabled);const summary=await page.locator('#validation-summary').textContent(),diagnostics=await page.locator('#diagnostics').textContent();assert.match(summary,/Compiler accepted/);assert.match(diagnostics,/LINT OK/);return {summary,diagnostics};}
    const baseline=await validate();
    async function edit(i,key,value){await page.locator(`[data-node-id="${nodes[i].id}"] > .block-head`).click();const f=page.locator(`#inspector [data-field="${key}"]`);await f.fill(value);await f.press('Tab');}
    await edit(1,'loop','6'); await edit(4,'zoom','(curve 0.4 -> 0.7)'); await edit(8,'length','24f');
    const expected=source.replace('loop 8','loop 6').replace('0.4 -> 0.8','0.4 -> 0.7').replace('every 32f','every 24f');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()),expected); const edited=await validate();
    // Native move controls change layer order while leaving snapshot before selection.
    await page.locator(`[data-node-id="${nodes[5].id}"] > .block-head`).click(); await page.click('#move-up');
    const moved=await page.evaluate(()=>({source:TranceComposer.getSource(),order:TranceComposer.getDocument().children[0].children[0].children[0].children.map(n=>n.kind)}));
    assert.deepEqual(moved.order,['copy','image','draw']);assert.ok(moved.source.indexOf('copy cur')<moved.source.indexOf('image secondary'));assert.ok(moved.source.indexOf('image secondary')<moved.source.indexOf('draw prev'));
    assert.equal(await page.locator('#source').inputValue(),moved.source);
    const visualOrder=await page.locator(`[data-node-id="${nodes[2].id}"] > .block-children > .block > .block-head .block-code`).allTextContents();
    assert.deepEqual(visualOrder.map(x=>x.split(/\s/)[0]),['copy','image','draw']); const reordered=await validate();
    await page.click('#undo'); assert.equal(await page.evaluate(()=>TranceComposer.getSource()),expected);
    await page.click('#undo');await page.click('#undo');await page.click('#undo');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()),source); assert.equal(await page.locator('#source').inputValue(),source);
    const restored=await validate(); assert.deepEqual(errors,[]);
    console.log(JSON.stringify({inspected,baseline,edited,reordered,movedOrder:moved.order,visualOrder,restored,pageErrors:errors},null,2));
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
