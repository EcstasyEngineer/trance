/* Native V4 editor contract: no V3 adapter or old grammar pieces. */
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
(async () => {
  const browser = await chromium.launch({headless:true,channel:'msedge'});
  const page = await browser.newPage({viewport:{width:1500,height:1000}});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  try {
    await page.goto(process.env.TRANCE_AUTHORING_URL || 'http://127.0.0.1:8766/');
    await page.waitForFunction(()=>document.querySelectorAll('#builtin-picker option').length===8);
    assert.equal(await page.locator('.palette-piece').count(),7);
    assert.deepEqual(await page.locator('.palette-piece').evaluateAll(es=>es.slice(0,1).map(e=>e.dataset.kind)),['parallel']);
    assert.equal(await page.locator('script[src="app.js"]').count(),0);
    assert.equal(await page.inputValue('#source-format'),'text');
    for(let i=0;i<8;i++) {
      await page.selectOption('#builtin-picker',String(i));
      assert.equal(await page.inputValue('#source-format'),'text');
      const ast=await page.evaluate(()=>window.TranceV4Text.parse(document.querySelector('#source').value));
      assert.equal(ast.version,4);assert.ok(ast.root.kind);
      await page.click('#validate');
      await page.waitForFunction(()=>/compilation (passed|failed)/.test(document.querySelector('#validation-summary').textContent));
      assert.match(await page.locator('#validation-summary').textContent(),/passed/,`Built-in ${i}: ${await page.locator('#diagnostics').textContent()}`);
    }
    await page.selectOption('#source-format','json');
    // Inherit and the boolean loop control use the same native AST route.
    const finite=duration=>({kind:'parallel',duration,children:[{node:{op:'image',source:'primary',visibility:[0,1]}}]});
    const cases=[
      {root:{kind:'sequential',duration:'inherit',children:[{node:finite(200)},{node:finite(300)}]},extent:500},
      {root:{kind:'parallel',duration:'inherit',children:[{node:finite(200)},{node:finite(300)}]},extent:300},
      {root:{kind:'sequential',children:[{node:finite(400)},{node:finite(400)}]},extent:800},
      {root:{kind:'sequential',loop_subpatterns:true,children:[{node:finite(400)},{node:finite(400)}]},extent:800},
      {root:{kind:'parallel',loop_subpatterns:true,children:[{node:finite(400)},{node:finite(600)}]},extent:600},
      {root:{kind:'sequential',duration:9,children:[{node:finite(2)},{node:{kind:'parallel',children:[{node:{op:'image',source:'primary'}}]}}]},extent:9,inheritedWindow:'[2, 9)'},
      {root:{kind:'parallel',duration:9,children:[{node:finite(2)},{node:{kind:'parallel',children:[{node:{op:'image',source:'primary'}}]}}]},extent:9,inheritedWindow:'[0, 9)'},
      {root:{kind:'sequential',repeat:2,children:[{node:finite(400)},{node:finite(400)}]},extent:1600},
    ];
    for(const {root,extent,inheritedWindow} of cases){
      await page.fill('#source',JSON.stringify({version:4,root}));await page.click('#apply-source');await page.click('#validate');
      await page.waitForFunction(()=>/compilation (passed|failed)/.test(document.querySelector('#validation-summary').textContent));
      assert.match(await page.locator('#validation-summary').textContent(),/passed/,await page.locator('#diagnostics').textContent());
      assert.match(await page.locator('#timeline').textContent(),new RegExp('authored axis '+extent+'f'));
      if(inheritedWindow)assert.ok((await page.locator('.timeline-bar[data-path="[1]"]').first().getAttribute('title')).includes(inheritedWindow));
    }
    const autoTimed=duration=>({kind:'parallel',duration,children:[{node:{op:'image',source:'primary'}}]});
    const autoRoot={version:4,root:{kind:'parallel',duration:2000,children:[{node:{kind:'sequential',duration:'inherit',children:[{node:autoTimed(200)},{node:autoTimed(300)}]}}]}};
    await page.fill('#source',JSON.stringify(autoRoot));await page.click('#apply-source');
    await page.locator('.block[data-path="[0]"] > .block-head').click();assert.match(await page.locator('[data-duration-preview]').textContent(),/500 frames/);
    await page.click('#validate');await page.waitForFunction(()=>/compilation (passed|failed)/.test(document.querySelector('#validation-summary').textContent));
    assert.match(await page.locator('#validation-summary').textContent(),/passed/,await page.locator('#diagnostics').textContent());
    await page.locator('.block[data-path="[0,1]"] > .block-head').click();
    await page.fill('#inspector [data-field="duration"]','400');await page.locator('#inspector [data-field="duration"]').dispatchEvent('change');
    await page.locator('.block[data-path="[0]"] > .block-head').click();assert.match(await page.locator('[data-duration-preview]').textContent(),/600 frames/);
    assert.equal(JSON.parse(await page.inputValue('#source')).root.children[0].node.duration,'inherit');
    await page.selectOption('#source-format','text');assert.match(await page.inputValue('#source'),/sequence for inherit/);
    await page.click('#validate');await page.waitForFunction(()=>/compilation (passed|failed)/.test(document.querySelector('#validation-summary').textContent));
    assert.match(await page.locator('#validation-summary').textContent(),/passed/,await page.locator('#diagnostics').textContent());
    await page.selectOption('#source-format','json');
    await page.click('#new-document');
    await page.locator('.palette-piece[data-kind="image"] button').click();
    await page.locator('.palette-piece[data-kind="image"]').dragTo(page.locator('.block[data-path="[]"] > .block-children > .drop-slot').last());
    assert.deepEqual(JSON.parse(await page.inputValue('#source')).root.children.map(e=>e.node.op),['image','image']);
    await page.click('#validate');
    await page.waitForFunction(()=>/compilation (passed|failed)/.test(document.querySelector('#validation-summary').textContent));
    assert.match(await page.locator('#validation-summary').textContent(),/passed/,await page.locator('#diagnostics').textContent());
    const G=require('../../docs/exploration/authoring/v4-grammar.js');
    const slowIndex=await page.locator('#builtin-picker option').evaluateAll(options=>options.find(o=>/slow/i.test(o.textContent)).value);
    await page.selectOption('#builtin-picker',slowIndex);
    let slow=JSON.parse(await page.inputValue('#source'));
    function findPath(node,predicate,path=[]){if(predicate(node))return path;for(let i=0;i<(node.children||[]).length;i++){const result=findPath(node.children[i].node,predicate,[...path,i]);if(result)return result;}return null;}
    const namedPath=findPath(slow.root,n=>n.id==='fast_section');assert.ok(namedPath);
    await page.locator(`.block[data-path='${JSON.stringify(namedPath)}'] > .block-head`).click();
    await page.getByRole('button',{name:'Duplicate',exact:true}).click();
    slow=JSON.parse(await page.inputValue('#source'));
    const ids=G.ids(slow);assert.equal(new Set(ids).size,ids.length);
    const copyPath=findPath(slow.root,n=>n.id==='fast_section_2');assert.ok(copyPath);
    const imagePath=findPath(G.at(slow,copyPath),n=>n.op==='image',copyPath);assert.ok(imagePath);
    await page.locator(`.block[data-path='${JSON.stringify(imagePath)}'] > .block-head`).click();
    const allowedScopes=['local','scene',...G.ancestorIds(slow,imagePath)];
    assert.ok(await page.locator('#inspector select[data-field="scope"]').count()>0);
    for(const selector of await page.locator('#inspector select[data-field="scope"]').all())assert.deepEqual(await selector.locator('option').evaluateAll(es=>es.map(e=>e.value)),allowedScopes);
    await page.click('#validate');await page.waitForFunction(()=>/compilation (passed|failed)/.test(document.querySelector('#validation-summary').textContent));
    assert.match(await page.locator('#validation-summary').textContent(),/passed/,await page.locator('#diagnostics').textContent());
    await page.click('#new-document');
    await page.locator('.palette-piece[data-kind="parallel"]').dragTo(page.locator('.drop-slot.empty'));
    await page.locator('#inspector select[data-field="Pattern type"]').selectOption('random');
    assert.equal(JSON.parse(await page.inputValue('#source')).root.children[0].node.children[0].node.op,'image');
    await page.getByRole('button',{name:'Give outputs a duration',exact:true}).click();
    let ast=JSON.parse(await page.inputValue('#source'));assert.equal(ast.root.children[0].node.kind,'random');
    await page.locator('.block[data-path="[0]"] > .block-head').click();
    await page.locator('#inspector input[data-field="allow self"]').check();
    await page.locator('#inspector select[data-field="order"]').selectOption('round_robin');
    ast=JSON.parse(await page.inputValue('#source'));assert.equal(ast.root.children[0].node.select.order,'round_robin');assert.equal(ast.root.children[0].node.select.allow_self,false);
    const rootEnd=page.locator('.block[data-path="[]"] > .block-children > .drop-slot').last();
    await page.locator('.palette-piece[data-kind="parallel"]').dragTo(rootEnd);
    await page.locator('#inspector select[data-field="Pattern type"]').selectOption('sequential');
    await page.getByRole('button',{name:'Give outputs a duration',exact:true}).click();
    ast=JSON.parse(await page.inputValue('#source'));assert.equal(ast.root.children[1].node.kind,'sequential');
    await page.locator('.block[data-path="[1]"] > .block-head').dragTo(page.locator('.block[data-path="[0]"] > .block-children > .drop-slot').last());
    ast=JSON.parse(await page.inputValue('#source'));assert.equal(ast.root.children.length,1);assert.equal(ast.root.children[0].node.children[1].node.kind,'sequential');
    await page.locator('.block[data-path="[0,1]"] > .block-head').click();
    await page.getByRole('button',{name:'Duplicate',exact:true}).click();
    ast=JSON.parse(await page.inputValue('#source'));assert.equal(ast.root.children[0].node.children.length,3);
    await page.getByRole('button',{name:'Delete',exact:true}).click();
    await page.click('#undo');
    ast=JSON.parse(await page.inputValue('#source'));assert.equal(ast.root.children[0].node.children.length,3);
    await page.locator('.block[data-path="[0,0]"] > .block-head').click();
    await page.locator('#inspector select[data-field="Duration"]').selectOption('fixed');
    await page.locator('#inspector input[data-field="duration"]').fill('96');
    await page.locator('#inspector input[data-field="duration"]').dispatchEvent('change');
    await page.locator('#inspector input[data-field="Enable chance exit"]').check();
    await page.locator('#inspector input[data-field="check every"]').fill('4');
    await page.locator('#inspector input[data-field="check every"]').dispatchEvent('change');
    ast=JSON.parse(await page.inputValue('#source'));
    assert.equal(ast.root.children[0].node.children[0].node.duration,96);
    assert.equal(ast.root.children[0].node.children[0].exit.check_every,4);
    const valid=await page.inputValue('#source');
    const downloadEvent=page.waitForEvent('download');await page.click('#export');
    const download=await downloadEvent;const fs=require('node:fs');
    assert.deepEqual(JSON.parse(fs.readFileSync(await download.path(),'utf8')),JSON.parse(valid));
    await page.click('#new-document');
    await page.locator('#file-input').setInputFiles({name:'roundtrip.v4.json',mimeType:'application/json',buffer:Buffer.from(valid)});
    await page.waitForFunction(()=>document.querySelector('#composition-title').textContent==='roundtrip.v4.json');
    assert.deepEqual(JSON.parse(await page.inputValue('#source')),JSON.parse(valid));
    await page.fill('#source','{"version":4,"root":');await page.click('#apply-source');
    assert.equal(await page.inputValue('#source'),'{"version":4,"root":');
    await page.locator('.block[data-path="[]"] > .block-head').click();
    assert.equal(await page.inputValue('#source'),'{"version":4,"root":');
    await page.fill('#source',valid);await page.click('#apply-source');
    await page.click('#validate');await page.waitForFunction(()=>/compilation (passed|failed)/.test(document.querySelector('#validation-summary').textContent));
    assert.match(await page.locator('#validation-summary').textContent(),/passed/,await page.locator('#diagnostics').textContent());
    assert.deepEqual(errors,[]);
    console.log('PASS: eight readable-text native V4 built-ins compile; root/nested Inherit, bounded boolean loops, inherited repeats + image visibility compile; explicit conversion wrapping; drag nesting/moving; selector editing; fixed duration and chance exit; duplicate/delete/undo; JSON import/export; invalid source retained.');
  } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
