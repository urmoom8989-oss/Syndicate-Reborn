// Fixed public messages avoid leaking local paths or exceeding WebSocket's 123-byte limit.
function publicReason(error){
 const s=String(error||'').toLowerCase();
 if(s.includes('different fortnite version'))return 'Wrong season selected. Host and players must select the same season in Syndicate.';
 if(s.includes('7777'))return 'Game port 7777 is busy. Close the other game server and retry.';
 if(s.includes('6969')||s.includes('another Syndicate'))return 'Another Syndicate copy is using matchmaking. Close it and retry.';
 if(s.includes('missing')||s.includes('incomplete'))return 'Required game or Syndicate files are missing. Check Syndicate for the file and repair steps.';
 if(s.includes('denied')||s.includes('cannot write'))return 'Syndicate cannot access its files. Move it to a writable folder and reopen it.';
 if(s.includes('players')||s.includes('occupancy'))return 'The host still has a match in progress. Wait for it to finish and press Ready again.';
 if(s.includes('exited'))return 'The game server stopped during startup. Automatic retry failed; restart Syndicate.';
 if(s.includes('loading')||s.includes('joinable')||s.includes('starting'))return 'The game server took too long to start. Close unused apps and restart Syndicate.';
 return 'Syndicate could not prepare this match. Open the launcher for the specific error and recovery steps.';
}
module.exports={publicReason};
