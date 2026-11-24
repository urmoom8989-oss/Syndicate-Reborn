// Syndicate's V-Bucks checkout (syndicateVbucksStore.js). Buying a V-Bucks pack in the STORE tab opens the game's
// checkout browser; SyndicateClient.dll points it here instead of Epic's store. There is nothing to fill in: the
// pack is paid with Syndicate Rewards earned in matches (syndicateRewardWallet.js), and Place Order adds its V-Bucks.
// Only this PC can use it (loopback), and it never reaches the internet, so it works the same in offline mode.
import express from 'express';
import crypto from 'node:crypto';
import {verifyToken} from '../User/tokenManager/tokenVerify.js';
import User from '../User/Mongodb/Schema/user.js';
import Profile from '../User/Mongodb/Schema/profiles.js';
import {rewardBalance,formatDollars} from '../Utils/syndicateRewardWallet.js';
import {packsWith,packFor,formatVbucks,formatPrice,grantVbucksPack,isLoopback,localAccount} from '../Utils/syndicateVbucksStore.js';

const app=express.Router();

// GET /account/api/oauth/exchange: the game asks for a one-time code to sign its checkout page in (Epic's SSO).
// The code here only tells the checkout page whose purchase it is; it cannot be used to log in.
const checkoutCodes=new Map();
app.get('/account/api/oauth/exchange',verifyToken,(req,res)=>{
 const code=crypto.randomBytes(16).toString('hex'),now=Date.now();
 for(const [k,v] of checkoutCodes)if(now-v.at>600e3)checkoutCodes.delete(k);
 checkoutCodes.set(code,{accountId:req.user.accountId,at:now});
 res.json({expiresInSeconds:300,code,creatingClientId:'ec684b8c687f479fadea3cb2ad83f5c6'});
});

// The URL the game wanted to open, decoded as often as it was encoded (the sign-in page wraps the store page).
function unwrap(url){
 let text=String(url||'');const all=[text];
 for(let i=0;i<4;i++){try{const next=decodeURIComponent(text);if(next===text)break;text=next;all.push(text);}catch{break;}}
 return all.join(' ');
}
const packIn=url=>{const m=/app-SyndicateVbucks(\d+)/i.exec(unwrap(url));return m?packFor('app-SyndicateVbucks'+m[1]):null;};
function accountFor(from){
 const m=/exchangeCode=([0-9a-f]{16,64})/i.exec(unwrap(from));
 const byCode=m&&checkoutCodes.get(m[1]);
 if(byCode&&Date.now()-byCode.at<600e3)return byCode.accountId;
 const launched=global.syndicateLaunchAccount;
 return localAccount()||(launched&&Date.now()-launched.at<12*3600e3?launched.accountId:null);
}
const local=(req,res,next)=>isLoopback(req.socket?.remoteAddress)?next():res.status(403).type('text').send('The Syndicate checkout only opens on the PC that runs the game.');

const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
// A store checkout laid out like the game's real one: payment on the left (Syndicate Rewards applied, account
// balance, other payment methods), order summary on the right, Place Order at the bottom. Syndicate-branded.
// Syndicate Rewards (the money earned in matches, syndicateRewardWallet.js) are the only way to pay; nothing can
// be typed and there are no card fields.
function page({pack,nonce,from,name,rewards}){
 const who=esc(name||'Player');
 const set=packsWith(pack);
 const packs=set.map(p=>`<label class="pick${pack&&p.appStoreId===pack.appStoreId?' on':''}"><input type="radio" name="pack" value="${p.appStoreId}"${pack&&p.appStoreId===pack.appStoreId?' checked':''}><span class="pn">${esc(formatVbucks(p.quantity-p.bonus))} V-Bucks${p.bonus?` <em>+${esc(formatVbucks(p.bonus))}</em>`:''}</span><span class="pp">${formatPrice(p.cents)}</span></label>`).join('');
 const state=JSON.stringify({pack:pack?.appStoreId||'',nonce,from,rewards,packs:Object.fromEntries(set.map(p=>[p.appStoreId,{quantity:p.quantity,base:p.quantity-p.bonus,bonus:p.bonus,cents:p.cents}]))}).replace(/</g,'\\u003c');
 const coin=(size)=>`<svg viewBox="0 0 40 40" width="${size}" height="${size}" aria-hidden="true"><defs><radialGradient id="vg${size}" cx="35%" cy="30%" r="75%"><stop offset="0" stop-color="#c9f1ff"/><stop offset=".55" stop-color="#4aa3f0"/><stop offset="1" stop-color="#1d5fb8"/></radialGradient></defs><circle cx="20" cy="20" r="18" fill="url(#vg${size})" stroke="#dff6ff" stroke-width="2"/><path d="M12 11h5l3 11 3-11h5l-6 18h-4z" fill="#fff"/></svg>`;
 return `<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Checkout</title><style>
*{box-sizing:border-box}html,body{margin:0;height:100%}
body{font-family:"Segoe UI","Helvetica Neue",Arial,sans-serif;color:#202020;background:rgba(0,0,0,.55);display:flex;align-items:center;justify-content:center;padding:10px;user-select:none;-webkit-font-smoothing:antialiased}
.win{width:min(1080px,100%);height:min(860px,100%);display:grid;grid-template-columns:1fr minmax(300px,380px);background:#fff;border-radius:6px;overflow:hidden;box-shadow:0 20px 60px rgba(0,0,0,.45)}
.l{display:flex;flex-direction:column;min-height:0;padding:0 14px 0 26px}
.head{display:flex;align-items:flex-end;border-bottom:1px solid #dcdcdc;margin:18px 12px 0 0}
.tab{width:50%;padding:12px 0;border-bottom:3px solid #0078f2;margin-bottom:-2px;font-size:14px;font-weight:600;letter-spacing:.3px}
.user{margin-left:auto;padding-bottom:12px;color:#0078f2;font-size:11px;font-weight:700;letter-spacing:.4px;text-transform:uppercase;display:flex;gap:6px;align-items:center}
.scroll{flex:1;min-height:0;overflow-y:auto;padding:12px 12px 20px 0}
.scroll::-webkit-scrollbar{width:8px}.scroll::-webkit-scrollbar-thumb{background:#b5b5b5;border-radius:4px}
.alert{display:none;align-items:center;gap:14px;border:1px solid #dcdcdc;border-radius:4px;padding:16px 18px;margin:6px 0 4px;font-size:14px}
.alert .xr{width:18px;height:18px;border-radius:50%;background:#d93025;color:#fff;display:grid;place-items:center;font-size:12px;font-weight:700;flex:none}
.alert .cl{margin-left:auto;background:none;border:0;font-size:20px;color:#555;cursor:pointer}
h3{margin:22px 0 12px;font-size:14px;font-weight:600;letter-spacing:.3px;text-transform:uppercase}
.rw{border:1px solid #0078f2;border-radius:4px;padding:18px}
.rw.short{border-color:#d93025}
.rwtop{display:flex;align-items:center;gap:14px;font-size:16px}
.ck{width:20px;height:20px;border-radius:50%;background:#0078f2;color:#fff;display:grid;place-items:center;font-size:12px;flex:none}
.tile{width:56px;height:40px;border:1px solid #c8c8c8;border-radius:4px;background:#fff;display:grid;place-items:center;flex:none;font-size:10px;font-weight:800;color:#333}
.star{width:20px;height:20px;border-radius:50%;background:#c9a227;display:grid;place-items:center;color:#fff;font-size:12px}
.rwtxt{margin:18px 0 10px 60px;font-size:14px;color:#333}
.rwrow{display:flex;gap:10px;margin-left:60px}
.fld{flex:1;border:1px solid #a8a8a8;border-radius:4px;padding:8px 14px;position:relative}
.fld small{display:block;font-size:12px;color:#666}.fld small i{color:#e8710a;font-style:normal}.fld b{font-weight:500;font-size:15px}
.fld .clr{position:absolute;right:12px;top:50%;transform:translateY(-50%);width:16px;height:16px;border-radius:50%;background:#666;color:#fff;font-size:10px;display:grid;place-items:center}
.ap{width:128px;border:1px solid #a8a8a8;border-radius:4px;display:flex;align-items:center;justify-content:center;gap:8px;font-size:12px;font-weight:700;letter-spacing:.4px}
.ap .o{width:18px;height:18px;border:2px solid #202020;border-radius:50%;display:grid;place-items:center;font-size:10px}
.plus{display:flex;align-items:center;gap:20px;margin:22px 0 0;color:#202020;font-size:26px;font-weight:300}.plus:before,.plus:after{content:"";flex:1;height:1px;background:#dcdcdc}
.pm{display:flex;align-items:center;gap:16px;padding:14px 16px;border-radius:4px;background:#f5f5f5;margin-bottom:10px;color:#777;font-size:16px}
.pm .rd{width:20px;height:20px;border-radius:50%;border:1px solid #a8a8a8;background:#fff;flex:none}
.pm .tile{color:#777}.pm b{color:#555}
.cardic{width:26px;height:18px;border-radius:3px;background:#8f8f8f;position:relative}.cardic:after{content:"";position:absolute;left:0;right:0;top:5px;height:3px;background:#5f5f5f}
.r{background:#f2f2f2;display:flex;flex-direction:column;min-height:0}
.rtop{padding:20px 22px 0;overflow-y:auto;flex:1}
.rh{display:flex;align-items:center;justify-content:space-between;margin:0 0 18px}.rh span{font-size:14px;font-weight:600;letter-spacing:.3px}
.rh button{background:none;border:0;font-size:22px;color:#444;cursor:pointer;line-height:1}
.item{display:flex;gap:12px;align-items:center}
.art{width:88px;height:80px;border-radius:4px;flex:none;display:grid;place-items:center;background:linear-gradient(160deg,#c2c6cc,#7b8089)}
.in1{font-size:16px;font-weight:700}.in2{font-size:14px;color:#555;margin-top:2px}.in3{font-size:12px;margin-top:2px}
.picks{display:grid;gap:6px;margin:14px 0 0}.pick{display:flex;align-items:center;gap:10px;padding:8px 10px;background:#fff;border:1px solid #d6d6d6;border-radius:4px;cursor:pointer;font-size:13px}.pick.on{border-color:#0078f2}.pick input{accent-color:#0078f2}.pn em{color:#0078f2;font-style:normal;font-size:11px;margin-left:4px}.pp{margin-left:auto;color:#555}
.lines{margin-top:22px;font-size:14px;color:#555}
.ln{display:flex;justify-content:space-between;padding:3px 0}
.tot{display:flex;justify-content:space-between;border-top:1px solid #cfcfcf;margin-top:10px;padding-top:12px;font-size:14px;font-weight:700;color:#202020}
.note{display:flex;gap:10px;align-items:center;margin-top:14px;padding:10px 12px;border-radius:4px;background:#e3e3e3;font-size:12px;color:#303030;line-height:1.35}
.note .dot{width:20px;height:20px;border-radius:50%;background:#303030;color:#fff;display:grid;place-items:center;font-size:11px;flex:none}
.note.short{background:#fbe3e1;color:#8a1c12}.note.short .dot{background:#d93025}
.code{margin-top:22px;display:flex;align-items:center;justify-content:space-between;border:1px solid #a8a8a8;border-radius:4px;padding:11px 14px;color:#777;font-size:14px;background:#fff}
.code .i{width:20px;height:20px;border-radius:50%;background:#444;color:#fff;display:grid;place-items:center;font-size:11px;font-weight:700;font-style:italic}
.foot{background:#f8f8f8;border-top:1px solid #dcdcdc;padding:18px 22px 22px}
.legal{font-size:12px;color:#303030;line-height:1.4;margin:0 0 6px}
.go{margin-top:10px;width:100%;height:56px;border:0;border-radius:4px;background:#0078f2;color:#fff;font-size:13px;font-weight:700;letter-spacing:.6px;text-transform:uppercase;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:10px}
.go:hover{background:#1a8cff}.go:disabled{background:#9cc8f5;cursor:default}
.spin{width:16px;height:16px;border-radius:50%;border:2px solid #fff;border-top-color:transparent;animation:s .8s linear infinite;display:none}.busy .spin{display:block}@keyframes s{to{transform:rotate(360deg)}}
.done{display:none;grid-column:1/-1;padding:60px 30px;text-align:center;overflow:auto}
.ok{width:76px;height:76px;margin:0 auto 18px;border-radius:50%;background:#1e8e3e;display:grid;place-items:center;font-size:40px;color:#fff}
.done h2{margin:0;font-size:26px}.done .sub{color:#555;margin:8px 0 24px;font-size:15px}
.rcpt{max-width:440px;margin:0 auto 24px;text-align:left;border:1px solid #dcdcdc;border-radius:4px;padding:6px 16px}
.rcpt .ln{border-bottom:1px solid #eee;padding:9px 0;font-size:14px;color:#303030}.rcpt .ln:last-child{border:0}
.done .go{max-width:440px;margin:0 auto}
@media (max-width:760px){.win{grid-template-columns:1fr;height:auto;max-height:100%;overflow:auto}.rwtxt,.rwrow{margin-left:0}}
</style></head><body><div class="win" id="win">
<div class="l" id="left">
<div class="head"><div class="tab">CHECKOUT</div><div class="user"><svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true"><path fill="currentColor" d="M8 8a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7zm0 1.5c-3 0-6 1.5-6 4V15h12v-1.5c0-2.5-3-4-6-4z"/></svg>${who}</div></div>
<div class="scroll">
<div class="alert" id="alert"><span class="xr">&#10005;</span><span id="alerttext"></span><button class="cl" id="alertx">&times;</button></div>
<h3>Syndicate Rewards</h3>
<div class="rw" id="rw">
<div class="rwtop"><span class="ck" id="rwck">&#10003;</span><span class="tile"><span class="star">&#9733;</span></span><span>Syndicate Rewards <b id="rwbal"></b></span></div>
<div class="rwtxt">Earned in matches: $5 for every elimination, $10 for every Victory Royale. Applied to this order:</div>
<div class="rwrow"><div class="fld"><small>Applied Rewards <i>*</i></small><b id="rwapplied">$0.00</b><span class="clr">&#10005;</span></div><div class="ap" id="rwbtn">APPLIED <span class="o">&#10003;</span></div></div>
</div>
<div class="plus">+</div>
<h3>Account balance</h3>
<div class="pm"><span class="rd"></span><span class="tile">SYN</span><span>Account Balance <b>$0.00</b></span></div>
<h3>Other payment methods</h3>
<div class="pm"><span class="rd"></span><span class="tile"><span class="cardic"></span></span><span>Credit Card / Debit Card</span></div>
<div class="pm"><span class="rd"></span><span class="tile">&#9673;</span><span>Digital Wallet</span></div>
<div class="pm"><span class="rd"></span><span class="tile">&#9733;</span><span>Gift Card</span></div>
</div></div>
<div class="r" id="right"><div class="rtop">
<div class="rh"><span>ORDER SUMMARY</span><button id="x" title="Close">&times;</button></div>
<div class="item"><div class="art">${coin(46)}</div><div><div class="in1" id="iname">Choose a pack</div><div class="in2">Syndicate</div><div class="in3" id="iprice"></div></div></div>
<div class="picks" id="picks"${pack?' style="display:none"':''}>${packs}</div>
<div class="lines"><div class="ln"><span>Price</span><span id="lprice">-</span></div><div class="ln"><span>Syndicate Rewards</span><span id="lrw">-</span></div>
<div class="tot"><span>Total</span><span id="ltot">$0.00</span></div></div>
<div class="note" id="note"><span class="dot">&#10022;</span><span id="notetext"></span></div>
<div class="code"><span>Enter creator code</span><span class="i">i</span></div>
</div>
<div class="foot"><p class="legal">You are getting V-Bucks on Syndicate, a private server. They are paid with Syndicate Rewards you earned in matches; no real money is used.</p>
<p class="legal">By selecting 'Place Order' below, the price is taken from your Syndicate Rewards and the V-Bucks are added to the account you are signed in with.</p>
<button class="go" id="go" disabled><span class="spin"></span><span id="golabel">Place order</span></button></div>
</div>
<div class="done" id="done"><div class="ok">&#10003;</div><h2>Thanks for your order!</h2><div class="sub" id="dsub"></div>
<div class="rcpt"><div class="ln"><span>Order number</span><span id="dorder"></span></div><div class="ln"><span>Date</span><span id="ddate"></span></div><div class="ln"><span>Paid with</span><span id="dpaid"></span></div><div class="ln"><span>Syndicate Rewards left</span><span id="drw"></span></div><div class="ln"><span>V-Bucks balance</span><span id="dbal"></span></div></div>
<button class="go" id="back">Back to Fortnite</button></div>
</div><script>
const S=${state};
const $=id=>document.getElementById(id);
const money=c=>'$'+(Math.max(0,c)/100).toLocaleString('en-US',{minimumFractionDigits:2,maximumFractionDigits:2});
const fmt=n=>n.toLocaleString('en-US');
// The game's checkout browser exposes its purchase bridge (UPurchaseFlowJSBridge: Receipt, RequestClose) on window.ue.
const has=(o,name)=>!!o&&(typeof o[name]==='function'||typeof o[name[0].toUpperCase()+name.slice(1)]==='function'||Object.keys(o).some(n=>n.toLowerCase()===name));
function bridge(){const ue=window.ue;if(!ue)return null;const list=[ue.purchaseflow,ue.purchaseflowjsbridge,ue.epicpurchaseflow];try{for(const k of Object.keys(ue))list.push(ue[k]);}catch(e){}return list.find(o=>has(o,'receipt'))||null;}
function call(o,name,arg){const f=o[name]||o[name[0].toUpperCase()+name.slice(1)]||o[Object.keys(o).find(n=>n.toLowerCase()===name)];if(typeof f==='function')return f.call(o,arg);}
function waitBridge(ms){return new Promise(r=>{const t0=Date.now();(function look(){const b=bridge();if(b||Date.now()-t0>ms)return r(b);setTimeout(look,100);})();});}
function alertBox(text){$('alerttext').textContent=text;$('alert').style.display=text?'flex':'none';}
$('alertx').onclick=()=>alertBox('');
function render(){
 $('rwbal').textContent=money(S.rewards);
 const p=S.packs[S.pack];
 if(!p){$('go').disabled=true;$('notetext').textContent='Choose a V-Bucks pack.';return;}
 const applied=Math.min(S.rewards,p.cents),short=p.cents-applied;
 $('iname').textContent=fmt(p.base)+' V-Bucks'+(p.bonus?' + '+fmt(p.bonus)+' Bonus':'');
 $('iprice').textContent=money(p.cents);$('lprice').textContent=money(p.cents);$('lrw').textContent='-'+money(applied);$('ltot').textContent=money(short);
 $('rwapplied').textContent=money(applied);
 $('rw').classList.toggle('short',short>0);$('note').classList.toggle('short',short>0);
 $('notetext').textContent=short>0?'You need '+money(short)+' more in Syndicate Rewards for this order. Earn $5 for every elimination and $10 for every Victory Royale.':"You've applied enough Syndicate Rewards to cover your order total! Earn more in your next match.";
 $('go').disabled=short>0;
 alertBox(short>0?'Not enough Syndicate Rewards to complete this order.':'');
 document.querySelectorAll('.pick').forEach(l=>l.classList.toggle('on',l.querySelector('input').value===S.pack));
}
document.querySelectorAll('.pick input').forEach(i=>i.addEventListener('change',()=>{S.pack=i.value;render();}));
render();
let finished=false;
async function close(info){const b=await waitBridge(400);if(b){try{call(b,'requestclose',info);return;}catch(e){}}try{window.close();}catch(e){}}
$('x').onclick=()=>close('UserCancelled');$('back').onclick=()=>close('');
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&!finished)close('UserCancelled');});
$('go').onclick=async()=>{
 if(!S.packs[S.pack])return;const go=$('go');go.disabled=true;go.classList.add('busy');$('golabel').textContent='Processing';alertBox('');
 try{
  const wait=new Promise(r=>setTimeout(r,1400)); // a real checkout takes a moment
  const r=await fetch('/syndicate/checkout/confirm',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pack:S.pack,nonce:S.nonce,from:S.from})});
  const j=await r.json().catch(()=>({}));
  await wait;
  if(!r.ok){if(Number.isSafeInteger(j.rewards)){S.rewards=j.rewards;render();}throw new Error(j.message||'This transaction could not be completed.');}
  finished=true;$('left').style.display='none';$('right').style.display='none';$('done').style.display='block';
  $('dsub').textContent=fmt(j.quantity)+' V-Bucks have been added to your account.';
  $('dorder').textContent='SYN-'+String(j.receipt.receiptId).slice(0,10).toUpperCase();
  $('ddate').textContent=new Date().toLocaleDateString('en-US',{year:'numeric',month:'long',day:'numeric'});
  $('dpaid').textContent='Syndicate Rewards '+money(S.packs[S.pack].cents);
  $('drw').textContent=money(j.rewards);$('dbal').textContent=fmt(j.balance)+' V-Bucks';
  const b=await waitBridge(1500);
  if(b){try{call(b,'receipt',{transactionid:j.receipt.receiptId,transactionstate:'Purchased',offers:[{offernamespace:'fn',offerid:S.pack,quantity:1,items:[{itemid:S.pack,entitlementid:j.receipt.receiptId,validationinfo:j.receipt.receiptInfo}]}]});}catch(e){}}
 }catch(e){go.classList.remove('busy');$('golabel').textContent='Place order';render();alertBox(e.message);}
};
</script></body></html>`;
}

// The account the checkout pays for and its Syndicate Rewards.
async function buyer(from){
 const accountId=accountFor(from);if(!accountId)return {name:'',rewards:0};
 const [user,doc]=await Promise.all([User.findOne({accountId},{username:1}).lean(),Profile.collection.findOne({accountId},{projection:{'profiles.common_core.stats':1}})]);
 return {name:user?.username||'',rewards:rewardBalance(doc?.profiles?.common_core)};
}
app.get('/syndicate/checkout',local,async(req,res)=>{
 const from=String(req.query.from||'');
 const pack=packFor(String(req.query.pack||''))||packIn(from);
 let who={name:'',rewards:0};
 try{who=await buyer(from);}catch{}
 console.log(`[Syndicate] V-Bucks checkout opened${pack?' for '+pack.appStoreId:''}, Syndicate Rewards ${formatDollars(who.rewards)} (game asked for: ${unwrap(from).slice(0,240)||'nothing'})`);
 res.set('Cache-Control','no-store').type('html').send(page({pack,nonce:crypto.randomUUID(),from,name:who.name,rewards:who.rewards}));
});
app.post('/syndicate/checkout/confirm',local,async(req,res)=>{
 const accountId=accountFor(req.body?.from);
 try{
  const {pack,balance,rewards,receipt}=await grantVbucksPack(accountId,String(req.body?.pack||''),req.body?.nonce);
  console.log(`[Syndicate] V-Bucks checkout: ${pack.quantity} V-Bucks bought by ${accountId} for ${formatPrice(pack.cents)} in Syndicate Rewards (${formatDollars(rewards)} left, ${balance} V-Bucks).`);
  res.json({ok:true,quantity:pack.quantity,balance,rewards,receipt:{receiptId:receipt.receiptId,receiptInfo:receipt.receiptInfo,appStoreId:receipt.appStoreId}});
 }catch(error){
  let rewards;try{rewards=(await buyer(req.body?.from)).rewards;}catch{}
  res.status(400).json({ok:false,message:error.message,rewards});
 }
});

export default app;
