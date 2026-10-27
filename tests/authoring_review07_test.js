// Independent resource-scope acceptance: node tests/authoring_review07_test.js
'use strict';
const assert = require('node:assert/strict');
const G = require('../docs/exploration/authoring/grammar.js');
let checks = 0;
async function lint(source, ok = true, diagnostic) {
  const response = await fetch('http://127.0.0.1:8766/api/validate', {
    method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({source})
  });
  assert.equal(response.status,200);
  const result = await response.json();
  assert.equal(result.ok,ok,source+'\n'+JSON.stringify(result));
  if(diagnostic) assert.match(result.diagnostics.join('\n'),diagnostic);
  checks++;
  return result;
}
function roundtrip(source) {
  const doc = G.parse(source);
  assert.deepEqual(doc.errors,[],source);
  assert.equal(G.serialize(doc),source);
  G.walk(doc,node=>{
    if(node.kind==='document')return;
    for(const field of G.fields(node)) {
      if(field.type==='boolean'||field.start===field.end)continue;
      G.setField(node,field.key,field.value);
    }
  });
  assert.equal(G.serialize(doc),source,'Existing field values must preserve every source byte');
  return doc;
}
const sources = [
  'pattern Scene for 128f { pattern Back for 128f {every 64f {image primary}} pattern Front for 128f {every 64f offset 32f {image secondary alpha (curve 0 -> 1 over Scene)}} }',
  'pattern Scene for 128f { every 64f -> pulse { every 8f -> cut { image primary alpha (curve 0 -> 1 over pulse) zoom [Scene.progress+cut.progress] } } }',
  'pattern Scene for 128f { pattern alpha for 128f {image primary} pattern zoom for 128f {every 8f -> brightness {image secondary}} }',
  'pattern Scene for 128f { image primary pattern Scene for 64f { image secondary alpha (curve 0 -> 1 over Scene) } }',
  'pattern Scene for 128f { burst -> controller period 8f chance 1/2 duration 32f { base -> idle { every 8f { image primary } } enter { anim secondary } burst -> held { image secondary alpha (curve 0 -> 1 over held) } } }',
  '# Lane annotations retained\npattern Scene for 128f {\n pattern Back for 128f {image primary} # outer lane\n pattern Front for 128f {every 8f -> pulse {image secondary alpha [pulse.progress]}}\n}\n'
];
(async()=>{
  for(const source of sources) await lint(G.serialize(roundtrip(source)));
  // Removed storage is preserved losslessly for migration, never accepted by either parser.
  for(const legacy of ['image primary -> stash','draw cur','copy cur -> previous',
      'image primary -> Scene.stash','draw Left.stash','draw missing',
      'every ramp 16f -> 8f steps 4 {image primary -> stash draw stash}']) {
    const source='pattern Scene for 128f { '+legacy+' }';
    const doc=G.parse(source);
    assert.equal(G.serialize(doc),source,'legacy source remains recoverable');
    assert(doc.errors.length,'removed storage must be diagnosed in editor');
    await lint(source,false);
  }
  const doc=roundtrip(sources[0]),front=doc.children[0].children[1],cadence=front.children[0];
  G.setField(cadence,'offset','16f');
  G.setField(cadence.children[0],'alpha','(curve 0 -> 0.8 over Scene)');
  await lint(G.serialize(doc));
  assert.match(G.serialize(doc),/offset 16f/);
  assert(!G.fields(cadence.children[0]).some(field=>['target','register'].includes(field.key)));
  // Moving an independent lane preserves its private image selection and source comments.
  const moved=roundtrip(sources[0]),scene=moved.children[0];
  scene.children.reverse();await lint(G.serialize(moved));
  const renamed=roundtrip(sources[0]);G.setField(renamed.children[0],'name','Renamed');
  await lint(G.serialize(renamed),false,/no enclosing pattern\/clock named 'Scene'/);
  await lint('pattern Scene for 128f { every 16f -> pulse { image primary alpha (curve 0 -> 1 over missing) } }',false,/no enclosing pattern\/clock named 'missing'/);
  const duplicate=roundtrip('pattern Scene for 128f { pattern Inner for 64f { image primary } }');
  const serialized=G.serialize({kind:'document',children:[duplicate.children[0].children[0]],tail:''});
  const clone=G.parse(serialized).children[0];
  assert.notEqual(clone.id,duplicate.children[0].children[0].id);
  duplicate.children[0].children.push(clone);await lint(G.serialize(duplicate));
  console.log('PASS '+checks+' real compiler cases: independent lanes, lossless migration rejection, clock references, field edits, duplicate and scope changes.');
})().catch(error=>{console.error(error);process.exitCode=1;});
