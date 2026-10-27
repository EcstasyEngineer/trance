// Independent text/audio field acceptance. Requires tools/authoring_server.py on 8766.
'use strict';
const assert = require('node:assert/strict');
const G = require('../docs/exploration/authoring/grammar.js');
let count = 0;
const wrap = statement => `pattern review_text_audio for 128f {\n ${statement}\n}`;
async function lint(source, expected = true) {
  const r = await fetch('http://127.0.0.1:8766/api/validate', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({source})});
  const result = await r.json();
  assert.equal(result.ok, expected, source+'\n'+JSON.stringify(result));
  count++;
}
function edit(statement, changes) {
  const d = G.parse(wrap(statement));
  assert.deepEqual(d.errors, []);
  const n = d.children[0].children[0];
  for (const [key,value] of changes) G.setField(n,key,value);
  const source = G.serialize(d);
  assert.deepEqual(G.parse(source).errors, [], source);
  assert.equal(G.serialize(G.parse(source)), source);
  return source;
}
(async () => {
  for (const kind of ['word','line','caption','subtext']) {
    const initial = kind+' primary chance (0.5)';
    for (const content of ['primary','secondary','runtime']) await lint(edit(initial,[['content',content]]));
    for (const chance of ['0','1','0.25','(0.75)','']) await lint(edit(initial,[['chance',chance]]));
    for (const key of ['zoom','origin','alpha','brightness']) {
      for (const value of ['0.3','(curve 0 -> 1 ease late over review_text_audio)','[max(0, this.progress)]']) {
        await lint(edit(initial,[[key,value]]));
        await lint(edit(initial,[[key,value],[key,'']]));
      }
    }
    for (const [key,value] of [['fade','in'],['fade','out'],['fade','inout'],['show','0.1..0.9'],['show','0f..32f'],['show','[this.frame < 40]'],['env','in 8f hold 16f out 8f'],['env','in 0.1 out 0.2']]) {
      await lint(edit(initial,[[key,value]]));
      await lint(edit(initial,[[key,value],[key,'']]));
    }
    await lint(wrap(kind+' alternate'),false);
    await lint(wrap(kind+' primary -> saved'),false);
  }
  for (const content of ['primary','secondary','runtime']) {
    for (const loop of ['true','false']) {
      for (const volume of ['0','0.7','(curve 0 -> 1 ease early over review_text_audio)','[this.progress]','']) {
        await lint(edit('audio primary loop volume 0.5',[['content',content],['audioLoop',loop],['volume',volume]]));
      }
    }
  }
  const stop = G.parse(wrap('audio stop'));
  assert.deepEqual(G.fields(stop.children[0].children[0]), []);
  await lint(G.serialize(stop));
  for (const statement of ['audio alternate','audio stop volume 0.5','audio primary chance 0.5','audio primary volume 0.5 loop']) await lint(wrap(statement),false);
  for (const kind of ['word','line','caption','subtext','audio']) {
    const st=kind==='audio'?'audio primary loop volume 0.5':kind+' primary chance 0.5';
    for (const placement of [`every 32f { ${st} }`,`pattern nested for 64f { ${st} }`,`burst period 8f chance 1/2 duration 32f { base { ${st} } burst { ${st} } }`]) await lint(wrap(placement));
  }
  // Indefinite base cannot drive normalized local curves, while a named finite ancestor can.
  await lint(wrap('burst period 8f chance 1/2 duration 32f { base { audio primary volume (curve 0 -> 1) } burst {} }'),false);
  await lint(wrap('burst period 8f chance 1/2 duration 32f { base { audio primary volume (curve 0 -> 1 over review_text_audio) } burst {} }'));
  console.log(`PASS ${count} product lint cases; text/audio content, optional field edits, probability, all parameters, loop, volume, stop, and scopes.`);
})().catch(error => {console.error(error);process.exitCode=1;});
