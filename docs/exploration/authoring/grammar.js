/* Lossless authoring CST, not a replacement for pattern_parser_v3.cpp.
 * Syntax is structural here; --lint remains the semantic authority. */
(function (root) {
  'use strict';
  let nextId = 1;
  const statements = new Set(['pattern','every','burst','base','enter','look','image','word','line','caption','subtext','anim','spiral','warp','drunk','audio']);
  const paramNames = ['zoom','origin','alpha','brightness','fade','show','env'];
  function tokens(source) {
    const out = []; let p = 0;
    while (p < source.length) {
      if (/\s/.test(source[p])) {p++; continue;}
      if (source[p] === '#') {while (p < source.length && source[p] !== '\n') p++; continue;}
      const start = p;
      if (source[p] === '[') {p++; while (p < source.length && source[p] !== ']') p++; if (p < source.length) p++;}
      else {
        const m = /^(?:->|\.\.|-?(?:\d+(?:\.(?!\.)\d*)?|\.\d+)|[A-Za-z_][A-Za-z_0-9]*)/.exec(source.slice(p));
        p += m ? m[0].length : 1;
      }
      out.push({text:source.slice(start,p),start,end:p});
    }
    return out;
  }
  function parse(source, options = {}) {
    source = String(source); const ts = tokens(source); let i = 0; const errors = [];
    const at = () => ts[i] ? ts[i].text : '';
    const loc = p => {const a=source.slice(0,p).split('\n'); return {line:a.length,column:a[a.length-1].length+1};};
    function take(expected) {const t=ts[i]; if(!t || (expected && t.text!==expected)) throw new Error('Expected '+(expected||'value')+', found '+(at()||'end of source')); i++;return t;}
    function optional(s) {if(at()===s){i++;return true;}return false;}
    function number(){const t=take();if(!/^-?(?:\d+(?:\.\d*)?|\.\d+)$/.test(t.text))throw new Error('Expected number');}
    function name(){const t=take();if(!/^[A-Za-z_][A-Za-z_0-9]*$/.test(t.text))throw new Error('Expected identifier');}
    function len(){if(optional('locked'))return;if(optional('beats')){number();return;}number();take('f');}
    function mod(){const par=optional('(');if(at().startsWith('[')){const t=take();if(!t.text.endsWith(']'))throw new Error('Unclosed expression');}else if(optional('curve')){number();take('->');number();if(optional('ease'))name();if(optional('over'))name();}else number();if(par)take(')');}
    function chance(){if(optional('chance')){const par=optional('(');number();if(par)take(')');}}
    function content(alt){const t=take();if(!['primary','secondary','either','runtime',...(alt?['alternate']:[])].includes(t.text))throw new Error('Unknown content '+t.text);if(t.text==='alternate')chance();}
    function params(){while(paramNames.includes(at())) {const k=take().text;if(['zoom','origin','alpha','brightness'].includes(k))mod();else if(k==='fade')name();else if(k==='show'){if(at().startsWith('['))take();else{number();optional('f');take('..');number();optional('f');}}else{take('in');number();optional('f');if(optional('hold')){number();optional('f');}take('out');number();optional('f');}}}
    function body(parentKind, begin){const children=[];let end=begin;while(i<ts.length && at()!=='}'){const before=i;let node;try{node=statement(parentKind,end);}catch(e){const st=ts[before].start; i=Math.max(i,before+1);while(i<ts.length && at()!=='}' && !statements.has(at()))i++;const en=i<ts.length?ts[i].start:source.length;node={id:'n'+nextId++,kind:'unknown',header:source.slice(st,en),leading:source.slice(end,st),start:st,end:en,location:loc(st),source:source.slice(st,en)};errors.push({...loc(st),message:e.message});}children.push(node);end=node.end;if(i===before)i++;}return {children,tail:source.slice(end,i<ts.length?ts[i].start:source.length)};}
    function statement(parentKind, previousEnd){const start=ts[i].start;let kind=take().text;let container=false;
      if(!statements.has(kind))throw new Error('Unknown statement '+kind);
      if(kind==='pattern'){name();take('for');len();while(['seq','loop'].includes(at())){if(optional('seq'))continue;take('loop');number();}container=true;}
      else if(kind==='every'){if(optional('ramp')){kind='ramp';number();take('f');take('->');number();take('f');take('steps');number();if(optional('ease'))name();if(optional('->'))name();}else{len();if(optional('->'))name();if(optional('offset'))len();}container=true;}
      else if(kind==='burst'){if(optional('->'))name();if(parentKind==='burst'){kind='burst-phase';}else{while(['period','chance','cooldown','duration'].includes(at())){const k=take().text;if(k==='chance'){number();take('/');number();}else{number();take('f');if(k==='duration'&&optional('..')){number();take('f');}}}}container=true;}
      else if(['base','enter','look'].includes(kind)){if(kind==='base'&&optional('->'))name();container=true;}
      else if(kind==='audio'){if(!optional('stop')){content(false);optional('loop');if(optional('volume'))mod();}}
      else if(kind==='anim')content(true);
      else if(kind==='spiral'){if(parentKind==='look'){kind='spiral-settings';while(['type','width'].includes(at())){take();take('=');number();}}else if(optional('speed'))mod();}
      else if(kind==='warp'){while(['amplitude','wavelength','speed'].includes(at())){take();mod();}}
      else if(kind==='drunk')mod();
      else{content(kind==='image');params();if(kind==='image'&&at()==='anim' && !['primary','secondary','either','runtime','alternate'].includes(ts[i+1]?.text)){take();if(kind==='image'&&at()==='every'&& /^\d+$/.test(ts[i+1]?.text||'') && ts[i+2]?.text!=='f'){take();number();if(['st','nd','rd','th'].includes(at()))take();}}chance();}
      let header,children,tail,end;
      if(container){const open=take('{');header=source.slice(start,open.start);const b=body(kind,open.end);children=b.children;tail=b.tail;end=take('}').end;}
      else{end=ts[i-1].end;header=source.slice(start,end);}
      return {id:'n'+nextId++,kind,header,...(container?{children,tail}:{}),leading:source.slice(previousEnd,start),start,end,location:loc(start),source:source.slice(start,end),sourcePath:options.sourcePath||null};
    }
    const b=body('document',0);if(i<ts.length){errors.push({...loc(ts[i].start),message:'Unexpected closing brace'});b.tail+=source.slice(ts[i].start);}
    const d={kind:'document',children:b.children,tail:b.tail,errors,diagnostics:errors};d.nodes=d.children;return d;
  }
  function serialize(node){if(node.kind==='document')return node.children.map(serialize).join('')+(node.tail||'');return (node.leading||'')+node.header+(node.children?'{' + node.children.map(serialize).join('')+(node.tail||'')+'}':'');}
  function walk(node,fn){fn(node);(node.children||[]).forEach(n=>walk(n,fn));}
  const palette = [
    ['pattern','Parallel pattern','pattern scene for 128f {\n}'],
    ['sequential-pattern','Sequential pattern','pattern scene for 128f seq {\n}'],
    ['every','Repeat cadence','every 64f {\n}'],['ramp','Accelerating / slowing cadence','every ramp 56f -> 12f steps 8 ease early -> cut {\n}'],
    ['burst','Burst controller','burst period 8f chance 1/12 cooldown 64f duration 64f..128f {\n  base {\n  }\n  burst {\n  }\n}'],
    ['base','Default phase','base {\n}'],['burst-phase','Interrupt phase','burst {\n}'],
    ['image','Image layer','image either zoom 0'],
    ['word','Word','word primary'],['line','Complete phrase','line primary'],['caption','Caption','caption primary'],['subtext','Subtext','subtext secondary'],
    ['anim','Select animation','anim either'],['spiral','Draw spiral','spiral speed 3'],['look','Appearance settings','look {\n  spiral type=1 width=20\n}'],
    ['spiral-settings','Spiral shape settings','spiral type=1 width=20'],['warp','Wave displacement','warp amplitude 0.1 wavelength 0.15 speed 2'],['drunk','Displacement shorthand','drunk 0.1'],
    ['audio','Theme audio','audio primary loop volume 0.5'],['audio-stop','Stop theme audio','audio stop']
  ].map(([kind,label,source])=>({kind,label,source}));
  // Field spans reference only the current header. Re-tokenize after every edit,
  // keeping unrelated whitespace, comments and repeated clauses byte-for-byte.
  function fields(node){const h=node.header||'',ts=tokens(h),f=[];let used={};
    const add=(key,label,a,b,type='text',options)=>{const n=used[key]||0;used[key]=n+1;f.push({key:key+(n?':'+n:''),label,start:a,end:b,value:h.slice(a,b),type,...(options?{options}:{})});};
    const range=(key,label,a,b=a,type='text',options)=>{if(ts[a]&&ts[b])add(key,label,ts[a].start,ts[b].end,type,options);};
    const lenEnd=a=>ts[a]?.text==='beats'?a+1:ts[a]?.text==='locked'?a:a+1;
    const modEnd=a=>{if(!ts[a])return a;if(ts[a].text==='('){let n=1,j=a+1;while(j<ts.length&&n){if(ts[j].text==='(')n++;if(ts[j].text===')')n--;j++;}return j-1;}if(ts[a].text==='curve'){let j=a+3;if(ts[j+1]?.text==='ease')j+=2;if(ts[j+1]?.text==='over')j+=2;return j;}return a;};
    if(node.kind==='pattern'){range('name','Pattern name',1);const j=2;range('length','Duration (Nf, beats N, locked)',j+1,lenEnd(j+1));add('sequence','Run child schedules sequentially',h.length,h.length,'boolean');f[f.length-1].value=ts.some((t,j)=>j>3&&t.text==='seq')?'true':'false';const l=ts.findIndex((t,j)=>j>3&&t.text==='loop');if(l>=0)range('loop','Iteration count',l+1);else add('loop','Iteration count',h.length,h.length);}
    if(node.kind==='every')range('length','Cadence duration',1,lenEnd(1));
    if(node.kind==='ramp'){range('rampStart','Ramp start length',2,3);range('rampEnd','Ramp end length',5,6);}
    if(['image','word','line','caption','subtext','anim','audio'].includes(node.kind)&&ts[1]?.text!=='stop')range('content','Theme content',1,1,'select',['primary','secondary','either',...(['image','anim'].includes(node.kind)?['alternate']:[])]);
    if(node.kind==='drunk')range('intensity','Displacement amplitude',1,modEnd(1));
    let scanStart=1;
    if(node.kind==='pattern')scanStart=lenEnd(3)+1;
    else if(node.kind==='every')scanStart=lenEnd(1)+1;
    else if(node.kind==='ramp')scanStart=7;
    else if(['image','word','line','caption','subtext','anim','audio'].includes(node.kind))scanStart=2;
    else if(['drunk'].includes(node.kind))scanStart=ts.length;
    for(let j=scanStart;j<ts.length;j++){const k=ts[j].text;
      if(['zoom','origin','alpha','brightness','amplitude','wavelength','speed','volume'].includes(k)){const e=modEnd(j+1);range(k,k,j+1,e);j=e;}
      else if(k==='show'){let e=j+1;if(!ts[e]?.text.startsWith('[')){if(ts[e+1]?.text==='f')e++;e+=2;if(ts[e+1]?.text==='f')e++;}range('show','Visibility window / [expression]',j+1,e);j=e;}
      else if(k==='env'){let e=j+1;while(e+1<ts.length&&!paramNames.includes(ts[e+1].text)&&!['chance','anim'].includes(ts[e+1].text))e++;range('env','Envelope: in X hold Y out Z',j+1,e);j=e;}
      else if(k==='fade')range('fade','Fade shape',j+1,j+1,'select',['in','out','inout']);
      else if(k==='->' && node.kind!=='image'&& !(node.kind==='ramp'&&j===4)){let e=j+1;if(ts[e+1]?.text==='.')e+=2;range('target','Clock name',j+1,e);j=e;}
      else if(['steps','ease','chance','period','cooldown','duration','offset'].includes(k)){let e=j+1;if(k==='chance'&&ts[e]?.text==='('){e+=2;}else if(k==='chance'&&ts[e+1]?.text==='/')e+=2;else if(['period','cooldown','duration','offset'].includes(k)){e=lenEnd(e);if(k==='duration'&&ts[e+1]?.text==='..')e+=3;}const alt=k==='chance'&&ts[1]?.text==='alternate'&&j===2;range(alt?'alternateChance':k,alt?'Theme alternation probability':k==='chance'?(node.kind==='burst'?'Interrupt odds (1/N)':'Selection probability'):k,j+1,e);j=e;}
      else if(k==='anim'){let e=j;if(ts[j+1]?.text==='every'){e=j+2;if(['st','nd','rd','th'].includes(ts[e+1]?.text))e++;}range('animation','Animation mode (anim / anim every Nth)',j,e);j=e;}
      else if(['type','width'].includes(k)&&ts[j+1]?.text==='=')range(k,'Spiral '+k,j+2);
    }
    if(node.kind==='audio'&&ts[1]?.text!=='stop'){add('audioLoop','Loop theme audio',h.length,h.length,'boolean');f[f.length-1].value=ts.some(t=>t.text==='loop')?'true':'false';}
    const ensure=(key,label,options)=>{if(!f.some(x=>x.key===key))add(key,label,h.length,h.length,options?'select':'text',options?['',...options]:undefined);};
    if(['image','word','line','caption','subtext'].includes(node.kind)){
      ['zoom','origin','alpha','brightness'].forEach(k=>ensure(k,k+' (constant, curve, or [expression])'));
      ensure('fade','Fade shape',['in','out','inout']);ensure('show','Visibility (fraction/frame range or [expression])');ensure('env','Envelope (in X hold Y out Z)');
    }
    if(['image'].includes(node.kind))ensure('animation','Animation (anim / anim every Nth)');
    if(['image','word','line','caption','subtext'].includes(node.kind))ensure('chance','Selection probability');
    if(['image','anim'].includes(node.kind)&&ts[1]?.text==='alternate')ensure('alternateChance','Theme alternation probability');
    if(['every','ramp','burst','base','burst-phase'].includes(node.kind))ensure('target','Clock name');
    if(node.kind==='every')ensure('offset','Cadence offset (Nf, beats N, locked)');
    if(node.kind==='ramp')ensure('ease','Ramp easing',['linear','late','early']);
    if(node.kind==='burst'){ensure('chance','Interrupt odds (1/N)');ensure('cooldown','Cooldown frames');ensure('duration','Hold duration (Nf or Nf..Nf)');}
    if(node.kind==='spiral')ensure('speed','Spiral speed modulator');
    if(node.kind==='spiral-settings'){ensure('type','Spiral type');ensure('width','Spiral width');}
    if(node.kind==='warp')['amplitude','wavelength','speed'].forEach(k=>ensure(k,k+' modulator'));
    if(node.kind==='audio'&&ts[1]?.text!=='stop')ensure('volume','Volume modulator');
    return f;
  }
  function setField(node,key,value){value=String(value);let f=fields(node).find(v=>v.key===key);if(!f)throw new Error('Unknown field '+key);
    if(key==='content'&&f.value==='alternate'&&value!=='alternate'){
      const alt=fields(node).find(x=>x.key==='alternateChance');if(alt&&alt.start!==alt.end){const ts=tokens(node.header),j=ts.findIndex(t=>t.start===alt.start);node.header=node.header.slice(0,ts[j-1].start)+node.header.slice(alt.end);}
    }
    if(key==='sequence'){const t=tokens(node.header).find((t,j)=>j>3&&t.text==='seq');if(t)node.header=node.header.slice(0,t.start)+node.header.slice(t.end);if(value==='true'){const ts=tokens(node.header),p=ts[ts.length-1].end;node.header=node.header.slice(0,p)+' seq '+node.header.slice(p);}return;}
    if(key==='audioLoop'){const ts=tokens(node.header),t=ts.find((t,j)=>j>1&&t.text==='loop');if(t)node.header=node.header.slice(0,t.start)+node.header.slice(t.end);if(value==='true'){const p=tokens(node.header)[1].end;node.header=node.header.slice(0,p)+' loop'+node.header.slice(p);}return;}
    if(key==='loop'&&f.start===f.end){if(value.trim()){const ts=tokens(node.header),p=ts[ts.length-1].end;node.header=node.header.slice(0,p)+' loop '+value+' '+node.header.slice(p);}return;}
    if(f.start===f.end){if(!value.trim())return;let clause;
      if(key==='target')clause='-> '+value;
      else if(key==='animation')clause=value;
      else if(['type','width'].includes(key))clause=key+'='+value;
      else clause=(key==='alternateChance'?'chance':key)+' '+value;
      // Parameters must precede image animation / final chance; destination must
      // follow content and its optional alternate chance.
      const ts=tokens(node.header);let p=ts[ts.length-1]?.end||node.header.length;
      if(key==='alternateChance')p=ts[1].end;
      else if(['image','word','line','caption','subtext'].includes(node.kind)&&key!=='chance'){
        const t=ts.find((x,index)=>index>1&&(x.text==='anim'||(x.text==='chance'&&!(ts[1]?.text==='alternate'&&index===2))));if(t)p=t.start;
      }
      else if(key==='target'&&['every','ramp'].includes(node.kind)){const t=ts.find(x=>x.text==='offset');if(t)p=t.start;}
      else if(key==='target'&&node.kind==='burst')p=ts[0].end;
      else if(key==='ease'&&node.kind==='ramp'){const t=ts.find((x,j)=>j>4&&x.text==='->');if(t)p=t.start;}
      node.header=node.header.slice(0,p)+' '+clause+' '+node.header.slice(p);return;
    }
    // Clearing an optional field removes its keyword as well as its operand.
    if(!value.trim()&& !['name','length','content','intensity','rampStart','rampEnd','steps','period'].includes(key)){
      const ts=tokens(node.header);const ix=ts.findIndex(t=>t.start===f.start);let begin=f.start;
      if(key==='animation')begin=f.start;else if(['type','width'].includes(key)&&ix>1)begin=ts[ix-2].start;else if(ix>0)begin=ts[ix-1].start;
      node.header=node.header.slice(0,begin)+node.header.slice(f.end);return;
    }
    node.header=node.header.slice(0,f.start)+value+node.header.slice(f.end);
  }
  function create(kind){const d=palette.find(x=>x.kind===kind);if(!d)throw new Error('Unknown palette kind '+kind);let n;if(kind==='burst-phase')n=parse('pattern wrapper for 128f {burst period 8f {'+d.source+'}}').children[0].children[0].children[0];else if(kind==='spiral-settings')n=parse('look {'+d.source+'}').children[0].children[0];else n=parse(d.source).children[0];n.leading='\n  ';return n;}
  const api={parse,serialize,walk,palette,fields,setField,create,tokens};
  if(typeof module!=='undefined'&&module.exports)module.exports=api;root.TranceGrammar=api;
})(typeof globalThis!=='undefined'?globalThis:this);
