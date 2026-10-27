// Independent media authoring acceptance; real product lint, never playback.
'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),cp=require('node:child_process');
const G=require('../docs/exploration/authoring/grammar.js');
const root=path.resolve(__dirname,'..'), exe=path.join(root,'build/windows-msvc/Release/trance.exe');
assert(fs.existsSync(exe),'Build Release trance.exe before this acceptance test');
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'trance-media-review02-'));
const cases=[];
const add=(label,body,ok=true)=>cases.push({label,source:`pattern scene for 128f { ${body} }`,ok});
for(const content of ['primary','secondary','runtime','alternate','alternate chance (0.4)']) {
 add(content,`image ${content} alpha 0.7 anim every 2nd chance 0.8`);
 add('standalone '+content,`anim ${content}`);
}
add('independent image lanes','pattern back for 128f {every 64f {image primary}} pattern front for 128f {every 64f offset 32f {image secondary alpha 0.5}}');
add('snapshot rejected','image primary copy cur -> previous',false);
add('all modifiers','image runtime zoom (curve 0 -> 0.5 ease late over scene) origin [this.progress] alpha 0.7 brightness 0.8 fade inout show 0f..8f env in 0.1 hold 0.2 out 0.1 anim every 3rd chance (0.6)');
add('reordered repeated modifiers','image primary alpha 0.5 zoom 0.1 alpha 0.8 show [this.frame % 2 == 0] fade in anim');
add('draw plain animation rejected','image primary draw cur zoom 0.2 anim',false);
add('every ordinal optional','image runtime anim every 3 chance 0.5');
add('wrong modifier order','image primary anim alpha 0.5',false);
add('draw nth rejected','image primary draw cur anim every 2nd',false);
add('standalone final chance rejected','anim runtime chance 0.5',false);
add('foreign register scope rejected','image primary -> absent.cur',false);
let passed=0;
try {
 for(const c of cases){
  const d=G.parse(c.source);assert.equal(G.serialize(d),c.source,c.label+' lossless');
  if(c.ok)assert.deepEqual(d.errors,[],c.label+' editor parser');
  fs.writeFileSync(path.join(dir,'case.pattern'),c.source);
  const r=cp.spawnSync(exe,['--lint','--pattern='+path.join(dir,'case.pattern')],{cwd:dir,encoding:'utf8',timeout:20000,windowsHide:true});
  assert.equal(r.status===0,c.ok,c.label+' product lint: '+r.stdout+r.stderr);passed++;
 }
 // Existing parameter edits preserve inline alternate chance, animation and final selection chance.
 const d=G.parse('pattern scene for 128f { image alternate chance (0.4) alpha 0.5 anim every 2nd chance 0.8 }');
 const image=d.children[0].children[0];assert(!G.fields(image).some(f=>f.key==='target')); G.setField(image,'alpha','0.6');G.setField(image,'show','0f..8f');
 assert.deepEqual(G.parse(G.serialize(d)).errors,[]);
 assert(G.serialize(d).includes('alternate chance (0.4)'));
 assert(G.serialize(d).includes('anim every 2nd chance 0.8'));
 fs.writeFileSync(path.join(dir,'case.pattern'),G.serialize(d));
 const edited=cp.spawnSync(exe,['--lint','--pattern='+path.join(dir,'case.pattern')],{cwd:dir,encoding:'utf8',timeout:20000,windowsHide:true});
 assert.equal(edited.status,0,edited.stdout+edited.stderr);passed++;
 console.log(`PASS review02: ${passed} product lint cases, lossless roundtrips and structured media edits`);
} finally {fs.rmSync(dir,{recursive:true,force:true});}

// Regression: alternation chance and image-selection chance are independent roles.
const alternate=G.parse('image alternate chance (0.4) alpha 0.5 anim every 2nd chance 0.8').children[0];
assert.equal(G.fields(alternate).find(f=>f.key==='alternateChance').value,'(0.4)');
assert.equal(G.fields(alternate).find(f=>f.key==='chance').value,'0.8');
G.setField(alternate,'content','primary');
assert.deepEqual(G.parse(alternate.header).errors,[]);
assert(alternate.header.includes('anim every 2nd chance 0.8'));
assert(!alternate.header.includes('chance (0.4)'));
const independent=G.parse('image alternate chance 0.4 alpha 0.5').children[0];
G.setField(independent,'chance','0.8');
assert.deepEqual(G.parse(independent.header).errors,[]);
assert.equal(G.fields(independent).find(f=>f.key==='alternateChance').value,'0.4');
assert.equal(G.fields(independent).find(f=>f.key==='chance').value,'0.8');
const standalone=G.parse('anim alternate chance (0.4)').children[0];G.setField(standalone,'content','runtime');
assert.deepEqual(G.parse(standalone.header).errors,[]);
assert(!standalone.header.includes('chance'));
assert(G.parse('draw cur anim').errors.length);
assert(!G.palette.some(piece=>['copy','draw'].includes(piece.kind))); 
console.log('PASS review02: independent chance controls, alternate content switching, removed storage rejection');
