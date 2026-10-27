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
  const source=async()=>JSON.parse(await page.inputValue('#source'));
  const fields=()=>page.locator('#inspector [data-field]').evaluateAll(es=>es.map(e=>e.dataset.field));
  const before=async(a,b)=>{const f=await fields();assert.ok(f.includes(a)&&f.includes(b)&&f.indexOf(a)<f.indexOf(b),`${a} precedes ${b}: ${f}`);};
  try{
    await page.goto(`http://127.0.0.1:${server.address().port}/trance/`);
    await page.waitForFunction(()=>document.querySelectorAll('#builtin-picker option').length===8);
    assert.equal(await page.locator('.palette-piece').count(),7);
    assert.equal(await page.locator('#validate').textContent(),'Validate AST structure');
    for(let i=0;i<8;i++){
      await page.selectOption('#builtin-picker',String(i));
      await page.click('#validate');
      assert.match(await page.locator('#validation-summary').textContent(),/structure checks passed.*native compilation not run/);
    }
    // Root Parallel supports direct outputs through both insertion paths.
    await page.click('#new-document');
    await page.locator('.palette-piece[data-kind="image"] button').click();
    await page.locator('.palette-piece[data-kind="image"]').dragTo(page.locator('.block[data-path="[]"] > .block-children > .drop-slot').last());
    let doc=await source();assert.equal(doc.root.kind,'parallel');assert.deepEqual(doc.root.children.map(e=>e.node.op),['image','image']);
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
    await page.fill('#source',JSON.stringify({version:4,root:{kind:'parallel',duration:256,children:[{node:{op:'image',opacity:{curve:{from:0,to:1,scope:'local'}},flip_probability:.5,zoom:1,source:'alternate'}}]}}));
    await page.click('#apply-source');
    await page.locator('.block[data-path="[0]"] > .block-head').click();
    await before('source','flip probability');await before('opacity mode','from');await before('zoom mode','zoom');
    await page.selectOption('#inspector [data-field="source"]','primary');
    assert.equal(await page.locator('#inspector [data-field="flip probability"]').count(),0);
    // Structural validation rejects an incomplete schedule without claiming compilation.
    await page.click('#new-document');await page.click('#validate');
    assert.match(await page.locator('#validation-summary').textContent(),/structure checks failed/);
    assert.equal(requests.some(r=>r.startsWith('POST ')),false);
    assert.ok(requests.includes('GET /trance/builtins-v4.json'));
    assert.deepEqual(errors,[]);
    console.log('PASS: static repository subpath; eight built-ins; honest structure validation; direct Parallel image + and drag; one Pattern dropdown; mode-before-parameters order.');
  }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(e=>{console.error(e);process.exitCode=1;});
