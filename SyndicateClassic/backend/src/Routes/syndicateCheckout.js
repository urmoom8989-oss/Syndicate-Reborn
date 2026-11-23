// Syndicate's V-Bucks checkout (syndicateVbucksStore.js). Buying a V-Bucks pack in the STORE tab opens the game's
// checkout browser; SyndicateClient.dll points it here instead of Epic's store. There is nothing to fill in and
// nothing is paid: Confirm adds the pack's V-Bucks. Only this PC can use it (loopback), and it never reaches the
// internet, so it works the same in offline mode.
import express from 'express';
import crypto from 'node:crypto';
import {verifyToken} from '../User/tokenManager/tokenVerify.js';
import {VBUCKS_PACKS,packFor,packTitle,formatVbucks,formatPrice,grantVbucksPack,isLoopback,localAccount} from '../Utils/syndicateVbucksStore.js';

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
function page({pack,nonce,from}){
 const packs=VBUCKS_PACKS.map(p=>`<label class="pack${pack&&p.appStoreId===pack.appStoreId?' on':''}"><input type="radio" name="pack" value="${p.appStoreId}"${pack&&p.appStoreId===pack.appStoreId?' checked':''}><span class="coin sm">V</span><span class="pname">${esc(formatVbucks(p.quantity-p.bonus))}${p.bonus?` <em>+${esc(formatVbucks(p.bonus))} bonus</em>`:''}</span><span class="pprice">${formatPrice(p.cents)}</span></label>`).join('');
 const state=JSON.stringify({pack:pack?.appStoreId||'',nonce,from,packs:Object.fromEntries(VBUCKS_PACKS.map(p=>[p.appStoreId,{title:packTitle(p),quantity:p.quantity,base:p.quantity-p.bonus,bonus:p.bonus,price:formatPrice(p.cents)}]))}).replace(/</g,'\\u003c');
 return `<!DOCTYPE html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Checkout</title><style>
*{box-sizing:border-box}html,body{margin:0;height:100%}
body{font-family:"Segoe UI",Arial,sans-serif;color:#e9f0ff;background:radial-gradient(1200px 700px at 50% -10%,#2a5fb8 0%,#14336b 45%,#0a1a38 100%);display:flex;align-items:center;justify-content:center;padding:18px;user-select:none}
.card{width:min(760px,100%);background:#0e1f3f;border:1px solid #2b4c86;border-radius:10px;box-shadow:0 18px 60px rgba(0,0,0,.55);overflow:hidden}
.top{display:flex;align-items:center;gap:12px;padding:16px 22px;background:linear-gradient(90deg,#173a78,#0f2652);border-bottom:1px solid #2b4c86}
h1{margin:0;font:800 26px/1 Impact,"Arial Narrow Bold","Arial Black",sans-serif;letter-spacing:1px;text-transform:uppercase;font-style:italic}
.secure{margin-left:auto;font-size:12px;color:#9fb6e0;display:flex;gap:6px;align-items:center}.secure b{color:#7cf29b}
.body{display:grid;grid-template-columns:1.25fr 1fr;gap:0}
.col{padding:20px 22px}.col+.col{border-left:1px solid #22406f;background:#0b1934}
h2{margin:0 0 10px;font:700 13px/1.2 "Segoe UI",Arial;letter-spacing:1.5px;color:#8fb0e6;text-transform:uppercase}
.item{display:flex;align-items:center;gap:14px;padding:14px;border-radius:8px;background:#132a54;border:1px solid #2b4c86}
.coin{flex:none;width:58px;height:58px;border-radius:50%;display:grid;place-items:center;font:900 30px Impact,"Arial Black",sans-serif;color:#fff;font-style:italic;background:radial-gradient(circle at 35% 30%,#7fd8ff,#2f8ee8 55%,#1a54a8);box-shadow:inset 0 -4px 0 rgba(0,0,0,.25),0 0 0 3px #a7e3ff55}
.coin.sm{width:26px;height:26px;font-size:15px;box-shadow:none}
.iname{font:800 22px/1.1 Impact,"Arial Black",sans-serif;font-style:italic;text-transform:uppercase}.isub{font-size:12px;color:#9fb6e0;margin-top:3px}
.bonus{display:inline-block;margin-left:6px;padding:2px 7px;border-radius:4px;background:#ffd400;color:#1b1b1b;font:800 12px "Segoe UI",Arial;vertical-align:middle;font-style:normal}
.iprice{margin-left:auto;font:700 20px "Segoe UI",Arial}
.packs{display:grid;gap:8px;margin-top:12px}.pack{display:flex;align-items:center;gap:10px;padding:9px 12px;border-radius:7px;background:#102349;border:1px solid #23457c;cursor:pointer}
.pack.on{border-color:#ffd400;background:#16305f}.pack input{accent-color:#ffd400}.pname{font-weight:700}.pname em{color:#ffd400;font-style:normal;font-size:12px;margin-left:4px}.pprice{margin-left:auto;color:#cfe0ff}
.method{display:flex;align-items:center;gap:10px;padding:12px;border-radius:8px;border:2px solid #ffd400;background:#16305f;margin-bottom:12px}
.method .dot{width:16px;height:16px;border-radius:50%;border:4px solid #ffd400;background:#0e1f3f}.method small{display:block;color:#9fb6e0;font-size:12px}
.fields{position:relative;display:grid;grid-template-columns:1fr 1fr;gap:8px;opacity:.45}
.fields input{width:100%;padding:10px;border-radius:6px;border:1px solid #2b4c86;background:#0a1630;color:#9fb6e0;font-size:14px}.fields .wide{grid-column:1/-1}
.locked{margin-top:8px;font-size:12px;color:#9fb6e0;display:flex;gap:6px;align-items:center}
.row{display:flex;justify-content:space-between;padding:7px 0;color:#cfe0ff;font-size:15px}.row.disc{color:#7cf29b}
.total{display:flex;justify-content:space-between;align-items:baseline;border-top:1px solid #2b4c86;margin-top:8px;padding-top:12px}
.total span{font:800 15px "Segoe UI",Arial;letter-spacing:1px;text-transform:uppercase}.total b{font:900 34px Impact,"Arial Black",sans-serif;font-style:italic;color:#ffd400}
.btns{display:flex;gap:10px;margin-top:18px}
button{flex:1;padding:14px 10px;border-radius:6px;border:0;cursor:pointer;font:900 20px Impact,"Arial Black",sans-serif;font-style:italic;letter-spacing:1px;text-transform:uppercase}
.cancel{background:transparent;color:#cfe0ff;border:2px solid #3b63a6}.cancel:hover{background:#16305f}
.go{background:#ffd400;color:#121212;box-shadow:0 4px 0 #b39500}.go:hover{background:#ffe14d}.go:disabled{opacity:.6;cursor:default}
.fine{padding:10px 22px;font-size:11px;color:#7f97c4;border-top:1px solid #22406f;background:#0a1730}
.err{display:none;margin-top:10px;padding:9px 12px;border-radius:6px;background:#5a1630;border:1px solid #a8344f;color:#ffd6de;font-size:13px}
.done{display:none;padding:46px 26px;text-align:center}.done .coin{width:92px;height:92px;font-size:48px;margin:0 auto 16px}
.done h3{margin:0;font:900 34px Impact,"Arial Black",sans-serif;font-style:italic;text-transform:uppercase}.done p{color:#cfe0ff;font-size:16px;margin:10px 0}.done .go{max-width:320px;margin:18px auto 0;display:block;width:100%}
@media (max-width:640px){.body{grid-template-columns:1fr}.col+.col{border-left:0;border-top:1px solid #22406f}}
</style></head><body><div class="card">
<div class="top"><h1>Checkout</h1><div class="secure">&#128274; <b>Free</b> Syndicate checkout</div></div>
<div id="shop"><div class="body"><div class="col">
<h2>Your order</h2>
<div class="item"><span class="coin">V</span><div><div class="iname" id="iname">Choose a pack</div><div class="isub">Fortnite &middot; V-Bucks</div></div><div class="iprice" id="iprice"></div></div>
<div class="packs" id="packs"${pack?' style="display:none"':''}>${packs}</div>
<h2 style="margin-top:18px">Payment method</h2>
<div class="method"><span class="dot"></span><div><b>Syndicate Free Pass</b><small>No payment needed. Nothing is charged.</small></div></div>
<div class="fields" aria-disabled="true"><input class="wide" disabled placeholder="Card number"><input disabled placeholder="MM / YY"><input disabled placeholder="CVV"><input class="wide" disabled placeholder="Name on card"></div>
<div class="locked">&#128683; Card details are turned off on this checkout.</div>
</div><div class="col">
<h2>Summary</h2>
<div class="row"><span>Price</span><span id="sprice">-</span></div>
<div class="row disc"><span>Syndicate discount</span><span id="sdisc">-</span></div>
<div class="row"><span>Sales tax</span><span>$0.00</span></div>
<div class="total"><span>You pay</span><b>$0.00</b></div>
<div class="btns"><button class="cancel" id="cancel">Cancel</button><button class="go" id="go" disabled>Confirm</button></div>
<div class="err" id="err"></div>
</div></div></div>
<div class="done" id="done"><span class="coin">V</span><h3>Purchase complete</h3><p id="dmsg"></p><p id="dbal" style="color:#9fb6e0;font-size:14px"></p><button class="go" id="back">Back to Fortnite</button></div>
<div class="fine">This is Syndicate's free checkout: no real money is used and no payment details are asked for. Your V-Bucks go to the account you are playing on.</div>
</div><script>
const S=${state};
const $=id=>document.getElementById(id);
// The game's checkout browser exposes its purchase bridge (UPurchaseFlowJSBridge: Receipt, RequestClose) on window.ue.
const has=(o,name)=>!!o&&(typeof o[name]==='function'||typeof o[name[0].toUpperCase()+name.slice(1)]==='function'||Object.keys(o).some(n=>n.toLowerCase()===name));
function bridge(){const ue=window.ue;if(!ue)return null;const list=[ue.purchaseflow,ue.purchaseflowjsbridge,ue.epicpurchaseflow];try{for(const k of Object.keys(ue))list.push(ue[k]);}catch(e){}return list.find(o=>has(o,'receipt'))||null;}
function call(o,name,arg){const f=o[name]||o[name[0].toUpperCase()+name.slice(1)]||o[Object.keys(o).find(n=>n.toLowerCase()===name)];if(typeof f==='function')return f.call(o,arg);}
function waitBridge(ms){return new Promise(r=>{const t0=Date.now();(function look(){const b=bridge();if(b||Date.now()-t0>ms)return r(b);setTimeout(look,100);})();});}
function choose(id){S.pack=id;const p=S.packs[id];if(!p)return;$('iname').innerHTML=p.base.toLocaleString('en-US')+' V-Bucks'+(p.bonus?' <span class="bonus">+'+p.bonus.toLocaleString('en-US')+' BONUS</span>':'');$('iprice').textContent=p.price;$('sprice').textContent=p.price;$('sdisc').textContent='-'+p.price;$('go').disabled=false;document.querySelectorAll('.pack').forEach(l=>l.classList.toggle('on',l.querySelector('input').value===id));}
document.querySelectorAll('.pack input').forEach(i=>i.addEventListener('change',()=>choose(i.value)));
if(S.pack)choose(S.pack);
async function close(info){const b=await waitBridge(400);if(b){try{call(b,'requestclose',info);return;}catch(e){}}try{window.close();}catch(e){}}
$('cancel').onclick=()=>close('UserCancelled');
$('back').onclick=()=>close('');
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&$('done').style.display!=='block')close('UserCancelled');});
$('go').onclick=async()=>{
 if(!S.pack)return;const go=$('go');go.disabled=true;go.textContent='Processing...';$('err').style.display='none';
 try{
  const r=await fetch('/syndicate/checkout/confirm',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({pack:S.pack,nonce:S.nonce,from:S.from})});
  const j=await r.json().catch(()=>({}));
  if(!r.ok)throw new Error(j.message||'The purchase did not go through.');
  $('shop').style.display='none';$('done').style.display='block';
  $('dmsg').textContent=j.quantity.toLocaleString('en-US')+' V-Bucks were added to your account.';
  $('dbal').textContent='You now have '+j.balance.toLocaleString('en-US')+' V-Bucks.';
  const b=await waitBridge(1500);
  if(b){try{call(b,'receipt',{transactionid:j.receipt.receiptId,transactionstate:'Purchased',offers:[{offernamespace:'fn',offerid:S.pack,quantity:1,items:[{itemid:S.pack,entitlementid:j.receipt.receiptId,validationinfo:j.receipt.receiptInfo}]}]});}catch(e){}}
  else $('back').textContent='Done - go back to Fortnite';
 }catch(e){go.disabled=false;go.textContent='Confirm';$('err').textContent=e.message;$('err').style.display='block';}
};
</script></body></html>`;
}

app.get('/syndicate/checkout',local,(req,res)=>{
 const from=String(req.query.from||'');
 const pack=packFor(String(req.query.pack||''))||packIn(from);
 console.log(`[Syndicate] V-Bucks checkout opened${pack?' for '+pack.appStoreId:''} (game asked for: ${unwrap(from).slice(0,240)||'nothing'})`);
 res.set('Cache-Control','no-store').type('html').send(page({pack,nonce:crypto.randomUUID(),from}));
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
