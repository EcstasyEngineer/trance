(function(root){
  'use strict';
  // A bounded static schedule, never a pixel or media simulator. Random
  // schedules and complex nested phase/cadence remain unresolved.
  const G=root.TranceV4||(typeof require==='function'?require('./v4-grammar.js'):null);
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
    const span=n.duration===undefined?(inherited===undefined?G.rootDuration(n):inherited):n.duration;
    if((n.offset!==undefined&&!directCadence(n))||n.repeat==='forever')return {span:finite(span)?span:null,end:null,reason:'Phase or unbounded timing needs a native trace'};
    if(!finite(span))return {span:null,end:null,reason:'Inherit needs a parent duration or finite timed subpatterns'};
    let end=span;
    if(n.kind==='sequential'&&!n.loop_subpatterns&&n.cadence===undefined){
      let cursor=0;
      for(const edge of n.children){
        if(cursor>=span)break;
        const child=edge.exit||!edge.node.kind?{end:null}:measure(edge.node,span-cursor);
        if(child.end===null){cursor=null;break;}cursor+=child.end;
      }
      end=cursor===null?null:Math.min(span,cursor);
    }
    const count=n.repeat===undefined?1:n.repeat;
    if(!finite(count))return {span:null,end:null,reason:'Unresolved repeat'};
    if(!finite(span*count)||(end!==null&&!finite(end*count)))return {span:null,end:null,reason:'Frame extent exceeds exact browser integer range'};
    return {span:span*count,end:end===null?null:end*count,visitSpan:span,visitEnd:end,count};
  }
  function layout(doc){
    const rows=[],notes=[],limit=600;let truncated=false;
    const rootMeasure=measure(doc.root),knownExtent=(rootMeasure.count>1&&rootMeasure.end!==null?rootMeasure.end:rootMeasure.span),extent=knownExtent||1;
    function note(path,message){notes.push((path.length?'root.'+path.join('.'):'root')+': '+message);}
    function visit(n,path,start,cap,inherited,occurrence=1){
      if(rows.length>=limit){truncated=true;return;}
      const m=measure(n,inherited),available=Math.max(0,cap-start),span=m.span===null?available:Math.min(available,m.count>1&&m.end!==null?m.end:m.span);
      const row={path:[...path],name:n.kind||n.op,start,end:start+span,authored:m.span,completion:m.end===null?null:start+Math.min(available,m.end),uncertain:m.end===null,occurrence,clipped:m.end===null?0:Math.max(0,m.end-available)};rows.push(row);
      if(row.clipped)note(path,row.clipped+' authored frames clipped by owner');
      if(!n.kind)return;
      if(m.end===null){row.reason=m.reason||'Child completion is unresolved';note(path,row.reason);return;}
      if(directCadence(n)){
        const lengths=intervals(n.cadence,m.visitSpan||span);
        if(!lengths){row.uncertain=true;note(path,'Cadence intervals cannot be established exactly');return;}
        for(let rep=0;rep<(m.count||1)&&rows.length<limit;rep++){
          const base=start+rep*m.visitSpan,bound=Math.min(cap,base+m.visitSpan);
          let index=0,phase=n.offset?(lengths[0]-n.offset%lengths[0])%lengths[0]:0,cursor=base,occurrence=1;
          if(phase)note(path,'initial '+Math.min(lengths[0]-phase,bound-base)+'f live window follows '+phase+'f pre-roll; entry media selection is suppressed until the first live cadence boundary');
          while(cursor<bound&&rows.length<limit){
            const length=lengths[Math.min(index,lengths.length-1)],logicalEnd=Math.min(base+m.visitSpan,cursor+length-phase),end=Math.min(bound,logicalEnd);
            if(logicalEnd<cursor+length-phase)note(path,(cursor+length-phase-logicalEnd)+' cadence body frames clipped by owner end');
            n.children.forEach((edge,i)=>{const before=rows.length;visit(edge.node,[...path,i],cursor,end,logicalEnd-cursor,occurrence);if(phase&&rows.length>before)rows[before].preRoll=true;});
            cursor=end;phase=0;index++;occurrence++;
          }
        }
        return;
      }
      if(n.kind==='random'||n.cadence!==undefined){row.uncertain=true;row.reason='Owner window is exact; internal random / cadence visits require a native trace';note(path,row.reason);return;}
      const visitSpan=m.visitSpan||span,visitEnd=m.visitEnd||span;
      if(n.kind==='sequential'&&!n.loop_subpatterns&&n.cadence===undefined&&visitEnd<visitSpan){row.inactive=m.span-m.end;note(path,row.inactive+' authored clock frames after sequence completion'+((m.count||1)>1?' across '+m.count+' visits; explicit repeats restart immediately':!path.length?'; director advances here (no automatic root replay)':' inside owner; this does not guarantee a blank screen'));}
      const count=m.count||1;
      for(let rep=0;rep<count;rep++){
        const base=start+rep*visitEnd;if(base>=cap)break;
        // Ancestor clipping changes visible bars, not the owning logical clock.
        const bound=Math.min(cap,base+visitSpan);
        if(n.kind==='sequential'){
          let cursor=base,cycle=0;
          do{
            const before=cursor;
            for(let i=0;i<n.children.length;i++){
              if(cursor>=bound)break;
              const child=n.children[i].node,cm=measure(child,base+visitSpan-cursor);
              visit(child,[...path,i],cursor,bound,base+visitSpan-cursor,rep+cycle+1);
              if(cm.end===null){cursor=bound;break;}cursor+=cm.end;
            }
            cycle++;if(cursor===before)break;
          }while(n.loop_subpatterns&&cursor<bound&&rows.length<limit);
        }else{
          n.children.forEach((e,i)=>{
            const first=measure(e.node,visitSpan);let cursor=base,cycle=0;
            do{const cm=measure(e.node,base+visitSpan-cursor);visit(e.node,[...path,i],cursor,bound,base+visitSpan-cursor,rep+cycle+1);if(!finite(cm.end))break;cursor+=cm.end;cycle++;}
            while(n.loop_subpatterns&&e.node.kind&&cursor<bound&&rows.length<limit);
            if(e.node.kind&&first.end!==null&&first.end<visitSpan&&!n.loop_subpatterns)note([...path,i],(visitSpan-first.end)+' inactive frames in parent after child completion; other outputs may remain visible');
          });
        }
      }
    }
    visit(doc.root,[],0,extent,rootMeasure.visitSpan);
    if(truncated||rows.length>=limit)notes.push('Timeline limited to '+limit+' occurrences; remaining repeated bars omitted.');
    return {rows,notes,extent,knownExtent:knownExtent!==null,exact:rootMeasure.end!==null&&rows.every(r=>!r.uncertain),completion:rootMeasure.end};
  }
  root.TranceV4Timeline={measure,layout,intervals};if(typeof module!=='undefined')module.exports=root.TranceV4Timeline;
})(typeof window!=='undefined'?window:globalThis);
