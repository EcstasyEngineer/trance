// Independent source-preservation review. No playback; optional native browser I/O.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const G=require('../docs/exploration/authoring/grammar.js');
const root=path.resolve(__dirname,'..');
const builtins=JSON.parse(fs.readFileSync(path.join(root,'docs/exploration/authoring/builtins.json'),'utf8')).builtins;
let rounds=0;
function roundtrip(s){assert.equal(G.serialize(G.parse(s)),s,'source preservation '+JSON.stringify(s));rounds++;}
const fixtures=[
 '', ' # comment only\r\n', '\ufeff# BOM\r\npattern p for 8f {image primary}',
 'pattern image for 8f {image primary -> every copy every -> burst draw burst}',
 'pattern p for 8f {image primary}pattern q for 8f {spiral speed 2}',
 'pattern p for 8f {future xyz 42\r\nimage primary} # final',
 'future foo { image primary } pattern q for 8f {}',
 'pattern p for 8f {future {future {image primary}}}',
 'pattern p for 8f {image primary zoom [1 + (2] alpha 1}',
 'pattern p for 8f {image primary zoom [broken }',
 'pattern p for 8f {image primary', 'pattern p for 8f {',
 '}}future 42\r\n', 'pattern p for 8f {image primary}}#tail',
 'pattern p for 8f # brace { comment\r\n{image primary # inline }\r\n}',
 'pattern p for 8f {every 4f {image primary}word primary caption secondary}',
];
for(const s of [...fixtures,...builtins.map(b=>b.source)]){
 for(const eol of ['\n','\r\n'])for(const final of [false,true]){
  const t=s.replace(/\r?\n/g,eol).replace(/[\r\n]+$/,'')+(final?eol:'');roundtrip(t);
 }
}
// Bounded deterministic mutation of known syntax, including every character cut.
const base='pattern p for 8f {every 4f {image primary zoom [this.frame + 1]} # c\nword secondary}';
for(let i=0;i<=base.length;i++)roundtrip(base.slice(0,i));
let seed=17;const parts=['pattern','image','primary','future','{','}','[',']','#x\n','\r\n',' ', '->','42','for'];
for(let c=0;c<200;c++){let s='';for(let j=0;j<20;j++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;s+=parts[seed%parts.length];}roundtrip(s);}
const edit='pattern p for 8f {\r\n # before\r\n image primary zoom 1 # inline\r\n future glyph Ω 42\r\n word secondary\r\n}#tail';
const d=G.parse(edit),image=d.children[0].children.find(n=>n.kind==='image');
G.setField(image,'zoom','2');assert.equal(G.serialize(d),edit.replace('zoom 1','zoom 2'));roundtrip(G.serialize(d));
async function compiler(){
 const cases=[
  ['# CRLF\r\npattern p for 8f {image primary}\r\n#tail',true],
  ['pattern image for 8f {image primary -> every copy every -> burst draw burst}',false],
  ['pattern p for 8f {every 4f {image primary}word primary caption secondary}',true],
  ['pattern p for 8f {image primary}pattern q for 8f {spiral speed 2}',false],
  ['pattern p for 8f {future xyz 42 image primary}',false],
  ['pattern p for 8f {image primary',false],
  ['pattern p for 8f {image primary zoom [broken }',false],
 ];
 for(const [source,expected]of cases){const r=await fetch((process.env.AUTHORING_URL||'http://127.0.0.1:8766')+'/api/validate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({source})});const result=await r.json();assert.equal(result.ok,expected,source+' '+result.stdout+result.stderr);}
 return cases.length;
}
async function browser(){
 const {chromium}=require('playwright');
 const b=await chromium.launch({headless:true,channel:process.env.PLAYWRIGHT_CHANNEL||'msedge'});
 const findings=[];
 try{
  const p=await b.newPage();await p.goto(process.env.AUTHORING_URL||'http://127.0.0.1:8766/');
  await p.waitForFunction(()=>window.TranceComposer);
  async function imported(source){await p.locator('#file-input').setInputFiles({name:'review17.pattern',mimeType:'text/plain',buffer:Buffer.from(source)});await p.waitForFunction(s=>TranceComposer.getSource()===s,source);}
  async function exported(){const [download]=await Promise.all([p.waitForEvent('download'),p.locator('#export').click()]);const chunks=[];for await(const chunk of await download.createReadStream())chunks.push(chunk);return Buffer.concat(chunks).toString('utf8');}
  const crlf='# preserved\r\npattern p for 8f {image primary}\r\n#final';
  await imported(crlf);assert.equal(await exported(),crlf,'native import/export CRLF');
  await p.locator('#apply-source').click();
  if(await exported()!==crlf)findings.push('Apply source without changes normalizes imported CRLF');
  const unknown='pattern p for 8f {\r\n future foo { image primary }\r\n}\r\n';
  await imported(unknown);assert.equal(await exported(),unknown,'native unknown import/export');
  const dense='pattern p for 8f {image primary}';await imported(dense);
  await p.evaluate(()=>TranceComposer.selectNode(TranceComposer.getDocument().children[0].children[0].id));
  await p.locator('#duplicate-block').click();const duplicated=await exported();
  if(G.parse(duplicated).errors.length)findings.push('Duplicate first child without whitespace creates invalid '+duplicated);
  await p.locator('#undo').click();assert.equal(await exported(),dense,'undo duplicate exact source');
  console.log(JSON.stringify({browser:'native imports, downloads, Apply, Duplicate, Undo',findings}));
  assert.deepEqual(findings,[],'unresolved source editing defects');
 }finally{await b.close();}
}
(async()=>{const lintCases=process.argv.includes('--compiler')?await compiler():0;if(process.argv.includes('--browser'))await browser();console.log(JSON.stringify({result:'pass',losslessCases:rounds,realCompilerCases:lintCases}));})().catch(e=>{console.error(e);process.exitCode=1;});
