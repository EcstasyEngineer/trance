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
    await before('Duration','duration');
    assert.deepEqual(await page.locator('#inspector [data-field="Duration"] option').evaluateAll(es=>es.map(e=>[e.value,e.textContent])),[['fixed','Fixed frames'],['inherit','Inherit'],['parent','Parent frames']]);
    await page.selectOption('#inspector [data-field="Duration"]','parent');
    assert.equal((await source()).root.children[2].node.duration,undefined);
    assert.match(await page.locator('.timeline-bar[data-path="[2]"]').first().getAttribute('title'),/\[0, 256\)/);
    assert.equal(await page.locator('#inspector [data-field="duration"]').count(),0);
    await page.selectOption('#inspector [data-field="Duration"]','fixed');
    await page.selectOption('#inspector [data-field="Cadence mode"]','fixed');
    await before('Cadence mode','cadence');
    await page.selectOption('#inspector [data-field="Repeat mode"]','count');
    await before('Repeat mode','repeat');
    const childrenBeforeRandom=(await source()).root.children[2].node.children;
    await page.locator('#inspector [data-field="Loop subpatterns"]').check();
    await page.selectOption('#inspector [data-field="Pattern type"]','random');
    assert.equal('loop_subpatterns' in (await source()).root.children[2].node,false);
    assert.deepEqual((await source()).root.children[2].node.children,childrenBeforeRandom);

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
    // Explicit Inherit recalculates from children at every level.
    const piece=duration=>({kind:'parallel',duration,children:[{node:{op:'image',source:'primary'}}]});
    const automatic={version:4,root:{kind:'sequential',duration:2000,children:[{node:piece(200)},{node:piece(300)}]}};
    await page.fill('#source',JSON.stringify(automatic));await page.click('#apply-source');
    await page.selectOption('#inspector [data-field="Duration"]','inherit');
    assert.equal((await source()).root.duration,'inherit');
    assert.match(await page.locator('[data-duration-preview]').textContent(),/500 frames/);
    assert.equal(await page.locator('#inspector input[data-field="duration"]').count(),0,'calculated frames are read-only');
    await page.locator('.block[data-path="[1]"] > .block-head').click();
    await page.fill('#inspector [data-field="duration"]','400');await page.locator('#inspector [data-field="duration"]').dispatchEvent('change');
    assert.match(await page.locator('.block[data-path="[]"] > .block-head .block-code').textContent(),/600f/);
    await page.locator('.block[data-path="[]"] > .block-head').click();
    assert.match(await page.locator('[data-duration-preview]').textContent(),/600 frames/);
    await page.click('#undo');assert.equal((await source()).root.children[1].node.duration,300);
    await page.locator('.block[data-path="[]"] > .block-head').click();assert.match(await page.locator('[data-duration-preview]').textContent(),/500 frames/);
    await page.click('#redo');assert.equal((await source()).root.children[1].node.duration,400);
    await page.locator('.block[data-path="[]"] > .block-head').click();assert.match(await page.locator('[data-duration-preview]').textContent(),/600 frames/);
    const current=await page.inputValue('#source');await page.fill('#source','broken child edit');
    assert.match(await page.locator('[data-duration-preview]').textContent(),/Apply pending source/);
    assert.doesNotMatch(await page.locator('[data-duration-preview]').textContent(),/600/);
    await page.fill('#source',current);await page.click('#apply-source');
    await page.selectOption('#source-format','text');assert.match(await page.inputValue('#source'),/sequence for inherit/);
    await page.selectOption('#source-format','json');
    const nestedAuto={version:4,root:{kind:'parallel',duration:2000,children:[{node:automatic.root}]}};
    await page.fill('#source',JSON.stringify(nestedAuto));await page.click('#apply-source');
    await page.locator('.block[data-path="[0]"] > .block-head').click();await page.selectOption('#inspector [data-field="Duration"]','inherit');
    assert.match(await page.locator('[data-duration-preview]').textContent(),/500 frames/);
    assert.equal((await source()).root.duration,2000);assert.equal((await source()).root.children[0].node.duration,'inherit');
    for(const width of [1500,320]){
      await page.setViewportSize({width,height:1000});
      await page.locator('.inspector-panel').evaluate(e=>e.scrollTop=0);
      await page.locator('.inspector-panel').screenshot({path:path.resolve(__dirname,'../../build/composer-qa/automatic-inherit-inspector-'+width+'.png')});
      await page.screenshot({path:path.resolve(__dirname,'../../build/composer-qa/automatic-inherit-'+width+'.png')});
    }
    await page.setViewportSize({width:1500,height:1000});

    await page.locator('.block[data-path="[0,1]"] > .block-head').click();
    await page.fill('#inspector [data-field="duration"]','400');await page.locator('#inspector [data-field="duration"]').dispatchEvent('change');
    await page.locator('.block[data-path="[0]"] > .block-head').click();assert.match(await page.locator('[data-duration-preview]').textContent(),/600 frames/);
    await page.selectOption('#inspector [data-field="Duration"]','parent');assert.equal((await source()).root.children[0].node.duration,undefined);
    assert.equal(await page.locator('[data-duration-preview]').count(),0);
    const parallelAuto={...automatic,root:{...automatic.root,kind:'parallel'}};
    await page.fill('#source',JSON.stringify(parallelAuto));await page.click('#apply-source');await page.selectOption('#inspector [data-field="Duration"]','inherit');
    assert.match(await page.locator('[data-duration-preview]').textContent(),/300 frames/);
    const unresolved={version:4,root:{kind:'parallel',duration:'inherit',children:[{node:{kind:'parallel',children:[{node:{op:'image',source:'primary'}}]}}]}};
    await page.fill('#source',JSON.stringify(unresolved));await page.click('#apply-source');
    assert.match(await page.locator('[data-duration-preview]').textContent(),/uses parent frames/);
    assert.doesNotMatch(await page.locator('[data-duration-preview]').textContent(),/300 frames/);
    await page.selectOption('#inspector [data-field="Pattern type"]','random');
    assert.equal(await page.locator('#inspector [data-field="Duration"] option[value="inherit"]').count(),0);
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
    assert.equal(await page.locator('#inspector h3').filter({hasText:'Subpattern behavior'}).count(),1);
    assert.equal(await page.locator('#inspector [data-field="Loop subpatterns"]').isChecked(),false);
    await page.selectOption('#inspector [data-field="Duration"]','inherit');
    doc=await source();assert.equal(doc.root.duration,'inherit');assert.equal('loop_subpatterns' in doc.root,false);
    assert.match(await page.locator('#timeline').textContent(),/authored axis 800f/);
    await page.locator('#inspector [data-field="Loop subpatterns"]').check();
    doc=await source();assert.equal(doc.root.loop_subpatterns,true);assert.equal(doc.root.duration,'inherit');
    assert.match(await page.locator('#timeline').textContent(),/authored axis 800f/);
    for(const width of [1500,320]){
      await page.setViewportSize({width,height:1000});
      await page.locator('.inspector-panel').evaluate(e=>e.scrollTop=0);
      await page.locator('.inspector-panel').screenshot({path:path.resolve(__dirname,'../../build/composer-qa/inherit-inspector-'+width+'.png')});
      await page.screenshot({path:path.resolve(__dirname,'../../build/composer-qa/inherit-composer-'+width+'.png')});
    }
    await page.setViewportSize({width:1500,height:1000});
    await page.click('#undo');assert.equal((await source()).root.loop_subpatterns,undefined);
    await page.click('#undo');assert.equal((await source()).root.duration,1000);
    await page.locator('#inspector [data-field="Loop subpatterns"]').check();
    assert.equal(await page.locator('.timeline-bar[data-path="[0]"]').count(),2,'loop restarts the list within the fixed window');
    await page.locator('#inspector [data-field="Loop subpatterns"]').uncheck();
    assert.equal((await source()).root.loop_subpatterns,false);
    assert.equal(await page.locator('.timeline-bar[data-path="[0]"]').count(),1);
    assert.equal(await page.locator('#inspector [data-field="Child policy"]').count(),0);
    assert.doesNotMatch(await page.locator('#inspector').textContent(),/Wait|Sample once|sampled/);
    for(const legacy of [{child_mode:'loop'},{duration:{min:64,max:128,step:8}}]){
      const invalid=JSON.stringify({version:4,root:{...schedule.root,...legacy}});
      await page.fill('#source',invalid);await page.click('#apply-source');
      assert.equal(await page.inputValue('#source'),invalid);
      assert.match(await page.locator('#diagnostics').textContent(),/root\.(child_mode|duration):/);
      assert.equal((await page.locator('.block.selected').getAttribute('data-path')),'[]');
    }
    await page.fill('#source',JSON.stringify(schedule));await page.click('#apply-source');
    const noBound={version:4,root:{kind:'parallel',children:[{node:{op:'image',source:'primary'}}]}};
    await page.fill('#source',JSON.stringify(noBound));await page.click('#apply-source');await page.click('#validate');
    assert.match(await page.locator('#diagnostics').textContent(),/root: Add a timed subpattern; direct outputs do not determine frames/);
    await page.fill('#source',JSON.stringify(schedule));await page.click('#apply-source');
    const unbounded={version:4,root:{kind:'sequential',duration:10,children:[{node:{kind:'parallel',duration:2,repeat:'forever',children:[{node:{op:'image',source:'primary'}}]}}]}};
    await page.fill('#source',JSON.stringify(unbounded));await page.click('#apply-source');await page.click('#validate');
    assert.match(await page.locator('#diagnostics').textContent(),/root.children\[0\].node: Subpattern repeat must be finite/);
    await page.fill('#source',JSON.stringify(schedule));await page.click('#apply-source');
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
    console.log('PASS: static subpath and eight built-ins; preserved type-conversion drafts and chosen-duration wrapping; proportional timeline, completion/tails, zoom/pan/selection, boolean loops and Inherit; safe dirty source and explicit format switching; 100-entry history; no overflow or page errors at 1500/736/320px.');
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(e=>{console.error(e);process.exitCode=1;});
