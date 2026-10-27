/* Authoring recipes lowered to existing v3 syntax, never new language keywords. */
(function(root) {
  'use strict';
  function ancestry(doc, target) {
    function visit(node, path) {
      if (node === target) return path.concat(node);
      for (const child of node.children || []) {
        const found = visit(child, path.concat(node));
        if (found) return found;
      }
      return null;
    }
    return visit(doc, []);
  }
  function createImageLane(G, doc, parent) {
    const path = ancestry(doc, parent);
    if (!path) throw Error('The target scope is no longer in this document.');
    if (!parent.children || ['enter', 'look'].includes(parent.kind))
      throw Error('An image lane needs a schedule container, outside entry setup and appearance settings.');
    const names = new Set();
    G.walk(doc, n => {const name = n.header?.match(/^pattern\s+([A-Za-z_][A-Za-z_0-9]*)\b/); if (name) names.add(name[1]);});
    let ordinal = 1;
    while (names.has('image_lane_' + ordinal)) ordinal++;
    let duration, note = '';
    for (const owner of path.slice().reverse()) {
      if (owner.kind === 'document') break;
      if (owner.kind === 'pattern') {
        duration = owner.header.match(/^pattern\s+\w+\s+for\s+(\d+\s*f|beats\s+\d+|locked)\b/)?.[1];
        break;
      }
      if (owner.kind === 'every') {
        duration = owner.header.match(/^every\s+(\d+\s*f|beats\s+\d+|locked)\b/)?.[1];
        break;
      }
      // A ramp segment or interruption can be shorter than its enclosing scene.
      // Do not pretend its parent's full duration is this lane's local lifespan.
      if (['ramp', 'burst', 'burst-phase', 'base'].includes(owner.kind)) break;
    }
    if (!duration) {
      duration = '128f';
      note = 'Lane duration defaults to 128f: edit it to match the intended local lifetime. Its owner can clip it.';
    }
    const name = 'image_lane_' + ordinal;
    const frames = duration.match(/^(\d+)\s*f$/);
    const cadence = frames ? Math.min(64, Number(frames[1])) + 'f' : duration;
    const node = G.parse(`pattern ${name} for ${duration} {\n  every ${cadence} {\n    image runtime zoom (curve 0 -> 0.5)\n  }\n}`).children[0];
    if (!node || node.kind !== 'pattern') throw Error('Could not create an image lane.');
    node.leading = '\n  ';
    node.authoringNote = note || 'Independent image storage; one image subject. Edit the cadence to choose how often it changes.';
    return node;
  }
  function isImageLane(G, node) {
    if (node.kind !== 'pattern' || !/^pattern\s+image_lane_\d+\b/.test(node.header || '')) return false;
    const subjects = [];
    G.walk(node, child => {
      if (['image', 'word', 'line', 'caption', 'subtext', 'spiral'].includes(child.kind)) subjects.push(child);
    });
    return subjects.length === 1 && subjects[0].kind === 'image';
  }
  const api = {createImageLane, isImageLane};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  root.TranceLayers = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
