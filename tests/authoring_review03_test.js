// Independent render-field acceptance: node tests/authoring_review03_test.js
'use strict';
const assert=require('node:assert/strict');
const G=require('../docs/exploration/authoring/grammar.js');
let checks=0;
async function lint(source, expected=true){
  const r=await fetch('http://127.0.0.1:8766/api/validate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({source})});
  const v=await r.json();assert.equal(v.ok,expected,source+'\n'+JSON.stringify(v));checks++;
}
function doc(statement){return G.parse('pattern render_review for 128f {\n '+statement+'\n}');}
function edited(statement,key,value){const d=doc(statement),n=d.children[0].children[0];assert.deepEqual(d.errors,[]);G.setField(n,key,value);const s=G.serialize(d);assert.deepEqual(G.parse(s).errors,[],s);return s;}
(async()=>{
  for(const k of ['zoom','origin','alpha','brightness'])for(const v of ['-0.2','(0.4)','curve 0 -> 1','(curve -0.2 -> 0.8 ease early over render_review)','curve 1 -> 0 ease late','[max(0, this.progress)]'])await lint(edited('image primary',k,v));
  for(const st of ['image primary','word primary','line primary','caption primary','subtext secondary']){
    for(const [k,v]of [['fade','in'],['fade','out'],['fade','inout'],['show','0.1..0.9'],['show','0f..32f'],['show','[this.frame < 64]'],['env','in 8f hold 16f out 8f'],['env','in 0.1 out 0.2']])await lint(edited(st,k,v));
  }
  const repeat=doc('image primary show 0f..64f show [this.frame > 8] alpha 0.2 alpha 0.5');
  const n=repeat.children[0].children[0];G.setField(n,'show:1','16f..48f');G.setField(n,'alpha:1','(curve 0 -> 1 ease linear)');
  assert(n.header.includes('show 0f..64f show 16f..48f'));assert(n.header.includes('alpha 0.2 alpha (curve'));await lint(G.serialize(repeat));
  G.setField(n,'show','');assert(n.header.includes('show 16f..48f'));await lint(G.serialize(repeat));
  for(const k of ['speed'])for(const v of ['2','(curve 1 -> 3 ease late over render_review)','[this.frame % 5]'])await lint(edited('spiral',k,v));
  for(const k of ['amplitude','wavelength','speed'])for(const v of ['0.15','(curve 0.1 -> 0.5 ease early over render_review)','[this.progress]'])await lint(edited('warp',k,v));
  for(const v of ['0.1','(curve 0 -> 0.2 over render_review)','[this.progress / 10]'])await lint(edited('drunk 0.1','intensity',v));
  for(const [k,v]of [['type','2'],['width','30']]){const d=doc('look {spiral type=1 width=20}');G.setField(d.children[0].children[0].children[0],k,v);await lint(G.serialize(d));}
  await lint('pattern render_review for 128f { pattern back for 128f {image primary} pattern front for 128f {image secondary zoom (curve 0 -> 1) origin 0.5 alpha 0.7 brightness 0.6 fade in show 0f..32f env in 8f out 8f} }');
  await lint('pattern render_review for 128f {image primary -> cur draw cur}',false);
  await lint('pattern render_review for 128f { every 32f -> pulse offset 4f { every 8f { image primary zoom (curve 0 -> 1 over pulse) } } }');
  console.log('PASS '+checks+' real product lint cases: scalar, curve, easing, ancestor, expressions, windows, envelopes, effects, repeated fields.');
})().catch(e=>{console.error(e);process.exitCode=1;});
