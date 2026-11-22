import express from "express";
import {clientBuild} from "../Utils/syndicateBuild.js";
import {modeImage,modeImages} from "../Utils/syndicateSeasonModes.js";
import {customNews,customTournaments,newsImageFile} from "../Utils/syndicateNewsSettings.js";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";
import Utils from "../Utils/Utils.js";

const app = express.Router();
const __dirname = path.dirname(fileURLToPath(import.meta.url));

app.get("/content/api/pages/*", async (req, res) => {
  const filePath = path.join(__dirname, "../local/ContentPages/Pages.json");
  const raw = fs.readFileSync(filePath, "utf-8");
  const Contentpages = JSON.parse(JSON.stringify(JSON.parse(raw)));

  const memory = Utils.GetVersion(req);
  const season = memory.season;
  const build = memory.build || "";

  let Language = "en";
  try {
    if (req.headers["accept-language"]) {
      Language = req.headers["accept-language"].includes("-") && req.headers["accept-language"] !== "es-419"
        ? req.headers["accept-language"].split("-")[0]
        : req.headers["accept-language"];
    }
  } catch {}

  if (!Contentpages.dynamicbackgrounds) {
    Contentpages.dynamicbackgrounds = {
      _title: "dynamicbackgrounds",
      _type: "DynamicBackgroundList",
      backgrounds: { backgrounds: [] }
    };
  }
  if (!Array.isArray(Contentpages.dynamicbackgrounds.backgrounds.backgrounds)) {
    Contentpages.dynamicbackgrounds.backgrounds.backgrounds = [];
  }

  const bgs = Contentpages.dynamicbackgrounds.backgrounds.backgrounds;

  while (bgs.length < 2) {
    bgs.push({ _type: "DynamicBackground", key: "lobby", stage: "season11" });
  }

  const mainStages = {10:"seasonx",11:"season11",12:"season12",13:"season13",14:"season14",15:"season15",16:"season16",17:"season17",18:"season18",19:"season19",20:"season20",21:"season21",22:"season22",23:"season23",24:"season24",25:"season25",26:"season26",27:"season27",28:"season28",29:"season29",30:"season30",31:"season31",32:"season32",33:"chapter6",34:"chapter6s2",35:"chapter6ms1",36:"chapter6s3",37:"chapter6s4"};
  const eventStages = {"11.10":"fortnitemares","11.30":"Galileo","11.31":"Galileo","11.40":"Winter19","11.50":"LoveAndWar","14.40":"halloween2020","15.10":"XmasStore2020","19.01":"winter2021","23.10":"winterfest2022","28.01":"winterfest2023","31.40":"fortnitemares2024","31.41":"fortnitemares2024","37.50":"fortnitemares2025","37.51":"fortnitemares2025"};

  bgs[0]._type = bgs[1]._type = "DynamicBackground";
  bgs[0].key = bgs[1].key = "lobby";
  bgs[0].stage = mainStages[season] || "";
  bgs[1].stage = eventStages[build] || bgs[0].stage;

  if(clientBuild(req.headers['user-agent'])==='4.5'){
    Contentpages.playlistinformation.playlist_info.playlists.push({playlist_name:'Playlist_50v50',hidden:false,special_border:'None',_type:'FortPlaylistInfo'});
    Contentpages.playlistinformation.playlist_info.playlists.push({playlist_name:'Playlist_Playground',hidden:false,special_border:'None',_type:'FortPlaylistInfo'});
  }
  // Arena Duos' event tile (Routes/events.js) uses the Solo Arena display info.
  const tournaments=Contentpages.tournamentinformation?.tournament_info?.tournaments;
  const soloInfo=Array.isArray(tournaments)&&tournaments.find(t=>t.tournament_display_id==='LG_ARENA_SOLO');
  if(soloInfo&&!tournaments.some(t=>t.tournament_display_id==='LG_ARENA_DUOS'))
    tournaments.push({...soloInfo,tournament_display_id:'LG_ARENA_DUOS'});
  // News and tournaments from the launcher (Settings > News & tournaments, Utils/syndicateNewsSettings.js).
  {
    const newsBuild=clientBuild(req.headers['user-agent']);
    const newsHost=String(req.socket.localAddress||'127.0.0.1').replace(/^::ffff:/,'');
    const newsBase='http://'+(newsHost.includes(':')?'['+newsHost+']':newsHost)+':'+req.socket.localPort;
    const picture=name=>name&&newsImageFile(name)?`${newsBase}/syndicate/newsimage/${encodeURIComponent(name)}`:'';
    const cups=customTournaments(newsBuild);
    if(cups.length&&Contentpages.tournamentinformation?.tournament_info){
      const list=Contentpages.tournamentinformation.tournament_info.tournaments??=[];
      const base=list.find(t=>t.tournament_display_id==='LG_ARENA_SOLO')||{};
      for(const t of cups){
        const art=picture(t.image)||base.poster_front_image||'';
        list.push({...base,_type:'Tournament Display Info',tournament_display_id:t.displayId,
          title_line_1:t.title,title_line_2:t.subtitle,long_format_title:[t.title,t.subtitle].filter(Boolean).join(' '),
          short_format_title:t.title,schedule_info:t.schedule,flavor_description:t.description,details_description:t.description,
          pin_earned_text:'',background_left_color:t.color,background_right_color:t.color,poster_fade_color:t.color,
          poster_front_image:art,poster_back_image:art,loading_screen_image:art,playlist_tile_image:art});
      }
    }
    const posts=[...customNews(newsBuild).map(n=>({...n,image:picture(n.image)})),
      ...cups.filter(t=>t.postInNews).map(t=>({id:t.displayId,title:t.title+(t.subtitle?' '+t.subtitle:''),
        body:[t.schedule,t.description,'Play it from the Compete tab.'].filter(Boolean).join('\n\n'),image:picture(t.image)}))];
    // Seasons 1 to 10 show news.messages; later ones (and 9.x/10.x's MOTD panel) show motds.
    if(posts.length){
      const now=new Date().toISOString();
      const messages=posts.map(p=>({image:p.image,hidden:false,messagetype:'normal',_type:'CommonUI Simple Message Base',adspace:'',title:p.title,body:p.body,spotlight:false}));
      const motds=posts.map((p,i)=>({entryType:'Text',image:p.image,tileImage:p.image,videoMute:false,hidden:false,tabTitleOverride:p.title.slice(0,20),
        _type:'CommonUI Simple Message MOTD',title:p.title,body:p.body,videoLoop:false,videoStreamingEnabled:false,sortingPriority:posts.length-i,
        id:'Syndicate_'+p.id+'_'+i,videoAutoplay:false,videoFullscreen:false,spotlight:false}));
      const page=title=>({_title:title,header:'',style:'None',_noIndex:false,alwaysShow:false,_activeDate:'2018-08-06T19:00:26.217Z',lastModified:now,_locale:'en-US'});
      Contentpages.battleroyalenews={...page('battleroyalenews'),news:{_type:'Battle Royale News',messages,motds}};
      Contentpages.battleroyalenewsv2={...page('battleroyalenewsv2'),news:{motds}};
    }
  }
  // Pictures chosen in the launcher's lobby editor go on the game's mode tiles, served by this backend.
  const pictures=modeImages(clientBuild(req.headers['user-agent']));
  if(!pictures.length)return res.json(Contentpages);
  const tileHost=String(req.socket.localAddress||'127.0.0.1').replace(/^::ffff:/,'');
  const tileBase='http://'+(tileHost.includes(':')?'['+tileHost+']':tileHost)+':'+req.socket.localPort;
  // Seasons 8 to X show Arena as the event's tile, which takes its picture from the tournament display info.
  const arenaInfo={arenasolo:'LG_ARENA_SOLO',arenaduos:'LG_ARENA_DUOS'};
  const tournamentList=(Contentpages.tournamentinformation?.tournament_info?.tournaments||[]).map(t=>{
    const p=pictures.find(p=>arenaInfo[p.mode]===t.tournament_display_id);
    if(!p)return t;
    let stamp=0;try{stamp=Math.round(fs.statSync(p.file).mtimeMs);}catch{}
    return {...t,playlist_tile_image:`${tileBase}/syndicate/modeimage/${encodeURIComponent(clientBuild(req.headers['user-agent']))}/${p.mode}?v=${stamp}`};
  });
  const info=Contentpages.playlistinformation?.playlist_info;
  const host=String(req.socket.localAddress||'127.0.0.1').replace(/^::ffff:/,'');
  const base='http://'+(host.includes(':')?'['+host+']':host)+':'+req.socket.localPort;
  const tileBuild=clientBuild(req.headers['user-agent']);
  const named=new Set(pictures.map(p=>p.playlist.toLowerCase()));
  const playlists=(info?.playlists||[]).filter(p=>!named.has(String(p.playlist_name).toLowerCase()));
  for(const p of pictures){
    let stamp=0;try{stamp=Math.round(fs.statSync(p.file).mtimeMs);}catch{}
    playlists.push({image:`${base}/syndicate/modeimage/${encodeURIComponent(tileBuild)}/${p.mode}?v=${stamp}`,playlist_name:p.playlist,hidden:false,special_border:'None',_type:'FortPlaylistInfo'});
  }
  res.json({...Contentpages,tournamentinformation:{...Contentpages.tournamentinformation,tournament_info:{...Contentpages.tournamentinformation?.tournament_info,tournaments:tournamentList}},playlistinformation:{...Contentpages.playlistinformation,playlist_info:{...info,playlists}}});
});

app.get("/syndicate/newsimage/:name", (req, res) => {
  const file=newsImageFile(req.params.name);
  if(!file)return res.status(404).end();
  res.sendFile(file);
});

app.get("/syndicate/modeimage/:build/:mode", (req, res) => {
  const file=modeImage(req.params.build,req.params.mode);
  if(!file)return res.status(404).end();
  res.sendFile(file);
});

export default app;