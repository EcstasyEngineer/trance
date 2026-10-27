const assert = require('node:assert/strict');
const fs = require('node:fs');
const G = require('../grammar.js');
const {chromium} = require('playwright');
(async () => {
  const builtin = JSON.parse(fs.readFileSync('docs/exploration/authoring/builtins.json','utf8')).builtins.find(b=>b.id===5);
  const source = builtin.source;
  assert.equal(builtin.name,'parallel'); assert.equal(builtin.sourcePatternName,'simple');
  const cpp = fs.readFileSync('src/trance/visual/builtin_patterns_v3.cpp','utf8');
  assert.equal(cpp.match(/kSimple = R"\(([\s\S]*?)\)";/)[1].replaceAll('\r',''),source);
  assert.match(cpp,/case 5:\s+return kSimple;/);
  const doc = G.parse(source); assert.deepEqual(doc.errors,[]); assert.equal(G.serialize(doc),source);
  const root=doc.children[0]; assert.equal(root.kind,'pattern');
  assert.deepEqual(root.children.map(n=>n.kind),['every','every','spiral']);
  const expected = [
    ['pattern',{name:'simple',length:'2048f',sequence:'false',loop:''}],
    ['every',{length:'64f',target:'',offset:''}],
    ['image',{content:'runtime',zoom:'(curve 0 -> 0.5)',animation:'anim every 3rd'}],
    ['every',{length:'32f',target:'',offset:''}],
    ['caption',{content:'primary'}],
    ['spiral',{speed:'3'}]
  ];
  const browser=await chromium.launch({channel:'msedge',headless:true});
  try {
    const page=await browser.newPage({viewport:{width:1700,height:1100}}), errors=[];
    page.on('pageerror',e=>errors.push(e.message));
    await page.goto('http://127.0.0.1:8766'); await page.waitForFunction(()=>window.TranceComposer);
    await page.selectOption('#builtin-picker','5');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()),source);
    assert.equal(await page.locator('#source').inputValue(),source);
    assert.equal(await page.locator('#composition-title').textContent(),'simple');
    assert.match(await page.locator('#builtin-picker option:checked').textContent(),/5\. parallel/);
    const nodes=await page.evaluate(()=>{const out=[];function walk(n){out.push({id:n.id,kind:n.kind,header:n.header});(n.children||[]).forEach(walk)}TranceComposer.getDocument().children.forEach(walk);return out});
    assert.equal(nodes.length,6);const inspected=[];
    for(let i=0;i<nodes.length;i++){
      const n=nodes[i]; assert.equal(n.kind,expected[i][0]);
      await page.locator(`[data-node-id="${n.id}"] > .block-head`).click();
      const fields=await page.locator('#inspector [data-field]').evaluateAll(es=>Object.fromEntries(es.map(e=>[e.dataset.field,e.value])));
      for(const [k,v] of Object.entries(expected[i][1]))assert.equal(fields[k],v,`${n.header}: ${k}`);
      inspected.push({...n,fields});
    }
    async function validate(){await page.click('#validate');await page.waitForFunction(()=>document.querySelector('#validation-summary').textContent.startsWith('Compiler accepted'),null,{timeout:30000});const diagnostics=await page.locator('#diagnostics').textContent();assert.match(diagnostics,/LINT OK/);return diagnostics;}
    const baseline=await validate();
    async function edit(id,key,value){await page.locator(`[data-node-id="${id}"] > .block-head`).click();const f=page.locator(`#inspector [data-field="${key}"]`);await f.fill(value);await f.dispatchEvent('change');}
    await edit(nodes[0].id,'length','1024f');
    await edit(nodes[3].id,'length','16f');
    await edit(nodes[2].id,'animation','anim every 4th');
    const edited=source.replace('2048f','1024f').replace('every 32f','every 16f').replace('anim every 3rd','anim every 4th');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()),edited);
    const changed=await validate();
    // Native HTML drag/drop: pointer-driven Playwright locator dragTo, no synthetic drop dispatch.
    const slot=page.locator(`[data-drop-parent="${nodes[3].id}"][data-drop-index="1"]`);
    await page.locator('[data-piece="word"]').dragTo(slot);
    const added=await page.evaluate(()=>({source:TranceComposer.getSource(),root:TranceComposer.getDocument().children[0]}));
    assert.deepEqual(added.root.children[1].children.map(n=>n.kind),['caption','word']);
    assert.equal(added.root.children[0].children.length,1);
    assert.equal(await page.locator('#source').inputValue(),added.source);
    const addedValidation=await validate();
    assert.equal(await page.locator('#inspector [data-field="content"]').inputValue(),'primary');
    await page.click('#delete-block');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()),edited);
    await page.click('#undo');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()),added.source);
    await page.click('#undo');
    assert.equal(await page.evaluate(()=>TranceComposer.getSource()),edited);
    assert.deepEqual(errors,[]);
    console.log(JSON.stringify({inspected,baseline,changed,addedValidation,nativeDrag:true,deleteAndUndo:true,pageErrors:errors},null,2));
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
