// Run: node tests/authoring_grammar_test.js. No browser dependencies required.
'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const root=path.resolve(__dirname,'..');
const G=require(path.join(root,'docs/exploration/authoring/grammar.js'));
const builtins=JSON.parse(fs.readFileSync(path.join(root,'docs/exploration/authoring/builtins.json'),'utf8')).builtins;
assert.equal(builtins.length,8);
for(const b of builtins){const doc=G.parse(b.source);assert.deepEqual(doc.errors,[],b.key);assert.equal(G.serialize(doc),b.source,b.key+' roundtrip');G.walk(doc,n=>{if(n.header)for(const f of G.fields(n)){assert.equal(n.header.slice(f.start,f.end),f.type==='boolean'?'':f.value);}});}
assert.equal(builtins[4].key,'parallel');assert.equal(builtins[4].sourcePatternName,'simple');
const coverage=`# comment before root\r\npattern scene for beats 8 loop 2 seq {\r\n
  look { spiral type=1 width=20 spiral width=12 }
  image alternate chance (0.5) zoom (curve -0.1 -> 0.5 ease late over scene) origin [max(0, this.progress)] alpha 0.5 brightness 0.7 fade inout show 0f..8f show [self.active and (scene.frame % 2 == 0)] env in 0.1 hold 0.2 out 0.1 anim every 3rd chance 0.6
  word primary line secondary caption runtime subtext secondary
  anim alternate chance 0.5
  spiral speed [scene.index + 1]
  warp amplitude (curve 0 -> 1) wavelength 0.15 speed 2 drunk 0.1
  audio primary loop volume (curve 0 -> 0.5) audio stop
  every locked -> pulse offset beats 1 { image primary }
  every ramp 8f -> 16f steps 4 ease early -> ramp { image secondary }
  burst -> controller period 8f duration 16f..32f chance 1/2 cooldown 8f {
    base -> waiting { every 8f { image primary } }
    enter { anim secondary }
    burst -> held { image secondary zoom (curve 0 -> 0.5) }
  }
} # tail comment\n`;
const doc=G.parse(coverage);assert.deepEqual(doc.errors,[]);assert.equal(G.serialize(doc),coverage);
const kinds=new Set();G.walk(doc,n=>kinds.add(n.kind));for(const p of G.palette){assert(kinds.has(p.kind==='audio-stop'?'audio':p.kind==='sequential-pattern'?'pattern':p.kind),'coverage '+p.kind);}
let image;G.walk(doc,n=>{if(n.kind==='image'&&!image)image=n;});
assert.equal(G.fields(image).find(f=>f.key==='show').value,'0f..8f');
G.setField(image,'zoom','(curve 0 -> 0.8 ease early)');assert(image.header.includes('zoom (curve 0 -> 0.8 ease early)'));assert(!G.parse(G.serialize(doc)).errors.length);
const minimal=G.create('image');for(const [k,v]of [['show','0f..8f'],['animation','anim every 2nd'],['chance','0.5'],['alpha','0.7']])G.setField(minimal,k,v);assert.equal(G.parse(G.serialize(minimal)).errors.length,0);
const named=G.parse('pattern seq for 128f { every ramp 24f -> 4f steps 4 -> cut { image primary } }');G.setField(named.children[0],'sequence','true');G.setField(named.children[0].children[0],'ease','early');assert(G.serialize(named).includes('pattern seq for'));assert(G.serialize(named).includes('steps 4  ease early -> cut'));assert.deepEqual(G.parse(G.serialize(named)).errors,[]);
const alternate=G.parse('image alternate chance (0.4) alpha 0.5').children[0];assert(G.fields(alternate).some(f=>f.key==='alternateChance'));assert(G.fields(alternate).some(f=>f.key==='chance'&&f.value===''));G.setField(alternate,'chance','0.7');G.setField(alternate,'content','primary');assert.deepEqual(G.parse(G.serialize(alternate)).errors,[]);assert(!alternate.header.includes('0.4'));assert(alternate.header.includes('chance 0.7'));
const comment=G.parse('pattern loop for 128f # keep this seq comment\n{ burst period 8f {base {}} }').children[0];G.setField(comment,'sequence','true');G.setField(comment,'loop','2');G.setField(comment.children[0],'target','controller');assert(G.serialize(comment).includes('burst -> controller  period'));assert(G.serialize(comment).includes('# keep this seq comment'));assert.deepEqual(G.parse(G.serialize(comment)).errors,[]);
const unknown='pattern p for 8f {\n  made_up 42\n  image primary\n}\n';const u=G.parse(unknown);assert(u.errors.length);assert.equal(G.serialize(u),unknown);assert(u.children[0].children.some(n=>n.kind==='unknown'));
const malformed='pattern x for 8f { image primary';const m=G.parse(malformed);assert(m.errors.length);assert.equal(G.serialize(m),malformed);
// Moves preserve all source, comments and nested nodes; only order is changed.
const move=G.parse('pattern p for 16f {\n # first\n image primary\n # second\n image secondary\n}\n');const cs=move.children[0].children;cs.unshift(cs.pop());assert.equal(G.parse(G.serialize(move)).errors.length,0);assert(G.serialize(move).indexOf('# second')<G.serialize(move).indexOf('# first'));
console.log('PASS eight builtins, all grammar families, lossless source/comments, structured fields, unknown preservation and reorder');

assert.match(G.create("sequential-pattern").header,/\bseq\b/);
assert.deepEqual(G.parse("pattern p for 8f { image either anim }").errors,[]);
