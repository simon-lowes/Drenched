/* Drenched interface engine: the shared model, the drawing helpers and the twelve
   compositions. Generated from the design reference by design/v4 gen.py; the plugin bridge
   and the interaction layer live in app.js. Plain script, no module system, so it runs in
   every web view JUCE supports. */
'use strict';
var FREQ=['63','125','250','500','1k','2k','4k','8k'];
/* The third-octave bands of the waterfall and the live spectrum (the engine's analysisBands),
   and which of them carry a label. */
var BANDS31=[20,25,31.5,40,50,63,80,100,125,160,200,250,315,400,500,630,800,1000,1250,1600,2000,2500,3150,4000,5000,6300,8000,10000,12500,16000,20000];
var WF_LAB=[[5,'63'],[8,'125'],[11,'250'],[14,'500'],[17,'1k'],[20,'2k'],[23,'4k'],[26,'8k'],[29,'16k Hz']];
/* The spaces. Recorded ones come from the plugin's bundled library (library/library.json),
   listed first; the modelled ones are synthesised from their size and decay, and keep the
   ids the engine knows them by (source/engine/include/drenched/Spaces.h). Sessions save the
   id, so the list can grow without breaking them. */
var MODELLED=[
 {id:'modelled-hall',name:'Grand Hall',no:'Modelled',kind:'hall',img:'hall',place:'Concert hall, from the balcony',W:28,D:42,H:16,mat:'Oak and plaster',bands:[2.9,2.8,2.6,2.4,2.3,2.0,1.6,1.1],pre:18,rec:'Modelled from the room\u2019s size and decay',date:'synthesised'},
 {id:'modelled-chapel',name:'Stone Chapel',no:'Modelled',kind:'chapel',img:'chapel',place:'Nave, mid aisle',W:13,D:36,H:17,mat:'Limestone',bands:[3.9,4.1,3.9,3.6,3.4,3.0,2.3,1.5],pre:24,rec:'Modelled from the room\u2019s size and decay',date:'synthesised'},
 {id:'modelled-garage',name:'Car Park Level 2',no:'Modelled',kind:'garage',img:'garage',place:'Underground deck',W:44,D:58,H:3.2,mat:'Bare concrete',bands:[1.5,1.8,2.0,1.9,1.8,1.6,1.3,0.9],pre:9,rec:'Modelled from the room\u2019s size and decay',date:'synthesised'},
 {id:'modelled-room',name:'Live Room B',no:'Modelled',kind:'room',img:'room',place:'Studio live room',W:9,D:12,H:4.2,mat:'Timber and diffusers',bands:[0.72,0.66,0.6,0.55,0.52,0.5,0.45,0.38],pre:4,rec:'Modelled from the room\u2019s size and decay',date:'synthesised'}
];
var LIBRARY=[],SPACES=MODELLED.slice(),USER=SPACES.length;
var KINDS={hall:1,chapel:1,garage:1,room:1};
function setLibrary(list){LIBRARY=(list||[]).filter(function(x){return x&&x.id&&x.name;}).map(function(x){var k=KINDS[x.kind]?x.kind:'hall';
  return {id:String(x.id),name:String(x.name),no:String(x.no||'Recorded'),kind:k,img:KINDS[x.img]?x.img:k,place:String(x.place||''),W:+x.W||14,D:+x.D||20,H:+x.H||8,
   mat:String(x.mat||''),bands:(x.bands||[2,2,2,2,2,2,2,2]).map(function(b){return clamp(+b||1,0.1,20);}),pre:+x.pre||0,rec:String(x.rec||''),date:String(x.date||''),
   credit:String(x.credit||''),licence:String(x.licence||''),source:String(x.source||''),changes:String(x.changes||''),recorded:true};});
 var user=SPACES.length>USER?SPACES[USER]:null;SPACES.length=0;LIBRARY.concat(MODELLED).forEach(function(x){SPACES.push(x);});USER=SPACES.length;if(user)SPACES.push(user);}
/* Up to four spaces for a row of swatches: the chosen one and those after it, wrapping. */
function swatchWindow(s){var n=Math.min(4,SPACES.length),out=[];for(var j=0;j<n;j++)out.push((s.sp+j)%SPACES.length);return out;}
function spaceIndex(id){for(var i=0;i<SPACES.length;i++)if(SPACES[i].id===id)return i;return -1;}
/* A loaded impulse response joins the end of the list, described by the plugin: its
   measured decay and the room size its echo density implies. A missing file keeps its slot
   so the session still says what it used. */
function setUserSpace(ir){SPACES.length=USER;if(!ir)return;var miss=!!ir.missing;
 SPACES.push({id:'user',name:String(ir.name||'Your impulse response'),no:'Your IR',kind:'user',img:'user',place:miss?'File not found':'Your own impulse response',
  W:+ir.W||10,D:+ir.D||14,H:+ir.H||5.5,mat:'Measured from the file',bands:(ir.bands||[1,1,1,1,1,1,1,1]).map(function(b){return clamp(+b||1,0.1,20);}),pre:0,
  rec:miss?'Load it again to hear it':(ir.bformat?'B-format':ir.layout?String(ir.layout)+' surround':+ir.ch===4?'True-stereo':+ir.ch===2?'Stereo':'Mono')+' file, '+Math.round(+ir.rate||48000)+' Hz',date:miss?'file missing':'by you',missing:miss});}
/* The twelve skins. Each is a token set plus a composition in COMPOSE; the ids match the
   CSS (.s-<id>) and the value saved in the plugin state. Order is the picker order. */
var SKINS=[
 {id:'anodised',name:'Anodised',tag:'The slab'},
 {id:'section',name:'Section',tag:'The drawing sheet'},
 {id:'catalogue',name:'Catalogue',tag:'The plate'},
 {id:'field',name:'Field',tag:'The plot is the screen'},
 {id:'vitrine',name:'Vitrine',tag:'The photograph'},
 {id:'signal',name:'Signal',tag:'The poster grid'},
 {id:'aperture',name:'Aperture',tag:'The light'},
 {id:'contact',name:'Contact',tag:'The print'},
 {id:'strata',name:'Strata',tag:'The landscape'},
 {id:'rotunda',name:'Rotunda',tag:'The round'},
 {id:'material',name:'Material',tag:'The surface'},
 {id:'folio',name:'Folio',tag:'The spread'}
];
function skinOf(id){for(var i=0;i<SKINS.length;i++)if(SKINS[i].id===id)return SKINS[i];return SKINS[6];}
var P={
 mix:{l:'Mix',min:0,max:100,st:1,help:'sets the balance of dry and wet. Lock it to keep it when you change space.'},
 decay:{l:'Decay',min:0.2,max:12,st:0.1,log:1,help:'scales the whole tail. Drag a point on the graph to change one band.'},
 pre:{l:'Pre-delay',min:0,max:250,st:1,help:'is the gap before the first reflection.'},
 size:{l:'Size',min:50,max:150,st:1,help:'stretches the room and its reflections.'},
 bright:{l:'Brightness',min:-12,max:12,st:0.5,help:'tilts the wet tone darker or brighter.'},
 el:{l:'Early/Late',min:0,max:100,st:1,help:'balances first reflections against the tail.'},
 dist:{l:'Distance',min:0,max:100,st:1,help:'moves the listener away from the source.'},
 output:{l:'Output',min:-24,max:12,st:0.1,help:'trims the plugin output.'},
 preSync:{l:'Pre-delay sync',min:0,max:1,st:1,help:'locks pre-delay to the host tempo.'},
 rev:{l:'Reverse',min:0,max:1,st:1,help:'plays the room backwards, so it swells up into each sound.'},
 gate:{l:'Gate',min:0,max:2000,st:1,help:'cuts the room off after this long, like a gated drum room. The living tail rests while it is on.'},
 attack:{l:'Attack',min:0,max:500,st:1,help:'fades the room in over this long, so it blooms behind the sound instead of starting with it.'},
 dLvl:{l:'Direct level',min:-60,max:6,st:0.1,help:'sets the level of the first sound to reach the listener in the room, before any reflection. Off leaves only the room.'},
 dDel:{l:'Direct delay',min:0,max:50,st:0.1,help:'moves the direct sound later than the room, for timing or a sense of distance.'},
 dCol:{l:'Direct colour',min:-12,max:12,st:0.5,help:'tilts the direct sound darker or brighter, leaving the room as it is.'},
 xover:{l:'Crossover',min:100,max:1000,st:1,log:1,help:'sets where Freeze stops holding. Below it, a frozen room keeps fading so the bass never builds up.'},
 locut:{l:'Wet low cut',min:20,max:1000,st:1,log:1,help:'removes low end from the wet signal.'},
 hicut:{l:'Wet high cut',min:1000,max:20000,st:10,log:1,help:'removes top end from the wet signal.'},
 width:{l:'Width',min:0,max:200,st:1,help:'narrows or widens the wet signal.'},
 lx:{l:'Listener position',min:0,max:100,st:0.1,help:'moves the listener across the room, from the left wall to the right. On the room map you can drag it.'},
 sx:{l:'Source position',min:0,max:100,st:0.1,help:'moves the sound across the stage, from the left wall to the right. On the room map you can drag it.'},
 cloud:{l:'Cloud size',min:0,max:100,st:1,help:'lets the reverb follow where the sound is panned. At 100 it fills the room; lower gathers it around the sound, so a voice panned to the left rear keeps its reverb there.'},
 xfeed:{l:'Cross-feed',min:0,max:100,st:1,help:'sets how much each side of the input reaches the opposite side of the room. At 0 the left and right get separate reverbs.'},
 duck:{l:'Ducking',min:0,max:100,st:1,help:'turns the tail down while the dry signal plays.'},
 mono:{l:'Mono below',min:0,max:500,st:1,help:'sums the wet signal to mono under this frequency.'},
 living:{l:'Living tail',min:0,max:100,st:1,help:'hands the tail over to a living one fitted to the same room, which can move and never repeats.'},
 move:{l:'Movement',min:0,max:100,st:1,help:'lets the living tail drift slowly, like air moving in the room. Turn up Living tail to hear it.'},
 moveRate:{l:'Movement rate',min:0.05,max:5,st:0.01,log:1,help:'sets how fast the living tail drifts.'},
 cpu:{l:'Low CPU',min:0,max:1,st:1,help:'runs the reverb on about a third of the processing by playing everything 21 ms late. Your DAW makes up the delay, so it stays in time; leave it off when you play live through the reverb.'}
};
/* The wet EQ: six bands on the reverb alone, a low shelf, four bells and a high shelf. Keys are
   eq<n>f, eq<n>g and, for the bells, eq<n>q; the plugin ids are eq<n>Freq, eq<n>Gain, eq<n>Q.
   A band at 0 dB does nothing, so the flat defaults play as the plugin did before the EQ. */
var EQ_N=6,EQ_DEF=[[100,0.71],[250,0.9],[700,0.9],[2000,0.9],[5000,0.9],[10000,0.71]],EQ_NAME=['Low shelf','Band 2','Band 3','Band 4','Band 5','High shelf'];
function eqBell(i){return i>0&&i<EQ_N-1;}
for(var eqi=0;eqi<EQ_N;eqi++){var eqn=eqi+1,eqs=eqi===0?'below':eqi===EQ_N-1?'above':'around';
 P['eq'+eqn+'f']={l:'EQ '+eqn+' frequency',min:20,max:20000,st:1,log:1,help:eqBell(eqi)?'sets the centre of band '+eqn+' of the wet EQ.':'sets where the '+EQ_NAME[eqi].toLowerCase()+' of the wet EQ turns over.'};
 P['eq'+eqn+'g']={l:'EQ '+eqn+' gain',min:-18,max:18,st:0.1,help:'lifts or cuts the reverb '+eqs+' this frequency. The dry sound is not changed, and at 0 dB the band does nothing.'};
 if(eqBell(eqi))P['eq'+eqn+'q']={l:'EQ '+eqn+' Q',min:0.1,max:10,st:0.01,log:1,help:'sets how narrow band '+eqn+' is. Higher is narrower.'};}
function eqDefaults(o){for(var i=0;i<EQ_N;i++){o['eq'+(i+1)+'f']=EQ_DEF[i][0];o['eq'+(i+1)+'g']=0;if(eqBell(i))o['eq'+(i+1)+'q']=EQ_DEF[i][1];}return o;}
function eqActive(s,i){return Math.abs(s['eq'+(i+1)+'g'])>=0.01;}
function eqFlat(s){for(var i=0;i<EQ_N;i++)if(eqActive(s,i))return false;return true;}
function eqUsed(s){var n=0;for(var i=0;i<EQ_N;i++)if(eqActive(s,i))n++;return n;}
var DEEP=['lx','sx','output','preSync','rev','gate','attack','dLvl','dDel','dCol','xover','living','move','moveRate','locut','hicut','width','cloud','xfeed','duck','mono','cpu'];
var TOGGLES=['preSync','rev','cpu'];
var MAIN=['decay','pre','size','bright','el','dist'];
function clamp(v,a,b){return Math.max(a,Math.min(b,v));}
function lerp(a,b,t){return a+(b-a)*t;}
function f1(n){return (Math.round(n*10)/10).toFixed(1);}
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;');}
var UID=0;function uid(){return 'u'+(++UID);}
function vol(sp){var v=sp.W*sp.D*sp.H;v=v>2000?Math.round(v/100)*100:Math.round(v/10)*10;return v.toLocaleString('en-GB')+' m\u00b3';}
function decayOf(b){return (b[3]+b[4])/2;}
/* The decay curve is one for both sides while linked. Unlinked, bands is the left side's and
   rbands the right's; side says which one the graph, its points and its readouts edit, and the
   other is drawn faintly behind it. */
function curBands(s){return !s.link&&s.side==='R'?s.rbands:s.bands;}
function ghostBands(s){return s.link?null:s.side==='R'?s.bands:s.rbands;}
function sideName(s){return s.side==='R'?'Right':'Left';}
/* the aria label of a point or a typed band value, with its side while unlinked */
function bandLabel(s,i){return (s.link?'':sideName(s)+' ')+(s.link?'Decay':'decay')+' at '+FREQ[i]+'Hz';}
function newState(){var sp=SPACES[0];return eqDefaults({sp:0,bands:sp.bands.slice(),rbands:sp.bands.slice(),link:true,side:'L',mix:30,pre:sp.pre,size:100,bright:0,el:55,dist:40,lx:50,sx:50,output:0,preSync:0,xover:320,locut:80,hicut:14000,width:100,duck:0,mono:0,living:0,move:20,moveRate:0.7,rev:0,gate:0,attack:0,dLvl:0,dDel:0,dCol:0,xfeed:100,cloud:100,cpu:0,mode:'studio',slot:'A',slots:{A:null,B:null},freeze:false,adv:false,browse:false,view:'plan',locks:{},hint:null,note:null,lastBand:4,skin:'aperture',skins:false,inL:-60,inR:-60,outL:-60,outR:-60});}
var st=newState();
function spOf(s){return SPACES[s.sp];}
function getV(s,k){return k==='decay'?decayOf(curBands(s)):s[k];}
function natV(s,k){var sp=spOf(s);return {decay:decayOf(sp.bands),pre:sp.pre,size:100,bright:0,el:55,dist:40}[k];}
function isOver(s,k){if(k==='decay'){var sp=spOf(s);return curBands(s).some(function(b,i){return Math.abs(b-sp.bands[i])>0.004;});}var n=natV(s,k);return n!=null&&Math.abs(getV(s,k)-n)>P[k].st/2;}
function distM(s){var sp=spOf(s),D=sp.D*s.size/100,lo=Math.min(1.5,D*0.15);return lo+s.dist/100*(D*0.85-lo);}
function distFromM(s,m){var sp=spOf(s),D=sp.D*s.size/100,lo=Math.min(1.5,D*0.15);return clamp((m-lo)/(D*0.85-lo)*100,0,100);}
function eqp(el){var a=el/100*Math.PI/2;function d(x){return x<0.001?'\u2212\u221e':(20*Math.log10(x)).toFixed(1).replace('-','\u2212');}return {e:d(Math.cos(a)),t:d(Math.sin(a))};}
function hzText(v){return v<1000?Math.round(v)+' Hz':(+(v/1000).toFixed(2))+' kHz';}
function dbSigned(v){return (v>0?'+':v<0?'\u2212':'')+Math.abs(v).toFixed(1)+' dB';}
function fmt(s,k,v){if(v==null)v=getV(s,k);
 if(/^eq\d[fgq]$/.test(k)){var ek=k.charAt(3);return ek==='f'?hzText(v):ek==='g'?dbSigned(v):v.toFixed(2);}
 switch(k){
 case 'mix':return Math.round(v)+'%';case 'decay':return f1(v)+' s';case 'pre':return Math.round(v)+' ms';case 'size':return Math.round(v)+'%';
 case 'bright':return (v>0?'+':v<0?'\u2212':'')+Math.abs(v).toFixed(1)+' dB';case 'el':return (100-Math.round(v))+' / '+Math.round(v);
 case 'dist':var old=s.dist;s.dist=v;var m=distM(s);s.dist=old;return f1(m)+' m';
 case 'output':return (v>0?'+':v<0?'\u2212':'')+Math.abs(v).toFixed(1)+' dB';case 'preSync':case 'rev':case 'cpu':return v>=0.5?'On':'Off';
 case 'xover':case 'locut':return Math.round(v)+' Hz';case 'hicut':return (v/1000).toFixed(1)+' kHz';
 case 'lx':case 'sx':var off=Math.round((v-50)*2);return off===0?'Centre':Math.abs(off)+'% '+(off<0?'left':'right');case 'width':case 'cloud':case 'xfeed':return Math.round(v)+'%';case 'duck':return v>0?Math.round(v)+'%':'Off';case 'mono':return v>0?Math.round(v)+' Hz':'Off';
 case 'living':return v>0?Math.round(v)+'%':'Off';case 'move':return v>0?Math.round(v)+'%':'Still';
 case 'moveRate':return (v<1?v.toFixed(2):v.toFixed(1))+' Hz';case 'gate':case 'attack':return v>0?Math.round(v)+' ms':'Off';
 case 'dLvl':return v<=-60?'Off':(v>0?'+':v<0?'\u2212':'')+Math.abs(v).toFixed(1)+' dB';case 'dDel':return f1(v)+' ms';
 case 'dCol':return (v>0?'+':v<0?'\u2212':'')+Math.abs(v).toFixed(1)+' dB';}}
function to01(k,v){var p=P[k];return p.log?Math.log(v/p.min)/Math.log(p.max/p.min):(v-p.min)/(p.max-p.min);}
function from01(k,n){var p=P[k];n=clamp(n,0,1);var v=p.log?p.min*Math.pow(p.max/p.min,n):p.min+n*(p.max-p.min);return Math.round(v/p.st)*p.st;}
function setParam(s,k,v){var p=P[k];v=clamp(Math.round(v/p.st)*p.st,p.min,p.max);
 if(k==='decay'){var cur=decayOf(curBands(s));if(cur>0){var fct=v/cur,sc=function(b){return clamp(b*fct,0.1,20);};s.bands=s.bands.map(sc);if(!s.link)s.rbands=s.rbands.map(sc);}}else s[k]=v;s.hint=k;s.note=null;}

/* ---- drawing helpers ---- */
function arcD(c,r,a0,a1){var p=Math.PI/180,x0=c+r*Math.cos(a0*p),y0=c+r*Math.sin(a0*p),x1=c+r*Math.cos(a1*p),y1=c+r*Math.sin(a1*p);return 'M'+x0.toFixed(1)+' '+y0.toFixed(1)+'A'+r+' '+r+' 0 '+((a1-a0)>180?1:0)+' 1 '+x1.toFixed(1)+' '+y1.toFixed(1);}
function knobSVG(v01,size,sysId){var c=size/2,r=size/2-4,a0=135,a1=135+270*clamp(v01,0,1),pa=a1*Math.PI/180,rv=sysId==='anodised'?r-5:r,s='<svg width="'+size+'" height="'+size+'" viewBox="0 0 '+size+' '+size+'" aria-hidden="true">';
 if(sysId==='anodised'){for(var i=0;i<=20;i++){var a=(135+13.5*i)*Math.PI/180,L=i%2?2:4;s+='<line class="k-tk" x1="'+(c+Math.cos(a)*(r-1)).toFixed(1)+'" y1="'+(c+Math.sin(a)*(r-1)).toFixed(1)+'" x2="'+(c+Math.cos(a)*(r-1+L)).toFixed(1)+'" y2="'+(c+Math.sin(a)*(r-1+L)).toFixed(1)+'"/>';}}
 s+='<path class="k-tr" d="'+arcD(c,rv,135,405)+'"/>';if(v01>0.004)s+='<path class="k-val" d="'+arcD(c,rv,a0,a1)+'"/>';
 s+='<circle class="k-body" cx="'+c+'" cy="'+c+'" r="'+(rv*0.72).toFixed(1)+'"/>';
 var i0=sysId==='catalogue'?0.78:0.32,i1=sysId==='catalogue'?0.8:0.64;
 s+='<line class="k-ptr" x1="'+(c+Math.cos(pa)*rv*i0).toFixed(1)+'" y1="'+(c+Math.sin(pa)*rv*i0).toFixed(1)+'" x2="'+(c+Math.cos(pa)*rv*i1).toFixed(1)+'" y2="'+(c+Math.sin(pa)*rv*i1).toFixed(1)+'"/>';return s+'</svg>';}
function ariaFor(s,k){var p=P[k];return 'role="slider" tabindex="0" aria-label="'+p.l+'" aria-valuemin="'+p.min+'" aria-valuemax="'+p.max+'" aria-valuenow="'+(+getV(s,k).toFixed(2))+'" aria-valuetext="'+esc(fmt(s,k))+'"';}
function knobEl(s,k,size,sysId){return '<div class="kn" data-knob="'+k+'" data-fid="kn-'+k+'" '+ariaFor(s,k)+'>'+knobSVG(to01(k,getV(s,k)),size,sysId)+'</div>';}
function slideEl(s,k){var n=(to01(k,getV(s,k))*100).toFixed(1);return '<div class="sl" data-knob="'+k+'" data-h="1" data-fid="kn-'+k+'" '+ariaFor(s,k)+'><div class="sl-t"></div><div class="sl-f" style="width:'+n+'%"></div><div class="sl-d" style="left:'+n+'%"></div></div>';}
function roEl(s,k,cls){return '<input class="ro '+(cls||'')+'" data-type="'+k+'" data-fid="ro-'+k+'" value="'+esc(fmt(s,k))+'" aria-label="'+P[k].l+' value" spellcheck="false">';}
function ovrEl(s,k){return isOver(s,k)?'<span class="ovr" title="Changed from the captured space" aria-label="changed from captured"></span>':'';}
var ICON={lockOn:'<svg width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><rect x="3" y="7" width="10" height="7" rx="1.5"/><path d="M5.5 7V5a2.5 2.5 0 015 0v2"/></svg>',
 lockOff:'<svg width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true"><rect x="3" y="7" width="10" height="7" rx="1.5"/><path d="M5.5 7V5a2.5 2.5 0 014.9-.6"/></svg>',
 prev:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10 3L5 8l5 5"/></svg>',
 next:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M6 3l5 5-5 5"/></svg>',
 down:'<svg width="12" height="12" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 6l4 4 4-4"/></svg>',
 freeze:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true"><path d="M8 1.5v13M2.4 4.75l11.2 6.5M2.4 11.25l11.2-6.5"/></svg>',
 match:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true"><path d="M2 8h1.5M5 5v6M8 3v10M11 5.5v5M14 8h-.5"/></svg>',
 more:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true"><path d="M2 4h12M2 8h12M2 12h7"/></svg>',
 eq:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true"><path d="M1.5 10.5c3 0 3.5-6 6.5-6s3.5 6 6.5 6"/></svg>',
 wf:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M1.5 13.5c2.5 0 3-6 5-6s2.5 6 8 6"/><path d="M4 10c2 0 2.5-4.5 4-4.5S10.5 10 14 10" opacity=".6"/><path d="M6.5 6.5c1.5 0 1.8-3 3-3s1.5 3 4.5 3" opacity=".35"/></svg>'};
/* the way into the wet EQ window; the diamond shows when the EQ is doing something */
function eqMark(s){return eqFlat(s)?'':'<span class="ovr" title="The wet EQ is in use" aria-label="in use"></span>';}
function eqBtn(s,cls,label,icon){return '<button class="'+cls+'" data-act="eq" aria-haspopup="dialog" aria-expanded="'+!!s.eq+'">'+(icon?ICON.eq:'')+(label||'Wet EQ')+eqMark(s)+'</button>';}
/* the way into the waterfall window: the playing response analysed, and the live spectrum */
function wfBtn(s,cls,label,icon){return '<button class="'+cls+'" data-act="wf" aria-haspopup="dialog" aria-expanded="'+!!s.wf+'">'+(icon?ICON.wf:'')+(label||'Waterfall')+'</button>';}
function lockBtn(s,k,label){var on=!!s.locks[k];return '<button class="lk" data-act="lock" data-k="'+k+'" aria-pressed="'+on+'" aria-label="Lock '+label+' when changing space" title="Lock '+label+'">'+(on?ICON.lockOn:ICON.lockOff)+'</button>';}
function smooth(pts){var d='M'+pts[0][0].toFixed(1)+' '+pts[0][1].toFixed(1);for(var i=0;i<pts.length-1;i++){var p0=pts[i-1]||pts[i],p1=pts[i],p2=pts[i+1],p3=pts[i+2]||p2;d+='C'+(p1[0]+(p2[0]-p0[0])/6).toFixed(1)+' '+(p1[1]+(p2[1]-p0[1])/6).toFixed(1)+' '+(p2[0]-(p3[0]-p1[0])/6).toFixed(1)+' '+(p2[1]-(p3[1]-p1[1])/6).toFixed(1)+' '+p2[0].toFixed(1)+' '+p2[1].toFixed(1);}return d;}
function niceMax(v){var c=[1,1.5,2,3,4,5,6,8,10,12,16,20];for(var i=0;i<c.length;i++)if(c[i]>=v)return c[i];return 24;}
function rtAt(b,x){var i=Math.floor(x),t=x-i;if(i>=7)return b[7];if(i<0)return b[0];return lerp(b[i],b[i+1],t);}
function hash(a,b){var x=Math.sin(a*12.9898+b*78.233)*43758.5453;return x-Math.floor(x);}
var RAMP=[[0x1B,0x0C,0x41],[0xB5,0x36,0x79],[0xFB,0x9B,0x06],[0xFC,0xFF,0xA4]];
function ramp(e){var x=clamp(e,0,1)*3,i=Math.min(2,Math.floor(x)),t=x-i,a=RAMP[i],b=RAMP[i+1];return 'rgb('+Math.round(lerp(a[0],b[0],t))+','+Math.round(lerp(a[1],b[1],t))+','+Math.round(lerp(a[2],b[2],t))+')';}

/* decay graph. o: {bars, heat, mist, dims, m:[L,R,T,B], noY, big, frame} */
function decaySVG(sysId,s,w,h,o){o=o||{};var sp=spOf(s),b=curBands(s),g=ghostBands(s),nat=sp.bands,bars=!!o.bars,m=o.m||[bars?14:44,16,bars?30:18,26],L=m[0],R=m[1],T=m[2],B=m[3],pw=w-L-R,ph=h-T-B;
 var ymax=niceMax(Math.max.apply(null,b.concat(nat,g||[]))*1.15);
 function X(i){return bars?L+pw*(i+0.5)/8:L+pw*i/7;}function Y(v){return T+ph*(1-v/ymax);}
 var u=uid(),out='<svg class="dg" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" data-decay="1" data-l="'+L+'" data-t="'+T+'" data-pw="'+pw+'" data-ph="'+ph+'" data-ymax="'+ymax+'" data-w="'+w+'" data-bars="'+(bars?1:0)+'" role="group" aria-label="'+graphLabel(s)+' Drag to edit a band, or focus a point and use the arrow keys.">';
 if(o.heat){var cols=o.cols||64,rows=o.rows||36,cw=pw/cols,rh=ph/rows;for(var c=0;c<cols;c++){var x01=(c+0.5)/cols,rt=rtAt(b,x01*7);for(var r=0;r<rows;r++){var t=(rows-r-0.5)/rows*ymax,dB=-60*t/rt+(hash(c,r)-0.5)*6-x01*4,e=1+dB/60;if(e<=0.02)continue;out+='<rect x="'+(L+c*cw).toFixed(1)+'" y="'+(T+r*rh).toFixed(1)+'" width="'+(cw+0.6).toFixed(1)+'" height="'+(rh+0.6).toFixed(1)+'" fill="'+ramp(e)+'"/>';}}
  if(o.frame!==false)out+='<rect class="frm" x="'+L+'" y="'+T+'" width="'+pw+'" height="'+ph+'"/>';
  if(o.cb){out+='<defs><linearGradient id="'+u+'cb" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#1B0C41"/><stop offset=".33" stop-color="#B53679"/><stop offset=".66" stop-color="#FB9B06"/><stop offset="1" stop-color="#FCFFA4"/></linearGradient></defs><rect x="'+(w-R+14)+'" y="'+T+'" width="6" height="'+ph+'" fill="url(#'+u+'cb)"/><text x="'+(w-R+26)+'" y="'+(T+8)+'">0 dB</text><text x="'+(w-R+26)+'" y="'+(T+ph/2+4)+'">\u221230</text><text x="'+(w-R+26)+'" y="'+(T+ph)+'">\u221260</text>';}}
 if(o.mist){out+='<defs><filter id="'+u+'mf" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="16"/></filter><linearGradient id="'+u+'mg" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#8FD3C8" stop-opacity=".5"/><stop offset="1" stop-color="#8FD3C8" stop-opacity="0"/></linearGradient></defs><g filter="url(#'+u+'mf)">';for(var mm=0;mm<28;mm++){var xm=mm/27,yv=Y(rtAt(b,xm*7));out+='<rect x="'+(L+xm*pw-14).toFixed(1)+'" y="'+yv.toFixed(1)+'" width="28" height="'+(T+ph-yv+10).toFixed(1)+'" fill="url(#'+u+'mg)" opacity="'+(0.5+hash(mm,3)*0.5).toFixed(2)+'"/>';}out+='</g>';}
 var stp=ymax<=1.5?0.25:ymax<=3?0.5:ymax<=6?1:ymax<=12?2:4;
 for(var v=0;v<=ymax+1e-6;v+=stp){var yy=Y(v).toFixed(1);out+='<line class="gr" x1="'+L+'" x2="'+(L+pw)+'" y1="'+yy+'" y2="'+yy+'"/>';if(!bars&&!o.noY)out+='<text x="'+(L-8)+'" y="'+(+yy+4)+'" text-anchor="end">'+(+v.toFixed(2))+(v===0?'':' s')+'</text>';}
 var xy=o.xy!=null?o.xy:h-8;for(var i=0;i<8;i++){out+='<text x="'+X(i).toFixed(1)+'" y="'+xy+'" text-anchor="middle">'+(bars?FREQ[i].toUpperCase():FREQ[i])+(i===7&&!bars?' Hz':'')+'</text>';}
 if(bars){var bw=o.bw||pw/8-12;for(i=0;i<8;i++){var ed=Math.abs(b[i]-nat[i])>0.004,y0=Y(b[i]);if(o.track)out+='<rect class="trk" x="'+(X(i)-bw/2).toFixed(1)+'" y="'+T+'" width="'+bw.toFixed(1)+'" height="'+ph+'"/>';out+='<rect class="bar'+(ed?' ed':'')+'" data-node="'+i+'" data-fid="nd-'+i+'" x="'+(X(i)-bw/2).toFixed(1)+'" y="'+y0.toFixed(1)+'" width="'+bw.toFixed(1)+'" height="'+(T+ph-y0).toFixed(1)+'" tabindex="0" role="slider" '+nodeAria(s,i,b[i])+'/>';
   if(g){var gy=Y(g[i]);out+='<rect class="gbar" x="'+(X(i)-bw/2+0.5).toFixed(1)+'" y="'+gy.toFixed(1)+'" width="'+(bw-1).toFixed(1)+'" height="'+Math.max(0,T+ph-gy).toFixed(1)+'"/>';}
   out+='<line class="cap-t" x1="'+(X(i)-bw/2-3).toFixed(1)+'" x2="'+(X(i)+bw/2+3).toFixed(1)+'" y1="'+Y(nat[i]).toFixed(1)+'" y2="'+Y(nat[i]).toFixed(1)+'"/>';
   out+='<text class="bv" x="'+X(i).toFixed(1)+'" y="'+(Math.min(y0,Y(nat[i]))-7).toFixed(1)+'" text-anchor="middle">'+b[i].toFixed(b[i]<1?2:1)+(ed?'*':'')+'</text>';}return out+sideCap(s,L+4,12)+'</svg>';}
 out+='<path class="dg-cap" d="'+smooth(nat.map(function(v,i){return [X(i),Y(v)];}))+'"/>';
 if(g){out+='<path class="dg-gho" d="'+smooth(g.map(function(v,i){return [X(i),Y(v)];}))+'"/>';for(i=0;i<8;i++)out+='<circle class="gnd" cx="'+X(i).toFixed(1)+'" cy="'+Y(g[i]).toFixed(1)+'" r="2.5"/>';}
 out+='<path class="dg-cur" d="'+smooth(b.map(function(v,i){return [X(i),Y(v)];}))+'"/>';
 if(o.dims){var lb=s.lastBand,dx=X(lb)+16,y1=Y(b[lb]),y2=Y(0);out+='<path class="dim" d="M'+dx+' '+y1.toFixed(1)+'V'+y2+'M'+(dx-4)+' '+(y1+4).toFixed(1)+'l8 -8M'+(dx-4)+' '+(y2+4)+'l8 -8M'+(X(lb)+5)+' '+y1.toFixed(1)+'H'+(dx+6)+'"/><text class="dimt" x="'+(dx+8)+'" y="'+((y1+y2)/2+4).toFixed(1)+'">'+(s.link?'':sideName(s)+', ')+b[lb].toFixed(2)+' s at '+FREQ[lb]+' Hz</text>';}
 for(i=0;i<8;i++){var e2=Math.abs(b[i]-nat[i])>0.004,cx=X(i).toFixed(1),cy=Y(b[i]).toFixed(1),cl='nd'+(e2?' ed':'')+(i===s.lastBand?' cur':''),ar=' data-node="'+i+'" data-fid="nd-'+i+'" tabindex="0" role="slider" '+nodeAria(s,i,b[i]);
  if(sysId==='section')out+='<rect class="'+cl+'"'+ar+' x="'+(cx-4.5)+'" y="'+(cy-4.5)+'" width="9" height="9"/>';else out+='<circle class="'+cl+'"'+ar+' cx="'+cx+'" cy="'+cy+'" r="'+(o.big?6:sysId==='catalogue'?4.5:5.5)+'"/>';}
 return out+sideCap(s,L+6,T+11)+'</svg>';}
/* the pieces every decay graph shares while the sides are unlinked */
function graphLabel(s){return s.link?'Decay time by frequency.':sideName(s)+' side\u2019s decay time by frequency; the '+(s.side==='R'?'left':'right')+' is drawn faintly.';}
function nodeAria(s,i,v){return 'aria-label="'+bandLabel(s,i)+'" aria-valuemin="0.1" aria-valuemax="20" aria-valuenow="'+v.toFixed(2)+'" aria-valuetext="'+v.toFixed(2)+' seconds"';}
/* a word in the plot naming the side the solid curve and its values belong to */
function sideCap(s,x,y,anchor){return s.link?'':'<text class="sd" x="'+x+'" y="'+y+'"'+(anchor?' text-anchor="'+anchor+'"':'')+'>'+sideName(s)+'</text>';}
/* the Link control: one curve for both sides, or a side to edit with the other shown faintly */
function linkCtl(s,cls){var on=!!s.link;
 return '<span class="dlk '+(cls||'')+'"><button class="dlb" data-act="dlink" aria-pressed="'+on+'" title="'+(on?'One decay for both sides. Click to give the left and right their own.':'The left and right decay at their own curves. Click to link them again.')+'">Link</button>'+
  (on?'':'<span class="dsd" role="group" aria-label="Side to edit"><button data-act="dside" data-v="L" aria-pressed="'+(s.side!=='R')+'">Left</button><button data-act="dside" data-v="R" aria-pressed="'+(s.side==='R')+'">Right</button></span>')+'</span>';}

/* wet EQ response: the Audio EQ Cookbook shelves (slope 1) and peaking bell, as the engine
   designs them (source/engine/include/drenched/Biquad.h), evaluated at 48 kHz */
var EQ_SR=48000,EQ_MIN=20,EQ_MAX=20000,EQ_DB=18;
function eqCoef(i,f,g,q){var A=Math.pow(10,g/40),w=2*Math.PI*clamp(f,1,EQ_SR*0.49)/EQ_SR,c=Math.cos(w),sn=Math.sin(w),b0,b1,b2,a0,a1,a2;
 if(eqBell(i)){var al=sn/(2*q);b0=1+al*A;b1=-2*c;b2=1-al*A;a0=1+al/A;a1=-2*c;a2=1-al/A;}
 else{var sh=2*Math.sqrt(A)*(sn/(2*Math.SQRT1_2)),lo=i===0,p=A+1,m=A-1;
  if(lo){b0=A*(p-m*c+sh);b1=2*A*(m-p*c);b2=A*(p-m*c-sh);a0=p+m*c+sh;a1=-2*(m+p*c);a2=p+m*c-sh;}
  else{b0=A*(p+m*c+sh);b1=-2*A*(m+p*c);b2=A*(p+m*c-sh);a0=p-m*c+sh;a1=2*(m-p*c);a2=p-m*c-sh;}}
 return [b0/a0,b1/a0,b2/a0,a1/a0,a2/a0];}
function eqMagDb(c,f){var w=2*Math.PI*f/EQ_SR,c1=Math.cos(w),s1=Math.sin(w),c2=Math.cos(2*w),s2=Math.sin(2*w);
 var nr=c[0]+c[1]*c1+c[2]*c2,ni=-(c[1]*s1+c[2]*s2),dr=1+c[3]*c1+c[4]*c2,di=-(c[3]*s1+c[4]*s2);return 10*Math.log10((nr*nr+ni*ni)/(dr*dr+di*di));}
function eqBandCoef(s,i){var n=i+1;return eqCoef(i,s['eq'+n+'f'],s['eq'+n+'g'],eqBell(i)?s['eq'+n+'q']:Math.SQRT1_2);}
/* the whole EQ's response at f, in dB; bands at 0 dB are skipped as the engine skips them */
function eqResponse(s,f,only){var t=0;for(var i=0;i<EQ_N;i++){if(only!=null&&i!==only)continue;if(!eqActive(s,i))continue;t+=eqMagDb(eqBandCoef(s,i),f);}return t;}
function eqSVG(s,w,h,cur){var L=36,R=14,T=14,B=22,pw=w-L-R,ph=h-T-B,span=Math.log(EQ_MAX/EQ_MIN);
 function X(f){return L+pw*Math.log(f/EQ_MIN)/span;}function Y(g){return T+ph*(EQ_DB-g)/(2*EQ_DB);}
 function curve(only){var d='';for(var k=0;k<=160;k++){var f=EQ_MIN*Math.exp(span*k/160),y=clamp(Y(eqResponse(s,f,only)),T-3,T+ph+3);d+=(k?'L':'M')+X(f).toFixed(1)+' '+y.toFixed(1);}return d;}
 var out='<svg class="eqg" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" data-eqg="1" data-l="'+L+'" data-t="'+T+'" data-pw="'+pw+'" data-ph="'+ph+'" data-w="'+w+'" role="group" aria-label="Wet EQ response, 20 Hz to 20 kHz, plus or minus 18 dB. Drag a point sideways for frequency and up or down for gain, or focus it and use the arrow keys.">';
 for(var g=-EQ_DB;g<=EQ_DB;g+=6){var yy=Y(g).toFixed(1);out+='<line class="'+(g===0?'zero':'gr')+'" x1="'+L+'" x2="'+(L+pw)+'" y1="'+yy+'" y2="'+yy+'"/><text x="'+(L-6)+'" y="'+(+yy+4)+'" text-anchor="end">'+(g>0?'+':g<0?'\u2212':'')+Math.abs(g)+'</text>';}
 [20,50,100,200,500,1000,2000,5000,10000,20000].forEach(function(f){var xx=X(f).toFixed(1);if(f>20&&f<20000)out+='<line class="gr" x1="'+xx+'" x2="'+xx+'" y1="'+T+'" y2="'+(T+ph)+'"/>';
  out+='<text x="'+xx+'" y="'+(h-6)+'" text-anchor="'+(f===20?'start':f===20000?'end':'middle')+'">'+(f>=1000?(f/1000)+'k':f)+(f===20000?' Hz':'')+'</text>';});
 /* the wet low cut and high cut come first, so the EQ has nothing left to shape beyond them */
 if(s.locut>EQ_MIN){var xl=X(s.locut).toFixed(1);out+='<line class="cut" x1="'+xl+'" x2="'+xl+'" y1="'+T+'" y2="'+(T+ph)+'"/><text class="cutt" x="'+(+xl-5)+'" y="'+(T+10)+'" text-anchor="end">Low cut</text>';}
 if(s.hicut<EQ_MAX){var xh=X(s.hicut).toFixed(1);out+='<line class="cut" x1="'+xh+'" x2="'+xh+'" y1="'+T+'" y2="'+(T+ph)+'"/><text class="cutt" x="'+(+xh+5)+'" y="'+(T+10)+'">High cut</text>';}
 var sum=curve(null);out+='<path class="fill" d="'+sum+'L'+(L+pw)+' '+Y(0).toFixed(1)+'L'+L+' '+Y(0).toFixed(1)+'Z"/>';
 if(cur!=null&&eqActive(s,cur))out+='<path class="one" d="'+curve(cur)+'"/>';
 out+='<path class="sum" d="'+sum+'"/>';
 for(var i=0;i<EQ_N;i++){var n=i+1,f=s['eq'+n+'f'],gn=s['eq'+n+'g'],q=eqBell(i)?s['eq'+n+'q']:null,cx=X(f).toFixed(1),cy=Y(gn).toFixed(1);
  out+='<g class="eqn'+(eqActive(s,i)?' on':'')+(i===cur?' cur':'')+'" data-eqn="'+i+'" data-fid="eqn-'+i+'" tabindex="0" role="slider" aria-label="'+EQ_NAME[i]+(eqBell(i)?', bell':'')+'. Left and right move the frequency, up and down the gain'+(eqBell(i)?', Page Up and Page Down the Q':'')+'." aria-valuemin="-18" aria-valuemax="18" aria-valuenow="'+gn.toFixed(1)+'" aria-valuetext="'+dbSigned(gn)+' at '+hzText(f)+(q?', Q '+q.toFixed(2):'')+'" transform="translate('+cx+' '+cy+')"><circle class="hit" r="14"/><circle r="8"/><text y="3.5">'+n+'</text></g>';}
 return out+'</svg>';}

/* the measured decay of the playing response: the broadband Schroeder curve from 0 dB down to
   -60 dB, drawn from the plugin's analysis (a: {decay, decayStep, seconds}) */
function decayCurveSVG(a,w,h){var L=30,R=12,T=10,B=20,pw=w-L-R,ph=h-T-B;
 var out='<svg class="dcv" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" role="img" aria-label="'+(a?'Broadband decay, reaching \u221260 dB after '+f1(a.seconds)+' seconds':'Broadband decay, waiting for the response')+'">';
 for(var g=0;g>=-60;g-=20){var y=(T+ph*(-g/60)).toFixed(1);out+='<line class="gr" x1="'+L+'" x2="'+(L+pw)+'" y1="'+y+'" y2="'+y+'"/><text x="'+(L-6)+'" y="'+(+y+3.5)+'" text-anchor="end">'+(g?'\u2212'+(-g):'0')+'</text>';}
 if(!a)return out+'</svg>';
 var n=a.decay.length,secs=a.decayStep*(n-1),stp=timeStep(secs),d='';
 for(var t=0;t<=secs+1e-6;t+=stp){var x=(L+pw*t/secs).toFixed(1);if(t)out+='<line class="gr" x1="'+x+'" x2="'+x+'" y1="'+T+'" y2="'+(T+ph)+'"/>';out+='<text x="'+x+'" y="'+(h-6)+'" text-anchor="'+(t?'middle':'start')+'">'+(t?(+t.toFixed(2))+' s':'0')+'</text>';}
 for(var i=0;i<n;i++){var v=clamp(+a.decay[i]||0,-60,0);d+=(i?'L':'M')+(L+pw*i/(n-1)).toFixed(1)+' '+(T+ph*(-v/60)).toFixed(1);}
 return out+'<path class="fill" d="'+d+'L'+(L+pw)+' '+(T+ph)+'L'+L+' '+(T+ph)+'Z"/><path class="cur" d="'+d+'"/></svg>';}
/* a readable spacing of time labels over secs seconds */
function timeStep(secs){return secs>12?4:secs>6?2:secs>3?1:secs>1.5?0.5:secs>0.6?0.25:0.1;}

/* photographs */
function photoEl(s,w,h,extra){var z=1+(1-s.dist/100)*0.22,st2=w?'width:'+w+'px;height:'+h+'px;':'';return '<div class="phw" style="'+st2+(extra||'')+'" role="img" aria-label="'+esc(spOf(s).name)+'"><div class="phi ph-'+spOf(s).img+'" style="transform:scale('+z.toFixed(3)+')'+''+'"></div></div>';}

/* plan drawing (Section) */
function planView(s,w,h){var sp=spOf(s),ss=s.size/100,W=sp.W*ss,D=sp.D*ss,H=sp.H*ss,cp=w<400,m=cp?16:64;
 var out='<svg width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" style="display:block" data-plan="1" data-w="'+w+'" ';
 var stp=W>30?5:W>12?2:1,body='';
 if(s.view==='section'){var sc=Math.min((w-2*m-60)/D,(h-2*m)/H),ox=(w-D*sc)/2,oy=(h+H*sc)/2+8,sd=sp.kind==='hall'?6:sp.kind==='chapel'?4:0;
  out+='data-mode="section" data-ox="'+ox.toFixed(1)+'" data-oy="'+oy.toFixed(1)+'" data-sc="'+sc.toFixed(3)+'">';
  out+='<rect class="pl-w" x="'+ox.toFixed(1)+'" y="'+(oy-H*sc).toFixed(1)+'" width="'+(D*sc).toFixed(1)+'" height="'+(H*sc).toFixed(1)+'"/><rect class="pl-w2" x="'+(ox-4)+'" y="'+(oy-H*sc-4).toFixed(1)+'" width="'+(D*sc+8).toFixed(1)+'" height="'+(H*sc+8).toFixed(1)+'"/>';
  if(sd)out+='<rect class="pl-f" x="'+ox.toFixed(1)+'" y="'+(oy-1.1*sc).toFixed(1)+'" width="'+(sd*sc).toFixed(1)+'" height="'+(1.1*sc).toFixed(1)+'"/>';
  if(sp.kind==='hall'||sp.kind==='chapel'){var pth='M'+(ox+(sd+1)*sc).toFixed(1)+' '+oy.toFixed(1);for(var zz=sd+1;zz<D-1;zz+=2){var rise=sp.kind==='hall'?(zz-sd)*0.12:0;pth+='V'+(oy-rise*sc).toFixed(1)+'H'+(ox+(zz+2)*sc).toFixed(1);}out+='<path class="pl-f" d="'+pth+'"/>';}
  var sx=ox+(sd?sd/2:2)*sc,sy=oy-(sd?2.6:1.6)*sc,lxm=distM(s),lx=ox+lxm*sc,ly=oy-1.2*sc;
  out+='<path class="pl-dir" d="M'+sx.toFixed(1)+' '+sy.toFixed(1)+'L'+lx.toFixed(1)+' '+ly.toFixed(1)+'"/>';
  var mx=(sx+lx)/2,ceil=oy-H*sc;out+='<path class="pl-ray" d="M'+sx.toFixed(1)+' '+sy.toFixed(1)+'L'+mx.toFixed(1)+' '+ceil.toFixed(1)+'L'+lx.toFixed(1)+' '+ly.toFixed(1)+'"/>';
  out+='<path class="pl-d" d="M'+ox.toFixed(1)+' '+(oy+22)+'H'+(ox+D*sc).toFixed(1)+'M'+(ox-4)+' '+(oy+26)+'l8 -8M'+(ox+D*sc-4).toFixed(1)+' '+(oy+26)+'l8 -8"/><text class="pl-tt" x="'+(ox+D*sc/2).toFixed(1)+'" y="'+(oy+40)+'" text-anchor="middle">D '+f1(D)+' m</text>';
  out+='<path class="pl-d" d="M'+(ox-22)+' '+oy.toFixed(1)+'V'+(oy-H*sc).toFixed(1)+'M'+(ox-26)+' '+(oy+4).toFixed(1)+'l8 -8M'+(ox-26)+' '+(oy-H*sc+4).toFixed(1)+'l8 -8"/><text class="pl-tt" x="'+(ox-30)+'" y="'+(oy-H*sc/2+4).toFixed(1)+'" text-anchor="end">H '+f1(H)+' m</text>';
  out+='<circle class="pl-mko" cx="'+sx.toFixed(1)+'" cy="'+sy.toFixed(1)+'" r="6"/><text class="pl-tt" x="'+(sx+10).toFixed(1)+'" y="'+(sy-8).toFixed(1)+'">S1</text>';
  out+='<g class="lis" data-drag="lis" data-fid="lis" tabindex="0" role="slider" aria-label="Listener distance from source" aria-valuetext="'+f1(lxm)+' metres"><circle class="pl-hit" cx="'+lx.toFixed(1)+'" cy="'+ly.toFixed(1)+'" r="16"/><circle class="pl-mk" cx="'+lx.toFixed(1)+'" cy="'+ly.toFixed(1)+'" r="6"/></g><text class="pl-tt" x="'+(lx+10).toFixed(1)+'" y="'+(ly-8).toFixed(1)+'">R1, '+f1(lxm)+' m</text>';
  if(!cp)out+='<text class="pl-t" x="'+(w-8)+'" y="'+(h-8)+'" text-anchor="end">LONGITUDINAL SECTION, '+sp.name.toUpperCase()+'</text>';return out+'</svg>';}
 var sc2=Math.min((w-2*m-(cp?0:120))/W,(h-2*m-(cp?0:30))/D),ox2=(w-W*sc2)/2-(cp?0:20),oy2=(h-D*sc2)/2+(cp?0:16),X=function(x){return ox2+x*sc2;},Y=function(y){return oy2+y*sc2;};
 out+='data-mode="plan" data-ox="'+ox2.toFixed(1)+'" data-oy="'+oy2.toFixed(1)+'" data-sc="'+sc2.toFixed(3)+'" data-rw="'+W.toFixed(2)+'">';
 for(var mx2=0;mx2<=W+0.01;mx2+=stp)out+='<line class="pl-g2" x1="'+X(mx2).toFixed(1)+'" x2="'+X(mx2).toFixed(1)+'" y1="'+(oy2-6)+'" y2="'+(Y(D)+6).toFixed(1)+'" opacity=".6"/>';
 out+='<rect class="pl-w2" x="'+(ox2-4)+'" y="'+(oy2-4)+'" width="'+(W*sc2+8).toFixed(1)+'" height="'+(D*sc2+8).toFixed(1)+'"/><rect class="pl-w" x="'+ox2.toFixed(1)+'" y="'+oy2.toFixed(1)+'" width="'+(W*sc2).toFixed(1)+'" height="'+(D*sc2).toFixed(1)+'"/>';
 var sdp=sp.kind==='hall'?6:sp.kind==='chapel'?4:0;
 if(sdp){out+='<rect class="pl-f" x="'+X(W*0.075).toFixed(1)+'" y="'+oy2.toFixed(1)+'" width="'+(W*0.85*sc2).toFixed(1)+'" height="'+(sdp*sc2).toFixed(1)+'"/>';if(!cp)out+='<text class="pl-t" x="'+X(W*0.09).toFixed(1)+'" y="'+(Y(sdp)-6).toFixed(1)+'">'+(sp.kind==='hall'?'STAGE':'CHANCEL')+'</text>';
  var rs=sp.kind==='hall'?1.15:1.7;for(var ry=sdp+1.5;ry<D-1;ry+=rs){out+='<path class="pl-f" d="M'+X(0.9).toFixed(1)+' '+Y(ry).toFixed(1)+'H'+X(W/2-0.9).toFixed(1)+'M'+X(W/2+0.9).toFixed(1)+' '+Y(ry).toFixed(1)+'H'+X(W-0.9).toFixed(1)+'"/>';}}
 if(sp.kind==='chapel'||sp.kind==='garage'){var cx3=sp.kind==='chapel'?[W*0.17,W*0.83]:[W*0.14,W*0.38,W*0.62,W*0.86];for(var cz=3;cz<D-1;cz+=(sp.kind==='chapel'?4.5:8)){cx3.forEach(function(x){out+='<rect class="pl-fl" x="'+(X(x)-2.5).toFixed(1)+'" y="'+(Y(cz)-2.5).toFixed(1)+'" width="5" height="5"/>';});}}
 if(sp.kind==='room'){out+='<path class="pl-f" d="M'+X(W*0.2).toFixed(1)+' '+Y(0.4).toFixed(1)+'H'+X(W*0.8).toFixed(1)+'"/>';if(!cp)out+='<text class="pl-t" x="'+X(W*0.2).toFixed(1)+'" y="'+(Y(0.4)+14).toFixed(1)+'">DIFFUSERS</text>';}
 var sX=X(s.sx/100*W),sY=Y(sdp?sdp*0.55:1.5),lm=distM(s),lX=X(s.lx/100*W),lY=Y(lm),dB=Math.hypot(lX-sX,lY-sY);
 [1,1.8,2.7].forEach(function(f,i2){out+='<circle class="pl-ring" cx="'+sX.toFixed(1)+'" cy="'+sY.toFixed(1)+'" r="'+(Math.min(dB,200)*f*0.38+10).toFixed(1)+'" opacity="'+(0.7-i2*0.2)+'"/>';});
 [ox2,X(W)].forEach(function(wx,i3){var ix=i3===0?-sX+2*ox2:2*X(W)-sX,t=(wx-ix)/(lX-ix),py=sY+(lY-sY)*t;out+='<path class="pl-ray" d="M'+sX.toFixed(1)+' '+sY.toFixed(1)+'L'+wx.toFixed(1)+' '+py.toFixed(1)+'L'+lX.toFixed(1)+' '+lY.toFixed(1)+'" opacity=".8"/>';});
 out+='<path class="pl-dir" d="M'+sX.toFixed(1)+' '+sY.toFixed(1)+'L'+lX.toFixed(1)+' '+lY.toFixed(1)+'"/>';
 if(cp)return out+'<circle class="pl-mko" cx="'+sX.toFixed(1)+'" cy="'+sY.toFixed(1)+'" r="4"/><circle class="pl-mk" cx="'+lX.toFixed(1)+'" cy="'+lY.toFixed(1)+'" r="4"/></svg>';
 out+='<path class="pl-d" d="M'+ox2.toFixed(1)+' '+(oy2-24)+'H'+X(W).toFixed(1)+'M'+(ox2-4).toFixed(1)+' '+(oy2-20)+'l8 -8M'+(X(W)-4).toFixed(1)+' '+(oy2-20)+'l8 -8M'+ox2.toFixed(1)+' '+(oy2-30)+'V'+(oy2-8)+'M'+X(W).toFixed(1)+' '+(oy2-30)+'V'+(oy2-8)+'"/><text class="pl-tt" x="'+X(W/2).toFixed(1)+'" y="'+(oy2-30)+'" text-anchor="middle">W '+f1(W)+' m</text>';
 out+='<path class="pl-d" d="M'+(ox2-24)+' '+oy2.toFixed(1)+'V'+Y(D).toFixed(1)+'M'+(ox2-28)+' '+(oy2+4).toFixed(1)+'l8 -8M'+(ox2-28)+' '+(Y(D)+4).toFixed(1)+'l8 -8"/><text class="pl-tt" x="'+(ox2-32)+'" y="'+Y(D/2).toFixed(1)+'" text-anchor="end">D</text><text class="pl-tt" x="'+(ox2-32)+'" y="'+(Y(D/2)+15).toFixed(1)+'" text-anchor="end">'+f1(D)+' m</text>';
 var dX=X(W)+18;out+='<path class="pl-d" d="M'+dX.toFixed(1)+' '+sY.toFixed(1)+'V'+lY.toFixed(1)+'M'+(dX-4).toFixed(1)+' '+(sY+4).toFixed(1)+'l8 -8M'+(dX-4).toFixed(1)+' '+(lY+4).toFixed(1)+'l8 -8M'+(lX+10).toFixed(1)+' '+lY.toFixed(1)+'H'+(dX+4).toFixed(1)+'" opacity=".8"/><text class="pl-tt" x="'+(dX+8).toFixed(1)+'" y="'+((sY+lY)/2+4).toFixed(1)+'">'+f1(Math.abs(lm-(sdp?sdp*0.55:1.5)))+' m</text><text class="pl-t" x="'+(dX+8).toFixed(1)+'" y="'+((sY+lY)/2+18).toFixed(1)+'">S1 to R1</text>';
 out+='<g class="lis" data-drag="src" data-fid="src" tabindex="0" role="slider" aria-label="Source position. Left and right move it across the stage." aria-valuetext="'+fmt(s,'sx')+'"><circle class="pl-hit" cx="'+sX.toFixed(1)+'" cy="'+sY.toFixed(1)+'" r="16"/><circle class="pl-mko" cx="'+sX.toFixed(1)+'" cy="'+sY.toFixed(1)+'" r="6"/></g><text class="pl-tt" x="'+(sX+10).toFixed(1)+'" y="'+(sY-8).toFixed(1)+'">S1</text>';
 out+='<g class="lis" data-drag="lis" data-fid="lis" tabindex="0" role="slider" aria-label="Listener position. Up and down change distance, left and right move across the room." aria-valuetext="'+f1(lm)+' metres from the front"><circle class="pl-hit" cx="'+lX.toFixed(1)+'" cy="'+lY.toFixed(1)+'" r="16"/><circle class="pl-mk" cx="'+lX.toFixed(1)+'" cy="'+lY.toFixed(1)+'" r="6"/></g><text class="pl-tt" x="'+(lX+10).toFixed(1)+'" y="'+(lY+18).toFixed(1)+'">R1</text>';
 var sb=stp*sc2;out+='<path class="pl-d" d="M'+(w-16-sb*2).toFixed(1)+' '+(h-22)+'H'+(w-16)+'M'+(w-16-sb*2).toFixed(1)+' '+(h-26)+'V'+(h-18)+'M'+(w-16-sb).toFixed(1)+' '+(h-26)+'V'+(h-18)+'M'+(w-16)+' '+(h-26)+'V'+(h-18)+'"/><text class="pl-t" x="'+(w-16-sb*2).toFixed(1)+'" y="'+(h-30)+'">0</text><text class="pl-t" x="'+(w-16)+'" y="'+(h-30)+'" text-anchor="end">'+(stp*2)+' m</text>';
 return out+'</svg>';}

/* pixel room (Signal) */
function pixelRoom(s,w,h){var sp=spOf(s),ss=s.size/100,cell=12,cols=Math.floor((w-16)/cell),rows=Math.floor((h-36)/cell),ox=(w-cols*cell)/2,oy=6;
 var k=Math.min((cols-4)/sp.W,(rows-4)/sp.D)*(0.55+0.45*(s.size-50)/100),rw=Math.max(6,Math.round(sp.W*k)),rh=Math.max(6,Math.round(sp.D*k));rw=Math.min(rw,cols-2);rh=Math.min(rh,rows-2);
 var c0=Math.floor((cols-rw)/2),r0=Math.floor((rows-rh)/2),sc=c0+1+Math.round(s.sx/100*(rw-3)),sr=r0+2,lc=c0+1+Math.round(s.lx/100*(rw-3)),D=sp.D*ss,lr=r0+1+Math.round(clamp(distM(s)/D,0,1)*(rh-3)),reach=rh*clamp(decayOf(s.bands)/3,0.3,1.2);
 var out='<svg width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" aria-hidden="true" style="display:block">';
 for(var r=0;r<rows;r++)for(var c=0;c<cols;c++){var x=ox+c*cell,y=oy+r*cell,inR=c>=c0&&c<c0+rw&&r>=r0&&r<r0+rh,wall=inR&&(c===c0||c===c0+rw-1||r===r0||r===r0+rh-1),fill=null;
  if(wall)fill='var(--tx)';else if(inR){var d=Math.hypot(c-sc,r-sr);if(d<reach&&(Math.floor(d)%3===0))fill='var(--ln)';}
  if(c===sc&&r===sr)fill='var(--ac)';if(c===lc&&r===lr)fill='var(--wet)';
  if(fill)out+='<rect x="'+(x+1)+'" y="'+(y+1)+'" width="'+(cell-2)+'" height="'+(cell-2)+'" fill="'+fill+'"'+(c===lc&&r===lr?' stroke="var(--tx)" stroke-width="1.5"':'')+'/>';
  else out+='<rect x="'+(x+cell/2-1)+'" y="'+(y+cell/2-1)+'" width="2" height="2" fill="var(--ln)"/>';}
 out+='<rect x="'+ox+'" y="'+(h-24)+'" width="10" height="10" fill="var(--ac)"/><text x="'+(ox+16)+'" y="'+(h-15)+'" font-size="10" font-family="var(--fnum)" fill="var(--tx2)" style="letter-spacing:.1em">SOURCE</text><rect x="'+(ox+88)+'" y="'+(h-24)+'" width="10" height="10" fill="var(--wet)" stroke="var(--tx)" stroke-width="1.5"/><text x="'+(ox+104)+'" y="'+(h-15)+'" font-size="10" font-family="var(--fnum)" fill="var(--tx2)" style="letter-spacing:.1em">LISTENER '+f1(distM(s))+' M</text>';
 return out+'</svg>';}

/* ---- helpers added for the twelve-direction page ---- */
function numCtl(s,k,cls,lab){return '<div class="nc '+(cls||'')+'" data-knob="'+k+'" data-h="1" data-fid="kn-'+k+'" '+ariaFor(s,k)+'>'+roEl(s,k)+'<span class="nc-l">'+(lab||P[k].l)+ovrEl(s,k)+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</span></div>';}
/* a typed value for one band of the side being edited */
function bandInput(s,sp,i,cls){var v=curBands(s)[i],ed=Math.abs(v-sp.bands[i])>0.004;return '<input class="ro'+(ed?' ed':'')+(cls?' '+cls:'')+'" data-type="band" data-i="'+i+'" data-fid="bi-'+i+'" value="'+v.toFixed(2)+'" aria-label="'+bandLabel(s,i)+', seconds" spellcheck="false">';}
function bandInputs(s,sp,hz){var out='';for(var i=0;i<8;i++)out+='<div>'+bandInput(s,sp,i)+'<span>'+FREQ[i]+(i===7&&hz!==false?' Hz':'')+'</span></div>';return out;}
/* polar decay graph (Rotunda): band as spoke, time as radius */
function polarSVG(sysId,s,size,o){o=o||{};var sp=spOf(s),b=curBands(s),g=ghostBands(s),nat=sp.bands,c=size/2,r0=o.r0||190,r1=o.r1||280,ymax=niceMax(Math.max.apply(null,b.concat(nat,g||[]))*1.15);
 function R(v){return r0+(r1-r0)*v/ymax;}function ang(i){return (-90+i*45)*Math.PI/180;}function pt(i,v){var r=R(v),a=ang(i);return [c+r*Math.cos(a),c+r*Math.sin(a)];}
 function poly(arr){return arr.map(function(v,i){var p=pt(i,v);return (i?'L':'M')+p[0].toFixed(1)+' '+p[1].toFixed(1);}).join('')+'Z';}
 var out='<svg class="dg pol" width="'+size+'" height="'+size+'" viewBox="0 0 '+size+' '+size+'" data-decay="1" data-polar="1" data-cx="'+c+'" data-cy="'+c+'" data-r0="'+r0+'" data-r1="'+r1+'" data-ymax="'+ymax+'" data-w="'+size+'" role="group" aria-label="'+graphLabel(s)+' Drawn around the room: drag a point outward for a longer decay, or focus it and use the arrow keys.">';
 var stp=ymax<=1.5?0.25:ymax<=3?0.5:ymax<=6?1:ymax<=12?2:4;
 var la=-67.5*Math.PI/180;for(var v=stp;v<=ymax+1e-6;v+=stp){out+='<circle class="gr" cx="'+c+'" cy="'+c+'" r="'+R(v).toFixed(1)+'"/><text x="'+(c+(R(v)+3)*Math.cos(la)).toFixed(1)+'" y="'+(c+(R(v)+3)*Math.sin(la)+3).toFixed(1)+'">'+(+v.toFixed(2))+' s</text>';}
 for(var i=0;i<8;i++){var a=ang(i),p0=pt(i,0),p1=pt(i,ymax),rl=r1+24;out+='<line class="spk" x1="'+p0[0].toFixed(1)+'" y1="'+p0[1].toFixed(1)+'" x2="'+p1[0].toFixed(1)+'" y2="'+p1[1].toFixed(1)+'"/><text x="'+(c+rl*Math.cos(a)).toFixed(1)+'" y="'+(c+rl*Math.sin(a)+4).toFixed(1)+'" text-anchor="middle">'+FREQ[i]+(i===7?' Hz':'')+'</text>';}
 out+='<path class="dg-cap" d="'+poly(nat)+'"/>';
 if(g){out+='<path class="dg-gho" d="'+poly(g)+'"/>';for(i=0;i<8;i++){var gp=pt(i,g[i]);out+='<circle class="gnd" cx="'+gp[0].toFixed(1)+'" cy="'+gp[1].toFixed(1)+'" r="2.5"/>';}}
 out+='<path class="dg-cur" d="'+poly(b)+'"/>';
 for(i=0;i<8;i++){var p=pt(i,b[i]),ed=Math.abs(b[i]-nat[i])>0.004,a2=ang(i),rv=R(b[i])+17;
  out+='<circle class="nd'+(ed?' ed':'')+(i===s.lastBand?' cur':'')+'" data-node="'+i+'" data-fid="nd-'+i+'" tabindex="0" role="slider" '+nodeAria(s,i,b[i])+' cx="'+p[0].toFixed(1)+'" cy="'+p[1].toFixed(1)+'" r="6"/>';
  out+='<text class="bv" x="'+(c+rv*Math.cos(a2)).toFixed(1)+'" y="'+(c+rv*Math.sin(a2)+4).toFixed(1)+'" text-anchor="middle">'+b[i].toFixed(b[i]<1?2:1)+(ed?'*':'')+'</text>';}
 return out+sideCap(s,12,size-12)+'</svg>';}
/* ridge decay graph (Strata): one ridge per band, time runs sideways */
function ridgesSVG(s,w,h){var sp=spOf(s),b=curBands(s),g=ghostBands(s),nat=sp.bands,L=64,R=40,T=56,B=28,pw=w-L-R,ph=h-T-B,rh=ph/8,amp=rh*2.2,ymax=niceMax(Math.max.apply(null,b.concat(nat,g||[]))*1.15);
 function X(v){return L+pw*v/ymax;}
 var out='<svg class="dg rdg" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" data-decay="1" data-horiz="1" data-l="'+L+'" data-t="'+T+'" data-pw="'+pw+'" data-ph="'+ph+'" data-rh="'+rh.toFixed(2)+'" data-ymax="'+ymax+'" data-w="'+w+'" role="group" aria-label="'+graphLabel(s)+' One ridge per band: drag the foot of a ridge sideways, or focus it and use the arrow keys.">';
 var stp=ymax<=1.5?0.25:ymax<=3?0.5:ymax<=6?1:ymax<=12?2:4;
 for(var v=0;v<=ymax+1e-6;v+=stp){var xx=X(v).toFixed(1);out+='<line class="gr" x1="'+xx+'" x2="'+xx+'" y1="'+(T+rh-amp).toFixed(1)+'" y2="'+(T+ph+6)+'"/><text x="'+xx+'" y="'+(h-8)+'" text-anchor="middle">'+(+v.toFixed(2))+(v===0?'':' s')+'</text>';}
 var late='';
 for(var i=0;i<8;i++){var base=T+(i+1)*rh,top=base-amp,ed=Math.abs(b[i]-nat[i])>0.004,xb=X(b[i]).toFixed(1),bs=base.toFixed(1),tp=top.toFixed(1);
  out+='<path class="rg-f" style="fill:rgba(239,235,225,'+(0.12-i*0.013).toFixed(3)+')" d="M'+L+' '+tp+'L'+xb+' '+bs+'H'+(L+pw)+'V'+(T+ph)+'H'+L+'Z"/>';
  out+='<path class="rg-cap" d="M'+L+' '+tp+'L'+X(nat[i]).toFixed(1)+' '+bs+'"/>';
  if(g){var xg=X(g[i]).toFixed(1);out+='<path class="rg-g" d="M'+L+' '+tp+'L'+xg+' '+bs+'"/><circle class="gnd" cx="'+xg+'" cy="'+bs+'" r="2.5"/>';}
  out+='<path class="rg'+(ed?' ed':'')+'" d="M'+L+' '+tp+'L'+xb+' '+bs+'H'+(L+pw)+'"/>';
  late+='<text class="rl" x="'+(L-10)+'" y="'+(base+4).toFixed(1)+'" text-anchor="end">'+FREQ[i]+(i===7?' Hz':'')+'</text>';
  late+='<text class="rv" x="'+(+xb+11)+'" y="'+(base-7).toFixed(1)+'">'+b[i].toFixed(2)+(ed?'*':'')+'</text>';
  late+='<circle class="nd'+(ed?' ed':'')+(i===s.lastBand?' cur':'')+'" data-node="'+i+'" data-fid="nd-'+i+'" tabindex="0" role="slider" '+nodeAria(s,i,b[i])+' cx="'+xb+'" cy="'+bs+'" r="5.5"/>';}
 return out+late+sideCap(s,w-R,16,'end')+'</svg>';}
/* material palettes (Material): the room's surface sets the tokens */
var MAT={hall:{g:'#2A1F17',p:'#33271E',r:'#3F3128',ln:'#574538',tx:'#F3E9DB',tx2:'#B9A48F',ac:'#E0A458',onac:'#1C120A'},
 chapel:{g:'#D8D1C2',p:'#E5DFD1',r:'#CBC3B1',ln:'#B0A794',tx:'#262219',tx2:'#6B6355',ac:'#8C4A3C',onac:'#FFFFFF'},
 garage:{g:'#55585A',p:'#606366',r:'#6E7174',ln:'#85888B',tx:'#F2F2F0',tx2:'#D2D4D3',ac:'#FFD23F',onac:'#1A1A1A'},
 room:{g:'#1E2522',p:'#27302C',r:'#313B36',ln:'#44504A',tx:'#EDEFEA',tx2:'#A4ADA6',ac:'#F2845C',onac:'#1A0E08'},
 user:{g:'#24262A',p:'#2C2F34',r:'#363A40',ln:'#4A4F57',tx:'#ECEDEF',tx2:'#A7ABB2',ac:'#8FB8DE',onac:'#0B1622'}};
function matStyle(k){var m=MAT[k];return '--g:'+m.g+';--p:'+m.p+';--r:'+m.r+';--ln:'+m.ln+';--tx:'+m.tx+';--tx2:'+m.tx2+';--ac:'+m.ac+';--ac2:'+m.ac+';--onac:'+m.onac+';--num:'+m.tx+';--mtr:'+m.ac+';--focus:'+m.tx;}
var STYLE={material:function(s){return matStyle(spOf(s).kind);}};

/* shared pieces */
function wordmark(s,cls,inner,raw){return '<button class="wmb" data-act="skins" aria-expanded="'+s.skins+'" aria-label="Change the look, current '+esc(skinOf(s.skin).name)+'" title="Change the look"><span class="'+cls+'"'+(raw?' style="margin:0"':'')+'>'+inner+'</span></button>';}
function segMode(s){return '<div class="seg" role="group" aria-label="View"><button data-act="mode" data-v="studio" aria-pressed="'+(s.mode==='studio')+'">Studio</button><button data-act="mode" data-v="stage" aria-pressed="'+(s.mode==='stage')+'">Stage</button></div>';}
function segAB(s){return '<div class="seg" role="group" aria-label="Compare"><button data-act="slot" data-v="A" aria-pressed="'+(s.slot==='A')+'" aria-label="Snapshot A">A</button><button data-act="slot" data-v="B" aria-pressed="'+(s.slot==='B')+'" aria-label="Snapshot B">B</button></div>';}
function hintText(s){var t;if(s.note)t=s.note;else if(s.hint){var k=s.hint,ov=isOver(s,k),nv=natV(s,k),lb=k==='decay'&&!s.link?sideName(s)+' decay':P[k].l;t='<b>'+lb+' '+esc(fmt(s,k))+'</b>, '+(nv==null?'':ov?'captured '+esc(fmt(s,k,nv))+'. ':'as captured. ')+P[k].l+' '+P[k].help+(k==='decay'&&!s.link?' The other side shows faintly.':'');}
 else t='Drag any control or a point on the graph. Click a value to type it. Shift gives fine steps, and the arrow keys work everywhere.';
 if(s.freeze)t='<b>Frozen.</b> The tail holds until you release Freeze. '+t;return '<div class="hint" title="'+t.replace(/<[^>]*>/g,'').replace(/"/g,'&quot;')+'">'+t+'</div>'+licChip(s);}
function days(n){return n+' day'+(n===1?'':'s');}
/* the trial, in the strip beside the hint until Drenched is activated */
function licChip(s){var L=s.licence;if(!L||L.state==='licensed')return '';
 if(L.state==='beta')return '<span class="licchip">Beta'+(s.bits===32?' \u00b7 32-bit':'')+'</span>';
 return '<button class="licchip'+(L.state==='ended'?' end':'')+'" data-act="licence" aria-haspopup="dialog">'+(L.state==='ended'?'Trial ended, reverb muted. <u>Activate</u>':'Trial: '+days(L.daysLeft)+' left. <u>Activate</u>')+'</button>';}
function dbText(v){return v<=-60?'\u2212\u221e':v.toFixed(1).replace('-','\u2212');}
function meterPct(v){return clamp(1+v/60,0,1)*100;}
function meters(){var i=Math.max(st.inL,st.inR),o=Math.max(st.outL,st.outR);return '<div class="mm">In<span class="bar"><i data-m="in" style="width:'+meterPct(i).toFixed(0)+'%"></i></span><em data-mt="in">'+dbText(i)+'</em></div><div class="mm">Out<span class="bar"><i data-m="out" style="width:'+meterPct(o).toFixed(0)+'%"></i></span><em data-mt="out">'+dbText(o)+'</em></div>';}
/* ---- the space browser: search, kinds, Similar, and the tools in a strip of their own ---- */
/* The kinds the browser filters by. A recorded space goes under its own kind, the modelled
   ones and a loaded impulse response get a group each, so every space is in exactly one and
   the counts add up. */
var BRW_KINDS=[['all','All'],['hall','Halls'],['chapel','Churches'],['room','Rooms'],['modelled','Modelled'],['user','Your IR']];
function brwKind(sp){return sp.kind==='user'?'user':sp.recorded?(sp.kind==='garage'?'room':sp.kind):'modelled';}
/* words a search finds a space by, beyond what its row says */
var BRW_WORDS={hall:'hall',chapel:'church chapel cathedral',garage:'room garage industrial',room:'room',user:'your own impulse response file'};
function brwText(sp){return (sp.name+' '+sp.place+' '+sp.mat+' '+sp.rec+' '+sp.date+' '+sp.no+' '+(BRW_WORDS[sp.kind]||'')+' '+(sp.recorded?'recorded':sp.kind==='user'?'':'modelled')).toLowerCase();}
function brwMatch(sp,q){var t=brwText(sp),w=q.toLowerCase().split(/\s+/);for(var i=0;i<w.length;i++)if(w[i]&&t.indexOf(w[i])<0)return false;return true;}
/* How far apart two spaces sound, for the Similar ordering. Each octave band's decay is
   compared on a log scale, so 1 s against 2 s counts as much as 4 s against 8 s, and the mid
   bands from 250 Hz to 2 kHz count double because they carry most of a reverb's character; a
   band at the 20 s ceiling is one the measurement could not resolve, so it is left out. Then
   the volume, also on a log scale but at a low weight (the size shows in the echo density and
   the sense of distance, far less than the decay does), and the spectral tilt, 4 and 8 kHz
   against 500 Hz and 1 kHz, so a dark room and a bright one of the same length stay apart. */
var SIM_W=[1,1,2,2,2,2,1,1],SIM_VOL=0.05,SIM_TILT=0.35;
function tiltOf(b){return Math.log((b[6]+b[7])/(b[3]+b[4]));}
function cubic(sp){return sp.W*sp.D*sp.H;}
function spaceDistance(a,b){var d=0,w=0;for(var i=0;i<8;i++){if(a.bands[i]>=20||b.bands[i]>=20)continue;var x=Math.log(a.bands[i]/b.bands[i]);d+=SIM_W[i]*x*x;w+=SIM_W[i];}
 var v=Math.log(cubic(a)/cubic(b)),t=tiltOf(a.bands)-tiltOf(b.bands);return Math.sqrt((w?d/w:0)+SIM_VOL*v*v+SIM_TILT*t*t);}
/* a few plain words on how b differs from a: length first, then tone, then size */
function similarWhy(a,b){var out=[],r=Math.log(decayOf(b.bands)/decayOf(a.bands)),t=tiltOf(b.bands)-tiltOf(a.bands),v=Math.log(cubic(b)/cubic(a)),ar=Math.abs(r),at=Math.abs(t),av=Math.abs(v);
 if(ar>=0.08)out.push((ar<0.3?'a little ':ar<0.8?'':'much ')+(r>0?'longer':'shorter'));
 if(at>=0.12)out.push((at<0.4?'a little ':'')+(t<0?'darker':'brighter'));
 if(av>=0.5)out.push((av<1.5?'':'much ')+(v>0?'bigger':'smaller'));
 var s=out.length?out.join(', '):'much the same';return s.charAt(0).toUpperCase()+s.slice(1);}
/* the rows the browser shows: the search and the kind applied, in library order, or by
   distance from the reference space when Similar is on, the reference itself left out */
function browseResults(s){var q=(s.brwQ||'').trim(),kind=s.brwKind||'all',ref=s.brwSim!=null&&SPACES[s.brwSim]?SPACES[s.brwSim]:null,rows=[],counts={};
 SPACES.forEach(function(sp,i){if(ref&&i===s.brwSim)return;if(q&&!brwMatch(sp,q))return;rows.push({i:i,sp:sp,k:brwKind(sp),d:ref?spaceDistance(ref,sp):0});});
 if(ref)rows.sort(function(a,b){return a.d-b.d;});
 rows.forEach(function(r){counts[r.k]=(counts[r.k]||0)+1;});counts.all=rows.length;
 return {rows:kind==='all'?rows:rows.filter(function(r){return r.k===kind;}),counts:counts,ref:ref};}
/* decay by band at a glance: eight bars on a log scale from 0.25 s to 16 s, so a dark room
   slopes down to the right and a bright one stays level */
function sparkSVG(b){var out='<svg class="brw-spk" width="47" height="22" viewBox="0 0 47 22" aria-hidden="true">';for(var i=0;i<8;i++){var h=Math.max(1.5,clamp(Math.log(b[i]/0.25)/Math.log(64),0,1)*22);out+='<rect x="'+(i*6)+'" y="'+(22-h).toFixed(1)+'" width="5" height="'+h.toFixed(1)+'"/>';}return out+'</svg>';}
function browserHTML(s){var R=browseResults(s),n=R.rows.length,cur=clamp(s.brwCur||0,0,Math.max(0,n-1)),q=s.brwQ||'',user=SPACES.length>USER;
 var chips=BRW_KINDS.map(function(k){var c=R.counts[k[0]]||0;if(k[0]==='user'&&!user)return '';return '<button data-act="brwkind" data-v="'+k[0]+'" aria-pressed="'+((s.brwKind||'all')===k[0])+'">'+k[1]+'<em>'+c+'</em></button>';}).join('');
 var rows=R.rows.map(function(r,j){var x=r.sp,on=r.i===s.sp,key=j===cur;
  return '<div class="brw-row'+(on?' on':'')+(key?' key':'')+'"><button class="brw-pick" role="option" id="brw-r'+j+'" aria-selected="'+key+'" aria-current="'+on+'" data-act="pick" data-v="'+r.i+'"><span class="brw-ph"><i class="ph-'+x.img+'"></i></span>'+
   '<span class="brw-nm"><b>'+esc(x.name)+'</b><small>'+esc(x.place)+'</small></span>'+(R.ref?'<span class="brw-why">'+similarWhy(R.ref,x)+'</span>':'')+
   '<span class="brw-rt">'+f1(decayOf(x.bands))+' s</span><span class="brw-vol">'+vol(x)+'</span>'+sparkSVG(x.bands)+'</button>'+
   '<button class="brw-sim" data-act="brwsim" data-v="'+r.i+'" tabindex="-1" aria-label="Spaces that sound like '+esc(x.name)+'" title="Spaces that sound like this one">\u2248</button></div>';}).join('');
 if(!n)rows='<div class="brw-empty">'+(q?'Nothing matches \u201c'+esc(q)+'\u201d. Try a place, a kind such as church, or a microphone such as ORTF.':'Nothing of this kind'+(R.ref?' sounds like '+esc(R.ref.name)+'.':' is here.'))+'<br><button data-act="brwclear">Show every space</button></div>';
 var simb=R.ref?'<button class="brw-simb" data-act="brwsim" data-v="off" aria-pressed="true" title="Back to the whole list">Sounding like '+esc(R.ref.name)+'</button>'
  :'<button class="brw-simb" data-act="brwsim" data-v="'+s.sp+'" aria-pressed="false" title="Order the list by how close each space sounds to '+esc(spOf(s).name)+'">Similar to this space</button>';
 var tool=function(act,cls,l,sm){return '<button data-act="'+act+'" class="'+cls+'"><b>'+l+'</b><small>'+sm+'</small></button>';};
 var tools=tool('loadir','mload','Load your own IR\u2026','WAV, AIFF, FLAC, SOFA')+tool('capture','mcap','Capture a space\u2026','Sweep or clap, and record')+tool('speakers','mspk','Reverb speakers\u2026','Level and delay each')+
  tool('eq','meq','Wet EQ\u2026',eqFlat(s)?'Six bands, flat':eqUsed(s)+(eqUsed(s)===1?' band':' bands')+' in use')+tool('wf','mwf','Waterfall\u2026','Decay and live spectrum')+tool('scenes','mscn','Scenes\u2026',s.scenes&&s.scenes.length?s.scenes.length+' stored':'For each shot')+
  (LIBRARY.length?tool('credits','mcred','Recording credits\u2026',LIBRARY.length+' recorded'):'')+(s.licence&&s.licence.state==='beta'?'':tool('licence','mlic','Licence\u2026',s.licence&&s.licence.state==='licensed'?'Activated':s.licence&&s.licence.state==='ended'?'Trial ended':'Trial'));
 return '<div class="cap brw'+(R.ref?' sim':'')+'" role="dialog" aria-modal="true" aria-labelledby="brw-h"><div class="cbox"><div class="chd"><h2 id="brw-h">Spaces</h2><span class="brw-n">'+SPACES.length+' to choose from. Type to search, the arrow keys move, Enter picks, and Shift with Enter lists the ones that sound like the highlighted space.</span><button class="cx" data-act="browse" aria-label="Close">Close</button></div>'+
  '<div class="brw-top"><input id="brw-q" data-fid="brw-q" type="text" value="'+esc(q)+'" placeholder="Search by name, place, kind or microphone" autocomplete="off" spellcheck="false" role="combobox" aria-label="Search the spaces" aria-autocomplete="list" aria-expanded="true" aria-controls="brw-list"'+(n?' aria-activedescendant="brw-r'+cur+'"':'')+'>'+simb+'</div>'+
  '<div class="brw-kinds" role="group" aria-label="Kind">'+chips+'</div>'+
  '<div class="brw-hd" aria-hidden="true"><span>'+(R.ref?'Closest first':'Space')+'</span>'+(R.ref?'<span>Next to '+esc(R.ref.name)+'</span>':'')+'<span>RT60</span><span>Volume</span><span>By band</span></div>'+
  '<div class="brw-list" id="brw-list" role="listbox" aria-label="Spaces">'+rows+'</div>'+
  '<div class="brw-tools">'+tools+'</div></div></div>';}
function advRows(s){
 var deepRow=function(k){if(TOGGLES.indexOf(k)>=0)return '<div class="row"><span>'+P[k].l+'</span><button class="tg" data-act="toggle" data-k="'+k+'" aria-pressed="'+(s[k]>=0.5)+'">'+fmt(s,k)+'</button></div>';return '<div class="row"><span>'+P[k].l+'</span>'+roEl(s,k)+'</div>';};
 return '<div class="ph"><span class="lbl">All parameters</span><span class="ph-r"><button data-act="skins" aria-expanded="'+s.skins+'">Look: '+esc(skinOf(s.skin).name)+'</button><button data-act="adv" aria-label="Close all parameters">Close</button></span></div>'+MAIN.map(function(k){return '<div class="row"><span>'+P[k].l+'</span>'+roEl(s,k)+'</div>';}).join('')+DEEP.map(deepRow).join('')+'<div class="row"><span>Early pattern</span><em>As captured</em></div>';}
function prevNext(s){return '<button class="ib" data-act="prev" aria-label="Previous space">'+ICON.prev+'</button><button class="ib" data-act="next" aria-label="Next space">'+ICON.next+'</button>';}

/* ---- stage (shared composition, styled by each system) ---- */
var CH=[['L',-14.2],['C',-17.8],['R',-14.6],['Lss',-19.9],['Rss',-20.7],['Lrs',-23.4],['Rrs',-24.1],['LFE',-31.0],['Ltf',-26.5],['Rtf',-27.2],['Ltr',-28.8],['Rtr',-29.6]];
function stageHTML(id,s){var sp=spOf(s),e=eqp(s.el);
 function tile(lbl,k,inner){return '<div class="tile"><div class="ph"><span class="lbl">'+lbl+(k&&k!=='el'?ovrEl(s,k):'')+'</span>'+(k?lockBtn(s,k,lbl):'')+'</div>'+inner+'</div>';}
 var th;if(id==='section')th=planView(s,298,220);else if(id==='signal')th=pixelRoom(s,298,220);else th=photoEl(s,298,220,id==='catalogue'?'filter:grayscale(1)':'');
 var wm={anodised:'<span class="an-wm">DRENCHED</span>',section:'<span class="se-title">Drenched</span>',catalogue:'<span class="ca-wm">Drenched</span>',field:'<span style="font:500 14px var(--fnum)">drenched</span>',vitrine:'<span class="vi-wm" style="margin:0">Drenched</span>',signal:'<span class="si-wm"><i></i>DRENCHED</span>',aperture:'<span class="ap-wm" style="margin:0">Drenched</span>',contact:'<span class="co-wm" style="margin:0">DRENCHED</span>',strata:'<span class="st-wm" style="margin:0">Drenched</span>',rotunda:'<span class="rt-wm" style="margin:0">Drenched</span>',material:'<span class="ma-wm" style="margin:0">DRENCHED</span>',folio:'<span class="fo-wm" style="margin:0;color:var(--tx);text-shadow:none">Drenched</span>'}[id];
 return '<div class="stg-top">'+wordmark(s,'wm',wm,true)+prevNext(s)+'<span style="font-size:13px">'+esc(sp.name)+'</span><span style="flex:1"></span>'+segMode(s)+segAB(s)+'</div>'+
 '<div class="stg"><div class="stg-nums">'+tile('RT60','decay',roEl(s,'decay'))+tile('Pre-delay','pre',roEl(s,'pre'))+tile('Early','el','<span class="big">'+e.e+'<small> dB</small></span>')+tile('Tail',null,'<span class="big">'+e.t+'<small> dB</small></span>')+tile('Mix','mix',roEl(s,'mix'))+'</div>'+
 '<div class="stg-met"><div class="ph"><span class="lbl">Output, 7.1.4</span><span class="lbl">dBFS, peak hold, slow ballistics</span></div><div class="mtrs">'+CH.map(function(c,ci){var lv=ci===0?s.outL:ci===2?s.outR:-60,p=meterPct(lv),ch=ci===0?'outL':ci===2?'outR':'';return '<div class="mt"><em data-mt="'+ch+'">'+dbText(lv)+'</em><div class="tr"><s></s><i data-m="'+ch+'" style="height:'+p.toFixed(0)+'%"></i><b style="bottom:'+Math.min(98,p+4).toFixed(0)+'%"></b></div><span>'+c[0]+'</span></div>';}).join('')+'</div></div>'+
 '<div class="stg-bot"><div class="stg-dec"><span class="lbl">Decay by frequency'+linkCtl(s,'end')+'</span>'+decaySVG(id,s,536,250,{bars:id==='signal',dims:id==='section'})+'</div><div class="stg-ct">'+MAIN.map(function(k){return '<div class="row"><span>'+P[k].l+ovrEl(s,k)+'</span>'+roEl(s,k)+'</div>';}).join('')+'<div class="row"><span>Freeze</span><button class="lk" data-act="freeze" aria-pressed="'+s.freeze+'" style="width:auto;padding:0 8px;font-size:11px">'+(s.freeze?'On':'Off')+'</button></div><div class="row"><span>Wet EQ'+eqMark(s)+'</span><button class="lk" data-act="eq" aria-haspopup="dialog" aria-expanded="'+!!s.eq+'" style="width:auto;padding:0 8px;font-size:11px">'+(eqFlat(s)?'Flat':'In use')+'</button></div><div class="row"><span>Waterfall</span><button class="lk" data-act="wf" aria-haspopup="dialog" aria-expanded="'+!!s.wf+'" style="width:auto;padding:0 8px;font-size:11px">Open</button></div></div><div class="stg-th">'+th+'<div class="cap"><b>'+esc(sp.name)+'</b><span>'+vol(sp)+', listener '+esc(fmt(s,'dist'))+'</span></div></div></div></div>'+
 '<div class="stg-strip">'+hintText(s)+meters()+'</div>';}

/* ---- the six studio compositions ---- */
var COMPOSE={
 anodised:function(s){var sp=spOf(s),e=eqp(s.el);
  var ctl=function(k){return '<div class="an-ctl">'+roEl(s,k)+knobEl(s,k,72,'anodised')+'<span class="an-lab">'+P[k].l+ovrEl(s,k)+'</span></div>';};
  var top='<div class="an-top">'+wordmark(s,'an-wm','DRENCHED')+prevNext(s)+'<button class="an-sel" data-act="browse" aria-expanded="'+s.browse+'" aria-label="Choose a space, current '+esc(sp.name)+'"><span class="st"><b>'+esc(sp.name)+'</b><small>'+esc(sp.place)+', '+vol(sp)+'</small></span>'+ICON.down+'</button><span style="flex:1"></span>'+segMode(s)+segAB(s)+'</div>';
  var win='<div class="an-win">'+photoEl(s,0,0,'position:absolute;inset:0')+'<div class="scrim"></div><div class="an-name"><b>'+esc(sp.name)+'</b><span>'+esc(sp.mat)+', '+vol(sp)+', captured '+esc(sp.date)+'</span></div>'+
   '<div class="an-ro"><div>'+roEl(s,'decay')+'<span>RT60'+(isOver(s,'decay')?', changed':'')+'</span></div><div>'+roEl(s,'pre')+'<span>Pre-delay</span></div><div><span class="ro" style="font-size:24px;height:30px;width:112px;white-space:nowrap;text-align:right;font-weight:300;display:inline-block;line-height:30px">'+e.e+' dB</span><span>Early</span></div><div><span class="ro" style="font-size:24px;height:30px;width:112px;white-space:nowrap;text-align:right;font-weight:300;display:inline-block;line-height:30px">'+e.t+' dB</span><span>Tail</span></div></div>'+
   '<div class="an-graph">'+decaySVG('anodised',s,600,250,{m:[40,20,14,26]})+'</div><div class="an-link">'+linkCtl(s)+'</div>'+
   '<div class="an-bands">'+bandInputs(s,sp,false)+'</div><div class="glass"></div></div>';
  var deck=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="an-deck"><div class="an-mix">'+roEl(s,'mix')+knobEl(s,'mix',128,'anodised')+'<span class="an-lab">Mix '+lockBtn(s,'mix','Mix')+'</span></div>'+MAIN.map(ctl).join('')+
   '<div class="an-acts"><button class="an-btn" data-act="freeze" aria-pressed="'+s.freeze+'"><i></i>Freeze</button><button class="an-btn" data-act="match"><i></i>Match a room</button><button class="an-btn" data-act="adv" aria-expanded="'+s.adv+'"><i></i>All parameters</button>'+eqBtn(s,'an-btn','<i></i>Wet EQ')+wfBtn(s,'an-btn','<i></i>Waterfall')+'</div></div>';
  return top+win+deck+'<div class="an-strip">'+hintText(s)+meters()+'</div>';},

 section:function(s){var sp=spOf(s),e=eqp(s.el);
  var head='<div class="se-head">'+wordmark(s,'se-title','Drenched')+'<span class="se-sub" style="color:var(--tx);font-size:12px">'+esc(sp.name.toUpperCase())+'</span><span class="se-sub">PLAN AND SECTION, SHEET '+esc(sp.no.replace(/\D/g,''))+'</span>'+prevNext(s)+'<button class="ca-link" data-act="browse" aria-expanded="'+s.browse+'" style="font:11px var(--fnum);letter-spacing:.08em;color:var(--tx2)">CHANGE SPACE</button><span style="flex:1"></span><div class="se-box"><span class="cap">VIEW</span><button data-act="mode" data-v="studio" aria-pressed="'+(s.mode==='studio')+'">STUDIO</button><button data-act="mode" data-v="stage" aria-pressed="'+(s.mode==='stage')+'">STAGE</button></div><div class="se-box"><span class="cap">REV</span><button data-act="slot" data-v="A" aria-pressed="'+(s.slot==='A')+'">A</button><button data-act="slot" data-v="B" aria-pressed="'+(s.slot==='B')+'">B</button></div></div>';
  var plan='<div class="se-plan"><div class="legend"><div class="se-box" style="height:24px;margin-right:6px"><button data-act="view" data-v="plan" aria-pressed="'+(s.view==='plan')+'">PLAN</button><button data-act="view" data-v="section" aria-pressed="'+(s.view==='section')+'">SECTION</button></div><span><i></i>S1 source</span><span><i class="r"></i>R1 listener. Drag either to move it</span></div>'+planView(s,664,620)+'<div class="se-photo">'+photoEl(s,212,132)+'<span>SITE PHOTOGRAPH, '+esc(sp.date.toUpperCase())+'</span></div></div>';
  var row=function(k){return '<div class="se-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+lockBtn(s,k,P[k].l)+'</div>';};
  var right=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="se-right"><div><div class="se-h"><span>SECTION A-A, DECAY TIME BY FREQUENCY</span><span>SOLID AS SET, DASHED AS CAPTURED</span></div><div class="se-lk">'+(s.link?'':'<span>FAINT, THE OTHER SIDE</span>')+linkCtl(s)+'</div>'+decaySVG('section',s,454,214,{dims:true,m:[40,16,16,24]})+'</div>'+
   '<div class="se-sched"><div class="se-row hd"><span>Parameter</span><span>Value</span><span style="text-align:right">Typed</span><span></span></div>'+row('mix')+MAIN.map(row).join('')+'</div>'+
   '<div class="se-acts"><button data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button data-act="match">Match a room</button><button data-act="adv" aria-expanded="'+s.adv+'">All parameters</button>'+eqBtn(s,'')+wfBtn(s,'')+'</div>'+
   '<div class="se-tb"><div><small>Drawing</small><b>'+esc(sp.name)+'</b></div><div><small>RT60 mid</small><b>'+esc(fmt(s,'decay'))+'</b></div><div><small>Early / tail</small><b>'+e.e+' / '+e.t+' dB</b></div><div><small>Captured</small><b>'+esc(sp.date)+'</b></div></div></div>';
  return '<div class="se-grid"></div><div class="se-frame"></div><div class="se-ticks"></div>'+head+plan+right+'<div class="se-strip">'+hintText(s)+meters()+'</div>';},

 catalogue:function(s){var sp=spOf(s),e=eqp(s.el),parts=sp.name.split(' '),last=parts.pop();
  var row=function(k){return '<div class="ca-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+'</div>';};
  var wall=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="ca-wall"><div class="ca-top">'+wordmark(s,'ca-wm','Drenched')+'<button class="ca-link" data-act="mode" data-v="studio" aria-pressed="'+(s.mode==='studio')+'">Day</button><button class="ca-link" data-act="mode" data-v="stage" aria-pressed="'+(s.mode==='stage')+'">Night</button><span style="width:8px"></span><button class="ca-link" data-act="slot" data-v="A" aria-pressed="'+(s.slot==='A')+'">A</button><button class="ca-link" data-act="slot" data-v="B" aria-pressed="'+(s.slot==='B')+'">B</button></div>'+
   '<div class="ca-no">'+esc(sp.no)+'</div><h3 class="ca-name">'+esc(parts.join(' '))+(parts.length?' ':'')+'<em>'+esc(last)+'</em></h3>'+
   '<div class="ca-nav">'+prevNext(s)+'<button class="ca-link" data-act="browse" aria-expanded="'+s.browse+'">Browse the collection</button></div>'+
   '<dl class="ca-dl"><dt>Location</dt><dd>'+esc(sp.place)+'</dd><dt>Volume</dt><dd>'+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</dd><dt>Captured</dt><dd>'+esc(sp.rec)+', '+esc(sp.date)+'</dd><dt>Listener</dt><dd>'+esc(fmt(s,'dist'))+' from the source, early '+e.e+' dB, tail '+e.t+' dB</dd></dl>'+
   '<div class="ca-fig"><div class="fh"><b>Fig. 1</b><span>Decay time by frequency</span>'+linkCtl(s,'end')+'</div>'+decaySVG('catalogue',s,488,176,{m:[36,14,12,24]})+'<p class="fc">Solid line as set, dashed as captured'+(s.link?'. Drag a point to change one band.':', faint the other side. Drag a point to change one band.')+'</p></div>'+
   '<div class="ca-list">'+MAIN.map(row).join('')+'</div>'+
   '<div class="ca-mix">'+knobEl(s,'mix',56,'catalogue')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div><div class="ca-acts"><button class="ca-link" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="ca-link" data-act="match">Match a room</button><button class="ca-link" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button>'+eqBtn(s,'ca-link')+wfBtn(s,'ca-link')+'</div></div></div>';
  return '<div class="ca-photo">'+photoEl(s,620,760)+(s.note?'<div class="ca-note" role="status">'+s.note+'</div>':'')+'<div class="ca-lic">'+licChip(s)+'</div></div>'+wall;},

 field:function(s){var sp=spOf(s),e=eqp(s.el);
  var fr=function(k,cls){return '<div class="fr '+(cls||'')+'">'+knobEl(s,k,cls?44:34,'field')+'<div class="t"><span>'+P[k].l+ovrEl(s,k)+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</span>'+roEl(s,k)+'</div></div>';};
  var rail=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="fi-rail">'+fr('mix','mix')+MAIN.map(function(k){return fr(k);}).join('')+'<div class="segs">'+segMode(s)+segAB(s)+'</div><div class="acts"><button class="fi-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="fi-btn" data-act="match">'+ICON.match+'Match a room</button><button class="fi-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button>'+eqBtn(s,'fi-btn',null,true)+wfBtn(s,'fi-btn',null,true)+'</div></div>';
  return '<div class="fi-heat">'+decaySVG('field',s,1200,760,{heat:true,cols:88,rows:44,m:[72,280,150,96],big:true,cb:true,xy:712})+'</div>'+
   '<div class="fi-big">'+roEl(s,'decay').replace('class="ro "','class="ro "')+'<div class="what"><b>RT60, mid band'+(s.link?'':', '+sideName(s).toLowerCase())+(isOver(s,'decay')?', changed':'')+'</b><span>Energy decay, frequency by time, \u221260 dB floor</span></div></div>'+
   '<div class="fi-sp">'+prevNext(s).replace('</button><button','</button><button class="ca-link" data-act="browse" aria-expanded="'+s.browse+'" style="font:13px var(--fui);color:var(--tx);text-decoration:none"><span class="nm">'+esc(sp.name)+', '+vol(sp)+', pre-delay '+esc(fmt(s,'pre'))+'</span></button><button')+linkCtl(s)+'</div>'+
   '<div class="fi-th">'+photoEl(s,148,92)+'<span>'+esc(sp.place)+'</span></div>'+rail+
   '<div class="fi-bands">'+[0,1,2,3,4,5,6,7].map(function(i){return bandInput(s,sp,i);}).join('')+'</div>'+
   '<div class="fi-strip">'+hintText(s)+meters()+'</div>';},

 vitrine:function(s){var sp=spOf(s),nsp=SPACES.length,pv=SPACES[(s.sp+nsp-1)%nsp],nx=SPACES[(s.sp+1)%nsp];
  var ctl=function(k){return '<div class="vi-ctl">'+knobEl(s,k,64,'vitrine')+roEl(s,k)+'<span class="nm">'+P[k].l+ovrEl(s,k)+'</span></div>';};
  var rail=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="vi-rail"><div class="vi-mix">'+knobEl(s,'mix',96,'vitrine')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div></div><div class="vi-grid">'+MAIN.map(ctl).join('')+'</div><div class="vi-acts"><button class="vi-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="vi-btn" data-act="match">'+ICON.match+'Match</button><button class="vi-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'More</button>'+eqBtn(s,'vi-btn','EQ',true)+wfBtn(s,'vi-btn','Waterfall',true)+'</div></div>';
  return '<div class="vi-photo">'+photoEl(s,0,0,'position:absolute;inset:0')+'</div><div class="vi-grade"></div>'+
   '<div class="vi-top">'+wordmark(s,'vi-wm','Drenched')+segMode(s)+segAB(s)+'</div>'+
   '<div class="vi-name"><h3>'+esc(sp.name)+'</h3><span>'+esc(sp.place)+', '+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</span></div>'+
   '<div class="vi-nav"><button data-act="prev">'+ICON.prev+esc(pv.name)+'</button><button data-act="browse" aria-expanded="'+s.browse+'">All spaces'+ICON.down+'</button><button data-act="next">'+esc(nx.name)+ICON.next+'</button></div>'+
   '<div class="vi-lens"><div class="lh"><span class="lt">Decay by frequency'+linkCtl(s)+'</span><span class="vv"><span><b>RT60</b>'+roEl(s,'decay')+'</span><span><b>Pre-delay</b>'+roEl(s,'pre')+'</span><span><b>Distance</b>'+roEl(s,'dist')+'</span></span></div>'+decaySVG('vitrine',s,748,250,{mist:true,m:[40,20,14,26],big:true})+'</div>'+rail+
   '<div class="vi-strip">'+hintText(s)+'</div>';},

 signal:function(s){var sp=spOf(s),ss=s.size/100,e=eqp(s.el);
  var blk=function(k,cls){var ed=isOver(s,k);return '<div class="blk'+(ed?' ed':'')+(k==='mix'?' wet mix':'')+' '+(cls||'')+'" data-knob="'+k+'" data-fid="kn-'+k+'" '+ariaFor(s,k)+'><div class="blk-l"><span>'+P[k].l+(ed?' *':'')+'</span>'+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</div>'+roEl(s,k)+'<div class="blk-bar"><i style="width:'+(to01(k,getV(s,k))*100).toFixed(1)+'%"></i></div></div>';};
  var vb=function(l,v,u){return '<div class="blk ro-only"><div class="blk-l"><span>'+l+'</span></div><div class="vbig">'+v+'<small>'+u+'</small></div><div class="blk-bar" style="background:transparent"></div></div>';};
  var rail=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="si-rail">'+blk('mix','mix')+blk('decay')+blk('pre')+'<div class="si-acts"><button class="si-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="si-btn" data-act="match">'+ICON.match+'Match a room</button><button class="si-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button>'+eqBtn(s,'si-btn',null,true)+wfBtn(s,'si-btn',null,true)+'</div></div>';
  return '<div class="si-grid"></div><div class="si-top">'+wordmark(s,'si-wm','<i></i>DRENCHED')+segMode(s)+segAB(s)+'</div>'+
   '<div class="si-space"><div class="nm"><button data-act="browse" aria-expanded="'+s.browse+'" aria-label="Choose a space, current '+esc(sp.name)+'">'+esc(sp.name)+'</button></div><div class="meta">'+prevNext(s)+'<span>'+esc(sp.place)+'</span><span>'+vol(sp)+'</span></div></div>'+
   '<div class="si-room"><div class="si-mh"><span>Room</span><span>'+f1(sp.W*ss)+' \u00d7 '+f1(sp.D*ss)+' \u00d7 '+f1(sp.H*ss)+' m</span></div>'+photoEl(s,398,140)+pixelRoom(s,398,260)+'</div>'+
   '<div class="si-bars"><div class="si-mh"><span>Decay by band</span><span class="mr">'+linkCtl(s)+'<span>'+(s.link?'Seconds, * edited':'* edited')+'</span></span></div>'+decaySVG('signal',s,402,494,{bars:true})+'</div>'+rail+
   '<div class="si-vals">'+blk('size')+blk('bright')+blk('el')+blk('dist')+vb('Early',e.e,'dB')+vb('Tail',e.t,'dB')+vb('Listener',f1(distM(s)),'m')+'</div>'+
   '<div class="si-strip">'+hintText(s)+meters()+'</div>';},
 aperture:function(s){var sp=spOf(s);
  var bar=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="ap-nums">'+bandInputs(s,sp)+'</div><div class="ap-bar">'+numCtl(s,'mix','mix')+MAIN.map(function(k){return numCtl(s,k);}).join('')+'<div class="ap-acts"><button class="ap-btn" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="ap-btn" data-act="match">Match a room</button><button class="ap-btn" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button>'+eqBtn(s,'ap-btn')+wfBtn(s,'ap-btn')+'</div></div>';
  return '<div class="ap-photo">'+photoEl(s,0,0,'position:absolute;inset:0')+'</div><div class="ap-grade"></div>'+
   '<div class="ap-top">'+wordmark(s,'ap-wm','Drenched')+segMode(s)+segAB(s)+'</div>'+
   '<div class="ap-name"><h3>'+esc(sp.name)+'</h3><span>'+prevNext(s)+'<button class="ap-link" data-act="browse" aria-expanded="'+s.browse+'">'+esc(sp.place)+', '+vol(sp)+'</button></span></div>'+
   '<div class="ap-lk">'+linkCtl(s)+'</div><div class="ap-graph">'+decaySVG('aperture',s,1120,236,{m:[0,0,16,4],noY:true,big:true})+'</div>'+bar+
   '<div class="ap-strip">'+hintText(s)+meters()+'</div>';},

 contact:function(s){var sp=spOf(s),e=eqp(s.el);
  var row=function(k){return '<div class="co-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+lockBtn(s,k,P[k].l)+'</div>';};
  var right=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="co-right"><div class="co-h"><span>Decay by frequency, seconds</span><span class="mr">'+linkCtl(s)+'<span>'+(s.link?'Ticks mark the capture':'Outlines, other side')+'</span></span></div><div class="co-fad">'+decaySVG('contact',s,506,340,{bars:true,bw:18,track:true,m:[14,14,30,26]})+'</div>'+
   '<div class="co-list">'+MAIN.map(row).join('')+'</div>'+
   '<div class="co-mix">'+knobEl(s,'mix',48,'contact')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix '+lockBtn(s,'mix','Mix')+'</span></div><div class="co-acts"><button class="co-btn" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="co-btn" data-act="match">Match a room</button><button class="co-btn" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button>'+eqBtn(s,'co-btn')+wfBtn(s,'co-btn')+'</div></div></div>';
  return '<div class="co-top">'+wordmark(s,'co-wm','DRENCHED')+segMode(s)+segAB(s)+'</div>'+
   '<div class="co-print">'+photoEl(s,506,430)+'<div class="co-cap"><h3>'+esc(sp.name)+'</h3><div class="co-meta"><span>Location</span><b>'+esc(sp.place)+'</b><span>Volume</span><b>'+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</b><span>Capture</span><b>'+esc(sp.rec)+', '+esc(sp.date)+'</b><span>Listener</span><b>'+esc(fmt(s,'dist'))+', early '+e.e+' dB, tail '+e.t+' dB</b></div></div><div class="co-nav">'+prevNext(s)+'<span style="font:11px var(--fnum);color:var(--tx2);letter-spacing:.08em">'+esc(sp.no.toUpperCase())+'</span><button class="co-link" data-act="browse" aria-expanded="'+s.browse+'">Browse the prints</button></div></div>'+right+
   '<div class="co-strip">'+hintText(s)+meters()+'</div>';},

 strata:function(s){var sp=spOf(s),parts=sp.name.split(' '),last=parts.pop();
  var c=function(k){return '<div class="st-c'+(k==='mix'?' mix':'')+'">'+roEl(s,k)+slideEl(s,k)+'<span class="nm">'+P[k].l+ovrEl(s,k)+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</span></div>';};
  var ctl=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="st-ctl">'+c('mix')+MAIN.map(c).join('')+'</div><div class="st-acts"><button class="st-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="st-btn" data-act="match">'+ICON.match+'Match a room</button><button class="st-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button>'+eqBtn(s,'st-btn',null,true)+wfBtn(s,'st-btn',null,true)+'</div>';
  return '<div class="st-photo">'+photoEl(s,440,760)+'<div class="st-name"><h3>'+esc(parts.join(' '))+(parts.length?' ':'')+'<em>'+esc(last)+'</em></h3><span>'+esc(sp.place)+', '+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</span><div class="nav">'+prevNext(s)+'<button class="st-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div></div></div>'+
   '<div class="st-top">'+wordmark(s,'st-wm','Drenched')+linkCtl(s)+segMode(s)+segAB(s)+'</div>'+
   '<div class="st-ridges">'+ridgesSVG(s,690,420)+'</div>'+ctl+
   '<div class="st-strip">'+hintText(s)+meters()+'</div>';},

 rotunda:function(s){var sp=spOf(s),e=eqp(s.el);
  var c=function(k){return '<div class="rt-c">'+knobEl(s,k,40,'rotunda')+'<div class="t"><span>'+P[k].l+ovrEl(s,k)+'</span>'+roEl(s,k)+'</div></div>';};
  var side=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="rt-side"><h3>'+esc(sp.name)+'</h3><div class="sub"><span>'+esc(sp.place)+', '+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</span><span>Listener '+esc(fmt(s,'dist'))+' from the source, early '+e.e+' dB, tail '+e.t+' dB</span></div><div class="rt-nav">'+prevNext(s)+'<button class="rt-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div>'+
   '<div class="rt-mix">'+knobEl(s,'mix',64,'rotunda')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div></div><div class="rt-grid">'+MAIN.map(c).join('')+'</div>'+
   '<div class="rt-acts"><button class="rt-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="rt-btn" data-act="match">'+ICON.match+'Match</button><button class="rt-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'More</button>'+eqBtn(s,'rt-btn','EQ',true)+wfBtn(s,'rt-btn','Waterfall',true)+'</div></div>';
  return '<div class="rt-top">'+wordmark(s,'rt-wm','Drenched')+segMode(s)+segAB(s)+'</div>'+
   '<div class="rt-round"><div class="rt-ph">'+photoEl(s,340,340)+'</div>'+polarSVG('rotunda',s,620,{r0:188,r1:276})+'<div class="rt-lk">'+linkCtl(s)+'</div></div>'+side+
   '<div class="rt-strip">'+hintText(s)+meters()+'</div>';},

 material:function(s){var sp=spOf(s),ss=s.size/100;
  var c=function(k){return '<div class="ma-c'+(k==='mix'?' mix':'')+'">'+knobEl(s,k,56,'material')+roEl(s,k)+'<span class="nm">'+P[k].l+ovrEl(s,k)+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</span></div>';};
  var ctl=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="ma-ctl">'+c('mix')+MAIN.map(c).join('')+'<div class="ma-acts"><button class="ma-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="ma-btn" data-act="match">'+ICON.match+'Match a room</button><button class="ma-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button>'+eqBtn(s,'ma-btn',null,true)+wfBtn(s,'ma-btn',null,true)+'</div></div>';
  return '<div class="ma-top">'+wordmark(s,'ma-wm','DRENCHED')+segMode(s)+segAB(s)+'</div>'+
   '<div class="ma-head"><h3>'+esc(sp.name)+'</h3><span class="sub"><i></i>'+esc(sp.mat)+', '+vol(sp)+', '+f1(sp.W*ss)+' \u00d7 '+f1(sp.D*ss)+' \u00d7 '+f1(sp.H*ss)+' m</span><div class="nav">'+prevNext(s)+'<button class="ma-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div></div>'+
   '<div class="ma-graph"><span class="lbl">Decay by frequency'+linkCtl(s)+'<span style="margin-left:auto">RT60 '+esc(fmt(s,'decay'))+', pre-delay '+esc(fmt(s,'pre'))+'</span></span>'+decaySVG('material',s,538,296,{m:[44,18,10,26]})+'</div>'+
   '<div class="ma-photo">'+photoEl(s,0,0,'position:absolute;inset:0')+'<span class="tag">'+esc(sp.place.toUpperCase())+', '+esc(sp.date.toUpperCase())+'</span></div>'+
   '<div class="ma-sw">'+swatchWindow(s).map(function(i){var x=SPACES[i],m=MAT[x.kind];return '<button data-act="pick" data-v="'+i+'" aria-current="'+(i===s.sp)+'"><i style="background:linear-gradient(135deg,'+m.g+' 50%,'+m.ac+' 50%)"></i><span>'+esc(x.name)+'<small>'+f1(decayOf(x.bands))+' s</small></span></button>';}).join('')+'</div>'+ctl+
   '<div class="ma-strip">'+hintText(s)+meters()+'</div>';},

 folio:function(s){var sp=spOf(s),e=eqp(s.el),parts=sp.name.split(' '),last=parts.pop();
  var row=function(k){return '<div class="fo-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+lockBtn(s,k,P[k].l)+'</div>';};
  var page=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="fo-page"><div class="fo-col"><span class="hd">'+esc(sp.no)+'</span><p><b>'+esc(sp.place)+'.</b> '+esc(sp.mat)+', '+vol(sp)+'. Captured '+esc(sp.date)+' with '+esc(sp.rec.toLowerCase())+'.</p><div class="dl"><span>Listener</span><span>'+esc(fmt(s,'dist'))+' from the source</span><span>Early</span><span>'+e.e+' dB</span><span>Tail</span><span>'+e.t+' dB</span><span>Pre-delay</span><span>'+esc(fmt(s,'pre'))+'</span></div></div>'+
   '<div class="fo-col fo-fig"><span class="hd">Fig. 1, decay time by frequency'+linkCtl(s,'end')+'</span>'+decaySVG('folio',s,468,236,{m:[48,14,12,24]})+'<p class="fc">Solid as set, dashed as captured'+(s.link?'':', faint the other side')+'. Drag a point to change one band.</p></div>'+
   '<div class="fo-col"><span class="hd">Settings</span>'+MAIN.map(row).join('')+'<div class="fo-mix">'+knobEl(s,'mix',44,'folio')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div><div class="fo-acts"><button class="fo-btn" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="fo-btn" data-act="match">Match a room</button><button class="fo-btn" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button>'+eqBtn(s,'fo-btn')+wfBtn(s,'fo-btn')+'</div></div></div></div>';
  return '<div class="fo-hero">'+photoEl(s,0,0,'position:absolute;inset:0')+'<div class="fo-top">'+wordmark(s,'fo-wm','Drenched')+segMode(s)+segAB(s)+'</div><div class="fo-title"><div><span class="kick">'+esc(sp.place)+'</span><h3>'+esc(parts.join(' '))+(parts.length?' ':'')+'<em>'+esc(last)+'</em></h3></div><div class="nav">'+prevNext(s)+'<button class="fo-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div></div></div>'+page+
   '<div class="fo-foot"><span class="no">DRENCHED, '+esc(sp.no.toUpperCase())+'</span>'+hintText(s)+meters()+'</div>';}

};
function screenHTML(id,s){return '<div class="sys s-'+id+' screen'+(s.mode==='stage'?' stage':'')+'" data-sys="'+id+'"'+(STYLE[id]?' style="'+STYLE[id](s)+'"':'')+'>'+(s.mode==='stage'?stageHTML(id,s):COMPOSE[id](s))+(s.browse?browserHTML(s):'')+'</div>';}
