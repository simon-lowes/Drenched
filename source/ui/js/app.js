/* Drenched interface: rendering, interaction, the skin picker and the bridge to the plugin.
   engine.js (loaded first) provides the model (st, SPACES, P, SKINS) and the twelve
   compositions. This file owns everything that touches the DOM or the host. */
'use strict';
(function(){
var live=document.getElementById('live');
var W=1200,H=760;
var JUCE=(window.__JUCE__&&window.__JUCE__.backend)?window.__JUCE__.backend:null;

/* ---- parameters: UI keys on the left, plugin parameter ids on the right ---- */
var PID={mix:'mix',pre:'predelay',size:'size',bright:'brightness',el:'earlyLate',dist:'distance',output:'output',freeze:'freeze',
         preSync:'predelaySync',xover:'crossover',locut:'wetLowCut',hicut:'wetHighCut',width:'width',duck:'ducking',mono:'monoBelow',
         living:'living',move:'movement',moveRate:'movementRate',rev:'reverse',gate:'gate',attack:'attack',dLvl:'directLevel',dDel:'directDelay',dCol:'directColour',xfeed:'crossFeed',cpu:'lowCpu',cloud:'cloudSize',lx:'listenerX',sx:'sourceX'};
for(var eb=1;eb<=EQ_N;eb++){PID['eq'+eb+'f']='eq'+eb+'Freq';PID['eq'+eb+'g']='eq'+eb+'Gain';if(eqBell(eb-1))PID['eq'+eb+'q']='eq'+eb+'Q';}
var BAND_ID=['decay63','decay125','decay250','decay500','decay1k','decay2k','decay4k','decay8k'];
/* the right side's curve, heard only while decayLink is 0; linked, the eight above serve both sides */
var RBAND_ID=['decayR63','decayR125','decayR250','decayR500','decayR1k','decayR2k','decayR4k','decayR8k'],LINK_ID='decayLink';
function keyOf(id){for(var k in PID)if(PID[k]===id)return k;return null;}
/* the parameter id of band i on the side being edited */
function bandId(s,i){return !s.link&&s.side==='R'?RBAND_ID[i]:BAND_ID[i];}
function paramValues(s){var o={};for(var k in PID)o[PID[k]]=k==='freeze'?(s.freeze?1:0):s[k];s.bands.forEach(function(b,i){o[BAND_ID[i]]=b;});s.rbands.forEach(function(b,i){o[RBAND_ID[i]]=b;});o[LINK_ID]=s.link?1:0;return o;}
function uiState(s){return {spaceId:SPACES[s.sp]?SPACES[s.sp].id:'',locks:Object.keys(s.locks).filter(function(k){return s.locks[k];}).join(','),mode:s.mode,view:s.view,skin:s.skin,speakers:speakersText(s)};}

var lastSent={},lastUi={};
function sync(){var cur=paramValues(st),out={},n=0,stale=false;for(var id in cur){if(lastSent[id]!==cur[id]){out[id]=cur[id];lastSent[id]=cur[id];n++;stale=stale||rebuilds(id);}}if(n&&JUCE)JUCE.emitEvent('params',out);if(stale&&JUCE)staleAnalysis();}
function syncUi(){var cur=uiState(st),out={},n=0;for(var k in cur){if(lastUi[k]!==cur[k]){out[k]=cur[k];lastUi[k]=cur[k];n++;}}if(n&&JUCE)JUCE.emitEvent('ui',out);if(out.spaceId!=null&&JUCE)staleAnalysis();}
/* a change that rebuilds the response: the displays keep the last analysis, dimmed, until the new one arrives */
function staleAnalysis(){if(!anaStale&&ana){anaStale=true;wfNote();}}
function gesture(ids,begin){if(JUCE)JUCE.emitEvent('gesture',{ids:ids,begin:!!begin});}

/* state pushed by the plugin: parameter values (host automation, preset recall) and the
   saved interface state (space, locks, view, skin) */
var irEvent=null,shortened=0,sceneSeen=false;
function applyFromHost(msg){if(!msg)return;var p=msg.params||{},u=msg.ui||{};
 if(u.shortened!=null){var sh=+u.shortened||0;if(sh>0&&sh!==shortened){st.hint=null;
   st.note='<b>Shortened to '+sh.toFixed(1)+' s.</b> The 32-bit version only has memory for this much of a surround reverb at this sample rate. The 64-bit version plays it in full.';}
  shortened=sh;}
 if('ir' in u)setUserSpace(u.ir);
 if('scenes' in u)setScenes(u.scenes);
 if(p.scene!=null){var sn=Math.round(+p.scene);if(sceneSeen&&sn!==st.scene&&sn>0&&sceneOf(sn)){st.hint=null;st.note='<b>'+esc(sceneOf(sn).name)+'</b> recalled: scene '+sn+', '+esc(sceneOf(sn).space)+'.';}st.scene=isFinite(sn)?sn:0;sceneSeen=true;}
 if(u.irEvent!=null){var ev=+u.irEvent;if(irEvent!==null&&ev!==irEvent){st.hint=null;
   st.note=u.irError?'<b>That file didn\u2019t load.</b> '+esc(u.irError):'<b>'+esc(SPACES[USER]?SPACES[USER].name:'Your IR')+'</b> loaded. The graph shows its measured decay; drag a band to reshape it.';}
  irEvent=ev;}
 // a value that isn't a number arrives as null, which + would read as 0
 for(var id in p){if(p[id]==null)continue;var v=+p[id];if(!isFinite(v))continue;lastSent[id]=v;var bi=BAND_ID.indexOf(id);if(bi>=0){st.bands[bi]=v;continue;}
  var ri=RBAND_ID.indexOf(id);if(ri>=0){st.rbands[ri]=v;continue;}if(id===LINK_ID){st.link=v>=0.5;if(st.link)st.side='L';continue;}
  var k=keyOf(id);if(!k)continue;if(k==='freeze')st.freeze=v>=0.5;else st[k]=v;}
 if(u.spaceId!=null){var si=spaceIndex(String(u.spaceId));if(si>=0){st.sp=si;lastUi.spaceId=String(u.spaceId);}}
 if(u.locks!=null){st.locks={};String(u.locks).split(',').forEach(function(k){if(k&&P[k])st.locks[k]=true;});lastUi.locks=uiState(st).locks;}
 if(u.mode==='studio'||u.mode==='stage'){st.mode=u.mode;lastUi.mode=u.mode;}
 if(u.view==='plan'||u.view==='section'){st.view=u.view;lastUi.view=u.view;}
 if(u.skin&&SKINS.some(function(x){return x.id===u.skin;})){st.skin=u.skin;lastUi.skin=u.skin;}
 if(Array.isArray(u.outputs)&&u.outputs.length)st.outputs=u.outputs.map(function(o){return {name:String(o&&o.name||''),wet:!!(o&&o.wet)};});
 if(u.speakers!=null){st.speakers=parseSpeakers(String(u.speakers));lastUi.speakers=speakersText(st);}
 schedule();}

/* meters arrive often; they update in place rather than through a full render */
function onMeters(m){if(!m)return;st.inL=+m.inL;st.inR=+m.inR;st.outL=+m.outL;st.outR=+m.outR;
 var i=Math.max(st.inL,st.inR),o=Math.max(st.outL,st.outR),map={'in':i,'out':o,outL:st.outL,outR:st.outR};
 var bars=live.querySelectorAll('[data-m]');for(var a=0;a<bars.length;a++){var el=bars[a],v=map[el.getAttribute('data-m')];if(v==null)continue;var pct=meterPct(v).toFixed(0)+'%';if(el.hasAttribute('style')&&el.style.height)el.style.height=pct;else el.style.width=pct;if(el.nextElementSibling&&el.nextElementSibling.tagName==='B')el.nextElementSibling.style.bottom=Math.min(98,meterPct(v)+4).toFixed(0)+'%';}
 var txt=live.querySelectorAll('[data-mt]');for(var b=0;b<txt.length;b++){var v2=map[txt[b].getAttribute('data-mt')];if(v2!=null)txt[b].textContent=dbText(v2);}
 if(Array.isArray(m.spk)){st.spkM=m.spk.map(Number);var sb=live.querySelectorAll('[data-spm]');for(var c=0;c<sb.length;c++){var sv=st.spkM[+sb[c].getAttribute('data-spm')];if(sv!=null)sb[c].style.width=meterPct(sv).toFixed(0)+'%';}}}

/* ---- Match a room: the plugin listens to a recording and reports the decay it heard ---- */
var BAND_NAMES=['63 Hz','125 Hz','250 Hz','500 Hz','1 kHz','2 kHz','4 kHz','8 kHz'];
function onMatched(m){if(!m)return;st.hint=null;
 if(m.listening)st.note='<b>Listening to '+esc(m.file||'the recording')+'</b> for the room\u2019s decay.';
 else if(m.error)st.note='<b>No match.</b> '+esc(m.error);
 else if(Array.isArray(m.bands)&&m.bands.length===8){var si=spaceIndex(String(m.spaceId||''));if(si>=0)changeSpace(si);
  st.bands=m.bands.map(function(b){return clamp(+b||1,0.1,20);});st.rbands=st.bands.slice();
  var missed=(m.heard||[]).map(function(n,i){return +n>0?null:BAND_NAMES[i];}).filter(Boolean);
  st.note='<b>Matched'+(si>=0?' to '+esc(SPACES[si].name):'')+'.</b> '+st.bands[4].toFixed(1)+' s at 1 kHz, as heard in '+esc(m.file||'the recording')+'.'+
   (missed.length?' '+missed.join(', ')+(missed.length>1?' are':' is')+' estimated.':' Drag a band to adjust.');}
 else return;
 renderLive();sync();syncUi();}

/* ---- the skin picker ---- */
function pickerHTML(){var ts=Object.assign({},st,{browse:false,adv:false,skins:false,hint:null,note:null,licence:{state:'licensed'}});
 var thumb=function(x){return screenHTML(x.id,ts).replace(/ data-fid="[^"]*"/g,'').replace(/tabindex="0"/g,'tabindex="-1"');};
 return '<div class="skins" role="dialog" aria-modal="true" aria-label="Choose a look"><div class="hd"><h2>Choose a look</h2><span>Twelve looks over the same controls. Every value stays where it is.</span><button data-act="skins" aria-label="Close">Close</button></div>'+
  '<div class="grid">'+SKINS.map(function(x){return '<div class="sk" role="button" tabindex="0" data-act="skin" data-v="'+x.id+'" aria-current="'+(x.id===st.skin)+'" aria-label="'+x.name+', '+x.tag.toLowerCase()+'"><div class="fr" inert>'+thumb(x)+'</div><div class="t"><b>'+x.name+'</b><span>'+x.tag+'</span></div></div>';}).join('')+'</div>'+
  '<div class="ft">Click the Drenched name in any look to come back here. The look is saved with this instance and becomes the default for new ones.</div></div>';}

/* ---- capturing a space: the plugin plays a sweep and records the room ---- */
st.capture=false;st.cap={method:'sweep',speakers:1,sweep:10,tail:6,level:-12,listen:10};st.capStatus={stage:'idle',progress:0,peakDb:-100,message:''};
var CAP_OPTS={method:[['sweep','Sine sweep'],['clap','Hand clap']],listen:[[5,'5 s'],[10,'10 s'],[20,'20 s']],speakers:[[1,'Left only'],[2,'Left, then right']],sweep:[[5,'5 s'],[10,'10 s'],[20,'20 s']],tail:[[3,'3 s'],[6,'6 s'],[12,'12 s']],level:[[-24,'\u221224 dB'],[-18,'\u221218 dB'],[-12,'\u221212 dB'],[-6,'\u22126 dB']]};
var CAP_ROWS=[['method','Method','A sweep gives the cleanest result. A clap needs no speakers and is quick to try.'],['listen','Listen for','Long enough to clap and let the room fall silent. Clap again if one goes wrong; the best clap is kept.'],['speakers','Speakers','Left, then right gives true stereo when you record with two microphones.'],['sweep','Sweep length','A longer sweep rises further above background noise.'],['tail','Ring-out','Make it longer than the room takes to fall silent.'],['level','Sweep level','Start low. Aim for a mic peak around \u221212 dB.']];
function capBusy(){var g=st.capStatus.stage;return g==='playing'||g==='working';}
function capPeak(){var p=+st.capStatus.peakDb;return p<=-99?'Mic peak: nothing yet':'Mic peak '+dbText(p)+' dB';}
function capRows(){return CAP_ROWS.filter(function(r){return r[0]==='method'||(st.cap.method==='clap')===(r[0]==='listen');});}
function captureHTML(){var c=st.cap,cs=st.capStatus,busy=capBusy(),clap=c.method==='clap',secs=clap?c.listen:c.speakers*(c.sweep+c.tail);
 var rows=capRows().map(function(r){return '<div class="cr"><span class="cl" id="cl-'+r[0]+'">'+r[1]+'</span><span class="seg" role="radiogroup" aria-labelledby="cl-'+r[0]+'">'+CAP_OPTS[r[0]].map(function(o){return '<button role="radio" data-act="capopt" data-k="'+r[0]+'" data-v="'+o[0]+'" aria-checked="'+(c[r[0]]===o[0])+'"'+(busy?' disabled':'')+'>'+o[1]+'</button>';}).join('')+'</span><small>'+r[2]+'</small></div>';}).join('');
 var status=cs.stage==='playing'?'<div class="cpr"><span class="bar"><i data-cap="p" style="width:'+(clamp(+cs.progress||0,0,1)*100).toFixed(1)+'%"></i></span><em data-cap="pk">'+capPeak()+'</em></div><span>'+esc(cs.message)+'</span>'
  :cs.stage==='working'?'<span>Working out the impulse response.</span>'
  :cs.message?'<span class="'+(cs.stage==='failed'?'err':'')+'">'+esc(cs.message)+'</span>':'';
 var action=cs.stage==='playing'?'<button class="cgo stop" data-act="capstop">Stop capture</button>'
  :cs.stage==='working'?'<button class="cgo" disabled>Working\u2026</button>'
  :'<button class="cgo" data-act="capstart">Start capture</button><span class="ct">Takes '+secs+' seconds. Captures are saved in Documents, Drenched, Captures.</span>';
 return '<div class="cap" role="dialog" aria-modal="true" aria-labelledby="cap-h"><div class="cbox"><div class="chd"><h2 id="cap-h">Capture a space</h2><button class="cx" data-act="capture" aria-label="Close">Close</button></div>'+
  (clap?'<p>Drenched listens through your microphone while you clap once where the sound should come from, then turns the best clap into an impulse response for this instance.</p>'+
  '<p>Put Drenched on a track whose input is the room microphone. Clap sharply, keep still and quiet while the room rings out.</p>'
  :'<p>Drenched plays a sweep through your speakers, records it with your microphone and turns the recording into an impulse response for this instance.</p>'+
  '<p>Put Drenched on a track whose input is the room microphone, turn input monitoring on, and set your speakers to a comfortable level.</p>')+
  rows+'<div class="cst" aria-live="polite">'+status+'</div><div class="cft">'+action+'</div></div></div>';}
/* capture progress arrives often; it updates in place unless the stage changes */
function onCapture(m){if(!m)return;var was=st.capStatus.stage,msgWas=st.capStatus.message;st.capStatus={stage:String(m.stage||'idle'),progress:+m.progress||0,peakDb:m.peakDb==null?-100:+m.peakDb,message:String(m.message||'')};
 if(was==='playing'&&st.capStatus.stage==='playing'&&msgWas===st.capStatus.message){var bar=live.querySelector('[data-cap="p"]'),pk=live.querySelector('[data-cap="pk"]');if(bar)bar.style.width=(clamp(st.capStatus.progress,0,1)*100).toFixed(1)+'%';if(pk)pk.textContent=capPeak();return;}
 // A finished capture loads straight away: close, so the room and its decay are in view.
 if(was==='working'&&st.capStatus.stage==='idle')st.capture=false;
 renderLive();}

/* ---- credits for the recorded spaces: who made each recording, and its licence ---- */
st.credits=false;
function creditsHTML(){var seen={},rows=[];LIBRARY.forEach(function(x){var k=x.credit+'|'+x.licence+'|'+x.source+'|'+x.changes;if(!x.credit||seen[k])return;seen[k]=[];rows.push({credit:x.credit,licence:x.licence,source:x.source,changes:x.changes,spaces:seen[k]});});
 LIBRARY.forEach(function(x){var k=x.credit+'|'+x.licence+'|'+x.source+'|'+x.changes;if(seen[k])seen[k].push(x.name);});
 var body=rows.length?'<ul class="crl">'+rows.map(function(r){return '<li><b>'+esc(r.credit)+'</b><span>'+esc(r.licence)+(r.source?', '+esc(r.source):'')+'</span><small>'+esc(r.spaces.join('; '))+'</small>'+(r.changes?'<small>Changes: '+esc(r.changes)+'</small>':'')+'</li>';}).join('')+'</ul>'
   :'<p>This build has no recorded spaces.</p>';
 return '<div class="cap crd" role="dialog" aria-modal="true" aria-labelledby="crd-h"><div class="cbox"><div class="chd"><h2 id="crd-h">Recording credits</h2><button class="cx" data-act="credits" aria-label="Close">Close</button></div>'+
  '<p>The recorded spaces are used under the licences below. Thank you to everyone who made and shared them.</p>'+body+'</div></div>';}

/* ---- each speaker's reverb level and delay, with a meter per output ---- */
st.spk=false;st.speakers=[];st.outputs=[{name:'L',wet:true},{name:'R',wet:true}];st.spkM=[];
var SPK_LEVEL={min:-60,max:12},SPK_DELAY={min:0,max:100};
function parseSpeakers(t){return t?t.split(';').map(function(p){var v=p.trim().split(/\s+/);return [clamp(+v[0]||0,SPK_LEVEL.min,SPK_LEVEL.max),clamp(+v[1]||0,SPK_DELAY.min,SPK_DELAY.max)];}):[];}
function speakerOf(s,i){return s.speakers[i]||[0,0];}
function speakersText(s){var n=s.outputs.length,parts=[],any=false;for(var i=0;i<n;i++){var v=speakerOf(s,i);any=any||v[0]!==0||v[1]!==0;parts.push(v[0]+' '+v[1]);}return any?parts.join(';'):'';}
function spkLevelText(l){return l<=SPK_LEVEL.min?'Off':(l>0?'+':l<0?'\u2212':'')+Math.abs(l).toFixed(1)+' dB';}
function speakersHTML(){var rows=st.outputs.map(function(o,i){var v=speakerOf(st,i),m=st.spkM[i]==null?-60:st.spkM[i],nm=esc(o.name||String(i+1));
  return '<div class="spr"><b>'+nm+'</b><span class="bar" aria-hidden="true"><i data-spm="'+i+'" style="width:'+meterPct(m).toFixed(0)+'%"></i></span>'+
   (o.wet?'<input data-fid="spl-'+i+'" data-spk="'+i+'" data-f="l" aria-label="'+nm+' reverb level" value="'+spkLevelText(v[0])+'"><input data-fid="spd-'+i+'" data-spk="'+i+'" data-f="d" aria-label="'+nm+' reverb delay" value="'+(+v[1]).toFixed(1)+' ms">'
    :'<span class="spn">No reverb on the LFE</span>')+'</div>';}).join('');
 return '<div class="cap spkd" role="dialog" aria-modal="true" aria-labelledby="spk-h"><div class="cbox"><div class="chd"><h2 id="spk-h">Reverb speakers</h2><button class="cx" data-act="speakers" aria-label="Close">Close</button></div>'+
  '<p>Set the reverb\u2019s level and delay in each speaker: push the rears later and quieter for depth, or line the room up with speakers at different distances. The dry sound is not changed.</p>'+
  '<div class="sph"><span>Speaker</span><span>Output</span><span>Level</span><span>Delay</span></div><div class="spl">'+rows+'</div>'+
  '<div class="cft"><button class="cgo stop" data-act="spkreset">Reset all</button><span class="ct">Levels from Off to +12 dB, delays up to 100 ms. Saved with the session.</span></div></div></div>';}

/* ---- the wet EQ: six bands on the reverb, drawn as a response curve with a point per band ---- */
st.eq=false;st.eqCur=null;var EQ_Q_SPAN=100;
function eqKey(i,c){return 'eq'+(i+1)+c;}
function eqHTML(){var cur=st.eqCur,cols=function(f){var o='';for(var i=0;i<EQ_N;i++)o+=f(i);return o;};
 var heads=cols(function(i){return '<b'+(i===cur?' class="cur"':'')+'>'+EQ_NAME[i]+'</b>';});
 var row=function(c){return cols(function(i){return eqBell(i)||c!=='q'?roEl(st,eqKey(i,c)):'<em title="The shelves have a fixed slope">\u2013</em>';});};
 return '<div class="cap eqd" role="dialog" aria-modal="true" aria-labelledby="eq-h"><div class="cbox"><div class="chd"><h2 id="eq-h">Wet EQ</h2><button class="cx" data-act="eq" aria-label="Close">Close</button></div>'+
  '<p>Six bands shape the reverb alone: a low shelf, four bells and a high shelf, after the wet low cut and high cut. The dry sound is not changed, and each value can be automated.</p>'+
  '<div class="eqw"><canvas class="eqs" data-cv="eqspec" data-w="678" data-h="200" aria-hidden="true"></canvas>'+eqSVG(st,728,236,cur)+'</div>'+
  '<div class="eqt" role="group" aria-label="Band values"><span></span>'+heads+'<span>Frequency</span>'+row('f')+'<span>Gain</span>'+row('g')+'<span>Q</span>'+row('q')+'</div>'+
  '<div class="cft"><button class="cgo stop" data-act="eqreset"'+(eqFlat(st)?' disabled':'')+'>Reset to flat</button><span class="ct">Drag a point sideways for frequency and up or down for gain. Scroll over a bell, or drag it with Alt held, for Q. Shift gives fine steps; double-click a point to return it to 0 dB.</span></div></div></div>';}
function eqSet(i,c,v){setParam(st,eqKey(i,c),v);st.eqCur=i;}
function eqReset(){for(var i=0;i<EQ_N;i++){eqSet(i,'f',EQ_DEF[i][0]);eqSet(i,'g',0);if(eqBell(i))eqSet(i,'q',EQ_DEF[i][1]);}st.eqCur=null;st.hint=null;st.note='<b>Wet EQ flat.</b> The reverb plays as recorded.';}
/* the ids a drag of one point touches: frequency and gain, or Q alone with Alt held */
function eqIds(i,alt){return alt&&eqBell(i)?[PID[eqKey(i,'q')]]:[PID[eqKey(i,'f')],PID[eqKey(i,'g')]];}
function applyEq(e){var d=drag,sc=d.w/d.rect.width,dx=(e.clientX-d.x0)*sc,dy=(e.clientY-d.y0)*sc,fine=e.shiftKey?0.1:1;
 if(d.alt){eqSet(d.i,'q',d.q0*Math.pow(EQ_Q_SPAN,-dy/d.ph*fine));}
 else{eqSet(d.i,'f',d.f0*Math.pow(EQ_MAX/EQ_MIN,dx/d.pw*fine));eqSet(d.i,'g',d.g0-dy/d.ph*2*EQ_DB*fine);}
 st.note=null;schedule();}

/* ---- the waterfall window: the playing response analysed (energy over time per third-octave
   band, the broadband decay, RT60 per octave) and the live spectrum of the reverb. The plugin
   sends 'analysis' when a new response plays and 'spectrum' each tick while a display is open;
   the live spectrum also sits behind the curve in the Wet EQ window. ---- */
st.wf=false;var ana=null,anaStale=false,dispOpen=false,specCur=[],previewTimer=0;
for(var sb=0;sb<31;sb++)specCur.push(-100);
var SPEC_FALL=1.5; /* dB per tick at 25 Hz, so a bar takes about two seconds to fall away */
/* the parameters whose change rebuilds the response, so a new analysis is on its way */
var REBUILD={lx:1,sx:1,size:1,dist:1,el:1,living:1,rev:1,gate:1,attack:1,dLvl:1,dDel:1,dCol:1,xfeed:1,cpu:1};
function rebuilds(id){var k=keyOf(id);return BAND_ID.indexOf(id)>=0||RBAND_ID.indexOf(id)>=0||id===LINK_ID||!!(k&&REBUILD[k]);}
function wfHTML(){if(!JUCE){var sig=st.sp+'|'+curBands(st).join(',');if(!ana||ana.sig!==sig){ana=fakeAnalysis(st);ana.sig=sig;}}
 var rt=ana?ana.rt60:null,cells='';for(var i=0;i<8;i++){var v=rt?+rt[i]:0,ok=v>0;cells+='<div'+(ok?'':' class="na" title="Not measurable in this response"')+' aria-label="'+BAND_NAMES[i]+', '+(ok?v.toFixed(2)+' seconds':'not measurable')+'"><b>'+(ok?v.toFixed(2):'\u2013')+'</b><span>'+BAND_NAMES[i]+'</span></div>';}
 return '<div class="cap wfd" role="dialog" aria-modal="true" aria-labelledby="wf-h"><div class="cbox"><div class="chd"><h2 id="wf-h">Waterfall</h2><button class="cx" data-act="wf" aria-label="Close">Close</button></div>'+
  '<p>The response you are hearing: each third-octave band falling away over time, the broadband decay with the RT60 measured in each octave, and the live spectrum of the reverb.</p>'+
  '<div class="wfg"><div class="wfp wfw"><div class="wfh"><b>Energy over time</b><span>A ridge per moment, the start at the back, 0 to \u221272 dB</span></div><canvas data-cv="wf" data-w="538" data-h="262" style="width:538px;height:262px" role="img" aria-label="Waterfall of the response: level in each third-octave band from 20 Hz to 20 kHz, one ridge per slice of time"></canvas></div>'+
  '<div class="wfp"><div class="wfh"><b>Decay</b><span>Broadband, dB</span></div>'+decayCurveSVG(ana,292,136)+'</div>'+
  '<div class="wfp"><div class="wfh"><b>RT60 by octave</b><span>Seconds to \u221260 dB</span></div><div class="wfn" role="group" aria-label="Measured RT60 per octave band">'+cells+'</div></div>'+
  '<div class="wfp wfs"><div class="wfh"><b>Live spectrum</b><span>The reverb alone, dB</span></div><canvas data-cv="spec" data-w="848" data-h="100" style="width:848px;height:100px" role="img" aria-label="Live spectrum of the reverb in third-octave bands"></canvas></div></div>'+
  '<div class="cft"><span class="ct" data-wf="note" aria-live="polite"></span></div></div></div>';}
function onAnalysis(m){if(!m||!Array.isArray(m.waterfall)||!Array.isArray(m.decay)||!Array.isArray(m.rt60))return;ana=m;anaStale=false;if(st.wf)schedule();}
function onSpectrum(m){if(!m||!Array.isArray(m.levels))return;for(var i=0;i<31;i++){var v=+m.levels[i];if(!isFinite(v))v=-100;specCur[i]=Math.max(v,specCur[i]-SPEC_FALL);}drawSpectra();}
/* the plugin only analyses and taps the reverb while a display is open */
function syncDisplays(){var want=!!(st.wf||st.eq);if(want===dispOpen)return;dispOpen=want;if(JUCE)JUCE.emitEvent('displays',{open:want});
 if(!JUCE){if(want&&!previewTimer)previewTimer=setInterval(fakeSpectrum,40);else if(!want&&previewTimer){clearInterval(previewTimer);previewTimer=0;}}
 if(!want)for(var i=0;i<31;i++)specCur[i]=-100;}
/* canvases are drawn at the page's real pixel size, so they stay crisp at any editor size */
function cvCtx(cv){var w=+cv.getAttribute('data-w'),h=+cv.getAttribute('data-h'),el=live.firstElementChild,sc=Math.min(3,(window.devicePixelRatio||1)*(el?el.getBoundingClientRect().width/W:1)),bw=Math.max(1,Math.round(w*sc)),bh=Math.max(1,Math.round(h*sc));
 if(cv.width!==bw||cv.height!==bh){cv.width=bw;cv.height=bh;}var c=cv.getContext('2d');c.setTransform(bw/w,0,0,bh/h,0,0);c.clearRect(0,0,w,h);return c;}
function tok(el,name){return getComputedStyle(el).getPropertyValue(name).trim()||'#888';}
function drawDisplays(){var wc=live.querySelector('canvas[data-cv="wf"]');if(wc)drawWaterfall(wc);drawSpectra();wfNote();}
function drawSpectra(){var cs=live.querySelectorAll('canvas[data-cv="spec"],canvas[data-cv="eqspec"]');for(var i=0;i<cs.length;i++)drawSpectrum(cs[i],cs[i].getAttribute('data-cv')==='eqspec');}
function wfNote(){var el=live.querySelector('[data-wf="note"]');if(!el)return;var cv=live.querySelector('canvas[data-cv="wf"]');if(cv)cv.style.opacity=anaStale?'.5':'';
 el.textContent=!ana?'Waiting for the response to play.':anaStale?'Measuring the new response.':'Measured from the response now playing: '+f1(ana.seconds)+' s to \u221260 dB. The spectrum shows the reverb alone, whatever the mix.';}
/* the waterfall: one ridge per slice of time, the start at the back and the tail coming
   forward, so every quieter ridge is in front of the louder one behind it; a nearer ridge
   hides what is behind it, and the accent is strongest where the energy is */
function drawWaterfall(cv){var a=ana,c=cvCtx(cv),w=+cv.getAttribute('data-w'),h=+cv.getAttribute('data-h'),box=cv.closest('.cbox'),ac=tok(box,'--ac'),tx2=tok(box,'--tx2'),p=tok(box,'--p'),ln=tok(box,'--ln');
 var L=36,R=44,T=12,B=20,pw=w-L-R,ph=h-T-B,RANGE=72;c.font='10px '+tok(box,'--fnum');c.textBaseline='middle';
 if(!a){c.fillStyle=tx2;c.textAlign='center';c.fillText('Waiting for the response to play',L+pw/2,T+ph/2);return;}
 var rows=a.waterfall.length,n=a.waterfall[0].length,depth=ph*0.46,amp=ph*0.54,skew=pw*0.14,rw=pw-skew,secs=a.sliceSeconds*(rows-1);
 /* f runs from 0 at the front (the end of the tail) to 1 at the back (the start) */
 function F(r){return rows>1?1-r/(rows-1):1;}function X(f,i){return L+f*skew+rw*i/(n-1);}function BY(f){return T+ph-f*depth;}
 /* the time axis runs down the receding right edge, where no ridge can cover it */
 c.textAlign='left';c.fillStyle=tx2;c.strokeStyle=ln;c.lineWidth=1;
 for(var t=0,stp=timeStep(secs);t<=secs+1e-6;t+=stp){var f=secs?1-t/secs:1,x=X(f,n-1),y=BY(f);c.beginPath();c.moveTo(x,y+0.5);c.lineTo(x+5,y+0.5);c.stroke();c.fillText(t?(+t.toFixed(2))+' s':'0',x+8,y);}
 c.textAlign='right';c.fillText('0 dB',X(1,0)-8,BY(1)-amp);
 for(var r=0;r<rows;r++){var fr=F(r),row=a.waterfall[r],by=BY(fr),top=[],on=[];
  for(var i=0;i<n;i++){var v=clamp((+row[i]+RANGE)/RANGE,0,1);top.push(by-amp*v);on.push(v>0.004);}
  c.beginPath();c.moveTo(X(fr,0),by);for(i=0;i<n;i++)c.lineTo(X(fr,i),top[i]);c.lineTo(X(fr,n-1),by);c.closePath();
  c.globalAlpha=1;c.fillStyle=p;c.fill();c.globalAlpha=0.05+0.13*fr;c.fillStyle=ac;c.fill();
  /* the ridge line stops where the band has reached the floor, so a silent tail stays clean */
  c.beginPath();for(i=0;i<n;i++)c[i&&on[i-1]?'lineTo':'moveTo'](X(fr,i),top[i]);c.globalAlpha=0.35+0.65*fr;c.strokeStyle=ac;c.lineWidth=r?1:1.25;c.stroke();}
 c.globalAlpha=1;c.textAlign='center';c.textBaseline='alphabetic';c.fillStyle=tx2;
 WF_LAB.forEach(function(l){c.fillText(l[1],X(0,l[0]),h-6);});}
/* the live spectrum: a bar per third-octave band; behind the EQ curve it is drawn faintly on
   the curve's own frequency axis */
function drawSpectrum(cv,eq){var c=cvCtx(cv),w=+cv.getAttribute('data-w'),h=+cv.getAttribute('data-h'),box=cv.closest('.cbox'),ac=tok(box,'--ac'),tx2=tok(box,'--tx2'),ln=tok(box,'--ln');
 if(eq){var span=Math.log(1000),ew=w/30*0.55;c.fillStyle=tok(box,'--tx');c.globalAlpha=0.12;
  for(var j=0;j<31;j++){var ex=w*Math.log(BANDS31[j]/20)/span,ey=h*clamp(-specCur[j]/90,0,1);if(ey<h-0.5)c.fillRect(ex-ew/2,ey,ew,h-ey);}return;}
 var L=40,R=12,T=8,B=20,pw=w-L-R,ph=h-T-B,RANGE=80,slot=pw/31,bw=slot*0.62;
 c.font='10px '+tok(box,'--fnum');c.textAlign='right';c.textBaseline='middle';c.strokeStyle=ln;c.lineWidth=1;c.fillStyle=tx2;
 for(var g=0;g>=-RANGE;g-=20){var y=Math.round(T+ph*(-g/RANGE))+0.5;c.beginPath();c.moveTo(L,y);c.lineTo(L+pw,y);c.stroke();c.fillText(g?'\u2212'+(-g):'0',L-8,y);}
 c.fillStyle=ac;for(var i=0;i<31;i++){var yy=T+ph*clamp(-specCur[i]/RANGE,0,1);if(yy<T+ph-0.5)c.fillRect(L+slot*i+(slot-bw)/2,yy,bw,T+ph-yy);}
 c.textAlign='center';c.textBaseline='alphabetic';c.fillStyle=tx2;WF_LAB.forEach(function(l){c.fillText(l[1],L+slot*(l[0]+0.5),h-6);});}
/* Without the plugin (a browser preview) the displays show a plausible response made from the
   decay graph: an exponential fall per band, and a spectrum that swells and settles. */
function fakeAnalysis(s){var b=curBands(s),rt=BANDS31.map(function(f){var x=Math.log(f/63)/Math.LN2;return Math.max(0.1,rtAt(b,x)*(x>7?1-(x-7)*0.2:1));}),secs=Math.max.apply(null,rt)*1.05,sl=secs/39,step=secs/199,wf=[],dec=[],e0=0;
 var shape=function(i){return -Math.abs(i-15)*0.35-(i>24?(i-24)*1.2:0);};
 for(var r=0;r<40;r++){var row=[];for(var i=0;i<31;i++)row.push(Math.round(10*clamp(shape(i)-60*r*sl/rt[i]+(hash(i,r)-0.5)*3,-90,0))/10);wf.push(row);}
 for(var k=0;k<200;k++){var e=0;for(i=0;i<31;i++)e+=Math.pow(10,(shape(i)-60*k*step/rt[i])/10);if(!k)e0=e;dec.push(Math.round(10*10*Math.log10(e/e0))/10);}
 return {seconds:secs,bands:BANDS31.slice(),waterfall:wf,sliceSeconds:sl,decay:dec,decayStep:step,rt60:b.slice()};}
function fakeSpectrum(){var t=Date.now()/1000,env=0.5+0.5*Math.sin(t*0.9),lv=[];for(var i=0;i<31;i++)lv.push(-24-Math.abs(i-14)*1.4-14*(1-env)+(hash(i,Math.floor(t*12))-0.5)*5);onSpectrum({levels:lv});}

/* ---- the space browser: the search, the kind, the reference space of Similar (an index into
   SPACES, or null) and the row the keyboard is on (a position in the results) ---- */
st.brwQ='';st.brwKind='all';st.brwSim=null;st.brwCur=0;
/* the row the keyboard is on, scrolled into view inside the list */
function brwReveal(){var l=live.querySelector('.brw-list'),r=live.querySelector('.brw-row.key');if(!l||!r)return;var t=r.offsetTop,b=t+r.offsetHeight;if(t<l.scrollTop)l.scrollTop=t;else if(b>l.scrollTop+l.clientHeight)l.scrollTop=b-l.clientHeight;}
function brwFocus(){var q=live.querySelector('#brw-q');if(q)q.focus({preventScroll:true});}
/* opening starts a fresh search; the kind and the Similar reference stay, so the next of the
   same kind, or the next closest, is one keypress away */
function openBrowser(){st.browse=true;st.brwQ='';if(st.brwSim!=null&&!SPACES[st.brwSim])st.brwSim=null;var rows=browseResults(st).rows;st.brwCur=0;for(var j=0;j<rows.length;j++)if(rows[j].i===st.sp)st.brwCur=j;}

/* ---- scenes: every setting and the space under a name, recalled here or by automating Scene ---- */
st.scn=false;st.scenes=[];st.scene=0;st.scnLocal={};
var MAX_SCENES=32;
function sceneOf(n){for(var i=0;i<st.scenes.length;i++)if(st.scenes[i].number===n)return st.scenes[i];return null;}
function freeScene(){for(var n=1;n<=MAX_SCENES;n++)if(!sceneOf(n))return n;return 0;}
function setScenes(list){st.scenes=(Array.isArray(list)?list:[]).map(function(x){return {number:+x.number,name:String(x.name||''),space:String(x.space||'')};}).filter(function(x){return x.number>0;});}
/* Without the plugin (a browser preview) scenes are kept on the page. */
function sceneLocal(act,n,name){if(act==='store'){var k=n||freeScene();if(!k)return;var old=sceneOf(k);st.scnLocal[k]=snap(st);
  if(old){old.space=SPACES[st.sp].name;if(name)old.name=name;}else st.scenes.push({number:k,name:name||'Scene '+k,space:SPACES[st.sp].name});st.scenes.sort(function(a,b){return a.number-b.number;});if(!n)st.scene=k;}
 else if(act==='rename'){var r=sceneOf(n);if(r&&name)r.name=name;}
 else if(act==='delete'){st.scenes=st.scenes.filter(function(x){return x.number!==n;});delete st.scnLocal[n];}
 else if(act==='recall'&&st.scnLocal[n]){var v=st.scnLocal[n];Object.keys(v).forEach(function(k){if(k!=='cpu')st[k]=Array.isArray(v[k])?v[k].slice():v[k];});st.scene=n;}}
function sceneAct(act,n,name){var msg={number:n||0,name:name||''};if(JUCE)JUCE.emitEvent('scene'+act.charAt(0).toUpperCase()+act.slice(1),msg);else sceneLocal(act,n,name);}
function scenesHTML(){var rows=st.scenes.map(function(x){var cur=x.number===st.scene,nm=esc(x.name);
  return '<div class="scr" aria-current="'+cur+'"><b>'+x.number+'</b><input data-fid="scn-'+x.number+'" data-scn="'+x.number+'" aria-label="Scene '+x.number+' name" maxlength="64" value="'+nm+'"><span class="scs" title="'+esc(x.space)+'">'+esc(x.space)+'</span>'+
   '<button class="lsm" data-act="screcall" data-v="'+x.number+'" aria-label="Recall '+nm+'">Recall</button><button class="lsm" data-act="scupdate" data-v="'+x.number+'" aria-label="Update '+nm+' to the current settings">Update</button><button class="lsm scx" data-act="scdelete" data-v="'+x.number+'" aria-label="Delete '+nm+'">Delete</button></div>';}).join('');
 var next=freeScene();
 return '<div class="cap scn" role="dialog" aria-modal="true" aria-labelledby="scn-h"><div class="cbox"><div class="chd"><h2 id="scn-h">Scenes</h2><button class="cx" data-act="scenes" aria-label="Close">Close</button></div>'+
  '<p>A scene keeps every setting and the space. Store one for each shot or location, then recall it here, or automate the Scene parameter in your DAW to change rooms on the cut. Freeze and Low CPU stay as they are.</p>'+
  (rows?'<div class="scl">'+rows+'</div>':'<p class="sce">No scenes yet. Set up the room for a shot, then store it.</p>')+
  '<div class="cft"><button class="cgo" data-act="scstore"'+(next?'':' disabled')+'>'+(next?'Store as scene '+next:'All 32 scenes are used')+'</button><span class="ct">Saved with the session. Scene Off leaves the settings alone.</span></div></div></div>';}

/* ---- the licence: a trial, then activation online or with a code made on another device ---- */
st.lic=false;st.licOff=false;st.licKey='';st.licCode='';
st.licence={state:'trial',daysLeft:14,name:'',licence:'',machine:'',page:'',busy:false,message:'',error:false,testKey:false};
function onLicence(m){if(!m)return;var was=st.licence.state;st.licence=Object.assign({},st.licence,m);
 if(st.licence.state==='licensed'&&was!=='licensed'){st.licKey='';st.licCode='';st.licOff=false;}schedule();}
function onPasted(m){if(!m||(m.target!=='licKey'&&m.target!=='licCode'))return;st[m.target]=String(m.text||'').trim();renderLive();
 var f=live.querySelector(m.target==='licKey'?'#lic-key':'#lic-code');if(f){f.focus({preventScroll:true});f.selectionStart=f.selectionEnd=f.value.length;}}
function licenceHTML(){var L=st.licence,busy=!!L.busy,dis=busy?' disabled':'',body;
 var msg='<p class="lmsg'+(L.error?' err':'')+'" role="'+(L.error?'alert':'status')+'">'+esc(L.message||'')+'</p>';
 if(L.state==='licensed'){
  body='<p>'+(L.name?'Licensed to '+esc(L.name)+' on this computer.':'Licensed on this computer.')+' It works offline from now on.</p>'+
   '<dl class="lkv"><dt>Licence number</dt><dd>'+esc(L.licence)+'</dd><dt>This computer</dt><dd>'+esc(L.machine)+'</dd></dl>'+msg+
   '<div class="cft"><button class="cgo stop" data-act="licoff"'+dis+'>Deactivate this computer</button><span class="ct">Frees one of the licence\u2019s three places, so you can move Drenched to another computer.</span></div>';}
 else{
  body='<p>'+(L.state==='ended'?'The trial has ended, so the reverb is muted and only the dry sound plays. Enter the licence number from your purchase email to bring it back.'
    :days(L.daysLeft)+' of the trial left. Enter the licence number from your purchase email to keep Drenched after that. Each licence works on three computers.')+'</p>'+
   '<label class="ll" for="lic-key">Licence number</label><div class="lrow"><input id="lic-key" data-fid="lic-key" data-in="licKey" autocomplete="off" spellcheck="false" placeholder="XXXXX-XXXXX-XXXXX-XXXXX-XXXXX" value="'+esc(st.licKey)+'"'+dis+'>'+
   '<button class="lsm" data-act="licpaste" data-v="licKey"'+dis+'>Paste</button><button class="cgo" data-act="licgo"'+dis+'>Activate</button></div>'+msg+
   '<button class="ldisc" data-act="licoffline" aria-expanded="'+st.licOff+'">Activate without internet</button>'+
   (st.licOff?'<ol class="lsteps"><li>On any device with internet, open <b>'+esc(L.page)+'</b>'+(JUCE?' <button class="lsm" data-act="licpage">Open</button>':'')+'</li>'+
    '<li>Enter your licence number and this computer\u2019s code:<span class="lcode"><b class="lmono">'+esc(L.machine)+'</b><button class="lsm" data-act="liccopy">Copy</button></span></li>'+
    '<li>Paste the activation code it gives you:<div class="lrow top"><textarea id="lic-code" data-fid="lic-code" data-in="licCode" spellcheck="false" aria-label="Activation code" placeholder="DRN1.\u2026"'+dis+'>'+esc(st.licCode)+'</textarea>'+
    '<button class="lsm" data-act="licpaste" data-v="licCode"'+dis+'>Paste</button><button class="cgo" data-act="liccode"'+dis+'>Use code</button></div></li></ol>':'');}
 var dev=L.testKey?'<p class="ldev">Development build: its activation codes use the published test key, so this copy is not for sale.</p>':'';
 return '<div class="cap lic" role="dialog" aria-modal="true" aria-labelledby="lic-h"><div class="cbox"><div class="chd"><h2 id="lic-h">'+(L.state==='licensed'?'Licence':'Activate Drenched')+'</h2><button class="cx" data-act="licence" aria-label="Close">Close</button></div>'+body+dev+'</div></div>';}
function licActivate(){if(JUCE)JUCE.emitEvent('licenceActivate',{licence:st.licKey});else st.licence=Object.assign({},st.licence,{message:'Activation works inside the plugin.',error:true});}

/* ---- rendering ---- */
function fit(){var w=live.clientWidth,h=live.clientHeight,sc=Math.min(w/W,h/H),el=live.firstElementChild;if(!el)return;
 el.style.transform='translate('+((w-W*sc)/2).toFixed(2)+'px,'+((h-H*sc)/2).toFixed(2)+'px) scale('+sc.toFixed(5)+')';
 var frs=live.querySelectorAll('.skins .fr');for(var i=0;i<frs.length;i++){var s2=frs[i].firstElementChild;if(s2)s2.style.transform='scale('+(frs[i].clientWidth/W).toFixed(5)+')';}
 if(st.wf||st.eq)drawDisplays();}
/* Replacing the page blurs an edited field, whose change handler redraws too: that redraw
   waits for this one to finish, as two at once fail. */
var rendering=false,renderAgain=false;
function renderLive(){if(rendering){renderAgain=true;return;}rendering=true;try{drawLive();}finally{rendering=false;}if(renderAgain){renderAgain=false;renderLive();}}
function drawLive(){var a=document.activeElement,fid=a&&live.contains(a)?a.getAttribute('data-fid'):null,bl=live.querySelector('.brw-list'),blt=bl?bl.scrollTop:0;
 var html=screenHTML(st.skin,st);if(st.capture)html=html.slice(0,-6)+captureHTML()+'</div>';if(st.lic)html=html.slice(0,-6)+licenceHTML()+'</div>';if(st.credits)html=html.slice(0,-6)+creditsHTML()+'</div>';if(st.spk)html=html.slice(0,-6)+speakersHTML()+'</div>';if(st.scn)html=html.slice(0,-6)+scenesHTML()+'</div>';if(st.eq)html=html.slice(0,-6)+eqHTML()+'</div>';if(st.wf)html=html.slice(0,-6)+wfHTML()+'</div>';if(st.skins)html=html.slice(0,-6)+pickerHTML()+'</div>';
 live.innerHTML=html;fit();syncDisplays();
 if(blt){var bl2=live.querySelector('.brw-list');if(bl2)bl2.scrollTop=blt;}
 if(fid){var n=live.querySelector('[data-fid="'+fid+'"]');if(n){n.focus({preventScroll:true});if(n.matches('input,textarea'))n.selectionStart=n.selectionEnd=n.value.length;}}
 // A dialog keeps the keyboard even when the control that had it is redrawn away.
 if(st.browse&&!live.contains(document.activeElement))brwFocus();
 if((st.lic||st.capture||st.credits||st.spk||st.scn||st.eq||st.wf)&&!live.contains(document.activeElement)){var d=live.querySelector('.cap .cgo:not([disabled])')||live.querySelector('.cap .cx');if(d)d.focus({preventScroll:true});}}
var raf=0;function schedule(){if(!raf)raf=requestAnimationFrame(function(){raf=0;renderLive();sync();syncUi();});}
window.schedule=schedule;
window.addEventListener('resize',fit);
if(window.ResizeObserver)new ResizeObserver(fit).observe(live);

/* ---- interaction ---- */
function snap(s){var o={sp:s.sp,bands:s.bands.slice(),rbands:s.rbands.slice(),link:s.link};for(var k in PID)if(k!=='freeze')o[k]=s[k];return o;}
/* a new space brings its curve to both sides; the link state stays as it was */
function changeSpace(i){var old=snap(st),sp=SPACES[i],held=[];st.sp=i;st.bands=sp.bands.slice();st.rbands=sp.bands.slice();st.pre=sp.pre;st.size=100;
 if(st.locks.decay){var f=decayOf(old.bands)/decayOf(sp.bands),sc=function(b){return clamp(b*f,0.1,20);};st.bands=st.bands.map(sc);st.rbands=st.rbands.map(sc);held.push('Decay');}
 if(st.locks.pre){st.pre=old.pre;held.push('Pre-delay');}if(st.locks.size){st.size=old.size;held.push('Size');}
 ['mix','bright','el','dist'].forEach(function(k){if(st.locks[k])held.push(P[k].l);});
 st.hint=null;st.note='<b>'+esc(sp.name)+'</b> loaded.'+(held.length?' Locked and held: '+held.join(', ')+'.':' Lock a value to hold it when you change space.')+(sp.credit?' Recording: '+esc(sp.credit)+', '+esc(sp.licence)+'.':'');return held;}
var drag=null,eqTap=null;
live.addEventListener('pointerdown',function(e){var t=e.target;if(t.closest('input,button'))return;
 var kn=t.closest('[data-knob]'),lis=t.closest('[data-drag]'),dg=t.closest('svg[data-decay]'),en=t.closest('[data-eqn]');
 if(en){var es=en.closest('svg'),ei=+en.getAttribute('data-eqn'),alt=(e.altKey||e.ctrlKey)&&eqBell(ei),now=Date.now();
  // the page is redrawn between the two clicks, so a double click is counted here: it returns the band to 0 dB
  if(eqTap&&eqTap.i===ei&&now-eqTap.t<400){eqTap=null;eqSet(ei,'g',0);st.note=null;schedule();e.preventDefault();return;}eqTap={i:ei,t:now};
  drag={type:'eq',i:ei,alt:alt,rect:es.getBoundingClientRect(),w:+es.getAttribute('data-w'),pw:+es.getAttribute('data-pw'),ph:+es.getAttribute('data-ph'),x0:e.clientX,y0:e.clientY,f0:st[eqKey(ei,'f')],g0:st[eqKey(ei,'g')],q0:st[eqKey(ei,'q')],ids:eqIds(ei,alt)};
  st.eqCur=ei;en.focus({preventScroll:true});schedule();}
 else if(kn){var k=kn.getAttribute('data-knob'),h=kn.getAttribute('data-h')==='1';drag={type:'knob',k:k,h:h,x0:e.clientX,y0:e.clientY,n0:to01(k,getV(st,k)),w:kn.getBoundingClientRect().width*(W/live.firstElementChild.getBoundingClientRect().width),ids:k==='decay'?(st.link?BAND_ID.slice():BAND_ID.concat(RBAND_ID)):[PID[k]]};kn.focus({preventScroll:true});}
 else if(lis){var sv=lis.closest('svg');drag={type:'lis',rect:sv.getBoundingClientRect(),w:+sv.getAttribute('data-w'),mode:sv.getAttribute('data-mode'),ox:+sv.getAttribute('data-ox'),oy:+sv.getAttribute('data-oy'),sc:+sv.getAttribute('data-sc'),rw:+sv.getAttribute('data-rw'),ids:lis.getAttribute('data-drag')==='src'?[PID.sx]:[PID.dist,PID.lx],src:lis.getAttribute('data-drag')==='src'};lis.focus({preventScroll:true});}
 else if(dg){var r=dg.getBoundingClientRect(),d={rect:r,l:+dg.getAttribute('data-l'),t:+dg.getAttribute('data-t'),pw:+dg.getAttribute('data-pw'),ph:+dg.getAttribute('data-ph'),ymax:+dg.getAttribute('data-ymax'),w:+dg.getAttribute('data-w'),bars:dg.getAttribute('data-bars')==='1'};
  var sx=(e.clientX-r.left)*d.w/r.width,sy=(e.clientY-r.top)*d.w/r.width;d.polar=dg.getAttribute('data-polar')==='1';d.horiz=dg.getAttribute('data-horiz')==='1';
  if(d.polar){d.cx=+dg.getAttribute('data-cx');d.cy=+dg.getAttribute('data-cy');d.r0=+dg.getAttribute('data-r0');d.r1=+dg.getAttribute('data-r1');var dd=Math.hypot(sx-d.cx,sy-d.cy);if(dd<d.r0-24||dd>d.r1+40)return;var ang=Math.atan2(sy-d.cy,sx-d.cx)*180/Math.PI;d.i=((Math.round((ang+90)/45)%8)+8)%8;}
  else if(d.horiz){d.rh=+dg.getAttribute('data-rh');if(sy<d.t||sy>d.t+d.ph+d.rh)return;d.i=clamp(Math.round((sy-d.t)/d.rh)-1,0,7);}
  else{if(sx<d.l||sx>d.l+d.pw)return;d.i=d.bars?clamp(Math.floor((sx-d.l)/(d.pw/8)),0,7):clamp(Math.round((sx-d.l)/(d.pw/7)),0,7);}
  d.type='decay';d.ids=[bandId(st,d.i)];drag=d;st.lastBand=d.i;applyDecay(e);
  var nd=live.querySelector('[data-fid="nd-'+d.i+'"]');if(nd)nd.focus({preventScroll:true});}
 else return;
 gesture(drag.ids,true);
 try{live.setPointerCapture(e.pointerId);}catch(_){}e.preventDefault();});
function applyDecay(e){var d=drag,sc=d.w/d.rect.width,sx=(e.clientX-d.rect.left)*sc,sy=(e.clientY-d.rect.top)*sc,v;if(d.polar)v=(Math.hypot(sx-d.cx,sy-d.cy)-d.r0)/(d.r1-d.r0)*d.ymax;else if(d.horiz)v=(sx-d.l)/d.pw*d.ymax;else v=(1-(sy-d.t)/d.ph)*d.ymax;v=clamp(v,0.1,d.ymax);curBands(st)[d.i]=Math.round(v*100)/100;st.hint='decay';st.note=null;schedule();}
live.addEventListener('pointermove',function(e){if(!drag)return;
 if(drag.type==='knob'){var k=live.firstElementChild.getBoundingClientRect().width/W,dn=drag.h?(e.clientX-drag.x0)/k/Math.max(drag.w,60):(drag.y0-e.clientY)/k/220;if(e.shiftKey)dn/=10;setParam(st,drag.k,from01(drag.k,drag.n0+dn));schedule();}
 else if(drag.type==='decay')applyDecay(e);
 else if(drag.type==='eq')applyEq(e);
 else if(drag.type==='lis'){var r=drag.rect,kk=drag.w/r.width,px=(e.clientX-r.left)*kk,py=(e.clientY-r.top)*kk;
  if(drag.src){if(drag.mode==='plan')st.sx=clamp(Math.round((px-drag.ox)/(drag.rw*drag.sc)*1000)/10,6,94);st.hint='sx';st.note=null;schedule();return;}
  if(drag.mode==='plan'){st.lx=clamp(Math.round((px-drag.ox)/(drag.rw*drag.sc)*1000)/10,6,94);st.dist=Math.round(distFromM(st,(py-drag.oy)/drag.sc));}else st.dist=Math.round(distFromM(st,(px-drag.ox)/drag.sc));
  st.hint='dist';st.note=null;schedule();}});
function endDrag(){if(drag){var ids=drag.ids;drag=null;schedule();gesture(ids,false);}}
live.addEventListener('pointerup',endDrag);live.addEventListener('pointercancel',endDrag);
/* on a bell, the wheel sets Q */
live.addEventListener('wheel',function(e){var en=e.target.closest&&e.target.closest('[data-eqn]');if(!en||drag)return;var i=+en.getAttribute('data-eqn');e.preventDefault();if(!eqBell(i)||!e.deltaY)return;
 eqSet(i,'q',st[eqKey(i,'q')]*Math.pow(e.shiftKey?1.015:1.06,e.deltaY<0?1:-1));st.note=null;schedule();},{passive:false});

live.addEventListener('keydown',function(e){var t=e.target,keys={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1,PageUp:10,PageDown:-10};
 if(e.key==='Escape'&&(st.skins||st.browse||st.capture||st.lic||st.credits||st.spk||st.scn||st.eq||st.wf)){if(st.skins||st.browse){st.skins=false;st.browse=false;}else if(st.wf)st.wf=false;else if(st.eq)st.eq=false;else if(st.scn)st.scn=false;else if(st.spk)st.spk=false;else if(st.credits)st.credits=false;else if(st.lic)st.lic=false;else st.capture=false;e.preventDefault();renderLive();return;}
 var bw=st.browse&&t.closest&&t.closest('.brw');
 if(bw){var R=browseResults(st),n=R.rows.length,bc=clamp(st.brwCur||0,0,Math.max(0,n-1)),mv={ArrowDown:1,ArrowUp:-1,PageDown:8,PageUp:-8}[e.key];
  if(mv){if(n){st.brwCur=clamp(bc+mv,0,n-1);renderLive();brwReveal();}e.preventDefault();return;}
  if(e.key==='Enter'&&t.id==='brw-q'){e.preventDefault();if(!n)return;var br=R.rows[bc];
   if(e.shiftKey){st.brwSim=br.i;st.brwCur=0;renderLive();brwReveal();}
   else{st.browse=false;changeSpace(br.i);renderLive();sync();syncUi();var ob=live.querySelector('[data-act="browse"]');if(ob)ob.focus({preventScroll:true});}return;}}
 if(e.key==='Enter'&&t.id==='lic-key'){e.preventDefault();licActivate();renderLive();return;}
 if(e.key==='Enter'&&t.matches('input[data-scn]')){e.preventDefault();t.blur();return;}
 if(t.matches('.sk')&&(e.key==='Enter'||e.key===' ')){e.preventDefault();t.click();return;}
 // a captured position marked on a drawing is an SVG button: Enter and Space press it
 if(t.matches('svg [data-act="pos"]')&&(e.key==='Enter'||e.key===' ')){e.preventDefault();t.dispatchEvent(new MouseEvent('click',{bubbles:true,cancelable:true}));return;}
 if(t.matches('[data-knob]')){var k=t.getAttribute('data-knob'),p=P[k];if(e.key==='Home')setParam(st,k,p.min);else if(e.key==='End')setParam(st,k,p.max);else if(keys[e.key])setParam(st,k,from01(k,to01(k,getV(st,k))+keys[e.key]*(e.shiftKey?0.002:0.02)));else return;e.preventDefault();schedule();}
 else if(t.matches('[data-eqn]')){var qi=+t.getAttribute('data-eqn'),fine=e.shiftKey,ek=e.key;st.eqCur=qi;
  if(ek==='ArrowLeft'||ek==='ArrowRight')eqSet(qi,'f',st[eqKey(qi,'f')]*Math.pow(2,(ek==='ArrowRight'?1:-1)/(fine?48:12)));
  else if(ek==='ArrowUp'||ek==='ArrowDown')eqSet(qi,'g',st[eqKey(qi,'g')]+(ek==='ArrowUp'?1:-1)*(fine?0.1:0.5));
  else if((ek==='PageUp'||ek==='PageDown')&&eqBell(qi))eqSet(qi,'q',st[eqKey(qi,'q')]*Math.pow(fine?1.015:1.12,ek==='PageUp'?1:-1));
  else if(ek==='Delete'||ek==='Backspace')eqSet(qi,'g',0);
  else if(ek==='Home'||ek==='End'){var nx=ek==='End'?EQ_N-1:0;st.eqCur=nx;renderLive();var nn=live.querySelector('[data-fid="eqn-'+nx+'"]');if(nn)nn.focus({preventScroll:true});e.preventDefault();return;}
  else return;st.note=null;e.preventDefault();schedule();}
 else if(t.matches('[data-node]')){var i=+t.getAttribute('data-node');if(e.key==='ArrowLeft'||e.key==='ArrowRight'){var ni=clamp(i+(e.key==='ArrowRight'?1:-1),0,7);st.lastBand=ni;e.preventDefault();renderLive();var n2=live.querySelector('[data-fid="nd-'+ni+'"]');if(n2)n2.focus({preventScroll:true});return;}
  if(e.key==='ArrowUp'||e.key==='ArrowDown'){var cb=curBands(st);cb[i]=clamp(Math.round((cb[i]+(e.key==='ArrowUp'?1:-1)*(e.shiftKey?0.01:0.1))*100)/100,0.1,20);st.lastBand=i;st.hint='decay';st.note=null;e.preventDefault();schedule();}}
 else if(t.matches('[data-drag="src"]')){if(e.key==='ArrowLeft'||e.key==='ArrowRight')st.sx=clamp(st.sx+(e.key==='ArrowRight'?3:-3),6,94);else return;st.hint='sx';st.note=null;e.preventDefault();schedule();}
 else if(t.matches('[data-drag]')){if(e.key==='ArrowUp'||e.key==='ArrowDown')st.dist=clamp(st.dist+(e.key==='ArrowDown'?2:-2),0,100);else if(e.key==='ArrowLeft'||e.key==='ArrowRight')st.lx=clamp(st.lx+(e.key==='ArrowRight'?3:-3),6,94);else return;st.hint='dist';st.note=null;e.preventDefault();schedule();}});
live.addEventListener('change',function(e){var t=e.target;
 if(t.matches('input[data-spk]')){var si=+t.getAttribute('data-spk'),lv=t.getAttribute('data-f')==='l',sr=t.value.replace(/\u2212/g,'-'),sn=sr.match(/-?\d+(\.\d+)?/),cur=speakerOf(st,si).slice();
  if(lv)cur[0]=/off/i.test(sr)?SPK_LEVEL.min:sn?clamp(Math.round(parseFloat(sn[0])*10)/10,SPK_LEVEL.min,SPK_LEVEL.max):cur[0];else if(sn)cur[1]=clamp(Math.round(parseFloat(sn[0])*10)/10,SPK_DELAY.min,SPK_DELAY.max);
  while(st.speakers.length<=si)st.speakers.push([0,0]);st.speakers[si]=cur;renderLive();syncUi();return;}
 if(t.matches('input[data-scn]')){var rn=+t.getAttribute('data-scn'),nv=t.value.trim();if(nv)sceneAct('rename',rn,nv.slice(0,64));renderLive();return;}
 if(!t.matches('input[data-type]'))return;var ty=t.getAttribute('data-type'),raw=t.value.replace(/\u2212/g,'-'),nums=raw.match(/-?\d+(\.\d+)?/g);
 if((ty==='lx'||ty==='sx')&&/cent/i.test(raw)){setParam(st,ty,50);st.note=null;renderLive();sync();return;}
 if(!nums){if(/off/i.test(raw)&&P[ty]&&(P[ty].min===0||ty==='dLvl'))setParam(st,ty,P[ty].min);st.note=null;renderLive();sync();return;}var v=parseFloat(nums[nums.length-1]);
 if(ty==='band'){var i=+t.getAttribute('data-i');curBands(st)[i]=clamp(v,0.1,20);st.lastBand=i;st.hint='decay';}
 else if(ty==='dist'){st.dist=Math.round(distFromM(st,v));st.hint='dist';}
 else if((ty==='lx'||ty==='sx')&&/left|right/i.test(raw)){setParam(st,ty,50+(/left/i.test(raw)?-v:v)/2);}
 else if((ty==='hicut'||/^eq\df$/.test(ty))&&/k/i.test(raw)){setParam(st,ty,v*1000);}
 else setParam(st,ty,v);
 st.note=null;renderLive();sync();});
live.addEventListener('input',function(e){var k=e.target.getAttribute&&e.target.getAttribute('data-in');if(k==='licKey'||k==='licCode')st[k]=e.target.value;
 if(e.target.id==='brw-q'){var ss=e.target.selectionStart;st.brwQ=e.target.value;st.brwCur=0;renderLive();var q=live.querySelector('#brw-q');if(q){q.focus({preventScroll:true});q.selectionStart=q.selectionEnd=ss;}brwReveal();}});
live.addEventListener('focusin',function(e){if(e.target.matches('input.ro'))e.target.select();});
live.addEventListener('click',function(e){var b=e.target.closest('[data-act]');
 if(!b){if(st.browse&&!e.target.closest('.brw .cbox')){st.browse=false;renderLive();}return;}
 var a=b.getAttribute('data-act'),v=b.getAttribute('data-v');if(a!=='browse'&&a!=='pick'&&a.indexOf('brw')!==0)st.browse=false;
 switch(a){
  case 'mode':st.mode=v;st.note=v==='stage'?'<b>Stage view</b> for post: large numerals, channel meters and locks, on the same tokens.':'<b>Studio view</b>: the space and its decay lead.';st.hint=null;break;
  case 'slot':if(v!==st.slot){st.slots[st.slot]=snap(st);var nx=st.slots[v]||snap(st);Object.keys(nx).forEach(function(k){st[k]=Array.isArray(nx[k])?nx[k].slice():nx[k];});st.slot=v;st.hint=null;st.note='<b>Snapshot '+v+'.</b> Changes now go to '+v+'. Switch back to compare.';}break;
  case 'freeze':st.freeze=!st.freeze;st.hint=null;st.note=null;break;
  case 'toggle':var tk=b.getAttribute('data-k');st[tk]=st[tk]>=0.5?0:1;st.hint=tk;st.note=null;break;
  case 'adv':st.adv=!st.adv;st.hint=null;st.note=st.adv?'<b>All parameters</b> opens in place, so the space and graph stay in view.':null;break;
  case 'match':st.hint=null;if(JUCE){JUCE.emitEvent('match',{});st.note='<b>Match a room.</b> Choose a recording made in the room you want to match: a voice, a band or a drum kit, with pauses where the sound stops and the room rings on.';}else st.note='<b>Match a room</b> listens to a recording made in a room and sets the decay to match it.';break;
  /* unlinking first gives the right the curve it already plays, so nothing changes at that moment */
  case 'dlink':st.hint=null;if(st.link){st.rbands=st.bands.slice();sync();st.link=false;st.side='L';st.note='<b>Unlinked.</b> The left and right now decay at their own curves. Choose a side and drag its points; the other side shows faintly.';}
   else{st.link=true;st.side='L';st.note='<b>Linked.</b> Both sides decay at the left curve again.';}break;
  case 'dside':st.side=v==='R'?'R':'L';st.hint=null;st.note='<b>'+sideName(st)+' side.</b> The graph, its points and the values now edit the '+sideName(st).toLowerCase()+' decay; the '+(st.side==='R'?'left':'right')+' shows faintly.';break;
  case 'lock':var lk=b.getAttribute('data-k');st.locks[lk]=!st.locks[lk];st.hint=null;st.note=(st.locks[lk]?'<b>Locked.</b> ':'<b>Unlocked.</b> ')+P[lk].l+(st.locks[lk]?' now holds when you change space.':' follows each space again.');break;
  case 'view':st.view=v;break;
  case 'browse':if(st.browse)st.browse=false;else openBrowser();break;
  case 'brwkind':st.brwKind=v;st.brwCur=0;break;
  case 'brwsim':st.brwSim=v==='off'?null:+v;st.brwCur=0;break;
  case 'brwclear':st.brwQ='';st.brwKind='all';st.brwCur=0;break;
  case 'findir':st.browse=false;st.hint=null;if(JUCE){JUCE.emitEvent('loadIr',{keep:true});st.note='Choose the file this session used. Your decay and other settings stay as they are.';}else st.note='<b>Find</b> opens a file chooser in the plugin.';break;
  case 'loadir':st.browse=false;st.hint=null;if(JUCE){JUCE.emitEvent('loadIr',{});st.note='Choose a WAV, AIFF, FLAC or SOFA impulse response: mono, stereo, true stereo, surround up to 9.1.6, or B-format.';}else st.note='<b>Load your own IR</b> opens a file chooser in the plugin.';break;
  case 'credits':st.credits=!st.credits;st.browse=false;st.hint=null;break;
  case 'report':st.browse=false;st.hint=null;if(JUCE){JUCE.emitEvent('copyReport',{});st.note='<b>Copied</b> the details of this setup: version, format, host, computer, audio settings, space and look. Paste them into your bug report with what you did, what you expected and what happened.';}else st.note='<b>Report a bug</b> copies this setup\'s details in the plugin.';break;
  case 'speakers':st.spk=!st.spk;st.browse=false;st.hint=null;break;
  case 'spkreset':st.speakers=[];break;
  case 'scenes':st.scn=!st.scn;st.browse=false;st.hint=null;break;
  case 'eq':st.eq=!st.eq;st.browse=false;st.hint=null;break;
  case 'wf':st.wf=!st.wf;st.browse=false;st.hint=null;break;
  case 'eqreset':eqReset();break;
  case 'scstore':sceneAct('store',0,'');st.hint=null;st.note='<b>Stored.</b> Automate Scene to recall it on the cut.';break;
  case 'scupdate':sceneAct('store',+v,'');st.hint=null;st.note='<b>'+esc((sceneOf(+v)||{}).name||'Scene '+v)+'</b> now holds the current settings.';break;
  case 'screcall':sceneAct('recall',+v,'');break;
  case 'scdelete':sceneAct('delete',+v,'');st.hint=null;st.note='<b>Scene '+v+' deleted.</b> The others keep their numbers, so their automation still works.';break;
  case 'capture':st.capture=!st.capture;st.hint=null;if(st.capture&&!capBusy()&&st.capStatus.stage!=='failed')st.capStatus.message='';break;
  case 'capopt':var ck=b.getAttribute('data-k');if(CAP_OPTS[ck]&&!capBusy())st.cap[ck]=ck==='method'?v:+v;break;
  case 'capstart':if(JUCE){st.capStatus={stage:'playing',progress:0,peakDb:-100,message:''};JUCE.emitEvent('startCapture',{method:st.cap.method,listenSeconds:st.cap.listen,speakers:st.cap.speakers,sweepSeconds:st.cap.sweep,tailSeconds:st.cap.tail,levelDb:st.cap.level});}
   else st.capStatus={stage:'failed',progress:0,peakDb:-100,message:'Capturing works inside the plugin, where Drenched can reach your speakers and microphone.'};break;
  case 'capstop':if(JUCE)JUCE.emitEvent('cancelCapture',{});break;
  case 'licence':st.lic=!st.lic;st.hint=null;if(st.lic){st.licence.message='';if(JUCE)JUCE.emitEvent('licenceOpen',{});}break;
  case 'licgo':licActivate();break;
  case 'liccode':if(JUCE)JUCE.emitEvent('licenceCode',{code:st.licCode});break;
  case 'licoff':if(JUCE)JUCE.emitEvent('licenceDeactivate',{});break;
  case 'licoffline':st.licOff=!st.licOff;break;
  case 'licpage':if(JUCE)JUCE.emitEvent('licencePage',{});break;
  case 'liccopy':if(JUCE)JUCE.emitEvent('copyText',{text:st.licence.machine});st.licence.message='Computer code copied.';st.licence.error=false;break;
  case 'licpaste':if(JUCE)JUCE.emitEvent('pasteText',{target:v});break;
  case 'pick':st.browse=false;changeSpace(+v);break;
  /* another captured position of the same room: the engine lets the old one ring out while
     the new one takes the sound; standing still does nothing */
  case 'pos':if(+v!==st.sp){var ps=SPACES[+v],held=changeSpace(+v);if(ps&&ps.position)st.note='<b>'+esc(ps.position)+(ps.atM!=null?', '+f1(ps.atM)+' m from the stage':'')+'.</b> '+esc(ps.mics)+(ps.mics?', ':'')+esc(ps.name)+'. '+(held.length?'Locked and held: '+held.join(', ')+'.':'The old position rings out while this one takes the sound.');}break;
  case 'prev':changeSpace((st.sp+SPACES.length-1)%SPACES.length);break;
  case 'next':changeSpace((st.sp+1)%SPACES.length);break;
  case 'skins':st.skins=!st.skins;break;
  case 'skin':if(SKINS.some(function(x){return x.id===v;})){st.skin=v;st.note='<b>'+esc(skinOf(v).name)+'.</b> Click the Drenched name to change the look again.';st.hint=null;}st.skins=false;break;
 }
 renderLive();sync();syncUi();
 if(a==='browse'&&st.browse){brwFocus();brwReveal();}
 else if(a==='brwsim'||a==='brwclear'){brwFocus();brwReveal();}
 else if(a==='skins'&&st.skins){var sk=live.querySelector('.skins .sk[aria-current="true"]');if(sk)sk.focus({preventScroll:true});}
 else if(a==='capture'&&st.capture){var cf=live.querySelector('.cap .cgo');if(cf)cf.focus({preventScroll:true});}
 else if(a==='speakers'&&st.spk){var sf=live.querySelector('.spkd input');if(sf)sf.focus({preventScroll:true});}
 else if(a==='eq'&&st.eq){var ef=live.querySelector('.eqd [data-eqn="'+(st.eqCur||0)+'"]');if(ef)ef.focus({preventScroll:true});}
 else if(a==='wf'&&st.wf){var wfc=live.querySelector('.wfd .cx');if(wfc)wfc.focus({preventScroll:true});}
 else if(a==='licence'&&st.lic){var lf=live.querySelector('#lic-key')||live.querySelector('.lic .cgo');if(lf)lf.focus({preventScroll:true});}
 else if(a==='capstart'||a==='capstop'){var cg=live.querySelector('.cap .cgo:not([disabled])')||live.querySelector('.cap .cx');if(cg)cg.focus({preventScroll:true});}
 else{var same=live.querySelector('[data-act="'+a+'"]'+(v!=null?'[data-v="'+v+'"]':'')+(a==='lock'||a==='toggle'||a==='capopt'?'[data-k="'+b.getAttribute('data-k')+'"]':''));if(same)same.focus({preventScroll:true});}});
/* the host may hand us a context menu on right click; the interface has none */
live.addEventListener('contextmenu',function(e){e.preventDefault();});

/* ---- start ---- */
/* Without the plugin (a browser preview) a few recorded spaces stand in for the library, as
   the plugin would pass them from library/library.json: the rooms captured from more than
   one place, so the positions can be seen, and one open-air and one underground space, so
   their drawings can be seen. */
var PREVIEW_CREDIT={credit:'Hyunkook Lee and Connor Millns, Applied Psychoacoustics Lab, University of Huddersfield: MAIR library (2017)',licence:'CC BY 4.0',source:'https://github.com/APL-Huddersfield/MAIR-Library-and-Renderer',kind:'hall',img:'hall',place:'Concert hall, University of Huddersfield',mat:'Size estimated from the recording',date:'MAIR library, 2017',no:'Hall 01',W:13.6,D:19,H:7.5,pre:0,room:'stpauls'};
var PREVIEW_LIBRARY=[
 Object.assign({id:'stpauls-ortf',name:'St Paul\u2019s Hall, ORTF',rec:'ORTF pair, 3.9 m from the stage, true stereo',bands:[2.11,2.48,2.53,2.35,2.16,1.88,1.35,0.72],position:'Stage front',mics:'ORTF pair',atM:3.9},PREVIEW_CREDIT),
 Object.assign({id:'stpauls-spaced',name:'St Paul\u2019s Hall, Spaced omnis',rec:'Spaced omni pair, 3.9 m from the stage, true stereo',bands:[2.18,2.44,2.51,2.36,2.14,1.83,1.34,0.79],position:'Stage front',mics:'Spaced omnis',atM:3.9},PREVIEW_CREDIT),
 Object.assign({id:'stpauls-decca',name:'St Paul\u2019s Hall, Decca tree',rec:'Decca tree, 3.9 m from the stage, true stereo',bands:[2.43,2.43,2.44,2.31,2.16,1.84,1.34,0.72],position:'Stage front',mics:'Decca tree',atM:3.9},PREVIEW_CREDIT),
 Object.assign({id:'stpauls-back',name:'St Paul\u2019s Hall, Back of stage',rec:'Decca tree, sources at the back of the stage, true stereo',bands:[3.16,2.4,2.49,2.34,2.15,1.85,1.37,0.84],position:'Players at the back of the stage',mics:'Decca tree'},PREVIEW_CREDIT),
 Object.assign({id:'stpauls-hall',name:'St Paul\u2019s Hall, In the hall',rec:'Rear-facing cardioids, 7 m from the stage',bands:[2.45,2.47,2.5,2.39,2.09,1.81,1.34,0.77],position:'In the hall',mics:'Rear-facing cardioids',atM:7},PREVIEW_CREDIT),
 {id:'detmold-konzerthaus',name:'Detmold Konzerthaus, Front stalls',no:'Hall 04',rec:'Dummy head (binaural), front stalls, sources 4 m apart, true stereo',kind:'hall',img:'hall',place:'Concert hall, about 600 seats, Detmold University of Music',mat:'Size estimated from the recording',date:'Detmold SRIR database, 2020',credit:'Sebastia Vicenc Amengual Gari, Banu Sahin, Dusty Eddy and Malte Kob, Detmold University of Music: Detmold SRIR database (2020)',licence:'CC BY 4.0',source:'https://zenodo.org/records/4116247',bands:[2.07,1.8,1.6,1.59,1.6,1.51,1.3,0.87],W:15.4,D:21.6,H:8.5,pre:0,room:'detmold-konzerthaus',position:'Front stalls',mics:'Dummy head'},
 {id:'detmold-konzerthaus-mid',name:'Detmold Konzerthaus, Mid hall',no:'Hall 04',rec:'Dummy head (binaural), middle of the hall, sources 8 m apart, true stereo',kind:'hall',img:'hall',place:'Concert hall, about 600 seats, Detmold University of Music',mat:'Size estimated from the recording',date:'Detmold SRIR database, 2020',credit:'Sebastia Vicenc Amengual Gari, Banu Sahin, Dusty Eddy and Malte Kob, Detmold University of Music: Detmold SRIR database (2020)',licence:'CC BY 4.0',source:'https://zenodo.org/records/4116247',bands:[1.86,1.78,1.57,1.57,1.58,1.45,1.26,0.84],W:12.3,D:17.3,H:6.8,pre:0,room:'detmold-konzerthaus',position:'Middle of the hall',mics:'Dummy head'},
 {id:'wdr-large-ortf',name:'WDR Gro\u00dfer Sendesaal, ORTF',no:'Hall 07',rec:'ORTF pair (Schoeps MK4)',kind:'hall',img:'hall',place:'Broadcast concert hall (Klaus-von-Bismarck-Saal), WDR, Cologne',mat:'Size estimated from the recording',date:'WDR studios compilation, 2012',credit:'P. Stade, B. Bernsch\u00fctz and M. R\u00fchl, TH K\u00f6ln: WDR broadcast studios impulse response compilation (2012)',licence:'CC BY 4.0',source:'https://zenodo.org/records/3930833',bands:[1.31,1.64,1.69,1.76,1.87,1.86,1.45,0.92],W:24.4,D:34.2,H:13.4,pre:0,room:'wdr-large',position:'Main microphones',mics:'ORTF pair'},
 {id:'wdr-large-ab',name:'WDR Gro\u00dfer Sendesaal, Spaced omnis',no:'Hall 07',rec:'Large AB pair of omnis (Schoeps MK2)',kind:'hall',img:'hall',place:'Broadcast concert hall (Klaus-von-Bismarck-Saal), WDR, Cologne',mat:'Size estimated from the recording',date:'WDR studios compilation, 2012',credit:'P. Stade, B. Bernsch\u00fctz and M. R\u00fchl, TH K\u00f6ln: WDR broadcast studios impulse response compilation (2012)',licence:'CC BY 4.0',source:'https://zenodo.org/records/3930833',bands:[1.39,1.61,1.7,1.78,1.86,1.84,1.46,1.0],W:19.1,D:26.7,H:10.5,pre:0,room:'wdr-large',position:'Main microphones',mics:'Spaced omnis'},
 {id:'edinburgh-tunnel',name:'Innocent Railway Tunnel, Edinburgh',no:'Space 06',rec:'B-format decoded to a supercardioid pair, mid tunnel, 10 m from the source',kind:'underground',img:'underground',place:'Railway tunnel of 1831, 517 m long, now a footpath, Edinburgh',mat:'Size estimated from the recording',date:'OpenAIR library',credit:'Andrew Chadwick and Simon Shelley: OpenAIR library (www.openairlib.net)',licence:'CC BY 4.0',source:'https://www.openair.hosted.york.ac.uk/?page_id=525',bands:[2.4,1.77,1.48,1.52,1.44,1.16,0.87,0.56],W:13.8,D:19.3,H:7.6,pre:0},
 {id:'trollers-gill',name:'Troller\'s Gill',no:'Space 13',rec:'B-format decoded to a supercardioid pair, facing a four-loudspeaker array',kind:'outdoor',img:'outdoor',place:'Narrow limestone gorge with steep cliffs, open air, Yorkshire Dales',mat:'Size estimated from the recording',date:'OpenAIR library',credit:'Andrew Chadwick and Simon Shelley: OpenAIR library (www.openairlib.net)',licence:'CC BY 4.0',source:'https://www.openair.hosted.york.ac.uk/?page_id=745',bands:[0.51,0.56,0.44,0.39,0.36,0.34,0.35,0.28],W:9.8,D:13.7,H:5.4,pre:0}
];
var init=null;
try{var d=window.__JUCE__&&window.__JUCE__.initialisationData&&window.__JUCE__.initialisationData.drenched;if(d&&d.length)init=d[0];}catch(_){}
if(init){setLibrary(init.library);Object.assign(st,{sp:0,bands:SPACES[0].bands.slice(),rbands:SPACES[0].bands.slice(),pre:SPACES[0].pre});applyFromHost(init);if(init.licence)Object.assign(st.licence,init.licence);st.bits=+init.bits||64;}
else{setLibrary(PREVIEW_LIBRARY);Object.assign(st,{sp:0,bands:SPACES[0].bands.slice(),rbands:SPACES[0].bands.slice(),pre:SPACES[0].pre});}
try{var hs=(location.hash||'').slice(1);if(SKINS.some(function(x){return x.id===hs;}))st.skin=hs;}catch(_){}
Object.assign(lastSent,paramValues(st));Object.assign(lastUi,uiState(st));
renderLive();
if(JUCE){JUCE.addEventListener('state',applyFromHost);JUCE.addEventListener('meters',onMeters);JUCE.addEventListener('capture',onCapture);JUCE.addEventListener('licence',onLicence);JUCE.addEventListener('pasted',onPasted);JUCE.addEventListener('matched',onMatched);JUCE.addEventListener('analysis',onAnalysis);JUCE.addEventListener('spectrum',onSpectrum);JUCE.emitEvent('ready',{});
 // after a reload the plugin has forgotten which displays were open, so it is told again
 dispOpen=false;syncDisplays();}
if(document.fonts&&document.fonts.ready)document.fonts.ready.then(fit);
})();
