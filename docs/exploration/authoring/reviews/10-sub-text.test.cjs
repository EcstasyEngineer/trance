const assert=require('node:assert/strict');
const fs=require('node:fs');
const G=require('../grammar.js');
const {chromium}=require('playwright');
(async()=>{
const source=JSON.parse(fs.readFileSync('docs/exploration/authoring/builtins.json','utf8')).builtins.find(x=>x.id===3).source;
assert.equal(fs.readFileSync('src/trance/visual/builtin_patterns_v3.cpp','utf8').match(/kSubText = R"\(([\s\S]*?)\)";/)[1].replaceAll('\r',''),source);
const doc=G.parse(source);assert.deepEqual(doc.errors,[]);assert.equal(G.serialize(doc),source);
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
const page=await browser.newPage({viewport:{width:1600,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
await page.goto('http://127.0.0.1:8766/');await page.waitForFunction(()=>window.TranceComposer);await page.selectOption('#builtin-picker','3');
assert.equal(await page.evaluate(()=>TranceComposer.getSource()),source);
const nodes=await page.evaluate(()=>{const out=[];function walk(n){out.push({id:n.id,kind:n.kind,header:n.header});(n.children||[]).forEach(walk)}TranceComposer.getDocument().children.forEach(walk);return out});
assert.deepEqual(nodes.map(n=>n.kind),['pattern','every','image','every','subtext','spiral']);
const inspected=[];
for(const n of nodes){await page.locator(`[data-node-id="${n.id}"] > .block-head`).click();const fields=await page.locator('#inspector [data-field]').evaluateAll(es=>Object.fromEntries(es.map(e=>[e.dataset.field,e.value])));assert.deepEqual(fields,Object.fromEntries(G.fields((()=>{let found;function walk(x){if(x.kind===n.kind && x.header===n.header)found=x;(x.children||[]).forEach(walk)}walk(doc);return found})()).map(f=>[f.key,f.value])));inspected.push({kind:n.kind,fields})}
async function validate(){await page.click('#validate');await page.waitForFunction(()=>!document.querySelector('#validate').disabled);const summary=await page.locator('#validation-summary').textContent(),diagnostics=await page.locator('#diagnostics').textContent();assert.match(summary,/Compiler accepted/);assert.match(diagnostics,/LINT OK/);return {summary,diagnostics}}
const baseline=await validate();
async function edit(i,key,value){await page.locator(`[data-node-id="${nodes[i].id}"] > .block-head`).click();const f=page.locator(`#inspector [data-field="${key}"]`);await f.fill(value);await f.dispatchEvent('change')}
await edit(1,'length','48f');await edit(2,'zoom','(curve 0 -> 0.6)');await edit(2,'animation','anim every 4th');await edit(3,'length','24f');await edit(5,'speed','3');
const editedSource=source.replace('every 64f','every 48f').replace('0.375','0.6').replace('3rd','4th').replace('every 32f','every 24f').replace('speed 4','speed 3');assert.equal(await page.evaluate(()=>TranceComposer.getSource()),editedSource);const edited=await validate();
await page.locator('[data-piece="caption"]').dragTo(page.locator(`[data-drop-parent="${nodes[3].id}"][data-drop-index="1"]`));
const afterDrag=await page.evaluate(()=>TranceComposer.getDocument());assert.equal(afterDrag.children[0].children[1].children[1].kind,'caption');const added=await validate();
const caption=afterDrag.children[0].children[1].children[1];await page.locator(`[data-node-id="${caption.id}"] > .block-head`).click();await page.click('#delete-block');assert.equal(await page.evaluate(()=>TranceComposer.getSource()),editedSource);
const deleted=await validate();assert.deepEqual(errors,[]);console.log(JSON.stringify({inspected,baseline,edited,dragAdded:true,added,deleted,errors},null,2));
}finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});

