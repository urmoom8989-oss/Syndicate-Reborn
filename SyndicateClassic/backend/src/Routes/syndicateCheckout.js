// Syndicate's V-Bucks checkout (syndicateVbucksStore.js). Buying a V-Bucks pack in the STORE tab opens the game's
// checkout browser; SyndicateClient.dll points it here instead of Epic's store. There is nothing to fill in and
// nothing is paid: Confirm adds the pack's V-Bucks. Only this PC can use it (loopback), and it never reaches the
// internet, so it works the same in offline mode.
import express from 'express';
import crypto from 'node:crypto';
import {verifyToken} from '../User/tokenManager/tokenVerify.js';
import User from '../User/Mongodb/Schema/user.js';
import {VBUCKS_PACKS,packFor,formatVbucks,formatPrice,grantVbucksPack,isLoopback,localAccount} from '../Utils/syndicateVbucksStore.js';

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
// A full store checkout, laid out like a real one (payment methods, card details, order summary, Place Order,
// order confirmation), Syndicate-branded. The only card is Syndicate's demo card: its details are filled in and
// locked, nothing can be typed, and the total is always $0.00.
function page({pack,nonce,from,name}){
 const who=esc(name||'Player');
 const packs=VBUCKS_PACKS.map(p=>`<label class="pick${pack&&p.appStoreId===pack.appStoreId?' on':''}"><input type="radio" name="pack" value="${p.appStoreId}"${pack&&p.appStoreId===pack.appStoreId?' checked':''}><span class="vb sm">V</span><span class="pn">${esc(formatVbucks(p.quantity-p.bonus))} V-Bucks${p.bonus?` <em>+${esc(formatVbucks(p.bonus))} bonus</em>`:''}</span><span class="pp">${formatPrice(p.cents)}</span></label>`).join('');
 const state=JSON.stringify({pack:pack?.appStoreId||'',nonce,from,packs:Object.fromEntries(VBUCKS_PACKS.map(p=>[p.appStoreId,{quantity:p.quantity,base:p.quantity-p.bonus,bonus:p.bonus,price:formatPrice(p.cents)}]))}).replace(/</g,'\\u003c');
 const lock='<svg viewBox="0 0 16 16" width="12" height="12" aria-hidden="true"><path fill="currentColor" d="M4 7V5a4 4 0 1 1 8 0v2h1v8H3V7h1zm2 0h4V5a2 2 0 1 0-4 0v2z"/></svg>';
 const field=(label,value,cls='')=>`<div class="fld ${cls}"><label>${label}</label><div class="in">${value}<span class="lk">${lock}</span></div></div>`;
 return `<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Checkout</title><style>
*{box-sizing:border-box}html,body{margin:0;height:100%}
body{font-family:"Segoe UI",Roboto,Arial,sans-serif;color:#f5f5f5;background:rgba(10,12,18,.92);display:flex;align-items:center;justify-content:center;padding:16px;user-select:none;-webkit-font-smoothing:antialiased}
.win{width:min(980px,100%);max-height:100%;display:flex;flex-direction:column;background:#18191d;border-radius:8px;box-shadow:0 24px 80px rgba(0,0,0,.6);overflow:hidden}
.bar{display:flex;align-items:center;gap:14px;padding:14px 22px;background:#121316;border-bottom:1px solid #2a2c33}
.brand{font:800 13px/1 "Segoe UI",Arial;letter-spacing:3px;color:#9ea3ad}.brand b{color:#fff}
.title{font:700 15px "Segoe UI",Arial;letter-spacing:2px;text-transform:uppercase}
.user{margin-left:auto;display:flex;align-items:center;gap:9px;font-size:13px;color:#c9ccd3}.av{width:28px;height:28px;border-radius:50%;background:#3b6fe0;display:grid;place-items:center;font-weight:700;color:#fff}
.x{margin-left:10px;width:30px;height:30px;border:0;border-radius:4px;background:transparent;color:#9ea3ad;font-size:20px;cursor:pointer}.x:hover{background:#2a2c33;color:#fff}
.main{display:grid;grid-template-columns:1.35fr 1fr;min-height:0;overflow:auto}
.l{padding:22px 26px}.r{padding:22px 24px;background:#202127;border-left:1px solid #2a2c33;display:flex;flex-direction:column}
h2{margin:0 0 12px;font:700 12px "Segoe UI",Arial;letter-spacing:1.6px;text-transform:uppercase;color:#9ea3ad}
.pm{display:flex;align-items:center;gap:12px;padding:12px 14px;border:1px solid #2f3139;border-radius:6px;margin-bottom:8px;background:#1d1e23}
.pm.on{border-color:#3b82f6;background:#1d2433}.pm.off{opacity:.45}
.rd{width:16px;height:16px;border-radius:50%;border:2px solid #6b7080;flex:none}.on .rd{border:5px solid #3b82f6}
.ic{width:38px;height:26px;border-radius:4px;flex:none;display:grid;place-items:center;font:800 9px Arial;color:#fff}
.ic.card{background:linear-gradient(135deg,#ffd400,#f08a00);color:#1b1b1b}.ic.new{background:#3a3d46}.ic.wal{background:#2f4b7a}.ic.gift{background:#6a3c8f}
.pm .t{font-size:14px;font-weight:600}.pm .s{font-size:12px;color:#9ea3ad;margin-top:2px}.pm .tag{margin-left:auto;font-size:11px;padding:2px 8px;border-radius:10px;background:#2a2c33;color:#c9ccd3}.pm.on .tag{background:#23406f;color:#cfe0ff}
.det{margin:14px 0 4px;padding:16px;border:1px solid #2f3139;border-radius:6px;background:#1b1c21}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.fld.w{grid-column:1/-1}
.fld label{display:block;font-size:11px;letter-spacing:.6px;text-transform:uppercase;color:#8b909b;margin-bottom:5px}
.in{position:relative;padding:10px 32px 10px 12px;border-radius:4px;background:#121316;border:1px solid #2f3139;font-size:14px;color:#d9dbe0;letter-spacing:.5px}
.lk{position:absolute;right:10px;top:50%;transform:translateY(-50%);color:#6b7080}
.demo{margin-top:12px;font-size:12px;color:#9ea3ad;display:flex;gap:8px;align-items:flex-start}.demo b{color:#ffd400}
.chk{display:flex;gap:8px;align-items:center;margin-top:12px;font-size:13px;color:#9ea3ad}.box{width:16px;height:16px;border-radius:3px;background:#3b82f6;display:grid;place-items:center;color:#fff;font-size:11px;opacity:.6}
.item{display:flex;gap:14px;align-items:center;padding-bottom:16px;border-bottom:1px solid #2f3139}
.art{width:84px;height:84px;border-radius:6px;flex:none;display:grid;place-items:center;background:radial-gradient(circle at 50% 35%,#3e7fd8,#1c3f7d 70%)}
.vb{width:54px;height:54px;border-radius:50%;display:grid;place-items:center;font:900 28px Impact,"Arial Black",sans-serif;font-style:italic;color:#fff;background:radial-gradient(circle at 35% 30%,#8fe0ff,#2f8ee8 55%,#1a54a8);box-shadow:inset 0 -4px 0 rgba(0,0,0,.25),0 0 0 3px #a7e3ff55}
.vb.sm{width:24px;height:24px;font-size:14px;box-shadow:none}
.in1{font-size:16px;font-weight:700}.in2{font-size:12px;color:#ffd400;margin-top:3px;font-weight:600}.in3{font-size:12px;color:#9ea3ad;margin-top:3px}
.picks{display:grid;gap:6px;margin:12px 0 4px}.pick{display:flex;align-items:center;gap:10px;padding:8px 10px;border:1px solid #2f3139;border-radius:5px;cursor:pointer;font-size:13px}.pick.on{border-color:#3b82f6;background:#1d2433}.pick input{accent-color:#3b82f6}.pn em{color:#ffd400;font-style:normal;font-size:11px;margin-left:4px}.pp{margin-left:auto;color:#c9ccd3}
.ln{display:flex;justify-content:space-between;font-size:14px;color:#c9ccd3;padding:6px 0}.ln.cr{color:#4ade80}
.tot{display:flex;justify-content:space-between;align-items:baseline;border-top:1px solid #2f3139;margin-top:8px;padding-top:14px}.tot span{font-weight:700;font-size:15px}.tot b{font-size:26px}
.legal{font-size:11px;color:#8b909b;line-height:1.5;margin:14px 0}
.go{width:100%;padding:15px;border:0;border-radius:4px;background:#0f6fe6;color:#fff;font:700 15px "Segoe UI",Arial;letter-spacing:1.5px;text-transform:uppercase;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:10px}
.go:hover{background:#2a84f5}.go:disabled{background:#2a3f63;color:#9fb3d6;cursor:default}
.cancel{margin-top:10px;width:100%;padding:11px;border:1px solid #3a3d46;border-radius:4px;background:transparent;color:#c9ccd3;font:600 13px "Segoe UI",Arial;letter-spacing:1px;text-transform:uppercase;cursor:pointer}.cancel:hover{background:#2a2c33}
.spin{width:16px;height:16px;border-radius:50%;border:2px solid #9fb3d6;border-top-color:transparent;animation:s .8s linear infinite;display:none}.busy .spin{display:block}@keyframes s{to{transform:rotate(360deg)}}
.err{display:none;margin-top:10px;padding:9px 12px;border-radius:4px;background:#4a1b24;border:1px solid #9b3445;color:#ffd6de;font-size:13px}
.secure{margin-top:auto;padding-top:14px;font-size:11px;color:#8b909b;display:flex;gap:6px;align-items:center}
.done{display:none;padding:44px 30px 36px;text-align:center}
.ok{width:72px;height:72px;margin:0 auto 18px;border-radius:50%;background:#16a34a;display:grid;place-items:center;font-size:38px;color:#fff}
.done h3{margin:0;font:700 26px "Segoe UI",Arial}.done .sub{color:#c9ccd3;margin:8px 0 22px;font-size:15px}
.rcpt{max-width:430px;margin:0 auto 22px;text-align:left;border:1px solid #2f3139;border-radius:6px;background:#1b1c21;padding:6px 16px}
.rcpt .ln{border-bottom:1px solid #26282e}.rcpt .ln:last-child{border:0}
.done .go{max-width:430px;margin:0 auto}
@media (max-width:700px){.main{grid-template-columns:1fr}.r{border-left:0;border-top:1px solid #2a2c33}}
</style></head><body><div class="win">
<div class="bar"><span class="brand"><b>SYNDICATE</b> STORE</span><span class="title">Checkout</span><span class="user"><span class="av">${esc((name||'P').slice(0,1).toUpperCase())}</span>Signed in as <b>${who}</b></span><button class="x" id="x" title="Close">&times;</button></div>
<div id="shop" class="main">
<div class="l">
<h2>Payment methods</h2>
<div class="pm on"><span class="rd"></span><span class="ic card">SYN</span><div><div class="t">Syndicate Card</div><div class="s">&bull;&bull;&bull;&bull; 0000 &middot; Expires 12/99</div></div><span class="tag">Saved</span></div>
<div class="pm off"><span class="rd"></span><span class="ic new">+</span><div><div class="t">New credit or debit card</div><div class="s">Not available on Syndicate</div></div></div>
<div class="pm off"><span class="rd"></span><span class="ic wal">&#9673;</span><div><div class="t">Digital wallet</div><div class="s">Not available on Syndicate</div></div></div>
<div class="pm off"><span class="rd"></span><span class="ic gift">&#9733;</span><div><div class="t">Gift card</div><div class="s">Not available on Syndicate</div></div></div>
<div class="det"><div class="grid">
${field('Name on card',who,'w')}
${field('Card number','&bull;&bull;&bull;&bull; &bull;&bull;&bull;&bull; &bull;&bull;&bull;&bull; 0000','w')}
${field('Expiration','12 / 99')}${field('CVV','&bull;&bull;&bull;')}
${field('Billing country','United States')}${field('ZIP / postal code','00000')}
</div>
<div class="demo">${lock}<span>This is Syndicate's <b>demo card</b>. It is not a real card, its details cannot be changed, and it is never charged.</span></div>
</div>
<div class="chk"><span class="box">&#10003;</span>Save this payment method for future purchases</div>
</div>
<div class="r">
<h2>Order summary</h2>
<div class="item"><div class="art"><span class="vb">V</span></div><div><div class="in1" id="iname">Choose a pack</div><div class="in2" id="ibonus"></div><div class="in3">Fortnite &middot; Digital currency</div></div></div>
<div class="picks" id="picks"${pack?' style="display:none"':''}>${packs}</div>
<div style="margin-top:12px">
<div class="ln"><span>Price</span><span id="lprice">-</span></div>
<div class="ln cr"><span>Syndicate Card credit</span><span id="lcredit">-</span></div>
<div class="ln"><span>Sales tax</span><span>$0.00</span></div>
<div class="tot"><span>Total</span><b>$0.00</b></div>
</div>
<div class="legal">By clicking Place Order you confirm this Syndicate purchase. The V-Bucks are added to the account you are signed in with. No real money is used and no payment details are collected.</div>
<button class="go" id="go" disabled><span class="spin"></span><span id="golabel">Place order</span></button>
<button class="cancel" id="cancel">Cancel</button>
<div class="err" id="err"></div>
<div class="secure">${lock} Payments on Syndicate are simulated. Nothing leaves this PC.</div>
</div></div>
<div class="done" id="done"><div class="ok">&#10003;</div><h3>Thanks for your order!</h3><div class="sub" id="dsub"></div>
<div class="rcpt"><div class="ln"><span>Order number</span><span id="dorder"></span></div><div class="ln"><span>Date</span><span id="ddate"></span></div><div class="ln"><span>Paid with</span><span>Syndicate Card &bull;&bull;&bull;&bull; 0000</span></div><div class="ln"><span>Total</span><span>$0.00</span></div><div class="ln"><span>V-Bucks balance</span><span id="dbal"></span></div></div>
<button class="go" id="back">Back to Fortnite</button></div>
</div><script>
const S=${state};
const $=id=>document.getElementById(id);
// The game's checkout browser exposes its purchase bridge (UPurchaseFlowJSBridge: Receipt, RequestClose) on window.ue.
const has=(o,name)=>!!o&&(typeof o[name]==='function'||typeof o[name[0].toUpperCase()+name.slice(1)]==='function'||Object.keys(o).some(n=>n.toLowerCase()===name));
function bridge(){const ue=window.ue;if(!ue)return null;const list=[ue.purchaseflow,ue.purchaseflowjsbridge,ue.epicpurchaseflow];try{for(const k of Object.keys(ue))list.push(ue[k]);}catch(e){}return list.find(o=>has(o,'receipt'))||null;}
function call(o,name,arg){const f=o[name]||o[name[0].toUpperCase()+name.slice(1)]||o[Object.keys(o).find(n=>n.toLowerCase()===name)];if(typeof f==='function')return f.call(o,arg);}
function waitBridge(ms){return new Promise(r=>{const t0=Date.now();(function look(){const b=bridge();if(b||Date.now()-t0>ms)return r(b);setTimeout(look,100);})();});}
const fmt=n=>n.toLocaleString('en-US');
function choose(id){S.pack=id;const p=S.packs[id];if(!p)return;$('iname').textContent=fmt(p.base)+' V-Bucks';$('ibonus').textContent=p.bonus?'+'+fmt(p.bonus)+' bonus V-Bucks':'';$('lprice').textContent=p.price;$('lcredit').textContent='-'+p.price;$('go').disabled=false;document.querySelectorAll('.pick').forEach(l=>l.classList.toggle('on',l.querySelector('input').value===id));}
document.querySelectorAll('.pick input').forEach(i=>i.addEventListener('change',()=>choose(i.value)));
if(S.pack)choose(S.pack);
let finished=false;
async function close(info){const b=await waitBridge(400);if(b){try{call(b,'requestclose',info);return;}catch(e){}}try{window.close();}catch(e){}}
$('cancel').onclick=()=>close('UserCancelled');$('x').onclick=()=>close(finished?'':'UserCancelled');$('back').onclick=()=>close('');
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&!finished)close('UserCancelled');});
$('go').onclick=async()=>{
 if(!S.pack)return;const go=$('go');go.disabled=true;go.classList.add('busy');$('golabel').textContent='Processing payment';$('err').style.display='none';$('cancel').disabled=true;
 try{
  const wait=new Promise(r=>setTimeout(r,1400)); // a real checkout takes a moment
  const r=await fetch('/syndicate/checkout/confirm',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pack:S.pack,nonce:S.nonce,from:S.from})});
  const j=await r.json().catch(()=>({}));
  await wait;
  if(!r.ok)throw new Error(j.message||'Your order could not be completed.');
  finished=true;$('shop').style.display='none';$('done').style.display='block';
  $('dsub').textContent=fmt(j.quantity)+' V-Bucks have been added to your account.';
  $('dorder').textContent='SYN-'+String(j.receipt.receiptId).slice(0,10).toUpperCase();
  $('ddate').textContent=new Date().toLocaleDateString('en-US',{year:'numeric',month:'long',day:'numeric'});
  $('dbal').textContent=fmt(j.balance)+' V-Bucks';
  const b=await waitBridge(1500);
  if(b){try{call(b,'receipt',{transactionid:j.receipt.receiptId,transactionstate:'Purchased',offers:[{offernamespace:'fn',offerid:S.pack,quantity:1,items:[{itemid:S.pack,entitlementid:j.receipt.receiptId,validationinfo:j.receipt.receiptInfo}]}]});}catch(e){}}
 }catch(e){go.disabled=false;go.classList.remove('busy');$('golabel').textContent='Place order';$('cancel').disabled=false;$('err').textContent=e.message;$('err').style.display='block';}
};
</script></body></html>`;
}

app.get('/syndicate/checkout',local,async(req,res)=>{
 const from=String(req.query.from||'');
 const pack=packFor(String(req.query.pack||''))||packIn(from);
 let name='';
 try{const accountId=accountFor(from);if(accountId)name=(await User.findOne({accountId},{username:1}).lean())?.username||'';}catch{}
 console.log(`[Syndicate] V-Bucks checkout opened${pack?' for '+pack.appStoreId:''} (game asked for: ${unwrap(from).slice(0,240)||'nothing'})`);
 res.set('Cache-Control','no-store').type('html').send(page({pack,nonce:crypto.randomUUID(),from,name}));
});
app.post('/syndicate/checkout/confirm',local,async(req,res)=>{
 try{
  const accountId=accountFor(req.body?.from);
  const {pack,balance,receipt}=await grantVbucksPack(accountId,String(req.body?.pack||''),req.body?.nonce);
  console.log(`[Syndicate] V-Bucks checkout: ${pack.quantity} V-Bucks added to ${accountId} (balance ${balance}).`);
  res.json({ok:true,quantity:pack.quantity,balance,receipt:{receiptId:receipt.receiptId,receiptInfo:receipt.receiptInfo,appStoreId:receipt.appStoreId}});
 }catch(error){res.status(400).json({ok:false,message:error.message});}
});

export default app;
