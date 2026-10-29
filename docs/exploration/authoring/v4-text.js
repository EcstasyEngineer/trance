/* Minimal V4 text frontend. Produces authored AST only; native validation owns timing. */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.TranceV4Text = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';
  const own = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
  const groups = ['parallel', 'sequence', 'cycle'];
  const properties = {image: ['zoom', 'baseline', 'opacity', 'envelope', 'animation', 'flip'],
    word: ['visible', 'chance'], caption: [], subtext: [], spiral: ['speed']};
  const aliases = {baseline: 'zoom_baseline', flip: 'flip_probability', chance: 'selection_probability', visible: 'visibility'};
  const SOURCE_LIMIT = 4 * 1024 * 1024, DEPTH_LIMIT = 64, CHILD_LIMIT = 4096;
  function parse(source) {
    source = String(source);
    if (source.length > SOURCE_LIMIT || new TextEncoder().encode(source).length > SOURCE_LIMIT)
      throw new SyntaxError('Source exceeds 4 MiB at line 1, column 1');
    source = source.replace(/^\uFEFF/, '');
    let pos = 0, token;
    function error(message) {
      const offset = token ? token.start : pos;
      const prefix = source.slice(0, offset), lines = prefix.split('\n');
      throw new SyntaxError(message + ' at line ' + lines.length + ', column ' + (lines[lines.length - 1].length + 1));
    }
    function next() {
      while (/[ \t\r\n\f\v]/.test(source[pos] || '') && pos < source.length) pos++;
      const start = pos, tail = source.slice(pos);
      const match = /^(?:->|[A-Za-z_][A-Za-z_0-9]*|[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?|[{}();,:@/])/.exec(tail);
      if (!tail) token = {value: '', start};
      else if (match) { pos += match[0].length; token = {value: match[0], start}; }
      else { token = {value: tail[0], start}; error('Unexpected character ' + tail[0]); }
    }
    function take(value) { if (token.value !== value) error('Expected ' + value); next(); }
    function identifier() {
      if (!/^[A-Za-z_][A-Za-z_0-9]*$/.test(token.value)) error('Expected identifier');
      const value = token.value; next(); return value;
    }
    function number(integer = false, minimum = -Infinity) {
      if (!/^[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?$/.test(token.value)) error('Expected number');
      const value = Number(token.value);
      if (!Number.isFinite(value) || (integer && (!Number.isInteger(value) || value > 2147483647)) || value < minimum)
        error(integer ? 'Expected bounded integer' : 'Expected finite number');
      next(); return value;
    }
    function probability() {
      const first = number(false, 0);
      if (token.value === '/') {
        if (!Number.isInteger(first) || first > 2147483647) error('Expected integer numerator');
        next(); const denominator = number(true, 1);
        if (first > denominator) error('Probability exceeds one');
        return {numerator: first, denominator};
      }
      if (first > 1) error('Probability exceeds one');
      return first;
    }
    function tuple(count, integer = false) {
      take('('); const values = [];
      for (let i = 0; i < count; i++) { if (i) take(','); values.push(number(integer, integer ? 0 : -Infinity)); }
      take(')'); return values;
    }
    function curve() {
      const from = number(); take('->'); const to = number();
      let scope = 'local'; if (token.value === '@') { next(); scope = identifier(); }
      return {curve: {from, to, scope}};
    }
    function scalar(property) {
      if (token.value === 'sum' && property === 'zoom') {
        next(); take('('); const first = curve(); take(','); const second = curve(); take(')');
        return {sum: [first, second]};
      }
      if (token.value === 'triangle' && property === 'opacity') {
        next(); const [from, peak, to] = tuple(3); return {triangle: {from, peak, to}};
      }
      const from = number();
      if (token.value !== '->') {
        if (property === 'zoom' || property === 'baseline') error('Expected linear curve');
        return from;
      }
      if (property === 'opacity') error('Opacity supports a constant or triangle');
      next(); const to = number(); let scope = 'local';
      if (token.value === '@') { next(); scope = identifier(); }
      return {curve: {from, to, scope}};
    }
    function composition(depth = 0) {
      if (depth >= DEPTH_LIMIT) error('Maximum composition depth is 64');
      const kind = identifier(); if (!groups.includes(kind)) error('Expected composition');
      const node = {kind: {parallel: 'parallel', sequence: 'sequential', cycle: 'random'}[kind], children: []};
      if (kind === 'cycle') node.select = {order: 'round_robin', allow_self: false, first: 0};
      const seen = new Set();
      while (token.value !== '{') {
        const clause = identifier(); if (seen.has(clause)) error('Duplicate clause ' + clause); seen.add(clause);
        if (clause === 'id') node.id = identifier();
        else if (clause === 'for') {if(token.value==='inherit'){node.duration='inherit';next();}else node.duration = number(true, 1);} else if (clause === 'repeat') node.repeat = number(true, 1);
        else if (clause === 'phase') node.offset = number(true, 0);
        else if (clause === 'every') {
          if (token.value === 'ramp') {
            next(); take('('); const from = number(true, 1); take(','); const to = number(true, 1);
            take(','); const steps = number(true, 1); take(','); take('early'); take(')');
            node.cadence = {ramp: {from, to, steps, ease: 'early'}};
          } else node.cadence = number(true, 1);
        } else if (clause === 'cooldown' && kind === 'cycle') node.select.cooldown = number(true, 0);
        else if (clause === 'loop_subpatterns' && kind !== 'cycle') {
          if (!['true','false'].includes(token.value)) error('Loop subpatterns requires true or false');
          node.loop_subpatterns = token.value === 'true'; next();
        } else error('Unknown composition clause ' + clause);
      }
      next();
      while (token.value !== '}') {
        if (!token.value) error('Expected }');
        if (node.children.length >= CHILD_LIMIT) error('Maximum children count is 4096');
        const edge = {};
        if (token.value === 'exit') {
          if (kind !== 'cycle') error('Exit requires a cycle edge');
          next(); const p = probability(); take('every'); const check_every = number(true, 1); take(':');
          edge.exit = {probability: p, check_every};
        }
        if (groups.includes(token.value)) edge.node = composition(depth + 1);
        else {
          if (kind !== 'parallel' || edge.exit) error('This edge requires a composition');
          edge.node = leaf();
        }
        node.children.push(edge);
      }
      next(); return node;
    }
    function leaf() {
      const op = identifier(); if (!own(properties, op)) error('Unknown output ' + op);
      const node = {op};
      if (op !== 'spiral') {
        node.source = identifier();
        const sources = {image: ['primary', 'secondary', 'either', 'alternate'], word: ['primary', 'secondary', 'either'],
          caption: ['primary', 'either'], subtext: ['secondary']};
        if (!sources[op].includes(node.source)) error('Unsupported source for ' + op);
      }
      while (token.value !== ';') {
        const property = identifier();
        if (!properties[op].includes(property)) error('Unsupported ' + op + ' property ' + property);
        const key = aliases[property] || property;
        if (own(node, key)) error('Duplicate property ' + property);
        if (property === 'animation') node.animation = {every: number(true, 1)};
        else if (property === 'envelope') { const [a, b, c] = tuple(3, true); node.envelope = {in: a, hold: b, out: c}; }
        else if (property === 'visible') node.visibility = tuple(2);
        else if (property === 'flip' || property === 'chance') node[key] = probability();
        else node[key] = scalar(property);
      }
      if (own(node, 'opacity') && own(node, 'envelope')) error('Opacity and envelope are exclusive');
      if (own(node, 'flip_probability') && node.source !== 'alternate') error('Flip requires alternate source');
      next(); return node;
    }
    next(); take('v4'); take(';'); const root = composition();
    if (token.value) error('Unexpected trailing input');
    return {version: 4, root};
  }
  function format(document) {
    let formattedBytes = 4;
    function fail(message) { throw new Error('Unsupported V4 text AST: ' + message); }
    function budget(text) {
      formattedBytes += text.length;
      if (formattedBytes > SOURCE_LIMIT) fail('formatted source exceeds 4 MiB');
      return text;
    }
    function shape(value, required, optional = []) {
      if (!value || typeof value !== 'object' || Array.isArray(value)) fail('expected object');
      for (const key of required) if (!own(value, key)) fail('missing ' + key);
      for (const key of Object.keys(value)) if (![...required, ...optional].includes(key)) fail('field ' + key);
    }
    function num(value, integer = false, min = -Infinity) {
      if (typeof value !== 'number' || !Number.isFinite(value) || value < min ||
        (integer && (!Number.isInteger(value) || value > 2147483647))) fail('invalid number');
      return Object.is(value, -0) ? '-0' : String(value);
    }
    function id(value) { if (typeof value !== 'string' || !/^[A-Za-z_][A-Za-z_0-9]*$/.test(value)) fail('invalid identifier'); return value; }
    function probability(value) {
      if (typeof value === 'number') { if (value > 1) fail('probability exceeds one'); return num(value, false, 0); }
      shape(value, ['numerator', 'denominator']);
      const a = num(value.numerator, true, 0), b = num(value.denominator, true, 1);
      if (value.numerator > value.denominator) fail('probability exceeds one');
      return a + '/' + b;
    }
    function curve(value) {
      shape(value, ['curve']); shape(value.curve, ['from', 'to'], ['scope']);
      const c = value.curve, scope = own(c, 'scope') ? id(c.scope) : 'local';
      return num(c.from) + ' -> ' + num(c.to) + (scope === 'local' ? '' : ' @' + scope);
    }
    function scalar(value, property) {
      if (typeof value === 'number') {
        if (property === 'zoom' || property === 'baseline') fail('expected linear curve');
        return num(value);
      }
      if (!value || typeof value !== 'object' || Array.isArray(value)) fail('expected scalar value');
      if (property === 'zoom' && own(value, 'sum')) {
        shape(value, ['sum']); if (!Array.isArray(value.sum) || value.sum.length !== 2) fail('sum requires two curves');
        return 'sum(' + value.sum.map(curve).join(', ') + ')';
      }
      if (property === 'opacity') {
        shape(value, ['triangle']); shape(value.triangle, ['from', 'peak', 'to']);
        return 'triangle(' + ['from', 'peak', 'to'].map(k => num(value.triangle[k])).join(', ') + ')';
      }
      return curve(value);
    }
    function composition(node, depth, prefix = '') {
      if (depth >= DEPTH_LIMIT) fail('maximum composition depth is 64');
      shape(node, ['kind', 'children'], ['id', 'duration', 'repeat', 'cadence', 'offset', 'select', 'loop_subpatterns']);
      const kinds = {parallel: 'parallel', sequential: 'sequence', random: 'cycle'};
      const kind = own(kinds, node.kind) ? kinds[node.kind] : null;
      if (!kind) fail('composition kind');
      if (!Array.isArray(node.children)) fail('children must be an array');
      if (node.children.length > CHILD_LIMIT) fail('maximum children count is 4096');
      let header = '  '.repeat(depth) + prefix + kind;
      if (own(node, 'id')) header += ' id ' + id(node.id);
      if (own(node, 'duration')) header += ' for ' + (node.duration==='inherit'?'inherit':num(node.duration, true, 1));
      if (own(node, 'repeat')) header += ' repeat ' + num(node.repeat, true, 1);
      if (own(node, 'cadence')) {
        if (typeof node.cadence === 'number') header += ' every ' + num(node.cadence, true, 1);
        else {
          shape(node.cadence, ['ramp']); const r = node.cadence.ramp; shape(r, ['from', 'to', 'steps', 'ease']);
          if (r.ease !== 'early') fail('cadence easing');
          header += ' every ramp(' + ['from', 'to', 'steps'].map(k => num(r[k], true, 1)).join(', ') + ', early)';
        }
      }
      if (own(node, 'offset')) header += ' phase ' + num(node.offset, true, 0);
      if (kind === 'cycle') {
        shape(node.select, ['order', 'allow_self', 'first'], ['cooldown']);
        if (node.select.order !== 'round_robin' || node.select.allow_self !== false || node.select.first !== 0) fail('cycle selector');
        if (own(node.select, 'cooldown')) header += ' cooldown ' + num(node.select.cooldown, true, 0);
        if (own(node, 'loop_subpatterns')) fail('cycle loop_subpatterns');
      } else if (own(node, 'select')) fail('selector on non-cycle');
      if (own(node, 'loop_subpatterns')) {
        if (typeof node.loop_subpatterns !== 'boolean') fail('loop_subpatterns needs true or false');
        header += ' loop_subpatterns ' + node.loop_subpatterns;
      }
      const lines = [budget(header + ' {\n').slice(0, -1)];
      for (const edge of node.children) {
        shape(edge, ['node'], ['exit']); let p = '';
        if (own(edge, 'exit')) {
          if (kind !== 'cycle' || !own(edge.node, 'kind')) fail('exit edge');
          shape(edge.exit, ['probability', 'check_every']);
          p = 'exit ' + probability(edge.exit.probability) + ' every ' + num(edge.exit.check_every, true, 1) + ': ';
        }
        if (own(edge.node, 'kind')) lines.push(composition(edge.node, depth + 1, p));
        else {
          if (kind !== 'parallel') fail('output on non-parallel');
          lines.push(budget('  '.repeat(depth + 1) + leaf(edge.node) + '\n').slice(0, -1));
        }
      }
      lines.push(budget('  '.repeat(depth) + '}\n').slice(0, -1)); return lines.join('\n');
    }
    function leaf(node) {
      if (!node || !own(properties, node.op)) fail('output');
      const allowed = properties[node.op].map(k => aliases[k] || k);
      shape(node, node.op === 'spiral' ? ['op'] : ['op', 'source'], allowed);
      let text = node.op;
      if (node.op !== 'spiral') {
        const sources = {image: ['primary', 'secondary', 'either', 'alternate'], word: ['primary', 'secondary', 'either'], caption: ['primary', 'either'], subtext: ['secondary']};
        if (!sources[node.op].includes(node.source)) fail('output source'); text += ' ' + node.source;
      }
      if (own(node, 'opacity') && own(node, 'envelope')) fail('opacity and envelope');
      if (own(node, 'flip_probability') && node.source !== 'alternate') fail('flip source');
      for (const property of properties[node.op]) {
        const key = aliases[property] || property; if (!own(node, key)) continue;
        const value = node[key];
        if (property === 'envelope') { shape(value, ['in', 'hold', 'out']); text += ' envelope(' + ['in', 'hold', 'out'].map(k => num(value[k], true, 0)).join(', ') + ')'; }
        else if (property === 'visible') { if (!Array.isArray(value) || value.length !== 2) fail('visibility'); text += ' visible(' + value.map(v => num(v)).join(', ') + ')'; }
        else if (property === 'animation') { shape(value, ['every']); text += ' animation ' + num(value.every, true, 1); }
        else if (property === 'chance' || property === 'flip') text += ' ' + property + ' ' + probability(value);
        else text += ' ' + property + ' ' + scalar(value, property);
      }
      return text + ';';
    }
    shape(document, ['version', 'root']); if (document.version !== 4) fail('version');
    return 'v4;\n' + composition(document.root, 0) + '\n';
  }
  return {parse, format};
});
