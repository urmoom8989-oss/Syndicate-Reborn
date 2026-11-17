#pragma once

struct FConfiguration
{
    static inline auto Playlist = L"/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo";
    // Server tick rate. Default 30; the launcher may pass TILTED_TICK_RATE (20-60),
    // e.g. 60 for light local matches so inputs are processed twice as often.
    static inline int MaxTickRate = [] {
        wchar_t Text[8]{};wchar_t* End=nullptr;
        if(GetEnvironmentVariableW(L"TILTED_TICK_RATE",Text,8)){
            const long Value=wcstol(Text,&End,10);
            if(End && !*End && Value>=20 && Value<=60)return int(Value);
        }
        return 30;
    }();
    // Late game: the launcher passes TILTED_LATE_GAME=1 (Settings > Bots > Late game), and may pass
    // TILTED_LATE_GAME_ZONE (1-7) for the circle the match starts in. The ImGui checkbox still works too.
    static inline bool bLateGame = [] {
        wchar_t Text[8]{};
        return GetEnvironmentVariableW(L"TILTED_LATE_GAME", Text, 8) && Text[0] == L'1';
    }();
    // Centre of the late-game circle, set when the bus route is laid out (the bots land around it).
    static inline bool bLateGameCenterSet = false;
    static inline float LateGameCenterX = 0.f, LateGameCenterY = 0.f;
    // Builds without a storm location list (1.7.2): the circle is the game's own; the centre above is replaced by
    // the storm's real centre once the storm reaches the late-game phase (bLateGameCenterAdopted), and bumped
    // LateGameCenterVersion tells the bots to pick their landing spots again.
    static inline bool bLateGameAdoptStormCenter = false, bLateGameCenterAdopted = false;
    static inline int LateGameCenterVersion = 0;
    static inline auto bLGV2 = true;
    static inline int LateGameZone = [] { // starting zone
        wchar_t Text[8]{};wchar_t* End=nullptr;
        if(GetEnvironmentVariableW(L"TILTED_LATE_GAME_ZONE",Text,8)){
            const long Value=wcstol(Text,&End,10);
            if(End && !*End && Value>=1 && Value<=7)return int(Value);
        }
        return 4; // late game starts in the fourth circle
    }();
    static inline auto bLateGameLongZone = false; // zone doesnt close for a long time
    static inline auto bEnableCheats = true;
    static inline auto SiphonAmount = 50; // set to 0 to disable
    static inline auto bInfiniteMats = false;
    static inline auto bInfiniteAmmo = false;
    static inline auto bForceRespawns = false; // build your client with this too!
    static inline auto bJoinInProgress = false;
    static inline auto bAutoRestart = false;
    static inline auto bKeepInventory = false;
    // Diagnostics must never occupy the public match port.
    static inline int Port = [] {
        if (GetEnvironmentVariableW(L"TILTED_BOT_SELFTEST", nullptr, 0)) {
            wchar_t Text[16]{};wchar_t* End=nullptr;
            if(GetEnvironmentVariableW(L"TILTED_TEST_PORT",Text,16)) {
                const long Value=wcstol(Text,&End,10);
                if(End && !*End && Value>=17000 && Value<=65535)return int(Value);
            }
            SetEnvironmentVariableW(L"TILTED_BOT_SELFTEST",nullptr);
            SetEnvironmentVariableW(L"TILTED_MULTIPLAYER_DIAGNOSTICS",nullptr);
        }
        // Fast queue: the launcher keeps the next match loading in the background on a second game port
        // (7777/7778) while the current match runs, so Ready joins at once. Only 7777-7799 are accepted.
        {
            wchar_t Text[16]{};wchar_t* End=nullptr;
            if(GetEnvironmentVariableW(L"TILTED_SERVER_PORT",Text,16)) {
                const long Value=wcstol(Text,&End,10);
                if(End && !*End && Value>=7777 && Value<=7799)return int(Value);
            }
        }
        return 7777;
    }();
    static inline constexpr auto bEnableIris = true; 
    static inline constexpr auto bGUI = true;
    static inline constexpr auto bCustomCrashReporter = true;
    static inline constexpr auto bUseStdoutLog = true;
    static inline constexpr auto WebhookURL = ""; // fill in if you want status to send to a webhook
};
