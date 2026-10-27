// Independent expression/curve/window/envelope authoring review; never plays media.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),cp=require('node:child_process');
const G=require('../docs/exploration/authoring/grammar.js');
const exe=path.resolve(__dirname,'../build/windows-msvc/Release/trance.exe');
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'trance-review06-'));
let count=0;
function lint(body,ok=true){const source=`pattern scene for 128f { every 64f -> cut { image primary ${body} } }`;
 const doc=G.parse(source);assert.equal(G.serialize(doc),source,'lossless '+body);
 if(ok)assert.deepEqual(doc.errors,[],'editor structure '+body);
 const file=path.join(temporary,'case.pattern');fs.writeFileSync(file,source);
 const r=cp.spawnSync(exe,['--lint','--pattern='+file],{cwd:temporary,encoding:'utf8',timeout:20000,windowsHide:true});
 assert(!r.error,String(r.error));assert.equal(r.status===0,ok,body+' '+r.stdout+r.stderr);count++;return source;
}
async function browser(){const {chromium}=require('playwright');const b=await chromium.launch({headless:true,channel:'msedge'});
 try{const p=await b.newPage();await p.goto('http://127.0.0.1:8766/');await p.waitForFunction(()=>window.TranceComposer);
 async function selectImage(){await p.evaluate(()=>{TranceComposer.loadSource('pattern scene for 128f {every 64f -> cut {image primary}}');const n=TranceComposer.getDocument().children[0].children[0].children[0];TranceComposer.selectNode(n.id);});await p.locator('details').filter({has:p.locator('#mod-mode')}).locator('summary').click();}
 const cases=[{mode:'constant',target:'zoom',from:'.25',expected:/zoom 0\.25/},
 {mode:'curve',target:'zoom',from:'-.25',to:'.75',clock:'scene',ease:'early',expected:/zoom \(curve -0\.25 -> 0\.75 ease early over scene\)/},
 {mode:'expression',target:'alpha',expr:'max(0, min(1, this.progress^2))',expected:/alpha \[max\(0, min\(1, this.progress\^2\)\)\]/},
 {mode:'expression',target:'show',expr:'this.active and this.frame%8<4',expected:/show \[this.active and this.frame%8<4\]/},
 {mode:'fraction-window',target:'show',from:'0.25',to:'0.75',expected:/show 0\.25\.\.0\.75/},
 {mode:'frame-window',target:'show',from:'4',to:'12',expected:/show 4f\.\.12f/},
 {mode:'envelope',target:'env',from:'8',to:'8',expected:/env in 8f hold 16f out 8f/}];
 for(const c of cases){await selectImage();await p.locator('#mod-target').selectOption(c.target);const allowed=await p.locator('#mod-mode option:enabled').evaluateAll(xs=>xs.map(x=>x.value));assert.deepEqual(allowed,c.target==='show'?['expression','fraction-window','frame-window']:c.target==='env'?['envelope']:['constant','curve','expression']);await p.locator('#mod-mode').selectOption(c.mode);if(c.from!==undefined)await p.locator('#mod-from').fill(c.from);if(c.to!==undefined)await p.locator('#mod-to').fill(c.to);if(c.expr)await p.locator('#mod-expression').fill(c.expr);if(c.clock)await p.locator('#mod-clock').selectOption(c.clock);if(c.ease)await p.locator('#mod-ease').selectOption(c.ease);await p.locator('#apply-modulator').click();assert.match(await p.locator('#source').inputValue(),c.expected);await p.locator('#validate').click();await p.waitForFunction(()=>!document.getElementById('validate').disabled);assert.match(await p.locator('#document-status').innerText(),/Compiler validated/);}
 return cases.length;
 }finally{await b.close();}}
async function main(){try{
 for(const e of ['-2^2','(2^3^2)/1000','!0','--2','1+2*3','8/2%3','1==1','1!=2','1<2','1>0','1<=1','1>=1','1 or 0 and 0','0?1:1?2:3','min(3,max(1,abs(-2)))','.25 + 1.','(this.progress + self.frame + cut.length + scene.position + root.index + this.active)/1000','unknown_scalar'])lint('zoom ['+e+']');
 for(const v of ['-0.25','0.25','1.','(0.5)','curve -1 -> .5','(curve 0 -> 1 ease early over scene)','curve 1 -> 0 ease late over cut'])lint('zoom '+v);
 for(const v of ['.25','1e-3','+1','curve 0 -> 1 ease cubic','curve 0 -> 1 over missing'])lint('zoom '+v,false);
 for(const v of ['show 0.25..0.75','show 4f..12f','show [this.frame%8<4] show [this.active or 0]','env in 8f hold 16f out 8f','env in 0.25 out 0.25','env in 8f hold 0.25 out 8f'])lint(v);
 for(const v of ['show 0f..0.5','show 0.75..0.25','show 0..1.1','show 0f..65f','env in .25 out .25','env in 0f out 8f','env in 0.5 hold 0.5 out 0.5'])lint(v,false);
 // These are deliberately documented compiler limits, not valid expression claims.
 for(const e of ['sin(1)','1e-3','1 +','(1','1<2<3','min(1,2,3)','missing.progress','this.missing','+1'])lint('zoom ['+e+']');
 const d=G.parse('pattern scene for 128f {image primary zoom [min(1, this.progress)] alpha 1 show [this.active] env in 8f out 8f}');const n=d.children[0].children[0];
 for(const [key,value]of [['zoom','(curve -0.25 -> 0.75 ease early over scene)'],['alpha','[this.frame%8<4?1:0]'],['show','4f..12f'],['env','in .1 hold .2 out .1']]){G.setField(n,key,value);assert.equal(G.fields(n).find(f=>f.key===key).value,value);}
 const browserCases=process.argv.includes('--browser')?await browser():0;
 console.log(JSON.stringify({result:'pass',productLintCases:count,guiCompositions:browserCases,limitations:'Raw expressions are retained without a strict syntax/type/function/attribute validator; lint acceptance does not prove evaluator meaning.'}));
 }finally{fs.rmSync(temporary,{recursive:true,force:true});}}
main().catch(e=>{console.error(e);process.exitCode=1;});





