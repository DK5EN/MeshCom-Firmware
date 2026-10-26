// RM GUI: every request of the page carries `X-MC: 1`. The node refuses state-changing requests
// (setparam, callfunction, sendmessage, every POST) without it or without a same-host Origin: a foreign
// web page cannot set a custom header without a CORS preflight, which the node does not answer.
(function(){var f=window.fetch;if(f){window.fetch=function(u,o){o=o||{};var h=new Headers(o.headers||{});h.set('X-MC','1');o.headers=h;return f.call(window,u,o);};}var s=window.XMLHttpRequest&&XMLHttpRequest.prototype.send;if(s){XMLHttpRequest.prototype.send=function(b){try{this.setRequestHeader('X-MC','1');}catch(e){}return s.call(this,b);};}})();
// these variables will hold the last loaded page name and sender in order to force a refresh
cpage="info";csender=undefined;
setInterval(autorefresh,30000);
// This function will be called in intervalls - can be used to auto-refresh content depending on what page is loaded
function autorefresh() {if(cpage=='messages')updateMessages();if(cpage=='wx')loadPage('wx',csender,false);if(cpage=='position')loadPage('position',csender,false);if(cpage=='mheard')loadPage('mheard',csender,false);if(cpage=='path')loadPage('path',csender,false);if(cpage=='rxlog')loadPage('rxlog',csender,false);};
// this function is used for login and logout
function login(pwd){var xhttp = new XMLHttpRequest(); xhttp.onreadystatechange=function(){if(this.readyState==4 && this.status==200){window.location.reload(true);}};xhttp.open("GET","?nodepassword="+pwd,true);xhttp.send();}
// this function is used to send a message from the browser via node to the mesh
//
// BP-09: the input fields used to be cleared unconditionally, right after
// xhttp.send() and without ever looking at the answer -- so a message the
// node refused (QRT) or dropped (QTA) took the operator's typed text with
// it, exactly the loss BP-09 fixes on the T-Deck and T-Deck Pro. The clear
// now waits for the response and only fires on "sendmessage ok"; every
// other outcome (refused / dropped / invalid / failed) leaves the text in
// place so it can be resent after the QRV. updateCharsLeft() is called
// from the handler because the onclick chain has long since run its own
// call by the time the answer arrives.
//
// The destination survives the send when it is a group number: the tab
// bar puts the selected group into #sendcall, and clearing it after every
// send silently turned the next quick message into a broadcast to '*'
// (DJ8MEH, 2026-09-11). A DM call sign is still cleared as before.
function sendMessage() {var xhttp=new XMLHttpRequest();xhttp.onreadystatechange=function(){if(this.readyState==4 && this.status==200 && this.responseText.indexOf("sendmessage ok")>=0){var sc=document.getElementById("sendcall");if(!/^[0-9]+$/.test(sc.value))sc.value=""; document.getElementById("messagetext").value=""; updateCharsLeft();}};xhttp.open("GET","/?sendmessage&tocall="+encodeURIComponent(document.getElementById("sendcall").value)+"&message="+encodeURIComponent(document.getElementById("messagetext").value),true);xhttp.send();}
// counts what is left of the node's 150-byte message limit (send_message() adds ":" or ":{call}").
// #1173: count UTF-8 bytes, not characters -- "ą" costs 2, an emoji 4 -- and cut an over-long
// text between characters (for..of walks code points, so no surrogate pair is split).
var mcEnc=new TextEncoder();function updateCharsLeft() {let maxlength=149;let call=document.getElementById("sendcall").value;if(call.length>0) {maxlength-=mcEnc.encode(call).length+2;}let t=document.getElementById("messagetext");let msglength=mcEnc.encode(t.value).length;if(msglength>maxlength){let out="";msglength=0;for(const ch of t.value){let b=mcEnc.encode(ch).length;if(msglength+b>maxlength)break;out+=ch;msglength+=b;}t.value=out;}document.getElementById("indicator_charsleft").innerHTML=maxlength-msglength;}
// MC-msg-history: BLEtoPhoneBuff/MAX_RING is only 20 slots and is shared
// with positions and acks, so a handful of new messages can push an old
// message out of the node's own ring within minutes. The browser tab
// keeps every message it has seen for the life of the page in
// mcHistory/mcSeen (capped at MC_HIST_MAX, oldest dropped first) so
// switching Info -> Messages -> Info -> Messages does not lose messages
// that scrolled out of the node's ring. This is browser memory only --
// node RAM is unchanged, and persisting the history to localStorage is
// deferred (not implemented in this round).
var mcSeen={};
var mcHistory=[];
var MC_HIST_MAX=200;
// MC-msg-tabs: this and the rest of the mcTab* functions live in the
// scaffold rather than sub_page_messages() because sub-pages are
// injected via innerHTML and their own <script> tags never run.
var mcTabSel='all';
try{var mcTabStored=localStorage.getItem('mcTab');if(mcTabStored!=null)mcTabSel=mcTabStored;}catch(e){}
// MC-msg-badges: unread-state model. mcReadIds is a session-only set of
// message ids the operator has definitely seen -- it survives while
// mcHistory holds the entry, but not a browser restart. mcWm holds, per
// tab key, a unix-time watermark below which everything on that tab
// counts as read; it is the only read-state that is persisted
// (localStorage 'mcWm'), because persisting the exact id set across days
// of ring turnover would grow without bound. A message is unread for a
// tab key when it is inbound (an own message-send is never unread), it
// matches that key's dst filter, its id has not been marked read this
// session, and its ts is strictly greater than that key's watermark --
// equal timestamps fall back to the id set, since an unsynced node clock
// can hand two independent messages the same second. Viewing "All"
// raises every tab's watermark to the newest message seen, because
// looking at the merged stream is defined as having read everything in
// it, not only the "all" tab itself.
var mcReadIds={};
var mcWm={};
try{var mcWmStored=localStorage.getItem('mcWm');if(mcWmStored!=null)mcWm=JSON.parse(mcWmStored);}catch(e){}if(typeof mcWm!=='object' || mcWm===null)mcWm={};
function mcSaveWm(){try{localStorage.setItem('mcWm',JSON.stringify(mcWm));}catch(e){}}
function mcTabMatchKey(key,dst){if(key=='all')return true;if(key=='*')return dst=='*';if(key=='dm')return dst!='*' && !/^[0-9]+$/.test(dst);return dst==key;}
function mcTabMatch(dst){return mcTabMatchKey(mcTabSel,dst);}
function mcUnreadCount(key){var wm=mcWm[key]||0;var n=0;for(var i=0;i<mcHistory.length;i++){var m=mcHistory[i];if(m.rx && mcTabMatchKey(key,m.dst) && !mcReadIds[m.id] && m.ts>wm)n++;}return n;}
// seeds the watermark of any tab key that has never had one (first visit
// to this browser, or a group number just added in setup) to the newest
// ts already known, so first load never shows a wall of unread badges
function mcSeedWm(){var btns=document.querySelectorAll('#mctabs .mctab');if(btns.length==0)return;var maxTs=0;for(var i=0;i<mcHistory.length;i++){if(mcHistory[i].ts>maxTs)maxTs=mcHistory[i].ts;}var changed=false;for(var j=0;j<btns.length;j++){var key=btns[j].getAttribute('data-tab');if(!(key in mcWm)){mcWm[key]=maxTs;changed=true;}}if(changed)mcSaveWm();}
// marks every history entry on the currently selected tab as read; viewing
// 'all' counts as having read every tab, so its watermark propagates to
// every configured tab key, not only 'all' itself
function mcMarkRead(){if(document.visibilityState==='hidden')return;var maxTs=0;var changed=false;for(var i=0;i<mcHistory.length;i++){var m=mcHistory[i];if(mcTabMatchKey(mcTabSel,m.dst)){if(!mcReadIds[m.id]){mcReadIds[m.id]=true;changed=true;}if(m.ts>maxTs)maxTs=m.ts;}}if(maxTs>(mcWm[mcTabSel]||0)){mcWm[mcTabSel]=maxTs;changed=true;}if(mcTabSel=='all'){var btns=document.querySelectorAll('#mctabs .mctab');for(var j=0;j<btns.length;j++){var key=btns[j].getAttribute('data-tab');if(maxTs>(mcWm[key]||0)){mcWm[key]=maxTs;changed=true;}}}if(changed)mcSaveWm();}
// the tab bar is re-rendered by the server on every loadPage('messages'),
// so badges are painted here on every call, never once at page load
function mcApplyTab(){var panel=document.getElementById('messages_panel');if(!panel)return;var els=panel.querySelectorAll('.message[data-dst]');for(var i=0;i<els.length;i++){els[i].hidden=!mcTabMatch(els[i].getAttribute('data-dst'));}mcSeedWm();mcMarkRead();var btns=document.querySelectorAll('#mctabs .mctab');for(var j=0;j<btns.length;j++){var key=btns[j].getAttribute('data-tab');btns[j].classList.toggle('mctab-on',key==mcTabSel);var label=key=='all'?'All':(key=='dm'?'DM':key);var cnt=mcUnreadCount(key);btns[j].innerHTML=label+(cnt>0?' <span class="mcbadge">'+cnt+'</span>':'');btns[j].classList.toggle('mctab-new',cnt>0);}}
function mcTab(btn){mcTabSel=btn.getAttribute('data-tab');try{localStorage.setItem('mcTab',mcTabSel);}catch(e){}var sc=document.getElementById('sendcall');if(sc){if(/^[0-9]+$/.test(mcTabSel))sc.value=mcTabSel;else if(mcTabSel=='*')sc.value='';}if(typeof updateCharsLeft==='function' && sc)updateCharsLeft();mcApplyTab();}
// messages that arrive while the tab is hidden must not count as read
// until the operator actually comes back to look at them
document.addEventListener('visibilitychange',function(){if(cpage=='messages')mcApplyTab();});
function mcHistIndex(id){for(var i=0;i<mcHistory.length;i++){if(mcHistory[i].id==id)return i;}return -1;}
// a tick is rank-monotone: the server's status ring is shared and churned and drops the tick of an old message, so a ranked-higher tick of the remembered entry survives (rank = index 0,1,4,3,2: heard < held < failed < ACK)
function mcTickKeep(h,el){if(!el.classList.contains('message-send'))return;var t=document.createElement('template');t.innerHTML=h;var o=t.content.querySelector('.mctick');if(!o)return;var n=el.querySelector('.mctick');var R=[0,1,4,3,2];if(n&&R[n.getAttribute('data-st')]>=R[o.getAttribute('data-st')])return;if(n)n.replaceWith(o);else{var p=el.querySelector('p');if(p)p.insertBefore(o,p.firstChild);}}
function mcMergeEntry(el){var id=el.getAttribute('data-id');var idx=mcHistIndex(id);if(idx>=0)mcTickKeep(mcHistory[idx].html,el);var html=el.outerHTML;var dst=el.getAttribute('data-dst')||'';var ts=parseInt(el.getAttribute('data-ts'))||0;var rx=el.classList.contains('message-received');if(idx<0){mcHistory.push({id:id,html:html,dst:dst,ts:ts,rx:rx});mcSeen[id]=true;if(mcHistory.length>MC_HIST_MAX){var dropped=mcHistory.shift();delete mcSeen[dropped.id];}}else{mcHistory[idx].html=html;mcHistory[idx].dst=dst;mcHistory[idx].ts=ts;mcHistory[idx].rx=rx;}}
function mcRemovePlaceholder(panel){var kids=panel.children;for(var i=kids.length-1;i>=0;i--){if(kids[i].tagName=='P')panel.removeChild(kids[i]);}}
// this function is an ayncronous loader that is used to update the received messages without re-loading the whole page, it will re-call itself after a timeout as long as the message-page is displayed
// it merges the response into mcHistory/mcSeen instead of overwriting the panel outright, so a message already on screen keeps its DOM position when only its ack mark changed
function mcProcessMessages(text,panel){var tmpl=document.createElement('template');tmpl.innerHTML=text;var els=tmpl.content.querySelectorAll('.message[data-id]');for(var i=0;i<els.length;i++){var el=els[i];var id=el.getAttribute('data-id');var existed=mcSeen.hasOwnProperty(id);mcMergeEntry(el);if(existed){var old=panel.querySelector('.message[data-id="'+id+'"]');if(old)old.replaceWith(el);}else{panel.appendChild(el);}}mcRemovePlaceholder(panel);if(mcHistory.length==0)panel.innerHTML='<p>No messages available.</p>';if(typeof mcApplyTab==='function')mcApplyTab();}
function updateMessages() {var xhttp=new XMLHttpRequest();xhttp.onreadystatechange=function(){if(this.readyState==4 && this.status==200){var panel=document.getElementById('messages_panel');if(panel!=null)mcProcessMessages(this.responseText,panel);}};setTimeout(function(){xhttp.open('GET','/?getmessages',true);xhttp.send();},1000);}
// rebuilds #messages_panel from mcHistory when the messages page is (re-)injected by loadPage(); first merges the server-rendered entries already sitting in the panel into mcHistory (same dedupe as mcProcessMessages) so nothing the server just sent is lost, then renders the full remembered history in order
function mcRenderHistory(){var panel=document.getElementById('messages_panel');if(!panel)return;var els=panel.querySelectorAll('.message[data-id]');for(var i=0;i<els.length;i++){mcMergeEntry(els[i]);}var html='';for(var j=0;j<mcHistory.length;j++){html+=mcHistory[j].html;}panel.innerHTML=html.length>0?html:'<p>No messages available.</p>';if(typeof mcApplyTab==='function')mcApplyTab();}
// this function sends a parameter:value request to the backend
// TRK-01: bei Erfolg (returncode==0, die Seite wird hier NICHT neu geladen) den Warnhinweis
// "<id>_warn" live ein-/ausblenden -- generisch ueber param, damit kuenftige Switches denselben Mechanismus erben
function setvalue(param,value,refresh) {fetch("/setparam/?"+param+"="+encodeURIComponent(value)).then(function(response){return response.json();}).then(function(jsonResponse){if(jsonResponse['returncode']==1)alert("Value could not be set.");if(jsonResponse['returncode']==2)alert("Parameter unknown to node.");if(jsonResponse['returncode']==0){var w=document.getElementById(param+"_warn");if(w)w.style.display=(value=="on")?"":"none";}if(jsonResponse['returncode']>0){loadPage(cpage,csender,false)}if(refresh)loadPage(cpage,csender,false);});}
// this function invokes a function call to the backend passing the function name and an optional parameter (e.g. sendpos)
function callfunction(functionname,functionparameter){fetch("/callfunction/?"+functionname+"="+functionparameter).then(function(response){return response.json();}).then(function (jsonResponse) {/*Nothing todo yet.*/})}
// CS-03: config restore. Lives here and not in the setup page, because the
// sub-pages are injected with innerHTML -- a <script> inside them never runs.
function uploadconfig(){var e=document.getElementById("cfgfile");if(!e||e.files.length==0){alert("choose a config file first");return;}if(!confirm("Restore this configuration and reboot the node?"))return;var r=new FileReader();r.onload=function(){fetch("/config",{method:"POST",headers:{"Content-Type":"application/json"},body:r.result}).then(function(x){return x.text().then(function(t){var m=t.replace(/<[^>]*>/g," ").trim();if(x.ok){alert("config imported: "+m);}else{alert("import failed: "+m);}});}).catch(function(err){alert("upload failed: "+err);});};r.readAsText(e.files[0]);}
// This function is used to toggle a css class so setup cars can collapse / expand
function togglecard(element){element.parentElement.classList.toggle("cardopen");}
// Remote Admin card of the Settings page (sub_page_setup()): scripts of sub-pages never run, so the logic lives here;
// loadPage() calls raSt() after the setup fragment is in. Password only in the POST body, never in a URL.
// RA-JS-BEGIN
var raA=0,raT=0;
function raM(t,b){var e=rmEl('ra_msg');if(e){e.textContent=t;e.style.color=b?'var(--mcred)':'';}}
function raX(e,t){raM(e===0?'Log in again to change these settings.':t,1);}
function raDis(m){clearTimeout(raT);raA=0;var b=rmEl('ra_clr');if(b)b.textContent='Clear';if(m)raM('');}
function raSt(){fetch('/rmstatus').then(rmJson).then(function(j){var o=rmEl('ra_on');if(!o)return;o.checked=!!j.on;o.disabled=!j.pw&&!j.on;
rmEl('ra_hint').textContent=j.pw?'':'Set a password first, then switch remote management on.';
rmEl('ra_st').textContent='password: '+(j.pw?'set':'not set')+(j.lock?', a sender is blocked for '+(j.lockS||1)+' s after wrong attempts':'');})
.catch(function(e){raX(e,'Could not read the state of this node. Reload the page.');});}
function raP(b,t){rmPost('/rmpasswd',b).then(rmJson).then(function(j){if(j&&j.ok)raM(t);else raM(rmErrText(j),1);raSt();})
.catch(function(e){raX(e,'The node did not answer. Try again.');});}
function raSet(){var i=rmEl('ra_pw'),p=i.value,e=rmPwProblem(p);raDis();
if(e){raM(e,1);return;}i.value='';raP('act=set&pw='+rmEnc(p),'Password set.');}
function raClr(){if(!raA){raA=1;rmEl('ra_clr').textContent='Really? tap again';raM('Clear the password? Remote management will be switched off.');raT=setTimeout(function(){raDis(1);},4000);return;}
raDis();raP('act=clear','Password cleared. Remote management is off.');}
function raOn(){var o=rmEl('ra_on'),v=o.checked?'on':'off';o.disabled=true;fetch('/setparam/?rm='+v).then(rmJson).then(function(j){var k=j&&j.returncode==0;
raM(k?'Remote management is '+v+'.':'Could not change it. Is a password set?',!k);raSt();})
.catch(function(e){raX(e,'The node did not answer. Try again.');raSt();});}
// RA-JS-END
// WQ-01: LoRa Queue panel on the rxlog page. rxlog is fetched by loadPage()
// and injected with innerHTML, which never runs a <script> tag it carries,
// so the rendering logic has to live here in the scaffold instead and be
// invoked from loadPage() after each fragment swap (that covers both the
// initial page load and the 30s autorefresh). The fragment itself only
// emits an empty #mcq div carrying data-* attributes; all markup below is
// built from those. JS strings use single quotes and HTML attribute
// values are written unquoted (none of them ever contain a space) so
// nothing here needs quote-escaping in the C string literals.
var mcQueueOpen=true;
function mcQueueToggle(btn){mcQueueOpen=!mcQueueOpen;var b=document.getElementById('mcq');if(b)b.hidden=!mcQueueOpen;if(btn)btn.textContent=mcQueueOpen?'hide':'show';}
function mcRenderQueue(){
var d=document.getElementById('mcq');
if(!d)return;
var p=[0,0,0,0,0,0];
var w=[0,0,0,0,0,0];
for(var i=1;i<=5;i++){p[i]=parseInt(d.getAttribute('data-p'+i))||0;w[i]=parseInt(d.getAttribute('data-w'+i))||0;}
var w0=parseInt(d.getAttribute('data-w0'))||0;
var ring=parseInt(d.getAttribute('data-ring'))||0;
var used=parseInt(d.getAttribute('data-p0'))||0;
var bp=parseInt(d.getAttribute('data-bp'))||0;
var bpname=d.getAttribute('data-bpname')||'';
var qrs=parseInt(d.getAttribute('data-qrs'))||0;
var qrsf=parseInt(d.getAttribute('data-qrsf'))||qrs;
var qrt=parseInt(d.getAttribute('data-qrt'))||0;
var win=parseInt(d.getAttribute('data-win'))||0;
var rx=parseInt(d.getAttribute('data-rx'))||0;
var tx=parseInt(d.getAttribute('data-tx'))||0;
var intv=parseInt(d.getAttribute('data-int'))||300;
var newid=parseInt(d.getAttribute('data-newid'))||0;
var dup=parseInt(d.getAttribute('data-dup'))||0;
var dwin=parseInt(d.getAttribute('data-dwin'))||0;
var age=parseInt(d.getAttribute('data-age'))||0;
var empty=ring-used;
if(empty<0)empty=0;
var qrsPct=ring>0?(qrs/ring*100):0;
var qrsfPct=ring>0?(qrsf/ring*100):0;
var qrtPct=ring>0?(qrt/ring*100):0;
var colors=['#A2182F','#E07B39','#3B7DD8','#6FA96F','#9E9E9E'];
var names=['crit','high','normal','low','bg'];
var html='';
html+='<div class=font-bold>TX queue</div>';
html+='<div class=mcq-barwrap><div class=mcq-bar>';
for(var pr=1;pr<=5;pr++){var wn=Math.min(w[pr],p[pr]);for(var c=0;c<p[pr]-wn;c++){html+='<div class=mcq-cell style=background:'+colors[pr-1]+'></div>';}for(var cw=0;cw<wn;cw++){html+='<div class=mcq-cell-wait style=border-color:'+colors[pr-1]+'></div>';}}
for(var c2=0;c2<empty;c2++){html+='<div class=mcq-cell-empty></div>';}
html+='</div>';
html+='<div class=mcq-tick-faint style=left:'+qrsPct+'%></div>';
html+='<div class=mcq-tick style=left:'+qrsfPct+'%></div>';
html+='<div class=mcq-tick style=left:'+qrtPct+'%></div>';
html+='</div>';
html+='<div class=mcq-ticklabels>';
html+='<span class=font-small style=position:absolute;left:'+qrsfPct+'%;transform:translateX(-50%)>QRS</span>';
html+='<span class=font-small style=position:absolute;left:'+qrtPct+'%;transform:translateX(-50%)>QRT</span>';
html+='</div>';
html+='<div class=mcq-legend>'+used+'/'+ring+' queued&nbsp;|&nbsp;';
for(var pr2=1;pr2<=5;pr2++){html+='<span class=mcq-swatch style=background:'+colors[pr2-1]+'></span>'+names[pr2-1]+' '+p[pr2]+' ';}
if(w0>0){html=html.replace(/ +$/,'')+', '+w0+' waiting for ACK';}
html+='</div>';
var bpcolor=(bp==0)?'#3B9E4F':((bp==1)?'#E07B39':'#A2182F');
var bptxt=(bp==0)?'accepted':((bp==1)?'slow down':'refused');
html+='<div class=font-bold>Your messages: <span style=color:'+bpcolor+'>'+bptxt+'</span></div>';
var drop=parseInt(d.getAttribute('data-drop'))||0;
var iv=intv*1000;
html+='<div class=font-bold>Queue: '+used+'/'+ring+' queued'+(win?', '+drop+' dropped in the last '+Math.round(intv/60)+' min, TX '+(tx/iv*100).toFixed(1)+' % / RX '+(rx/iv*100).toFixed(1)+' %':'')+'</div>';
html+='<div class=font-small>(QRS forecast at&nbsp;'+qrsf+' for your next msgs, line&nbsp;&ge;'+qrs+', QRT at&nbsp;&ge;'+qrt+' of '+ring+')</div>';
if(win==0){
html+='<div class=font-bold>Dedup window: n/a (no completed 5-min window yet)</div>';
}else if(dwin==0){
html+='<div class=font-bold>Dedup window: n/a (no new ids in the last window)</div>';
}else{
var dwc=(dwin<40||dwin>48)?'#E07B39':'inherit';
html+='<div class=font-bold>Dedup window:&nbsp;&asymp;<span style=color:'+dwc+'>'+dwin+'</span> min</div>';
html+='<div class=font-small>('+newid+' new ids, '+dup+' dups in last '+Math.round(intv/60)+' min; safe corridor 40-48 min)</div>';
}
html+='<div class=font-bold>Channel utilisation (last 5 min)</div>';
if(win==0){
html+='<div class=font-small>n/a</div>';
}else{
var rxPct=rx/(intv*1000)*100;if(rxPct>100)rxPct=100;
var txPct=tx/(intv*1000)*100;if(txPct>100)txPct=100;
var totPct=(rx+tx)/(intv*1000)*100;if(totPct>100)totPct=100;
html+='<div class=mcq-util-row><span class=mcq-util-label>rx '+rxPct.toFixed(1)+'%</span><div class=mcq-util-track><div style=height:100%;width:'+rxPct+'%;background:#3B7DD8></div></div></div>';
html+='<div class=mcq-util-row><span class=mcq-util-label>tx '+txPct.toFixed(1)+'%</span><div class=mcq-util-track><div style=height:100%;width:'+txPct+'%;background:#A2182F></div></div></div>';
html+='<div class=font-small>total '+totPct.toFixed(1)+'% ('+age+' s ago)</div>';
}
d.innerHTML=html;
var btn=document.getElementById('mcqtogglebtn');
d.hidden=!mcQueueOpen;
if(btn)btn.textContent=mcQueueOpen?'hide':'show';
}
