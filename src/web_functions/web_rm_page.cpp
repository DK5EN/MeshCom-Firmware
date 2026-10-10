/**
 * RM web GUI W2b: the Remote page (GET /?page=remote) -- HTML skeleton and the scaffold JS that drives it.
 * Contract: docs/rm-gui/impl-plan.md (C4), look and rules: docs/rm-gui/verdict-ux.md, verdict-webtests.md.
 *
 * Rules the code keeps:
 *  - Sub-pages are injected with innerHTML, scripts in them never run: sub_page_remote() prints HTML and an
 *    inline <style> only; rmScaffoldJs() prints the JS once, from deliver_scaffold().
 *  - Every web_client call is a print/println of ONE string literal of at most 512 bytes (RAK4631: one write is
 *    capped at 2048 B and the rest dropped; nRF52 printf over-reads above 255 B). No printf here.
 *  - The JS writes all server data with textContent. Passwords go in POST bodies only, the input is cleared
 *    when the request is built, nothing is stored in the browser.
 *  - tools/webgui_rm_test.js extracts the literals below and runs them in jsdom: keep the print/println
 *    string-literal form (adjacent literals are concatenated, only \\ \" \n escapes are used).
 *  - The JS carries no comments (they would cost bytes on every page load) and no preprocessor lines.
 */
#include <Arduino.h>
#include "web_rm_page.h"
#include "web_rm_util.h"
#include "web_functions.h"

/** Remote page body: the sub-header, then the static skeleton. The elements are filled by the scaffold JS.
 *  Every card uses the shared collapsablecard markup (CSS and togglecard() live in web_functions.cpp): teaser span,
 *  toggle button and exactly one body div. The class names are short and unique to this page, so no id prefix. */
void sub_page_remote()
{
    _create_meshcom_subheader("Remote");
    web_client.println("<div id=\"content_inner\">\n"
                       "<style>\n"
                       ".rmrow{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:7px 0;}\n"
                       ".rmrow input[type=text],.rmrow input[type=password]{flex:1;min-width:9em;}\n"
                       ".rmchips{display:flex;flex-wrap:wrap;gap:8px;margin:7px 0;}\n"
                       ".rmchip{flex-direction:column;align-items:flex-start;min-width:7em;text-align:left;}\n"
                       ".rmchip.rmsel{background:var(--mclightblue);}");
    web_client.println(".rmtiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(8.5em,1fr));gap:8px;margin:7px 0;}\n"
                       ".rmtile{min-height:3.4em;justify-content:center;text-align:center;}\n"
                       ".rmwarn{background:var(--mclightred);}\n"
                       ".rmarmed{background:var(--mcmidred);font-weight:bold;}\n"
                       "#rm_page button:disabled{opacity:0.45;cursor:default;}\n"
                       ".rmsw:indeterminate{background-color:#d4d4d4;}\n"
                       ".rmsw:indeterminate::before{left:.9em;}");
    web_client.println(".rmok{color:#1a7f37;font-weight:bold;}\n"
                       ".rmbad{color:var(--mcred);font-weight:bold;}\n"
                       ".rmlock{font-weight:bold;}\n"
                       "#rm_msg{min-height:1.4em;}\n"
                       ".rmtab{width:100%;border-collapse:collapse;}\n"
                       ".rmtab th,.rmtab td{padding:2px 5px;text-align:left;vertical-align:top;border-bottom:solid 1px #e0e0e0;}");
    web_client.println("#rm_msgs td:nth-child(2),#rm_msgs td:nth-child(4),#rm_log td:nth-child(3),#rm_log td:nth-child(4),#rm_log td:nth-child(5){overflow-wrap:anywhere;}\n"
                       "#rm_msgs td:nth-child(1),#rm_log td:nth-child(1),.rmtab button{white-space:nowrap;}");
    web_client.println("@media (max-width:600px){.rmtab thead{display:none;}\n"
                       ".rmtab,#rm_msgs,#rm_log{display:block;}\n"
                       "#rm_msgs tr,#rm_log tr{display:block;padding:3px 0;border-bottom:solid 1px #e0e0e0;}");
    web_client.println("#rm_msgs td,#rm_log td{display:inline-block;border:0;padding:1px 8px 1px 0;}\n"
                       "#rm_msgs td:nth-child(2),#rm_msgs td:nth-child(4),#rm_log td:nth-child(4),#rm_log td:nth-child(5){display:block;}");
    web_client.println("#rm_log td:nth-child(2)::before{content:\"from \";}\n"
                       "#rm_log td:nth-child(3)::before{content:\"ctr \";}}\n"
                       "</style>\n"
                       "<div id=\"rm_page\">\n"
                       "<p id=\"rm_auth\" class=\"rmbad\" style=\"display:none\"></p>\n"
                       "<p id=\"rm_net\" class=\"font-small\"></p>\n"
                       "<p id=\"rm_msg\" class=\"rmmsg\"></p>\n"
                       "<p id=\"rm_force\" class=\"rmmsg\"></p>\n"
                       "<p id=\"rm_chain\" class=\"font-small\"></p>");
    web_client.println("<div class=\"cardlayout collapsablecard cardopen\"><label class=\"cardlabel\">This node</label>\n"
                       "<span>Password and remote management of this node.</span>\n"
                       "<button class=\"cardtoggle\" onclick=\"togglecard(this)\"><i></i></button>\n"
                       "<div>");
    web_client.println("<div class=\"rmrow\"><input type=\"checkbox\" role=\"switch\" id=\"rm_on\" disabled><label id=\"rm_onlbl\" for=\"rm_on\">Remote management</label><span id=\"rm_pwstate\" class=\"font-small\"></span><span id=\"rm_lockstate\" class=\"font-small\"></span></div>\n"
                       "<p id=\"rm_onhint\" class=\"font-small\"></p>");
    web_client.println("<div class=\"rmrow\"><input type=\"password\" id=\"rm_selfpw\" maxlength=\"14\" autocomplete=\"new-password\" placeholder=\"New password\" aria-label=\"New password for this node\"><button type=\"button\" data-act=\"selfset\">Set</button><button type=\"button\" data-act=\"selfclear\" id=\"rm_selfclear\">Clear</button></div>\n"
                       "<p class=\"font-small\">This password also protects the net console (port 2323) and the KISS port, not only remote management.</p>\n"
                       "<p id=\"rm_selfmsg\" class=\"rmmsg\"></p>\n"
                       "</div></div>");
    web_client.println("<div class=\"cardlayout collapsablecard cardopen\"><label class=\"cardlabel\">Choose a node</label>\n"
                       "<span>Pick the node to manage.</span>\n"
                       "<button class=\"cardtoggle\" onclick=\"togglecard(this)\"><i></i></button>\n"
                       "<div>");
    web_client.println("<p class=\"font-small\">Nodes heard directly in the last 3 hours. Remote management may be switched off on any of them.</p>\n"
                       "<div id=\"rm_heard\" class=\"rmchips\"></div>\n"
                       "<p class=\"font-small\"><button type=\"button\" data-act=\"heard\">Refresh list</button></p>\n"
                       "<p class=\"font-small\">Saved on this node</p>\n"
                       "<div id=\"rm_saved\" class=\"rmchips\"></div>");
    web_client.println("<div class=\"rmrow\"><input type=\"text\" id=\"rm_call\" maxlength=\"9\" autocomplete=\"off\" autocapitalize=\"characters\" placeholder=\"Call sign, for example DK5EN-12\" aria-label=\"Call sign of the node\"></div>\n"
                       "<p id=\"rm_callchk\" class=\"font-small\"></p>");
    web_client.println("<div id=\"rm_pwrow\" class=\"rmrow\"><input type=\"password\" id=\"rm_pw\" maxlength=\"14\" autocomplete=\"new-password\" placeholder=\"Password of that node\" aria-label=\"Password of the node\"><button type=\"button\" data-act=\"remember\">Remember</button></div>\n"
                       "<p id=\"rm_savednote\" class=\"font-small\"></p>");
    web_client.println("<div class=\"rmrow\"><button type=\"button\" id=\"rm_forget\" data-act=\"forget\">Forget this node</button><button type=\"button\" id=\"rm_forgetall\" data-act=\"forgetall\">Forget all</button></div>\n"
                       "</div></div>\n");
    web_client.println("<div class=\"cardlayout collapsablecard cardopen\"><label class=\"cardlabel\">Messages</label>\n"
                       "<span>Commands sent from this page.</span>\n"
                       "<button class=\"cardtoggle\" onclick=\"togglecard(this)\"><i></i></button>\n"
                       "<div><table class=\"rmtab font-small\"><thead><tr><th>time</th><th>command</th><th>state</th><th>reply</th></tr></thead><tbody id=\"rm_msgs\"></tbody></table></div></div>\n");
    web_client.println("<div class=\"cardlayout collapsablecard cardopen\"><label class=\"cardlabel\">Actions</label>\n"
                       "<span>Status, switches, restart.</span>\n"
                       "<button class=\"cardtoggle\" onclick=\"togglecard(this)\"><i></i></button>\n"
                       "<div><div id=\"rm_info\" class=\"rmtiles\"></div><div id=\"rm_sw\"></div><p id=\"rm_swnote\" class=\"font-small\"></p></div></div>\n");
    web_client.println("<div class=\"cardlayout collapsablecard cardopen\"><label class=\"cardlabel\">Node settings</label>\n"
                       "<span>Radio, sensors, name, position, queues.</span>\n"
                       "<button class=\"cardtoggle\" onclick=\"togglecard(this)\"><i></i></button>\n"
                       "<div id=\"rm_cards\"></div></div>\n");
    web_client.println("<div id=\"rm_adv\" class=\"cardlayout collapsablecard\"><label class=\"cardlabel\">Advanced</label>\n"
                       "<span>Output pin, counters, command log.</span>\n"
                       "<button class=\"cardtoggle\" onclick=\"togglecard(this)\"><i></i></button>\n"
                       "<div>\n"
                       "<p class=\"font-small\">Switch an output pin of the node (only works if the pin is set as an output there).</p>");
    web_client.println("<div class=\"rmrow\"><select id=\"rm_pin\" aria-label=\"Output pin\"></select><label for=\"rm_pinsw\">Output</label><input type=\"checkbox\" role=\"switch\" class=\"rmsw\" id=\"rm_pinsw\" data-sw=\"pin\" disabled></div>\n"
                       "<p id=\"rm_cnt\" class=\"font-small\"></p>");
    web_client.println("<p class=\"font-small\">Commands run on this node</p>\n"
                       "<table class=\"rmtab font-small\"><thead><tr><th>ago</th><th>from</th><th>ctr</th><th>command</th><th>result</th></tr></thead><tbody id=\"rm_log\"></tbody></table>\n"
                       "</div></div>\n"
                       "</div>\n"
                       "</div>");
}

/** Scaffold JS of the Remote page: rmPageInit() / rmPageLeave() are called by loadPage(). Called once from
 *  deliver_scaffold(), inside its <script> block. All global names start with rm. */
void rmScaffoldJs()
{
    web_client.println("var rmShown=false,rmT={tick:0,arm:0,poll:0},rmSel={call:'',slot:-1},rmKnown=Object.create(null),rmSaved=[],rmHeard=[],rmLock=0,rmSrvLock=0,rmSrvMsg='',rmBusy=false,rmArmId='',rmTx={val:10,touched:false},rmAuth=false,rmOut=false,rmStat=null,rmWasLocked=false,rmRows=Object.create(null);\n"
                       "var rmCmds={status:'Status',sendpos:'Send position',sendtrack:'Send track',reboot:'Restart',sync:'Re-sync counter',radio:'Radio',sens:'Sensors',name:'Name',atxt:'APRS text',pos:'Position'};");
    web_client.println("var rmReally='Really? tap again',rmNoAns='The node did not answer the request. Check the connection and try again.',rmNote={t:'',c:''},rmSw=Object.create(null);\n"
                       "function rmCf(l,w){return 'Tap again within 4 seconds to confirm: '+l+' on '+w+'.';}");
    web_client.println("var rmTog={gps:0,track:1,display:2,mesh:3,gateway:4,led:5},rmTogName={gps:'GPS',track:'Track',display:'Display',led:'Light',mesh:'Mesh',gateway:'Gateway'};");
    web_client.println("var rmErr={limit:'Two tries to this node are still unanswered. Wait a little before another try.',busy:'Wait a few seconds before the next command.',pw:'The password is not valid: 1 to 14 plain characters, no space at the start or end.',dst:'That call sign is not valid, or it is this node.',call:'That call sign is not valid.',cmd:'That command is not allowed.',");
    web_client.println("ctr:'This node ran out of counter values.',store:'The node could not save it. Try again.',send:'The radio queue is full. Try again in a moment.',size:'The request was too large.',short:'The request was incomplete. Try again.',act:'The request was not understood.',form:'The request could not be read.',slot:'The saved node changed. Reload the page.',dup:'This node is already saved in another place.',token:'This page is out of date. Reload it and try again.'};\n");
    web_client.println("var rmForce=null;rmErr.passwd=rmErr.pw;rmErr.range='That value is outside the allowed range.';rmErr.text='The text contains characters the node will not accept.';rmErr.unknown='The node does not know that one.';rmErr.unsupported='That is not available on this node.';\n");
    web_client.println("rmErr.end='There are no more rows.';rmErr.gps='The position is controlled by GPS on that node.';rmErr.hidden='That node does not send its position, so it is not shown.';rmErr.failed='The node could not do it.';\n"
                                              "function rmNow(){return Date.now();}");
    web_client.println("function rmEl(i){return document.getElementById(i);}\n"
                       "function rmTxt(i,t,c){var e=rmEl(i);if(!e)return;e.textContent=t;if(c!==undefined)e.className=c;}\n"
                       "function rmMsg(t,c){rmNote.t=t;rmNote.c='rmmsg '+(c||'');rmRenderLock();}\n"
                       "function rmEnc(s){return encodeURIComponent(s);}\n"
                       "function rmErrText(t){return Object.prototype.hasOwnProperty.call(rmErr,t)?rmErr[t]:'The request was refused ('+t+').';}\n"
                       "function rmAgo(s){return s<120?s+' s':s<7200?Math.round(s/60)+' min':Math.round(s/3600)+' h';}");
    web_client.println("function rmUp(m){m=parseInt(m,10);if(isNaN(m))return '';return 'up '+(m<120?m+' min':Math.floor(m/60)+' h'+(m%60?' '+(m%60)+' min':''));}\n"
                       "function rmValidCall(c){return typeof c=='string'&&c.length<=9&&/^[A-Z0-9]{2,}-[0-9]{1,2}$/.test(c);}");
    web_client.println("function rmPwProblem(p){if(!p.length)return 'Enter a password.';if(p.length>14)return 'The password can be at most 14 characters.';if(!/^[\\x20-\\x7e]+$/.test(p))return 'Use only plain letters, digits and symbols (no umlauts or special characters).';if(p.charAt(0)==' ')return 'The password must not start with a space.';if(p.charAt(p.length-1)==' ')return 'The password must not end with a space.';");
    web_client.println("if(p=='none')return 'The word none is reserved: it clears the password. Choose another one.';return '';}\n"
                       "function rmKnFor(c){if(!rmKnown[c])rmKnown[c]={sw:[-1,-1,-1,-1,-1,-1],led:false,cur:null,max:null,ver:'',up:'',bat:'',at:0};return rmKnown[c];}\n"
                       "function rmKn(){return rmKnFor(rmSel.call);}\n"
                       "function rmCap(k){return(k.max!==null&&k.max>0)?k.max:15;}\n"
                       "function rmLockLeft(){var m=Math.max(rmLock,rmSrvLock)-rmNow();return m>0?Math.ceil(m/1000):0;}");
    web_client.println("function rmLocked(){return rmBusy||rmAuth||rmLockLeft()>0;}\n"
                       "function rmSlotOf(c){for(var i=0;i<rmSaved.length;i++)if(rmSaved[i]&&rmSaved[i].used&&rmSaved[i].call==c)return i;return -1;}\n");
    web_client.println("var rmTL=Object.create(null);function rmRowLocked(d){return rmBusy||rmAuth||rmLock>rmNow()||rmTL[d]>rmNow();}\n"
                       "function rmLabel(f){var p=f.split(' '),n=p[0];if(rmCmds[n])return rmCmds[n];if(rmTogName[n])return rmTogName[n]+' '+p[1];if(n=='txpower')return 'TX power '+p[1]+' dBm';if(n=='setout')return 'Pin '+p[1]+' '+p[2];return f;}");
    web_client.println("function rmParseStatus(r){if(typeof r!='string'||r.indexOf('ok v=')!==0)return null;var o={ver:'',up:'',bat:'',sw:[-1,-1,-1,-1,-1,-1],led:false,cur:null,max:null},t=r.split(' '),U='GTDMWL',i,j,k,m,s,ok;\n"
                       "for(i=1;i<t.length;i++){k=t[i];\n"
                       "if(k.indexOf('v=')==0)o.ver=k.substring(2);\n"
                       "else if(k.indexOf('up=')==0)o.up=k.substring(3);\n"
                       "else if(k.indexOf('bat=')==0)o.bat=k.substring(4);");
    web_client.println("else if(k.indexOf('s=')==0){s=k.substring(2);if(s.length!=6&&s.length!=5)return null;ok=true;m=[-1,-1,-1,-1,-1,-1];for(j=0;j<s.length;j++){if(s.charAt(j)==U.charAt(j))m[j]=1;else if(s.charAt(j)==U.charAt(j).toLowerCase())m[j]=0;else ok=false;}if(!ok)return null;for(j=0;j<6;j++)if(m[j]>=0)o.sw[j]=m[j];if(s.length==6)o.led=true;}\n"
                       "else if(k.indexOf('p=')==0){m=/^p=(-?\\d+)\\/(-?\\d+)$/.exec(k);if(m){o.cur=+m[1];o.max=+m[2];}}");
    web_client.println("else if(k=='led=0'||k=='led=1'){o.led=true;if(o.sw[5]<0)o.sw[5]=+k.charAt(4);}\n"
                       "else if(k=='gw=0'||k=='gw=1'){if(o.sw[4]<0)o.sw[4]=+k.charAt(3);}\n"
                       "else if(k=='mesh=0'||k=='mesh=1'){if(o.sw[3]<0)o.sw[3]=+k.charAt(5);}}\n"
                       "return o;}\n"
                       "function rmApplyEntry(k,e){var c=e.cmd.split(' '),n=c[0],o,m,i;");
    web_client.println("if(n=='status'){o=rmParseStatus(e.reply);if(!o)return;for(i=0;i<6;i++)if(o.sw[i]>=0)k.sw[i]=o.sw[i];if(o.led)k.led=true;if(o.cur!==null){k.cur=o.cur;k.max=o.max;}k.ver=o.ver;k.up=o.up;k.bat=o.bat;k.at=rmNow()-e.ago*1000;}\n"
                       "else if(rmTog[n]!==undefined){m=/^ok (\\w+)=(on|off)$/.exec(e.reply);if(m&&m[1]==n){k.sw[rmTog[n]]=m[2]=='on'?1:0;if(n=='led')k.led=true;}}\n");
    web_client.println("else if(n=='radio'){m=/ p=(-?\\d+)\\/(-?\\d+)$/.exec(e.reply);if(m){k.cur=+m[1];k.max=+m[2];}}\n"
                       "else if(n=='txpower'){m=/^ok txpower=(-?\\d+)$/.exec(e.reply);if(m)k.cur=+m[1];}}");
    web_client.println("function rmApplySent(list){var a=list.slice(0).sort(function(x,y){return y.ago-x.ago;}),i,e;\n"
                       "for(i=0;i<a.length;i++){e=a[i];if(e.rep&&e.ver&&e.st=='ok')rmApplyEntry(rmKnFor(String(e.dst)),e);}}\n");
    web_client.println("function rmErrOf(e){var t=(e.st=='err'&&e.ver&&typeof e.reply=='string'&&e.reply.indexOf('err ')==0)?e.reply.substring(4).split(' ')[0]:'';return t&&Object.prototype.hasOwnProperty.call(rmErr,t)?rmErr[t]:'';}\n"
                       "function rmOkText(e){var c=e.cmd.split(' '),n=c[0],o;\n"
                       "if(n=='status'){o=rmParseStatus(e.reply);return o?'Connected. Version '+o.ver+(o.up!==''?', '+rmUp(o.up):'')+(o.bat!==''?', battery '+o.bat+' %':'')+'.':e.msg;}\n"
                       "if(n=='sendpos')return 'Done. The node sent its position.';");
    web_client.println("if(n=='sendtrack')return 'Done. The node sent its track.';\n"
                       "if(n=='reboot')return 'Done. The node is restarting, it is back in about 30 seconds.';\n"
                       "if(n=='sync')return 'Connected. The counters are in step.';\n"
                       "if(rmTogName[n])return 'Done. '+rmTogName[n]+' is now '+c[1]+'.';\n"
                       "if(n=='txpower')return 'Done. TX power is now '+c[1]+' dBm.';\n"
                       "if(n=='setout')return 'Done. Pin '+c[1]+' is now '+c[2]+'.';\n"
                       "return rmOkNew(e,c,n)||e.msg||'Done.';}");
    web_client.println("function rmPost(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});}\n"
                       "function rmJson(r){if(r.status==401||r.status==403){rmAuthFail();return Promise.reject(0);}return r.json();}\n"
                       "function rmAuthFail(){rmAuth=true;clearTimeout(rmT.poll);rmT.poll=0;var e=rmEl('rm_auth');if(e){e.textContent='Your session has ended. Please log in again (reload this page).';e.style.display='';}rmRender();}");
    web_client.println("function rmCatch(e,id){if(e===0)return;if(id)rmTxt(id,rmNoAns,'rmmsg rmbad');else rmMsg(rmNoAns,'rmbad');}\n"
                       "function rmHintOff(){var e=rmEl('rm_selfmsg');if(/rmconf/.test(rmNote.c))rmMsg('','');if(e&&/rmconf/.test(e.className))rmTxt('rm_selfmsg','','rmmsg');}\n"
                       "function rmDisarm(){clearTimeout(rmT.arm);rmT.arm=0;rmArmId='';rmHintOff();rmRender();}");
    web_client.println("function rmConfirm(id,hint,mid){if(rmArmId==id){clearTimeout(rmT.arm);rmT.arm=0;rmArmId='';return true;}clearTimeout(rmT.arm);rmArmId=id;rmT.arm=setTimeout(rmDisarm,4000);rmRender();if(mid)rmTxt(mid,hint,'rmmsg rmconf');else rmMsg(hint,'rmconf');return false;}\n"
                       "function rmBtn(box,label,cls,dis,attrs){var b=document.createElement('button'),k;b.type='button';b.className=cls||'';b.disabled=!!dis;b.textContent=label;for(k in attrs)b.setAttribute(k,attrs[k]);box.appendChild(b);return b;}");
    web_client.println("function rmTile(box,label,cmd,args,cf,cls,dis){var arm=cf&&rmArmId==cf,a={'data-cmd':cmd,'data-args':args};if(cf)a['data-cf']=cf;return rmBtn(box,arm?rmReally:label,'rmtile '+(cls||'')+(arm?' rmarmed':''),dis,a);}");
    web_client.println("function rmSwSet(i,st,dis,last,shown){i.disabled=!!dis;i.indeterminate=st<0&&!(shown&&last);i.checked=st<0?last=='on':st==1;}");
    web_client.println("function rmSwRow(box,n,st,dis){var w=document.createElement('div'),l=document.createElement('label'),i=document.createElement('input'),a=rmArmId==n+' off';w.className='rmrow';i.type='checkbox';i.id='rm_sw_'+n;i.className='rmsw';i.setAttribute('role','switch');i.setAttribute('data-sw',n);");
    web_client.println("if(n=='mesh'||n=='gateway')i.setAttribute('data-cf',n+' off');l.htmlFor=i.id;l.textContent=a?rmTogName[n]+': '+rmReally:rmTogName[n];l.className=a?'rmarmed':'';rmSwSet(i,st,dis,rmSw[rmSel.call+' '+n]);w.appendChild(l);w.appendChild(i);box.appendChild(w);}");
    web_client.println("function rmRenderTiles(k,dis){var b=rmEl('rm_info'),i,d=[['status','Refresh status'],['sendpos','Send position now'],['sendtrack','Send track now']],o=['gps','track','display','led','mesh','gateway'],p=rmEl('rm_pin').value;\n"
                       "b.textContent='';for(i=0;i<d.length;i++)rmTile(b,d[i][1],d[i][0],'','','',dis);");
    web_client.println("rmBtn(b,'Re-sync counter','rmtile',dis,{'data-act':'sync',title:'Use this if the node keeps answering that the counters are out of step.'});rmTile(b,'Restart','reboot','','reboot','rmwarn',dis);\n"
                       "b=rmEl('rm_sw');b.textContent='';\n"
                       "for(i=0;i<o.length;i++){if(o[i]=='led'&&!k.led)continue;rmSwRow(b,o[i],k.sw[rmTog[o[i]]],dis);}");
    web_client.println("rmTxt('rm_swnote',k.at?'State from the last answer of '+rmSel.call+', '+rmAgo(Math.round((rmNow()-k.at)/1000))+' ago.':'State unknown until the node answers a status request. Press Refresh status.','font-small');\n"
                       "rmSwSet(rmEl('rm_pinsw'),-1,dis,rmSw[rmSel.call+' '+p],1);}\n"
                       "function rmTxBuild(p,k,dis){var cap=rmCap(k),r=document.createElement('div'),x,a;\n"
                       "if(!rmTx.touched&&k.cur!==null)rmTx.val=k.cur;if(rmTx.val>cap)rmTx.val=cap;if(rmTx.val<0)rmTx.val=0;r.className='rmrow';a=rmArmId=='txpower '+rmTx.val;");
    web_client.println("rmBtn(r,'-','',rmTx.val<=0,{'data-act':'txdn'}).id='rm_rtxdn';x=document.createElement('b');x.id='rm_rtxval';x.textContent=rmTx.val+' dBm';r.appendChild(x);rmBtn(r,'+','',rmTx.val>=cap,{'data-act':'txup'}).id='rm_rtxup';");
    web_client.println("rmBtn(r,a?rmReally:'Apply',a?'rmarmed':'',dis,{'data-act':'txapply'}).id='rm_rtxapply';p.appendChild(r);\n"
                       "x=document.createElement('div');x.id='rm_rtxnote';x.className='font-small';x.textContent=k.max!==null&&k.max>0?'':'The node has not reported its limit yet, so the highest value is 15 dBm.';p.appendChild(x);}");
    web_client.println("function rmChip(box,call,l2,l3,cls){var b=rmBtn(box,'','rmchip '+cls+(rmSel.call==call?' rmsel':''),false,{'data-act':'pick','data-call':call}),s=document.createElement('b');s.textContent=call;b.appendChild(s);\n"
                       "if(l2){s=document.createElement('span');s.className='font-small';s.textContent=l2;b.appendChild(s);}\n"
                       "if(l3){s=document.createElement('span');s.className='font-small';s.textContent=l3;b.appendChild(s);}}");
    web_client.println("function rmRenderNode(){var h=rmEl('rm_heard'),s=rmEl('rm_saved'),i,x,c=rmSel.call,t=rmEl('rm_callchk'),n=0,sv=rmSel.slot>=0;\n"
                       "h.textContent='';s.textContent='';\n"
                       "for(i=0;i<rmHeard.length;i++){x=rmHeard[i];rmChip(h,String(x.call),String(x.hw||''),rmAgo(x.age_s|0)+' ago'+(x.rssi?', '+x.rssi+' dBm':''),'');}\n"
                       "if(!rmHeard.length)h.textContent='No node heard directly yet.';\n"
                       "for(i=0;i<rmSaved.length;i++){x=rmSaved[i];if(x&&x.used){rmChip(s,String(x.call),'','','rmsaved');n++;}}");
    web_client.println("if(!n)s.textContent='Nothing saved yet.';\n"
                       "rmEl('rm_forgetall').style.display=n?'':'none';\n"
                       "if(!c)t.textContent='';\n"
                       "else if(rmValidCall(c)){t.textContent='Call sign looks right.';t.className='font-small rmok';}\n"
                       "else{t.textContent='Not a call sign. Use letters or digits, a dash and an SSID, for example DK5EN-12.';t.className='font-small rmbad';}\n"
                       "rmEl('rm_pwrow').style.display=sv?'none':'';rmEl('rm_forget').style.display=sv?'':'none';");
    web_client.println("rmTxt('rm_savednote',sv?'The password of '+c+' is saved. No need to type it.':'Type the password of the node you want to manage. Tip: press Remember to skip typing.','font-small');\n"
                       "rmEl('rm_forgetall').textContent=rmArmId=='forgetall'?rmReally:'Forget all';rmEl('rm_selfclear').textContent=rmArmId=='selfclear'?rmReally:'Clear';}\n"
                       "function rmRenderLock(){var n=rmLockLeft(),t=rmNote.t,c=rmNote.c;");
    web_client.println("if(!t){c='rmmsg rmlock';if(rmBusy)t='Sending ...';else if(!rmAuth&&n>0)t=(rmSrvMsg?rmSrvMsg+' ':'')+'Next command possible in '+n+' s.';}\n"
                       "rmTxt('rm_msg',t,c);}\n"
                       "function rmRenderMsgs(dis){var j=rmStat,b=rmEl('rm_msgs'),a,i,e,k,tr,v,x,c,cf,n,seen={},q,d=rmEl('rm_chain'),u=0,s,z;if(!j)return;d.textContent='';\n");
    web_client.println("for(i=0;i<j.targets.length;i++){q=j.targets[i];if(rmForce&&q.dst==rmForce.d&&!q.canForce){rmForce=null;rmForceBtn();}if(q.chainMsg)d.textContent=q.dst+': '+q.chainMsg;else if(q.pending)d.textContent=q.dst+': checking the connection first, your command follows.';}\n");
    web_client.println("a=j.sent.slice(0).sort(function(x,y){return x.ago-y.ago;});\n");
    web_client.println("for(i=0;i<a.length;i++){e=a[i];k=e.dst+'#'+e.ctr+'#'+e.cmd;if(seen[k])continue;seen[k]=1;tr=rmRows[k];\n"
"if(!tr){tr=rmRows[k]=document.createElement('tr');for(n=0;n<4;n++)tr.appendChild(document.createElement('td'));}\n");
    web_client.println("c=tr.children;n=e.cmd.split(' ');v=e.st=='queued'||e.st=='waiting'?'sent':e.st=='noanswer'?'no answer':e.ver&&(e.st=='ok'||e.st=='err')?'verified':'unverified';\n"
"x=e.st=='ok'&&v=='verified'?rmOkText(e):(rmErrOf(e)||e.msg||'');tr.title=rmErrOf(e)||e.msg||'';\n");
    web_client.println("c[0].textContent=rmAgo(e.ago)+' ago';c[1].textContent=e.dst+' '+rmLabel(e.cmd);c[2].textContent=v;c[2].className=v=='verified'?(e.st=='ok'?'rmok':'rmbad'):v=='sent'?'':'rmbad';c[3].textContent='';z=document.createElement('div');z.textContent=v=='sent'?'':x;c[3].appendChild(z);\n");
    web_client.println("s=rmSlotOf(e.dst);z=n[0]=='status'||n[0]=='sync'?'':'ag '+k;c[3].title=s<0?'Save the node to run commands again':'';\n"
"if(s>=0)rmBtn(c[3],z&&rmArmId==z?rmReally:'Run again','',rmRowLocked(e.dst),{'data-act':'again','data-dst':e.dst,'data-rc':n[0],'data-ra':n.slice(1).join(' '),'data-cf':z});\n");
    web_client.println("if(b.children[u]!==tr)b.insertBefore(tr,b.children[u]||null);u++;}\n");
    web_client.println("for(k in rmRows)if(!seen[k]){if(rmRows[k].parentNode)rmRows[k].parentNode.removeChild(rmRows[k]);delete rmRows[k];}}\n");
    web_client.println("var rmIn={},rmPK=['lat','lon','alt','src'],rmDefs=[{c:'radio',l:'Radio',f:[['f','Frequency',' MHz'],['sf','Spreading factor',''],['cr','Coding rate','',function(v){return '4/'+v;}],['bw','Bandwidth',' kHz'],['p','TX power (now/max)',' dBm']]},\n"
                       "{c:'sens',l:'Sensors',f:[['t','Temperature',' C'],['h','Humidity',' %'],['p','Pressure',' hPa'],['t2','Second temperature',' C']]},\n");
    web_client.println("{c:'name',l:'Name',f:[],w:[['v','Name','name',19,'n']]},{c:'atxt',l:'APRS text',f:[],w:[['v','APRS text','atxt',39,'a']]},\n");
    web_client.println("{c:'pos',l:'Position',p:rmPK,f:[['lat','Latitude',' deg'],['lon','Longitude',' deg'],['alt','Altitude',' m'],['src','Source','',function(v){var m={gps:'from GPS',nofix:'GPS on, no fix',set:'set by hand'};return Object.prototype.hasOwnProperty.call(m,v)?m[v]:v;}]],\n"
                       "w:[['lat','Latitude','lat',11],['lon','Longitude','lon',11],['alt','Altitude (m)','alt',5]]},\n");
    web_client.println("{c:'txq',l:'TX queue',k:1,f:[['q','Queued (now/capacity)',''],['bp','State','',function(v){var m={quiet:'quiet',qrs:'slow down',qrt:'hold'};return Object.prototype.hasOwnProperty.call(m,v)?m[v]:v;}],['tx','Sent',''],['rt','Retransmitted',''],['dr','Dropped',''],['u','Channel use',' %']]},\n");
    web_client.println("{c:'mbox',l:'Mailbox',k:1,u:'This node has no mailbox.',f:[['m','Mode',''],['u','Used/slots',''],['b','Bytes',''],['a','Actions last hour (done/limit)',''],['st','Stored',''],['dl','Delivered',''],['ak','Acknowledged',''],['dr','Dropped',''],['bl','Blocked',''],['nt','Notified','']]},\n"
                       "{c:'maxhop',l:'Max hop',k:1,f:[['t','Text messages',''],['p','Position beacons','']]}];\n"
                       "function rmClrIn(c){var k;for(k in rmIn)if(k.indexOf('rm_f_'+c+'_')==0)delete rmIn[k];}\n");
    web_client.println("function rmChk(k,v){var i,c,m;if(v==='')return '';\n"
                       "if(k=='lat'||k=='lon'){m=k=='lat'?90:180;return(/^-?\\d{1,3}(\\.\\d{1,6})?$/.test(v)&&Math.abs(+v)<=m)?false:(k=='lat'?'Latitude':'Longitude')+' must be -'+m+' to '+m+', decimal point, at most 6 decimals.';}\n"
                       "if(k=='alt')return(/^\\d{1,5}$/.test(v)&&+v<=40000)?false:'Altitude must be a whole number from 0 to 40000.';\n"
                       "for(i=0;i<v.length;i++){c=v.charAt(i);if(!(/[A-Za-z0-9 .+_@?()*-]/.test(c)||(k=='name'&&/[,\\/]/.test(c))))return 'Not allowed: '+c;}");
    web_client.println("if(v.charAt(0)==' '||v.charAt(v.length-1)==' ')return 'No space at the start or end.';\n"
                       "if(v.indexOf('  ')>=0)return 'No double space.';\n"
                       "if(k=='name'&&v.toLowerCase()=='none')return 'The name must not be none.';\n"
                       "return v.length>(k=='name'?19:39)?'Too long.':false;}\n");
    web_client.println("function rmWUpd(d){var s=rmEl('rm_f_'+d.c+'_set'),h='',a=[],j,w,v,r,e=false,t=rmEl('rm_f_'+d.c+'_cnt'),c=rmSel.call;\n"
                       "for(j=0;j<d.w.length;j++){w=d.w[j];v=rmEl('rm_f_'+d.c+'_'+w[0]).value;a.push(v);if(v==='')e=true;r=rmChk(w[2],v);if(r!==false){e=true;if(!h)h=r;}if(t)t.textContent=v.length+'/'+w[3];}\n");
    web_client.println("s.disabled=e||!rmValidCall(c)||rmLocked()||rmCapOf(c)<2;s.setAttribute('data-args',a.join(' '));s.setAttribute('data-cf',d.c+' '+a.join(' '));rmTxt('rm_f_'+d.c+'_hint',h,'font-small rmbad');}\n"
                       "function rmWire(x,d){x.addEventListener('input',function(){rmIn[x.id]=x.value;rmWUpd(d);});}\n");
    web_client.println("function rmKv(r,f){var o={},t,i,a;if(typeof r!='string'||r.indexOf('ok ')!==0)return null;t=r.substring(3).split(' ');\n"
                       "for(i=0;i<t.length;i++){if(!f&&/^[na]=/.test(t[i])){o[t[i].charAt(0)]=t.slice(i).join(' ').substring(2);break;}a=t[i].indexOf('=');if(a>0)o[t[i].substring(0,a)]=t[i].substring(a+1);}return o;}\n"
                       "function rmPosKv(p,r){var o={},t,i;if(typeof r!='string'||r.indexOf('ok ')!==0)return null;t=r.substring(3).split(' ');if(t.length!=p.length)return null;for(i=0;i<p.length;i++)o[p[i]]=t[i];return o;}");
    web_client.println("function rmOkNew(e,c,n){var o=rmKv(e.reply),q=rmPosKv(rmPK,e.reply),t=c.slice(1).join(' '),w=n=='name'?'name':'APRS text';\n"
                       "if(n=='radio'&&o&&o.f)return 'Radio: '+o.f+' MHz, SF '+o.sf+', '+o.bw+' kHz, TX power '+o.p+'.';\n"
                       "if(n=='sens')return 'Sensors read.';\n"
                       "if(n=='name'||n=='atxt'){o=o&&o[n=='name'?'n':'a'];if(t)return 'Done. The '+w+' is now '+(o||t)+'.';return o?'The '+w+' is '+(o=='-'?'empty':o)+'.':'';}\n");
    web_client.println("if(n=='pos'){if(t)return 'Done. Position set to '+c[1]+', '+c[2]+', '+c[3]+' m.';return q?'Position '+q.lat+', '+q.lon+', '+q.alt+' m, '+rmDefs[4].f[3][3](q.src)+'.':'';}return '';}\n");
    web_client.println("function rmWBuild(p,d,o){var j,w,x,k,t,y;if(!d.w)return;t=/^(name|atxt)$/.test(d.w[0][2]);\n"
                       "for(j=0;j<d.w.length;j++){w=d.w[j];x=document.createElement('input');x.id='rm_f_'+d.c+'_'+w[0];x.type='text';x.autocomplete='off';x.setAttribute('aria-label',w[1]);if(d.w.length>1)x.placeholder=w[1];\n"
                       "k=o&&o[w[4]||w[0]];x.value=rmIn[x.id]!==undefined?rmIn[x.id]:(k&&k!='-'?k:'');p.appendChild(x);rmWire(x,d);}\n");
    web_client.println("y=['cnt','hint','note'];for(j=t?0:1;j<(t?3:2);j++){x=document.createElement('div');x.id='rm_f_'+d.c+'_'+y[j];x.className='font-small';if(j==2)x.textContent='Stored exactly as typed.';p.appendChild(x);}\n"
                       "rmBtn(p,'Set','',true,{'data-cmd':d.c,'data-args':'','data-cf':d.c}).id='rm_f_'+d.c+'_set';rmWUpd(d);}\n");
    web_client.println("function rmLast(c,n){var a=rmStat?rmStat.sent:[],i,b=null;for(i=0;i<a.length;i++)if(a[i].dst==c&&a[i].cmd.split(' ')[0]==n&&(!b||a[i].ago<b.ago)&&a[i].st=='ok'&&a[i].ver)b=a[i];return b;}\n"
                       "function rmCapOf(c){var a=rmStat?rmStat.targets:[],i;for(i=0;i<a.length;i++)if(a[i].dst==c)return a[i].cap||0;return 0;}");
    web_client.println("var rmMh={on:0,job:'',args:'',fl:0,t:0,pre:'',rows:[],total:0,msg:'',det:null,gen:0,tm:0,ctr:-1};\nfunction rmUns(c,n,v){var a=rmStat?rmStat.sent:[],i;for(i=0;i<a.length;i++)if(a[i].dst==c&&a[i].cmd==n&&a[i].st=='err'&&a[i].reply=='err unsupported'&&(!v||a[i].ago<v.ago))return true;return false;}\n");
    web_client.println("function rmMhReset(){clearTimeout(rmMh.tm);rmMh.gen++;rmMh.on=0;rmMh.fl=0;rmMh.job='';rmMh.pre='';rmMh.rows=[];rmMh.total=0;rmMh.msg='';rmMh.det=null;}\nfunction rmNodeChg(n){if(n!=rmSel.call){rmIn={};rmMhReset();rmForce=null;rmForceBtn();}}\n");
    web_client.println("function rmMhEnd(m){clearTimeout(rmMh.tm);rmMh.gen++;rmMh.on=0;rmMh.fl=0;rmMh.job='';rmMh.pre='';rmMh.msg=m||'';rmRender();}\nfunction rmMhStop(){rmMhEnd('Stopped.');}\nfunction rmMhGo(job,args){var pr=rmPre();if(rmMh.on)return;if(pr){rmMsg(pr,'rmbad');return;}\nif(!rmMh.pre){rmMh.pre=rmSel.slot>=0?'slot='+rmSel.slot+'&call='+rmEnc(rmSel.call):'dst='+rmEnc(rmSel.call)+'&pw='+rmEnc(rmEl('rm_pw').value);rmEl('rm_pw').value='';}\n");
    web_client.println("rmMh.on=1;rmMh.job=job;rmMh.args=args;rmMh.msg='';rmMh.det=null;if(job=='list'){rmMh.rows=[];rmMh.total=0;}rmMhStep();rmRender();}\nfunction rmMhStart(){rmMhGo('list','0');}\nfunction rmMhDet(c){c=String(c).trim().toUpperCase();if(rmMh.on)return;if(!rmValidCall(c)){rmMsg('Enter a node call such as DK5EN-12.','rmbad');return;}rmMhGo('det',c);}\n");
    web_client.println("function rmMhStep(){var w,e;if(!rmMh.on)return;if(rmMh.fl){e=rmMhFind();if(e)rmMhGot(e);else if(rmNow()-rmMh.t>120000)rmMhEnd('There was no answer from the node.');return;}\n");
    web_client.println("w=rmTL[rmSel.call]-rmNow();if(w>0){clearTimeout(rmMh.tm);rmMh.tm=setTimeout(rmMhStep,Math.min(w+30,15000));return;}rmMhSend();}\n");
    // Driver match: the sent[] entry must carry the ctr the send answered with. A send answered viaSync gets its
    // ctr only later, so rmMh.ctr stays -1 there and rmMhFind falls back to the time-window match (cmd, dst, ago).
    web_client.println("function rmMhSend(){var g=rmMh.gen;rmMh.fl=1;rmMh.ctr=-1;rmMh.t=rmNow();rmOut=true;\nrmPost('/rmsend',rmMh.pre+'&cmd=mh&args='+rmEnc(rmMh.args)).then(rmJson).then(function(j){if(g!=rmMh.gen||!rmMh.on)return;\nif(j&&j.ok){rmMh.ctr=(j.viaSync||typeof j.ctr!='number')?-1:j.ctr;rmPoll();return;}rmMh.fl=0;if(j&&j.err=='busy'&&j.retry>0){rmTL[rmSel.call]=rmNow()+j.retry*1000;rmMhStep();return;}\n");
    web_client.println("rmMhEnd(rmErrText(j&&j.err?String(j.err):'send'));}).catch(function(e){if(g==rmMh.gen&&e!==0)rmMhEnd(rmNoAns);});}\nfunction rmMhFind(){var a=rmStat?rmStat.sent:[],i,b=null,e,x=rmNow()-rmMh.t+2000;for(i=0;i<a.length;i++){e=a[i];if(e.dst==rmSel.call&&e.cmd=='mh '+rmMh.args&&(rmMh.ctr<0||e.ctr==rmMh.ctr)&&e.ago*1000<=x&&(!b||e.ago<b.ago)&&(e.st=='ok'||e.st=='err'||e.st=='noanswer'))b=e;}return b;}\n");
    web_client.println("function rmMhL(v,l,u,f){return l+': '+((v===undefined||v=='-')?'not present':f?f(v):v+u);}\nfunction rmYn(v){return v=='1'?'yes':'no';}\nfunction rmMhGot(e){var r=e.reply,t,i,o={},k,n,d,x;rmMh.fl=0;\nif(e.st=='noanswer'||!e.ver||typeof r!='string')return rmMhEnd('There was no answer from the node.');\n");
    web_client.println("if(r.indexOf('err ')==0){k=r.substring(4).split(' ')[0];if(k=='end'&&rmMh.job=='list')return rmMhEnd('End of the list.');return rmMhEnd(k=='unknown'?'That node is not known there.':rmErrText(k));}\nt=r.split(' ');if(t[0]!='ok')return rmMhEnd('The reply could not be read.');\nif(rmMh.job=='det'){for(i=2;i<t.length;i++){k=t[i].indexOf('=');if(k>0)o[t[i].substring(0,k)]=t[i].substring(k+1);}\n");
    web_client.println("d=t[1]=='d'?[rmMhL(o.g,'Gateway','',rmYn),rmMhL(o.m,'Mesh','',rmYn),rmMhL(o.r,'RSSI',' dBm'),rmMhL(o.s,'SNR',' dB'),rmMhL((o.la=='-'||o.lo=='-')?'-':o.la+', '+o.lo,'Position',''),rmMhL(o.di,'Distance',' km'),rmMhL(o.a,'Altitude',' m'),rmMhL(o.n,'Its neighbours',''),rmMhL(o.x,'Only it hears',''),rmMhL(o.h,'It hears',''),rmMhL(o.t,'Heard',' min ago')]:\n");
    web_client.println("[rmMhL(o.h,'Hops',''),rmMhL(o.k,'Routes',''),rmMhL(o.g,'Via gateway','',rmYn),'Relay: '+((o.rc===undefined||o.rc=='-')?'not known':o.rc),rmMhL(o.t,'Age',' min'),rmMhL(o.v,'Via','',function(v){return v.split(',').join(', ');})];\nrmMh.det={c:rmMh.args,l:d};return rmMhEnd('');}\nn=rmMh.rows.length;rmMh.total=parseInt(t[1],10)||0;for(i=3;i+1<t.length&&rmMh.rows.length<128;i+=2)rmMh.rows.push({c:t[i],m:t[i+1]});\n");
    web_client.println("if(rmMh.rows.length==n||!/^[0-9]+$/.test(t[2]||'-')||rmMh.rows.length>=128)return rmMhEnd('End of the list.');\nrmMh.args=t[2];rmMhStep();rmRender();}\nfunction rmMhCard(b,dis){var p=document.createElement('div'),x,tb,tr,i,r;p.id='rm_card_mh';b.appendChild(p);x=document.createElement('strong');x.textContent='Heard list';p.appendChild(x);\nrmBtn(p,'Read','',dis||rmMh.on,{'data-act':'mhgo'});rmBtn(p,'Stop','',!rmMh.on,{'data-act':'mhstop'});\n");
    web_client.println("x=document.createElement('div');x.id='rm_mh_prog';x.textContent=(rmMh.total||rmMh.rows.length?'Read '+rmMh.rows.length+' of '+rmMh.total+'. ':'')+rmMh.msg;p.appendChild(x);\n");
    web_client.println("tb=document.createElement('table');tb.id='rm_mh_tab';for(i=0;i<rmMh.rows.length;i++){r=rmMh.rows[i];tr=document.createElement('tr');rmCell(tr,r.c);rmCell(tr,r.m+' min ago');x=document.createElement('td');rmBtn(x,'Details','',dis||rmMh.on,{'data-act':'mhdet','data-call':r.c});tr.appendChild(x);tb.appendChild(tr);}p.appendChild(tb);\n");
    web_client.println("x=document.createElement('input');x.id='rm_f_mh_other';x.maxLength=9;x.placeholder='Other node';x.value=rmIn[x.id]||'';x.addEventListener('input',function(){rmIn[this.id]=this.value;});p.appendChild(x);rmBtn(p,'Look up','',dis||rmMh.on,{'data-act':'mhother'});\n");
    web_client.println("x=document.createElement('div');x.id='rm_mh_det';p.appendChild(x);if(rmMh.det){r=document.createElement('strong');r.textContent=rmMh.det.c;x.appendChild(r);for(i=0;i<rmMh.det.l.length;i++){r=document.createElement('div');r.textContent=rmMh.det.l[i];x.appendChild(r);}}}\n");
    web_client.println("function rmRenderCards(){var b=rmEl('rm_cards'),c=rmSel.call,cap=rmCapOf(c),dis=!rmValidCall(c)||rmLocked(),d,i,j,p,v,o,x,fa=document.activeElement,fid='';if(!b)return;\n"
                       "if(fa&&fa.tagName=='INPUT'&&/^rm_f_/.test(fa.id)){rmIn[fa.id]=fa.value;fid=fa.id;}b.textContent='';\n");
    web_client.println("if(cap<2){p=document.createElement('p');p.className='font-small rmcapnote';p.textContent=rmLast(c,'sync')?'This node runs older firmware: only the basic commands work.':'This node has not reported support for these commands yet. Press Re-sync counter under Actions.';b.appendChild(p);}\n");
    web_client.println("for(i=0;i<rmDefs.length;i++){d=rmDefs[i];p=document.createElement('div');p.id='rm_card_'+d.c;b.appendChild(p);x=document.createElement('strong');x.textContent=d.l;p.appendChild(x);\n"
                       "rmBtn(p,'Read','',dis||cap<2,{'data-cmd':d.c,'data-args':''});v=rmLast(c,d.c);o=v?(d.p?rmPosKv(d.p,v.reply):d.k?rmKv(v.reply,1):rmKv(v.reply)):null;\n");
    web_client.println("for(j=0;o&&j<d.f.length;j++){x=document.createElement('div');x.id='rm_f_'+d.c+'_'+d.f[j][0];x.textContent=d.f[j][1]+': '+((o[d.f[j][0]]===undefined||o[d.f[j][0]]=='-')?'not present':(d.f[j][3]?d.f[j][3](o[d.f[j][0]]):o[d.f[j][0]])+d.f[j][2]);p.appendChild(x);}rmWBuild(p,d,o);if(d.c=='radio')rmTxBuild(p,rmKn(),dis);");
    web_client.println("if(d.u&&rmUns(c,d.c,v)){x=document.createElement('div');x.textContent=d.u;p.appendChild(x);}}rmMhCard(b,dis||cap<2);if(fid&&rmEl(fid))rmEl(fid).focus();}");
    web_client.println("function rmCell(tr,t){var d=document.createElement('td');d.textContent=String(t);tr.appendChild(d);}\n"
"function rmRenderLog(){var j=rmStat,b,i,r,tr;if(!j)return;\n");
    web_client.println("b=rmEl('rm_log');b.textContent='';for(i=0;i<j.log.length;i++){r=j.log[i];tr=document.createElement('tr');rmCell(tr,rmAgo(r.ago));rmCell(tr,r.src);rmCell(tr,r.ctr);rmCell(tr,r.cmd);rmCell(tr,r.res);b.appendChild(tr);}\n");
    web_client.println("rmTxt('rm_cnt','Executed '+j.ok+', rejected '+j.rej+', counter high-water '+j.hwm+'.');}\n"
                       "function rmRenderSelf(){var j=rmStat,o=rmEl('rm_on');if(!j)return;o.checked=!!j.on;o.disabled=!j.pw&&!j.on;o.style.display=rmEl('rm_onlbl').style.display=j.pw||j.on?'':'none';\n"
                       "rmTxt('rm_pwstate','password: '+(j.pw?'set':'not set'),'font-small');");
    web_client.println("rmTxt('rm_lockstate',j.lock?'A sender is blocked for '+(j.lockS||1)+' s after wrong attempts':'','font-small');\n"
                       "rmTxt('rm_onhint',j.pw?'':'Set a password first, then switch remote management on.','font-small');}\n"
                       "function rmRender(){if(!rmShown||!rmEl('rm_page'))return;var k=rmKn(),dis=!rmValidCall(rmSel.call)||rmLocked();\n"
                       "rmRenderNode();rmRenderTiles(k,dis);rmRenderLock();rmRenderMsgs();rmRenderCards();rmRenderLog();rmRenderSelf();");
    web_client.println("}\n"
"function rmPre(){var c=rmSel.call,p;if(!rmValidCall(c))return 'Choose a node first.';if(rmSel.slot<0){p=rmPwProblem(rmEl('rm_pw').value);if(p)return 'Type the password of '+c+' first. '+p;}return '';}");
    web_client.println("function rmSendCmd(cmd,args,dc,sl,fc){var call=dc||rmSel.call,ss=dc?sl:rmSel.slot,body,pw,pr=dc?'':rmPre(),full=args?cmd+' '+args:cmd;\n"
                       "if(!fc&&(dc?rmRowLocked(dc):rmLocked()))return;rmForce=null;rmTxt('rm_force','');\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n");
    web_client.println("body='cmd='+rmEnc(cmd)+'&args='+rmEnc(args);\n"
                       "if(ss>=0)body='slot='+ss+'&'+body;\n"
                       "else{pw=rmEl('rm_pw').value;body='dst='+rmEnc(call)+'&pw='+rmEnc(pw)+'&'+body;rmEl('rm_pw').value='';pw='';}\n"
                       "if(dc)body+='&call='+rmEnc(dc);if(!dc&&ss>=0)body+='&call='+rmEnc(call);if(fc)body+='&force=1';rmBusy=true;rmMsg('','');rmRender();\n"
                       "rmPost('/rmsend',body).then(rmJson).then(function(j){");
    web_client.println("if(j&&j.ok){rmLock=rmNow()+10000;rmOut=true;rmMsg('','');rmPoll();}\n");
    web_client.println("else{pr=j&&j.err?String(j.err):'send';rmMsg(rmErrText(pr),'rmbad');if(pr=='busy')rmLock=rmNow()+10000;\n"
                       "\n");
    web_client.println("if(pr=='limit'&&j.canForce&&!fc&&ss>=0&&call==rmSel.call){rmForce={c:cmd,a:args,d:call,s:ss};rmForceBtn();}}\n"
                       "}).catch(rmCatch).then(function(){rmBusy=false;rmRender();});}\n"
                       "function rmForceBtn(){var b=rmEl('rm_force');if(!b)return;b.textContent='';if(rmForce&&rmForce.d==rmSel.call)rmBtn(b,'Try once more','',false,{'data-act':'force'});}\n"
                       "function rmTap(b){var cmd=b.getAttribute('data-cmd'),args=b.getAttribute('data-args')||'',cf=b.getAttribute('data-cf')||'',pr=rmPre();if(!args)rmClrIn(cmd);");
    web_client.println("if(rmLocked())return;\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n"
                       "if(cf&&!rmConfirm(cf,rmCf(rmLabel((cmd+' '+args).trim()),rmSel.call)))return;\n"
                       "rmSendCmd(cmd,args);}\n");
    web_client.println("function rmAgain(b){var d=b.getAttribute('data-dst'),cmd=b.getAttribute('data-rc'),args=b.getAttribute('data-ra')||'',cf=b.getAttribute('data-cf')||'',s=rmSlotOf(d);\n"
                       "if(s<0||rmRowLocked(d))return;\n"
                       "if(cf&&!rmConfirm(cf,rmCf(rmLabel((cmd+' '+args).trim()),d)))return;\n"
                       "rmSendCmd(cmd,args,d,s);}\n");
    web_client.println("function rmTxApply(){var k=rmKn(),v=Math.min(rmTx.val,rmCap(k)),cf,pr=rmPre();if(rmLocked())return;\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n"
                       "cf=(k.cur===null||v<k.cur)?'txpower '+v:'';\n"
                       "if(cf&&!rmConfirm(cf,'Tap Apply again within 4 seconds to set the TX power of '+rmSel.call+' to '+v+' dBm.'))return;");
    web_client.println("rmSendCmd('txpower',String(v));}\n"
                       "function rmPick(c){rmNodeChg(c);rmDisarmQuiet();rmSel.call=c;rmSel.slot=rmSlotOf(c);rmTx.touched=false;rmEl('rm_call').value=c;rmRender();}\n"
                       "function rmDisarmQuiet(){clearTimeout(rmT.arm);rmT.arm=0;rmArmId='';}\n"
                       "function rmCallInput(){var c=rmEl('rm_call');c.value=c.value.toUpperCase();rmDisarmQuiet();rmNodeChg(c.value.trim());rmSel.call=c.value.trim();rmSel.slot=rmSlotOf(rmSel.call);rmTx.touched=false;rmRender();}");
    web_client.println("function rmLoadNodes(){fetch('/rmnodes').then(rmJson).then(function(j){rmSaved=(j&&j.nodes)||[];rmSel.slot=rmSlotOf(rmSel.call);rmRender();}).catch(function(e){rmCatch(e,'rm_net');});}\n"
                       "function rmLoadHeard(){fetch('/rmheard').then(rmJson).then(function(j){rmHeard=(j&&j.heard)||[];rmRender();}).catch(function(e){rmCatch(e,'rm_net');});}");
    web_client.println("function rmNodesPost(body,okText){rmPost('/rmnodes',body).then(rmJson).then(function(j){if(j&&j.ok){rmMsg(okText,'rmok');rmLoadNodes();}else rmMsg(rmErrText(j&&j.err?String(j.err):'form'),'rmbad');}).catch(rmCatch);}\n"
                       "function rmRemember(){var c=rmSel.call,i=rmEl('rm_pw'),pw=i.value,pr=rmPwProblem(pw),s=-1,n;\n"
                       "if(!rmValidCall(c)){rmMsg('Enter a valid call sign first.','rmbad');return;}\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n"
                       "for(n=0;n<3;n++)if(!rmSaved[n]||!rmSaved[n].used){s=n;break;}");
    web_client.println("if(s<0){rmMsg('All 3 places on this node are used. Forget one first.','rmbad');return;}\n"
                       "i.value='';rmNodesPost('act=save&slot='+s+'&call='+rmEnc(c)+'&pw='+rmEnc(pw),c+' is saved.');pw='';}\n"
                       "function rmForget(){if(rmSel.slot<0)return;rmNodesPost('act=del&slot='+rmSel.slot,rmSel.call+' is forgotten.');}");
    web_client.println("function rmForgetAll(){if(!rmConfirm('forgetall','Tap Forget all again within 4 seconds to remove every saved node.'))return;rmNodesPost('act=forget','All saved nodes are forgotten.');}\n"
                       "function rmSelfSet(){var i=rmEl('rm_selfpw'),pw=i.value,pr=rmPwProblem(pw),b;\n"
                       "if(pr){rmTxt('rm_selfmsg',pr,'rmmsg rmbad');return;}\n"
                       "b='act=set&pw='+rmEnc(pw);i.value='';pw='';");
    web_client.println("rmPost('/rmpasswd',b).then(rmJson).then(function(j){if(j&&j.ok){rmTxt('rm_selfmsg','Password set.','rmmsg rmok');rmPoll();}else rmTxt('rm_selfmsg',rmErrText(j&&j.err?String(j.err):'form'),'rmmsg rmbad');}).catch(function(e){rmCatch(e,'rm_selfmsg');});}\n"
                       "function rmSelfClear(){if(!rmConfirm('selfclear','Clear the password? Remote management will be switched off.','rm_selfmsg'))return;");
    web_client.println("rmPost('/rmpasswd','act=clear').then(rmJson).then(function(j){if(j&&j.ok){rmTxt('rm_selfmsg','Password cleared. Remote management is off.','rmmsg rmok');rmPoll();}else rmTxt('rm_selfmsg',rmErrText(j&&j.err?String(j.err):'form'),'rmmsg rmbad');}).catch(function(e){rmCatch(e,'rm_selfmsg');});}\n"
                       "function rmSelfOn(){var o=rmEl('rm_on'),v=o.checked?'on':'off';o.disabled=true;");
    web_client.println("fetch('/setparam/?rm='+v).then(rmJson).then(function(j){rmTxt('rm_selfmsg',j&&j.returncode==0?'Remote management is '+v+'.':'Could not change it. Is a password set?','rmmsg');rmPoll();}).catch(function(e){rmCatch(e,'rm_selfmsg');});}");
    web_client.println("function rmSwChg(i){var n=i.getAttribute('data-sw'),v=i.checked?'on':'off',cf=v=='off'?(i.getAttribute('data-cf')||''):'',pr=rmPre(),pin=n=='pin',c=pin?'setout':n,a=pin?rmEl('rm_pin').value+' '+v:v;");
    web_client.println("if(rmLocked()||pr){if(pr&&!rmLocked())rmMsg(pr,'rmbad');rmRender();return;}\n"
                       "if(cf&&!rmConfirm(cf,rmCf(rmLabel(c+' '+a),rmSel.call)))return;\n"
                       "rmSw[rmSel.call+' '+(pin?rmEl('rm_pin').value:n)]=v;rmSendCmd(c,a);}\n"
                       "function rmChange(e){var t=e.target;if(t.id=='rm_pin')rmRender();else if(t.getAttribute&&t.getAttribute('data-sw'))rmSwChg(t);}\n"
                       "function rmClick(e){var b=e.target;while(b&&b.tagName!='BUTTON'){if(b.id=='rm_page')return;b=b.parentNode;}\n"
                       "if(!b||b.disabled)return;var a=b.getAttribute('data-act');");
    web_client.println("if(b.hasAttribute('data-cmd'))rmTap(b);\n"
                       "else if(a=='pick')rmPick(b.getAttribute('data-call'));else if(a=='mhgo')rmMhStart();");
    web_client.println("else if(a=='mhstop')rmMhStop();else if(a=='mhdet')rmMhDet(b.getAttribute('data-call'));else if(a=='mhother')rmMhDet(rmEl('rm_f_mh_other').value);\n"
                                              "else if(a=='sync')rmSendCmd('sync','');\n"
                                              "else if(a=='again')rmAgain(b);\n"
                       "else if(a=='force'&&rmForce&&rmForce.d==rmSel.call)rmSendCmd(rmForce.c,rmForce.a,rmForce.d,rmForce.s,true);\n");
    web_client.println("else if(a=='txdn'){rmTx.val--;rmTx.touched=true;rmDisarmQuiet();rmRender();}\n"
                       "else if(a=='txup'){rmTx.val++;rmTx.touched=true;rmDisarmQuiet();rmRender();}\n"
                       "else if(a=='txapply')rmTxApply();\n"
                       "else if(a=='remember')rmRemember();\n"
                       "else if(a=='forget')rmForget();");
    web_client.println("else if(a=='forgetall')rmForgetAll();\n"
                       "else if(a=='selfset')rmSelfSet();\n"
                       "else if(a=='selfclear')rmSelfClear();\n"
                       "else if(a=='heard')rmLoadHeard();}\n"
                       "function rmGot(j){var i,t,c=rmSel.call;j.sent=j.sent||[];j.log=j.log||[];j.targets=j.targets||[];rmStat=j;rmApplySent(j.sent);\n"
                       "rmOut=false;rmSrvLock=0;rmSrvMsg='';rmTL=Object.create(null);\n"
                       "for(i=0;i<j.sent.length;i++)if(j.sent[i].st=='queued'||j.sent[i].st=='waiting')rmOut=true;");
    web_client.println("for(i=0;i<j.targets.length;i++){t=j.targets[i];if(t.pending)rmOut=true;rmTL[t.dst]=t.locked?1e15:t.retry>0?rmNow()+t.retry*1000:0;if(t.dst==c){if(t.retry>0)rmSrvLock=rmNow()+t.retry*1000;if(t.locked)rmSrvMsg='Two tries to '+c+' are still unanswered.';}}\n"
                       "rmMhStep();rmRender();}\n"
                       "function rmNext(){clearTimeout(rmT.poll);rmT.poll=0;if(rmShown&&!rmAuth)rmT.poll=setTimeout(rmPoll,rmOut?3000:10000);}\n"
                       "function rmPoll(){if(!rmShown)return;clearTimeout(rmT.poll);rmT.poll=0;");
    web_client.println("fetch('/rmstatus').then(rmJson).then(function(j){if(!rmShown)return;rmAuth=false;rmTxt('rm_auth','');rmEl('rm_auth').style.display='none';rmTxt('rm_net','');rmGot(j);rmNext();})\n"
                       ".catch(function(e){if(e===0||!rmShown)return;rmTxt('rm_net','No answer from the node, trying again.','rmbad');rmNext();});}\n"
                       "function rmTick(){if((typeof cpage!='undefined'&&cpage!='remote')||!rmEl('rm_page')){rmPageLeave();return;}\n"
                       "var l=rmLocked();if(l||rmWasLocked)rmRender();rmWasLocked=l;}");
    web_client.println("function rmPageLeave(){clearInterval(rmT.tick);clearTimeout(rmT.poll);clearTimeout(rmT.arm);rmT.tick=0;rmT.poll=0;rmT.arm=0;rmShown=false;rmArmId='';rmMhReset();}\n"
                       "function rmPageInit(){var i,p,o;rmPageLeave();if(!rmEl('rm_page'))return;rmShown=true;rmRows=Object.create(null);rmAuth=false;rmBusy=false;rmArmId='';\n"
                       "p=rmEl('rm_pin');for(i=0;i<16;i++){o=document.createElement('option');o.value=(i<8?'a':'b')+(i%8);o.textContent=o.value;p.appendChild(o);}");
    web_client.println("rmEl('rm_page').onclick=rmClick;rmEl('rm_page').onchange=rmChange;rmEl('rm_call').oninput=rmCallInput;rmEl('rm_on').onchange=rmSelfOn;rmEl('rm_call').value=rmSel.call;\n"
                       "rmT.tick=setInterval(rmTick,1000);rmRender();rmPoll();rmLoadNodes();rmLoadHeard();}");
}
