/* Serve the publishable files under a repository subpath, with no API. */
const assert=require('node:assert/strict');
const http=require('node:http');
const fs=require('node:fs/promises');
const path=require('node:path');
const {chromium}=require('playwright');
(async()=>{
  const web=path.resolve(__dirname,'../../docs/exploration/authoring');
  const requests=[];
  const server=http.createServer(async(req,res)=>{
    requests.push(req.method+' '+req.url);
    try{
      if(!req.url.startsWith('/trance/'))throw Error('outside site');
      const relative=decodeURIComponent(req.url.slice('/trance/'.length).split('?')[0])||'index.html';
      const file=path.resolve(web,relative);
      if(!file.startsWith(web+path.sep))throw Error('outside site');
      const data=await fs.readFile(file);
      const type={'.html':'text/html','.js':'text/javascript','.json':'application/json','.css':'text/css'}[path.extname(file)]||'application/octet-stream';
      res.writeHead(200,{'Content-Type':type});res.end(data);
    }catch{res.writeHead(404);res.end('No API on the static site');}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  const browser=await chromium.launch({headless:true,channel:'msedge'});
  const page=await browser.newPage({viewport:{width:1500,height:1000}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  const source=async()=>await page.evaluate(()=>document.querySelector('#source-format').value==='json'?JSON.parse(document.querySelector('#source').value):window.TranceV4Text.parse(document.querySelector('#source').value));
  const fields=()=>page.locator('#inspector [data-field]').evaluateAll(es=>es.map(e=>e.dataset.field));
  const before=async(a,b)=>{const f=await fields();assert.ok(f.includes(a)&&f.includes(b)&&f.indexOf(a)<f.indexOf(b),`${a} precedes ${b}: ${f}`);};
  try{
    await page.goto(`http://127.0.0.1:${server.address().port}/trance/`);
    await page.waitForFunction(()=>document.querySelectorAll('#builtin-picker option').length===8);
    assert.equal(await page.locator('.palette-piece').count(),7);
    assert.equal(await page.locator('#validate').textContent(),'Validate AST structure');
    for(let i=0;i<8;i++){
      await page.selectOption('#builtin-picker',String(i));
      assert.equal(await page.inputValue('#source-format'),'text');
      await page.click('#validate');
      assert.match(await page.locator('#validation-summary').textContent(),/structure checks passed.*native compilation not run/);
    }
    await page.selectOption('#builtin-picker','3');
    assert.equal(await page.locator('.timeline-bar[data-path="[1,0]"]').count(),9);
    assert.match(await page.locator('.timeline-bar[data-path="[1,0]"]').first().getAttribute('title'),/entry selection suppressed/);
    assert.equal(await page.locator('.timeline-bar[data-path="[1,0]"]').evaluateAll(es=>new Set(es.map(e=>e.parentElement)).size),1,'all visits share one lane');
    await fs.mkdir(path.resolve(__dirname,'../../build/composer-qa'),{recursive:true});
    for(const width of [1500,736,320]){
      await page.setViewportSize({width,height:1000});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
      await page.locator('#timeline').screenshot({path:path.resolve(__dirname,'../../build/composer-qa/flash-timeline-'+width+'.png')});
    }
    await page.setViewportSize({width:1500,height:1000});
    // Root Parallel supports direct outputs through both insertion paths.
    await page.click('#new-document');
    await page.locator('.palette-piece[data-kind="image"] button').click();
    await page.locator('.palette-piece[data-kind="image"]').dragTo(page.locator('.block[data-path="[]"] > .block-children > .drop-slot').last());
    let doc=await source();assert.equal(doc.root.kind,'parallel');assert.deepEqual(doc.root.children.map(e=>e.node.op),['image','image']);
    const original=JSON.stringify(doc);
    await page.locator('.block[data-path="[]"] > .block-head').click();
    await page.selectOption('#inspector [data-field="Pattern type"]','sequential');
    doc=await source();assert.deepEqual(doc.root.children,JSON.parse(original).root.children);
    await page.click('#validate');assert.match(await page.locator('#diagnostics').textContent(),/root.children\[0\].node: Outputs need a Parallel owner/);
    await page.fill('#inspector [data-field="Output wrapper frames"]','200');
    await page.getByRole('button',{name:'Give outputs a duration',exact:true}).click();
    doc=await source();assert.deepEqual(doc.root.children.map(e=>e.node.duration),[200,200]);
    assert.equal(doc.root.children[0].node.children[0].node.op,'image');
    await page.click('#undo');await page.click('#undo');assert.deepEqual(await source(),JSON.parse(original));
    // A nested Pattern defaults to Parallel and accepts an unwrapped image.
    await page.locator('.block[data-path="[]"] > .block-head').click();
    await page.locator('.palette-piece[data-kind="parallel"] button').click();
    await page.locator('.palette-piece[data-kind="image"] button').click();
    doc=await source();assert.equal(doc.root.children[2].node.kind,'parallel');assert.ok(doc.root.children[2].node.children.every(e=>e.node.op==='image'));
    assert.equal(JSON.stringify(doc).includes('sequential'),false);
    await page.locator('.block[data-path="[2]"] > .block-head').click();
    await before('Duration mode','duration');
    await page.selectOption('#inspector [data-field="Duration mode"]','sampled');
    await before('Duration mode','min');
    await page.selectOption('#inspector [data-field="Cadence mode"]','fixed');
    await before('Cadence mode','cadence');
    await page.selectOption('#inspector [data-field="Repeat mode"]','count');
    await before('Repeat mode','repeat');
    await page.selectOption('#inspector [data-field="Pattern type"]','random');
    await before('order','allow self');
    await page.selectOption('#inspector [data-field="order"]','round_robin');
    assert.equal(await page.locator('#inspector [data-field="allow self"]').count(),0);
    // Mode controls precede their enabled scalar/source fields even when JSON order differs.
    await page.selectOption('#source-format','json');
    await page.fill('#source',JSON.stringify({version:4,root:{kind:'parallel',duration:256,children:[{node:{op:'image',opacity:{curve:{from:0,to:1,scope:'local'}},flip_probability:.5,zoom:1,source:'alternate'}}]}}));
    await page.click('#apply-source');
    await page.locator('.block[data-path="[0]"] > .block-head').click();
    await before('source','flip probability');await before('opacity mode','from');await before('zoom mode','zoom');
    await page.selectOption('#inspector [data-field="source"]','primary');
    assert.equal(await page.locator('#inspector [data-field="flip probability"]').count(),0);
    // One proportional axis; root completion is distinct from its authored clock.
    const schedule={version:4,root:{kind:'sequential',duration:1000,children:[400,400].map(duration=>({node:{kind:'parallel',duration,children:[{node:{op:'image',source:'primary'}}]}}))}};
    await page.fill('#source',JSON.stringify(schedule));await page.click('#apply-source');
    assert.match(await page.locator('#timeline').textContent(),/root completes at 800f/);
    assert.match(await page.locator('#timeline').textContent(),/200 authored clock frames/);
    await page.fill('#timeline [data-field="Time zoom"]','2');await page.locator('#timeline [data-field="Time zoom"]').dispatchEvent('change');
    await page.fill('#timeline [data-field="First frame"]','300');await page.locator('#timeline [data-field="First frame"]').dispatchEvent('change');
    assert.match(await page.locator('.timeline-axis').textContent(),/^300f/);
    await page.locator('.timeline-overview').scrollIntoViewIfNeeded();
    const overviewBox=await page.locator('.timeline-overview').boundingBox();
    await page.mouse.move(overviewBox.x+overviewBox.width*.9,overviewBox.y+12);await page.mouse.down();
    await page.mouse.move(overviewBox.x+overviewBox.width*.6,overviewBox.y+12);await page.mouse.up();
    assert.ok(Math.abs(Number(await page.inputValue('#timeline [data-field="First frame"]'))-350)<3,'overview drag controls same viewport');

    await page.locator('.timeline-bar[data-path="[1]"]').first().click();
    assert.equal(await page.locator('.block.selected').getAttribute('data-path'),'[1]');
    await page.waitForTimeout(2700);
    await fs.mkdir(path.resolve(__dirname,'../../build/composer-qa'),{recursive:true});
    for(const width of [1500,736,320]){
      await page.setViewportSize({width,height:1000});
      assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true,'no overflow at '+width);
      await page.locator('#timeline').scrollIntoViewIfNeeded();
      await page.screenshot({path:path.resolve(__dirname,'../../build/composer-qa/composer-'+width+'.png')});
      await page.locator('#timeline').screenshot({path:path.resolve(__dirname,'../../build/composer-qa/timeline-'+width+'.png')});
    }
    await page.setViewportSize({width:1500,height:1000});
    await page.locator('.block[data-path="[]"] > .block-head').click();
    await page.selectOption('#inspector [data-field="Child policy"]','wait');
    doc=await source();assert.equal(doc.root.duration,undefined);assert.equal(doc.root.child_mode,'wait');
    assert.match(await page.locator('#timeline').textContent(),/authored axis 800f/);
    await page.click('#undo');assert.equal((await source()).root.duration,1000);
    // Source edits survive block selection, new-document attempts and format switches.
    await page.fill('#source','broken draft');await page.click('#new-document');
    await page.selectOption('#source-format','text');assert.equal(await page.inputValue('#source-format'),'json');
    assert.equal(await page.inputValue('#source'),'broken draft');
    await page.fill('#source',JSON.stringify(schedule));await page.click('#apply-source');
    await page.selectOption('#source-format','text');assert.match(await page.inputValue('#source'),/^v4;/);
    await page.selectOption('#source-format','json');
    // Every edit entry path shares the 100-snapshot history policy.
    await page.click('#new-document');
    await page.evaluate(()=>{for(let i=0;i<105;i++){const field=document.querySelector('#inspector [data-field="duration"]');field.value=300+i;field.dispatchEvent(new Event('change'));}});
    await page.evaluate(()=>{for(let i=0;i<100;i++)document.querySelector('#undo').click();});
    assert.equal((await source()).root.duration,304);assert.equal(await page.locator('#undo').isDisabled(),true);
    await page.setViewportSize({width:360,height:900});
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.setViewportSize({width:1500,height:1000});
    // Structural validation rejects an incomplete schedule without claiming compilation.
    await page.click('#new-document');await page.click('#validate');
    assert.match(await page.locator('#validation-summary').textContent(),/structure checks failed/);
    assert.equal(requests.some(r=>r.startsWith('POST ')),false);
    assert.ok(requests.includes('GET /trance/builtins-v4.json'));
    assert.deepEqual(errors,[]);
    console.log('PASS: static subpath and eight built-ins; preserved type-conversion drafts and chosen-duration wrapping; proportional timeline, completion/tails, zoom/pan/selection and Wait; safe dirty source and explicit format switching; 100-entry history; no overflow or page errors at 1500/736/320px.');
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(e=>{console.error(e);process.exitCode=1;});
