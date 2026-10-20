/* Drenched interface engine: the shared model, the drawing helpers and the twelve
   compositions. Generated from the design reference by design/v4 gen.py; the plugin bridge
   and the interaction layer live in app.js. Plain script, no module system, so it runs in
   every web view JUCE supports. */
'use strict';
var FREQ=['63','125','250','500','1k','2k','4k','8k'];
var SPACES=[
 {name:'Grand Hall',no:'Hall 03',kind:'hall',place:'Concert hall, from the balcony',W:28,D:42,H:16,mat:'Oak and plaster',bands:[2.9,2.8,2.6,2.4,2.3,2.0,1.6,1.1],pre:18,rec:'Spaced omni pair, row 12',date:'March 2024'},
 {name:'Stone Chapel',no:'Chapel 07',kind:'chapel',place:'Nave, mid aisle',W:13,D:36,H:17,mat:'Limestone',bands:[3.9,4.1,3.9,3.6,3.4,3.0,2.3,1.5],pre:24,rec:'Ambisonic, pew 9',date:'November 2023'},
 {name:'Car Park Level 2',no:'Concrete 02',kind:'garage',place:'Underground deck',W:44,D:58,H:3.2,mat:'Bare concrete',bands:[1.5,1.8,2.0,1.9,1.8,1.6,1.3,0.9],pre:9,rec:'Cardioid pair, bay 14',date:'June 2024'},
 {name:'Live Room B',no:'Room 11',kind:'room',place:'Studio live room',W:9,D:12,H:4.2,mat:'Timber and diffusers',bands:[0.72,0.66,0.6,0.55,0.52,0.5,0.45,0.38],pre:4,rec:'Room pair, 3 m',date:'January 2025'}
];
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
 xover:{l:'Crossover',min:100,max:1000,st:1,log:1,help:'sets where Freeze stops holding. Below it, a frozen room keeps fading so the bass never builds up.'},
 locut:{l:'Wet low cut',min:20,max:1000,st:1,log:1,help:'removes low end from the wet signal.'},
 hicut:{l:'Wet high cut',min:1000,max:20000,st:10,log:1,help:'removes top end from the wet signal.'},
 width:{l:'Width',min:0,max:200,st:1,help:'narrows or widens the wet signal.'},
 duck:{l:'Ducking',min:0,max:100,st:1,help:'turns the tail down while the dry signal plays.'},
 mono:{l:'Mono below',min:0,max:500,st:1,help:'sums the wet signal to mono under this frequency.'}
};
var DEEP=['output','preSync','xover','locut','hicut','width','duck','mono'];
var MAIN=['decay','pre','size','bright','el','dist'];
function clamp(v,a,b){return Math.max(a,Math.min(b,v));}
function lerp(a,b,t){return a+(b-a)*t;}
function f1(n){return (Math.round(n*10)/10).toFixed(1);}
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;');}
var UID=0;function uid(){return 'u'+(++UID);}
function vol(sp){var v=sp.W*sp.D*sp.H;v=v>2000?Math.round(v/100)*100:Math.round(v/10)*10;return v.toLocaleString('en-GB')+' m\u00b3';}
function decayOf(b){return (b[3]+b[4])/2;}
function newState(){var sp=SPACES[0];return {sp:0,bands:sp.bands.slice(),mix:30,pre:sp.pre,size:100,bright:0,el:55,dist:40,output:0,preSync:0,xover:320,locut:80,hicut:14000,width:100,duck:0,mono:0,lx:0.5,mode:'studio',slot:'A',slots:{A:null,B:null},freeze:false,adv:false,browse:false,view:'plan',locks:{},hint:null,note:null,lastBand:4,skin:'aperture',skins:false,inL:-60,inR:-60,outL:-60,outR:-60};}
var st=newState();
function spOf(s){return SPACES[s.sp];}
function getV(s,k){return k==='decay'?decayOf(s.bands):s[k];}
function natV(s,k){var sp=spOf(s);return {decay:decayOf(sp.bands),pre:sp.pre,size:100,bright:0,el:55,dist:40}[k];}
function isOver(s,k){if(k==='decay'){var sp=spOf(s);return s.bands.some(function(b,i){return Math.abs(b-sp.bands[i])>0.004;});}var n=natV(s,k);return n!=null&&Math.abs(getV(s,k)-n)>P[k].st/2;}
function distM(s){var sp=spOf(s),D=sp.D*s.size/100,lo=Math.min(1.5,D*0.15);return lo+s.dist/100*(D*0.85-lo);}
function distFromM(s,m){var sp=spOf(s),D=sp.D*s.size/100,lo=Math.min(1.5,D*0.15);return clamp((m-lo)/(D*0.85-lo)*100,0,100);}
function eqp(el){var a=el/100*Math.PI/2;function d(x){return x<0.001?'\u2212\u221e':(20*Math.log10(x)).toFixed(1).replace('-','\u2212');}return {e:d(Math.cos(a)),t:d(Math.sin(a))};}
function fmt(s,k,v){if(v==null)v=getV(s,k);switch(k){
 case 'mix':return Math.round(v)+'%';case 'decay':return f1(v)+' s';case 'pre':return Math.round(v)+' ms';case 'size':return Math.round(v)+'%';
 case 'bright':return (v>0?'+':v<0?'\u2212':'')+Math.abs(v).toFixed(1)+' dB';case 'el':return (100-Math.round(v))+' / '+Math.round(v);
 case 'dist':var old=s.dist;s.dist=v;var m=distM(s);s.dist=old;return f1(m)+' m';
 case 'output':return (v>0?'+':v<0?'\u2212':'')+Math.abs(v).toFixed(1)+' dB';case 'preSync':return v>=0.5?'On':'Off';
 case 'xover':case 'locut':return Math.round(v)+' Hz';case 'hicut':return (v/1000).toFixed(1)+' kHz';
 case 'width':return Math.round(v)+'%';case 'duck':return v>0?Math.round(v)+'%':'Off';case 'mono':return v>0?Math.round(v)+' Hz':'Off';}}
function to01(k,v){var p=P[k];return p.log?Math.log(v/p.min)/Math.log(p.max/p.min):(v-p.min)/(p.max-p.min);}
function from01(k,n){var p=P[k];n=clamp(n,0,1);var v=p.log?p.min*Math.pow(p.max/p.min,n):p.min+n*(p.max-p.min);return Math.round(v/p.st)*p.st;}
function setParam(s,k,v){var p=P[k];v=clamp(Math.round(v/p.st)*p.st,p.min,p.max);
 if(k==='decay'){var cur=decayOf(s.bands);if(cur>0){var fct=v/cur;s.bands=s.bands.map(function(b){return clamp(b*fct,0.1,20);});}}else s[k]=v;s.hint=k;s.note=null;}

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
 more:'<svg width="14" height="14" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true"><path d="M2 4h12M2 8h12M2 12h7"/></svg>'};
function lockBtn(s,k,label){var on=!!s.locks[k];return '<button class="lk" data-act="lock" data-k="'+k+'" aria-pressed="'+on+'" aria-label="Lock '+label+' when changing space" title="Lock '+label+'">'+(on?ICON.lockOn:ICON.lockOff)+'</button>';}
function smooth(pts){var d='M'+pts[0][0].toFixed(1)+' '+pts[0][1].toFixed(1);for(var i=0;i<pts.length-1;i++){var p0=pts[i-1]||pts[i],p1=pts[i],p2=pts[i+1],p3=pts[i+2]||p2;d+='C'+(p1[0]+(p2[0]-p0[0])/6).toFixed(1)+' '+(p1[1]+(p2[1]-p0[1])/6).toFixed(1)+' '+(p2[0]-(p3[0]-p1[0])/6).toFixed(1)+' '+(p2[1]-(p3[1]-p1[1])/6).toFixed(1)+' '+p2[0].toFixed(1)+' '+p2[1].toFixed(1);}return d;}
function niceMax(v){var c=[1,1.5,2,3,4,5,6,8,10,12,16,20];for(var i=0;i<c.length;i++)if(c[i]>=v)return c[i];return 24;}
function rtAt(b,x){var i=Math.floor(x),t=x-i;if(i>=7)return b[7];if(i<0)return b[0];return lerp(b[i],b[i+1],t);}
function hash(a,b){var x=Math.sin(a*12.9898+b*78.233)*43758.5453;return x-Math.floor(x);}
var RAMP=[[0x1B,0x0C,0x41],[0xB5,0x36,0x79],[0xFB,0x9B,0x06],[0xFC,0xFF,0xA4]];
function ramp(e){var x=clamp(e,0,1)*3,i=Math.min(2,Math.floor(x)),t=x-i,a=RAMP[i],b=RAMP[i+1];return 'rgb('+Math.round(lerp(a[0],b[0],t))+','+Math.round(lerp(a[1],b[1],t))+','+Math.round(lerp(a[2],b[2],t))+')';}

/* decay graph. o: {bars, heat, mist, dims, m:[L,R,T,B], noY, big, frame} */
function decaySVG(sysId,s,w,h,o){o=o||{};var sp=spOf(s),b=s.bands,nat=sp.bands,bars=!!o.bars,m=o.m||[bars?14:44,16,bars?30:18,26],L=m[0],R=m[1],T=m[2],B=m[3],pw=w-L-R,ph=h-T-B;
 var ymax=niceMax(Math.max.apply(null,b.concat(nat))*1.15);
 function X(i){return bars?L+pw*(i+0.5)/8:L+pw*i/7;}function Y(v){return T+ph*(1-v/ymax);}
 var u=uid(),out='<svg class="dg" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" data-decay="1" data-l="'+L+'" data-t="'+T+'" data-pw="'+pw+'" data-ph="'+ph+'" data-ymax="'+ymax+'" data-w="'+w+'" data-bars="'+(bars?1:0)+'" role="group" aria-label="Decay time by frequency. Drag to edit a band, or focus a point and use the arrow keys.">';
 if(o.heat){var cols=o.cols||64,rows=o.rows||36,cw=pw/cols,rh=ph/rows;for(var c=0;c<cols;c++){var x01=(c+0.5)/cols,rt=rtAt(b,x01*7);for(var r=0;r<rows;r++){var t=(rows-r-0.5)/rows*ymax,dB=-60*t/rt+(hash(c,r)-0.5)*6-x01*4,e=1+dB/60;if(e<=0.02)continue;out+='<rect x="'+(L+c*cw).toFixed(1)+'" y="'+(T+r*rh).toFixed(1)+'" width="'+(cw+0.6).toFixed(1)+'" height="'+(rh+0.6).toFixed(1)+'" fill="'+ramp(e)+'"/>';}}
  if(o.frame!==false)out+='<rect class="frm" x="'+L+'" y="'+T+'" width="'+pw+'" height="'+ph+'"/>';
  if(o.cb){out+='<defs><linearGradient id="'+u+'cb" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#1B0C41"/><stop offset=".33" stop-color="#B53679"/><stop offset=".66" stop-color="#FB9B06"/><stop offset="1" stop-color="#FCFFA4"/></linearGradient></defs><rect x="'+(w-R+14)+'" y="'+T+'" width="6" height="'+ph+'" fill="url(#'+u+'cb)"/><text x="'+(w-R+26)+'" y="'+(T+8)+'">0 dB</text><text x="'+(w-R+26)+'" y="'+(T+ph/2+4)+'">\u221230</text><text x="'+(w-R+26)+'" y="'+(T+ph)+'">\u221260</text>';}}
 if(o.mist){out+='<defs><filter id="'+u+'mf" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="16"/></filter><linearGradient id="'+u+'mg" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#8FD3C8" stop-opacity=".5"/><stop offset="1" stop-color="#8FD3C8" stop-opacity="0"/></linearGradient></defs><g filter="url(#'+u+'mf)">';for(var mm=0;mm<28;mm++){var xm=mm/27,yv=Y(rtAt(b,xm*7));out+='<rect x="'+(L+xm*pw-14).toFixed(1)+'" y="'+yv.toFixed(1)+'" width="28" height="'+(T+ph-yv+10).toFixed(1)+'" fill="url(#'+u+'mg)" opacity="'+(0.5+hash(mm,3)*0.5).toFixed(2)+'"/>';}out+='</g>';}
 var stp=ymax<=1.5?0.25:ymax<=3?0.5:ymax<=6?1:ymax<=12?2:4;
 for(var v=0;v<=ymax+1e-6;v+=stp){var yy=Y(v).toFixed(1);out+='<line class="gr" x1="'+L+'" x2="'+(L+pw)+'" y1="'+yy+'" y2="'+yy+'"/>';if(!bars&&!o.noY)out+='<text x="'+(L-8)+'" y="'+(+yy+4)+'" text-anchor="end">'+(+v.toFixed(2))+(v===0?'':' s')+'</text>';}
 var xy=o.xy!=null?o.xy:h-8;for(var i=0;i<8;i++){out+='<text x="'+X(i).toFixed(1)+'" y="'+xy+'" text-anchor="middle">'+(bars?FREQ[i].toUpperCase():FREQ[i])+(i===7&&!bars?' Hz':'')+'</text>';}
 if(bars){var bw=o.bw||pw/8-12;for(i=0;i<8;i++){var ed=Math.abs(b[i]-nat[i])>0.004,y0=Y(b[i]);if(o.track)out+='<rect class="trk" x="'+(X(i)-bw/2).toFixed(1)+'" y="'+T+'" width="'+bw.toFixed(1)+'" height="'+ph+'"/>';out+='<rect class="bar'+(ed?' ed':'')+'" data-node="'+i+'" data-fid="nd-'+i+'" x="'+(X(i)-bw/2).toFixed(1)+'" y="'+y0.toFixed(1)+'" width="'+bw.toFixed(1)+'" height="'+(T+ph-y0).toFixed(1)+'" tabindex="0" role="slider" aria-label="Decay at '+FREQ[i]+'Hz" aria-valuemin="0.1" aria-valuemax="20" aria-valuenow="'+b[i].toFixed(2)+'" aria-valuetext="'+b[i].toFixed(2)+' seconds"/>';
   out+='<line class="cap-t" x1="'+(X(i)-bw/2-3).toFixed(1)+'" x2="'+(X(i)+bw/2+3).toFixed(1)+'" y1="'+Y(nat[i]).toFixed(1)+'" y2="'+Y(nat[i]).toFixed(1)+'"/>';
   out+='<text class="bv" x="'+X(i).toFixed(1)+'" y="'+(Math.min(y0,Y(nat[i]))-7).toFixed(1)+'" text-anchor="middle">'+b[i].toFixed(b[i]<1?2:1)+(ed?'*':'')+'</text>';}return out+'</svg>';}
 out+='<path class="dg-cap" d="'+smooth(nat.map(function(v,i){return [X(i),Y(v)];}))+'"/>';
 out+='<path class="dg-cur" d="'+smooth(b.map(function(v,i){return [X(i),Y(v)];}))+'"/>';
 if(o.dims){var lb=s.lastBand,dx=X(lb)+16,y1=Y(b[lb]),y2=Y(0);out+='<path class="dim" d="M'+dx+' '+y1.toFixed(1)+'V'+y2+'M'+(dx-4)+' '+(y1+4).toFixed(1)+'l8 -8M'+(dx-4)+' '+(y2+4)+'l8 -8M'+(X(lb)+5)+' '+y1.toFixed(1)+'H'+(dx+6)+'"/><text class="dimt" x="'+(dx+8)+'" y="'+((y1+y2)/2+4).toFixed(1)+'">'+b[lb].toFixed(2)+' s at '+FREQ[lb]+' Hz</text>';}
 for(i=0;i<8;i++){var e2=Math.abs(b[i]-nat[i])>0.004,cx=X(i).toFixed(1),cy=Y(b[i]).toFixed(1),cl='nd'+(e2?' ed':'')+(i===s.lastBand?' cur':''),ar=' data-node="'+i+'" data-fid="nd-'+i+'" tabindex="0" role="slider" aria-label="Decay at '+FREQ[i]+'Hz" aria-valuemin="0.1" aria-valuemax="20" aria-valuenow="'+b[i].toFixed(2)+'" aria-valuetext="'+b[i].toFixed(2)+' seconds"';
  if(sysId==='section')out+='<rect class="'+cl+'"'+ar+' x="'+(cx-4.5)+'" y="'+(cy-4.5)+'" width="9" height="9"/>';else out+='<circle class="'+cl+'"'+ar+' cx="'+cx+'" cy="'+cy+'" r="'+(o.big?6:sysId==='catalogue'?4.5:5.5)+'"/>';}
 return out+'</svg>';}

/* photographs */
function photoEl(s,w,h,extra){var z=1+(1-s.dist/100)*0.22,st2=w?'width:'+w+'px;height:'+h+'px;':'';return '<div class="phw" style="'+st2+(extra||'')+'" role="img" aria-label="'+esc(spOf(s).name)+'"><div class="phi sp'+s.sp+'" style="transform:scale('+z.toFixed(3)+')'+''+'"></div></div>';}

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
 var sX=X(W/2),sY=Y(sdp?sdp*0.55:1.5),lm=distM(s),lX=X(s.lx*W),lY=Y(lm),dB=Math.hypot(lX-sX,lY-sY);
 [1,1.8,2.7].forEach(function(f,i2){out+='<circle class="pl-ring" cx="'+sX.toFixed(1)+'" cy="'+sY.toFixed(1)+'" r="'+(Math.min(dB,200)*f*0.38+10).toFixed(1)+'" opacity="'+(0.7-i2*0.2)+'"/>';});
 [ox2,X(W)].forEach(function(wx,i3){var ix=i3===0?-sX+2*ox2:2*X(W)-sX,t=(wx-ix)/(lX-ix),py=sY+(lY-sY)*t;out+='<path class="pl-ray" d="M'+sX.toFixed(1)+' '+sY.toFixed(1)+'L'+wx.toFixed(1)+' '+py.toFixed(1)+'L'+lX.toFixed(1)+' '+lY.toFixed(1)+'" opacity=".8"/>';});
 out+='<path class="pl-dir" d="M'+sX.toFixed(1)+' '+sY.toFixed(1)+'L'+lX.toFixed(1)+' '+lY.toFixed(1)+'"/>';
 if(cp)return out+'<circle class="pl-mko" cx="'+sX.toFixed(1)+'" cy="'+sY.toFixed(1)+'" r="4"/><circle class="pl-mk" cx="'+lX.toFixed(1)+'" cy="'+lY.toFixed(1)+'" r="4"/></svg>';
 out+='<path class="pl-d" d="M'+ox2.toFixed(1)+' '+(oy2-24)+'H'+X(W).toFixed(1)+'M'+(ox2-4).toFixed(1)+' '+(oy2-20)+'l8 -8M'+(X(W)-4).toFixed(1)+' '+(oy2-20)+'l8 -8M'+ox2.toFixed(1)+' '+(oy2-30)+'V'+(oy2-8)+'M'+X(W).toFixed(1)+' '+(oy2-30)+'V'+(oy2-8)+'"/><text class="pl-tt" x="'+X(W/2).toFixed(1)+'" y="'+(oy2-30)+'" text-anchor="middle">W '+f1(W)+' m</text>';
 out+='<path class="pl-d" d="M'+(ox2-24)+' '+oy2.toFixed(1)+'V'+Y(D).toFixed(1)+'M'+(ox2-28)+' '+(oy2+4).toFixed(1)+'l8 -8M'+(ox2-28)+' '+(Y(D)+4).toFixed(1)+'l8 -8"/><text class="pl-tt" x="'+(ox2-32)+'" y="'+Y(D/2).toFixed(1)+'" text-anchor="end">D</text><text class="pl-tt" x="'+(ox2-32)+'" y="'+(Y(D/2)+15).toFixed(1)+'" text-anchor="end">'+f1(D)+' m</text>';
 var dX=X(W)+18;out+='<path class="pl-d" d="M'+dX.toFixed(1)+' '+sY.toFixed(1)+'V'+lY.toFixed(1)+'M'+(dX-4).toFixed(1)+' '+(sY+4).toFixed(1)+'l8 -8M'+(dX-4).toFixed(1)+' '+(lY+4).toFixed(1)+'l8 -8M'+(lX+10).toFixed(1)+' '+lY.toFixed(1)+'H'+(dX+4).toFixed(1)+'" opacity=".8"/><text class="pl-tt" x="'+(dX+8).toFixed(1)+'" y="'+((sY+lY)/2+4).toFixed(1)+'">'+f1(Math.abs(lm-(sdp?sdp*0.55:1.5)))+' m</text><text class="pl-t" x="'+(dX+8).toFixed(1)+'" y="'+((sY+lY)/2+18).toFixed(1)+'">S1 to R1</text>';
 out+='<circle class="pl-mko" cx="'+sX.toFixed(1)+'" cy="'+sY.toFixed(1)+'" r="6"/><text class="pl-tt" x="'+(sX+10).toFixed(1)+'" y="'+(sY-8).toFixed(1)+'">S1</text>';
 out+='<g class="lis" data-drag="lis" data-fid="lis" tabindex="0" role="slider" aria-label="Listener position. Up and down change distance, left and right move across the room." aria-valuetext="'+f1(lm)+' metres from the front"><circle class="pl-hit" cx="'+lX.toFixed(1)+'" cy="'+lY.toFixed(1)+'" r="16"/><circle class="pl-mk" cx="'+lX.toFixed(1)+'" cy="'+lY.toFixed(1)+'" r="6"/></g><text class="pl-tt" x="'+(lX+10).toFixed(1)+'" y="'+(lY+18).toFixed(1)+'">R1</text>';
 var sb=stp*sc2;out+='<path class="pl-d" d="M'+(w-16-sb*2).toFixed(1)+' '+(h-22)+'H'+(w-16)+'M'+(w-16-sb*2).toFixed(1)+' '+(h-26)+'V'+(h-18)+'M'+(w-16-sb).toFixed(1)+' '+(h-26)+'V'+(h-18)+'M'+(w-16)+' '+(h-26)+'V'+(h-18)+'"/><text class="pl-t" x="'+(w-16-sb*2).toFixed(1)+'" y="'+(h-30)+'">0</text><text class="pl-t" x="'+(w-16)+'" y="'+(h-30)+'" text-anchor="end">'+(stp*2)+' m</text>';
 return out+'</svg>';}

/* pixel room (Signal) */
function pixelRoom(s,w,h){var sp=spOf(s),ss=s.size/100,cell=12,cols=Math.floor((w-16)/cell),rows=Math.floor((h-36)/cell),ox=(w-cols*cell)/2,oy=6;
 var k=Math.min((cols-4)/sp.W,(rows-4)/sp.D)*(0.55+0.45*(s.size-50)/100),rw=Math.max(6,Math.round(sp.W*k)),rh=Math.max(6,Math.round(sp.D*k));rw=Math.min(rw,cols-2);rh=Math.min(rh,rows-2);
 var c0=Math.floor((cols-rw)/2),r0=Math.floor((rows-rh)/2),sc=c0+Math.floor(rw/2),sr=r0+2,lc=c0+1+Math.round(s.lx*(rw-3)),D=sp.D*ss,lr=r0+1+Math.round(clamp(distM(s)/D,0,1)*(rh-3)),reach=rh*clamp(decayOf(s.bands)/3,0.3,1.2);
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
function bandInputs(s,sp){return s.bands.map(function(v,i){var ed=Math.abs(v-sp.bands[i])>0.004;return '<div><input class="ro'+(ed?' ed':'')+'" data-type="band" data-i="'+i+'" data-fid="bi-'+i+'" value="'+v.toFixed(2)+'" aria-label="Decay at '+FREQ[i]+'Hz, seconds" spellcheck="false"><span>'+FREQ[i]+(i===7?' Hz':'')+'</span></div>';}).join('');}
/* polar decay graph (Rotunda): band as spoke, time as radius */
function polarSVG(sysId,s,size,o){o=o||{};var sp=spOf(s),b=s.bands,nat=sp.bands,c=size/2,r0=o.r0||190,r1=o.r1||280,ymax=niceMax(Math.max.apply(null,b.concat(nat))*1.15);
 function R(v){return r0+(r1-r0)*v/ymax;}function ang(i){return (-90+i*45)*Math.PI/180;}function pt(i,v){var r=R(v),a=ang(i);return [c+r*Math.cos(a),c+r*Math.sin(a)];}
 function poly(arr){return arr.map(function(v,i){var p=pt(i,v);return (i?'L':'M')+p[0].toFixed(1)+' '+p[1].toFixed(1);}).join('')+'Z';}
 var out='<svg class="dg pol" width="'+size+'" height="'+size+'" viewBox="0 0 '+size+' '+size+'" data-decay="1" data-polar="1" data-cx="'+c+'" data-cy="'+c+'" data-r0="'+r0+'" data-r1="'+r1+'" data-ymax="'+ymax+'" data-w="'+size+'" role="group" aria-label="Decay time by frequency, drawn around the room. Drag a point outward for a longer decay, or focus it and use the arrow keys.">';
 var stp=ymax<=1.5?0.25:ymax<=3?0.5:ymax<=6?1:ymax<=12?2:4;
 var la=-67.5*Math.PI/180;for(var v=stp;v<=ymax+1e-6;v+=stp){out+='<circle class="gr" cx="'+c+'" cy="'+c+'" r="'+R(v).toFixed(1)+'"/><text x="'+(c+(R(v)+3)*Math.cos(la)).toFixed(1)+'" y="'+(c+(R(v)+3)*Math.sin(la)+3).toFixed(1)+'">'+(+v.toFixed(2))+' s</text>';}
 for(var i=0;i<8;i++){var a=ang(i),p0=pt(i,0),p1=pt(i,ymax),rl=r1+24;out+='<line class="spk" x1="'+p0[0].toFixed(1)+'" y1="'+p0[1].toFixed(1)+'" x2="'+p1[0].toFixed(1)+'" y2="'+p1[1].toFixed(1)+'"/><text x="'+(c+rl*Math.cos(a)).toFixed(1)+'" y="'+(c+rl*Math.sin(a)+4).toFixed(1)+'" text-anchor="middle">'+FREQ[i]+(i===7?' Hz':'')+'</text>';}
 out+='<path class="dg-cap" d="'+poly(nat)+'"/><path class="dg-cur" d="'+poly(b)+'"/>';
 for(i=0;i<8;i++){var p=pt(i,b[i]),ed=Math.abs(b[i]-nat[i])>0.004,a2=ang(i),rv=R(b[i])+17;
  out+='<circle class="nd'+(ed?' ed':'')+(i===s.lastBand?' cur':'')+'" data-node="'+i+'" data-fid="nd-'+i+'" tabindex="0" role="slider" aria-label="Decay at '+FREQ[i]+'Hz" aria-valuemin="0.1" aria-valuemax="20" aria-valuenow="'+b[i].toFixed(2)+'" aria-valuetext="'+b[i].toFixed(2)+' seconds" cx="'+p[0].toFixed(1)+'" cy="'+p[1].toFixed(1)+'" r="6"/>';
  out+='<text class="bv" x="'+(c+rv*Math.cos(a2)).toFixed(1)+'" y="'+(c+rv*Math.sin(a2)+4).toFixed(1)+'" text-anchor="middle">'+b[i].toFixed(b[i]<1?2:1)+(ed?'*':'')+'</text>';}
 return out+'</svg>';}
/* ridge decay graph (Strata): one ridge per band, time runs sideways */
function ridgesSVG(s,w,h){var sp=spOf(s),b=s.bands,nat=sp.bands,L=64,R=40,T=56,B=28,pw=w-L-R,ph=h-T-B,rh=ph/8,amp=rh*2.2,ymax=niceMax(Math.max.apply(null,b.concat(nat))*1.15);
 function X(v){return L+pw*v/ymax;}
 var out='<svg class="dg rdg" width="'+w+'" height="'+h+'" viewBox="0 0 '+w+' '+h+'" data-decay="1" data-horiz="1" data-l="'+L+'" data-t="'+T+'" data-pw="'+pw+'" data-ph="'+ph+'" data-rh="'+rh.toFixed(2)+'" data-ymax="'+ymax+'" data-w="'+w+'" role="group" aria-label="Decay time by frequency, one ridge per band. Drag the foot of a ridge sideways, or focus it and use the arrow keys.">';
 var stp=ymax<=1.5?0.25:ymax<=3?0.5:ymax<=6?1:ymax<=12?2:4;
 for(var v=0;v<=ymax+1e-6;v+=stp){var xx=X(v).toFixed(1);out+='<line class="gr" x1="'+xx+'" x2="'+xx+'" y1="'+(T+rh-amp).toFixed(1)+'" y2="'+(T+ph+6)+'"/><text x="'+xx+'" y="'+(h-8)+'" text-anchor="middle">'+(+v.toFixed(2))+(v===0?'':' s')+'</text>';}
 var late='';
 for(var i=0;i<8;i++){var base=T+(i+1)*rh,top=base-amp,ed=Math.abs(b[i]-nat[i])>0.004,xb=X(b[i]).toFixed(1),bs=base.toFixed(1),tp=top.toFixed(1);
  out+='<path class="rg-f" style="fill:rgba(239,235,225,'+(0.12-i*0.013).toFixed(3)+')" d="M'+L+' '+tp+'L'+xb+' '+bs+'H'+(L+pw)+'V'+(T+ph)+'H'+L+'Z"/>';
  out+='<path class="rg-cap" d="M'+L+' '+tp+'L'+X(nat[i]).toFixed(1)+' '+bs+'"/>';
  out+='<path class="rg'+(ed?' ed':'')+'" d="M'+L+' '+tp+'L'+xb+' '+bs+'H'+(L+pw)+'"/>';
  late+='<text class="rl" x="'+(L-10)+'" y="'+(base+4).toFixed(1)+'" text-anchor="end">'+FREQ[i]+(i===7?' Hz':'')+'</text>';
  late+='<text class="rv" x="'+(+xb+11)+'" y="'+(base-7).toFixed(1)+'">'+b[i].toFixed(2)+(ed?'*':'')+'</text>';
  late+='<circle class="nd'+(ed?' ed':'')+(i===s.lastBand?' cur':'')+'" data-node="'+i+'" data-fid="nd-'+i+'" tabindex="0" role="slider" aria-label="Decay at '+FREQ[i]+'Hz" aria-valuemin="0.1" aria-valuemax="20" aria-valuenow="'+b[i].toFixed(2)+'" aria-valuetext="'+b[i].toFixed(2)+' seconds" cx="'+xb+'" cy="'+bs+'" r="5.5"/>';}
 return out+late+'</svg>';}
/* material palettes (Material): the room's surface sets the tokens */
var MAT={hall:{g:'#2A1F17',p:'#33271E',r:'#3F3128',ln:'#574538',tx:'#F3E9DB',tx2:'#B9A48F',ac:'#E0A458',onac:'#1C120A'},
 chapel:{g:'#D8D1C2',p:'#E5DFD1',r:'#CBC3B1',ln:'#B0A794',tx:'#262219',tx2:'#6B6355',ac:'#8C4A3C',onac:'#FFFFFF'},
 garage:{g:'#55585A',p:'#606366',r:'#6E7174',ln:'#85888B',tx:'#F2F2F0',tx2:'#D2D4D3',ac:'#FFD23F',onac:'#1A1A1A'},
 room:{g:'#1E2522',p:'#27302C',r:'#313B36',ln:'#44504A',tx:'#EDEFEA',tx2:'#A4ADA6',ac:'#F2845C',onac:'#1A0E08'}};
function matStyle(k){var m=MAT[k];return '--g:'+m.g+';--p:'+m.p+';--r:'+m.r+';--ln:'+m.ln+';--tx:'+m.tx+';--tx2:'+m.tx2+';--ac:'+m.ac+';--ac2:'+m.ac+';--onac:'+m.onac+';--num:'+m.tx+';--mtr:'+m.ac+';--focus:'+m.tx;}
var STYLE={material:function(s){return matStyle(spOf(s).kind);}};

/* shared pieces */
function wordmark(s,cls,inner,raw){return '<button class="wmb" data-act="skins" aria-expanded="'+s.skins+'" aria-label="Change the look, current '+esc(skinOf(s.skin).name)+'" title="Change the look"><span class="'+cls+'"'+(raw?' style="margin:0"':'')+'>'+inner+'</span></button>';}
function segMode(s){return '<div class="seg" role="group" aria-label="View"><button data-act="mode" data-v="studio" aria-pressed="'+(s.mode==='studio')+'">Studio</button><button data-act="mode" data-v="stage" aria-pressed="'+(s.mode==='stage')+'">Stage</button></div>';}
function segAB(s){return '<div class="seg" role="group" aria-label="Compare"><button data-act="slot" data-v="A" aria-pressed="'+(s.slot==='A')+'" aria-label="Snapshot A">A</button><button data-act="slot" data-v="B" aria-pressed="'+(s.slot==='B')+'" aria-label="Snapshot B">B</button></div>';}
function hintText(s){var t;if(s.note)t=s.note;else if(s.hint){var k=s.hint,ov=isOver(s,k),nv=natV(s,k);t='<b>'+P[k].l+' '+esc(fmt(s,k))+'</b>, '+(nv==null?'':ov?'captured '+esc(fmt(s,k,nv))+'. ':'as captured. ')+P[k].l+' '+P[k].help;}
 else t='Drag any control or a point on the graph. Click a value to type it. Shift gives fine steps, and the arrow keys work everywhere.';
 if(s.freeze)t='<b>Frozen.</b> The tail holds until you release Freeze. '+t;return '<div class="hint">'+t+'</div>';}
function dbText(v){return v<=-60?'\u2212\u221e':v.toFixed(1).replace('-','\u2212');}
function meterPct(v){return clamp(1+v/60,0,1)*100;}
function meters(){var i=Math.max(st.inL,st.inR),o=Math.max(st.outL,st.outR);return '<div class="mm">In<span class="bar"><i data-m="in" style="width:'+meterPct(i).toFixed(0)+'%"></i></span><em data-mt="in">'+dbText(i)+'</em></div><div class="mm">Out<span class="bar"><i data-m="out" style="width:'+meterPct(o).toFixed(0)+'%"></i></span><em data-mt="out">'+dbText(o)+'</em></div>';}
function menuHTML(s){return '<div class="menu" role="menu"><div class="mh"><span>Example spaces</span><span>RT60, volume</span></div>'+SPACES.map(function(sp,i){return '<button role="menuitem" data-act="pick" data-v="'+i+'" aria-current="'+(i===s.sp)+'"><span>'+esc(sp.name)+'</span><small>'+f1(decayOf(sp.bands))+' s, '+vol(sp)+'</small></button>';}).join('')+'</div>';}
function advRows(s){
 var deepRow=function(k){if(k==='preSync')return '<div class="row"><span>'+P[k].l+'</span><button class="tg" data-act="toggle" data-k="preSync" aria-pressed="'+(s.preSync>=0.5)+'">'+fmt(s,k)+'</button></div>';return '<div class="row"><span>'+P[k].l+'</span>'+roEl(s,k)+'</div>';};
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
 '<div class="stg-bot"><div class="stg-dec"><span class="lbl">Decay by frequency</span>'+decaySVG(id,s,536,250,{bars:id==='signal',dims:id==='section'})+'</div><div class="stg-ct">'+MAIN.map(function(k){return '<div class="row"><span>'+P[k].l+ovrEl(s,k)+'</span>'+roEl(s,k)+'</div>';}).join('')+'<div class="row"><span>Freeze</span><button class="lk" data-act="freeze" aria-pressed="'+s.freeze+'" style="width:auto;padding:0 8px;font-size:11px">'+(s.freeze?'On':'Off')+'</button></div></div><div class="stg-th">'+th+'<div class="cap"><b>'+esc(sp.name)+'</b><span>'+vol(sp)+', listener '+esc(fmt(s,'dist'))+'</span></div></div></div></div>'+
 '<div class="stg-strip">'+hintText(s)+meters()+'</div>';}

/* ---- the six studio compositions ---- */
var COMPOSE={
 anodised:function(s){var sp=spOf(s),e=eqp(s.el);
  var ctl=function(k){return '<div class="an-ctl">'+roEl(s,k)+knobEl(s,k,72,'anodised')+'<span class="an-lab">'+P[k].l+ovrEl(s,k)+'</span></div>';};
  var top='<div class="an-top">'+wordmark(s,'an-wm','DRENCHED')+prevNext(s)+'<button class="an-sel" data-act="browse" aria-expanded="'+s.browse+'" aria-label="Choose a space, current '+esc(sp.name)+'"><span class="st"><b>'+esc(sp.name)+'</b><small>'+esc(sp.place)+', '+vol(sp)+'</small></span>'+ICON.down+'</button><span style="flex:1"></span>'+segMode(s)+segAB(s)+'</div>';
  var win='<div class="an-win">'+photoEl(s,0,0,'position:absolute;inset:0')+'<div class="scrim"></div><div class="an-name"><b>'+esc(sp.name)+'</b><span>'+esc(sp.mat)+', '+vol(sp)+', captured '+esc(sp.date)+'</span></div>'+
   '<div class="an-ro"><div>'+roEl(s,'decay')+'<span>RT60'+(isOver(s,'decay')?', changed':'')+'</span></div><div>'+roEl(s,'pre')+'<span>Pre-delay</span></div><div><span class="ro" style="font-size:24px;height:30px;width:112px;white-space:nowrap;text-align:right;font-weight:300;display:inline-block;line-height:30px">'+e.e+' dB</span><span>Early</span></div><div><span class="ro" style="font-size:24px;height:30px;width:112px;white-space:nowrap;text-align:right;font-weight:300;display:inline-block;line-height:30px">'+e.t+' dB</span><span>Tail</span></div></div>'+
   '<div class="an-graph">'+decaySVG('anodised',s,600,250,{m:[40,20,14,26]})+'</div>'+
   '<div class="an-bands">'+s.bands.map(function(v,i){var ed=Math.abs(v-sp.bands[i])>0.004;return '<div><input class="ro'+(ed?' ed':'')+'" data-type="band" data-i="'+i+'" data-fid="bi-'+i+'" value="'+v.toFixed(2)+'" aria-label="Decay at '+FREQ[i]+'Hz, seconds" spellcheck="false"><span>'+FREQ[i]+'</span></div>';}).join('')+'</div><div class="glass"></div></div>';
  var deck=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="an-deck"><div class="an-mix">'+roEl(s,'mix')+knobEl(s,'mix',128,'anodised')+'<span class="an-lab">Mix '+lockBtn(s,'mix','Mix')+'</span></div>'+MAIN.map(ctl).join('')+
   '<div class="an-acts"><button class="an-btn" data-act="freeze" aria-pressed="'+s.freeze+'"><i></i>Freeze</button><button class="an-btn" data-act="match"><i></i>Match a room</button><button class="an-btn" data-act="adv" aria-expanded="'+s.adv+'"><i></i>All parameters</button></div></div>';
  return top+win+deck+'<div class="an-strip">'+hintText(s)+meters()+'</div>';},

 section:function(s){var sp=spOf(s),e=eqp(s.el);
  var head='<div class="se-head">'+wordmark(s,'se-title','Drenched')+'<span class="se-sub" style="color:var(--tx);font-size:12px">'+esc(sp.name.toUpperCase())+'</span><span class="se-sub">PLAN AND SECTION, SHEET '+esc(sp.no.replace(/\D/g,''))+'</span>'+prevNext(s)+'<button class="ca-link" data-act="browse" aria-expanded="'+s.browse+'" style="font:11px var(--fnum);letter-spacing:.08em;color:var(--tx2)">CHANGE SPACE</button><span style="flex:1"></span><div class="se-box"><span class="cap">VIEW</span><button data-act="mode" data-v="studio" aria-pressed="'+(s.mode==='studio')+'">STUDIO</button><button data-act="mode" data-v="stage" aria-pressed="'+(s.mode==='stage')+'">STAGE</button></div><div class="se-box"><span class="cap">REV</span><button data-act="slot" data-v="A" aria-pressed="'+(s.slot==='A')+'">A</button><button data-act="slot" data-v="B" aria-pressed="'+(s.slot==='B')+'">B</button></div></div>';
  var plan='<div class="se-plan"><div class="legend"><div class="se-box" style="height:24px;margin-right:6px"><button data-act="view" data-v="plan" aria-pressed="'+(s.view==='plan')+'">PLAN</button><button data-act="view" data-v="section" aria-pressed="'+(s.view==='section')+'">SECTION</button></div><span><i></i>S1 source</span><span><i class="r"></i>R1 listener, drag to move</span></div>'+planView(s,664,620)+'<div class="se-photo">'+photoEl(s,212,132)+'<span>SITE PHOTOGRAPH, '+esc(sp.date.toUpperCase())+'</span></div></div>';
  var row=function(k){return '<div class="se-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+lockBtn(s,k,P[k].l)+'</div>';};
  var right=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="se-right"><div><div class="se-h"><span>SECTION A-A, DECAY TIME BY FREQUENCY</span><span>SOLID AS SET, DASHED AS CAPTURED</span></div>'+decaySVG('section',s,454,214,{dims:true,m:[40,16,16,24]})+'</div>'+
   '<div class="se-sched"><div class="se-row hd"><span>Parameter</span><span>Value</span><span style="text-align:right">Typed</span><span></span></div>'+row('mix')+MAIN.map(row).join('')+'</div>'+
   '<div class="se-acts"><button data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button data-act="match">Match a room</button><button data-act="adv" aria-expanded="'+s.adv+'">All parameters</button></div>'+
   '<div class="se-tb"><div><small>Drawing</small><b>'+esc(sp.name)+'</b></div><div><small>RT60 mid</small><b>'+esc(fmt(s,'decay'))+'</b></div><div><small>Early / tail</small><b>'+e.e+' / '+e.t+' dB</b></div><div><small>Captured</small><b>'+esc(sp.date)+'</b></div></div></div>';
  return '<div class="se-grid"></div><div class="se-frame"></div><div class="se-ticks"></div>'+head+plan+right+'<div class="se-strip">'+hintText(s)+meters()+'</div>';},

 catalogue:function(s){var sp=spOf(s),e=eqp(s.el),parts=sp.name.split(' '),last=parts.pop();
  var row=function(k){return '<div class="ca-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+'</div>';};
  var wall=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="ca-wall"><div class="ca-top">'+wordmark(s,'ca-wm','Drenched')+'<button class="ca-link" data-act="mode" data-v="studio" aria-pressed="'+(s.mode==='studio')+'">Day</button><button class="ca-link" data-act="mode" data-v="stage" aria-pressed="'+(s.mode==='stage')+'">Night</button><span style="width:8px"></span><button class="ca-link" data-act="slot" data-v="A" aria-pressed="'+(s.slot==='A')+'">A</button><button class="ca-link" data-act="slot" data-v="B" aria-pressed="'+(s.slot==='B')+'">B</button></div>'+
   '<div class="ca-no">'+esc(sp.no)+'</div><h3 class="ca-name">'+esc(parts.join(' '))+(parts.length?' ':'')+'<em>'+esc(last)+'</em></h3>'+
   '<div class="ca-nav">'+prevNext(s)+'<button class="ca-link" data-act="browse" aria-expanded="'+s.browse+'">Browse the collection</button></div>'+
   '<dl class="ca-dl"><dt>Location</dt><dd>'+esc(sp.place)+'</dd><dt>Volume</dt><dd>'+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</dd><dt>Captured</dt><dd>'+esc(sp.rec)+', '+esc(sp.date)+'</dd><dt>Listener</dt><dd>'+esc(fmt(s,'dist'))+' from the source, early '+e.e+' dB, tail '+e.t+' dB</dd></dl>'+
   '<div class="ca-fig"><div class="fh"><b>Fig. 1</b><span>Decay time by frequency</span></div>'+decaySVG('catalogue',s,488,176,{m:[36,14,12,24]})+'<p class="fc">Solid line as set, dashed as captured. Drag a point to change one band.</p></div>'+
   '<div class="ca-list">'+MAIN.map(row).join('')+'</div>'+
   '<div class="ca-mix">'+knobEl(s,'mix',56,'catalogue')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div><div class="ca-acts"><button class="ca-link" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="ca-link" data-act="match">Match a room</button><button class="ca-link" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button></div></div></div>';
  return '<div class="ca-photo">'+photoEl(s,620,760)+'</div>'+wall;},

 field:function(s){var sp=spOf(s),e=eqp(s.el);
  var fr=function(k,cls){return '<div class="fr '+(cls||'')+'">'+knobEl(s,k,cls?44:34,'field')+'<div class="t"><span>'+P[k].l+ovrEl(s,k)+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</span>'+roEl(s,k)+'</div></div>';};
  var rail=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="fi-rail">'+fr('mix','mix')+MAIN.map(function(k){return fr(k);}).join('')+'<div class="segs">'+segMode(s)+segAB(s)+'</div><div class="acts"><button class="fi-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="fi-btn" data-act="match">'+ICON.match+'Match a room</button><button class="fi-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button></div></div>';
  return '<div class="fi-heat">'+decaySVG('field',s,1200,760,{heat:true,cols:88,rows:44,m:[72,280,150,96],big:true,cb:true,xy:712})+'</div>'+
   '<div class="fi-big">'+roEl(s,'decay').replace('class="ro "','class="ro "')+'<div class="what"><b>RT60, mid band'+(isOver(s,'decay')?', changed':'')+'</b><span>Energy decay, frequency by time, \u221260 dB floor</span></div></div>'+
   '<div class="fi-sp">'+prevNext(s).replace('</button><button','</button><button class="ca-link" data-act="browse" aria-expanded="'+s.browse+'" style="font:13px var(--fui);color:var(--tx);text-decoration:none"><span class="nm">'+esc(sp.name)+', '+vol(sp)+', pre-delay '+esc(fmt(s,'pre'))+'</span></button><button')+'</div>'+
   '<div class="fi-th">'+photoEl(s,148,92)+'<span>'+esc(sp.place)+'</span></div>'+rail+
   '<div class="fi-bands">'+s.bands.map(function(v,i){var ed=Math.abs(v-sp.bands[i])>0.004;return '<input class="ro'+(ed?' ed':'')+'" data-type="band" data-i="'+i+'" data-fid="bi-'+i+'" value="'+v.toFixed(2)+'" aria-label="Decay at '+FREQ[i]+'Hz, seconds" spellcheck="false">';}).join('')+'</div>'+
   '<div class="fi-strip">'+hintText(s)+meters()+'</div>';},

 vitrine:function(s){var sp=spOf(s),pv=SPACES[(s.sp+3)%4],nx=SPACES[(s.sp+1)%4];
  var ctl=function(k){return '<div class="vi-ctl">'+knobEl(s,k,64,'vitrine')+roEl(s,k)+'<span class="nm">'+P[k].l+ovrEl(s,k)+'</span></div>';};
  var rail=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="vi-rail"><div class="vi-mix">'+knobEl(s,'mix',96,'vitrine')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div></div><div class="vi-grid">'+MAIN.map(ctl).join('')+'</div><div class="vi-acts"><button class="vi-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="vi-btn" data-act="match">'+ICON.match+'Match</button><button class="vi-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'More</button></div></div>';
  return '<div class="vi-photo">'+photoEl(s,0,0,'position:absolute;inset:0')+'</div><div class="vi-grade"></div>'+
   '<div class="vi-top">'+wordmark(s,'vi-wm','Drenched')+segMode(s)+segAB(s)+'</div>'+
   '<div class="vi-name"><h3>'+esc(sp.name)+'</h3><span>'+esc(sp.place)+', '+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</span></div>'+
   '<div class="vi-nav"><button data-act="prev">'+ICON.prev+esc(pv.name)+'</button><button data-act="browse" aria-expanded="'+s.browse+'">All spaces'+ICON.down+'</button><button data-act="next">'+esc(nx.name)+ICON.next+'</button></div>'+
   '<div class="vi-lens"><div class="lh"><span>Decay by frequency</span><span class="vv"><span><b>RT60</b>'+roEl(s,'decay')+'</span><span><b>Pre-delay</b>'+roEl(s,'pre')+'</span><span><b>Distance</b>'+roEl(s,'dist')+'</span></span></div>'+decaySVG('vitrine',s,748,250,{mist:true,m:[40,20,14,26],big:true})+'</div>'+rail+
   '<div class="vi-strip">'+hintText(s)+'</div>';},

 signal:function(s){var sp=spOf(s),ss=s.size/100,e=eqp(s.el);
  var blk=function(k,cls){var ed=isOver(s,k);return '<div class="blk'+(ed?' ed':'')+(k==='mix'?' wet mix':'')+' '+(cls||'')+'" data-knob="'+k+'" data-fid="kn-'+k+'" '+ariaFor(s,k)+'><div class="blk-l"><span>'+P[k].l+(ed?' *':'')+'</span>'+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</div>'+roEl(s,k)+'<div class="blk-bar"><i style="width:'+(to01(k,getV(s,k))*100).toFixed(1)+'%"></i></div></div>';};
  var vb=function(l,v,u){return '<div class="blk ro-only"><div class="blk-l"><span>'+l+'</span></div><div class="vbig">'+v+'<small>'+u+'</small></div><div class="blk-bar" style="background:transparent"></div></div>';};
  var rail=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="si-rail">'+blk('mix','mix')+blk('decay')+blk('pre')+'<div class="si-acts"><button class="si-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="si-btn" data-act="match">'+ICON.match+'Match a room</button><button class="si-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button></div></div>';
  return '<div class="si-grid"></div><div class="si-top">'+wordmark(s,'si-wm','<i></i>DRENCHED')+segMode(s)+segAB(s)+'</div>'+
   '<div class="si-space"><div class="nm"><button data-act="browse" aria-expanded="'+s.browse+'" aria-label="Choose a space, current '+esc(sp.name)+'">'+esc(sp.name)+'</button></div><div class="meta">'+prevNext(s)+'<span>'+esc(sp.place)+'</span><span>'+vol(sp)+'</span></div></div>'+
   '<div class="si-room"><div class="si-mh"><span>Room</span><span>'+f1(sp.W*ss)+' \u00d7 '+f1(sp.D*ss)+' \u00d7 '+f1(sp.H*ss)+' m</span></div>'+photoEl(s,398,140)+pixelRoom(s,398,260)+'</div>'+
   '<div class="si-bars"><div class="si-mh"><span>Decay by band</span><span>Seconds, * edited</span></div>'+decaySVG('signal',s,402,494,{bars:true})+'</div>'+rail+
   '<div class="si-vals">'+blk('size')+blk('bright')+blk('el')+blk('dist')+vb('Early',e.e,'dB')+vb('Tail',e.t,'dB')+vb('Listener',f1(distM(s)),'m')+'</div>'+
   '<div class="si-strip">'+hintText(s)+meters()+'</div>';},
 aperture:function(s){var sp=spOf(s);
  var bar=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="ap-nums">'+bandInputs(s,sp)+'</div><div class="ap-bar">'+numCtl(s,'mix','mix')+MAIN.map(function(k){return numCtl(s,k);}).join('')+'<div class="ap-acts"><button class="ap-btn" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="ap-btn" data-act="match">Match a room</button><button class="ap-btn" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button></div></div>';
  return '<div class="ap-photo">'+photoEl(s,0,0,'position:absolute;inset:0')+'</div><div class="ap-grade"></div>'+
   '<div class="ap-top">'+wordmark(s,'ap-wm','Drenched')+segMode(s)+segAB(s)+'</div>'+
   '<div class="ap-name"><h3>'+esc(sp.name)+'</h3><span>'+prevNext(s)+'<button class="ap-link" data-act="browse" aria-expanded="'+s.browse+'">'+esc(sp.place)+', '+vol(sp)+'</button></span></div>'+
   '<div class="ap-graph">'+decaySVG('aperture',s,1120,236,{m:[0,0,16,4],noY:true,big:true})+'</div>'+bar+
   '<div class="ap-strip">'+hintText(s)+meters()+'</div>';},

 contact:function(s){var sp=spOf(s),e=eqp(s.el);
  var row=function(k){return '<div class="co-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+lockBtn(s,k,P[k].l)+'</div>';};
  var right=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="co-right"><div class="co-h"><span>Decay by frequency, seconds</span><span>Ticks mark the capture</span></div><div class="co-fad">'+decaySVG('contact',s,506,340,{bars:true,bw:18,track:true,m:[14,14,30,26]})+'</div>'+
   '<div class="co-list">'+MAIN.map(row).join('')+'</div>'+
   '<div class="co-mix">'+knobEl(s,'mix',48,'contact')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix '+lockBtn(s,'mix','Mix')+'</span></div><div class="co-acts"><button class="co-btn" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="co-btn" data-act="match">Match a room</button><button class="co-btn" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button></div></div></div>';
  return '<div class="co-top">'+wordmark(s,'co-wm','DRENCHED')+segMode(s)+segAB(s)+'</div>'+
   '<div class="co-print">'+photoEl(s,506,430)+'<div class="co-cap"><h3>'+esc(sp.name)+'</h3><div class="co-meta"><span>Location</span><b>'+esc(sp.place)+'</b><span>Volume</span><b>'+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</b><span>Capture</span><b>'+esc(sp.rec)+', '+esc(sp.date)+'</b><span>Listener</span><b>'+esc(fmt(s,'dist'))+', early '+e.e+' dB, tail '+e.t+' dB</b></div></div><div class="co-nav">'+prevNext(s)+'<span style="font:11px var(--fnum);color:var(--tx2);letter-spacing:.08em">'+esc(sp.no.toUpperCase())+'</span><button class="co-link" data-act="browse" aria-expanded="'+s.browse+'">Browse the prints</button></div></div>'+right+
   '<div class="co-strip">'+hintText(s)+meters()+'</div>';},

 strata:function(s){var sp=spOf(s),parts=sp.name.split(' '),last=parts.pop();
  var c=function(k){return '<div class="st-c'+(k==='mix'?' mix':'')+'">'+roEl(s,k)+slideEl(s,k)+'<span class="nm">'+P[k].l+ovrEl(s,k)+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</span></div>';};
  var ctl=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="st-ctl">'+c('mix')+MAIN.map(c).join('')+'</div><div class="st-acts"><button class="st-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="st-btn" data-act="match">'+ICON.match+'Match a room</button><button class="st-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button></div>';
  return '<div class="st-photo">'+photoEl(s,440,760)+'<div class="st-name"><h3>'+esc(parts.join(' '))+(parts.length?' ':'')+'<em>'+esc(last)+'</em></h3><span>'+esc(sp.place)+', '+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</span><div class="nav">'+prevNext(s)+'<button class="st-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div></div></div>'+
   '<div class="st-top">'+wordmark(s,'st-wm','Drenched')+segMode(s)+segAB(s)+'</div>'+
   '<div class="st-ridges">'+ridgesSVG(s,690,420)+'</div>'+ctl+
   '<div class="st-strip">'+hintText(s)+meters()+'</div>';},

 rotunda:function(s){var sp=spOf(s),e=eqp(s.el);
  var c=function(k){return '<div class="rt-c">'+knobEl(s,k,40,'rotunda')+'<div class="t"><span>'+P[k].l+ovrEl(s,k)+'</span>'+roEl(s,k)+'</div></div>';};
  var side=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="rt-side"><h3>'+esc(sp.name)+'</h3><div class="sub"><span>'+esc(sp.place)+', '+vol(sp)+', '+esc(sp.mat.toLowerCase())+'</span><span>Listener '+esc(fmt(s,'dist'))+' from the source, early '+e.e+' dB, tail '+e.t+' dB</span></div><div class="rt-nav">'+prevNext(s)+'<button class="rt-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div>'+
   '<div class="rt-mix">'+knobEl(s,'mix',64,'rotunda')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div></div><div class="rt-grid">'+MAIN.map(c).join('')+'</div>'+
   '<div class="rt-acts"><button class="rt-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="rt-btn" data-act="match">'+ICON.match+'Match</button><button class="rt-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'More</button></div></div>';
  return '<div class="rt-top">'+wordmark(s,'rt-wm','Drenched')+segMode(s)+segAB(s)+'</div>'+
   '<div class="rt-round"><div class="rt-ph">'+photoEl(s,340,340)+'</div>'+polarSVG('rotunda',s,620,{r0:188,r1:276})+'</div>'+side+
   '<div class="rt-strip">'+hintText(s)+meters()+'</div>';},

 material:function(s){var sp=spOf(s),ss=s.size/100;
  var c=function(k){return '<div class="ma-c'+(k==='mix'?' mix':'')+'">'+knobEl(s,k,56,'material')+roEl(s,k)+'<span class="nm">'+P[k].l+ovrEl(s,k)+(k==='mix'?lockBtn(s,'mix','Mix'):'')+'</span></div>';};
  var ctl=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="ma-ctl">'+c('mix')+MAIN.map(c).join('')+'<div class="ma-acts"><button class="ma-btn" data-act="freeze" aria-pressed="'+s.freeze+'">'+ICON.freeze+'Freeze</button><button class="ma-btn" data-act="match">'+ICON.match+'Match a room</button><button class="ma-btn" data-act="adv" aria-expanded="'+s.adv+'">'+ICON.more+'All parameters</button></div></div>';
  return '<div class="ma-top">'+wordmark(s,'ma-wm','DRENCHED')+segMode(s)+segAB(s)+'</div>'+
   '<div class="ma-head"><h3>'+esc(sp.name)+'</h3><span class="sub"><i></i>'+esc(sp.mat)+', '+vol(sp)+', '+f1(sp.W*ss)+' \u00d7 '+f1(sp.D*ss)+' \u00d7 '+f1(sp.H*ss)+' m</span><div class="nav">'+prevNext(s)+'<button class="ma-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div></div>'+
   '<div class="ma-graph"><span class="lbl">Decay by frequency<span style="margin-left:auto">RT60 '+esc(fmt(s,'decay'))+', pre-delay '+esc(fmt(s,'pre'))+'</span></span>'+decaySVG('material',s,538,296,{m:[44,18,10,26]})+'</div>'+
   '<div class="ma-photo">'+photoEl(s,0,0,'position:absolute;inset:0')+'<span class="tag">'+esc(sp.place.toUpperCase())+', '+esc(sp.date.toUpperCase())+'</span></div>'+
   '<div class="ma-sw">'+SPACES.map(function(x,i){var m=MAT[x.kind];return '<button data-act="pick" data-v="'+i+'" aria-current="'+(i===s.sp)+'"><i style="background:linear-gradient(135deg,'+m.g+' 50%,'+m.ac+' 50%)"></i><span>'+esc(x.name)+'<small>'+f1(decayOf(x.bands))+' s</small></span></button>';}).join('')+'</div>'+ctl+
   '<div class="ma-strip">'+hintText(s)+meters()+'</div>';},

 folio:function(s){var sp=spOf(s),e=eqp(s.el),parts=sp.name.split(' '),last=parts.pop();
  var row=function(k){return '<div class="fo-row"><span class="nm">'+P[k].l+ovrEl(s,k)+'</span>'+slideEl(s,k)+roEl(s,k)+lockBtn(s,k,P[k].l)+'</div>';};
  var page=s.adv?'<div class="adv">'+advRows(s)+'</div>':'<div class="fo-page"><div class="fo-col"><span class="hd">'+esc(sp.no)+'</span><p><b>'+esc(sp.place)+'.</b> '+esc(sp.mat)+', '+vol(sp)+'. Captured '+esc(sp.date)+' with '+esc(sp.rec.toLowerCase())+'.</p><div class="dl"><span>Listener</span><span>'+esc(fmt(s,'dist'))+' from the source</span><span>Early</span><span>'+e.e+' dB</span><span>Tail</span><span>'+e.t+' dB</span><span>Pre-delay</span><span>'+esc(fmt(s,'pre'))+'</span></div></div>'+
   '<div class="fo-col fo-fig"><span class="hd">Fig. 1, decay time by frequency</span>'+decaySVG('folio',s,468,236,{m:[36,14,12,24]})+'<p class="fc">Solid as set, dashed as captured. Drag a point to change one band.</p></div>'+
   '<div class="fo-col"><span class="hd">Settings</span>'+MAIN.map(row).join('')+'<div class="fo-mix">'+knobEl(s,'mix',44,'folio')+'<div class="ml">'+roEl(s,'mix')+'<span>Mix, dry to wet '+lockBtn(s,'mix','Mix')+'</span></div><div class="fo-acts"><button class="fo-btn" data-act="freeze" aria-pressed="'+s.freeze+'">Freeze</button><button class="fo-btn" data-act="match">Match a room</button><button class="fo-btn" data-act="adv" aria-expanded="'+s.adv+'">All parameters</button></div></div></div></div>';
  return '<div class="fo-hero">'+photoEl(s,0,0,'position:absolute;inset:0')+'<div class="fo-top">'+wordmark(s,'fo-wm','Drenched')+segMode(s)+segAB(s)+'</div><div class="fo-title"><div><span class="kick">'+esc(sp.place)+'</span><h3>'+esc(parts.join(' '))+(parts.length?' ':'')+'<em>'+esc(last)+'</em></h3></div><div class="nav">'+prevNext(s)+'<button class="fo-link" data-act="browse" aria-expanded="'+s.browse+'">All spaces</button></div></div></div>'+page+
   '<div class="fo-foot"><span class="no">DRENCHED, '+esc(sp.no.toUpperCase())+'</span>'+hintText(s)+meters()+'</div>';}

};
function screenHTML(id,s){return '<div class="sys s-'+id+' screen'+(s.mode==='stage'?' stage':'')+'" data-sys="'+id+'"'+(STYLE[id]?' style="'+STYLE[id](s)+'"':'')+'>'+(s.mode==='stage'?stageHTML(id,s):COMPOSE[id](s))+(s.browse?menuHTML(s):'')+'</div>';}
