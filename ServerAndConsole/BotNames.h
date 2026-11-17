#pragma once
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

inline const std::vector<std::string> DefaultBotDisplayNames = {
    "ObsessedTech.",    "Timeless.",       "Grampa_Joe",     "Agua_Cat",         "AGenuineHuman",  "NooooMyBueno",
    "Pongo_x8.6",       "xx-AnimeGirl-xx", "90cranker",      "ColonelChugJug",   "Gatordile81",    "JonseyForever35",
    "PositiveFeels",    "TimeToGo80",      "QueenBeet74",    "AimLikeIdaho",     "CrazyPea96",     "GetItGotItGood",
    "JustABitEpic",     "PrancingPwnee",   "TooManyBeets",   "MintElephant26",   "AngryDuck51",    "CrepeSalad",
    "GhostChicken12",   "KittyCat80",      "PrinceWombat",   "WalkInThePark66",  "BliceCake",      "AthenaOrApollo",
    "DoctorLobby92",    "Gooddoggo80",     "Kregore73",      "SergentSummer",    "WildCactusBob",  "BraunyBanana",
    "AtTheBeach321",    "DoubleDaring",    "Goosezilla13",   "LetsBePals23",     "ShadowArrow58",  "Wondertail",
    "SoggyCookie26",    "BagelBoy82",      "DoubleDuel75",   "Grandma40",        "LewtGoblin7",    "Shepard52",
    "Yeetman57",        "AboveMule633",    "BellyFlop40",    "DoubleRainbow96",  "HashtagToad57",  "McCucumber71",
    "ShieldHorse63",    "Beebitme",        "PurpleCrayon85", "Blackjack31",      "DrPlanet",       "HeliumHog",
    "Meshuncle",        "ShootyMcGee40",   "SweetPenguin16", "SpiffyPowder6866", "BlinkImGone44",  "DrumGunnar",
    "HeyThereFriend81", "Mouthful95",      "SilverySilver",  "PortableOx",       "LootTrooper51",  "BoatingIsLife",
    "ElPollo85",        "Hoodwinked12",    "N0nDa1ry",       "SirTricksALot21",  "A_Sweaty_Dog",   "LousyCentaur",
    "BoldPrediction",   "FlavorCaptain",   "HotelBlankets",  "NotAPalidome",     "SteelGoose18",   "&darkBeast&",
    "Notapiggy",        "BrainInvader",    "FlimsyGoat",     "HowAreMy90s",      "Number141",      "TAgYOuRIt9",
    "BobDobaleena",     "CactusDad80",     "FlossPatrol82",  "iHazHighGround",   "ParanoidCactus", "ThermalDragon39",
    "OldWaterBottle28", "LazarLazar.",     "SypherPK.",      "Tejo-TV.",         "billoute",       "Specx",
    "Charles",          "NickVX"};

inline std::vector<std::string> BotDisplayNames = DefaultBotDisplayNames;

// The launcher sends the full name roster in SYNDICATE_BOT_NAMES, separated by '|'.
// Without it, the built-in names above are used.
inline int ConfigureBotNames()
{
    BotDisplayNames = DefaultBotDisplayNames;
    constexpr DWORD Capacity = 4096;
    wchar_t Buffer[Capacity]{};
    DWORD Length = GetEnvironmentVariableW(L"SYNDICATE_BOT_NAMES", Buffer, Capacity);
    if (Length == 0 || Length >= Capacity) return 0;

    int Bytes = WideCharToMultiByte(CP_UTF8, 0, Buffer, static_cast<int>(Length), nullptr, 0, nullptr, nullptr);
    if (Bytes <= 0) return 0;
    std::string Encoded(static_cast<size_t>(Bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, Buffer, static_cast<int>(Length), Encoded.data(), Bytes, nullptr, nullptr);

    std::vector<std::string> Custom;
    std::unordered_set<std::string> Seen;
    size_t At = 0;
    while (At <= Encoded.size())
    {
        size_t End = Encoded.find('|', At);
        std::string Name = Encoded.substr(At, End == std::string::npos ? std::string::npos : End - At);
        size_t First = Name.find_first_not_of(" \t\r\n");
        if (First != std::string::npos)
        {
            size_t Last = Name.find_last_not_of(" \t\r\n");
            Name = Name.substr(First, Last - First + 1);
            std::string Key = Name;
            std::transform(Key.begin(), Key.end(), Key.begin(),
                [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
            if (!Key.empty() && Seen.insert(Key).second) Custom.push_back(Name);
        }
        if (End == std::string::npos) break;
        At = End + 1;
    }
    if (Custom.empty()) return 0;

    BotDisplayNames = Custom;
    return static_cast<int>(Custom.size());
}