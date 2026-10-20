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
         preSync:'predelaySync',xover:'crossover',locut:'wetLowCut',hicut:'wetHighCut',width:'width',duck:'ducking',mono:'monoBelow'};
var BAND_ID=['decay63','decay125','decay250','decay500','decay1k','decay2k','decay4k','decay8k'];
function keyOf(id){for(var k in PID)if(PID[k]===id)return k;return null;}
function paramValues(s){var o={};for(var k in PID)o[PID[k]]=k==='freeze'?(s.freeze?1:0):s[k];s.bands.forEach(function(b,i){o[BAND_ID[i]]=b;});return o;}
function uiState(s){return {space:s.sp,lx:s.lx,locks:Object.keys(s.locks).filter(function(k){return s.locks[k];}).join(','),mode:s.mode,view:s.view,skin:s.skin};}

var lastSent={},lastUi={};
function sync(){var cur=paramValues(st),out={},n=0;for(var id in cur){if(lastSent[id]!==cur[id]){out[id]=cur[id];lastSent[id]=cur[id];n++;}}if(n&&JUCE)JUCE.emitEvent('params',out);}
function syncUi(){var cur=uiState(st),out={},n=0;for(var k in cur){if(lastUi[k]!==cur[k]){out[k]=cur[k];lastUi[k]=cur[k];n++;}}if(n&&JUCE)JUCE.emitEvent('ui',out);}
function gesture(ids,begin){if(JUCE)JUCE.emitEvent('gesture',{ids:ids,begin:!!begin});}

/* state pushed by the plugin: parameter values (host automation, preset recall) and the
   saved interface state (space, locks, view, skin) */
function applyFromHost(msg){if(!msg)return;var p=msg.params||{},u=msg.ui||{};
 for(var id in p){var v=+p[id];if(!isFinite(v))continue;lastSent[id]=v;var bi=BAND_ID.indexOf(id);if(bi>=0){st.bands[bi]=v;continue;}
  var k=keyOf(id);if(!k)continue;if(k==='freeze')st.freeze=v>=0.5;else st[k]=v;}
 if(u.space!=null){st.sp=clamp(Math.round(+u.space)||0,0,SPACES.length-1);lastUi.space=st.sp;}
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
function pickerHTML(){var ts=Object.assign({},st,{browse:false,adv:false,skins:false,hint:null,note:null});
 var thumb=function(x){return screenHTML(x.id,ts).replace(/ data-fid="[^"]*"/g,'').replace(/tabindex="0"/g,'tabindex="-1"');};
 return '<div class="skins" role="dialog" aria-modal="true" aria-label="Choose a look"><div class="hd"><h2>Choose a look</h2><span>Twelve looks over the same controls. Every value stays where it is.</span><button data-act="skins" aria-label="Close">Close</button></div>'+
  '<div class="grid">'+SKINS.map(function(x){return '<div class="sk" role="button" tabindex="0" data-act="skin" data-v="'+x.id+'" aria-current="'+(x.id===st.skin)+'" aria-label="'+x.name+', '+x.tag.toLowerCase()+'"><div class="fr" inert>'+thumb(x)+'</div><div class="t"><b>'+x.name+'</b><span>'+x.tag+'</span></div></div>';}).join('')+'</div>'+
  '<div class="ft">Click the Drenched name in any look to come back here. The look is saved with this instance and becomes the default for new ones.</div></div>';}

/* ---- rendering ---- */
function fit(){var w=live.clientWidth,h=live.clientHeight,sc=Math.min(w/W,h/H),el=live.firstElementChild;if(!el)return;
 el.style.transform='translate('+((w-W*sc)/2).toFixed(2)+'px,'+((h-H*sc)/2).toFixed(2)+'px) scale('+sc.toFixed(5)+')';
 var frs=live.querySelectorAll('.skins .fr');for(var i=0;i<frs.length;i++){var s2=frs[i].firstElementChild;if(s2)s2.style.transform='scale('+(frs[i].clientWidth/W).toFixed(5)+')';}}
function renderLive(){var a=document.activeElement,fid=a&&live.contains(a)?a.getAttribute('data-fid'):null;
 var html=screenHTML(st.skin,st);if(st.skins)html=html.slice(0,-6)+pickerHTML()+'</div>';
 live.innerHTML=html;fit();
 if(fid){var n=live.querySelector('[data-fid="'+fid+'"]');if(n)n.focus({preventScroll:true});}}
var raf=0;function schedule(){if(!raf)raf=requestAnimationFrame(function(){raf=0;renderLive();sync();syncUi();});}
window.schedule=schedule;
window.addEventListener('resize',fit);
if(window.ResizeObserver)new ResizeObserver(fit).observe(live);

/* ---- interaction ---- */
function snap(s){var o={sp:s.sp,bands:s.bands.slice(),lx:s.lx};for(var k in PID)if(k!=='freeze')o[k]=s[k];return o;}
function changeSpace(i){var old=snap(st),sp=SPACES[i],held=[];st.sp=i;st.bands=sp.bands.slice();st.pre=sp.pre;st.size=100;
 if(st.locks.decay){var f=decayOf(old.bands)/decayOf(sp.bands);st.bands=st.bands.map(function(b){return clamp(b*f,0.1,20);});held.push('Decay');}
 if(st.locks.pre){st.pre=old.pre;held.push('Pre-delay');}if(st.locks.mix)held.push('Mix');
 st.hint=null;st.note='<b>'+esc(sp.name)+'</b> loaded.'+(held.length?' Locked and held: '+held.join(', ')+'.':' Lock a value to hold it when you change space.');}
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
 if(e.key==='Escape'&&(st.skins||st.browse)){st.skins=false;st.browse=false;e.preventDefault();renderLive();return;}
 if(t.matches('.sk')&&(e.key==='Enter'||e.key===' ')){e.preventDefault();t.click();return;}
 if(t.matches('[data-knob]')){var k=t.getAttribute('data-knob'),p=P[k];if(e.key==='Home')setParam(st,k,p.min);else if(e.key==='End')setParam(st,k,p.max);else if(keys[e.key])setParam(st,k,from01(k,to01(k,getV(st,k))+keys[e.key]*(e.shiftKey?0.002:0.02)));else return;e.preventDefault();schedule();}
 else if(t.matches('[data-node]')){var i=+t.getAttribute('data-node');if(e.key==='ArrowLeft'||e.key==='ArrowRight'){var ni=clamp(i+(e.key==='ArrowRight'?1:-1),0,7);st.lastBand=ni;e.preventDefault();renderLive();var n2=live.querySelector('[data-fid="nd-'+ni+'"]');if(n2)n2.focus({preventScroll:true});return;}
  if(e.key==='ArrowUp'||e.key==='ArrowDown'){st.bands[i]=clamp(Math.round((st.bands[i]+(e.key==='ArrowUp'?1:-1)*(e.shiftKey?0.01:0.1))*100)/100,0.1,20);st.lastBand=i;st.hint='decay';st.note=null;e.preventDefault();schedule();}}
 else if(t.matches('[data-drag]')){if(e.key==='ArrowUp'||e.key==='ArrowDown')st.dist=clamp(st.dist+(e.key==='ArrowDown'?2:-2),0,100);else if(e.key==='ArrowLeft'||e.key==='ArrowRight')st.lx=clamp(st.lx+(e.key==='ArrowRight'?0.03:-0.03),0.06,0.94);else return;st.hint='dist';st.note=null;e.preventDefault();schedule();}});
live.addEventListener('change',function(e){var t=e.target;if(!t.matches('input[data-type]'))return;var ty=t.getAttribute('data-type'),raw=t.value.replace(/\u2212/g,'-'),nums=raw.match(/-?\d+(\.\d+)?/g);
 if(!nums){renderLive();return;}var v=parseFloat(nums[nums.length-1]);
 if(ty==='band'){var i=+t.getAttribute('data-i');st.bands[i]=clamp(v,0.1,20);st.lastBand=i;st.hint='decay';}
 else if(ty==='dist'){st.dist=Math.round(distFromM(st,v));st.hint='dist';}
 else if(ty==='hicut'&&/k/i.test(raw)){setParam(st,ty,v*1000);}
 else setParam(st,ty,v);
 st.note=null;renderLive();sync();});
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
  case 'pick':st.browse=false;changeSpace(+v);break;
  case 'prev':changeSpace((st.sp+SPACES.length-1)%SPACES.length);break;
  case 'next':changeSpace((st.sp+1)%SPACES.length);break;
  case 'skins':st.skins=!st.skins;break;
  case 'skin':if(SKINS.some(function(x){return x.id===v;})){st.skin=v;st.note='<b>'+esc(skinOf(v).name)+'.</b> Click the Drenched name to change the look again.';st.hint=null;}st.skins=false;break;
 }
 renderLive();sync();syncUi();
 if(a==='browse'&&st.browse){var mi=live.querySelector('.menu button[aria-current="true"]');if(mi)mi.focus({preventScroll:true});}
 else if(a==='skins'&&st.skins){var sk=live.querySelector('.skins .sk[aria-current="true"]');if(sk)sk.focus({preventScroll:true});}
 else{var same=live.querySelector('[data-act="'+a+'"]'+(v!=null?'[data-v="'+v+'"]':'')+(a==='lock'||a==='toggle'?'[data-k="'+b.getAttribute('data-k')+'"]':''));if(same)same.focus({preventScroll:true});}});
/* the host may hand us a context menu on right click; the interface has none */
live.addEventListener('contextmenu',function(e){e.preventDefault();});

/* ---- start ---- */
var init=null;
try{var d=window.__JUCE__&&window.__JUCE__.initialisationData&&window.__JUCE__.initialisationData.drenched;if(d&&d.length)init=d[0];}catch(_){}
if(init)applyFromHost(init);
try{var hs=(location.hash||'').slice(1);if(SKINS.some(function(x){return x.id===hs;}))st.skin=hs;}catch(_){}
Object.assign(lastSent,paramValues(st));Object.assign(lastUi,uiState(st));
renderLive();
if(JUCE){JUCE.addEventListener('state',applyFromHost);JUCE.addEventListener('meters',onMeters);JUCE.emitEvent('ready',{});}
if(document.fonts&&document.fonts.ready)document.fonts.ready.then(fit);
})();
