#pragma once
#include <string_view>
#include <optional>
namespace MatchLaunchOptions
{
inline std::optional<int> Number(std::wstring_view Cmd,std::wstring_view Key)
{
    auto At=Cmd.find(Key);if(At==Cmd.npos|| (At&&Cmd[At-1]!=L' '))return {};
    At+=Key.size();auto End=Cmd.find_first_of(L" \t\"",At);auto S=Cmd.substr(At,End==Cmd.npos?End:End-At);
    if(S.empty()||S.size()>2)return {};int V=0;for(auto C:S){if(C<L'0'||C>L'9')return {};V=V*10+int(C-L'0');}return V;
}
struct Settings {int Minimum=99,Maximum=99;std::optional<bool> EditOnRelease;};
inline Settings Parse(std::wstring_view Cmd)
{
    Settings S;auto Low=Number(Cmd,L"-SyndicateBotsMin="),High=Number(Cmd,L"-SyndicateBotsMax=");
    if(Low&&High&&*Low<=*High){S.Minimum=*Low;S.Maximum=*High;}
    auto Edit=Number(Cmd,L"-SyndicateEditOnRelease=");if(Edit&&*Edit<=1)S.EditOnRelease=*Edit!=0;
    return S;
}
}
