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
         living:'living',move:'movement',moveRate:'movementRate',rev:'reverse',gate:'gate',attack:'attack',dLvl:'directLevel',dDel:'directDelay',dCol:'directColour'};
var BAND_ID=['decay63','decay125','decay250','decay500','decay1k','decay2k','decay4k','decay8k'];
function keyOf(id){for(var k in PID)if(PID[k]===id)return k;return null;}
function paramValues(s){var o={};for(var k in PID)o[PID[k]]=k==='freeze'?(s.freeze?1:0):s[k];s.bands.forEach(function(b,i){o[BAND_ID[i]]=b;});return o;}
function uiState(s){return {spaceId:SPACES[s.sp]?SPACES[s.sp].id:'',lx:s.lx,locks:Object.keys(s.locks).filter(function(k){return s.locks[k];}).join(','),mode:s.mode,view:s.view,skin:s.skin};}

var lastSent={},lastUi={};
function sync(){var cur=paramValues(st),out={},n=0;for(var id in cur){if(lastSent[id]!==cur[id]){out[id]=cur[id];lastSent[id]=cur[id];n++;}}if(n&&JUCE)JUCE.emitEvent('params',out);}
function syncUi(){var cur=uiState(st),out={},n=0;for(var k in cur){if(lastUi[k]!==cur[k]){out[k]=cur[k];lastUi[k]=cur[k];n++;}}if(n&&JUCE)JUCE.emitEvent('ui',out);}
function gesture(ids,begin){if(JUCE)JUCE.emitEvent('gesture',{ids:ids,begin:!!begin});}

/* state pushed by the plugin: parameter values (host automation, preset recall) and the
   saved interface state (space, locks, view, skin) */
var irEvent=null,shortened=0;
function applyFromHost(msg){if(!msg)return;var p=msg.params||{},u=msg.ui||{};
 if(u.shortened!=null){var sh=+u.shortened||0;if(sh>0&&sh!==shortened){st.hint=null;
   st.note='<b>Shortened to '+sh.toFixed(1)+' s.</b> The 32-bit version only has memory for this much of a surround reverb at this sample rate. The 64-bit version plays it in full.';}
  shortened=sh;}
 if('ir' in u)setUserSpace(u.ir);
 if(u.irEvent!=null){var ev=+u.irEvent;if(irEvent!==null&&ev!==irEvent){st.hint=null;
   st.note=u.irError?'<b>That file didn\u2019t load.</b> '+esc(u.irError):'<b>'+esc(SPACES[USER]?SPACES[USER].name:'Your IR')+'</b> loaded. The graph shows its measured decay; drag a band to reshape it.';}
  irEvent=ev;}
 for(var id in p){var v=+p[id];if(!isFinite(v))continue;lastSent[id]=v;var bi=BAND_ID.indexOf(id);if(bi>=0){st.bands[bi]=v;continue;}
  var k=keyOf(id);if(!k)continue;if(k==='freeze')st.freeze=v>=0.5;else st[k]=v;}
 if(u.spaceId!=null){var si=spaceIndex(String(u.spaceId));if(si>=0){st.sp=si;lastUi.spaceId=String(u.spaceId);}}
 if(u.lx!=null){st.lx=clamp(+u.lx||0.5,0.06,0.94);lastUi.lx=st.lx;}
 if(u.locks!=null){st.locks={};String(u.locks).split(',').forEach(function(k){if(k&&P[k])st.locks[k]=true;});lastUi.locks=uiState(st).locks;}
 if(u.mode==='studio'||u.mode==='stage'){st.mode=u.mode;lastUi.mode=u.mode;}
 if(u.view==='plan'||u.view==='section'){st.view=u.view;lastUi.view=u.view;}
 if(u.skin&&SKINS.some(function(x){return x.id===u.skin;})){st.skin=u.skin;lastUi.skin=u.skin;}
 schedule();}

/* meters arrive often; they update in place rather than through a full render */
function onMeters(m){if(!m)return;st.inL=+m.inL;st.inR=+m.inR;st.outL=+m.outL;st.outR=+m.outR;
 var i=Math.max(st.inL,st.inR),o=Math.max(st.outL,st.outR),map={'in':i,'out':o,outL:st.outL,outR:st.outR};
 var bars=live.querySelectorAll('[data-m]');for(var a=0;a<bars.length;a++){var el=bars[a],v=map[el.getAttribute('data-m')];if(v==null)continue;var pct=meterPct(v).toFixed(0)+'%';if(el.hasAttribute('style')&&el.style.height)el.style.height=pct;else el.style.width=pct;if(el.nextElementSibling&&el.nextElementSibling.tagName==='B')el.nextElementSibling.style.bottom=Math.min(98,meterPct(v)+4).toFixed(0)+'%';}
 var txt=live.querySelectorAll('[data-mt]');for(var b=0;b<txt.length;b++){var v2=map[txt[b].getAttribute('data-mt')];if(v2!=null)txt[b].textContent=dbText(v2);}}

/* ---- the skin picker ---- */
function pickerHTML(){var ts=Object.assign({},st,{browse:false,adv:false,skins:false,hint:null,note:null,licence:{state:'licensed'}});
 var thumb=function(x){return screenHTML(x.id,ts).replace(/ data-fid="[^"]*"/g,'').replace(/tabindex="0"/g,'tabindex="-1"');};
 return '<div class="skins" role="dialog" aria-modal="true" aria-label="Choose a look"><div class="hd"><h2>Choose a look</h2><span>Twelve looks over the same controls. Every value stays where it is.</span><button data-act="skins" aria-label="Close">Close</button></div>'+
  '<div class="grid">'+SKINS.map(function(x){return '<div class="sk" role="button" tabindex="0" data-act="skin" data-v="'+x.id+'" aria-current="'+(x.id===st.skin)+'" aria-label="'+x.name+', '+x.tag.toLowerCase()+'"><div class="fr" inert>'+thumb(x)+'</div><div class="t"><b>'+x.name+'</b><span>'+x.tag+'</span></div></div>';}).join('')+'</div>'+
  '<div class="ft">Click the Drenched name in any look to come back here. The look is saved with this instance and becomes the default for new ones.</div></div>';}

/* ---- capturing a space: the plugin plays a sweep and records the room ---- */
st.capture=false;st.cap={speakers:1,sweep:10,tail:6,level:-12};st.capStatus={stage:'idle',progress:0,peakDb:-100,message:''};
var CAP_OPTS={speakers:[[1,'Left only'],[2,'Left, then right']],sweep:[[5,'5 s'],[10,'10 s'],[20,'20 s']],tail:[[3,'3 s'],[6,'6 s'],[12,'12 s']],level:[[-24,'\u221224 dB'],[-18,'\u221218 dB'],[-12,'\u221212 dB'],[-6,'\u22126 dB']]};
var CAP_ROWS=[['speakers','Speakers','Left, then right gives true stereo when you record with two microphones.'],['sweep','Sweep length','A longer sweep rises further above background noise.'],['tail','Ring-out','Make it longer than the room takes to fall silent.'],['level','Sweep level','Start low. Aim for a mic peak around \u221212 dB.']];
function capBusy(){var g=st.capStatus.stage;return g==='playing'||g==='working';}
function capPeak(){var p=+st.capStatus.peakDb;return p<=-99?'Mic peak: nothing yet':'Mic peak '+dbText(p)+' dB';}
function captureHTML(){var c=st.cap,cs=st.capStatus,busy=capBusy(),secs=c.speakers*(c.sweep+c.tail);
 var rows=CAP_ROWS.map(function(r){return '<div class="cr"><span class="cl" id="cl-'+r[0]+'">'+r[1]+'</span><span class="seg" role="radiogroup" aria-labelledby="cl-'+r[0]+'">'+CAP_OPTS[r[0]].map(function(o){return '<button role="radio" data-act="capopt" data-k="'+r[0]+'" data-v="'+o[0]+'" aria-checked="'+(c[r[0]]===o[0])+'"'+(busy?' disabled':'')+'>'+o[1]+'</button>';}).join('')+'</span><small>'+r[2]+'</small></div>';}).join('');
 var status=cs.stage==='playing'?'<div class="cpr"><span class="bar"><i data-cap="p" style="width:'+(clamp(+cs.progress||0,0,1)*100).toFixed(1)+'%"></i></span><em data-cap="pk">'+capPeak()+'</em></div><span>'+esc(cs.message)+'</span>'
  :cs.stage==='working'?'<span>Working out the impulse response.</span>'
  :cs.message?'<span class="'+(cs.stage==='failed'?'err':'')+'">'+esc(cs.message)+'</span>':'';
 var action=cs.stage==='playing'?'<button class="cgo stop" data-act="capstop">Stop capture</button>'
  :cs.stage==='working'?'<button class="cgo" disabled>Working\u2026</button>'
  :'<button class="cgo" data-act="capstart">Start capture</button><span class="ct">Takes '+secs+' seconds. Captures are saved in Documents, Drenched, Captures.</span>';
 return '<div class="cap" role="dialog" aria-modal="true" aria-labelledby="cap-h"><div class="cbox"><div class="chd"><h2 id="cap-h">Capture a space</h2><button class="cx" data-act="capture" aria-label="Close">Close</button></div>'+
  '<p>Drenched plays a sweep through your speakers, records it with your microphone and turns the recording into an impulse response for this instance.</p>'+
  '<p>Put Drenched on a track whose input is the room microphone, turn input monitoring on, and set your speakers to a comfortable level.</p>'+
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
    '<li>Paste the activation code it gives you:<div class="lrow top"><textarea id="lic-code" data-fid="lic-code" data-in="licCode" spellcheck="false" aria-label="Activation code" placeholder="DRN1.…"'+dis+'>'+esc(st.licCode)+'</textarea>'+
    '<button class="lsm" data-act="licpaste" data-v="licCode"'+dis+'>Paste</button><button class="cgo" data-act="liccode"'+dis+'>Use code</button></div></li></ol>':'');}
 var dev=L.testKey?'<p class="ldev">Development build: its activation codes use the published test key, so this copy is not for sale.</p>':'';
 return '<div class="cap lic" role="dialog" aria-modal="true" aria-labelledby="lic-h"><div class="cbox"><div class="chd"><h2 id="lic-h">'+(L.state==='licensed'?'Licence':'Activate Drenched')+'</h2><button class="cx" data-act="licence" aria-label="Close">Close</button></div>'+body+dev+'</div></div>';}
function licActivate(){if(JUCE)JUCE.emitEvent('licenceActivate',{licence:st.licKey});else st.licence=Object.assign({},st.licence,{message:'Activation works inside the plugin.',error:true});}

/* ---- rendering ---- */
function fit(){var w=live.clientWidth,h=live.clientHeight,sc=Math.min(w/W,h/H),el=live.firstElementChild;if(!el)return;
 el.style.transform='translate('+((w-W*sc)/2).toFixed(2)+'px,'+((h-H*sc)/2).toFixed(2)+'px) scale('+sc.toFixed(5)+')';
 var frs=live.querySelectorAll('.skins .fr');for(var i=0;i<frs.length;i++){var s2=frs[i].firstElementChild;if(s2)s2.style.transform='scale('+(frs[i].clientWidth/W).toFixed(5)+')';}}
function renderLive(){var a=document.activeElement,fid=a&&live.contains(a)?a.getAttribute('data-fid'):null;
 var html=screenHTML(st.skin,st);if(st.capture)html=html.slice(0,-6)+captureHTML()+'</div>';if(st.lic)html=html.slice(0,-6)+licenceHTML()+'</div>';if(st.credits)html=html.slice(0,-6)+creditsHTML()+'</div>';if(st.skins)html=html.slice(0,-6)+pickerHTML()+'</div>';
 live.innerHTML=html;fit();
 if(fid){var n=live.querySelector('[data-fid="'+fid+'"]');if(n){n.focus({preventScroll:true});if(n.matches('input,textarea'))n.selectionStart=n.selectionEnd=n.value.length;}}
 // A dialog keeps the keyboard even when the control that had it is redrawn away.
 if((st.lic||st.capture||st.credits)&&!live.contains(document.activeElement)){var d=live.querySelector('.cap .cgo:not([disabled])')||live.querySelector('.cap .cx');if(d)d.focus({preventScroll:true});}}
var raf=0;function schedule(){if(!raf)raf=requestAnimationFrame(function(){raf=0;renderLive();sync();syncUi();});}
window.schedule=schedule;
window.addEventListener('resize',fit);
if(window.ResizeObserver)new ResizeObserver(fit).observe(live);

/* ---- interaction ---- */
function snap(s){var o={sp:s.sp,bands:s.bands.slice(),lx:s.lx};for(var k in PID)if(k!=='freeze')o[k]=s[k];return o;}
function changeSpace(i){var old=snap(st),sp=SPACES[i],held=[];st.sp=i;st.bands=sp.bands.slice();st.pre=sp.pre;st.size=100;
 if(st.locks.decay){var f=decayOf(old.bands)/decayOf(sp.bands);st.bands=st.bands.map(function(b){return clamp(b*f,0.1,20);});held.push('Decay');}
 if(st.locks.pre){st.pre=old.pre;held.push('Pre-delay');}if(st.locks.mix)held.push('Mix');
 st.hint=null;st.note='<b>'+esc(sp.name)+'</b> loaded.'+(held.length?' Locked and held: '+held.join(', ')+'.':' Lock a value to hold it when you change space.')+(sp.credit?' Recording: '+esc(sp.credit)+', '+esc(sp.licence)+'.':'');}
var drag=null;
live.addEventListener('pointerdown',function(e){var t=e.target;if(t.closest('input,button'))return;
 var kn=t.closest('[data-knob]'),lis=t.closest('[data-drag]'),dg=t.closest('svg[data-decay]');
 if(kn){var k=kn.getAttribute('data-knob'),h=kn.getAttribute('data-h')==='1';drag={type:'knob',k:k,h:h,x0:e.clientX,y0:e.clientY,n0:to01(k,getV(st,k)),w:kn.getBoundingClientRect().width*(W/live.firstElementChild.getBoundingClientRect().width),ids:k==='decay'?BAND_ID.slice():[PID[k]]};kn.focus({preventScroll:true});}
 else if(lis){var sv=lis.closest('svg');drag={type:'lis',rect:sv.getBoundingClientRect(),w:+sv.getAttribute('data-w'),mode:sv.getAttribute('data-mode'),ox:+sv.getAttribute('data-ox'),oy:+sv.getAttribute('data-oy'),sc:+sv.getAttribute('data-sc'),rw:+sv.getAttribute('data-rw'),ids:[PID.dist]};lis.focus({preventScroll:true});}
 else if(dg){var r=dg.getBoundingClientRect(),d={rect:r,l:+dg.getAttribute('data-l'),t:+dg.getAttribute('data-t'),pw:+dg.getAttribute('data-pw'),ph:+dg.getAttribute('data-ph'),ymax:+dg.getAttribute('data-ymax'),w:+dg.getAttribute('data-w'),bars:dg.getAttribute('data-bars')==='1'};
  var sx=(e.clientX-r.left)*d.w/r.width,sy=(e.clientY-r.top)*d.w/r.width;d.polar=dg.getAttribute('data-polar')==='1';d.horiz=dg.getAttribute('data-horiz')==='1';
  if(d.polar){d.cx=+dg.getAttribute('data-cx');d.cy=+dg.getAttribute('data-cy');d.r0=+dg.getAttribute('data-r0');d.r1=+dg.getAttribute('data-r1');var dd=Math.hypot(sx-d.cx,sy-d.cy);if(dd<d.r0-24||dd>d.r1+40)return;var ang=Math.atan2(sy-d.cy,sx-d.cx)*180/Math.PI;d.i=((Math.round((ang+90)/45)%8)+8)%8;}
  else if(d.horiz){d.rh=+dg.getAttribute('data-rh');if(sy<d.t||sy>d.t+d.ph+d.rh)return;d.i=clamp(Math.round((sy-d.t)/d.rh)-1,0,7);}
  else{if(sx<d.l||sx>d.l+d.pw)return;d.i=d.bars?clamp(Math.floor((sx-d.l)/(d.pw/8)),0,7):clamp(Math.round((sx-d.l)/(d.pw/7)),0,7);}
  d.type='decay';d.ids=[BAND_ID[d.i]];drag=d;st.lastBand=d.i;applyDecay(e);
  var nd=live.querySelector('[data-fid="nd-'+d.i+'"]');if(nd)nd.focus({preventScroll:true});}
 else return;
 gesture(drag.ids,true);
 try{live.setPointerCapture(e.pointerId);}catch(_){}e.preventDefault();});
function applyDecay(e){var d=drag,sc=d.w/d.rect.width,sx=(e.clientX-d.rect.left)*sc,sy=(e.clientY-d.rect.top)*sc,v;if(d.polar)v=(Math.hypot(sx-d.cx,sy-d.cy)-d.r0)/(d.r1-d.r0)*d.ymax;else if(d.horiz)v=(sx-d.l)/d.pw*d.ymax;else v=(1-(sy-d.t)/d.ph)*d.ymax;v=clamp(v,0.1,d.ymax);st.bands[d.i]=Math.round(v*100)/100;st.hint='decay';st.note=null;schedule();}
live.addEventListener('pointermove',function(e){if(!drag)return;
 if(drag.type==='knob'){var k=live.firstElementChild.getBoundingClientRect().width/W,dn=drag.h?(e.clientX-drag.x0)/k/Math.max(drag.w,60):(drag.y0-e.clientY)/k/220;if(e.shiftKey)dn/=10;setParam(st,drag.k,from01(drag.k,drag.n0+dn));schedule();}
 else if(drag.type==='decay')applyDecay(e);
 else if(drag.type==='lis'){var r=drag.rect,kk=drag.w/r.width,px=(e.clientX-r.left)*kk,py=(e.clientY-r.top)*kk;
  if(drag.mode==='plan'){st.lx=clamp((px-drag.ox)/(drag.rw*drag.sc),0.06,0.94);st.dist=Math.round(distFromM(st,(py-drag.oy)/drag.sc));}else st.dist=Math.round(distFromM(st,(px-drag.ox)/drag.sc));
  st.hint='dist';st.note=null;schedule();}});
function endDrag(){if(drag){var ids=drag.ids;drag=null;schedule();gesture(ids,false);}}
live.addEventListener('pointerup',endDrag);live.addEventListener('pointercancel',endDrag);
live.addEventListener('keydown',function(e){var t=e.target,keys={ArrowUp:1,ArrowRight:1,ArrowDown:-1,ArrowLeft:-1,PageUp:10,PageDown:-10};
 if(e.key==='Escape'&&(st.skins||st.browse||st.capture||st.lic||st.credits)){if(st.skins||st.browse){st.skins=false;st.browse=false;}else if(st.credits)st.credits=false;else if(st.lic)st.lic=false;else st.capture=false;e.preventDefault();renderLive();return;}
 if(e.key==='Enter'&&t.id==='lic-key'){e.preventDefault();licActivate();renderLive();return;}
 if(t.matches('.sk')&&(e.key==='Enter'||e.key===' ')){e.preventDefault();t.click();return;}
 if(t.matches('[data-knob]')){var k=t.getAttribute('data-knob'),p=P[k];if(e.key==='Home')setParam(st,k,p.min);else if(e.key==='End')setParam(st,k,p.max);else if(keys[e.key])setParam(st,k,from01(k,to01(k,getV(st,k))+keys[e.key]*(e.shiftKey?0.002:0.02)));else return;e.preventDefault();schedule();}
 else if(t.matches('[data-node]')){var i=+t.getAttribute('data-node');if(e.key==='ArrowLeft'||e.key==='ArrowRight'){var ni=clamp(i+(e.key==='ArrowRight'?1:-1),0,7);st.lastBand=ni;e.preventDefault();renderLive();var n2=live.querySelector('[data-fid="nd-'+ni+'"]');if(n2)n2.focus({preventScroll:true});return;}
  if(e.key==='ArrowUp'||e.key==='ArrowDown'){st.bands[i]=clamp(Math.round((st.bands[i]+(e.key==='ArrowUp'?1:-1)*(e.shiftKey?0.01:0.1))*100)/100,0.1,20);st.lastBand=i;st.hint='decay';st.note=null;e.preventDefault();schedule();}}
 else if(t.matches('[data-drag]')){if(e.key==='ArrowUp'||e.key==='ArrowDown')st.dist=clamp(st.dist+(e.key==='ArrowDown'?2:-2),0,100);else if(e.key==='ArrowLeft'||e.key==='ArrowRight')st.lx=clamp(st.lx+(e.key==='ArrowRight'?0.03:-0.03),0.06,0.94);else return;st.hint='dist';st.note=null;e.preventDefault();schedule();}});
live.addEventListener('change',function(e){var t=e.target;if(!t.matches('input[data-type]'))return;var ty=t.getAttribute('data-type'),raw=t.value.replace(/\u2212/g,'-'),nums=raw.match(/-?\d+(\.\d+)?/g);
 if(!nums){if(/off/i.test(raw)&&P[ty]&&(P[ty].min===0||ty==='dLvl'))setParam(st,ty,P[ty].min);st.note=null;renderLive();sync();return;}var v=parseFloat(nums[nums.length-1]);
 if(ty==='band'){var i=+t.getAttribute('data-i');st.bands[i]=clamp(v,0.1,20);st.lastBand=i;st.hint='decay';}
 else if(ty==='dist'){st.dist=Math.round(distFromM(st,v));st.hint='dist';}
 else if(ty==='hicut'&&/k/i.test(raw)){setParam(st,ty,v*1000);}
 else setParam(st,ty,v);
 st.note=null;renderLive();sync();});
live.addEventListener('input',function(e){var k=e.target.getAttribute&&e.target.getAttribute('data-in');if(k==='licKey'||k==='licCode')st[k]=e.target.value;});
live.addEventListener('focusin',function(e){if(e.target.matches('input.ro'))e.target.select();});
live.addEventListener('click',function(e){var b=e.target.closest('[data-act]');
 if(!b){if(st.browse&&!e.target.closest('.menu')){st.browse=false;renderLive();}return;}
 var a=b.getAttribute('data-act'),v=b.getAttribute('data-v');if(a!=='browse'&&a!=='pick')st.browse=false;
 switch(a){
  case 'mode':st.mode=v;st.note=v==='stage'?'<b>Stage view</b> for post: large numerals, channel meters and locks, on the same tokens.':'<b>Studio view</b>: the space and its decay lead.';st.hint=null;break;
  case 'slot':if(v!==st.slot){st.slots[st.slot]=snap(st);var nx=st.slots[v]||snap(st);Object.keys(nx).forEach(function(k){st[k]=Array.isArray(nx[k])?nx[k].slice():nx[k];});st.slot=v;st.hint=null;st.note='<b>Snapshot '+v+'.</b> Changes now go to '+v+'. Switch back to compare.';}break;
  case 'freeze':st.freeze=!st.freeze;st.hint=null;st.note=null;break;
  case 'toggle':var tk=b.getAttribute('data-k');st[tk]=st[tk]>=0.5?0:1;st.hint=tk;st.note=null;break;
  case 'adv':st.adv=!st.adv;st.hint=null;st.note=st.adv?'<b>All parameters</b> opens in place, so the space and graph stay in view.':null;break;
  case 'match':st.hint=null;st.note='<b>Match a room</b> listens to a recording and suggests the closest space and settings. It arrives with the room model.';break;
  case 'lock':var lk=b.getAttribute('data-k');st.locks[lk]=!st.locks[lk];st.hint=null;st.note=(st.locks[lk]?'<b>Locked.</b> ':'<b>Unlocked.</b> ')+P[lk].l+(st.locks[lk]?' now holds when you change space.':' follows each space again.');break;
  case 'view':st.view=v;break;
  case 'browse':st.browse=!st.browse;break;
  case 'loadir':st.browse=false;st.hint=null;if(JUCE){JUCE.emitEvent('loadIr',{});st.note='Choose a WAV, AIFF or FLAC impulse response: mono, stereo or four-channel true stereo.';}else st.note='<b>Load your own IR</b> opens a file chooser in the plugin.';break;
  case 'credits':st.credits=!st.credits;st.browse=false;st.hint=null;break;
  case 'capture':st.capture=!st.capture;st.hint=null;if(st.capture&&!capBusy()&&st.capStatus.stage!=='failed')st.capStatus.message='';break;
  case 'capopt':var ck=b.getAttribute('data-k');if(CAP_OPTS[ck]&&!capBusy())st.cap[ck]=+v;break;
  case 'capstart':if(JUCE){st.capStatus={stage:'playing',progress:0,peakDb:-100,message:''};JUCE.emitEvent('startCapture',{speakers:st.cap.speakers,sweepSeconds:st.cap.sweep,tailSeconds:st.cap.tail,levelDb:st.cap.level});}
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
  case 'prev':changeSpace((st.sp+SPACES.length-1)%SPACES.length);break;
  case 'next':changeSpace((st.sp+1)%SPACES.length);break;
  case 'skins':st.skins=!st.skins;break;
  case 'skin':if(SKINS.some(function(x){return x.id===v;})){st.skin=v;st.note='<b>'+esc(skinOf(v).name)+'.</b> Click the Drenched name to change the look again.';st.hint=null;}st.skins=false;break;
 }
 renderLive();sync();syncUi();
 if(a==='browse'&&st.browse){var mi=live.querySelector('.menu button[aria-current="true"]');if(mi)mi.focus({preventScroll:true});}
 else if(a==='skins'&&st.skins){var sk=live.querySelector('.skins .sk[aria-current="true"]');if(sk)sk.focus({preventScroll:true});}
 else if(a==='capture'&&st.capture){var cf=live.querySelector('.cap .cgo');if(cf)cf.focus({preventScroll:true});}
 else if(a==='licence'&&st.lic){var lf=live.querySelector('#lic-key')||live.querySelector('.lic .cgo');if(lf)lf.focus({preventScroll:true});}
 else if(a==='capstart'||a==='capstop'){var cg=live.querySelector('.cap .cgo:not([disabled])')||live.querySelector('.cap .cx');if(cg)cg.focus({preventScroll:true});}
 else{var same=live.querySelector('[data-act="'+a+'"]'+(v!=null?'[data-v="'+v+'"]':'')+(a==='lock'||a==='toggle'||a==='capopt'?'[data-k="'+b.getAttribute('data-k')+'"]':''));if(same)same.focus({preventScroll:true});}});
/* the host may hand us a context menu on right click; the interface has none */
live.addEventListener('contextmenu',function(e){e.preventDefault();});

/* ---- start ---- */
var init=null;
try{var d=window.__JUCE__&&window.__JUCE__.initialisationData&&window.__JUCE__.initialisationData.drenched;if(d&&d.length)init=d[0];}catch(_){}
if(init){setLibrary(init.library);Object.assign(st,{sp:0,bands:SPACES[0].bands.slice(),pre:SPACES[0].pre});applyFromHost(init);if(init.licence)Object.assign(st.licence,init.licence);st.bits=+init.bits||64;}
try{var hs=(location.hash||'').slice(1);if(SKINS.some(function(x){return x.id===hs;}))st.skin=hs;}catch(_){}
Object.assign(lastSent,paramValues(st));Object.assign(lastUi,uiState(st));
renderLive();
if(JUCE){JUCE.addEventListener('state',applyFromHost);JUCE.addEventListener('meters',onMeters);JUCE.addEventListener('capture',onCapture);JUCE.addEventListener('licence',onLicence);JUCE.addEventListener('pasted',onPasted);JUCE.emitEvent('ready',{});}
if(document.fonts&&document.fonts.ready)document.fonts.ready.then(fit);
})();
