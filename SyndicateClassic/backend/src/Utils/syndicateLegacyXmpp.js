import net from 'node:net';
import tls from 'node:tls';
import {readFileSync} from 'node:fs';
import {StringDecoder} from 'node:string_decoder';
import {WebSocket} from 'ws';
import sax from 'sax';
// Self-signed, loopback only (the adapter listens on 127.0.0.1). The launcher installs this exact
// certificate into 1.7.2/1.8.x as a trusted root (LegacyXmppTrust.cs); keep both in sync.
// No built-in key ships with the source. Put your own key.pem/cert.pem in Runtime/Server/legacy-xmpp-tls
// (and trust that cert in the 1.7.2/1.8.2 builds) to enable TLS for those seasons.

const escape=value=>String(value).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;');
// Text content is re-encoded only where XML requires it. The XMPP server's parser (xml-parser) does not
// decode entities, so quotes encoded as &quot; reached it literally: the JSON in every Pre-season /
// Season 1 presence status and party message failed to parse and was dropped (friends showed as
// "Online" with no party, parties as PRIVATE, invites never arrived). Quotes are legal in XML text.
export const escapeText=value=>String(value).replaceAll('&','&amp;').replaceAll('<','&lt;');
export function startLegacyXmpp({port=7071,upstream='ws://127.0.0.1:70',host='127.0.0.1',tlsOptions=null}={}){
 let secureContext=null;
 // Use Runtime/Server/legacy-xmpp-tls/{key,cert}.pem when present; there is no built-in certificate. Without TLS, 1.7.2/1.8.2 never send their login ("XMPP log-in credentials not valid (6)").
 let fileOptions=null;try{fileOptions={key:readFileSync(new URL('../../../Runtime/Server/legacy-xmpp-tls/key.pem',import.meta.url)),cert:readFileSync(new URL('../../../Runtime/Server/legacy-xmpp-tls/cert.pem',import.meta.url))};}catch{}
 try {const options=tlsOptions||fileOptions;if(!options)throw new Error('no key.pem/cert.pem in Runtime/Server/legacy-xmpp-tls');// 1.7.2/1.8.2 (OpenSSL 1.0.2, libjingle) only speak TLS 1.0, which Node refuses by default
  // ("unsupported protocol"). Allow it here; this listener is loopback only.
  secureContext=tls.createSecureContext({minVersion:'TLSv1',ciphers:'DEFAULT:@SECLEVEL=0',...options});}catch(error){console.warn('[Legacy XMPP] TLS setup failed: '+error.message);}

 const server=net.createServer(socket=>{
  console.log('[Legacy XMPP] connection');
  const ws=new WebSocket(upstream,'xmpp');const decoder=new StringDecoder('utf8');
  let queue=[],queued=0,stanza='',depth=0,closed=false;
  const legacyIds=new Map();let encrypted=false;
  const forwardStanza=text=>{
   if(/^<starttls\b/.test(text)){
    if(!secureContext || encrypted)return close();
    // Wrap the socket right away: waiting for the write callback dropped the game's TLS hello
    // (it arrived while nothing was listening), so the handshake never finished.
    socket.removeListener('data',receive);socket.pause();
    socket.write('<proceed xmlns="urn:ietf:params:xml:ns:xmpp-tls"/>');
    const secure=new tls.TLSSocket(socket,{isServer:true,secureContext,minVersion:'TLSv1'});socket=secure;
    secure.on('error',error=>{console.warn('[Legacy XMPP] TLS error: '+error.message);close();});secure.on('close',close);secure.on('data',receive);
    secure.on('secure',()=>{encrypted=true;console.log('[Legacy XMPP] encrypted transport established');});
    return;
   }

   const id=text.match(/^<iq\b[^>]*\bid="([^"]*)"/);
   const kind=/<bind\b[^>]*xmlns="urn:ietf:params:xml:ns:xmpp-bind"/.test(text)?'_xmpp_bind1':/<session\b[^>]*xmlns="urn:ietf:params:xml:ns:xmpp-session"/.test(text)?'_xmpp_session1':null;
   if(id&&kind){legacyIds.set(kind,id[1]);text=text.replace(/^(<iq\b[^>]*\bid=")[^"]*/,'$1'+kind);}
   send(text);
  };
  const close=()=>{if(closed)return;closed=true;socket.destroy();ws.terminate();};
  const send=text=>{if(ws.readyState===WebSocket.OPEN)ws.send(text);else{queued+=Buffer.byteLength(text);if(queued>1048576)return close();queue.push(text);}};
  const parser=sax.parser(true,{xmlns:false});
  parser.ondoctype=close;parser.onerror=()=>{console.warn('[Legacy XMPP] parse error');close();};
  parser.onopentag=node=>{
   if(depth===0)console.log('[Legacy XMPP] incoming '+node.name);
   if(node.name==='stream:stream'){send('<open xmlns="urn:ietf:params:xml:ns:xmpp-framing" to="prod.ol.epicgames.com" version="1.0"/>');return;}
   depth++;stanza+='<'+node.name+Object.entries(node.attributes).map(([k,v])=>' '+k+'="'+escape(v)+'"').join('')+'>';
   if(stanza.length>1048576)close();
  };
  parser.ontext=text=>{if(depth)stanza+=escapeText(text);if(stanza.length>1048576)close();};
  parser.oncdata=parser.ontext;
  parser.onclosetag=name=>{
   if(name==='stream:stream'){send('<close xmlns="urn:ietf:params:xml:ns:xmpp-framing"/>');return;}
   stanza+='</'+name+'>';if(--depth===0){forwardStanza(stanza);stanza='';}
  };
  const receive=chunk=>{try{parser.write(decoder.write(chunk));}catch{close();}};
  socket.on('data',receive);
  socket.on('error',close);socket.on('close',close);ws.on('error',close);ws.on('close',close);
  ws.on('open',()=>{for(const message of queue)ws.send(message);queue=[];queued=0;});
  ws.on('message',data=>{
   let text=data.toString();console.log('[Legacy XMPP] outgoing '+(text.match(/<(?!\?)([\w:]+)/)?.[1]||'unknown'));
   if(/^<\?xml/.test(text))text=text.replace(/^<\?xml[^>]*>\s*/,'');
   if(/^<open\b/.test(text))text=text.replace(/^<open\b/,'<stream:stream').replace('xmlns="urn:ietf:params:xml:ns:xmpp-framing"','xmlns="jabber:client" xmlns:stream="http://etherx.jabber.org/streams"').replace(/\/>\s*$/,'>');
   else if(/^<close\b/.test(text))text='</stream:stream>';
   // The adapter is loopback-only; do not advertise unsupported TLS/compression upgrades.
   if(!secureContext || encrypted)text=text.replace(/<starttls\b[^>]*(?:\/>|>[\s\S]*?<\/starttls>)/g,'');
   text=text.replace(/<compression\b[^>]*(?:\/>|>[\s\S]*?<\/compression>)/g,'');
   text=text.replace(/^(<iq\b[^>]*\bid=")(_xmpp_bind1|_xmpp_session1)(")/,(_,before,id,after)=>before+(legacyIds.get(id)||id)+after);
   if(!socket.destroyed)socket.write(text);
  });
 });
 server.on('error',error=>console.error('[Syndicate legacy XMPP]',error.code));server.listen(port,host);return server;
}
