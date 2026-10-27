// Independent burst grammar/inspector acceptance; product --lint only, no playback.
'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),os=require('node:os'),path=require('node:path'),cp=require('node:child_process');
const G=require('../docs/exploration/authoring/grammar.js');
const exe=path.resolve(__dirname,'../build/windows-msvc/Release/trance.exe');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'trance-review05-'));
const cases=[];
const add=(name,body,ok=true)=>cases.push({name,source:`pattern scene for 256f { ${body} }`,ok});
add('minimal defaults','burst period 8f {base {image primary} burst {image secondary}}');
add('all named branches','burst -> controller period 8f chance 1/2 cooldown 16f duration 24f {base -> resting {every 8f -> cut {image primary zoom (curve 0 -> 0.2 over controller)}} enter {anim runtime} burst -> held {image secondary zoom (curve 0 -> 0.4 over held)}}');
add('sampled duration and nested clocks','burst -> controller duration 9f..25f cooldown 0f chance 1/1 period 8f {base -> resting {every 16f -> cut {image primary zoom (curve 0 -> 0.3 over scene)}} burst -> held {every 8f -> cut {image secondary zoom (curve 0 -> 0.4 over held)}}}');
add('nested controller under held','burst period 8f duration 16f..64f {burst {burst period 4f duration 8f {base {image primary} burst {image secondary}}}}');
add('nested controller under base','burst period 8f {base {burst period 4f {base {image primary} burst {image secondary}}}}');
add('zero duration rounds up','burst period 8f duration 0f cooldown 0f {burst {image primary}}');
add('nonmultiple duration','burst period 8f duration 9f {burst {image primary env in 1f hold 1f out 1f}}');
add('sampled fractional envelope','burst period 8f duration 16f..32f {burst {image primary env in 0.1 hold 0.5 out 0.1}}');
add('missing period','burst {base {}}',false);
add('zero period','burst period 0f {base {}}',false);
add('zero denominator','burst period 8f chance 1/0 {base {}}',false);
add('wrong numerator','burst period 8f chance 2/3 {base {}}',false);
add('reverse range','burst period 8f duration 32f..16f {burst {}}',false);
add('duplicate branch','burst period 8f {base {} base {}}',false);
add('entry timed child','burst period 8f {enter {every 4f {image primary}} burst {}}',false);
add('entry named','burst period 8f {enter -> setup {anim primary} burst {}}',false);
add('base normalized curve','burst period 8f {base {image primary zoom (curve 0 -> 1)}}',false);
add('sampled frame envelope','burst period 8f duration 16f..32f {burst {image primary env in 1f hold 1f out 1f}}',false);
add('sampled ramp','burst period 8f duration 16f..32f {burst {every ramp 8f -> 4f steps 2 {image primary}}}',false);
add('nested indefinite controller normalized','burst period 8f {base {burst -> inner period 4f {burst {image primary zoom (curve 0 -> 1 over inner)}}}}',false);
let product=0,roundtrips=0;
function lint(source){const file=path.join(temp,'case.pattern');fs.writeFileSync(file,source);const r=cp.spawnSync(exe,['--lint','--pattern='+file],{cwd:temp,encoding:'utf8',timeout:20000,windowsHide:true});assert(!r.error,String(r.error));return r;}
try{
 for(const c of cases){const doc=G.parse(c.source);assert.equal(G.serialize(doc),c.source,c.name+' lossless');roundtrips++;if(c.ok)assert.deepEqual(doc.errors,[],c.name+' editor parse');const r=lint(c.source);assert.equal(r.status===0,c.ok,c.name+' '+r.stdout+r.stderr);product++;}
 const original='pattern scene for 256f {\n # controller comment\n burst -> controller period 8f chance 1/2 cooldown 16f duration 24f..48f {base -> resting {} enter {anim primary} burst -> held {image secondary}}\n}';
 const doc=G.parse(original),controller=doc.children[0].children[0];
 for(const [key,value] of [['period','4f'],['chance','1/3'],['cooldown','12f'],['duration','16f'],['target','switcher']]){G.setField(controller,key,value);const r=lint(G.serialize(doc));assert.equal(r.status,0,key+' edit '+r.stdout+r.stderr);product++;}
 assert(G.serialize(doc).includes('# controller comment'));assert.equal(controller.children[0].header,'base -> resting ');assert.equal(controller.children[2].header,'burst -> held ');
 for(const [key,value] of [['duration','24f..48f'],['chance',''],['cooldown',''],['duration',''],['target','']]){G.setField(controller,key,value);const r=lint(G.serialize(doc));assert.equal(r.status,0,key+' clear/range '+r.stdout+r.stderr);product++;}
 for(const [key,value] of [['target','newcontroller'],['chance','1/4'],['cooldown','0f'],['duration','16f..32f']]){G.setField(controller,key,value);const r=lint(G.serialize(doc));assert.equal(r.status,0,key+' add '+r.stdout+r.stderr);product++;}
 assert(!G.palette.some(piece=>piece.kind==='enter'));
 for(const kind of ['base','enter','burst-phase']){const n=kind==='enter'?G.parse('enter {\n}').children[0]:G.create(kind);assert.equal(n.kind,kind);if(kind!=='enter'){G.setField(n,'target','phase');assert.equal(G.fields(n).find(f=>f.key==='target').value,'phase');}assert.equal(G.serialize(n).trim(),n.header+'{\n}');}
 console.log(JSON.stringify({result:'pass',productLintCases:product,losslessRoundtrips:roundtrips,checks:['fixed/ranged/default burst options','branch and controller names','nested clocks','invalid product diagnostics','inspector replacement removal addition','palette contextual phase construction and source-only entry compatibility']}));
}finally{fs.rmSync(temp,{recursive:true,force:true});}
