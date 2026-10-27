const assert=require('node:assert/strict');
const G=require('../docs/exploration/authoring/v4-grammar.js');
const doc={version:4,root:{kind:'parallel',id:'outer',children:[{node:{kind:'parallel',id:'lane',duration:64,children:[{node:{kind:'parallel',id:'inner',duration:32,children:[{node:{op:'image',source:'primary',zoom:{sum:[{curve:{from:0,to:1,scope:'lane'}},{curve:{from:0,to:1,scope:'outer'}},{curve:{from:0,to:1,scope:'inner'}}]}}}]}}]}},{node:{kind:'parallel',id:'lane_2',children:[]}}]}};
const copy=G.cloneForInsert(doc,doc.root.children[0]);
assert.equal(copy.node.id,'lane_3');assert.equal(copy.node.children[0].node.id,'inner_2');
assert.deepEqual(copy.node.children[0].node.children[0].node.zoom.sum.map(v=>v.curve.scope),['lane_3','outer','inner_2']);
assert.equal(doc.root.children[0].node.id,'lane');
assert.deepEqual(G.ancestorIds(doc,[0,0,0]),['outer','lane','inner']);
assert.deepEqual(G.ancestorIds(doc,[1]),['outer','lane_2']);
assert.equal(G.uniqueId(doc,'lane'),'lane_3');
assert.deepEqual(G.properties({op:'spiral'}),['speed']);
for(const op of ['word','line'])assert.deepEqual(G.properties({op}),['source','visibility','selection_probability']);
for(const op of ['caption','subtext']){assert.ok(G.properties({op}).includes('opacity'));assert.ok(!G.properties({op,opacity:1}).includes('envelope'));assert.ok(!G.properties({op,envelope:{}}).includes('opacity'));assert.ok(!G.properties({op}).includes('zoom'));}
assert.ok(!G.properties({op:'image',source:'primary'}).includes('flip_probability'));
assert.ok(G.properties({op:'image',source:'alternate'}).includes('flip_probability'));
assert.ok(!G.properties({op:'image'}).includes('speed'));
assert.ok(!G.properties({op:'image',opacity:1}).includes('envelope'));
assert.ok(!G.properties({op:'image',envelope:{}}).includes('opacity'));
console.log('PASS: duplicate IDs and internal/external curve references; actual ancestor scopes; native output property capabilities.');
