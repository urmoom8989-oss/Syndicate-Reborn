#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <cwctype>
#include <algorithm>
    // Cert builds can assert instead of returning null for an absent package.
    // Query their registry before resolving any optional bot asset.
    inline bool SyndicateAssetAvailable(const wchar_t* Path){
        if(VersionInfo.FortniteVersion!=1.11)return true;
        static std::unordered_map<std::wstring,bool> Cache;
        std::wstring Key=Path;if(Key.ends_with(L"_C"))Key.resize(Key.size()-2);
        auto It=Cache.find(Key);if(It!=Cache.end())return It->second;
        auto Helpers=FindClass("AssetRegistryHelpers");auto Interface=FindClass("AssetRegistry");
        auto Data=FindStruct("AssetData");if(!Helpers || !Interface || !Data)return false;
        auto Obj=Helpers->GetDefaultObj();auto Get=Obj->GetFunction("GetAssetRegistry");if(!Get)return false;
        uint8 Return[32]{};Obj->ProcessEvent(Get,Return);auto Registry=*(UObject**)Return;if(!Registry)return false;
        UFunction* Query=nullptr;for(auto F=Interface->GetChildren();F;F=F->GetNext())if(F->Name.ToString()=="GetAssetByObjectPath")Query=(UFunction*)F;
        if(!Query)return false;
        auto Meta=Query->GetParamsNamed();std::vector<uint8> Params(Meta.Size,0);bool Input=false;FName Name(Key.c_str());
        for(auto& P:Meta.NameOffsetMap)if(P.Name=="ObjectPath" && P.ElementSize==sizeof(FName) && P.Offset+P.ElementSize<=Params.size()){memcpy(Params.data()+P.Offset,&Name,sizeof(Name));Input=true;}
        if(!Input)return false;Registry->ProcessEvent(Query,Params.data());
        auto Offset=Data->GetOffset("ObjectPath");bool Found=false;
        for(auto& P:Meta.NameOffsetMap)if(P.Name=="ReturnValue" && Offset!=uint32(-1) && Offset+sizeof(FName)<=P.ElementSize && P.Offset+P.ElementSize<=Params.size())Found=GetFromOffset<FName>(Params.data()+P.Offset,Offset).ComparisonIndex!=0;
        Cache.emplace(Key,Found);return Found;
    }
    // Every outfit (AthenaCharacterItemDefinition) this build really has, from its asset registry: (object path, name),
    // NPC/test/dev entries removed. Pre-season (1.7.2) does not keep its outfits where later seasons do, so loading
    // /Game/Athena/Items/Cosmetics/Characters/<id> failed there for bots and players alike. Cached for the process.
    // (object path, name) of every asset of a class in this build's asset registry.
    inline std::vector<std::pair<std::wstring,std::wstring>> SyndicateRegistryAssets(const wchar_t* ClassName){
        std::vector<std::pair<std::wstring,std::wstring>> Out;
        auto Helpers=FindClass("AssetRegistryHelpers");auto Interface=FindClass("AssetRegistry");auto Data=FindStruct("AssetData");
        if(!Helpers || !Interface || !Data)return Out;
        auto Obj=Helpers->GetDefaultObj();auto Get=Obj->GetFunction("GetAssetRegistry");if(!Get)return Out;
        uint8 Return[32]{};Obj->ProcessEvent(Get,Return);auto Registry=*(UObject**)Return;if(!Registry)return Out;
        UFunction* Query=nullptr;for(auto F=Interface->GetChildren();F;F=F->GetNext())if(F->Name.ToString()=="GetAssetsByClass")Query=(UFunction*)F;
        if(!Query)return Out;
        auto Meta=Query->GetParamsNamed();std::vector<uint8> Params(Meta.Size+16,0);FName Name(ClassName);uint32 ArrayOffset=uint32(-1);
        for(auto& P:Meta.NameOffsetMap){
            if(P.Name=="ClassName" && P.ElementSize==sizeof(FName) && P.Offset+sizeof(FName)<=Meta.Size)memcpy(Params.data()+P.Offset,&Name,sizeof(FName));
            else if(P.Name=="OutAssetData")ArrayOffset=P.Offset;
            else if(P.Name=="bSearchSubClasses" && P.Offset<Meta.Size)Params[P.Offset]=1;}
        if(ArrayOffset==uint32(-1))return Out;
        Registry->ProcessEvent(Query,Params.data());
        auto& Found=*(TArray<uint8>*)(Params.data()+ArrayOffset);
        auto Size=Data->GetPropertiesSize();auto PathOffset=Data->GetOffset("ObjectPath");
        if(PathOffset==uint32(-1) || Size<=0 || Found.Num()<0 || Found.Num()>200000)return Out;
        for(int I=0;I<Found.Num();I++){
            auto Path=GetFromOffset<FName>(&Found.Get(I,Size),PathOffset);if(Path.ComparisonIndex==0)continue;
            std::wstring W(Path.ToWString().c_str());auto Dot=W.find_last_of(L'.');
            Out.emplace_back(W,Dot==std::wstring::npos?W:W.substr(Dot+1));}
        return Out;
    }
    inline bool SyndicateJunkCosmetic(std::wstring N){
        for(auto& C:N)C=(wchar_t)towlower(C);
        return N.find(L"npc")!=std::wstring::npos || N.find(L"test")!=std::wstring::npos || N.find(L"_tbd")!=std::wstring::npos || N.find(L"random")!=std::wstring::npos || N.find(L"_dev_")!=std::wstring::npos || N.find(L"mannequin")!=std::wstring::npos;
    }
    // Every outfit (AthenaCharacterItemDefinition, CID_*) this build really has: (object path, name), junk removed.
    inline const std::vector<std::pair<std::wstring,std::wstring>>& SyndicateRegistryOutfits(){
        static std::vector<std::pair<std::wstring,std::wstring>> Out;static bool Done=false;if(Done)return Out;Done=true;
        for(auto& [Path,N]:SyndicateRegistryAssets(L"AthenaCharacterItemDefinition")){
            std::wstring L=N;for(auto& C:L)C=(wchar_t)towlower(C);
            if(L.rfind(L"cid_",0)==0 && !SyndicateJunkCosmetic(N))Out.emplace_back(Path,N);}
        return Out;
    }
    // Pre-season (1.7.2) has no outfit items in its registry: its Battle Royale characters are hero types (FortHeroType)
    // with "Athena" in the name. (object path, name), junk removed.
    // Full object path of a loaded object ("/Game/X/Y.Y"), from its outer package.
    inline std::wstring SyndicateObjectPath(const UObject* O){
        if(!O || !O->Outer)return L"";
        std::string Pkg=O->Outer->Name.ToString().c_str(),N=O->Name.ToString().c_str();
        std::string P=Pkg+"."+N;return std::wstring(P.begin(),P.end());
    }
    // Pre-season (1.7.2) has no outfit items and its asset registry answers nothing on the match server: its Battle Royale
    // characters are the default recruits, hero types with "Athena" in the name. The ones already in memory are found,
    // and their numbered/lettered siblings in the same folder are loaded (HID_..._01 -> _02.._12, ..._A -> _B.._H), so every
    // recruit is in the list even when only one was loaded. (object path, name), junk removed. Built once, on first use.
    inline const std::vector<std::pair<std::wstring,std::wstring>>& SyndicateRegistryAthenaHeroes(){
        static std::vector<std::pair<std::wstring,std::wstring>> Out;static bool Done=false;if(Done)return Out;Done=true;
        std::unordered_map<std::wstring,bool> Seen;
        auto Add=[&](const std::wstring& Path,const std::wstring& N){
            std::wstring L=N;for(auto& C:L)C=(wchar_t)towlower(C);
            if(L.find(L"athena")==std::wstring::npos || L.find(L"_menu")!=std::wstring::npos || SyndicateJunkCosmetic(N) || Seen.count(L))return; // _Menu: the lobby-only recruit
            Seen[L]=true;Out.emplace_back(Path,N);};
        for(auto& [Path,N]:SyndicateRegistryAssets(L"FortHeroType"))Add(Path,N);
        auto HeroClass=FindClass("FortHeroType");if(!HeroClass)return Out;
        std::vector<std::pair<std::wstring,std::wstring>> Loaded;
        for(int I=0;I<TUObjectArray::Num();++I){auto O=TUObjectArray::GetObjectByIndex(I);
            if(!O || !O->Class || !O->IsA(HeroClass))continue;
            std::string N=O->Name.ToString().c_str();if(N.rfind("Default__",0)==0)continue;
            auto Path=SyndicateObjectPath(O);if(Path.empty())continue;
            Loaded.emplace_back(Path,std::wstring(N.begin(),N.end()));}
        for(auto& [Path,N]:Loaded)Add(Path,N);
        // Siblings: same folder, the last number or letter changed.
        for(auto [Path,N]:Loaded){
            std::wstring L=N;for(auto& C:L)C=(wchar_t)towlower(C);if(L.find(L"athena")==std::wstring::npos)continue;
            auto Slash=Path.find_last_of(L'/');if(Slash==std::wstring::npos)continue;std::wstring Folder=Path.substr(0,Slash+1);
            std::vector<std::wstring> Names;
            size_t D=N.size();while(D>0 && iswdigit(N[D-1]))--D;
            if(D<N.size()){const size_t Width=N.size()-D;for(int K=1;K<=12;++K){wchar_t B[8];swprintf_s(B,L"%0*d",int(Width),K);Names.push_back(N.substr(0,D)+B);}}
            else if(N.size()>1 && N[N.size()-2]==L'_' && iswalpha(N.back())){for(wchar_t C=L'A';C<=L'H';++C)Names.push_back(N.substr(0,N.size()-1)+C);}
            for(auto& S:Names){std::wstring Low=S;for(auto& C:Low)C=(wchar_t)towlower(C);if(Seen.count(Low))continue;
                auto P=Folder+S+L"."+S;
                if(auto H=SDK::FindObject(P.c_str(),HeroClass)){Add(P,S);continue;}
                if(SDK::Offsets::StaticLoadObject)if(SDK::StaticLoadObject(P.c_str(),HeroClass))Add(P,S);}
        }
        return Out;
    }
    inline std::wstring SyndicateRegistryOutfitPath(const std::string& Name){
        for(auto& [Path,N]:SyndicateRegistryOutfits())if(N.size()==Name.size() && std::equal(N.begin(),N.end(),Name.begin(),[](wchar_t A,char B){return towlower(A)==(wchar_t)tolower((unsigned char)B);}))return Path;
        return L"";
    }
