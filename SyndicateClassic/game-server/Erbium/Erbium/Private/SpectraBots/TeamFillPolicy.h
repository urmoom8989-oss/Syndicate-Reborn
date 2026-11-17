#pragma once
#include <map>
#include <vector>
#include <algorithm>
#include <sstream>
#include <string>
namespace SyndicateTeamFill {
inline bool ReadSelection(std::istream& In,int Pid,long long Now,const std::string& Mode){
 std::string Line;int FilePid=0,Fill=0;long long Expires=0;std::string FileMode;
 while(std::getline(In,Line)){if(!Line.empty()&&Line.back()=='\r')Line.pop_back();auto S=Line.find('=');if(S==std::string::npos)continue;auto K=Line.substr(0,S),V=Line.substr(S+1);std::istringstream N(V);
  if(K=="pid")N>>FilePid;else if(K=="expires")N>>Expires;else if(K=="fill")N>>Fill;else if(K=="mode")FileMode=V;
 }
 return FilePid==Pid && Expires>Now && FileMode==Mode && Mode!="solo" && Fill==1;
}
struct Human {int Team;bool Fill;int Extra=0;}; // Extra: party bots that must join this human's team
struct Bot {int Team;int OriginalTeam;};
inline std::map<int,int> OpenSlots(const std::vector<Human>& Humans,int Size){
 std::map<int,int> Count,Extra;std::map<int,bool> Allowed;
 for(auto H:Humans){if(!Count.count(H.Team))Allowed[H.Team]=true;++Count[H.Team];Allowed[H.Team]=Allowed[H.Team]&&H.Fill;Extra[H.Team]+=H.Extra;}
 std::map<int,int> Slots;if(Size!=2&&Size!=4)return Slots;
 for(auto C:Count){if(C.second>=Size)continue;
  if(Allowed[C.first])Slots[C.first]=Size-C.second;
  else if(Extra[C.first]>0)Slots[C.first]=(std::min)(Size-C.second,Extra[C.first]);}
 return Slots;
}
inline std::vector<int> Plan(const std::vector<Human>& Humans,const std::vector<Bot>& Bots,int Size){
 auto Slots=OpenSlots(Humans,Size);std::vector<int> Teams;std::vector<bool> Assigned(Bots.size(),false);
 for(size_t I=0;I<Bots.size();++I){auto B=Bots[I];Teams.push_back(B.OriginalTeam>=0?B.OriginalTeam:B.Team);
  if(B.OriginalTeam>=0 && Slots[B.Team]>0){Teams[I]=B.Team;--Slots[B.Team];Assigned[I]=true;}}
 for(auto& S:Slots)for(size_t I=0;I<Bots.size()&&S.second>0;++I)if(!Assigned[I]){Teams[I]=S.first;Assigned[I]=true;--S.second;}
 return Teams;
}
}
