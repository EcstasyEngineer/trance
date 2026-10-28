(function(root){
  'use strict';
  // A bounded static schedule, never a pixel or media simulator. Random and
  // sampled schedules and complex nested phase/cadence remain unresolved.
  const finite=v=>Number.isSafeInteger(v)&&v>0;
  // Mirrors OccurrenceCycler::begin_iteration: midpoint easing, positive
  // lround, then distribute residual frames backwards until the span fits.
  function intervals(cadence,span){
    if(typeof cadence==='number')return finite(cadence)?[cadence]:null;
    const r=cadence&&cadence.ramp;if(!r||!finite(r.steps)||r.steps>65536||r.steps>span||!finite(r.from)||!finite(r.to))return null;
    const raw=Array.from({length:r.steps},(_,i)=>{let p=(i+.5)/r.steps;if(r.ease==='early')p=1-Math.pow(1-p,3);else if(r.ease==='late')p=p*p*p;return r.from+(r.to-r.from)*p;});
    const sum=raw.reduce((a,b)=>a+b,0),values=raw.map(v=>Math.max(1,Math.floor(v*span/sum+.5)));
    let diff=span-values.reduce((a,b)=>a+b,0),i=values.length-1;
    while(diff){if(diff>0){values[i]++;diff--;}else if(values[i]>1){values[i]--;diff++;}i=i?i-1:values.length-1;}
    return values;
  }
  function directCadence(n){return n.kind==='parallel'&&n.cadence!==undefined&&n.children.every(e=>!e.node.kind&&!e.exit);}
  function measure(n,inherited){
    if(!n.kind)return {span:inherited,end:inherited};
    if((n.offset!==undefined&&!directCadence(n))||typeof n.duration==='object'||n.repeat==='forever')return {span:finite(n.duration)?n.duration:null,end:null,reason:'Sampled / random / phase / cadence timing needs a native trace'};
    const timed=(n.children||[]).filter(e=>e.node.kind);
    const child=timed.map(e=>measure(e.node));
    let span=n.duration;
    if(n.child_mode==='wait')span=child.length&&child.every(m=>finite(m.end))?(n.kind==='sequential'?child.reduce((s,m)=>s+m.end,0):Math.max(...child.map(m=>m.end))):null;
    if(span===undefined)span=inherited;
    if(!finite(span))return {span:null,end:null,reason:'No exact finite owner duration'};
    let end=span;
    if(n.kind==='sequential'&&n.child_mode!=='loop'&&n.cadence===undefined){
      const measures=n.children.map(e=>e.exit||!e.node.kind?{end:null}:measure(e.node,span));
      end=measures.every(m=>finite(m.end))?Math.min(span,measures.reduce((s,m)=>s+m.end,0)):null;
    }
    const count=n.repeat===undefined?1:n.repeat;
    if(!finite(count))return {span:null,end:null,reason:'Unresolved repeat'};
    if(!finite(span*count)||(end!==null&&!finite(end*count)))return {span:null,end:null,reason:'Frame extent exceeds exact browser integer range'};
    return {span:span*count,end:end===null?null:end*count,visitSpan:span,visitEnd:end,count};
  }
  function layout(doc){
    const rows=[],notes=[],limit=600;let truncated=false;
    const rootMeasure=measure(doc.root),sample=doc.root.duration,knownExtent=(rootMeasure.count>1&&rootMeasure.end!==null?rootMeasure.end:rootMeasure.span)||(sample&&finite(sample.max)?sample.max:null),extent=knownExtent||1;
    function note(path,message){notes.push((path.length?'root.'+path.join('.'):'root')+': '+message);}
    function visit(n,path,start,cap,inherited,occurrence=1){
      if(rows.length>=limit){truncated=true;return;}
      const m=measure(n,inherited),available=Math.max(0,cap-start),span=m.span===null?available:Math.min(available,m.count>1&&m.end!==null?m.end:m.span);
      const row={range:typeof n.duration==='object'&&n.duration?n.duration.min+'–'+n.duration.max+'f':null,path:[...path],name:n.kind||n.op,start,end:start+span,authored:m.span,completion:m.end===null?null:start+Math.min(available,m.end),uncertain:m.end===null,occurrence,clipped:m.end===null?0:Math.max(0,m.end-available)};rows.push(row);
      if(row.clipped)note(path,row.clipped+' authored frames clipped by owner');
      if(!n.kind)return;
      if(m.end===null){row.reason=m.reason||'Child completion is unresolved';note(path,(row.range?'sampled duration '+row.range+'; ':'')+row.reason);return;}
      if(directCadence(n)){
        const lengths=intervals(n.cadence,m.visitSpan||span);
        if(!lengths){row.uncertain=true;note(path,'Cadence intervals cannot be established exactly');return;}
        for(let rep=0;rep<(m.count||1)&&rows.length<limit;rep++){
          const base=start+rep*m.visitSpan,bound=Math.min(cap,base+m.visitSpan);
          let index=0,phase=n.offset?(lengths[0]-n.offset%lengths[0])%lengths[0]:0,cursor=base,occurrence=1;
          if(phase)note(path,'initial '+Math.min(lengths[0]-phase,bound-base)+'f live window follows '+phase+'f pre-roll; entry media selection is suppressed until the first live cadence boundary');
          while(cursor<bound&&rows.length<limit){
            const length=lengths[Math.min(index,lengths.length-1)],end=Math.min(bound,cursor+length-phase);
            if(end<cursor+length-phase)note(path,(cursor+length-phase-end)+' cadence body frames clipped by owner end');
            n.children.forEach((edge,i)=>{const before=rows.length;visit(edge.node,[...path,i],cursor,end,end-cursor,occurrence);if(phase&&rows.length>before)rows[before].preRoll=true;});
            cursor=end;phase=0;index++;occurrence++;
          }
        }
        return;
      }
      if(n.kind==='random'||n.cadence!==undefined){row.uncertain=true;row.reason='Owner window is exact; internal random / cadence visits require a native trace';note(path,row.reason);return;}
      const visitSpan=m.visitSpan||span,visitEnd=m.visitEnd||span;
      if(n.kind==='sequential'&&n.child_mode!=='loop'&&n.cadence===undefined&&visitEnd<visitSpan){row.inactive=m.span-m.end;note(path,row.inactive+' authored clock frames after sequence completion'+((m.count||1)>1?' across '+m.count+' visits; explicit repeats restart immediately':!path.length?'; director advances here (no automatic root replay)':' inside owner; this does not guarantee a blank screen'));}
      const count=m.count||1;
      for(let rep=0;rep<count;rep++){
        const base=start+rep*visitEnd;if(base>=cap)break;
        const bound=Math.min(cap,base+visitSpan);
        if(n.kind==='sequential'){
          let cursor=base,cycle=0;
          do{
            const before=cursor;
            for(let i=0;i<n.children.length;i++){
              if(cursor>=bound)break;
              const child=n.children[i].node,cm=measure(child,visitSpan);
              visit(child,[...path,i],cursor,bound,visitSpan,rep+cycle+1);
              if(cm.end===null){cursor=bound;break;}cursor+=cm.end;
            }
            cycle++;if(cursor===before)break;
          }while(n.child_mode==='loop'&&cursor<bound&&rows.length<limit);
        }else{
          n.children.forEach((e,i)=>{
            const cm=measure(e.node,visitSpan);let cursor=base,cycle=0;
            do{visit(e.node,[...path,i],cursor,bound,visitSpan,rep+cycle+1);if(!finite(cm.end))break;cursor+=cm.end;cycle++;}
            while(n.child_mode==='loop'&&e.node.kind&&cursor<bound&&rows.length<limit);
            if(e.node.kind&&cm.end!==null&&cm.end<visitSpan&&n.child_mode!=='loop')note([...path,i],(visitSpan-cm.end)+' inactive frames in parent after child completion; other outputs may remain visible');
          });
        }
      }
    }
    visit(doc.root,[],0,extent,extent);
    if(truncated||rows.length>=limit)notes.push('Timeline limited to '+limit+' occurrences; remaining repeated bars omitted.');
    return {rows,notes,extent,knownExtent:knownExtent!==null,exact:rootMeasure.end!==null&&rows.every(r=>!r.uncertain),completion:rootMeasure.end};
  }
  root.TranceV4Timeline={measure,layout,intervals};if(typeof module!=='undefined')module.exports=root.TranceV4Timeline;
})(typeof window!=='undefined'?window:globalThis);
