const assert=require('node:assert/strict');
const {chromium}=require('playwright');
(async()=>{
 const browser=await chromium.launch({channel:'msedge',headless:true});
 try{
  const page=await browser.newPage({viewport:{width:1700,height:1000}}),errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(process.env.AUTHORING_URL||'http://127.0.0.1:8766');await page.waitForFunction(()=>window.TranceComposer);
  for(let i=1;i<=8;i++){
   await page.selectOption('#builtin-picker',String(i));
   const original=await page.locator('#source').inputValue();
   const id=await page.evaluate(()=>{const out=[];TranceGrammar.walk(TranceComposer.getDocument(),n=>out.push(n));return out.find(n=>n.kind==='image').id});
   await page.locator(`[data-node-id="${id}"] > .block-head`).click();
   assert.equal(await page.locator('#source').inputValue(),original,'inspection must preserve exact source '+i);
   assert.equal(await page.locator('[aria-label="Animation accent"]').count(),1);
   assert.equal(await page.locator('#inspector input[data-field="animation"]').count(),0,'no opaque animation string input');
  }
  await page.selectOption('#builtin-picker','5');
  const id=await page.evaluate(()=>{const out=[];TranceGrammar.walk(TranceComposer.getDocument(),n=>out.push(n));return out.find(n=>n.kind==='image').id});
  await page.locator(`[data-node-id="${id}"] > .block-head`).click();
  assert.equal(await page.locator('[data-animation-every]').inputValue(),'3');
  await page.locator('[data-animation-every]').fill('7');await page.locator('[data-animation-every]').press('Tab');
  assert.match(await page.locator('#source').inputValue(),/anim every 7th/);
  await page.selectOption('[aria-label="Animation accent mode"]','');
  assert.doesNotMatch(await page.locator('#source').inputValue(),/\banim\b/);
  await page.selectOption('[aria-label="Animation accent mode"]','anim');
  assert.match(await page.locator('#source').inputValue(),/\banim\b/);
  await page.selectOption('[aria-label="Animation accent mode"]','anim every 4th');
  assert.equal(await page.locator('[data-animation-every]').inputValue(),'4');
  const before=await page.locator('#source').inputValue();
  await page.locator('[data-animation-every]').fill('0');await page.locator('[data-animation-every]').press('Tab');
  assert.equal(await page.locator('#source').inputValue(),before,'invalid interval rejected');
  await page.locator('summary').filter({hasText:'Advanced: saved image handle'}).click();
  await page.locator('#inspector [data-field="target"]').fill('chosen');await page.locator('#inspector [data-field="target"]').press('Tab');
  assert.match(await page.locator('#source').inputValue(),/image runtime -> chosen\s+zoom/);
  assert.deepEqual(errors,[]);
  console.log('PASS all8 inspection preserves source; image animation mode/count and advanced handle editing');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1});
