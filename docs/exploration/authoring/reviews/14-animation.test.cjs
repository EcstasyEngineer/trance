const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const G = require('../grammar.js');
const {chromium} = require('playwright');
const repo = path.resolve(__dirname, '../../../..');
(async () => {
  const builtin = JSON.parse(fs.readFileSync(path.join(repo,'docs/exploration/authoring/builtins.json'),'utf8')).builtins.find(b=>b.id===7);
  const source = builtin.source;
  const cpp = fs.readFileSync(path.join(repo,'src/trance/visual/builtin_patterns_v3.cpp'),'utf8');
  assert.equal(cpp.match(/kAnimation = R"\(([\s\S]*?)\)";/)[1].replaceAll('\r',''),source);
  assert.match(cpp,/case 7:\s+return kAnimation;/);
  assert.equal(builtin.key,'animation');
  const doc = G.parse(source);
  assert.deepEqual(doc.errors,[]); assert.equal(G.serialize(doc),source);
  const expected = [
    ['pattern',{name:'animation',length:'1024f',sequence:'false',loop:''}],
    ['every',{length:'64f',offset:'',target:''}],
    ['image',{content:'runtime',zoom:'(curve 0 -> 0.625)',animation:'anim'}],
    ['every',{length:'64f',offset:'48f',target:''}],
    ['image',{content:'secondary',target:'still',env:'in 8f hold 16f out 8f',zoom:'(curve 0.5 -> 0.625)',animation:''}],
    ['every',{length:'32f',offset:'',target:''}],
    ['caption',{content:'runtime'}],
    ['spiral',{speed:'3'}]
  ];
  const browser = await chromium.launch({channel:process.env.PLAYWRIGHT_CHANNEL||'msedge',headless:true});
  try {
    const page = await browser.newPage({viewport:{width:1700,height:1200}}), errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.goto(process.env.AUTHORING_URL||'http://127.0.0.1:8766');
    await page.waitForFunction(()=>window.TranceComposer);
    await page.selectOption('#builtin-picker','7');
    const current=()=>page.evaluate(()=>TranceComposer.getSource());
    assert.equal(await current(),source);assert.equal(await page.locator('#source').inputValue(),source);
    const nodes=await page.evaluate(()=>{const out=[];function visit(n){out.push({id:n.id,kind:n.kind,header:n.header});(n.children||[]).forEach(visit)}TranceComposer.getDocument().children.forEach(visit);return out});
    assert.equal(nodes.length,8);
    const head=id=>page.locator(`[data-node-id="${id}"] > .block-head`), inspected=[];
    for(let i=0;i<nodes.length;i++) {
      const n=nodes[i];assert.equal(n.kind,expected[i][0]);await head(n.id).click();
      const fields=await page.locator('#inspector [data-field]').evaluateAll(es=>Object.fromEntries(es.map(e=>[e.dataset.field,e.value])));
      for(const [k,v] of Object.entries(expected[i][1]))assert.equal(fields[k],v,`${n.header}: ${k}`);
      inspected.push({kind:n.kind,fields});
    }
    async function validate() {
      await page.click('#validate');
      await page.waitForFunction(()=>document.querySelector('#validation-summary').textContent.startsWith('Compiler accepted'),null,{timeout:30000});
      const diagnostics=await page.locator('#diagnostics').textContent();assert.match(diagnostics,/LINT OK/);return diagnostics;
    }
    const baseline=await validate();
    async function edit(id,key,value) {await head(id).click();const field=page.locator(`#inspector [data-field="${key}"]`);await field.fill(value);await field.dispatchEvent('change');}
    await edit(nodes[3].id,'offset','40f');
    await edit(nodes[4].id,'env','in 6f hold 20f out 6f');
    await head(nodes[4].id).click();
    await page.locator('.inspector-section summary').filter({hasText:'Compose a curve'}).click();
    await page.selectOption('#mod-target','zoom');await page.selectOption('#mod-mode','curve');
    await page.fill('#mod-from','0.45');await page.fill('#mod-to','0.7');await page.click('#apply-modulator');
    const edited=source.replace('offset 48f','offset 40f').replace('in 8f hold 16f out 8f','in 6f hold 20f out 6f').replace('(curve 0.5 -> 0.625)','(curve 0.45 -> 0.7)');
    assert.equal(await current(),edited);const editedValidation=await validate();
    // Native pointer drag moves the spiral layer before both image schedules.
    await head(nodes[7].id).dragTo(page.locator(`[data-drop-parent="${nodes[0].id}"][data-drop-index="0"]`));
    const moved=await current();
    const movedDoc=G.parse(edited), root=movedDoc.children[0];root.children.unshift(root.children.pop());
    assert.equal(moved,G.serialize(movedDoc));
    assert.deepEqual(await page.evaluate(()=>TranceComposer.getDocument().children[0].children.map(n=>n.kind)),['spiral','every','every','every']);
    const movedValidation=await validate();
    await head(nodes[7].id).click();await page.click('#delete-block');
    const deleted=G.parse(moved);deleted.children[0].children.shift();assert.equal(await current(),G.serialize(deleted));
    await page.click('#undo');assert.equal(await current(),moved);
    await page.click('#undo');assert.equal(await current(),edited);
    assert.equal(await page.locator('#source').inputValue(),edited);assert.deepEqual(errors,[]);
    console.log(JSON.stringify({inspected,baseline,editedValidation,movedValidation,nativeMove:true,deleteAndUndo:true,pageErrors:errors},null,2));
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
