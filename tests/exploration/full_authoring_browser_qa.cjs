// Run against tools/authoring_server.py. Requires Playwright + installed Edge.
'use strict';
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const os=require('node:os');
async function main(){
  const browser=await chromium.launch({headless:true,channel:'msedge'});
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'trance-composer-qa-'));
  try{
    const page=await browser.newPage({viewport:{width:1600,height:1100},acceptDownloads:true});
    const errors=[];page.on('pageerror',e=>errors.push(e.message));
    await page.goto(process.argv[2]||'http://127.0.0.1:8766/');
    await page.waitForFunction(()=>window.TranceComposer);
    const builtins=await page.evaluate(async()=> (await(await fetch('/api/builtins')).json()).builtins);
    assert.equal(builtins.length,8);
    for(const builtin of builtins){
      await page.locator('#builtin-picker').selectOption(String(builtin.id));
      assert.equal(await page.locator('#source').inputValue(),builtin.source,builtin.key+' source');
      assert.equal(await page.locator('.block[data-kind="unknown"]').count(),0,builtin.key+' structured');
      assert((await page.locator('.block').count())>=4,builtin.key+' visible blocks');
      await page.locator('#validate').click();
      await page.waitForFunction(()=>!document.getElementById('validate').disabled);
      assert.match(await page.locator('#document-status').innerText(),/Compiler validated/,builtin.key+' real compiler');
    }
    await page.locator('#new-document').click();
    const cadence=page.locator('.block[data-kind="every"]').first();
    const end=cadence.locator(':scope > .block-children > .drop-slot').last();
    await page.locator('.palette-piece[data-piece="word"]').dragTo(end);
    assert.match(await page.locator('#source').inputValue(),/word primary/,'native palette drag added a word');
    const word=page.locator('.block[data-kind="word"] > .block-head');
    await word.dragTo(cadence.locator(':scope > .block-children > .drop-slot').first());
    let source=await page.locator('#source').inputValue();
    assert(source.indexOf('word primary')<source.indexOf('image runtime'),'native block drag reordered children');
    const root=page.locator('.block[data-kind="pattern"]').first();
    await page.locator('.block[data-kind="word"] > .block-head').dragTo(root.locator(':scope > .block-children > .drop-slot').last());
    assert.equal(await cadence.locator('.block[data-kind="word"]').count(),0,'cross-scope native move');
    assert.equal(await root.locator(':scope > .block-children > .block[data-kind="word"]').count(),1);
    await page.locator('.block[data-kind="image"] > .block-head').click();
    await page.locator('[data-field="content"]').selectOption('secondary');
    await page.locator('[data-field="zoom"]').fill('(curve 0 -> 0.4 over untitled)');
    await page.locator('[data-field="zoom"]').press('Tab');
    source=await page.locator('#source').inputValue();assert.match(source,/image secondary.*zoom \(curve 0 -> 0\.4 over untitled\)/);
    const before=source;
    await page.locator('#duplicate-block').click();
    assert.equal(await page.locator('.block[data-kind="image"]').count(),2,'duplicate');
    await page.locator('#undo').click();assert.equal(await page.locator('#source').inputValue(),before,'undo exact');
    await page.locator('#redo').click();assert.equal(await page.locator('.block[data-kind="image"]').count(),2,'redo');
    await page.locator('#validate').click();await page.waitForFunction(()=>!document.getElementById('validate').disabled);
    assert.match(await page.locator('#document-status').innerText(),/Compiler validated/,'edited composition lint');
    const draft='pattern p for 32f {\n  image primary\n  unknown_widget 42\n}\n';
    await page.locator('#source').fill(draft);await page.locator('#apply-source').click();
    assert.equal(await page.locator('#source').inputValue(),draft,'unrecognized source preserved');
    const downloadPromise=page.waitForEvent('download');await page.locator('#export').click();const download=await downloadPromise;
    const exported=path.join(temporary,'export.pattern');await download.saveAs(exported);
    assert.equal(fs.readFileSync(exported,'utf8'),draft,'export preserves unknown draft');
    await page.locator('#file-input').setInputFiles({name:'roundtrip.pattern',mimeType:'text/plain',buffer:Buffer.from(builtins[5].source)});
    await page.waitForFunction(s=>window.TranceComposer.getSource()===s,builtins[5].source);
    assert.equal(await page.locator('#source').inputValue(),builtins[5].source,'file import all grammar');
    await page.setViewportSize({width:390,height:844});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'mobile page does not overflow');
    assert.deepEqual(errors,[],'browser runtime errors');
    console.log(JSON.stringify({result:'pass',builtins:8,palettePieces:await page.locator('.palette-piece').count(),checks:['eight complete compositions and real lint','native palette drag','native reorder','native cross-scope move','properties and ancestor curve','duplicate undo redo','unknown preservation','download/import','mobile','browser errors']}));
  }finally{await browser.close();fs.rmSync(temporary,{recursive:true,force:true});}
}
main().catch(e=>{console.error(e);process.exitCode=1;});
