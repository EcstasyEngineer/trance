'use strict';
const {chromium}=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
  const browser=await chromium.launch({headless:true,channel:process.env.PLAYWRIGHT_CHANNEL||'msedge'});
  try{
    const page=await browser.newPage({viewport:{width:1600,height:1000}}),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.goto(process.env.AUTHORING_URL||'http://127.0.0.1:8766/');
    await page.waitForFunction(()=>window.TranceComposer);
    await page.locator('#source').fill('pattern scene for 256f {\n}\n');
    await page.locator('#apply-source').click();
    for(let i=1;i<=2;i++){
      await page.locator('.palette-piece[data-piece="image-lane"]').dragTo(page.locator('.block[data-kind="pattern"]').first().locator(':scope > .block-children > .drop-slot').last());
      assert.match(await page.locator('#source').inputValue(),new RegExp('pattern image_lane_'+i+' for 256f'));
    }
    let source=await page.locator('#source').inputValue();
    assert.equal((source.match(/every 64f/g)||[]).length,2);
    assert(!/\b(?:copy|draw)\b|image runtime\s*->/.test(source),'independent lanes require no explicit image handles');
    assert.equal(await page.evaluate(()=>TranceComposer.getDocument().children[0].children.filter(n=>TranceLayers.isImageLane(TranceGrammar,n)).length),2);
    await page.locator('#validate').click();await page.waitForFunction(()=>!document.getElementById('validate').disabled);
    assert.match(await page.locator('#document-status').innerText(),/Compiler validated/);
    assert.equal(await page.locator('#leapfrog-example').count(),0,'Flash Text is the builtin, not an extra example button');
    const builtins=await page.evaluate(async()=> (await(await fetch('/api/builtins')).json()).builtins);
    const flash=builtins.find(b=>/flash.?text/i.test(b.key));assert(flash);
    await page.locator('#builtin-picker').selectOption(String(flash.id));
    source=await page.locator('#source').inputValue();
    assert.match(source,/every 128f offset 64f/);assert.match(source,/fade inout/);
    assert(!/\b(?:copy|draw)\b/.test(source.replace(/^#.*$/gm,'')));
    await page.locator('#validate').click();await page.waitForFunction(()=>!document.getElementById('validate').disabled);
    assert.match(await page.locator('#document-status').innerText(),/Compiler validated/);
    assert.deepEqual(errors,[]);
    console.log('PASS native independent lane drops, inherited timing, unique scopes, register-free source, Flash Text builtin replacement, actual lint');
  }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
