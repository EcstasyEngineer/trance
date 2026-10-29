const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const T = require('../docs/exploration/authoring/v4-text.js');
// Read the literal production sources rather than a potentially stale browser cache.
const native = fs.readFileSync(path.join(__dirname, '../src/trance/visual/builtin_patterns_v4.cpp'), 'utf8');
const fixtures = [...native.matchAll(/case\s+(\d+):\s+return\s+R"v4\(([\s\S]*?)\)v4";/g)].map(m => JSON.parse(m[2]));
assert.equal(fixtures.length, 8);
const goldens = require('./fixtures/v4-text.json');
assert.equal(goldens.length, 8);
goldens.forEach((text, index) => assert.deepEqual(T.parse(text), fixtures[index], 'literal text fixture ' + (index + 1)));
for (const fixture of fixtures) {
  const before = JSON.stringify(fixture), text = T.format(fixture);
  assert.deepEqual(T.parse(text), fixture, fixture.root.id);
  assert.equal(T.format(T.parse(text)), text);
  assert.equal(JSON.stringify(fixture), before, 'format mutated its input');
}
const valid = [
  'v4; parallel for 32 { spiral speed 1e-7 -> -2.5E+3; }',
  'v4; parallel for 32 loop_subpatterns true { parallel for 2 { word primary chance 1/3; } }',
  'v4; sequence { parallel for 2 { image either; } }',
  'v4; parallel loop_subpatterns false { spiral; }',
  '\ufeffv4; parallel { spiral speed .1; }',
];
for (const source of valid) assert.equal(T.format(T.parse(T.format(T.parse(source)))), T.format(T.parse(source)));
const invalid = [
  '', 'v3; parallel {}', 'v4; parallel {} trailing', 'v4; parallel {', 'v4;\u00a0parallel {}',
  'v4; parallel for 3 for 4 {}', 'v4; parallel id a id b {}',
  'v4; parallel every 1e999 {}', 'v4; parallel for 1.2 {}',
  'v4; parallel for 2garbage {}', 'v4; parallel { spiral speed 1e+; }',
  'v4; parallel { spiral speed 1.2.3; }', 'v4; parallel { spiral speed 0x10; }',
  'v4; parallel { word primary chance 1/0; }', 'v4; parallel { word primary chance 2; }',
  'v4; parallel { image primary flip 0.5; }', 'v4; parallel { caption secondary; }',
  'v4; parallel { image either opacity 0.5 envelope(1,2,3); }',
  'v4; parallel { image either zoom 0.5; }', 'v4; parallel { image either zoom sum(0 -> 1, 0 -> 1, 0 -> 1); }',
  'v4; parallel { image either opacity 0 -> 1; }',
  'v4; parallel { image either zoom 0 -> 1 zoom 0 -> 2; }',
  'v4; parallel { word primary selection_probability 0.5; }',
  'v4; parallel { line primary; }', 'v4; parallel { caption primary opacity 0.5; }',
  'v4; cycle loop_subpatterns false {}', 'v4; sequence { image either; }',
  'v4; parallel { exit 1/3 every 8: parallel {} }',
  'v4; parallel child_mode loop {}', 'v4; parallel child_mode independent {}',
  'v4; parallel for sample(64,128,8) {}', 'v4; parallel loop_subpatterns 1 {}',
  'v4; parallel loop_subpatterns true loop_subpatterns false {}',
  'v4; parallel every ramp(8,2,4,late) {}', 'v4; parallel repeat forever {}',
];
for (const source of invalid) assert.throws(() => T.parse(source), /line \d+, column \d+/, source);
const unsupported = [
  d => d.name = 'invented',
  d => d.root.weight = 2,
  d => d.root.child_mode = 'loop',
  d => d.root.duration = {min:64,max:128,step:8},
  d => d.root.loop_subpatterns = 'true',
  d => d.root.children[0].weight = 2,
  d => d.root.children[0].node.children[0].node.zoom = {curve: {from: 0, to: 1, ease: 'early'}},
  d => d.root.children[0].node.children[0].node.zoom = {sum: [{curve: {from: 0, to: 1}}]},
  d => d.root.children[0].node.children[0].node.zoom = 0.2,
];
for (const mutate of unsupported) {
  const doc = structuredClone(fixtures[0]); mutate(doc); const before = JSON.stringify(doc);
  assert.throws(() => T.format(doc), /Unsupported V4 text AST/);
  assert.equal(JSON.stringify(doc), before);
}
const cycle = structuredClone(fixtures[7]); cycle.root.children[0].node.select.order = 'random';
assert.throws(() => T.format(cycle), /cycle selector/);
assert.equal(T.parse('v4;\nparallel {\n spiral speed 1e-7;\n}').root.children[0].node.speed, 1e-7);
assert.throws(() => T.parse('v4;\nparallel {\n spiral unknown 2;\n}'), /line 3, column/);
assert.throws(() => T.parse(' '.repeat(4 * 1024 * 1024 + 1)), /4 MiB/);
assert.throws(() => T.parse('v4; ' + 'parallel { '.repeat(66) + '}'.repeat(66)), /depth/);
assert.throws(() => T.parse('v4; parallel { ' + 'spiral; '.repeat(4097) + '}'), /children count/);
let deep = {kind: 'parallel', children: []};
for (let i = 0; i < 66; i++) deep = {kind: 'parallel', children: [{node: deep}]};
assert.throws(() => T.format({version: 4, root: deep}), /depth/);
assert.throws(() => T.format({version: 4, root: {kind: 'parallel', children: Array.from({length: 4097}, () => ({node: {op: 'spiral'}}))}}), /children count/);
const recursive = {kind: 'parallel', children: []}; recursive.children.push({node: recursive});
assert.throws(() => T.format({version: 4, root: recursive}), /depth/);
console.log('PASS: all 8 native fixtures exact AST/text roundtrip; stable format; finite exponents; strict errors; unsupported AST preserves input.');
