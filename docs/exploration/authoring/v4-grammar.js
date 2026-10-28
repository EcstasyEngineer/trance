(function (root) {
  'use strict';
  const kinds = ['parallel', 'sequential', 'random'];
  const outputs = ['image', 'word', 'line', 'caption', 'subtext', 'spiral'];
  const clone = value => JSON.parse(JSON.stringify(value));
  function create(type) {
    if (kinds.includes(type)) return {kind:type, duration:128, children:[{node:type==='parallel'?{op:'image',source:'either'}:{kind:'parallel',duration:128,children:[{node:{op:'image',source:'either'}}]}}], ...(type==='random'?{select:{order:'random',allow_self:false,first:0,cooldown:0}}:{})};
    if (!outputs.includes(type)) throw Error('Unknown piece');
    return type==='spiral'?{op:type,speed:1}:{op:type,source:'either'};
  }
  function parse(source) {
    const doc=JSON.parse(source);
    if (!doc || doc.version!==4 || !doc.root || !kinds.includes(doc.root.kind)) throw Error('Expected version 4 and a composition root.');
    function visit(n,depth=0) {
      if(depth>64)throw Error('Maximum pattern nesting depth is 64.');
      if (!n || typeof n!=='object') throw Error('A child must contain a node.');
      if (n.kind) {if (!kinds.includes(n.kind)||!Array.isArray(n.children)) throw Error('Invalid composition kind or children.'); n.children.forEach(e=>{if(!e||typeof e!=='object')throw Error('A child entry needs a node.');visit(e.node,depth+1);});}
      else if (!outputs.includes(n.op)) throw Error('Unknown output operation.');
    }
    visit(doc.root); return doc;
  }
  // Deliberately bounded browser checks. Native compilation remains authoritative.
  // Empty patterns may be edited, but cannot pass validation or native compilation.
  function validate(doc) {
    const errors=[],seen=new Set(),integer=(v,min=0)=>Number.isSafeInteger(v)&&v>=min&&v<=2147483647;
    const fail=(path,message)=>errors.push(path+': '+message);
    try{parse(JSON.stringify(doc));}catch(e){return [e.message];}
    function exactFinite(n){
      if(!n.kind||n.kind==='random'||n.offset!==undefined||n.repeat==='forever'||(n.duration!==undefined&&!integer(n.duration,1)))return false;
      if(n.child_mode==='wait')return n.children.some(e=>e.node.kind)&&n.children.filter(e=>e.node.kind).every(e=>!e.exit&&exactFinite(e.node));
      if(n.kind==='sequential'&&n.child_mode!=='loop'&&n.cadence===undefined&&!n.children.every(e=>!e.exit&&exactFinite(e.node)))return false;
      return integer(n.duration,1);
    }
    function visit(n,path,parent,edge){
      if(n.kind){
        if(!n.children.length||n.children.length>4096)fail(path,'Pattern needs 1–4096 children.');
        if(n.id!==undefined){if(typeof n.id!=='string'||!n.id.length||seen.has(n.id))fail(path,'Scope IDs must be nonempty and unique.');seen.add(n.id);}
        if(!parent&&n.duration===undefined&&n.child_mode!=='wait')fail(path,'Root requires a finite duration or Wait for children.');
        if(n.duration!==undefined){const d=n.duration;if(typeof d==='object'&&d){if(!integer(d.min,1)||!integer(d.max,d.min)||!integer(d.step,1)||(d.max-d.min)%d.step)fail(path,'Sampled duration needs aligned positive min, max and step.');}else if(!integer(d,1))fail(path,'Duration must be a positive whole number of frames.');}
        if(n.child_mode!==undefined&&(!['independent','loop','wait'].includes(n.child_mode)||n.kind==='random'))fail(path,'Child policy must be independent, loop or wait on Parallel or Sequential.');
        if(n.child_mode==='wait'){
          for(const p of ['duration','cadence','offset','repeat'])if(n[p]!==undefined)fail(path+'.'+p,'Wait for children omits owner '+p+'.');
          const gates=n.children.filter(e=>e.node.kind);
          if(!gates.length)fail(path,'Wait for children needs at least one timed child composition.');
          if(gates.some(e=>e.exit||!exactFinite(e.node)))fail(path,'Wait for children needs exact finite child completion; sampled, random, phase and unbounded gates are unsupported.');
        }
        if(n.repeat!==undefined){if(n.duration===undefined)fail(path,'Repeat requires an explicit duration.');if(n.repeat!=='forever'&&!integer(n.repeat,1))fail(path,'Repeat must be a positive count or forever.');}
        if(n.offset!==undefined&&!integer(n.offset))fail(path,'Offset must be a nonnegative whole number of frames.');
        if(n.cadence!==undefined){if(typeof n.cadence==='number'){if(!integer(n.cadence,1))fail(path,'Cadence must be a positive whole number.');}else{const r=n.cadence&&n.cadence.ramp;if(!r||!integer(r.from,1)||!integer(r.to,1)||!integer(r.steps,1)||!['linear','early','late'].includes(r.ease)||!integer(n.duration,1)||r.steps>n.duration)fail(path,'Ramp needs valid endpoints, easing, steps and a fixed duration fitting all steps.');}}
        if(n.kind==='random'){const s=n.select;if(!s||!['random','round_robin'].includes(s.order)||typeof s.allow_self!=='boolean'||!integer(s.first)||s.first>=n.children.length)fail(path,'Random pattern needs a selection policy and valid first child.');else if(s.order==='round_robin'&&s.allow_self)fail(path,'Round robin cannot select itself.');}
        else if(n.select!==undefined)fail(path,'Selection policy belongs to Random patterns.');
        n.children.forEach((e,i)=>visit(e.node,path+'.children['+i+'].node',n,e));
      }else{
        if(!parent||parent.kind!=='parallel')fail(path,'Outputs need a Parallel owner.');
        if(n.op!=='spiral'&&!['primary','secondary','either',...(n.op==='image'?['alternate']:[])].includes(n.source))fail(path,'Choose a supported theme source.');
        if(n.animation&&(!integer(n.animation.every,1)||n.op!=='image'))fail(path,'Animation accent requires a positive every count on an image.');
        if(n.opacity!==undefined&&n.envelope!==undefined)fail(path,'Choose opacity or envelope.');
      }
      if(edge&&edge.exit){if(parent.kind!=='random')fail(path,'Chance exit belongs to a Random child.');if(!integer(edge.exit.check_every,1))fail(path,'Chance exit check interval must be positive.');}
      if(parent&&parent.kind!=='parallel'&&n.kind&&(parent.kind==='sequential'||!edge.exit)&&((n.duration===undefined&&n.child_mode!=='wait')||n.repeat==='forever'))fail(path,'Child needs finite duration, or a Random chance exit.');
    }
    visit(doc.root,'root',null,null);return errors;
  }
  function at(doc,path) {let node=doc.root; for(const index of path) node=node.children[index].node; return node;}
  function edge(doc,path) {return path.length?at(doc,path.slice(0,-1)).children[path.at(-1)]:null;}
  function ids(doc) {const found=[];function visit(n){if(n.id)found.push(n.id);(n.children||[]).forEach(e=>visit(e.node));}visit(doc.root);return found;}
  function uniqueId(doc,base='scope') {const used=new Set(ids(doc));let id=base,index=2;while(used.has(id))id=base+'_'+index++;return id;}
  function ancestorIds(doc,path) {const found=[];let n=doc.root;if(n.id)found.push(n.id);for(const index of path){n=n.children[index].node;if(n.kind&&n.id)found.push(n.id);}return found;}
  function cloneForInsert(doc,entry) {
    const copied=clone(entry),used=new Set(ids(doc)),renamed=new Map();
    function rename(n){if(n.id){const original=n.id;let next=original,index=2;while(used.has(next))next=original+'_'+index++;used.add(next);renamed.set(original,next);n.id=next;}(n.children||[]).forEach(e=>rename(e.node));}
    rename(copied.node);
    function references(value){if(!value||typeof value!=='object')return;if(value.curve&&renamed.has(value.curve.scope))value.curve.scope=renamed.get(value.curve.scope);Object.values(value).forEach(references);}
    references(copied);return copied;
  }
  function properties(n) {
    if(n.kind)return [...(n.child_mode==='wait'?[]:['duration','repeat','cadence','offset']),'id',...(n.kind==='random'?['select']:[])];
    if(n.op==='spiral')return ['speed'];
    if(n.op==='image')return ['source','visibility','zoom','zoom_baseline',...(!n.envelope?['opacity']:[]),...(!('opacity'in n)?['envelope']:[]),'animation','selection_probability',...(n.source==='alternate'?['flip_probability']:[])];
    return ['source','visibility','selection_probability',...(['caption','subtext'].includes(n.op)?[...(!n.envelope?['opacity']:[]),...(!('opacity'in n)?['envelope']:[])]:[])];
  }
  const defaults={duration:128,repeat:2,cadence:64,offset:0,id:'scope',select:{order:'random',allow_self:false,first:0,cooldown:0},zoom:{curve:{from:0,to:0.5,scope:'local',ease:'linear'}},zoom_baseline:0,opacity:1,speed:1,visibility:[0,1],animation:{every:4},envelope:{in:8,hold:16,out:8},flip_probability:0.5,selection_probability:1,source:'either'};
  const api={kinds,outputs,clone,create,parse,validate,at,edge,defaults,ids,uniqueId,ancestorIds,cloneForInsert,properties};
  root.TranceV4=api; if(typeof module!=='undefined') module.exports=api;
})(typeof window!=='undefined'?window:globalThis);
