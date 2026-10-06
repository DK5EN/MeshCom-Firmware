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

/** Remote page body: the sub-header, then the static skeleton. The elements are filled by the scaffold JS. */
void sub_page_remote()
{
    _create_meshcom_subheader("Remote");
    web_client.println("<div id=\"content_inner\">\n"
                       "<style>\n"
                       "#rm_page .rmrow{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:7px 0;}\n"
                       "#rm_page .rmrow input[type=text],#rm_page .rmrow input[type=password]{flex:1;min-width:9em;}\n"
                       "#rm_page .rmchips{display:flex;flex-wrap:wrap;gap:8px;margin:7px 0;}\n"
                       "#rm_page .rmchip{flex-direction:column;align-items:flex-start;min-width:7em;text-align:left;}\n"
                       "#rm_page .rmchip.rmsel{background:var(--mclightblue);}");
    web_client.println("#rm_page .rmtiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(8.5em,1fr));gap:8px;margin:7px 0;}\n"
                       "#rm_page .rmtile{min-height:3.4em;justify-content:center;text-align:center;}\n"
                       "#rm_page div.rmtile{display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;border:dashed 1px var(--mcgray);border-radius:5px;padding:4px;}\n"
                       "#rm_page .rmsmall{min-height:0;padding:1px 10px;}\n"
                       "#rm_page .rmon{background:var(--mclightgreen);}\n"
                       "#rm_page .rmoff{background:#e6e6e6;}");
    web_client.println("#rm_page .rmwarn{background:var(--mclightred);}\n"
                       "#rm_page .rmarmed{background:var(--mcmidred);font-weight:bold;}\n"
                       "#rm_page button:disabled{opacity:0.45;cursor:default;}\n"
                       "#rm_page .rmok{color:#1a7f37;font-weight:bold;}\n"
                       "#rm_page .rmbad{color:var(--mcred);font-weight:bold;}\n"
                       "#rm_page .rmlock{font-weight:bold;min-height:1.4em;}\n"
                       "#rm_page .rmact{padding:3px 0;border-bottom:solid 1px #e0e0e0;}\n"
                       "#rm_page .rmtab{width:100%;border-collapse:collapse;}");
    web_client.println("#rm_page .rmtab th,#rm_page .rmtab td{padding:2px 5px;text-align:left;vertical-align:top;border-bottom:solid 1px #e0e0e0;font-size:x-small;word-break:break-word;}\n"
                       "</style>\n"
                       "<div id=\"rm_page\">\n"
                       "<p id=\"rm_auth\" class=\"rmbad\" style=\"display:none\"></p>\n"
                       "<p id=\"rm_net\" class=\"font-small\"></p>\n"
                       "<div class=\"cardlayout\">\n"
                       "<label class=\"cardlabel\">This node</label>");
    web_client.println("<div class=\"rmrow\"><input type=\"checkbox\" role=\"switch\" id=\"rm_on\" disabled><label id=\"rm_onlbl\" for=\"rm_on\">Remote management</label><span id=\"rm_pwstate\" class=\"font-small\"></span><span id=\"rm_lockstate\" class=\"font-small\"></span></div>\n"
                       "<p id=\"rm_onhint\" class=\"font-small\"></p>");
    web_client.println("<div class=\"rmrow\"><input type=\"password\" id=\"rm_selfpw\" maxlength=\"14\" autocomplete=\"new-password\" placeholder=\"New password\" aria-label=\"New password for this node\"><button type=\"button\" data-act=\"selfset\">Set</button><button type=\"button\" data-act=\"selfclear\" id=\"rm_selfclear\">Clear</button></div>\n"
                       "<p class=\"font-small\">This password also protects the net console (port 2323) and the KISS port, not only remote management.</p>\n"
                       "<p id=\"rm_selfmsg\" class=\"rmmsg\"></p>\n"
                       "</div>");
    web_client.println("<div class=\"cardlayout\">\n"
                       "<label class=\"cardlabel\">Choose a node</label>\n"
                       "<p class=\"font-small\">Nodes heard directly in the last 3 hours. Remote management may be switched off on any of them.</p>\n"
                       "<div id=\"rm_heard\" class=\"rmchips\"></div>\n"
                       "<p class=\"font-small\"><button type=\"button\" data-act=\"heard\">Refresh list</button></p>\n"
                       "<p class=\"font-small\">Saved on this node</p>\n"
                       "<div id=\"rm_saved\" class=\"rmchips\"></div>");
    web_client.println("<div class=\"rmrow\"><input type=\"text\" id=\"rm_call\" maxlength=\"9\" autocomplete=\"off\" autocapitalize=\"characters\" placeholder=\"Call sign, for example DK5EN-12\" aria-label=\"Call sign of the node\"></div>\n"
                       "<p id=\"rm_callchk\" class=\"font-small\"></p>");
    web_client.println("<div id=\"rm_pwrow\" class=\"rmrow\"><input type=\"password\" id=\"rm_pw\" maxlength=\"14\" autocomplete=\"new-password\" placeholder=\"Password of that node\" aria-label=\"Password of the node\"><button type=\"button\" data-act=\"remember\">Remember on this node</button></div>\n"
                       "<p id=\"rm_savednote\" class=\"font-small\"></p>");
    web_client.println("<div class=\"rmrow\"><button type=\"button\" id=\"rm_test\" data-act=\"test\">Test connection</button><button type=\"button\" id=\"rm_forget\" data-act=\"forget\">Forget this node</button><button type=\"button\" id=\"rm_forgetall\" data-act=\"forgetall\">Forget all</button></div>\n"
                       "</div>\n");
    web_client.println("<div class=\"cardlayout\"><label class=\"cardlabel\">Messages</label>\n"
"<p id=\"rm_msg\" class=\"rmmsg\"></p>\n"
"<p id=\"rm_lockline\" class=\"rmlock\"></p>\n"
"<p id=\"rm_chain\" class=\"font-small\"></p>\n");
    web_client.println("<table class=\"rmtab\"><thead><tr><th>time</th><th>command</th><th>state</th><th>reply</th><th></th></tr></thead><tbody id=\"rm_msgs\"></tbody></table></div>\n");
    web_client.println("<div class=\"cardlayout\"><label class=\"cardlabel\">Info</label><div id=\"rm_info\" class=\"rmtiles\"></div></div>");
    web_client.println("<div class=\"cardlayout\"><label class=\"cardlabel\">Switches</label><div id=\"rm_sw\" class=\"rmtiles\"></div><p id=\"rm_swnote\" class=\"font-small\"></p></div>\n");
    web_client.println("<div class=\"cardlayout\"><label class=\"cardlabel\">Radio</label>\n");
    web_client.println("<div id=\"rm_radio\">\n"
                       "<div class=\"rmrow\"><button type=\"button\" id=\"rm_rtxdn\" data-act=\"txdn\">-</button><b id=\"rm_rtxval\">10 dBm</b><button type=\"button\" id=\"rm_rtxup\" data-act=\"txup\">+</button><button type=\"button\" id=\"rm_rtxapply\" data-act=\"txapply\">Apply</button></div>");
    web_client.println("<p id=\"rm_rtxnote\" class=\"font-small\"></p></div></div>\n"
                       "<div class=\"cardlayout\"><label class=\"cardlabel\">Restart</label><div id=\"rm_rs\" class=\"rmtiles\"></div></div>\n"
                       "<details id=\"rm_adv\" open class=\"cardlayout\"><summary class=\"font-bold\">Advanced</summary>\n"
                       "<p class=\"font-small\">Switch an output pin of the node (only works if the pin is set as an output there).</p>");
    web_client.println("<div class=\"rmrow\"><select id=\"rm_pin\" aria-label=\"Output pin\"></select><button type=\"button\" id=\"rm_pinon\" data-act=\"pinon\">Pin on</button><button type=\"button\" id=\"rm_pinoff\" data-act=\"pinoff\">Pin off</button></div>\n");
    web_client.println("<p class=\"font-small\">Re-sync counter: use this if the node keeps answering that the counters are out of step.</p>\n"
                       "<div class=\"rmrow\"><button type=\"button\" id=\"rm_sync\" data-act=\"sync\">Re-sync counter</button></div>\n"
                       "<p id=\"rm_cnt\" class=\"font-small\"></p>");
    web_client.println("<p class=\"font-small\">Commands run on this node</p>\n"
                       "<table class=\"rmtab\"><thead><tr><th>ago</th><th>from</th><th>ctr</th><th>command</th><th>result</th></tr></thead><tbody id=\"rm_log\"></tbody></table>\n"
                       "</details>\n"
                       "</div>\n"
                       "</div>");
}

/** Scaffold JS of the Remote page: rmPageInit() / rmPageLeave() are called by loadPage(). Called once from
 *  deliver_scaffold(), inside its <script> block. All global names start with rm. */
void rmScaffoldJs()
{
    web_client.println("var rmShown=false,rmT={tick:0,arm:0,poll:0},rmSel={call:'',slot:-1},rmKnown=Object.create(null),rmSaved=[],rmHeard=[],rmLock=0,rmSrvLock=0,rmSrvMsg='',rmBusy=false,rmArmId='',rmTx={val:10,touched:false},rmAuth=false,rmOut=false,rmStat=null,rmWasLocked=false,rmRows=Object.create(null);\n"
                       "var rmCmds={status:'Status',sendpos:'Send position',sendtrack:'Send track',reboot:'Restart',sync:'Re-sync counter'};");
    web_client.println("var rmTog={gps:0,track:1,display:2,mesh:3,gateway:4,led:5},rmTogName={gps:'GPS',track:'Track',display:'Display',led:'Light',mesh:'Mesh',gateway:'Gateway'};");
    web_client.println("var rmErr={limit:'Two tries to this node are still unanswered. Wait a little before another try.',busy:'Wait a few seconds before the next command.',passwd:'The password is not valid: 1 to 14 plain characters, no space at the start or end.',pw:'The password is not valid: 1 to 14 plain characters, no space at the start or end.',dst:'That call sign is not valid, or it is this node.',call:'That call sign is not valid.',cmd:'That command is not allowed.',");
    web_client.println("ctr:'This node ran out of counter values.',store:'The node could not save it. Try again.',send:'The radio queue is full. Try again in a moment.',size:'The request was too large.',short:'The request was incomplete. Try again.',act:'The request was not understood.',form:'The request could not be read.',slot:'That saved place does not exist.',dup:'This node is already saved in another place.',token:'This page is out of date. Reload it and try again.'};\n"
                       "function rmNow(){return Date.now();}");
    web_client.println("function rmEl(i){return document.getElementById(i);}\n"
                       "function rmTxt(i,t,c){var e=rmEl(i);if(!e)return;e.textContent=t;if(c!==undefined)e.className=c;}\n"
                       "function rmMsg(t,c){rmTxt('rm_msg',t,'rmmsg '+(c||''));}\n"
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
                       "else if(rmTog[n]!==undefined){m=/^ok (\\w+)=(on|off)$/.exec(e.reply);if(m&&m[1]==n){k.sw[rmTog[n]]=m[2]=='on'?1:0;if(n=='led')k.led=true;}}\n"
                       "else if(n=='txpower'){m=/^ok txpower=(-?\\d+)$/.exec(e.reply);if(m)k.cur=+m[1];}}");
    web_client.println("function rmApplySent(list){var a=list.slice(0).sort(function(x,y){return y.ago-x.ago;}),i,e;\n"
                       "for(i=0;i<a.length;i++){e=a[i];if(e.rep&&e.ver&&e.st=='ok')rmApplyEntry(rmKnFor(String(e.dst)),e);}}\n"
                       "function rmOkText(e){var c=e.cmd.split(' '),n=c[0],o;\n"
                       "if(n=='status'){o=rmParseStatus(e.reply);return o?'Connected. Version '+o.ver+(o.up!==''?', '+rmUp(o.up):'')+(o.bat!==''?', battery '+o.bat+' %':'')+'.':e.msg;}\n"
                       "if(n=='sendpos')return 'Done. The node sent its position.';");
    web_client.println("if(n=='sendtrack')return 'Done. The node sent its track.';\n"
                       "if(n=='reboot')return 'Done. The node is restarting, it is back in about 30 seconds.';\n"
                       "if(n=='sync')return 'Connected. The counters are in step.';\n"
                       "if(rmTogName[n])return 'Done. '+rmTogName[n]+' is now '+c[1]+'.';\n"
                       "if(n=='txpower')return 'Done. TX power is now '+c[1]+' dBm.';\n"
                       "if(n=='setout')return 'Done. Pin '+c[1]+' is now '+c[2]+'.';\n"
                       "return e.msg||'Done.';}");
    web_client.println("function rmPost(u,b){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});}\n"
                       "function rmJson(r){if(r.status==401||r.status==403){rmAuthFail();return Promise.reject(0);}return r.json();}\n"
                       "function rmAuthFail(){rmAuth=true;clearTimeout(rmT.poll);rmT.poll=0;var e=rmEl('rm_auth');if(e){e.textContent='Your session has ended. Please log in again (reload this page).';e.style.display='';}rmRender();}");
    web_client.println("function rmCatch(e,id){if(e===0)return;rmTxt(id||'rm_msg','The node did not answer the request. Check the connection and try again.','rmmsg rmbad');}\n"
                       "function rmHintOff(){var i,e,a=['rm_msg','rm_selfmsg'];for(i=0;i<2;i++){e=rmEl(a[i]);if(e&&/rmconf/.test(e.className))rmTxt(a[i],'','rmmsg');}}\n"
                       "function rmDisarm(){clearTimeout(rmT.arm);rmT.arm=0;rmArmId='';rmHintOff();rmRender();}");
    web_client.println("function rmConfirm(id,hint,mid){if(rmArmId==id){clearTimeout(rmT.arm);rmT.arm=0;rmArmId='';return true;}clearTimeout(rmT.arm);rmArmId=id;rmT.arm=setTimeout(rmDisarm,4000);rmRender();rmTxt(mid||'rm_msg',hint,'rmmsg rmconf');return false;}\n"
                       "function rmBtn(box,label,cls,dis,attrs){var b=document.createElement('button'),k;b.type='button';b.className=cls||'';b.disabled=!!dis;b.textContent=label;for(k in attrs)b.setAttribute(k,attrs[k]);box.appendChild(b);return b;}");
    web_client.println("function rmTile(box,label,cmd,args,cf,cls,dis){var arm=cf&&rmArmId==cf,a={'data-cmd':cmd,'data-args':args};if(cf)a['data-cf']=cf;return rmBtn(box,arm?'Really? tap again':label,'rmtile '+(cls||'')+(arm?' rmarmed':''),dis,a);}\n"
                       "function rmSwTile(box,n,st,dis){var cf=(n=='mesh'||n=='gateway')?n+' off':'',nm=rmTogName[n],w,s;\n"
                       "if(st==1)return rmTile(box,nm+' on',n,'off',cf,'rmon',dis);\n"
                       "if(st==0)return rmTile(box,nm+' off',n,'on','','rmoff',dis);");
    web_client.println("w=document.createElement('div');w.className='rmtile rmunk';s=document.createElement('span');s.textContent=nm+' ?';w.appendChild(s);\n"
                       "rmTile(w,'On',n,'on','','rmsmall',dis);rmTile(w,'Off',n,'off',cf,'rmsmall',dis);box.appendChild(w);}");
    web_client.println("function rmRenderTiles(k,dis){var b=rmEl('rm_info'),i,d=[['status','Refresh status'],['sendpos','Send position now'],['sendtrack','Send track now']],o=['gps','track','display','led','mesh','gateway'],cap=rmCap(k),ap,arm,ids=['rm_test','rm_pinon','rm_pinoff','rm_sync'];\n"
                       "b.textContent='';for(i=0;i<d.length;i++)rmTile(b,d[i][1],d[i][0],'','','',dis);\n"
                       "b=rmEl('rm_sw');b.textContent='';\n"
                       "for(i=0;i<o.length;i++){if(o[i]=='led'&&!k.led)continue;rmSwTile(b,o[i],k.sw[rmTog[o[i]]],dis);}");
    web_client.println("rmTxt('rm_swnote',k.at?'State from the last answer of '+rmSel.call+', '+rmAgo(Math.round((rmNow()-k.at)/1000))+' ago.':'State unknown until the node answers a status request. Press Refresh status.','font-small');\n"
                       "b=rmEl('rm_rs');b.textContent='';rmTile(b,'Restart','reboot','','reboot','rmwarn',dis);\n"
                       "if(!rmTx.touched&&k.cur!==null)rmTx.val=k.cur;if(rmTx.val>cap)rmTx.val=cap;if(rmTx.val<0)rmTx.val=0;");
    web_client.println("rmTxt('rm_rtxval',rmTx.val+' dBm');rmEl('rm_rtxup').disabled=rmTx.val>=cap;rmEl('rm_rtxdn').disabled=rmTx.val<=0;\n"
                       "ap=rmEl('rm_rtxapply');arm=rmArmId=='txpower '+rmTx.val;ap.disabled=dis;ap.textContent=arm?'Really? tap again':'Apply';ap.className=arm?'rmarmed':'';\n"
                       "rmTxt('rm_rtxnote',k.max!==null&&k.max>0?'Highest value this node reports: '+k.max+' dBm.':'The node has not reported its limit yet, so the highest value is 15 dBm.','font-small');\n"
                       "for(i=0;i<ids.length;i++)rmEl(ids[i]).disabled=dis;}");
    web_client.println("function rmChip(box,call,l2,l3,cls){var b=rmBtn(box,'','rmchip '+cls+(rmSel.call==call?' rmsel':''),false,{'data-act':'pick','data-call':call}),s=document.createElement('b');s.textContent=call;b.appendChild(s);\n"
                       "if(l2){s=document.createElement('span');s.className='font-small';s.textContent=l2;b.appendChild(s);}\n"
                       "if(l3){s=document.createElement('span');s.className='font-small';s.textContent=l3;b.appendChild(s);}}");
    web_client.println("function rmRenderNode(){var h=rmEl('rm_heard'),s=rmEl('rm_saved'),i,x,c=rmSel.call,t=rmEl('rm_callchk'),n=0,sv=rmSel.slot>=0;\n"
                       "h.textContent='';s.textContent='';\n"
                       "for(i=0;i<rmHeard.length;i++){x=rmHeard[i];rmChip(h,String(x.call),String(x.hw||''),rmAgo(x.age_s|0)+' ago'+(x.rssi?', '+x.rssi+' dBm':''),'');}\n"
                       "if(!rmHeard.length)h.textContent='No node heard directly yet.';\n"
                       "for(i=0;i<rmSaved.length;i++){x=rmSaved[i];if(x&&x.used){rmChip(s,String(x.call),'saved on this node','','rmsaved');n++;}}");
    web_client.println("if(!n)s.textContent='Nothing saved yet.';\n"
                       "rmEl('rm_forgetall').style.display=n?'':'none';\n"
                       "if(!c)t.textContent='';\n"
                       "else if(rmValidCall(c)){t.textContent='Call sign looks right.';t.className='font-small rmok';}\n"
                       "else{t.textContent='Not a call sign. Use letters or digits, a dash and an SSID, for example DK5EN-12.';t.className='font-small rmbad';}\n"
                       "rmEl('rm_pwrow').style.display=sv?'none':'';rmEl('rm_forget').style.display=sv?'':'none';");
    web_client.println("rmTxt('rm_savednote',sv?'The password of '+c+' is saved on this node. No need to type it.':'Type the password of the node you want to manage. Tip: save it on this node to skip typing.','font-small');\n"
                       "rmEl('rm_forgetall').textContent=rmArmId=='forgetall'?'Really? tap again':'Forget all';rmEl('rm_selfclear').textContent=rmArmId=='selfclear'?'Really? tap again':'Clear';}\n"
                       "function rmRenderLock(){var n=rmLockLeft(),t='';");
    web_client.println("if(rmBusy)t='Sending ...';else if(!rmAuth&&n>0)t=(rmSrvMsg?rmSrvMsg+' ':'')+'Next command possible in '+n+' s.';\n"
                       "rmTxt('rm_lockline',t,'rmlock');}\n"
                       "function rmRenderMsgs(dis){var j=rmStat,b=rmEl('rm_msgs'),a,i,e,k,tr,v,x,c,cf,n,seen={},q,d=rmEl('rm_chain'),u=0,s,z;if(!j)return;d.textContent='';\n");
    web_client.println("for(i=0;i<j.targets.length;i++){q=j.targets[i];if(q.chainMsg)d.textContent=q.dst+': '+q.chainMsg;else if(q.pending)d.textContent=q.dst+': checking the connection first, your command follows.';}\n");
    web_client.println("a=j.sent.slice(0).sort(function(x,y){return x.ago-y.ago;});\n");
    web_client.println("for(i=0;i<a.length;i++){e=a[i];k=e.dst+'#'+e.ctr+'#'+e.cmd;if(seen[k])continue;seen[k]=1;tr=rmRows[k];\n"
"if(!tr){tr=rmRows[k]=document.createElement('tr');for(n=0;n<5;n++)tr.appendChild(document.createElement('td'));}\n");
    web_client.println("c=tr.children;n=e.cmd.split(' ');v=e.st=='queued'||e.st=='waiting'?'sent':e.st=='noanswer'?'no answer':e.ver&&(e.st=='ok'||e.st=='err')?'verified':'unverified';\n"
"x=e.st=='ok'&&v=='verified'?rmOkText(e):(e.msg||'');tr.title=e.msg||'';\n");
    web_client.println("c[0].textContent=rmAgo(e.ago)+' ago';c[1].textContent=e.dst+' '+rmLabel(e.cmd);c[2].textContent=v;c[2].className=v=='verified'?(e.st=='ok'?'rmok':'rmbad'):v=='sent'?'':'rmbad';c[3].textContent=v=='sent'?'':x;\n");
    web_client.println("s=rmSlotOf(e.dst);z=n[0]=='status'||n[0]=='sync'?'':'ag '+k;c[4].textContent='';c[4].title=s<0?'Save the node to run commands again':'';\n"
"if(s>=0)rmBtn(c[4],z&&rmArmId==z?'Really? tap again':'Run again','',rmRowLocked(e.dst),{'data-act':'again','data-dst':e.dst,'data-rc':n[0],'data-ra':n.slice(1).join(' '),'data-cf':z});\n");
    web_client.println("if(b.children[u]!==tr)b.insertBefore(tr,b.children[u]||null);u++;}\n");
    web_client.println("for(k in rmRows)if(!seen[k]){if(rmRows[k].parentNode)rmRows[k].parentNode.removeChild(rmRows[k]);delete rmRows[k];}}\n");
    web_client.println("function rmCell(tr,t){var d=document.createElement('td');d.textContent=String(t);tr.appendChild(d);}\n"
"function rmRenderLog(){var j=rmStat,b,i,r,tr;if(!j)return;\n");
    web_client.println("b=rmEl('rm_log');b.textContent='';for(i=0;i<j.log.length;i++){r=j.log[i];tr=document.createElement('tr');rmCell(tr,rmAgo(r.ago));rmCell(tr,r.src);rmCell(tr,r.ctr);rmCell(tr,r.cmd);rmCell(tr,r.res);b.appendChild(tr);}\n");
    web_client.println("rmTxt('rm_cnt','Executed '+j.ok+', rejected '+j.rej+', counter high-water '+j.hwm+'.');}\n"
                       "function rmRenderSelf(){var j=rmStat,o=rmEl('rm_on');if(!j)return;o.checked=!!j.on;o.disabled=!j.pw&&!j.on;o.style.display=rmEl('rm_onlbl').style.display=j.pw||j.on?'':'none';\n"
                       "rmTxt('rm_pwstate','password: '+(j.pw?'set':'not set'),'font-small');");
    web_client.println("rmTxt('rm_lockstate',j.lock?'Remote commands blocked for '+(j.lockS||1)+' s after wrong attempts':'','font-small');\n"
                       "rmTxt('rm_onhint',j.pw?'':'Set a password first, then switch remote management on.','font-small');}\n"
                       "function rmRender(){if(!rmShown||!rmEl('rm_page'))return;var k=rmKn(),dis=!rmValidCall(rmSel.call)||rmLocked();\n"
                       "rmRenderNode();rmRenderTiles(k,dis);rmRenderLock();rmRenderMsgs();rmRenderLog();rmRenderSelf();");
    web_client.println("}\n"
"function rmPre(){var c=rmSel.call,p;if(!rmValidCall(c))return 'Choose a node first.';if(rmSel.slot<0){p=rmPwProblem(rmEl('rm_pw').value);if(p)return 'Type the password of '+c+' first. '+p;}return '';}");
    web_client.println("function rmSendCmd(cmd,args,dc,sl){var call=dc||rmSel.call,body,pw,pr=dc?'':rmPre(),full=args?cmd+' '+args:cmd;\n"
                       "if(dc?rmRowLocked(dc):rmLocked())return;\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n");
    web_client.println("body='cmd='+rmEnc(cmd)+'&args='+rmEnc(args);\n"
                       "if(dc)body='slot='+sl+'&'+body;else if(rmSel.slot>=0)body='slot='+rmSel.slot+'&'+body;\n"
                       "else{pw=rmEl('rm_pw').value;body='dst='+rmEnc(call)+'&pw='+rmEnc(pw)+'&'+body;rmEl('rm_pw').value='';pw='';}\n"
                       "rmBusy=true;rmMsg('','');rmRender();\n"
                       "rmPost('/rmsend',body).then(rmJson).then(function(j){");
    web_client.println("if(j&&j.ok){rmLock=rmNow()+10000;rmOut=true;rmMsg('','');rmPoll();}\n"
                       "else{pr=j&&j.err?String(j.err):'send';rmMsg(rmErrText(pr),'rmbad');if(pr=='busy')rmLock=rmNow()+10000;}\n"
                       "}).catch(rmCatch).then(function(){rmBusy=false;rmRender();});}\n"
                       "function rmTap(b){var cmd=b.getAttribute('data-cmd'),args=b.getAttribute('data-args')||'',cf=b.getAttribute('data-cf')||'',pr=rmPre();");
    web_client.println("if(rmLocked())return;\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n"
                       "if(cf&&!rmConfirm(cf,'Tap again within 4 seconds to confirm: '+rmLabel((cmd+' '+args).trim())+' on '+rmSel.call+'.'))return;\n"
                       "rmSendCmd(cmd,args);}\n");
    web_client.println("function rmAgain(b){var d=b.getAttribute('data-dst'),cmd=b.getAttribute('data-rc'),args=b.getAttribute('data-ra')||'',cf=b.getAttribute('data-cf')||'',s=rmSlotOf(d);\n"
                       "if(s<0||rmRowLocked(d))return;\n"
                       "if(cf&&!rmConfirm(cf,'Tap again within 4 seconds to confirm: '+rmLabel((cmd+' '+args).trim())+' on '+d+'.'))return;\n"
                       "rmSendCmd(cmd,args,d,s);}\n");
    web_client.println("function rmTxApply(){var k=rmKn(),v=Math.min(rmTx.val,rmCap(k)),cf,pr=rmPre();if(rmLocked())return;\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n"
                       "cf=(k.cur===null||v<k.cur)?'txpower '+v:'';\n"
                       "if(cf&&!rmConfirm(cf,'Tap Apply again within 4 seconds to set the TX power of '+rmSel.call+' to '+v+' dBm.'))return;");
    web_client.println("rmSendCmd('txpower',String(v));}\n"
                       "function rmPick(c){rmDisarmQuiet();rmSel.call=c;rmSel.slot=rmSlotOf(c);rmTx.touched=false;rmEl('rm_call').value=c;rmRender();}\n"
                       "function rmDisarmQuiet(){clearTimeout(rmT.arm);rmT.arm=0;rmArmId='';}\n"
                       "function rmCallInput(){var c=rmEl('rm_call');c.value=c.value.toUpperCase();rmDisarmQuiet();rmSel.call=c.value.trim();rmSel.slot=rmSlotOf(rmSel.call);rmTx.touched=false;rmRender();}");
    web_client.println("function rmLoadNodes(){fetch('/rmnodes').then(rmJson).then(function(j){rmSaved=(j&&j.nodes)||[];rmSel.slot=rmSlotOf(rmSel.call);rmRender();}).catch(function(e){rmCatch(e,'rm_net');});}\n"
                       "function rmLoadHeard(){fetch('/rmheard').then(rmJson).then(function(j){rmHeard=(j&&j.heard)||[];rmRender();}).catch(function(e){rmCatch(e,'rm_net');});}");
    web_client.println("function rmNodesPost(body,okText){rmPost('/rmnodes',body).then(rmJson).then(function(j){if(j&&j.ok){rmMsg(okText,'rmok');rmLoadNodes();}else rmMsg(rmErrText(j&&j.err?String(j.err):'form'),'rmbad');}).catch(rmCatch);}\n"
                       "function rmRemember(){var c=rmSel.call,i=rmEl('rm_pw'),pw=i.value,pr=rmPwProblem(pw),s=-1,n;\n"
                       "if(!rmValidCall(c)){rmMsg('Enter a valid call sign first.','rmbad');return;}\n"
                       "if(pr){rmMsg(pr,'rmbad');return;}\n"
                       "for(n=0;n<3;n++)if(!rmSaved[n]||!rmSaved[n].used){s=n;break;}");
    web_client.println("if(s<0){rmMsg('All 3 places on this node are used. Forget one first.','rmbad');return;}\n"
                       "i.value='';rmNodesPost('act=save&slot='+s+'&call='+rmEnc(c)+'&pw='+rmEnc(pw),c+' is saved on this node.');pw='';}\n"
                       "function rmForget(){if(rmSel.slot<0)return;rmNodesPost('act=del&slot='+rmSel.slot,rmSel.call+' is forgotten.');}");
    web_client.println("function rmForgetAll(){if(!rmConfirm('forgetall','Tap Forget all again within 4 seconds to remove every saved node.'))return;rmNodesPost('act=forget','All saved nodes are forgotten.');}\n"
                       "function rmSelfSet(){var i=rmEl('rm_selfpw'),pw=i.value,pr=rmPwProblem(pw),b;\n"
                       "if(pr){rmTxt('rm_selfmsg',pr,'rmmsg rmbad');return;}\n"
                       "b='act=set&pw='+rmEnc(pw);i.value='';pw='';");
    web_client.println("rmPost('/rmpasswd',b).then(rmJson).then(function(j){if(j&&j.ok){rmTxt('rm_selfmsg','Password set.','rmmsg rmok');rmPoll();}else rmTxt('rm_selfmsg',rmErrText(j&&j.err?String(j.err):'form'),'rmmsg rmbad');}).catch(function(e){rmCatch(e,'rm_selfmsg');});}\n"
                       "function rmSelfClear(){if(!rmConfirm('selfclear','Clear the password? Remote management will be switched off.','rm_selfmsg'))return;");
    web_client.println("rmPost('/rmpasswd','act=clear').then(rmJson).then(function(j){if(j&&j.ok){rmTxt('rm_selfmsg','Password cleared. Remote management is off.','rmmsg rmok');rmPoll();}else rmTxt('rm_selfmsg',rmErrText(j&&j.err?String(j.err):'form'),'rmmsg rmbad');}).catch(function(e){rmCatch(e,'rm_selfmsg');});}\n"
                       "function rmSelfOn(){var o=rmEl('rm_on'),v=o.checked?'on':'off';o.disabled=true;");
    web_client.println("fetch('/setparam/?rm='+v).then(rmJson).then(function(j){rmTxt('rm_selfmsg',j&&j.returncode==0?'Remote management is '+v+'.':'Could not change it. Is a password set?','rmmsg');rmPoll();}).catch(function(e){rmCatch(e,'rm_selfmsg');});}\n"
                       "function rmPin(v){var p=rmEl('rm_pin').value;rmSendCmd('setout',p+' '+v);}\n"
                       "function rmClick(e){var b=e.target;while(b&&b.tagName!='BUTTON'){if(b.id=='rm_page')return;b=b.parentNode;}\n"
                       "if(!b||b.disabled)return;var a=b.getAttribute('data-act');");
    web_client.println("if(b.hasAttribute('data-cmd'))rmTap(b);\n"
                       "else if(a=='pick')rmPick(b.getAttribute('data-call'));\n"
                       "else if(a=='test')rmSendCmd('status','');\n"
                       "else if(a=='sync')rmSendCmd('sync','');\n"
                       "else if(a=='pinon')rmPin('on');\n"
                       "else if(a=='pinoff')rmPin('off');\n"
                       "else if(a=='again')rmAgain(b);\n");
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
                       "rmRender();}\n"
                       "function rmNext(){clearTimeout(rmT.poll);rmT.poll=0;if(rmShown&&!rmAuth)rmT.poll=setTimeout(rmPoll,rmOut?3000:10000);}\n"
                       "function rmPoll(){if(!rmShown)return;clearTimeout(rmT.poll);rmT.poll=0;");
    web_client.println("fetch('/rmstatus').then(rmJson).then(function(j){if(!rmShown)return;rmAuth=false;rmTxt('rm_auth','');rmEl('rm_auth').style.display='none';rmTxt('rm_net','');rmGot(j);rmNext();})\n"
                       ".catch(function(e){if(e===0||!rmShown)return;rmTxt('rm_net','No answer from the node, trying again.','rmbad');rmNext();});}\n"
                       "function rmTick(){if((typeof cpage!='undefined'&&cpage!='remote')||!rmEl('rm_page')){rmPageLeave();return;}\n"
                       "var l=rmLocked();if(l||rmWasLocked)rmRender();rmWasLocked=l;}");
    web_client.println("function rmPageLeave(){clearInterval(rmT.tick);clearTimeout(rmT.poll);clearTimeout(rmT.arm);rmT.tick=0;rmT.poll=0;rmT.arm=0;rmShown=false;rmArmId='';}\n"
                       "function rmPageInit(){var i,p,o;rmPageLeave();if(!rmEl('rm_page'))return;rmShown=true;rmRows=Object.create(null);rmAuth=false;rmBusy=false;rmArmId='';\n"
                       "p=rmEl('rm_pin');for(i=0;i<16;i++){o=document.createElement('option');o.value=(i<8?'a':'b')+(i%8);o.textContent=o.value;p.appendChild(o);}");
    web_client.println("rmEl('rm_page').onclick=rmClick;rmEl('rm_call').oninput=rmCallInput;rmEl('rm_on').onchange=rmSelfOn;rmEl('rm_call').value=rmSel.call;\n"
                       "rmT.tick=setInterval(rmTick,1000);rmRender();rmPoll();rmLoadNodes();rmLoadHeard();}");
}
