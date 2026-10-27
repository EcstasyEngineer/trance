// Independent super_fast authoring review. Start tools/authoring_server.py first.
'use strict';
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'../..');
const G=require(path.join(root,'docs/exploration/authoring/grammar.js'));
async function main(){
  const cpp=fs.readFileSync(path.join(root,'src/trance/visual/builtin_patterns_v3.cpp'),'utf8');
  const source=cpp.match(/const char\* kSuperFast = R"\(([\s\S]*?)\)";/)[1];
  const fallback=JSON.parse(fs.readFileSync(path.join(root,'docs/exploration/authoring/builtins.json'),'utf8')).builtins.find(b=>b.id===8);
  assert.equal(fallback.source,source,'fallback equals actual C++');
  const doc=G.parse(source); assert.equal(G.serialize(doc),source,'CST lossless');
  const browser=await chromium.launch({headless:true,channel:process.env.PLAYWRIGHT_CHANNEL||'msedge'});
  try{
    const page=await browser.newPage({viewport:{width:1600,height:1100}});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto(process.env.AUTHORING_URL||'http://127.0.0.1:8766/');
    await page.waitForFunction(()=>window.TranceComposer);
    await page.locator('#builtin-picker').selectOption('8');
    assert.equal(await page.locator('#source').inputValue(),source);
    const api=await page.evaluate(async()=> (await(await fetch('/api/builtins')).json()).builtins.find(b=>b.id===8));
    assert.equal(api.source,source); assert.equal(api.key,'super_fast');
    assert.equal(await page.locator('.block').count(),10);
    assert.equal(await page.locator('.block[data-kind="unknown"]').count(),0);
    const expected=[
      ['pattern',{name:'super_fast',length:'2048f'}],
      ['burst',{target:'rapid',period:'8f',chance:'1/12',cooldown:'64f',duration:'64f..128f'}],
      ['base',{target:''}],['every',{length:'8f'}],
      ['image',{content:'runtime',zoom:'(curve 0.0625 -> 0.1875)'}],
      ['burst-phase',{target:''}],['image',{content:'runtime',zoom:'(curve 0.1 -> 0.7)',animation:'anim'}],
      ['every',{length:'8f'}],['word',{content:'primary',chance:'0.25'}],['spiral',{speed:'3'}]
    ];
    for(let i=0;i<expected.length;i++){
      const block=page.locator('.block').nth(i);assert.equal(await block.getAttribute('data-kind'),expected[i][0]);
      await block.locator(':scope > .block-head').click();
      for(const [key,value] of Object.entries(expected[i][1]))assert.equal(await page.locator(`[data-field="${key}"]`).inputValue(),value,`block ${i} ${key}`);
      assert.equal(await page.locator('#block-source').inputValue(),await block.locator(':scope > .block-head > .block-code').innerText());
    }
    async function lint(){await page.locator('#validate').click();await page.waitForFunction(()=>!document.getElementById('validate').disabled);assert.match(await page.locator('#document-status').innerText(),/Compiler validated/);assert.match(await page.locator('#diagnostics').innerText(),/LINT OK/);}
    async function field(key,value){const input=page.locator(`[data-field="${key}"]`);await input.fill(value);await input.press('Tab');}
    await lint();
    await page.locator('.block[data-kind="burst"] > .block-head').click();
    await field('duration','80f..144f');await field('chance','1/9');
    await page.locator('.block[data-kind="base"] .block[data-kind="every"] > .block-head').click();await field('length','12f');
    const edited=await page.locator('#source').inputValue();
    assert.match(edited,/chance 1\/9 cooldown 64f duration 80f\.\.144f/);assert.match(edited,/every 12f/);await lint();
    for(let i=0;i<3;i++)await page.locator('#undo').click();assert.equal(await page.locator('#source').inputValue(),source,'field undo exact');
    // Native palette drag into the base branch, then native inspector deletion.
    const base=page.locator('.block[data-kind="base"]');
    await page.locator('.palette-piece[data-piece="word"]').dragTo(base.locator(':scope > .block-children > .drop-slot').last());
    assert.equal(await base.locator('.block[data-kind="word"]').count(),1);
    await base.locator('.block[data-kind="word"] > .block-head').click();
    await page.locator('#delete-block').click();
    assert.equal(await page.locator('#source').inputValue(),source,'delete added word exact');
    await page.locator('#undo').click();assert.equal(await page.locator('.block[data-kind="word"]').count(),2);
    await page.locator('#undo').click();assert.equal(await page.locator('#source').inputValue(),source,'native addition undo exact');
    // Native move puts the independent word cadence earlier; undo restores bytes.
    const pattern=page.locator('.block[data-kind="pattern"]');
    await pattern.locator(':scope > .block-children > .block[data-kind="every"] > .block-head').dragTo(pattern.locator(':scope > .block-children > .drop-slot').first());
    const moved=await page.locator('#source').inputValue();assert(moved.indexOf('word primary')<moved.indexOf('burst -> rapid'));
    await page.locator('#undo').click();assert.equal(await page.locator('#source').inputValue(),source,'native move undo exact');
    await lint(); assert.deepEqual(errors,[]);
    console.log(JSON.stringify({result:'pass',builtin:'8 super_fast',blocks:10,lintChecks:3,checks:['C++/fallback/API/CST/source exact','ten native inspectors','duration chance base cadence edits','native word add delete and undo','native word cadence reorder and undo','source exact restoration','no browser errors']}));
  }finally{await browser.close();}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
