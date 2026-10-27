'use strict';
const assert = require('node:assert/strict');
const G = require('../docs/exploration/authoring/grammar.js');
const L = require('../docs/exploration/authoring/layer-authoring.js');
function fixture(header) {return G.parse(`${header} {\n}`);}
for (const length of ['16f', '2048f', 'beats 2', 'locked']) {
  const doc = fixture(`pattern scene for ${length}`), parent = doc.children[0];
  const lane = L.createImageLane(G, doc, parent);
  parent.children.push(lane);
  assert.match(lane.header, new RegExp('for ' + length + '\\s*$'));
  assert.equal(lane.children[0].kind, 'every');
  assert.equal(lane.children[0].children.length, 1);
  assert.equal(lane.children[0].children[0].kind, 'image');
  assert.equal(L.isImageLane(G, lane), true);
  assert.equal(L.isImageLane(G, G.parse(G.serialize(lane)).children[0]), true);
  const literal = /^(\d+)f$/.exec(length);
  assert.match(lane.children[0].header, new RegExp('every ' + (literal ? Math.min(64, Number(literal[1])) + 'f' : length) + '\\s*$'));
  assert.doesNotMatch(lane.children[0].children[0].header, /^image\s+\w+\s*->/);
  assert.doesNotMatch(G.serialize(lane), /\bcopy\b|\bdraw\b/);
  assert.equal(G.parse(G.serialize(doc)).errors.length, 0);
  const second = L.createImageLane(G, doc, parent);
  assert.match(second.header, /image_lane_2/);
}
const nested = G.parse('pattern scene for 1024f { every 8f { } }');
assert.match(L.createImageLane(G, nested, nested.children[0].children[0]).header, /for 8f/);
for (const body of ['every ramp 56f -> 12f steps 8 { }', 'burst period 8f {base { } burst { } }']) {
  const doc = G.parse('pattern scene for 1024f {' + body + '}');
  const node = L.createImageLane(G, doc, doc.children[0].children[0]);
  assert.match(node.header, /for 128f/);
  assert.match(node.authoringNote, /defaults to 128f/);
}
const doc = G.parse('pattern image_lane_1 for 16f { look { } }');
assert.throws(() => L.createImageLane(G, doc, doc.children[0].children[0]), /schedule container/);
assert.throws(() => L.createImageLane(G, doc, {kind:'pattern',children:[]}), /no longer/);
assert.match(L.createImageLane(G, doc, doc.children[0]).header, /image_lane_2/);
assert.equal(L.isImageLane(G, G.parse('pattern image_lane_1 for 16f {image primary word primary}').children[0]), false);
assert.equal(L.isImageLane(G, G.parse('pattern image_lane_1 for 16f {draw cur}').children[0]), false);
assert.equal(L.isImageLane(G, G.parse('pattern ordinary for 16f {image primary}').children[0]), false);
console.log('Independent image lane recipe: duration inheritance, unique names, single subject and explicit fallback passed.');
