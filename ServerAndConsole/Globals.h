#pragma once
namespace Globals
{
inline bool bIsProdServer = false;
inline bool bCreativeEnabled = false, bSTWEnabled = false, bEventEnabled = false, bBotsEnabled = true;
inline int MinBotsToSpawn = 99, MaxBotsToSpawn = 99, MinPlayersForEarlyStart = 95;
inline int NextTeamIndex = 3, CurrentPlayersOnTeam = 0, MaxPlayersPerTeam = 1;
inline int NextBotTeamIndex = 102, CurrentBotsOnTeam = 0;
inline void ResetTeams()
{
    CurrentPlayersOnTeam = 0;
    CurrentBotsOnTeam = 0;
    NextTeamIndex = 3;
    NextBotTeamIndex = 102;
}
} // namespace Globals
