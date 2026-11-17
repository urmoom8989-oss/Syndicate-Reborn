#include "pch.h"
#include "../Public/SyndicateDBNORecovery.h"
#include "../../Erbium/Public/SyndicateSeasonAssets.h"
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <set>
#include "../Public/FortPlayerControllerAthena.h"
#include "../Public/FortGameMode.h"
#include "../Public/FortWeapon.h"
#include "../Public/BuildingSMActor.h"
#include "../Public/FortKismetLibrary.h"
#include "../../Engine/Public/NetDriver.h"
extern uint64_t ApplyCharacterCustomization;
#include "../../Erbium/Public/Configuration.h"
#include "../Public/FortLootPackage.h"
#include "../../Erbium/Public/Events.h"
#include "../../Erbium/Public/LateGame.h"
#include "../Public/BuildingItemCollectorActor.h"
#include "../Public/FortPhysicsPawn.h"
#include "../Public/BattleRoyaleGamePhaseLogic.h"
#include "../../Erbium/Public/GUI.h"
#include "../Public/FortAthenaCreativePortal.h"


static void (*HumanBotReviveStartedOG)(UObject*,FFrame&)=nullptr;
static void (*HumanBotReviveInterruptedOG)(UObject*,FFrame&)=nullptr;
static void HumanBotReviveStarted(UObject* Context,FFrame& Stack) {
    auto Target=Stack.Locals?*(AFortPlayerPawnAthena**)Stack.Locals:nullptr;
    if(HumanBotReviveStartedOG)HumanBotReviveStartedOG(Context,Stack);
    SyndicateDBNO::StartHumanBotRevive(Context->Cast<AFortPlayerControllerAthena>(),Target);
}
static void HumanBotReviveInterrupted(UObject* Context,FFrame& Stack) {
    auto Target=Stack.Locals?*(AFortPlayerPawnAthena**)Stack.Locals:nullptr;
    SyndicateDBNO::InterruptHumanBotRevive(Context->Cast<AFortPlayerControllerAthena>(),Target);
    if(HumanBotReviveInterruptedOG)HumanBotReviveInterruptedOG(Context,Stack);
}

void AFortPlayerControllerAthena::GetPlayerViewPoint(AFortPlayerControllerAthena* PlayerController, FVector& Loc, FRotator& Rot)
{
    // The client may already be spectating while the server still says Playing.
    // Respect the native camera/view target rather than the retained dead pawn.
    if(VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5){
        if(GetPlayerViewPointOG){GetPlayerViewPointOG(PlayerController,Loc,Rot);return;}
        auto Target=PlayerController->GetViewTarget();
        if(Target && Target!=PlayerController && !Target->bActorIsBeingDestroyed){
            Loc=Target->K2_GetActorLocation();Rot=Target->K2_GetActorRotation();return;
        }
    }
	// Spectating can retain MyFortPawn. Do not use that old pawn as the camera.
	static auto SpectatingName = FName(L"Spectating");
	if (PlayerController->StateName == SpectatingName)
	{
		if (GetPlayerViewPointOG)
			GetPlayerViewPointOG(PlayerController, Loc, Rot);
		else
		{
			Loc = PlayerController->LastSpectatorSyncLocation;
			Rot = PlayerController->LastSpectatorSyncRotation;
		}
		return;
	}
	// Older builds may not have a native viewpoint implementation.
	if (auto Pawn = PlayerController->MyFortPawn)
	{
		Loc = Pawn->K2_GetActorLocation();
		Rot = PlayerController->GetControlRotation();
		return;
	}

	if (GetPlayerViewPointOG)
		GetPlayerViewPointOG(PlayerController, Loc, Rot);
	else
	{
		static auto SFName = FName(L"Spectating");
		if (PlayerController->StateName == SFName)
		{
			Loc = PlayerController->LastSpectatorSyncLocation;
			Rot = PlayerController->LastSpectatorSyncRotation;
		}
		else
		{
			auto ViewTarget = PlayerController->GetViewTarget();

			if (ViewTarget)
			{
				Loc = ViewTarget->K2_GetActorLocation();
				//if (auto TargetPawn = ViewTarget->Cast<AFortPlayerPawnAthena>())
				//	Loc.Z += TargetPawn->BaseEyeHeight;
				Rot = ViewTarget->K2_GetActorRotation();
			}
		}
	}
}

// Every Syndicate season reads the saved locker. Before, 3.5 and 5.x were skipped, and 1.7.2 / 1.8.x
// too: UE 4.16 stores a soft reference as an FString path, not an FName, so reading it as an FName
// (AssetPathName.ToWString) crashed the server. SyndicateSoftPath reads both layouts.
static bool SyndicateLockerBuild()
{
    auto V=VersionInfo.FortniteVersion;
    return V==1.72 || V==1.8 || V==1.81 || V==1.82 || V==1.11 || (V>=3.5 && V<13); // 11.x: Chapter 2 Season 1; 12.x: Season 2
}
// Path of a soft object reference, for StaticLoadObject when it is not loaded yet.
static std::wstring SyndicateSoftPath(FSoftObjectPtr& Soft)
{
    if(VersionInfo.EngineVersion<=4.16){
        auto& Path=*(FString*)(__int64(&Soft)+offsetof(FSoftObjectPtr,ObjectID));
        return Path.Num()>0?std::wstring(Path.ToWString().c_str()):std::wstring();
    }
    return Soft.ObjectID.AssetPathName.ComparisonIndex>0?std::wstring(Soft.ObjectID.AssetPathName.ToWString().c_str()):std::wstring();
}
// Soft paths of an array of soft object pointers. On UE 4.16 an element is 0x20 bytes (weak pointer, tag, then the
// path as an FString at +0x10), not the SDK's newer 0x28-byte layout, so walking the array with the SDK type read
// garbage from the second element on and crashed the server (Save the World heroes have several specializations).
// The path's memory is checked before it is read: the crash reporter kills the server on any bad read.
template<typename T> static std::vector<std::wstring> SyndicateSoftPaths(TArray<T>& Array)
{
    std::vector<std::wstring> Out;const int N=Array.Num();auto Base=(uint8*)Array.GetData();
    if(N<=0 || N>64 || !Base)return Out;
    auto Readable=[](const void* P,size_t Size){MEMORY_BASIC_INFORMATION M{};if(!P || !VirtualQuery(P,&M,sizeof(M)) || M.State!=MEM_COMMIT || (M.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
        return (uint8*)P+Size<=(uint8*)M.BaseAddress+M.RegionSize;};
    if(VersionInfo.EngineVersion>4.16){for(int I=0;I<N;I++)Out.push_back(SyndicateSoftPath(*(FSoftObjectPtr*)(Base+I*sizeof(T))));return Out;}
    if(!Readable(Base,size_t(N)*0x20))return Out;
    for(int I=0;I<N;I++){
        auto Field=Base+I*0x20+0x10;auto Chars=*(const wchar_t**)Field;const int Count=*(int32*)(Field+8);
        if(Count<=1 || Count>512 || !Readable(Chars,size_t(Count)*sizeof(wchar_t))){Out.emplace_back();continue;}
        Out.emplace_back(Chars,Chars+Count-1); // Count includes the terminating zero
    }
    return Out;
}
// A hero's parts (head, body, ...) by part type on UE 4.16 (1.7.2 / 1.8.x), where FSoftObjectPtr::Get() and the SDK's
// soft pointer layout can't be used: paths are read with SyndicateSoftPaths and loaded. Also used for the bots' outfits.
void SyndicateHeroPartsLegacy(UFortHeroType* Hero,const UObject* (&Parts)[6])
{
    if(!Hero)return;
    for(auto& SpecPath:SyndicateSoftPaths(Hero->Specializations)){
        auto Spec=SpecPath.empty()?nullptr:(UFortHeroSpecialization*)StaticLoadObject(SpecPath.c_str(),UFortHeroSpecialization::StaticClass());
        if(Spec)for(auto& PartPath:SyndicateSoftPaths(Spec->CharacterParts)){
            auto Part=PartPath.empty()?nullptr:(const UCustomCharacterPart*)StaticLoadObject(PartPath.c_str(),UCustomCharacterPart::StaticClass());
            if(Part && Part->CharacterPartType<6)Parts[Part->CharacterPartType]=Part;
        }
    }
}
// The player's name: GetPlayerName where it exists, the PlayerName property on 1.7.2 / 1.8.x.
static std::string SyndicatePlayerNameOf(UObject* State)
{
    if(!State)return {};
    if(auto Fn=State->GetFunction("GetPlayerName")){auto Name=State->Call<FString>(Fn);std::string Raw=Name.ToString().c_str();Name.Free();return Raw;}
    if(auto O=State->GetOffset("PlayerName");O!=uint32(-1))return std::string(GetFromOffset<FString>(State,O).ToString().c_str());
    return {};
}

// Restore the complete saved locker before inventory creation and pawn replication.
static UAthenaCharacterPartItemDefinition* RestoreSyndicateAccessories(AFortPlayerControllerAthena* PC, AFortPlayerPawnAthena* Pawn, bool& HasSavedBackpack, std::vector<std::pair<std::string,std::string>>& Styles)
{
    if(!SyndicateLockerBuild() || !PC->PlayerState)return nullptr;
    auto Raw=SyndicatePlayerNameOf(PC->PlayerState);if(Raw.empty())return nullptr;
    std::wstring Hex;const wchar_t* Digits=L"0123456789abcdef";
    for(unsigned char C:Raw){C=(unsigned char)std::tolower(C);Hex+=Digits[C>>4];Hex+=Digits[C&15];}
    wchar_t Ready[32768]{};if(!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768))return nullptr;
    std::wstring Path=Ready;auto Slash=Path.find_last_of(L"\\/");if(Slash==std::wstring::npos)return nullptr;
    Path.resize(Slash+1);Path+=L"locker\\"+Hex+L".loadout";
    FILE* File=nullptr;if(_wfopen_s(&File,Path.c_str(),L"rb") || !File)return nullptr;
    auto Loadout=FindStruct("FortAthenaLoadout");UAthenaCharacterPartItemDefinition* Backpack=nullptr;
    char Line[512]{};
    while(fgets(Line,sizeof(Line),File)){
        std::string Row=Line;while(!Row.empty() && (Row.back()=='\r' || Row.back()=='\n'))Row.pop_back();
        auto Equal=Row.find('=');if(Equal==std::string::npos)continue;
        auto Key=Row.substr(0,Equal),Id=Row.substr(Equal+1);
        if(Key.rfind("variant_",0)==0){
            auto Channel=Key.substr(8);bool Valid=!Channel.empty() && Channel.size()<49 && !Id.empty() && Id.size()<49;
            for(unsigned char C:Channel)if(!std::isalnum(C) && C!='_')Valid=false;
            for(unsigned char C:Id)if(!std::isalnum(C) && C!='_' && C!='.')Valid=false;
            if(Valid && Styles.size()<8)Styles.emplace_back(Channel,Id);
            continue;
        }
        // Wraps (camo), Season 7+: wrap_<slot>=<WrapId> goes into the loadout's ItemWraps array (one entry per
        // slot: vehicles first, then weapons). Before this the file had no wraps, so none showed in matches.
        if(Key.rfind("wrap_",0)==0){
            const int Slot=atoi(Key.c_str()+5);
            bool Valid=Slot>=0 && Slot<8 && !Id.empty() && Id.size()<=80;
            for(unsigned char C:Id)if(!std::isalnum(C) && C!='_')Valid=false;
            auto WrapsOffset=Loadout?Loadout->GetOffset("ItemWraps"):uint32(-1);
            if(!Valid || WrapsOffset==uint32(-1))continue;
            std::string Asset="/Game/Athena/Items/Cosmetics/ItemWraps/"+Id+"."+Id;
            std::wstring Wide(Asset.begin(),Asset.end());auto Class=FindClass("AthenaItemWrapDefinition");
            auto Item=(Class && SyndicateAssetAvailable(Wide.c_str()))?StaticLoadObject(Wide.c_str(),Class):nullptr;
            if(!Item){printf("[SyndicateLocker] unavailable wrap %d=%s\n",Slot,Id.c_str());continue;}
            for(UObject* Owner:{(UObject*)PC,(UObject*)Pawn})
                for(auto Property:{"CosmeticLoadoutPC","CustomizationLoadout","CosmeticLoadout"}){
                    auto Off=Owner->GetOffset(Property);auto Prop=Owner->GetProperty(Property);
                    if(Off==uint32(-1) || !Prop || WrapsOffset+sizeof(TArray<UObject*>)>GetFromOffset<uint32>(Prop,Offsets::ElementSize))continue;
                    auto& Wraps=GetFromOffset<TArray<UObject*>>(Owner,Off+WrapsOffset);
                    while(Wraps.Num()<=Slot)Wraps.Add(nullptr);
                    Wraps[Slot]=(UObject*)Item;
                    printf("[SyndicateLocker] restored wrap %d=%s on %s\n",Slot,Id.c_str(),Property);
                }
            continue;
        }
        const char* Field=nullptr;const char* Folder=nullptr;const char* ClassName=nullptr;
        if(Key=="pickaxe"){Field="Pickaxe";Folder="Pickaxes";ClassName="AthenaPickaxeItemDefinition";}
        else if(Key=="backpack"){Field="Backpack";Folder="Backpacks";ClassName="AthenaBackpackItemDefinition";}
        else if(Key=="glider"){Field="Glider";Folder="Gliders";ClassName="AthenaGliderItemDefinition";}
        else if(Key=="contrail"){Field="SkyDiveContrail";Folder="Contrails";ClassName="AthenaSkyDiveContrailItemDefinition";} // Season 5+ (other builds: field missing, skipped)
        else continue;
        if(Id.size()>200 || (Id.empty() && Key!="backpack"))continue;
        bool Valid=true;for(unsigned char C:Id)if(!std::isalnum(C) && C!='_')Valid=false;
        if(!Valid)continue;
        std::string Asset="/Game/Athena/Items/Cosmetics/"+std::string(Folder)+"/"+Id+"."+Id;
        std::wstring Wide(Asset.begin(),Asset.end());auto Class=FindClass(ClassName);
        auto Item=(!Id.empty() && Class && SyndicateAssetAvailable(Wide.c_str()))?StaticLoadObject(Wide.c_str(),Class):nullptr;
        if(!Item && !Id.empty()){printf("[SyndicateLocker] unavailable %s=%s\n",Key.c_str(),Id.c_str());continue;}
        if(Key=="backpack")HasSavedBackpack=true;
        auto FieldOffset=Loadout?Loadout->GetOffset(Field):uint32(-1);
        if(FieldOffset!=uint32(-1))for(UObject* Owner:{(UObject*)PC,(UObject*)Pawn})
            for(auto Property:{"CosmeticLoadoutPC","CustomizationLoadout","CosmeticLoadout"}){
                auto Off=Owner->GetOffset(Property);auto Prop=Owner->GetProperty(Property);
                if(Off!=uint32(-1) && Prop && FieldOffset+sizeof(void*)<=GetFromOffset<uint32>(Prop,Offsets::ElementSize))
                    {GetFromOffset<const UObject*>(Owner,Off+FieldOffset)=Item;printf("[SyndicateLocker] restored %s=%s on %s\n",Key.c_str(),Id.c_str(),Property);}
            }
        if(Key=="backpack")Backpack=(UAthenaCharacterPartItemDefinition*)Item;
    }
    fclose(File);PC->ForceNetUpdate();Pawn->ForceNetUpdate();return Backpack;
}


// Resolve saved selections against this client's actual cosmetic variant definitions.
int ApplySyndicateStyles(AFortPlayerControllerAthena* PC, AFortPlayerPawnAthena* Pawn, UAthenaCharacterItemDefinition* Character,
    const std::vector<std::pair<std::string,std::string>>& Styles, const UObject* (&Parts)[6])
{
    // Outfit styles exist from Season 4 on. This used to run on 4.5 only (fixed 48-byte channel
    // entries), so a style picked in the lobby was lost in the match on every other season. The
    // channel entry layout is now read from the build (VariantChannelTag / ActiveVariantTag offsets).
    if(VersionInfo.FortniteVersion<4.0 || !Character || Styles.empty())return 0;
    auto ChannelType=FindStruct("McpVariantChannelInfo");auto Loadout=FindStruct("FortAthenaLoadout");
    if(!ChannelType || !Loadout)return 0;
    const int EntrySize=ChannelType->GetPropertiesSize();
    const auto TagOffset=ChannelType->GetOffset("VariantChannelTag"),ActiveOffset=ChannelType->GetOffset("ActiveVariantTag"),UsedForOffset=ChannelType->GetOffset("ItemVariantIsUsedFor");
    if(EntrySize<16 || EntrySize>256 || TagOffset==uint32(-1) || ActiveOffset==uint32(-1))return 0;
    auto ChannelsOff=Loadout->GetOffset("CharacterVariantChannels"),CharacterOff=Loadout->GetOffset("Character");
    auto ItemOff=Character->GetOffset("ItemVariants");if(ItemOff==uint32(-1) || ChannelsOff==uint32(-1) || CharacterOff==uint32(-1))return 0;
    auto Matches=[](FName Name,const std::string& Saved){auto Text=Name.ToString();
        for(auto Prefix:{"Cosmetics.Variant.Channel.","Cosmetics.Variant.Property."})if(Text.rfind(Prefix,0)==0){Text.erase(0,strlen(Prefix));break;}
        return _stricmp(Text.c_str(),Saved.c_str())==0;};
    int Applied=0;auto& Variants=GetFromOffset<TArray<UObject*>>(Character,ItemOff);
    if(Variants.Num()>16)return 0;
    for(auto V:Variants)if(V){
        auto ChannelOff=V->GetOffset("VariantChannelTag");if(ChannelOff==uint32(-1))continue;
        auto Channel=GetFromOffset<FName>(V,ChannelOff);
        for(auto& Saved:Styles)if(Matches(Channel,Saved.first)){
            auto OptionsOff=V->GetOffset("PartOptions");auto Type=FindStruct("PartVariantDef");bool IsPart=OptionsOff!=uint32(-1);
            if(!IsPart){OptionsOff=V->GetOffset("MaterialOptions");Type=FindStruct("MaterialVariantDef");}
            // Numerical styles have no PartOptions/MaterialOptions. Resolve all selections
            // through the installed asset's supported tags, never an arbitrary saved FName.
            auto AllTagsFn=V->GetFunction("GetAllVariantTags");
            if(!AllTagsFn || AllTagsFn->GetParamsNamed().Size!=sizeof(FGameplayTagContainer))continue;
            auto Tags=V->Call<FGameplayTagContainer>(AllTagsFn);
            FName Active{};bool Valid=false;
            for(auto Tag:Tags.GameplayTags)if(Matches(Tag.TagName,Saved.second)){Active=Tag.TagName;Valid=true;break;}
            Tags.GameplayTags.Free();Tags.ParentTags.Free();if(!Valid)continue;
            uint8* Row=nullptr;
            if(IsPart && Type){auto TagOff=Type->GetOffset("CustomizationVariantTag");
                if(TagOff==uint32(-1))continue;
                auto& Options=GetFromOffset<TArray<uint8>>(V,OptionsOff);if(Options.Num()>64)continue;
                for(int I=0;I<Options.Num();++I){auto Candidate=&Options.Get(I,Type->GetPropertiesSize());
                    if(GetFromOffset<FName>(Candidate,TagOff)==Active){Row=Candidate;break;}}
                if(!Row)continue;
            }
            {
                if(IsPart){auto PO=Type->GetOffset("VariantParts");auto Prop=Type->GetProperty("VariantParts");
                    if(PO!=uint32(-1) && Prop){auto Inner=GetFromOffset<const UObject*>(Prop,0x70);auto& Values=GetFromOffset<TArray<uint8>>(Row,PO);
                        if(Inner && Values.Num()<32){auto Size=GetFromOffset<uint32>(Inner,Offsets::ElementSize);auto Kind=Inner->Class->Name.ToString();
                            for(int J=0;J<Values.Num();++J){const UCustomCharacterPart* Part=nullptr;auto Value=&Values.Get(J,Size);
                                if(Kind=="ObjectProperty" && Size==sizeof(void*))Part=GetFromOffset<const UCustomCharacterPart*>(Value,0);
                                else if(Kind=="SoftObjectProperty" && Size==sizeof(TSoftObjectPtr<UCustomCharacterPart>)){auto& Soft=*(TSoftObjectPtr<UCustomCharacterPart>*)Value;Part=Soft.Get();if(!Part)Part=(const UCustomCharacterPart*)StaticLoadObject(Soft.ObjectID.AssetPathName.ToWString().c_str(),UCustomCharacterPart::StaticClass());}
                                if(Part && Part->CharacterPartType<6)Parts[Part->CharacterPartType]=Part;
                            }
                        }
                    }
                }
                for(UObject* Owner:{(UObject*)PC,(UObject*)Pawn})for(auto Property:{"CosmeticLoadoutPC","CustomizationLoadout","CosmeticLoadout"}){
                    auto O=Owner->GetOffset(Property);auto P=Owner->GetProperty(Property);if(O==uint32(-1) || !P || GetFromOffset<uint32>(P,Offsets::ElementSize)<Loadout->GetPropertiesSize())continue;
                    GetFromOffset<const UObject*>(Owner,O+CharacterOff)=Character;
                    auto& Channels=GetFromOffset<TArray<uint8>>(Owner,O+ChannelsOff);if(Channels.Num()>16)continue;
                    uint8* Found=nullptr;for(int K=0;K<Channels.Num();++K){auto Entry=&Channels.Get(K,EntrySize);if(GetFromOffset<FName>(Entry,TagOffset)==Channel){Found=Entry;break;}}
                    if(Found){GetFromOffset<FName>(Found,ActiveOffset)=Active;if(UsedForOffset!=uint32(-1))GetFromOffset<const UObject*>(Found,UsedForOffset)=Character;}
                    else {uint8 Entry[256]{};GetFromOffset<FName>(Entry,TagOffset)=Channel;GetFromOffset<FName>(Entry,ActiveOffset)=Active;
                        if(UsedForOffset!=uint32(-1))GetFromOffset<const UObject*>(Entry,UsedForOffset)=Character;Channels.Add(*Entry,EntrySize);}
                }
                ++Applied;
            }
        }
    }
    return Applied;
}

// Remember each human's applied outfit. Some zones (Season X Tilted Town) make
// the game re-run its own customization, which has no MCP profile here and
// falls back to default parts; the watchdog puts the selected outfit back.
struct SyndicateOutfitRecord{const UObject* Parts[6]{};uint32 Off=uint32(-1);ULONGLONG Next=0;};
static std::unordered_map<UObject*,SyndicateOutfitRecord> SyndicateOutfits;

// "Lean" (Pre-season 1.7.2 only; the game's Purple Stuff, renamed by TiltedClient.dll). Drinking one heals 75 health
// and 25 shield, a little at a time over 10 seconds (each up to 100); the game's own Purple Stuff result is replaced
// by the values from just before. A drink is noticed when the count in the inventory goes down while the player did
// not get rid of it some other way: dropping it (ServerAttemptInventoryDrop) or swapping it out by picking something
// up with a full inventory (the pickup handlers) both mark the player for 3 s, and then nothing happens to health or
// shield. Before, a swap counted as a drink and rolled the old coin flip (+50, or a drain to 1 HP).
std::unordered_map<AFortPlayerControllerAthena*,ULONGLONG> SyndicateLeanDropped;
// Set by RemoveInventoryItem (FortInventory.cpp) when a Lean is used up, before infinite ammo can skip the removal:
// with the menu's infinite ammo on, the count never goes down, so the count alone missed every drink.
std::unordered_map<AFortPlayerControllerAthena*,ULONGLONG> SyndicateLeanUsed;
void SyndicateLeanMarkPickup(UObject* PawnObject){
    if(VersionInfo.FortniteVersion!=1.72 || !PawnObject)return;
    auto Pawn=(AFortPlayerPawnAthena*)PawnObject;auto PC=(AFortPlayerControllerAthena*)Pawn->Controller;
    // Only a pickup made while holding Lean can swap it out; any other pickup must not cancel a real drink.
    auto Held=(UObject*)Pawn->CurrentWeapon;if(!PC || !Held)return;
    auto DataOff=Held->GetOffset("WeaponData");auto Data=DataOff!=uint32(-1)?GetFromOffset<UObject*>(Held,DataOff):nullptr;
    if(Data && Data->Name.ToString().find("PurpleStuff")!=std::string::npos)SyndicateLeanDropped[PC]=GetTickCount64();
}
// The game's own Purple Stuff is a gameplay effect that keeps adding shield for ~20 s and overrides the values set
// here; it is removed from the player while a Lean is active. Returns how many were removed (logged once per name).
static int SyndicateRemovePurpleStuffEffects(AFortPlayerControllerAthena* PC){
    auto State=PC?PC->PlayerState:nullptr;auto ASC=State?State->AbilitySystemComponent:nullptr;
    if(!ASC || !ASC->HasActiveGameplayEffects())return 0;
    std::vector<const UClass*> Effects;static std::set<std::string> Logged;
    for(int I=0;I<ASC->ActiveGameplayEffects.GameplayEffects_Internal.Num();++I){
        auto Def=ASC->ActiveGameplayEffects.GameplayEffects_Internal.Get(I,FActiveGameplayEffect::Size()).Spec.Def;
        if(!Def || !Def->Class)continue;std::string N=Def->Class->Name.ToString().c_str();
        if(N.find("PurpleStuff")==std::string::npos && N.find("Purple_Stuff")==std::string::npos && N.find("Slurp")==std::string::npos)continue;
        if(std::find(Effects.begin(),Effects.end(),Def->Class)==Effects.end())Effects.push_back(Def->Class);
        if(Logged.insert(N).second)printf("[Syndicate] Lean: removing the game's own effect %s\n",N.c_str());
    }
    auto Remove=ASC->GetFunction("RemoveActiveGameplayEffectBySourceEffect");if(!Remove)return 0;
    for(auto Type:Effects){
        auto Params=Remove->GetParamsNamed();std::vector<uint8> Data(Params.Size,0);bool HasEffect=false;
        for(auto& P:Params.NameOffsetMap){if(P.Offset+P.ElementSize>Data.size())continue;
            if(P.Name=="GameplayEffect" && P.ElementSize==sizeof(void*)){memcpy(Data.data()+P.Offset,&Type,sizeof(Type));HasEffect=true;}
            if(P.Name=="StacksToRemove" && P.ElementSize==4){int All=-1;memcpy(Data.data()+P.Offset,&All,4);}}
        if(HasEffect)ASC->ProcessEvent(Remove,Data.data());
    }
    return int(Effects.size());
}
// Health and shield as the ability system keeps them (FortHealthSet attributes). On 1.7.2 Pawn->SetShield/SetHealth did
// not change what the player sees (a Lean heal set that way never showed), and the read-back lagged. Bots heal the same
// way (CommitBotDamageAttribute).
static UObject* SyndicateHealthSetOf(AFortPlayerPawnAthena* Pawn){
    if(!Pawn)return nullptr;
    auto O=Pawn->GetOffset("HealthSet");if(O!=uint32(-1))if(auto Set=GetFromOffset<UObject*>(Pawn,O))return Set;
    auto State=Pawn->PlayerState;auto AO=State?State->GetOffset("AbilitySystemComponent"):uint32(-1);
    auto ASC=AO!=uint32(-1)?GetFromOffset<UObject*>(State,AO):nullptr;auto SO=ASC?ASC->GetOffset("SpawnedAttributes"):uint32(-1);
    static auto Class=FindClass("FortHealthSet");
    if(SO!=uint32(-1) && Class)for(auto Set:GetFromOffset<TArray<UObject*>>(ASC,SO))if(Set && Set->IsA(Class))return Set;
    return nullptr;
}
// The shield attribute is CurrentShield in most builds; fall back to Shield where that is the name.
static const char* SyndicateVitalName(UObject* Set,const char* Name){
    if(!Set || strcmp(Name,"CurrentShield")!=0 || Set->GetProperty("CurrentShield"))return Name;
    return Set->GetProperty("Shield")?"Shield":Name;
}
static float SyndicateVitalGet(AFortPlayerPawnAthena* Pawn,const char* Name){
    const bool Shield=strcmp(Name,"Health")!=0;
    auto Set=SyndicateHealthSetOf(Pawn);static auto Type=FindStruct("FortGameplayAttributeData");Name=SyndicateVitalName(Set,Name);
    if(Set && Type){auto O=Set->GetOffset(Name),V=Type->GetOffset("CurrentValue");auto Prop=Set->GetProperty(Name);
        if(O!=uint32(-1) && Prop){auto Size=GetFromOffset<uint32>(Prop,Offsets::ElementSize);
            if(Size==4)return GetFromOffset<float>((uint8*)Set,O);
            if(V!=uint32(-1) && V+4<=Size)return GetFromOffset<float>((uint8*)Set+O,V);}}
    return Shield?Pawn->GetShield():Pawn->GetHealth();
}
static void SyndicateVitalSet(AFortPlayerPawnAthena* Pawn,const char* Name,float Value){
    if(!Pawn || !std::isfinite(Value))return;
    if(strcmp(Name,"Health")==0)Pawn->SetHealth(Value);else Pawn->SetShield(Value);
    auto Set=SyndicateHealthSetOf(Pawn);static auto Type=FindStruct("FortGameplayAttributeData");Name=SyndicateVitalName(Set,Name);
    if(Set){auto Prop=Set->GetProperty(Name);auto Off=Set->GetOffset(Name);
        if(Prop && Off!=uint32(-1)){auto Size=GetFromOffset<uint32>(Prop,Offsets::ElementSize);auto Data=(uint8*)Set+Off;
            if(Size==4)memcpy(Data,&Value,4);
            else if(Type)for(auto Field:{"BaseValue","CurrentValue","UnclampedBaseValue","UnclampedCurrentValue"}){auto F=Type->GetOffset(Field);if(F!=uint32(-1) && F+4<=Size)memcpy(Data+F,&Value,4);}}}
    Pawn->ForceNetUpdate();
}
void SyndicateLeanTick(){
    if(VersionInfo.FortniteVersion!=1.72)return;
    struct Track{int Count=-1;float Health=0,Shield=0;bool Healing=false;float GivenHealth=0,GivenShield=0;ULONGLONG HealStart=0,GuardUntil=0;};
    static std::unordered_map<AFortPlayerControllerAthena*,Track> Tracks;static UWorld* TrackWorld=nullptr;
    auto World=UWorld::GetWorld();auto Net=World?(UNetDriver*)World->NetDriver:nullptr;if(!Net)return;
    if(TrackWorld!=World){TrackWorld=World;Tracks.clear();SyndicateLeanDropped.clear();SyndicateLeanUsed.clear();}
    const ULONGLONG Now=GetTickCount64();
    constexpr float HealthTotal=75.f,ShieldTotal=25.f,Seconds=10.f;
    for(auto Conn:Net->ClientConnections){
        auto PC=Conn?(AFortPlayerControllerAthena*)Conn->PlayerController:nullptr;if(!PC || !PC->WorldInventory)continue;
        auto Pawn=(AFortPlayerPawnAthena*)PC->MyFortPawn;auto& T=Tracks[PC];
        if(!Pawn || Pawn->bActorIsBeingDestroyed || SyndicateVitalGet(Pawn,"Health")<=0.f){T=Track{};continue;}
        int Count=0;
        for(int i=0;i<PC->WorldInventory->Inventory.ReplicatedEntries.Num();i++){
            auto& Entry=PC->WorldInventory->Inventory.ReplicatedEntries.Get(i,FFortItemEntry::Size());
            if(Entry.ItemDefinition && Entry.ItemDefinition->Name.ToString().find("PurpleStuff")!=std::string::npos)Count+=Entry.Count;
        }
        auto Dropped=SyndicateLeanDropped.find(PC);const bool DroppedNow=Dropped!=SyndicateLeanDropped.end() && Now-Dropped->second<3000;
        // The game's own Purple Stuff adds a little shield (and health) every moment for about 20 s after a drink. For
        // 25 s after a Lean those small steps are taken back; a real heal (a potion, a medkit: a big jump) still counts.
        // The values set here read back a frame late, so the guard compares with what this tracker last set (T.Health /
        // T.Shield), not with what GetHealth/GetShield returned: before, it undid the Lean's own heal and let the game's
        // gain through.
        bool Set=false;float SetH=0,SetS=0;
        if(T.GuardUntil && Now<T.GuardUntil)SyndicateRemovePurpleStuffEffects(PC);
        if(T.GuardUntil && Now<T.GuardUntil){
            const float H=SyndicateVitalGet(Pawn,"Health"),S=SyndicateVitalGet(Pawn,"CurrentShield");
            if(H>T.Health){if(H-T.Health<5.f)SyndicateVitalSet(Pawn,"Health",T.Health);else T.Health=H;} // big jump: a real heal, keep it
            if(S>T.Shield){if(S-T.Shield<5.f)SyndicateVitalSet(Pawn,"CurrentShield",T.Shield);else T.Shield=S;}
        } else T.GuardUntil=0;
        auto Used=SyndicateLeanUsed.find(PC);const bool UsedNow=Used!=SyndicateLeanUsed.end();
        if(UsedNow)SyndicateLeanUsed.erase(Used);
        if(UsedNow || (T.Count>=0 && Count<T.Count)){
            // Undo whatever the game's own Purple Stuff (or the swap) just did to health and shield.
            SyndicateVitalSet(Pawn,"Health",float(T.Health));SyndicateVitalSet(Pawn,"CurrentShield",float(T.Shield));
            if(DroppedNow)printf("[Syndicate] Lean: dropped or swapped out, no effect\n");
            else{
                // A second drink while one is still healing adds its own 10 s on top of what is left.
                if(T.Healing){T.GivenHealth-=HealthTotal;T.GivenShield-=ShieldTotal;}
                else{T.GivenHealth=0;T.GivenShield=0;}
                T.Healing=true;T.HealStart=Now;T.GuardUntil=Now+25000;printf("[Syndicate] Lean: drinking, +75 health and +25 shield over 10 s\n");
                // Diagnostics (first drinks): every gameplay effect on the player right now.
                static int Listed=0;if(Listed<3){++Listed;auto ASC=PC->PlayerState?PC->PlayerState->AbilitySystemComponent:nullptr;
                    if(ASC && ASC->HasActiveGameplayEffects())for(int I=0;I<ASC->ActiveGameplayEffects.GameplayEffects_Internal.Num();++I){
                        auto Def=ASC->ActiveGameplayEffects.GameplayEffects_Internal.Get(I,FActiveGameplayEffect::Size()).Spec.Def;
                        if(Def && Def->Class)printf("[Syndicate] Lean: active effect %s\n",Def->Class->Name.ToString().c_str());}}
                SyndicateRemovePurpleStuffEffects(PC);
            }
        }
        if(T.Healing){
            const float F=(std::min)(1.f,float(Now-T.HealStart)/(Seconds*1000.f));
            // Add only what is due since the last tick, so damage taken during the heal is not undone.
            // Whole points only: SetHealth/SetShield keep whole numbers, so a per-tick fraction (~0.08) was lost every time.
            const float AddHealth=(std::max)(0.f,std::floor(HealthTotal*F+.001f)-T.GivenHealth),AddShield=(std::max)(0.f,std::floor(ShieldTotal*F+.001f)-T.GivenShield);
            // Build on the tracked values (the read-back lags a frame); damage taken shows as a lower read value.
            // A read more than 3 below the tracked value is damage; less is the read-back lag.
            auto Track=[](float Tracked,float Read){return Read<Tracked-3.f?Read:Tracked;};
            const float BaseH=T.GuardUntil?Track(T.Health,SyndicateVitalGet(Pawn,"Health")):SyndicateVitalGet(Pawn,"Health"),BaseS=T.GuardUntil?Track(T.Shield,SyndicateVitalGet(Pawn,"CurrentShield")):SyndicateVitalGet(Pawn,"CurrentShield");
            SetH=BaseH;SetS=BaseS;
            if(AddHealth>0){SetH=(std::min)(100.f,BaseH+AddHealth);SyndicateVitalSet(Pawn,"Health",SetH);T.GivenHealth+=AddHealth;Set=true;}
            if(AddShield>0){SetS=(std::min)(100.f,BaseS+AddShield);SyndicateVitalSet(Pawn,"CurrentShield",SetS);T.GivenShield+=AddShield;Set=true;}
            if(F>=1.f){T.Healing=false;printf("[Syndicate] Lean: heal finished\n");}
        }
        T.Count=Count;
        if(Set){T.Health=SetH;T.Shield=SetS;Pawn->ForceNetUpdate();}
        else if(T.GuardUntil){const float H=SyndicateVitalGet(Pawn,"Health"),S=SyndicateVitalGet(Pawn,"CurrentShield");if(H<T.Health-3.f)T.Health=H;if(S<T.Shield-3.f)T.Shield=S;} // damage only
        else{T.Health=SyndicateVitalGet(Pawn,"Health");T.Shield=SyndicateVitalGet(Pawn,"CurrentShield");}
    }
}
// The name of an object read from a field whose layout differs per season, or "?" when the pointer is not a live object.
// On 6.20 PlayerState.CharacterParts is not a plain array of parts: its first slots held 0x37 and naming it crashed
// the match server a few seconds after a player landed. Memory is checked first (the crash reporter kills the server
// on any bad read, even inside __try), then the object array must point back at it.
static std::string SyndicateObjectNameSafe(const UObject* Obj){
    if(!Obj)return "none";
    MEMORY_BASIC_INFORMATION M{};
    if(!VirtualQuery(Obj,&M,sizeof(M)) || M.State!=MEM_COMMIT || (M.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || (const uint8*)Obj+0x30>(const uint8*)M.BaseAddress+M.RegionSize)return "?";
    const int32 Index=Obj->Index;
    if(Index<0 || Index>=TUObjectArray::Num() || TUObjectArray::GetObjectByIndex(Index)!=Obj)return "?";
    return Obj->Name.ToString().c_str();
}
// Diagnostics for bugs that only show inside a match (Season X Wild West skin, Season 6 Guided Missile): every change
// of a real player's controlled pawn, body movement mode and head/body parts goes to the match server log, with names.
static void SyndicatePlayerChangeLog(AFortPlayerControllerAthena* PC){
    struct Seen{UObject* Pawn=nullptr;const UObject* Parts[2]{};uint8 Move=255;};
    static std::unordered_map<AFortPlayerControllerAthena*,Seen> Last;static UWorld* LastWorld=nullptr;
    if(LastWorld!=UWorld::GetWorld()){LastWorld=UWorld::GetWorld();Last.clear();}
    auto& L=Last[PC];auto Pawn=(UObject*)PC->Pawn;
    if(Pawn!=L.Pawn){printf("[SyndicateDiag] player now controls %s\n",Pawn?Pawn->Class->Name.ToString().c_str():"nothing");L.Pawn=Pawn;}
    if(auto Fort=(AFortPlayerPawnAthena*)PC->MyFortPawn)if(auto M=(UObject*)Fort->CharacterMovement){auto O=M->GetOffset("MovementMode");
        if(O!=uint32(-1)){uint8 Mode=GetFromOffset<uint8>(M,O);if(Mode!=L.Move){printf("[SyndicateDiag] player body movement mode %d -> %d (%s)\n",int(L.Move),int(Mode),Pawn==(UObject*)Fort?"controlling own body":"controlling something else");L.Move=Mode;}}}
    // Same lookup as the outfit code below: newer seasons keep the parts inside a CustomCharacterParts struct.
    if(auto State=PC->PlayerState){static uint32 O=[&]{auto Off=State->GetOffset("CharacterParts",0x100000);
            if(Off!=uint32(-1)){auto Struct=FindStruct("CustomCharacterParts");auto Inner=Struct?Struct->GetOffset("Parts"):uint32(-1);return Inner==uint32(-1)?uint32(-1):Off+Inner;}
            return State->GetOffset("CharacterParts");}();
        if(O!=uint32(-1)){
        auto& Now=GetFromOffset<const UObject*[6]>(State,O);
        for(int I=0;I<2;++I)if(Now[I]!=L.Parts[I]){printf("[SyndicateDiag] player %s part is now %s\n",I?"body":"head",SyndicateObjectNameSafe(Now[I]).c_str());L.Parts[I]=Now[I];}}}
}
void SyndicateOutfitWatchdog(){
    static ULONGLONG Next=0;auto Now=GetTickCount64();if(Now<Next)return;Next=Now+300;
    auto World=UWorld::GetWorld();auto Net=World?(UNetDriver*)World->NetDriver:nullptr;if(!Net)return;
    for(auto Conn:Net->ClientConnections){
        auto PC=Conn?(AFortPlayerControllerAthena*)Conn->PlayerController:nullptr;if(!PC)continue;
        SyndicatePlayerChangeLog(PC);
        auto State=PC->PlayerState;if(!State)continue;
        auto It=SyndicateOutfits.find(State);if(It==SyndicateOutfits.end())continue;auto& R=It->second;
        if(R.Off==uint32(-1)||Now<R.Next)continue;
        auto Pawn=(AFortPlayerPawnAthena*)PC->MyFortPawn;if(!Pawn||Pawn->bActorIsBeingDestroyed)continue;
        auto& Current=GetFromOffset<const UObject*[6]>(State,R.Off);
        bool Changed=false;for(int I=0;I<6;++I)if(R.Parts[I]&&Current[I]!=R.Parts[I])Changed=true;
        if(!Changed)continue;
        R.Next=Now+800;
        for(uint8 I=0;I<6;I++)if(R.Parts[I]){Current[I]=R.Parts[I];Pawn->ServerChoosePart(I,R.Parts[I]);}
        if(auto Rep=State->GetFunction("OnRep_CharacterParts"))if(Rep->GetParamsNamed().Size==0)State->ProcessEvent(Rep,nullptr);
        State->ForceNetUpdate();Pawn->ForceNetUpdate();
        printf("[SyndicateLocker] outfit was reset by the game; restored the selected outfit\n");
    }
}

extern UObject* SyndicateLoadStwHero(const std::string& Id);
// The player's Save the World hero pick (launcher, while Unlock all is on): the backend writes hero=HID_... into
// Runtime\Server\locker\<name>.loadout. Empty when there is none.
static std::string SyndicateSavedStwHero(UObject* State)
{
    auto Raw=SyndicatePlayerNameOf(State);if(Raw.empty())return {};
    std::wstring Hex;const wchar_t* Digits=L"0123456789abcdef";
    for(unsigned char C:Raw){C=(unsigned char)std::tolower(C);Hex+=Digits[C>>4];Hex+=Digits[C&15];}
    wchar_t Ready[32768]{};if(!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768))return {};
    std::wstring Path=Ready;auto Slash=Path.find_last_of(L"\\/");if(Slash==std::wstring::npos)return {};
    Path.resize(Slash+1);Path+=L"locker\\"+Hex+L".loadout";
    FILE* File=nullptr;if(_wfopen_s(&File,Path.c_str(),L"rb") || !File)return {};
    std::string Hero;char Line[512]{};
    while(fgets(Line,sizeof(Line),File)){
        std::string Row=Line;while(!Row.empty() && (Row.back()=='\r' || Row.back()=='\n'))Row.pop_back();
        if(Row.rfind("hero=",0)==0){Hero=Row.substr(5);break;}
    }
    fclose(File);return Hero;
}

// Syndicate custom outfits (Season 3, while Unlock all is on). The locker lists them as their own outfits
// (backend syndicateCustomOutfits.js; the Syndicate menu registers the item on the client). The backend saves the
// base outfit as the character and adds custom=<name> to Runtime\Server\locker\<name>.loadout.
// "phantom": The Reaper's suit (CID_084) with the default Spitfire head.
static std::string SyndicateSavedCustomOutfit(UObject* State)
{
    auto Raw=SyndicatePlayerNameOf(State);if(Raw.empty())return {};
    std::wstring Hex;const wchar_t* Digits=L"0123456789abcdef";
    for(unsigned char C:Raw){C=(unsigned char)std::tolower(C);Hex+=Digits[C>>4];Hex+=Digits[C&15];}
    wchar_t Ready[32768]{};if(!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768))return {};
    std::wstring Path=Ready;auto Slash=Path.find_last_of(L"\\/");if(Slash==std::wstring::npos)return {};
    Path.resize(Slash+1);Path+=L"locker\\"+Hex+L".loadout";
    FILE* File=nullptr;if(_wfopen_s(&File,Path.c_str(),L"rb") || !File)return {};
    std::string Custom;char Line[512]{};
    while(fgets(Line,sizeof(Line),File)){
        std::string Row=Line;while(!Row.empty() && (Row.back()=='\r' || Row.back()=='\n'))Row.pop_back();
        if(Row.rfind("custom=",0)==0){Custom=Row.substr(7);break;}
    }
    fclose(File);return Custom;
}

// A hero's parts by type (head 0, body 1, hat 2, backpack 3, charm 4, face 5), as RestoreSyndicateOutfit reads them.
static void SyndicateHeroPartsOf(UFortHeroType* Hero,const UObject* (&Parts)[6])
{
    if(!Hero)return;
    if(VersionInfo.EngineVersion<=4.16){SyndicateHeroPartsLegacy(Hero,Parts);return;}
    for(auto& SoftSpec:Hero->Specializations){auto Spec=SoftSpec.Get();
        if(!Spec){auto Path=SyndicateSoftPath(SoftSpec);if(!Path.empty())Spec=(const UFortHeroSpecialization*)StaticLoadObject(Path.c_str(),UFortHeroSpecialization::StaticClass());}
        if(Spec)for(auto& SoftPart:Spec->CharacterParts){auto Part=SoftPart.Get();
            if(!Part){auto Path=SyndicateSoftPath(SoftPart);if(!Path.empty())Part=(const UCustomCharacterPart*)StaticLoadObject(Path.c_str(),UCustomCharacterPart::StaticClass());}
            if(Part && Part->CharacterPartType<6)Parts[Part->CharacterPartType]=Part;
        }
    }
}

// The default Spitfire head: the male default recruit whose head part is M_Med_BLK_Sydney_Head_01_ATH (CID_006 on
// 3.5, the S3 log listed CID_005 Soldier, 006 BLK_Sydney, 007 HIS_Sparks, 008 ASN_Jae). Its head, hat
// and face parts replace the outfit's (a slot the default has nothing in is emptied). Found once, then cached.
static bool SyndicateSpitfireHead(const UObject* (&Head)[6])
{
    static const UObject* Found[6]{};static int State=0; // 0 not looked, 1 found, -1 missing
    if(State==0){
        State=-1;
        for(int N:{6,5,7,8}){if(State>0)break;
            wchar_t Path[160];swprintf_s(Path,L"/Game/Athena/Items/Cosmetics/Characters/CID_%03d_Athena_Commando_M_Default.CID_%03d_Athena_Commando_M_Default",N,N);
            auto Recruit=SyndicateAssetAvailable(Path)?(UAthenaCharacterItemDefinition*)StaticLoadObject(Path,UAthenaCharacterItemDefinition::StaticClass()):nullptr;
            if(!Recruit || !Recruit->HeroDefinition){printf("[SyndicateLocker] phantom: CID_%03d not found\n",N);continue;}
            const UObject* Parts[6]{};SyndicateHeroPartsOf(Recruit->HeroDefinition,Parts);
            std::string Name=Parts[0]?std::string(Parts[0]->Name.ToString().c_str()):std::string("none");
            printf("[SyndicateLocker] phantom: CID_%03d head %s hat %s face %s\n",N,Name.c_str(),Parts[2]?Parts[2]->Name.ToString().c_str():"none",Parts[5]?Parts[5]->Name.ToString().c_str():"none");
            std::string Lower=Name;for(auto& C:Lower)C=(char)std::tolower((unsigned char)C);
            if(Parts[0] && (Lower.find("sydney")!=std::string::npos || Lower.find("_blk_")!=std::string::npos)){for(int I=0;I<6;I++)Found[I]=Parts[I];State=1;}
        }
        if(State<0)printf("[SyndicateLocker] phantom: no Spitfire head found; the outfit keeps The Reaper's head\n");
    }
    if(State<0)return false;
    for(int I=0;I<6;I++)Head[I]=Found[I];
    return true;
}

int RestoreSyndicateOutfit(AFortPlayerControllerAthena* PlayerController,AFortPlayerPawnAthena* FortPawn,UAthenaCharacterPartItemDefinition* SavedBackpack,bool HasSavedBackpack,const std::vector<std::pair<std::string,std::string>>& SavedStyles)
{
    // Reapply the selected outfit on every possession, including the aircraft pawn.
    if(SyndicateLockerBuild()){
        UAthenaCharacterItemDefinition* Character=nullptr;
        if(PlayerController->HasCosmeticLoadoutPC())Character=PlayerController->CosmeticLoadoutPC.Character;
        if(!Character && PlayerController->HasCustomizationLoadout())Character=PlayerController->CustomizationLoadout.Character;
        // The 4.00 dedicated-host loadout can remain the default; prefer the local account's saved locker.
        if(auto Raw=SyndicatePlayerNameOf(PlayerController->PlayerState);!Raw.empty()){
            std::wstring Hex;const wchar_t* Digits=L"0123456789abcdef";
            for(unsigned char C:Raw){C=(unsigned char)std::tolower(C);Hex+=Digits[C>>4];Hex+=Digits[C&15];}
            wchar_t Ready[32768]{};if(GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768)){
                std::wstring Path=Ready;auto Slash=Path.find_last_of(L"\\/");if(Slash!=std::wstring::npos){Path.resize(Slash+1);Path+=L"locker\\"+Hex+L".txt";
                    char Id[256]{};size_t N=0;
                    FILE* File=nullptr;if(_wfopen_s(&File,Path.c_str(),L"rb")==0 && File){N=fread(Id,1,255,File);fclose(File);
                        while(N>0 && (Id[N-1]=='\n'||Id[N-1]=='\r'||Id[N-1]==' '||Id[N-1]=='\t'))Id[--N]=0;
                    }
                    // Default outfit - the player picked the empty/default locker slot (backend writes "DEFAULT"),
                    // OR has no saved locker file at all (e.g. a fresh Pre-season player who would otherwise keep the
                    // raw host character / Ramirez): roll a random default recruit each match, like regular Fortnite,
                    // so they get a BR default. Chapter 2 (>=11) uses CID_556..563 RebirthDefault A-D, F then M;
                    // earlier seasons use CID_001..008.
                    if(N==0 || _stricmp(Id,"DEFAULT")==0){
                        if(VersionInfo.FortniteVersion>=11){int P=rand()%8;int Num=556+P;char Sex=P<4?'F':'M';char Let=char('A'+(P%4));
                            N=(size_t)snprintf(Id,sizeof(Id),"CID_%03d_Athena_Commando_%c_RebirthDefault%c",Num,Sex,Let);}
                        else {int P=1+(rand()%8);N=(size_t)snprintf(Id,sizeof(Id),"CID_%03d_Athena_Commando_%c_Default",P,P<=4?'F':'M');}
                    }
                    bool Valid=N>4 && _strnicmp(Id,"CID_",4)==0;for(size_t I=0;I<N;I++)if(!std::isalnum((unsigned char)Id[I]) && Id[I]!='_')Valid=false;
                    if(Valid){std::string Asset="/Game/Athena/Items/Cosmetics/Characters/"+std::string(Id)+"."+Id;std::wstring Wide(Asset.begin(),Asset.end());auto Saved=SyndicateAssetAvailable(Wide.c_str())?(UAthenaCharacterItemDefinition*)StaticLoadObject(Wide.c_str(),UAthenaCharacterItemDefinition::StaticClass()):nullptr;if(Saved)Character=Saved;}
                }
            }
        }
        int Count=0;
        // A Save the World hero pick replaces the outfit's own hero: its head and body parts go on the player.
        UFortHeroType* StwHero=nullptr;
        if(auto HeroId=SyndicateSavedStwHero(PlayerController->PlayerState);!HeroId.empty()){
            StwHero=(UFortHeroType*)SyndicateLoadStwHero(HeroId);
            printf("[SyndicateLocker] STW hero %s %s\n",HeroId.c_str(),StwHero?"loaded":"is not in this build; keeping the outfit");
        }
        // Pre-season (1.7.2) has no outfit items: its characters are Athena hero types. With no outfit found, a random
        // one of those (like the real Pre-season's random recruit each match), instead of the same orange spawn body.
        if(!StwHero && !(Character && Character->HeroDefinition) && VersionInfo.EngineVersion<=4.16){
            auto& Heroes=SyndicateRegistryAthenaHeroes();static const UClass* HeroClass=FindClass("FortHeroType");
            // One pick per player per match: the island pawn and the match pawn must wear the same recruit.
            static std::unordered_map<AFortPlayerControllerAthena*,size_t> Picked;static UWorld* PickWorld=nullptr;
            if(PickWorld!=UWorld::GetWorld()){PickWorld=UWorld::GetWorld();Picked.clear();}
            for(int Try=0;Try<6 && !StwHero && !Heroes.empty() && HeroClass;++Try){
                auto Kept=Picked.find(PlayerController);
                const size_t Index=(Try==0 && Kept!=Picked.end() && Kept->second<Heroes.size())?Kept->second:size_t(rand())%Heroes.size();
                Picked[PlayerController]=Index;
                auto& Pick=Heroes[Index];
                StwHero=(UFortHeroType*)StaticLoadObject(Pick.first.c_str(),HeroClass);
                if(StwHero){std::string N(Pick.second.begin(),Pick.second.end());printf("[SyndicateLocker] Pre-season recruit %s\n",N.c_str());}
            }
        }
        if(StwHero || (Character && Character->HeroDefinition)){
            auto Hero=StwHero?StwHero:Character->HeroDefinition;const UObject* Parts[6]{};
            // On UE 4.16 (1.7.2-1.8.2) FSoftObjectPtr::Get() reads an incompatible weak-pointer layout and
            // crashes on possession (access violation in InternalGet), so there we skip Get() and resolve the
            // outfit's specializations and parts by their soft path instead, like the rest of this file does.
            const bool CanSoftGet=VersionInfo.EngineVersion>4.16;
            if(CanSoftGet){
                for(auto& SoftSpec:Hero->Specializations){auto Spec=SoftSpec.Get();
                    if(!Spec){auto Path=SyndicateSoftPath(SoftSpec);if(!Path.empty())Spec=(const UFortHeroSpecialization*)StaticLoadObject(Path.c_str(),UFortHeroSpecialization::StaticClass());}
                    if(Spec)for(auto& SoftPart:Spec->CharacterParts){auto Part=SoftPart.Get();
                        if(!Part){auto Path=SyndicateSoftPath(SoftPart);if(!Path.empty())Part=(const UCustomCharacterPart*)StaticLoadObject(Path.c_str(),UCustomCharacterPart::StaticClass());}
                        if(Part && Part->CharacterPartType<6)Parts[Part->CharacterPartType]=Part;
                    }
                }
            }else SyndicateHeroPartsLegacy(Hero,Parts);
            if(!StwHero)ApplySyndicateStyles(PlayerController,FortPawn,Character,SavedStyles,Parts); // outfit styles don't apply to a hero
            // Custom outfit "phantom" (Season 3): The Reaper's suit with the default Spitfire head.
            bool ClearSlot[6]{};
            if(!StwHero && VersionInfo.FortniteVersion>=3.5 && VersionInfo.FortniteVersion<4 && Character &&
               _stricmp(Character->Name.ToString().c_str(),"CID_084_Athena_Commando_M_Assassin")==0 && SyndicateSavedCustomOutfit(PlayerController->PlayerState)=="phantom"){
                const UObject* Head[6]{};
                if(SyndicateSpitfireHead(Head)){
                    for(int I:{0,2,5}){Parts[I]=Head[I];ClearSlot[I]=!Head[I];}
                    printf("[SyndicateLocker] phantom: The Reaper's suit with Spitfire's head\n");
                }
            }
            if(Parts[0] && Parts[1]){
                auto State=PlayerController->PlayerState;
                auto Off=State->GetOffset("CharacterParts",0x100000);
                if(Off!=uint32(-1)){auto Struct=FindStruct("CustomCharacterParts");auto Inner=Struct?Struct->GetOffset("Parts"):uint32(-1);Off=Inner==uint32(-1)?uint32(-1):Off+Inner;}
                else {Off=State->GetOffset("CharacterParts");if(Off==uint32(-1))Off=State->GetOffset("LocalCharacterParts");}
                // With an STW hero the player keeps the outfit's Battle Royale hero type: only the look changes.
                // (The STW hero type as HeroType left the player unable to open the glider.)
                if(State->HasHeroType() && (!StwHero || (Character && Character->HeroDefinition)))State->HeroType=StwHero?Character->HeroDefinition:Hero;
                // Seasons 3 and 5 rebuild the visible character from the loadout's Character, which
                // still held the default outfit and overwrote the saved one. Put the saved outfit in
                // the loadout first, rebuild, then set the parts explicitly.
                if((VersionInfo.FortniteVersion>=3.5 && VersionInfo.FortniteVersion<4) || VersionInfo.FortniteVersion>=5){
                    auto LoadoutType=FindStruct("FortAthenaLoadout");auto CharacterField=LoadoutType?LoadoutType->GetOffset("Character"):uint32(-1);
                    if(CharacterField!=uint32(-1))for(UObject* Owner:{(UObject*)PlayerController,(UObject*)FortPawn})
                        for(auto Property:{"CosmeticLoadoutPC","CustomizationLoadout","CosmeticLoadout"}){
                            auto O=Owner->GetOffset(Property);auto P=Owner->GetProperty(Property);
                            if(O!=uint32(-1) && P && CharacterField+sizeof(void*)<=GetFromOffset<uint32>(P,Offsets::ElementSize))
                                GetFromOffset<const UObject*>(Owner,O+CharacterField)=Character;
                        }
                    UFortKismetLibrary::UpdatePlayerCustomCharacterPartsVisualization(State);
                    if(!UFortKismetLibrary::UpdatePlayerCustomCharacterPartsVisualization__Ptr && ApplyCharacterCustomization)
                        ((void (*)(AActor*, AActor*))ApplyCharacterCustomization)(State, FortPawn);
                }
                for(uint8 I=0;I<6;I++)if(Parts[I]){if(Off!=uint32(-1))GetFromOffset<const UObject*[6]>(State,Off)[I]=Parts[I];FortPawn->ServerChoosePart(I,Parts[I]);Count++;}
                for(uint8 I=0;I<6;I++)if(ClearSlot[I] && Off!=uint32(-1))GetFromOffset<const UObject*[6]>(State,Off)[I]=nullptr; // the base outfit's own hat/face
                if((VersionInfo.FortniteVersion>=3.5 && VersionInfo.FortniteVersion<4) || VersionInfo.FortniteVersion>=5)
                    if(auto Rep=State->GetFunction("OnRep_CharacterParts"))if(Rep->GetParamsNamed().Size==0)State->ProcessEvent(Rep,nullptr);
                State->ForceNetUpdate();FortPawn->ForceNetUpdate();
                if(VersionInfo.FortniteVersion>=6 && Off!=uint32(-1)){auto& Record=SyndicateOutfits[State];Record.Off=Off;Record.Next=0;
                    for(int I=0;I<6;I++)Record.Parts[I]=Parts[I];
                    if(SavedBackpack)for(auto Part:SavedBackpack->CharacterParts)if(Part && Part->CharacterPartType<6)Record.Parts[Part->CharacterPartType]=Part;}
            }
        }
        if(HasSavedBackpack && !SavedBackpack && VersionInfo.FortniteVersion>=3){
            auto Empty=SyndicateAssetAvailable(L"/Game/Characters/CharacterParts/Backpacks/NoBackpack.NoBackpack")?(UObject*)StaticLoadObject(L"/Game/Characters/CharacterParts/Backpacks/NoBackpack.NoBackpack",UCustomCharacterPart::StaticClass()):nullptr;
            if(Empty)FortPawn->ServerChoosePart(3,Empty);
        }
        if(SavedBackpack)for(auto Part:SavedBackpack->CharacterParts)
            if(Part && Part->CharacterPartType<6)FortPawn->ServerChoosePart(Part->CharacterPartType,Part);
        if(auto Notify=FortPawn->GetFunction("OnRep_CustomizationLoadout"))if(Notify->GetParamsNamed().Size==0)FortPawn->ProcessEvent(Notify,nullptr);
        if(auto Notify=FortPawn->GetFunction("OnRep_CosmeticLoadout"))if(Notify->GetParamsNamed().Size==0)FortPawn->ProcessEvent(Notify,nullptr);
        FortPawn->ForceNetUpdate();
        wchar_t Ready[32768]{};
        if(GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768)){FILE* Log=nullptr;auto Path=std::wstring(Ready)+L".bot.log";if(_wfopen_s(&Log,Path.c_str(),L"a")==0 && Log){fprintf(Log,"human outfit: selected=%s appliedParts=%d\n",Character?Character->Name.ToString().c_str():"missing controller loadout",Count);fclose(Log);}}
        printf("[SyndicateLocker] outfit=%s parts=%d\n",Character?Character->Name.ToString().c_str():"none",Count);
        return Count;
    }

    return 0;
}


void ValidateSyndicateLegacyLocker(AFortPlayerControllerAthena* PC,AFortPlayerPawnAthena* Pawn){
    if(VersionInfo.FortniteVersion!=1.11 || !GetEnvironmentVariableW(L"TILTED_BOT_SELFTEST",nullptr,0))return;
    auto Fn=PC->PlayerState->GetFunction("GetPlayerName");if(!Fn)return;
    auto N=PC->PlayerState->Call<FString>(Fn);auto Raw=N.ToString();N.Free();std::wstring Hex;const wchar_t* Digits=L"0123456789abcdef";
    for(unsigned char C:Raw){C=(unsigned char)std::tolower(C);Hex+=Digits[C>>4];Hex+=Digits[C&15];}
    wchar_t Ready[32768]{};if(!GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768))return;
    auto Folder=std::filesystem::path(Ready).parent_path()/"locker";std::filesystem::create_directories(Folder);
    for(auto Id:{"CID_017_Athena_Commando_M","CID_035_Athena_Commando_M_Medieval","CID_034_Athena_Commando_F_Medieval"}){
        std::ofstream(Folder/(Hex+L".txt"))<<Id;
        std::ofstream(Folder/(Hex+L".loadout"))<<"character="<<Id<<(std::string(Id)=="CID_034_Athena_Commando_F_Medieval"?"\npickaxe=HalloweenScythe\nglider=Glider_ID_001\n":"\npickaxe=Pickaxe_ID_011_Medieval\nglider=Glider_ID_002_Medieval\n");
        bool HasBackpack=false;std::vector<std::pair<std::string,std::string>> Styles;auto Backpack=RestoreSyndicateAccessories(PC,Pawn,HasBackpack,Styles);
        RestoreSyndicateOutfit(PC,Pawn,Backpack,HasBackpack,Styles);
        auto L=FindStruct("FortAthenaLoadout");int Valid=0;
        if(L)for(auto Field:{"Pickaxe","Glider"}){auto F=L->GetOffset(Field);for(auto P:{"CosmeticLoadoutPC","CustomizationLoadout"}){auto O=PC->GetOffset(P);if(F!=uint32(-1)&&O!=uint32(-1)){auto Item=GetFromOffset<UObject*>(PC,O+F);if(Item){++Valid;printf("[S2LOCKER] %s=%s\n",Field,Item->Name.ToString().c_str());}break;}}}
        printf("[S2LOCKER] selected=%s accessoryFields=%d\n",Id,Valid);
    }
}
// Back from a Guided Missile (Season 6+): the player's character kept the remote-control pose, so it slid
// around with frozen walk/run animations. The pose comes from the weapon in hand (the launcher, whose
// animation set is "RemoteControl") and from the remote-control flags. Clear the flags, and when the weapon
// in hand is gone from the inventory (the missile was used up) or is still the launcher, put the pickaxe
// in hand, the way the game switches weapons after a used-up item.
static void SyndicateAfterRemoteControl(AFortPlayerControllerAthena* PC, AFortPlayerPawnAthena* Pawn)
{
	if (!PC || !Pawn || Pawn->bActorIsBeingDestroyed) return;
	int Cleared = 0;
	for (UObject* Owner : { (UObject*)PC, (UObject*)Pawn })
		for (auto Name : { "bIsUsingRemoteControlPawn", "bIsControllingRCPawn" })
			if (auto Prop = Owner->GetProperty(Name)) {
				auto Off = Owner->GetOffset(Name);
				if (Off != uint32(-1) && (GetFromOffset<uint8>(Owner, Off) & Prop->GetFieldMask())) { GetFromOffset<uint8>(Owner, Off) &= ~Prop->GetFieldMask(); ++Cleared; }
			}
	bool NeedWeapon = true;
	if (Pawn->HasCurrentWeapon())
		if (auto Weapon = Pawn->CurrentWeapon; Weapon && !Weapon->bActorIsBeingDestroyed) {
			std::string Name = Weapon->Class->Name.ToString().c_str();
			bool Launcher = Name.find("RemoteControl") != std::string::npos || Name.find("Guided") != std::string::npos;
			auto GuidOff = Weapon->GetOffset("ItemEntryGuid");
			bool InInventory = GuidOff == uint32(-1);
			if (GuidOff != uint32(-1) && PC->WorldInventory) {
				auto& Guid = GetFromOffset<FGuid>(Weapon, GuidOff);
				for (int i = 0; i < PC->WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
					if (PC->WorldInventory->Inventory.ReplicatedEntries.Get(i, FFortItemEntry::Size()).ItemGuid == Guid) { InInventory = true; break; }
			}
			NeedWeapon = Launcher || !InInventory;
		}
	bool Equipped = false;
	if (NeedWeapon && PC->WorldInventory)
		for (int i = 0; i < PC->WorldInventory->Inventory.ReplicatedEntries.Num(); i++) {
			auto& Entry = PC->WorldInventory->Inventory.ReplicatedEntries.Get(i, FFortItemEntry::Size());
			if (Entry.ItemDefinition && Entry.ItemDefinition->ItemType == EFortItemType::GetWeaponHarvest()) { PC->ServerExecuteInventoryItem(Entry.ItemGuid); Equipped = true; break; }
		}
	if (auto Move = Pawn->CharacterMovement) {
		auto O = Move->GetOffset("MovementMode");
		if (O != uint32(-1) && GetFromOffset<uint8>(Move, O) == 0) if (auto F = Move->GetFunction("SetMovementMode")) Move->Call<void>(F, uint8(1), uint8(0));
	}
	PC->ForceNetUpdate(); Pawn->ForceNetUpdate();
	printf("[Syndicate] after remote control: cleared %d flag(s), %s\n", Cleared, Equipped ? "pickaxe put in hand" : NeedWeapon ? "no pickaxe found" : "weapon in hand kept");
}

extern uint64_t ApplyCharacterCustomization;
uint64_t InitializePlayerGameplayAbilities_;
void AFortPlayerControllerAthena::ServerAcknowledgePossession(UObject* Context, FFrame& Stack)
{
	AActor* Pawn;
	Stack.StepCompiledIn(&Pawn);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	if (!Pawn)
		return;

	auto FortPawn = (AFortPlayerPawnAthena*)Pawn;

	static auto FortPCServerAcknowledgePossession = (void(*)(AFortPlayerControllerAthena*, AActor*))DefaultObjImpl("FortPlayerController")->Vft[Stack.GetCurrentNativeFunction()->GetVTableIndex()];
	FortPCServerAcknowledgePossession(PlayerController, Pawn);
	// Guided Missile (and other remote pawns) possess a projectile pawn, then hand
	// control back to the same player pawn. Neither is a new life: do not wipe
	// or rebuild the inventory, otherwise firing a Guided Missile empties it.
	{
		static std::unordered_map<void*, TWeakObjectPtr<AActor>> SyndicatePossessed;
		std::string PawnClass = Pawn->Class->Name.ToString().c_str();
		if (PawnClass.find("PlayerPawn") == std::string::npos)
		{
			printf("[Syndicate] remote pawn possessed (%s); inventory kept\n", PawnClass.c_str());
			return;
		}
		auto& Last = SyndicatePossessed[PlayerController];
		if (Last.Get() == Pawn)
		{
			printf("[Syndicate] control returned to the same pawn; inventory kept\n");
			SyndicateAfterRemoteControl(PlayerController, FortPawn);
			return;
		}
		Last = TWeakObjectPtr<AActor>(Pawn);
	}
	// Warmup possession also needs its character parts, even when inventory
	// already exists. Previously the refresh was restricted to Num == 0.
	AFortPlayerPawnAthena::RefreshSeasonTwoAppearance(FortPawn, PlayerController->PlayerState);

	auto Num = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Num();

	auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
	
    auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;
	if (Playlist && Playlist->RespawnType > 0 && Num > 0)
	{
		if (FConfiguration::bLateGame)
			FortPawn->SetShield(100.f);
	}
	if ((!FConfiguration::bKeepInventory || FConfiguration::bLateGame) && PlayerController->WorldInventory)
	{	
		UEAllocatedVector<FGuid> GuidsToRemove;
		for (int i = 0; i < PlayerController->WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
		{
			auto& Entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Get(i, FFortItemEntry::Size());

			if (Entry.ItemDefinition->CanBeDropped())
			{
				//NewPlayer->WorldInventory->Inventorxy.ReplicatedEntries.Remove(i, FFortItemEntry::Size());
				//i--;

				GuidsToRemove.push_back(Entry.ItemGuid);
			}
			if (VersionInfo.FortniteVersion < 3 && Entry.ItemDefinition->ItemType == EFortItemType::GetWeaponHarvest())
			{
				//PlayerController->ServerExecuteInventoryItem(Entry.ItemGuid);
				//PlayerController->QuickBars->ServerActivateSlotInternal(0, 0, 0.f, true);
			}
		}

		for (auto& Guid : GuidsToRemove)
			PlayerController->WorldInventory->Remove(Guid);
	}

	if (VersionInfo.FortniteVersion >= 18)
	{
		if (VersionInfo.FortniteVersion >= 25.20)
		{
			static auto Effect = FindObject<UClass>(L"/Game/Athena/SafeZone/GE_OutsideSafeZoneDamage.GE_OutsideSafeZoneDamage_C");

			bool Found = false;
			auto AbilitySystemComponent = PlayerController->PlayerState->AbilitySystemComponent;

			for (int i = 0; i < AbilitySystemComponent->ActiveGameplayEffects.GameplayEffects_Internal.Num(); i++)
			{
				auto& ActiveEffect = AbilitySystemComponent->ActiveGameplayEffects.GameplayEffects_Internal.Get(i, FActiveGameplayEffect::Size());

				if (ActiveEffect.Spec.Def)
					if (ActiveEffect.Spec.Def->IsA(Effect))
					{
						Found = true;
						break;
					}
			}

			if (!Found)
			{
				auto EffectHandle = FGameplayEffectContextHandle();
				auto SpecHandle = AbilitySystemComponent->BP_ApplyGameplayEffectToSelf(Effect, 0.f, EffectHandle);

				//AbilitySystemComponent->SetActiveGameplayEffectLevel(SpecHandle, 1);

				AbilitySystemComponent->UpdateActiveGameplayEffectSetByCallerMagnitude(SpecHandle,
					FGameplayTag(FName(L"SetByCaller.StormCampingDamage")), 1.f);
			}
		}

		FortPawn->bIsInAnyStorm = false;
		FortPawn->OnRep_IsInAnyStorm();
		FortPawn->bIsInsideSafeZone = true;
		FortPawn->OnRep_IsInsideSafeZone();
	}

	if (VersionInfo.FortniteVersion < 15 && Num > 0 && Playlist && Playlist->RespawnType > 0)
	{
		auto Interface = PlayerController->PlayerState->GetInterface(IFortAbilitySystemInterface::StaticClass());
		if (InitializePlayerGameplayAbilities_ && Interface)
		{
			auto InitializePlayerGameplayAbilities = (void(*&)(const IInterface*))InitializePlayerGameplayAbilities_;

			InitializePlayerGameplayAbilities(Interface);
		}
		else
			for (auto& AbilitySet : AFortGameMode::AbilitySets)
				PlayerController->PlayerState->AbilitySystemComponent->GiveAbilitySet(AbilitySet);
	}

    bool HasSavedBackpack=false;
    std::vector<std::pair<std::string,std::string>> SavedStyles;
    auto SavedBackpack=RestoreSyndicateAccessories(PlayerController,FortPawn,HasSavedBackpack,SavedStyles);
	if (Num == 0)
	{
		static auto SmartItemDefClass = FindClass("FortSmartBuildingItemDefinition");
		static bool HasCosmeticLoadoutPC = PlayerController->HasCosmeticLoadoutPC();
		static bool HasCustomizationLoadout = PlayerController->HasCustomizationLoadout();

		// A saved pickaxe whose weapon definition is missing in this build must not leave the player without one.
		if (HasCosmeticLoadoutPC && PlayerController->CosmeticLoadoutPC.Pickaxe && PlayerController->CosmeticLoadoutPC.Pickaxe->WeaponDefinition)
			PlayerController->WorldInventory->GiveItem(PlayerController->CosmeticLoadoutPC.Pickaxe->WeaponDefinition);
		else if (HasCustomizationLoadout && PlayerController->CustomizationLoadout.Pickaxe && PlayerController->CustomizationLoadout.Pickaxe->WeaponDefinition)
			PlayerController->WorldInventory->GiveItem(PlayerController->CustomizationLoadout.Pickaxe->WeaponDefinition);
		else // Use a default only when no saved or replicated pickaxe is available.
		{
			static auto DefaultPickaxe = FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01");

			PlayerController->WorldInventory->GiveItem(DefaultPickaxe);
		}
		
		if (GameMode->StartingItems.Num() == 0)
		{

			static auto WallBuild = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall");
			static auto FloorBuild = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor");
			static auto StairBuild = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W");
			static auto ConeBuild = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS");
			static auto EditTool = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/EditTool.EditTool");

			PlayerController->WorldInventory->GiveItem(WallBuild);
			PlayerController->WorldInventory->GiveItem(FloorBuild);
			PlayerController->WorldInventory->GiveItem(StairBuild);
			PlayerController->WorldInventory->GiveItem(ConeBuild);
			PlayerController->WorldInventory->GiveItem(EditTool);
		}
		else
			for (int i = 0; i < GameMode->StartingItems.Num(); i++)
			{
				auto& StartingItem = GameMode->StartingItems.Get(i, FItemAndCount::Size());

				if (StartingItem.Count && (!SmartItemDefClass || !StartingItem.Item->IsA(SmartItemDefClass)))
					PlayerController->WorldInventory->GiveItem(StartingItem.Item, StartingItem.Count);
			}


		/*auto pickaxeEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([](FFortItemEntry& entry)
			{ return entry.ItemDefinition->IsA<UFortWeaponMeleeItemDefinition>(); }, FFortItemEntry::Size());

		if (pickaxeEntry && VersionInfo.FortniteVersion > 3)
		{
			PlayerController->ServerExecuteInventoryItem(pickaxeEntry->ItemGuid);
			PlayerController->ClientEquipItem(pickaxeEntry->ItemGuid, true);
		}*/

		UFortKismetLibrary::UpdatePlayerCustomCharacterPartsVisualization(PlayerController->PlayerState);
		if (!UFortKismetLibrary::UpdatePlayerCustomCharacterPartsVisualization__Ptr && ApplyCharacterCustomization)
		{
			((void (*)(AActor*, AActor*)) ApplyCharacterCustomization)(PlayerController->PlayerState, Pawn);
		}


		auto Interface = PlayerController->PlayerState->GetInterface(IFortAbilitySystemInterface::StaticClass());
		if (InitializePlayerGameplayAbilities_ && Interface)
		{
			auto InitializePlayerGameplayAbilities = (void(*&)(const IInterface*))InitializePlayerGameplayAbilities_;

			InitializePlayerGameplayAbilities(Interface);
		}
		else
			for (auto& AbilitySet : AFortGameMode::AbilitySets)
				PlayerController->PlayerState->AbilitySystemComponent->GiveAbilitySet(AbilitySet);
	}
	else if (FConfiguration::bLateGame && (!FConfiguration::bKeepInventory || FConfiguration::bLateGame))
	{
		// Items missing from this build come back empty (Item null) and are skipped, never dereferenced.
		auto ClipOf = [](const UFortItemDefinition* Item) -> int
		{
			if (!Item)
				return 0;
			auto Weapon = Item->IsA<UFortGadgetItemDefinition>() ? ((UFortGadgetItemDefinition*)Item)->GetWeaponItemDefinition() : Item->Cast<UFortWeaponItemDefinition>();
			auto Stats = Weapon ? AFortInventory::GetStats(Weapon) : nullptr;
			return Stats ? Stats->ClipSize : 0;
		};
		auto Give = [&](const UFortItemDefinition* Item, int Count, int Loaded = 0)
		{
			if (Item && Count > 0)
				PlayerController->WorldInventory->GiveItem(Item, Count, Loaded);
		};

		// Chapter 1 (Edwin's list, 2026-11-17), rolled again for every player and every match:
		//   1 a rifle (SCAR, blue AR, burst or scoped), 2 a shotgun (any kind and rarity), 3 an SMG (30%) or a healable (70%),
		//   4 a sniper, rocket launcher, grenade launcher or grenades, 5 a consumable; plus a trap half the time (damage or movement).
		// Chapter 2 and later builds use their own rifle / shotgun / utility lists in the same slots (LateGame.cpp).
		auto AssaultRifle = LateGame::GetRifle();
		auto Shotgun = LateGame::GetAnyShotgun();
		auto Slot3 = LateGame::GetSmgOrHeal();
		auto Slot4 = LateGame::GetExplosiveOrSniper();
		auto Slot5 = LateGame::GetConsumable();
		for (int Try = 0; Try < 4 && Slot5.Item && Slot5.Item == Slot3.Item; Try++)
			Slot5 = LateGame::GetConsumable(); // two different heals when slot 3 rolled one
		auto Trap = LateGame::GetTrap();

		Give(LateGame::GetResource(EFortResourceType::Wood), 500);
		Give(LateGame::GetResource(EFortResourceType::Stone), 500);
		Give(LateGame::GetResource(EFortResourceType::Metal), 500);

		Give(LateGame::GetAmmo(EAmmoType::Assault), 250);
		Give(LateGame::GetAmmo(EAmmoType::Shotgun), 50);
		Give(LateGame::GetAmmo(EAmmoType::Submachine), 400);
		Give(LateGame::GetAmmo(EAmmoType::Rocket), 6);
		Give(LateGame::GetAmmo(EAmmoType::Sniper), 20);

		Give(AssaultRifle.Item, AssaultRifle.Count, ClipOf(AssaultRifle.Item));
		Give(Shotgun.Item, Shotgun.Count, ClipOf(Shotgun.Item));
		Give(Slot3.Item, Slot3.Count, ClipOf(Slot3.Item));
		Give(Slot4.Item, Slot4.Count, ClipOf(Slot4.Item));
		if (Slot5.Item != Slot3.Item)
			Give(Slot5.Item, Slot5.Count, ClipOf(Slot5.Item));
		else
			Give(Slot5.Item, Slot5.Count, 0); // only one kind of heal in this build: a second stack of it
		Give(Trap.Item, Trap.Count);
		auto NameOf = [](const FLateGameItem& I, const char* None) { return I.Item ? std::string(I.Item->Name.ToString().c_str()) : std::string(None); };
		printf("[Syndicate] late game loadout: %s, %s, %s, %s, %s, trap: %s\n",
			NameOf(AssaultRifle, "no rifle").c_str(), NameOf(Shotgun, "no shotgun").c_str(), NameOf(Slot3, "nothing").c_str(),
			NameOf(Slot4, "nothing").c_str(), NameOf(Slot5, "nothing").c_str(), NameOf(Trap, "none").c_str());
	}
    RestoreSyndicateOutfit(PlayerController,FortPawn,SavedBackpack,HasSavedBackpack,SavedStyles);
}

uint32 ServerAttemptAircraftJumpVft;
void AFortPlayerControllerAthena::ServerAttemptAircraftJump_(UObject* Context, FFrame& Stack)
{
	FRotator Rotation;
	Stack.StepCompiledIn(&Rotation);
	Stack.IncrementCode();

	AFortPlayerControllerAthena* PlayerController = nullptr;
	auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;

	// Late game respawns the jumper with RestartPlayer from 2.00 on. Pre-season and Season 1 (1.7.2 to 1.11) keep the
	// game's own jump: the bus is parked over the late-game circle, so the native jump already skydives the pawn there,
	// while RestartPlayer put a non-skydiving pawn at a spawn pad and then in the sky.
	if (VersionInfo.FortniteVersion >= 11.00 || (FConfiguration::bLateGame && VersionInfo.FortniteVersion >= 2.00))
	{
		static auto bIsComp = Context->IsA(FindClass("FortControllerComponent_Aircraft"));
		if (bIsComp)
			PlayerController = (AFortPlayerControllerAthena*)((UActorComponent*)Context)->GetOwner();
		else
			PlayerController = (AFortPlayerControllerAthena*)Context;

		GameMode->RestartPlayer(PlayerController);
		PlayerController->ClientSetRotation(Rotation, true);

		if (PlayerController->MyFortPawn)
		{
			//PlayerController->MyFortPawn->BeginSkydiving(true);
			//PlayerController->MyFortPawn->SetHealth(100.f);
		}
	}
	else
	{
		static auto ServerAttemptAircraftJumpOG = (void(*)(AFortPlayerControllerAthena*, FRotator&)) ((AFortPlayerControllerAthena*)Context)->Vft[ServerAttemptAircraftJumpVft];

		ServerAttemptAircraftJumpOG((AFortPlayerControllerAthena*)Context, Rotation);
		PlayerController = (AFortPlayerControllerAthena*)Context;
	}

	// Leaving the bus ends the warmup: like the real game, everyone starts the match with 100 health
	// and no shield. Before 11.00 the game's own jump keeps the spawn-island pawn, so shield drunk on
	// the island carried into the match. Late game sets its own values below.
	if (!FConfiguration::bLateGame && PlayerController && PlayerController->MyFortPawn)
	{
		auto Pawn = PlayerController->MyFortPawn;
		const float HealthBefore = Pawn->GetHealth(), ShieldBefore = Pawn->GetShield();
		Pawn->SetHealth(100.f);
		Pawn->SetShield(0.f);
		printf("[Syndicate] bus jump reset: health %.0f -> %.0f, shield %.0f -> %.0f\n", HealthBefore, Pawn->GetHealth(), ShieldBefore, Pawn->GetShield());
	}
	else if (!FConfiguration::bLateGame)
		printf("[Syndicate] bus jump reset skipped: %s\n", PlayerController ? "no pawn yet" : "no controller");
	
	if (FConfiguration::bLateGame && PlayerController && PlayerController->MyFortPawn)
	{
		// Full health too: in late game the storm is already up during the bus, so the player's body left on the
		// spawn island could take storm damage before the jump.
		PlayerController->MyFortPawn->SetHealth(100.f);
		PlayerController->MyFortPawn->SetShield(100.f);
		auto Aircraft = GameState->HasAircrafts() ? (GameState->Aircrafts.Num() > 0 ? GameState->Aircrafts[0] : nullptr) : (GameState->HasAircraft() ? GameState->Aircraft : nullptr);
		if (!Aircraft && VersionInfo.FortniteVersion >= 11.00) // gamephaselogic builds
		{
			auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(UWorld::GetWorld());

			Aircraft = GamePhaseLogic ? GamePhaseLogic->Aircrafts_GameState[0].Get() : nullptr;
		}
		if (!Aircraft)
			return;

		FVector AircraftLocation = Aircraft->K2_GetActorLocation();

		float Angle = (float)rand() / 5215.03002625f;
		float Radius = (float)(rand() % 1000);

		float OffsetX = cosf(Angle) * Radius;
		float OffsetY = sinf(Angle) * Radius;

		FVector Offset;
		Offset.X = OffsetX;
		Offset.Y = OffsetY;
		Offset.Z = 0.0f;

		FVector NewLoc = AircraftLocation + Offset;

		PlayerController->MyFortPawn->K2_SetActorLocation(NewLoc, false, nullptr, true);
	}
}

// The seat weapon the pawn holds in its vehicle seat (the X-4 Stormwing's guns, the Baller's grapple...), as its inventory
// entry; null when the pawn is not in a seat that has one. While it is, the pawn must keep that weapon: switching to a
// quickbar slot used to equip the normal weapon inside the vehicle, so the plane shot your own gun instead of its guns
// and a Baller driver could swing the pickaxe and harvest from inside the ball.
static FFortItemEntry* SyndicateSeatWeaponEntry(AFortPlayerControllerAthena* PlayerController)
{
	auto Pawn = PlayerController ? PlayerController->MyFortPawn : nullptr;
	if (!Pawn) return nullptr;
	static auto GetVehicleFunc = Pawn->GetFunction("GetVehicleActor") ? Pawn->GetFunction("GetVehicleActor") : Pawn->GetFunction("GetVehicle");
	if (!GetVehicleFunc) return nullptr;
	AActor* Vehicle = Pawn->Call<AActor*>(GetVehicleFunc);
	if (!Vehicle) return nullptr;
	auto SeatWeaponComponent = (UFortVehicleSeatWeaponComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatWeaponComponent::StaticClass());
	auto SeatComponent = (UFortVehicleSeatComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatComponent::StaticClass());
	if (!SeatWeaponComponent || !SeatComponent) return nullptr;
	auto SeatIdx = SeatComponent->FindSeatIndex(Pawn);
	UFortWeaponItemDefinition* Weapon = nullptr;
	for (int i = 0; i < SeatWeaponComponent->WeaponSeatDefinitions.Num(); i++)
	{
		auto& Definition = SeatWeaponComponent->WeaponSeatDefinitions.Get(i, FWeaponSeatDefinition::Size());
		if (Definition.SeatIndex == SeatIdx) { Weapon = Definition.VehicleWeapon; break; }
	}
	if (!Weapon) return nullptr;
	// Only while the seat weapon is actually in the inventory (it is removed on exit and seat changes before the
	// previous weapon is equipped again).
	return PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{ return entry.ItemDefinition == Weapon; }, FFortItemEntry::Size());
}

void AFortPlayerControllerAthena::ServerExecuteInventoryItem_(UObject* Context, FFrame& Stack)
{
	FGuid ItemGuid;
	Stack.StepCompiledIn(&ItemGuid);
	Stack.IncrementCode();

	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	if (!PlayerController || !PlayerController->MyFortPawn)
		return;

	auto entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{
			return entry.ItemGuid == ItemGuid;
		}, FFortItemEntry::Size());

	if (!entry)
		return;

	// In a vehicle seat with its own weapon: keep that weapon (put it back in the client's hands).
	if (auto SeatEntry = SyndicateSeatWeaponEntry(PlayerController); SeatEntry && SeatEntry->ItemGuid != ItemGuid)
	{
		PlayerController->ClientEquipItem(SeatEntry->ItemGuid, true);
		return;
	}

	UFortItemDefinition* ItemDefinition = (UFortItemDefinition*)entry->ItemDefinition;

	if (auto Gadget = ItemDefinition->Cast<UFortGadgetItemDefinition>())
		ItemDefinition = Gadget->GetWeaponItemDefinition();
	else if (ItemDefinition->IsA(UFortDecoItemDefinition::StaticClass()))
	{
		PlayerController->MyFortPawn->PickUpActor(nullptr, ItemDefinition);
		((AFortWeapon*)PlayerController->MyFortPawn->CurrentWeapon)->ItemEntryGuid = ItemGuid;

		static auto ContextTrapClass = FindClass("FortDecoTool_ContextTrap");
		// Only a context trap (FortContextTrapItemDefinition) goes in this property. The Mounted Turret is a plain
		// FortTrapItemDefinition: putting it here made the client log "Forged object" and the server crash on placing it.
		if (PlayerController->MyFortPawn->CurrentWeapon->IsA(ContextTrapClass))
			((AFortWeapon*)PlayerController->MyFortPawn->CurrentWeapon)->ContextTrapItemDefinition =
				ItemDefinition->IsA(UFortContextTrapItemDefinition::StaticClass()) ? ItemDefinition : nullptr;

		return;
	}


	auto Weapon = PlayerController->MyFortPawn->EquipWeaponDefinition(ItemDefinition, ItemGuid, entry->HasTrackerGuid() ? entry->TrackerGuid : FGuid(), false);
	if (!Weapon)
		return;
	// Weapon swap speed: the client can't fire the new weapon until this actor (and the pawn's CurrentWeapon)
	// reach it. Send both on the next net update instead of waiting behind builds, pickups and bots
	// (NetDriver.cpp also puts each player's current weapon at the front of their send list).
	Weapon->ForceNetUpdate();
	PlayerController->MyFortPawn->ForceNetUpdate();
	if (VersionInfo.FortniteVersion <= 2.5)
	{
		static auto BuildingToolClass = FindClass("FortWeap_BuildingTool");
		if (Weapon->IsA(BuildingToolClass))
		{
			static auto RoofPiece = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS");
			static auto FloorPiece = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor");
			static auto WallPiece = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall");
			static auto StairPiece = FindObject<UFortItemDefinition>(L"/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W");

			static auto RoofMetadata = FindObject<UObject>(L"/Game/Building/EditModePatterns/Roof/EMP_Roof_RoofC.EMP_Roof_RoofC");
			static auto StairMetadata = FindObject<UObject>(L"/Game/Building/EditModePatterns/Stair/EMP_Stair_StairW.EMP_Stair_StairW");
			static auto WallMetadata = FindObject<UObject>(L"/Game/Building/EditModePatterns/Wall/EMP_Wall_Solid.EMP_Wall_Solid");
			static auto FloorMetadata = FindObject<UObject>(L"/Game/Building/EditModePatterns/Floor/EMP_Floor_Floor.EMP_Floor_Floor");

			static auto DefaultMetadataOffset = Weapon->GetOffset("DefaultMetadata");
			static auto OnRep_DefaultMetadata = Weapon->GetFunction("OnRep_DefaultMetadata");

			if (ItemDefinition == RoofPiece)
				GetFromOffset<const UObject*>(Weapon, DefaultMetadataOffset) = RoofMetadata;
			else if (ItemDefinition == StairPiece)
				GetFromOffset<const UObject*>(Weapon, DefaultMetadataOffset) = StairMetadata;
			else if (ItemDefinition == WallPiece)
				GetFromOffset<const UObject*>(Weapon, DefaultMetadataOffset) = WallMetadata;
			else if (ItemDefinition == FloorPiece)
				GetFromOffset<const UObject*>(Weapon, DefaultMetadataOffset) = FloorMetadata;

			Weapon->ProcessEvent(OnRep_DefaultMetadata, nullptr);
		}
	}

	if (auto DecoTool = Weapon->Cast<AFortDecoTool>())
	{
		DecoTool->ItemDefinition = ItemDefinition;

		if (auto ContextTrapTool = Weapon->Cast<AFortDecoTool_ContextTrap>())
			ContextTrapTool->ContextTrapItemDefinition = ItemDefinition->IsA(UFortContextTrapItemDefinition::StaticClass()) ? (UFortContextTrapItemDefinition*)ItemDefinition : nullptr;
	}
}


void AFortPlayerControllerAthena::ServerExecuteInventoryWeapon(UObject* Context, FFrame& Stack)
{
	AFortWeapon* Weapon;
	Stack.StepCompiledIn(&Weapon);
	Stack.IncrementCode();

	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	if (!PlayerController)
		return;

	auto entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{
			return entry.ItemGuid == Weapon->ItemEntryGuid;
		}, FFortItemEntry::Size());

	if (!entry || !PlayerController->MyFortPawn)
		return;

	UFortItemDefinition* ItemDefinition = (UFortItemDefinition*)entry->ItemDefinition;

	PlayerController->MyFortPawn->EquipWeaponDefinition(ItemDefinition, Weapon->ItemEntryGuid, entry->HasTrackerGuid() ? entry->TrackerGuid : FGuid(), false);

	if (ItemDefinition->IsA(UFortDecoItemDefinition::StaticClass()))
	{
		auto DecoTool = (AFortDecoTool*)Weapon;
		DecoTool->ItemDefinition = ItemDefinition;

		if (auto ContextTrapTool = Weapon->Cast<AFortDecoTool_ContextTrap>())
			ContextTrapTool->ContextTrapItemDefinition = ItemDefinition->IsA(UFortContextTrapItemDefinition::StaticClass()) ? (UFortContextTrapItemDefinition*)ItemDefinition : nullptr;
	}
}

bool CanBePlacedByPlayer(TSubclassOf<AActor> BuildClass)
{
	return ((ABuildingSMActor*)BuildClass->GetDefaultObj())->bIsPlayerBuildable;
	/*auto GameState = ((AFortGameStateAthena*)UWorld::GetWorld()->GameState);
	static auto HasAllPlayerBuildableClasses = GameState->HasAllPlayerBuildableClasses();
	return HasAllPlayerBuildableClasses ? GameState->AllPlayerBuildableClasses.Search([BuildClass](TSubclassOf<AActor> Class)
		{ return Class == BuildClass; }) != 0 : true;*/
}

uint64_t CantBuild_ = 0;
uint64_t CanAffordToPlaceBuildableClass_;
uint64_t PayBuildableClassPlacementCost_;
uint64_t CanPlaceBuildableClassInStructuralGrid_;
void AFortPlayerControllerAthena::ServerCreateBuildingActor(UObject* Context, FFrame& Stack)
{
	TSubclassOf<AActor> BuildingClass;
	FVector BuildLoc;
	FRotator BuildRot;
	bool bMirrored;
	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	// A knocked (DBNO) player can only crawl: no placing builds. The game normally stops this on the client, but a
	// knocked teammate could still build and edit here, so the server refuses it too.
	if (PlayerController && PlayerController->MyFortPawn && PlayerController->MyFortPawn->IsDBNO())
		return;
	struct _Pad_0xC
	{
		uint8_t Padding[0xC];
	};
	struct _Pad_0x18
	{
		uint8_t Padding[0x18];
	};

	FBuildingClassData BuildingClassData;
	if (VersionInfo.FortniteVersion >= 8.30)
	{
		struct FCreateBuildingActorData { uint32_t BuildingClassHandle; _Pad_0xC BuildLoc; _Pad_0xC BuildRot; bool bMirrored; uint8_t Pad_1[0x3]; float SyncKey; uint8 Pad_2[0x4]; FBuildingClassData BuildingClassData; };
		struct FCreateBuildingActorData_New { uint32_t BuildingClassHandle; uint8_t Pad_1[0x4]; _Pad_0x18 BuildLoc; _Pad_0x18 BuildRot; bool bMirrored; uint8_t Pad_2[0x3]; float SyncKey; FBuildingClassData BuildingClassData; };

		if (VersionInfo.FortniteVersion >= 20.00)
		{
			FCreateBuildingActorData_New CreateBuildingData;
			Stack.StepCompiledIn(&CreateBuildingData);

			BuildLoc = *(FVector*)&CreateBuildingData.BuildLoc;
			BuildRot = *(FRotator*)&CreateBuildingData.BuildRot;
			bMirrored = CreateBuildingData.bMirrored;
			BuildingClassData = CreateBuildingData.BuildingClassData;

			BuildingClass = AFortGameStateAthena::BuildingClassMap[CreateBuildingData.BuildingClassHandle];
			if (!BuildingClass)
			{
				Stack.IncrementCode();
				return;
			}
		}
		else
		{
			FCreateBuildingActorData CreateBuildingData;
			Stack.StepCompiledIn(&CreateBuildingData);

			BuildLoc = *(FVector*)&CreateBuildingData.BuildLoc;
			BuildRot = *(FRotator*)&CreateBuildingData.BuildRot;
			bMirrored = CreateBuildingData.bMirrored;
			BuildingClassData = CreateBuildingData.BuildingClassData;

			BuildingClass = AFortGameStateAthena::BuildingClassMap[CreateBuildingData.BuildingClassHandle];
			if (!BuildingClass)
			{
				Stack.IncrementCode();
				return;
			}
		}
		BuildingClassData.BuildingClass = BuildingClass;
	}
	else
	{
		Stack.StepCompiledIn(&BuildingClassData);
		Stack.StepCompiledIn(&BuildLoc);
		Stack.StepCompiledIn(&BuildRot);
		Stack.StepCompiledIn(&bMirrored);

		auto GameState = (AFortGameStateAthena*)UWorld::GetWorld()->GameState;
		static auto HasAllPlayerBuildableClasses = GameState->HasAllPlayerBuildableClasses();
		if (HasAllPlayerBuildableClasses && !GameState->AllPlayerBuildableClasses.Contains(BuildingClassData.BuildingClass))
		{
			Stack.IncrementCode();
			return;
		}

		BuildingClass = BuildingClassData.BuildingClass;
	}
	Stack.IncrementCode();

	if (!BuildingClass)
		return;

	UFortWorldItem* Item = nullptr;
	auto Resource = UFortKismetLibrary::K2_GetResourceItemDefinition(((ABuildingSMActor*)BuildingClass->GetDefaultObj())->ResourceType);
	if (!FConfiguration::bInfiniteMats)
	{
		auto CanAffordToPlaceBuildableClass = (bool(*)(AFortPlayerControllerAthena*, FBuildingClassData)) CanAffordToPlaceBuildableClass_;

		if (CanAffordToPlaceBuildableClass)
		{
			if (!CanAffordToPlaceBuildableClass(PlayerController, BuildingClassData))
				return;
		}
		else if (!PlayerController->bBuildFree)
		{

			auto ItemP = PlayerController->WorldInventory->Inventory.ItemInstances.Search([&](UFortWorldItem* entry)
				{ return entry->ItemEntry.ItemDefinition == Resource; });

			if (!ItemP)
				return;

			Item = *ItemP;

			if (Item->ItemEntry.Count < 10)
				return;
		}
	}

	TArray<ABuildingSMActor*> RemoveBuildings;
	if (VersionInfo.FortniteVersion >= 27)
	{
		char _Unk_OutVar1;
		auto CantBuild = (__int64 (*)(UWorld*, TSubclassOf<AActor>&, _Pad_0x18, _Pad_0x18, bool, TArray<ABuildingSMActor*> *, char*))CantBuild_;

		if (CantBuild(UWorld::GetWorld(), BuildingClass, *(_Pad_0x18*)&BuildLoc, *(_Pad_0x18*)&BuildRot, bMirrored, &RemoveBuildings, &_Unk_OutVar1))
			return;
	}
	else
	{
		char _Unk_OutVar1;
		auto CantBuild = (__int64 (*)(UWorld*, const UClass*, _Pad_0xC, _Pad_0xC, bool, TArray<ABuildingSMActor*> *, char*))CantBuild_;
		auto CantBuildNew = (__int64 (*)(UWorld*, const UClass*, _Pad_0x18, _Pad_0x18, bool, TArray<ABuildingSMActor*> *, char*))CantBuild_;

		if (VersionInfo.FortniteVersion >= 20.00 ? CantBuildNew(UWorld::GetWorld(), BuildingClass, *(_Pad_0x18*)&BuildLoc, *(_Pad_0x18*)&BuildRot, bMirrored, &RemoveBuildings, &_Unk_OutVar1) : CantBuild(UWorld::GetWorld(), BuildingClass, *(_Pad_0xC*)&BuildLoc, *(_Pad_0xC*)&BuildRot, bMirrored, &RemoveBuildings, &_Unk_OutVar1))
			return;
	}	

	for (auto& RemoveBuilding : RemoveBuildings)
		RemoveBuilding->K2_DestroyActor();
	RemoveBuildings.Free();

	static auto K2_SpawnBuildingActor = ABuildingSMActor::GetDefaultObj()->GetFunction("K2_SpawnBuildingActor");

	ABuildingSMActor* Building = nullptr;
	/*if (K2_SpawnBuildingActor)
	{
		FTransform SpawnTransform(BuildLoc, BuildRot);
		Building = ABuildingSMActor::K2_SpawnBuildingActor(PlayerController, BuildingClass, SpawnTransform, PlayerController, nullptr, false, false);
	}
	else*/
		Building = UWorld::SpawnActorUnfinished<ABuildingSMActor>(BuildingClass, BuildLoc, BuildRot, PlayerController);

		Building->InitializeKismetSpawnedBuildingActor(Building, PlayerController, true, nullptr, false);
		UWorld::FinishSpawnActor(Building, BuildLoc, BuildRot);

	if (!Building)
		return;

	static auto UpgradeLevelOffset = FBuildingClassData::StaticStruct()->GetOffset("UpgradeLevel");
	Building->CurrentBuildingLevel = VersionInfo.EngineVersion >= 5.3 ? *(uint8*)(__int64(&BuildingClassData) + UpgradeLevelOffset) : *(uint32*)(__int64(&BuildingClassData) + UpgradeLevelOffset);
	Building->OnRep_CurrentBuildingLevel();

	Building->SetMirrored(bMirrored);

	Building->bPlayerPlaced = true;


	if (((AFortPlayerStateAthena*)PlayerController->PlayerState)->HasTeamIndex())
		Building->Team = ((AFortPlayerStateAthena*)PlayerController->PlayerState)->TeamIndex;

	if (Building->HasTeamIndex())
		Building->TeamIndex = Building->Team;

	if (!PlayerController->bBuildFree && !FConfiguration::bInfiniteMats)
	{
		auto PayBuildableClassPlacementCost = (int(*)(AFortPlayerControllerAthena*, FBuildingClassData)) PayBuildableClassPlacementCost_;

		PayBuildableClassPlacementCost(PlayerController, BuildingClassData);
	}

	FGameplayTagContainer TargetTags{};

	auto Interface = (IGameplayTagAssetInterface*)Building->GetInterface(IGameplayTagAssetInterface::StaticClass());
	if (Interface)
	{
		auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2];
		GetOwnedGameplayTags(Interface, &TargetTags);
		//Interface->GetOwnedGameplayTags(&TargetTags);
	}

	PlayerController->GetQuestManager(1)->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetBuild(), 1, Building);

	TargetTags.GameplayTags.Free();
	TargetTags.ParentTags.Free();
}

void SetEditingPlayer(ABuildingSMActor* _this, AFortPlayerStateAthena* NewEditingPlayer)
{
	if (_this->Role == 3 && (!_this->EditingPlayer || !NewEditingPlayer))
	{
		_this->SetNetDormancy(2 - (NewEditingPlayer != 0));
		_this->ForceNetUpdate();

		auto EditingPlayer = _this->EditingPlayer;
		if (EditingPlayer)
		{
			auto Handle = EditingPlayer->Owner;

			if (Handle)
				if (auto PlayerController = Handle->Cast<AFortPlayerControllerAthena>())
				{
					_this->EditingPlayer = NewEditingPlayer;
					_this->OnRep_EditingPlayer();
					return;
				}
		}
		else
		{
			if (!NewEditingPlayer)
			{
				_this->EditingPlayer = NewEditingPlayer;
				_this->OnRep_EditingPlayer();
				return;
			}

			auto Handle = NewEditingPlayer->Owner;

			if (auto PlayerController = Handle->Cast<AFortPlayerControllerAthena>())
			{
				_this->EditingPlayer = NewEditingPlayer;
				_this->OnRep_EditingPlayer();
			}
		}
	}
}

void AFortPlayerControllerAthena::ServerBeginEditingBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	Stack.StepCompiledIn(&Building);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	if (!PlayerController || !PlayerController->MyFortPawn || !Building || !PlayerController->PlayerState || (Building->bPlayerPlaced && Building->Team != ((AFortPlayerStateAthena*)PlayerController->PlayerState)->TeamIndex)) // only your own team's builds, not enemy-placed
		return;
	if (PlayerController->MyFortPawn->IsDBNO()) // knocked players can't edit
		return;

	AFortPlayerStateAthena* PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;
	if (!PlayerState)
		return;
	
	SetEditingPlayer(Building, PlayerState);
	
	if (!PlayerController->MyFortPawn->CurrentWeapon->IsA<AFortWeap_EditingTool>())
	{
		auto EditToolEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{
				return entry.ItemDefinition->Class == UFortEditToolItemDefinition::StaticClass();
			}, FFortItemEntry::Size());

		PlayerController->MyFortPawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)EditToolEntry->ItemDefinition, EditToolEntry->ItemGuid, EditToolEntry->HasTrackerGuid() ? EditToolEntry->TrackerGuid : FGuid(), false);
	}

	if (auto EditTool = PlayerController->MyFortPawn->CurrentWeapon->Cast<AFortWeap_EditingTool>())
	{
		EditTool->EditActor = Building;
		EditTool->ForceNetUpdate();
		EditTool->OnRep_EditActor();
	}
}


uint64_t ReplaceBuildingActor_ = 0;
uint64_t InitializeBuildingActor_ = 0;
uint64_t PostInitializeSpawnedBuildingActor_ = 0;
void AFortPlayerControllerAthena::ServerEditBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	TSubclassOf<AActor> NewClass;
	uint8 RotationIterations;
	bool bMirrored;
	Stack.StepCompiledIn(&Building);
	Stack.StepCompiledIn(&NewClass);
	Stack.StepCompiledIn(&RotationIterations);
	Stack.StepCompiledIn(&bMirrored);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	if (!PlayerController || !Building || !NewClass || !Building->IsA<ABuildingSMActor>() || !CanBePlacedByPlayer(NewClass) || Building->EditingPlayer != PlayerController->PlayerState || Building->bDestroyed || (PlayerController->MyFortPawn && PlayerController->MyFortPawn->IsDBNO()))
	{
		return;
	}

	SetEditingPlayer(Building, nullptr);

	auto ReplaceBuildingActor = (ABuildingSMActor * (*&)(ABuildingSMActor*, unsigned int, TSubclassOf<AActor>, unsigned int, int, bool, AFortPlayerControllerAthena*)) ReplaceBuildingActor_;
	auto ReplaceBuildingActor__New = (ABuildingSMActor * (*&)(ABuildingSMActor*, unsigned int, TSubclassOf<AActor>&, unsigned int, int, bool, AFortPlayerControllerAthena*)) ReplaceBuildingActor_;

	ABuildingSMActor* NewBuild;
	
	if (VersionInfo.FortniteVersion < 27)
		NewBuild = ReplaceBuildingActor(Building, 1, NewClass, Building->CurrentBuildingLevel, RotationIterations, bMirrored, PlayerController);
	else
		NewBuild = ReplaceBuildingActor__New(Building, 1, NewClass, Building->CurrentBuildingLevel, RotationIterations, bMirrored, PlayerController);

	/*else
	{
		// reimpl of replacebuildingactor
		NewBuild = ABuildingSMActor::K2_SpawnBuildingActor(PlayerController, NewClass, Building->GetTransform(), nullptr, nullptr, true, false);

		NewBuild->CurrentBuildingLevel = Building->CurrentBuildingLevel;
		NewBuild->SetMirrored(bMirrored);
		NewBuild->SetTeam(Building->TeamIndex);
		auto InitializeBuildingActor = (void(*)(ABuildingSMActor*,
			uint8 Reason,
			int16 InOwnerPersistentID,
			ABuildingSMActor * BuildingOwner,
			const ABuildingSMActor * ReplacedBuilding,
			bool bForcePlayBuildUpAnim)) InitializeBuildingActor_;
		InitializeBuildingActor(NewBuild, 2, ((AFortPlayerStateAthena*)PlayerController->PlayerState)->WorldPlayerId, nullptr, Building, true);
		NewBuild->BuildingReplacementType = 1;

		Building->ReplacementDestructionReason = 1;
		Building->OnReplacementDestruction.Process(1, NewBuild);
		Building->bAutoReleaseCurieContainerOnDestroyed = false;

		auto PostInitializeSpawnedBuildingActor = (void(*)(ABuildingSMActor*, uint8_t Reason)) PostInitializeSpawnedBuildingActor_;
		PostInitializeSpawnedBuildingActor(NewBuild, 2);
		UWorld::FinishSpawnActor(NewBuild, Building->K2_GetActorLocation(), Building->K2_GetActorRotation());
		Building->SilentDie(true);
	}*/
	
	if (NewBuild)
	{
		NewBuild->bPlayerPlaced = true;
	}
}

void AFortPlayerControllerAthena::ServerEndEditingBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	Stack.StepCompiledIn(&Building);
	Stack.IncrementCode();

	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	if (!PlayerController || !PlayerController->MyFortPawn || !Building || Building->EditingPlayer != PlayerController->PlayerState/* || Building->Team != static_cast<AFortPlayerStateAthena*>(PlayerController->PlayerState)->TeamIndex*/ || Building->bDestroyed)
		return;

	SetEditingPlayer(Building, nullptr);

	/*if (VersionInfo.EngineVersion >= 4.24)
	{
		auto EditToolEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{
				return entry.ItemDefinition->Class == UFortEditToolItemDefinition::StaticClass();
			}, FFortItemEntry::Size());

		PlayerController->MyFortPawn->EquipWeaponDefinition((UFortWeaponItemDefinition*)EditToolEntry->ItemDefinition, EditToolEntry->ItemGuid, EditToolEntry->HasTrackerGuid() ? EditToolEntry->TrackerGuid : FGuid(), false);
	}*/

	auto EditToolEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{
			return entry.ItemDefinition->Class == UFortEditToolItemDefinition::StaticClass();
		}, FFortItemEntry::Size());

	if (!EditToolEntry)
		return;

	auto EditToolPtr = PlayerController->Pawn->CurrentWeaponList.Search([&](AActor* Weapon__Uncasted)
		{ return ((AFortWeapon*)Weapon__Uncasted)->ItemEntryGuid == EditToolEntry->ItemGuid; });

	if (!EditToolPtr)
		return;

	if (auto EditTool = *(AFortWeap_EditingTool**)EditToolPtr)
	{
		EditTool->EditActor = nullptr;
		EditTool->ForceNetUpdate();
		EditTool->OnRep_EditActor();
	}
}


void AFortPlayerControllerAthena::ServerRepairBuildingActor(UObject* Context, FFrame& Stack)
{
	ABuildingSMActor* Building;
	Stack.StepCompiledIn(&Building);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	if (!PlayerController)
		return;

	auto Price = (int32)std::floor((10.f * (1.f - Building->GetHealthPercent())) * 0.75f);
	auto res = UFortKismetLibrary::K2_GetResourceItemDefinition(Building->ResourceType);
	auto ItemP = PlayerController->WorldInventory->Inventory.ItemInstances.Search([res](UFortWorldItem* entry)
		{
			return entry->ItemEntry.ItemDefinition == res;
		});
	auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([res](FFortItemEntry& entry)
		{
			return entry.ItemDefinition == res;
		}, FFortItemEntry::Size());
	if (!ItemP)
		return;
	auto Item = *ItemP;
	if ((itemEntry->Count - Price) < 0)
		return;

	itemEntry->Count -= Price;
	if (itemEntry->Count <= 0)
		PlayerController->WorldInventory->Remove(itemEntry->ItemGuid);
	else
	{
		Item->ItemEntry.Count = itemEntry->Count;
		PlayerController->WorldInventory->UpdateEntry(*itemEntry);
		Item->ItemEntry.bIsDirty = true;
	}

	Building->RepairBuilding(PlayerController, Price);
}

void AFortPlayerControllerAthena::ServerAttemptInventoryDrop(UObject* Context, FFrame& Stack)
{
	FGuid Guid;
	int32 Count;
	bool bTrash = false; // this only exists on some newer builds
	Stack.StepCompiledIn(&Guid);
	Stack.StepCompiledIn(&Count);
	Stack.StepCompiledIn(&bTrash);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	if (!PlayerController || !PlayerController->Pawn)
		return;

	auto ItemP = PlayerController->WorldInventory->Inventory.ItemInstances.Search([&](UFortWorldItem* entry)
		{ return entry->ItemEntry.ItemGuid == Guid; });
	auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{ return entry.ItemGuid == Guid; }, FFortItemEntry::Size());
	if (!ItemP)
		return;
	auto Item = *ItemP;

	itemEntry->Count -= Count;
	if (itemEntry->ItemDefinition && itemEntry->ItemDefinition->Name.ToString().find("PurpleStuff") != std::string::npos)
		SyndicateLeanDropped[PlayerController] = GetTickCount64(); // a drop, not a drink (SyndicateLeanTick)

	FVector FinalLoc = PlayerController->Pawn->K2_GetActorLocation();

	static auto WID_Launcher_Petrol = FindObject<UFortWorldItemDefinition>(L"/Game/Athena/Items/Weapons/Prototype/WID_Launcher_Petrol.WID_Launcher_Petrol");

	if (itemEntry->ItemDefinition == WID_Launcher_Petrol)
	{
		static auto BGA_Petrol_PickupClass = FindObject<UClass>(L"/Game/Athena/Items/Weapons/Prototype/PetrolPump/BGA_Petrol_Pickup.BGA_Petrol_Pickup_C");

		AActor* PetrolPickup = UWorld::SpawnActor<AActor>(BGA_Petrol_PickupClass, FinalLoc);

		PlayerController->WorldInventory->Remove(Guid);
		PlayerController->WorldInventory->Update(itemEntry);

		return;
	}

	FVector ForwardVector = PlayerController->Pawn->GetActorForwardVector();
	//ForwardVector.Z = 0.0f;
	//ForwardVector.Normalize();

	// Short toss at waist height: 450 units ahead and above the head put items inside hills and walls.
	FinalLoc = FinalLoc + ForwardVector * 150.f;

	const float RandomAngleVariation = ((float)rand() * 0.00109866634f) - 18.f;
	const float FinalAngle = (RandomAngleVariation + 360.f / ((float)rand() * 0.00015259254737998596f)) * 0.017453292519943295f;

	FinalLoc.X += cos(FinalAngle) * 40.f;
	FinalLoc.Y += sin(FinalAngle) * 40.f;

	// Glider / mobility item uses: where the remaining charges actually live, so the refill-on-pickup bug can be
	// fixed at the right field. Logged once per item name when a glider is dropped.
	if (itemEntry->ItemDefinition)
	{
		auto NameStr = itemEntry->ItemDefinition->Name.ToString();
		if (NameStr.find("Glider") != std::string::npos || NameStr.find("Mobility") != std::string::npos)
		{
			static std::set<std::string> Seen;
			if (Seen.insert(std::string(NameStr.c_str())).second)
			{
				printf("[GliderDrop] %s Count=%d LoadedAmmo=%d Durability=%.1f StateValues=%d\n",
					NameStr.c_str(), itemEntry->Count, itemEntry->LoadedAmmo, itemEntry->Durability, itemEntry->StateValues.Num());
				for (int i = 0; i < itemEntry->StateValues.Num() && i < 8; i++)
				{
					auto& SV = itemEntry->StateValues.Get(i, FFortItemEntryStateValue::Size());
					printf("[GliderDrop]   state[%d] type=%d int=%d name=%s\n", i, (int)SV.StateType, SV.IntValue, SV.NameValue.ToString().c_str());
				}
			}
		}
	}
	auto Dropped = AFortInventory::SpawnPickup(PlayerController->Pawn->K2_GetActorLocation() + PlayerController->Pawn->GetActorForwardVector() * 70.f + FVector(0, 0, 50), *itemEntry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), PlayerController->MyFortPawn, Count, true, true, true, nullptr, FinalLoc);
	printf("[SyndicateDrop] %s x%d -> %s\n", itemEntry->ItemDefinition ? itemEntry->ItemDefinition->Name.ToString().c_str() : "?", Count, Dropped ? "pickup spawned" : "NO PICKUP");
	if (itemEntry->Count <= 0 || Count < 0)
		PlayerController->WorldInventory->Remove(Guid);
	else
	{
		Item->ItemEntry.Count = itemEntry->Count;
		PlayerController->WorldInventory->UpdateEntry(*itemEntry);
		Item->ItemEntry.bIsDirty = true;
	}
}

class UAthenaToyItemDefinition : public UObject
{
public:
	UCLASS_COMMON_MEMBERS(UAthenaToyItemDefinition);

	DEFINE_PROP(ToySpawnAbility, TSoftClassPtr<UClass>);
};

extern uint64_t ConstructAbilitySpec;
uint64_t GiveAbilityAndActivateOnce;
void AFortPlayerControllerAthena::ServerPlayEmoteItem(UObject* Context, FFrame& Stack)
{
	UObject* Asset;
	float RandomNumber = 0.f;
	Stack.StepCompiledIn(&Asset);
	Stack.StepCompiledIn(&RandomNumber);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	if (!PlayerController || !PlayerController->PlayerState || !PlayerController->MyFortPawn || !Asset)
		return;

	auto AbilitySystemComponent = ((AFortPlayerStateAthena*)PlayerController->PlayerState)->AbilitySystemComponent;

	if (auto CharacterVehicle = PlayerController->Pawn ? PlayerController->Pawn->Cast<AFortCharacterVehicle>() : nullptr)
		AbilitySystemComponent = CharacterVehicle->OverrideAbilitySystemComponent;
	if (!AbilitySystemComponent || !GiveAbilityAndActivateOnce) return;
	UObject* AbilityToUse = nullptr;

	static auto SprayClass = FindClass("AthenaSprayItemDefinition");
	if (Asset->IsA(SprayClass))
	{
		static auto SprayAbilityClass = FindObject<UClass>(L"/Game/Abilities/Sprays/GAB_Spray_Generic.GAB_Spray_Generic_C");
		AbilityToUse = SprayAbilityClass ? SprayAbilityClass->GetDefaultObj() : nullptr;

		if(!(PlayerController->PlayerState->HasbIsABot() && PlayerController->PlayerState->bIsABot))
            if(auto Quests=PlayerController->GetQuestManager(1))Quests->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetSpray(), 1, true, nullptr);
	}
	else if (auto ToyAsset = Asset->Cast<UAthenaToyItemDefinition>())
	{
		AbilityToUse = ToyAsset->ToySpawnAbility ? ToyAsset->ToySpawnAbility->GetDefaultObj() : nullptr;

		if(!(PlayerController->PlayerState->HasbIsABot() && PlayerController->PlayerState->bIsABot))
            if(auto Quests=PlayerController->GetQuestManager(1))Quests->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetToy(), 1, true, nullptr);
	}
	else if (auto DanceAsset = Asset->Cast<UAthenaDanceItemDefinition>())
	{
		static auto HasbMovingEmote = PlayerController->MyFortPawn->HasbMovingEmote();
		if (HasbMovingEmote)
			PlayerController->MyFortPawn->bMovingEmote = DanceAsset->bMovingEmote;

		static auto HasWalkForwardSpeed = PlayerController->MyFortPawn->HasEmoteWalkSpeed();
		if (HasWalkForwardSpeed)
			PlayerController->MyFortPawn->EmoteWalkSpeed = DanceAsset->WalkForwardSpeed;

		static auto HasbMovingEmoteForwardOnly = PlayerController->MyFortPawn->HasbMovingEmoteForwardOnly();
		if (HasbMovingEmoteForwardOnly)
			PlayerController->MyFortPawn->bMovingEmoteForwardOnly = DanceAsset->bMoveForwardOnly;

		static auto HasbMovingEmoteFollowingOnly = PlayerController->MyFortPawn->HasbMovingEmoteFollowingOnly();
		if (HasbMovingEmoteFollowingOnly)
			PlayerController->MyFortPawn->bMovingEmoteFollowingOnly = DanceAsset->bMoveFollowingOnly;

		auto CustomAbility = DanceAsset->HasCustomDanceAbility() ? DanceAsset->CustomDanceAbility.Get() : nullptr;

		if (CustomAbility)
			AbilityToUse = CustomAbility->GetDefaultObj();
		else
		{
			static auto EmoteAbilityClass = FindObject<UClass>(L"/Game/Abilities/Emotes/GAB_Emote_Generic.GAB_Emote_Generic_C");
			AbilityToUse = EmoteAbilityClass ? EmoteAbilityClass->GetDefaultObj() : nullptr;
		}

		if(!(PlayerController->PlayerState->HasbIsABot() && PlayerController->PlayerState->bIsABot))
            if(auto Quests=PlayerController->GetQuestManager(1))Quests->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetEmote(), 1, true, nullptr);
	}

	if (AbilityToUse)
	{
		auto Spec = (FGameplayAbilitySpec*)malloc(FGameplayAbilitySpec::Size());
		memset(PBYTE(Spec), 0, FGameplayAbilitySpec::Size());

		if (ConstructAbilitySpec)
			((void (*)(FGameplayAbilitySpec*, const UObject*, int, int, UObject*)) ConstructAbilitySpec)(Spec, AbilityToUse, 1, -1, Asset);
		else
		{
			Spec->MostRecentArrayReplicationKey = -1;
			Spec->ReplicationID = -1;
			Spec->ReplicationKey = -1;
			Spec->Ability = (UFortGameplayAbility*)AbilityToUse;
			Spec->Level = 1;
			Spec->InputID = -1;
			Spec->Handle.Handle = rand();
			Spec->SourceObject = Asset;
		}
		FGameplayAbilitySpecHandle handle;
		((void (*)(UAbilitySystemComponent*, FGameplayAbilitySpecHandle*, FGameplayAbilitySpec*, void*)) GiveAbilityAndActivateOnce)(AbilitySystemComponent, &handle, Spec, nullptr);

		free(Spec);

		if (PlayerController->MyFortPawn->HasLastReplicatedEmoteExecuted())
		{
			auto OldEmote = PlayerController->MyFortPawn->LastReplicatedEmoteExecuted;
			PlayerController->MyFortPawn->LastReplicatedEmoteExecuted = Asset;
			PlayerController->MyFortPawn->OnRep_LastReplicatedEmoteExecuted(OldEmote);
		}
	}
}

uint8 ToDeathCause(AFortPlayerPawnAthena* Pawn, FGameplayTagContainer& DeathTags, bool bDBNO)
{
	static auto ToDeathCause = AFortPlayerStateAthena::GetDefaultObj()->GetFunction("ToDeathCause");
	if (ToDeathCause)
	{
		if (!AFortPlayerStateAthena::ToDeathCause__Ptr)
			AFortPlayerStateAthena::ToDeathCause__Ptr = ToDeathCause;

		return AFortPlayerStateAthena::ToDeathCause(DeathTags, bDBNO);
	}
	else if (VersionInfo.EngineVersion >= 4.19)
	{
		static uint64_t ToDeathCauseNative = 0;

		if (!ToDeathCauseNative)
		{
			if (VersionInfo.EngineVersion == 4.19)
				ToDeathCauseNative = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 20 41 0F B6 F8 48 8B DA 48 8B F1 E8 ? ? ? ? 33 ED").Get();
			else if (VersionInfo.EngineVersion == 4.20)
				ToDeathCauseNative = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 0F B6 FA 48 8B D9 E8 ? ? ? ? 33 F6 48 89 74 24").Get();
			else if (VersionInfo.EngineVersion == 4.21)
				ToDeathCauseNative = Memcury::Scanner::FindPattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 20 0F B6 FA 48 8B D9 E8 ? ? ? ? 33").Get();
		}


		if (ToDeathCauseNative)
		{
			if (VersionInfo.EngineVersion == 4.19)
			{
				static uint8(*ToDeathCause_)(AFortPlayerPawnAthena * Pawn, FGameplayTagContainer TagContainer, char bDBNO) = decltype(ToDeathCause_)(ToDeathCauseNative);
				return ToDeathCause_(Pawn, DeathTags, bDBNO);
			}
			else
			{
				static uint8(*ToDeathCause_)(FGameplayTagContainer TagContainer, char bDBNO) = decltype(ToDeathCause_)(ToDeathCauseNative);
				return ToDeathCause_(DeathTags, bDBNO);
			}
		}
	}

	return 0;
}

uint64 RemoveFromAlivePlayers_ = 0;
extern bool RemoveSyndicateHumanFromBotMatch(AFortPlayerControllerAthena*);
extern int SyndicateBotShotDeathCause(AFortPlayerControllerAthena*);
extern void SyndicatePublishHumanKnock(AFortPlayerControllerAthena*, FFortPlayerDeathReport&, int);
extern bool IsSyndicateFinalElimination(AFortPlayerPawnAthena*);
extern bool HandleSyndicateBotDeathReport(AFortPlayerControllerAthena*, FFortPlayerDeathReport&);
// Siphon: +SiphonAmount on an elimination, health first (up to 100), the rest to shield (up to 100).
// Before: at full health the shield was doubled, and at 51-99 health exactly 50 shield was added.
void SyndicateApplySiphon(AFortPlayerStateAthena* KillerPlayerState, AFortPlayerPawnAthena* KillerPawn)
{
	// Siphon is an Arena rule. Normal Solo/Duo/Squad matches keep their normal
	// health/shield economy; only ShowdownAlt (Arena) playlists grant it.
	if (!wcsstr(FConfiguration::Playlist, L"ShowdownAlt")) return;
	if (FConfiguration::SiphonAmount <= 0 || !KillerPlayerState || !KillerPlayerState->AbilitySystemComponent || !KillerPawn) return;
	auto Handle = KillerPlayerState->AbilitySystemComponent->MakeEffectContext();
	FGameplayTag Tag;
	static auto Cue = FName(L"GameplayCue.Shield.PotionConsumed");
	Tag.TagName = Cue;
	auto PredictionKey = (FPredictionKey*)malloc(FPredictionKey::Size());
	memset((PBYTE)PredictionKey, 0, FPredictionKey::Size());
	KillerPlayerState->AbilitySystemComponent->NetMulticast_InvokeGameplayCueAdded(Tag, *PredictionKey, Handle);
	KillerPlayerState->AbilitySystemComponent->NetMulticast_InvokeGameplayCueExecuted(Tag, *PredictionKey, Handle);
	free(PredictionKey);
	float Health = KillerPawn->GetHealth(), Shield = KillerPawn->GetShield(), Gain = (float)FConfiguration::SiphonAmount;
	float Heal = Gain < 100.f - Health ? Gain : (100.f - Health > 0.f ? 100.f - Health : 0.f);
	Health += Heal;
	Shield = Shield + Gain - Heal > 100.f ? 100.f : Shield + Gain - Heal;
	KillerPawn->SetHealth(Health);
	KillerPawn->SetShield(Shield);
}
// Bot eliminations by a human (bots die through the bot system, not ClientOnPawnDied).
void SyndicateApplySiphonFor(AFortPlayerControllerAthena* KillerPC)
{
	if (KillerPC) SyndicateApplySiphon((AFortPlayerStateAthena*)KillerPC->PlayerState, (AFortPlayerPawnAthena*)KillerPC->MyFortPawn);
}

void AFortPlayerControllerAthena::ClientOnPawnDied(AFortPlayerControllerAthena* PlayerController, FFortPlayerDeathReport& DeathReport)
{
	if (HandleSyndicateBotDeathReport(PlayerController, DeathReport)) return;
	if (!PlayerController)
		return ClientOnPawnDiedOG(PlayerController, DeathReport);
	auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
	auto GameState = GameMode ? (AFortGameStateAthena*)GameMode->GameState : nullptr;
	auto PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;
	if(!GameState || !PlayerState || !PlayerController->Pawn)return ClientOnPawnDiedOG(PlayerController,DeathReport);
	// ClientOnPawnDied is an elimination notification: forwarding it on a knock
	// takes the client out of pawn control and enters the death/spectator UI.
	// Native DBNO replication already supplies the crawling state.
	auto DownedPawn = PlayerController->MyFortPawn ? PlayerController->MyFortPawn : PlayerController->Pawn;
	const bool Finalizing = IsSyndicateFinalElimination(DownedPawn);
	if (DownedPawn && DownedPawn->IsDBNO() && !Finalizing)
	{
		// Still put the knock in the kill feed ("X knocked Y"): the client shows it from DeathInfo with bDBNO set.
		if (VersionInfo.FortniteVersion > 1.8 || VersionInfo.EngineVersion >= 4.19)
			SyndicatePublishHumanKnock(PlayerController, DeathReport, ToDeathCause(DownedPawn, DeathReport.Tags, true));
		return;
	}


	if (PlayerController->WorldInventory && PlayerController->Pawn && ((PlayerController->Pawn->HasbShouldDropItemsOnDeath() ? PlayerController->Pawn->bShouldDropItemsOnDeath : true) && !FConfiguration::bKeepInventory))
	{
		bool bHasMats = false;
		for (int i = 0; i < PlayerController->WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
		{
			auto& entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Get(i, FFortItemEntry::Size());

			if (entry.ItemDefinition->CanBeDropped())
				AFortInventory::SpawnPickup(PlayerController->Pawn->K2_GetActorLocation(), entry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetPlayerElimination(), PlayerController->MyFortPawn);
		}
	}

	auto KillerPlayerState = (AFortPlayerStateAthena*)DeathReport.KillerPlayerState;
	auto KillerPawn = (AFortPlayerPawnAthena*)DeathReport.KillerPawn;
	auto KillerPlayerController = KillerPlayerState && KillerPlayerState->Owner ? KillerPlayerState->Owner->Cast<AFortPlayerControllerAthena>() : nullptr;

	if (VersionInfo.FortniteVersion > 1.8 || VersionInfo.EngineVersion >= 4.19)
	{
		if (PlayerState->HasPawnDeathLocation())
			PlayerState->PawnDeathLocation = PlayerController->Pawn ? PlayerController->Pawn->K2_GetActorLocation() : FVector();

		if (PlayerState->HasDeathInfo())
		{
			memset(&PlayerState->DeathInfo, 0, FDeathInfo::Size());
			PlayerState->DeathInfo.bDBNO = !Finalizing && PlayerController->Pawn && PlayerController->Pawn->IsDBNO();
			if (FDeathInfo::HasKiller())
				PlayerState->DeathInfo.Killer = KillerPlayerState;
			if (FDeathInfo::HasDeathLocation())
				PlayerState->DeathInfo.DeathLocation = PlayerState->HasPawnDeathLocation() ? PlayerState->PawnDeathLocation : (PlayerController->Pawn ? PlayerController->Pawn->K2_GetActorLocation() : FVector());
			if (FDeathInfo::HasDeathTags())
				// Chapter 2 (11.x): MoveSoundStimulusBroadcastInterval+0x18 lands inside the pawn's Inventory pointer, so the
				// copied "tag container" was garbage and OnRep_DeathInfo crashed the server on the first death. Use the
				// death report's own tags there.
				PlayerState->DeathInfo.DeathTags = VersionInfo.FortniteVersion >= 11 ? DeathReport.Tags : PlayerController->Pawn ? *(FGameplayTagContainer*)(__int64(&PlayerController->Pawn->MoveSoundStimulusBroadcastInterval) + 0x10) : FGameplayTagContainer();
			if (FDeathInfo::HasDeathClassSlot())
				PlayerState->DeathInfo.DeathClassSlot = -1;
			PlayerState->DeathInfo.DeathCause = ToDeathCause(PlayerController->Pawn, DeathReport.Tags, PlayerState->DeathInfo.bDBNO);
            auto BotCause=SyndicateBotShotDeathCause(PlayerController);
            if(BotCause>=0)PlayerState->DeathInfo.DeathCause=uint8(BotCause);
			//PlayerState->DeathInfo.Downer = KillerPlayerState;
			if (FDeathInfo::HasFinisherOrDowner())
				PlayerState->DeathInfo.FinisherOrDowner = KillerPlayerState ? KillerPlayerState : PlayerState;
			// A pawn-less death (a bot taken off the bus, a player removed from the match) has no tags to copy.
			if (FDeathInfo::HasFinisherOrDownerTags() && (KillerPawn || PlayerController->Pawn))
				PlayerState->DeathInfo.FinisherOrDownerTags = KillerPawn ? KillerPawn->GameplayTags : PlayerController->Pawn->GameplayTags;
			if (FDeathInfo::HasVictimTags() && PlayerController->Pawn)
				PlayerState->DeathInfo.VictimTags = PlayerController->Pawn->GameplayTags;
			if (FDeathInfo::HasDistance())
				PlayerState->DeathInfo.Distance = PlayerController->Pawn ? (PlayerState->DeathInfo.DeathCause != /*EDeathCause::FallDamage*/ 1 ? (KillerPawn ? KillerPawn->GetDistanceTo(PlayerController->Pawn) : 0) : (PlayerController->MyFortPawn->HasLastFallDistance() ? PlayerController->MyFortPawn->LastFallDistance : 0)) : 0;
			if (FDeathInfo::HasbInitialized())
				PlayerState->DeathInfo.bInitialized = true;
			PlayerState->OnRep_DeathInfo();
		}

		if (KillerPlayerState && KillerPawn && KillerPawn->Controller && KillerPawn->Controller != PlayerController)
		{
			if (KillerPlayerState->HasKillScore())
				KillerPlayerState->KillScore++;
			else
				KillerPlayerState->Kills++;
			KillerPlayerState->OnRep_Kills();
			if (KillerPlayerState->HasTeamKillScore())
			{
				KillerPlayerState->TeamKillScore++;
				KillerPlayerState->OnRep_TeamKillScore();
			}

			struct Test { AFortPlayerStateAthena* ps; uint8_t p[0x8]; };

			Test t{ PlayerState };
			KillerPlayerState->ClientReportKill(t);
			if (KillerPlayerState->HasTeamKillScore())
				KillerPlayerState->ClientReportTeamKill(KillerPlayerState->TeamKillScore);

			for (auto& Damager : PlayerController->Pawn->Damagers)
			{
				if (Damager.DamageCauser && Damager.DamageCauser != KillerPlayerController && Damager.DamageCauser->IsA<AFortPlayerControllerAthena>())
				{
					FGameplayTagContainer TargetTags{};
					auto DamagerController = (AFortPlayerControllerAthena*)Damager.DamageCauser;

					auto Interface = (IGameplayTagAssetInterface*)PlayerController->Pawn->GetInterface(IGameplayTagAssetInterface::StaticClass());
					if (Interface)
					{
						auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2];
						GetOwnedGameplayTags(Interface, &TargetTags);
						//Interface->GetOwnedGameplayTags(&TargetTags);
					}

					if (!(DamagerController->PlayerState->HasbIsABot() && DamagerController->PlayerState->bIsABot))
						if (auto Quests = DamagerController->GetQuestManager(1)) Quests->SendStatEvent(DamagerController, EFortQuestObjectiveStatEvent::GetKillContribution(), 1, false, PlayerController->Pawn, TargetTags);

					TargetTags.GameplayTags.Free();
					TargetTags.ParentTags.Free();
				}
			}

			FGameplayTagContainer TargetTags{};

			auto Interface = (IGameplayTagAssetInterface*)PlayerController->Pawn->GetInterface(IGameplayTagAssetInterface::StaticClass());
			if (Interface)
			{
				auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2];
				GetOwnedGameplayTags(Interface, &TargetTags);
				//Interface->GetOwnedGameplayTags(&TargetTags);
			}

			if (KillerPlayerController && !(KillerPlayerState->HasbIsABot() && KillerPlayerState->bIsABot))
				if (auto Quests = KillerPlayerController->GetQuestManager(1)) Quests->SendStatEvent(KillerPlayerController, EFortQuestObjectiveStatEvent::GetKill(), 1, false, PlayerController->Pawn, TargetTags);

			TargetTags.GameplayTags.Free();
			TargetTags.ParentTags.Free();
		}

		static auto IsRespawningAllowedFunc = GameState->GetFunction("IsRespawningAllowed");

		bool bRespawnAllowed = false;

		if (!IsRespawningAllowedFunc)
		{
			auto Playlist = VersionInfo.FortniteVersion >= 3.5 && GameMode->HasWarmupRequiredPlayerCount() ? (GameMode->GameState->HasCurrentPlaylistInfo() ? GameMode->GameState->CurrentPlaylistInfo.BasePlaylist : GameMode->GameState->CurrentPlaylistData) : nullptr;

			// respawn except storm needs to be fixed
			bRespawnAllowed = Playlist ? Playlist->RespawnType > 0 : false;
		}
		else
			bRespawnAllowed = GameState->Call<bool>(IsRespawningAllowedFunc, PlayerState);

		if (!bRespawnAllowed && (Finalizing || !PlayerController->Pawn || !PlayerController->Pawn->IsDBNO()) && PlayerState->HasPlace())
		{
			PlayerState->Place = GameState->PlayersLeft;
			PlayerState->OnRep_Place();

			AFortWeapon* DamageCauser = nullptr;
			static auto ProjectileBaseClass = FindClass("FortProjectileBase");
			if (DeathReport.DamageCauser ? DeathReport.DamageCauser->IsA(ProjectileBaseClass) : false)
			{
				auto Owner = DeathReport.DamageCauser->Owner;

				if (Owner && Owner->Cast<AFortWeapon>())
					DamageCauser = (AFortWeapon*)Owner;
				else if (auto Controller = Owner ? Owner->Cast<AFortPlayerControllerAthena>() : nullptr)
					DamageCauser = Controller->Pawn ? (AFortWeapon*)Controller->Pawn->CurrentWeapon : nullptr;
				else if (auto Pawn = Owner ? Owner->Cast<AFortPlayerPawnAthena>() : nullptr)
					DamageCauser = (AFortWeapon*)Pawn->CurrentWeapon;
			}
			else if (auto Weapon = DeathReport.DamageCauser ? DeathReport.DamageCauser->Cast<AFortWeapon>() : nullptr)
				DamageCauser = Weapon;
			if (!RemoveSyndicateHumanFromBotMatch(PlayerController) && RemoveFromAlivePlayers_)
			{
				((void (*)(AFortGameMode*, AFortPlayerControllerAthena*, AFortPlayerStateAthena*, AFortPlayerPawnAthena*, UFortItemDefinition*, uint8, char))RemoveFromAlivePlayers_)(GameMode, PlayerController, KillerPlayerState == PlayerState ? nullptr : KillerPlayerState, KillerPawn, DamageCauser && DamageCauser->IsA<AFortWeapon>() ? DamageCauser->WeaponData : nullptr, PlayerState->HasDeathInfo() ? PlayerState->DeathInfo.DeathCause : 0, 0);
			}

			if (VersionInfo.FortniteVersion >= 15)
			{
				//static auto SpectatingName = FName(L"Spectating");
				//PlayerController->StateName = SpectatingName;
				//PlayerController->ClientGotoState(SpectatingName);
				PlayerController->Pawn->CharacterMovement->ProcessEvent(PlayerController->Pawn->CharacterMovement->GetFunction("DisableMovement"), nullptr);
			}

			if (PlayerController->Pawn && KillerPlayerState && KillerPlayerState != PlayerState && KillerPlayerState->Place == 1)
			{
				/*if (PlayerState->Place == 1)
				{
					KillerPlayerState = PlayerState;
					KillerPawn = (AFortPlayerPawnAthena*)PlayerController->Pawn;
				}*/

				auto KillerWeapon = DamageCauser ? DamageCauser->WeaponData : nullptr;


				if (VersionInfo.FortniteVersion >= 16)
				{
					KillerPlayerController->PlayWinEffects(KillerPawn, KillerWeapon, PlayerState->DeathInfo.DeathCause, false);
					KillerPlayerController->ClientNotifyWon(KillerPawn, KillerWeapon, PlayerState->DeathInfo.DeathCause);
					KillerPlayerController->ClientNotifyTeamWon(KillerPawn, KillerWeapon, PlayerState->DeathInfo.DeathCause);
				}

				if (KillerPlayerState != PlayerState && VersionInfo.FortniteVersion >= 19)
				{
					auto Crown = FindObject<UFortItemDefinition>(L"/VictoryCrownsGameplay/Items/AGID_VictoryCrown.AGID_VictoryCrown");

					TArray<FFortItemEntryStateValue> StateValues{};
					auto Value = (FFortItemEntryStateValue*)malloc(FFortItemEntryStateValue::Size());
					memset((PBYTE)Value, 0, FFortItemEntryStateValue::Size());

					Value->IntValue = 1;
					Value->StateType = 2;
					StateValues.Add(*Value, FFortItemEntryStateValue::Size());

					free(Value);
					KillerPlayerController->WorldInventory->GiveItem(Crown, 1, 0, 0, true, true, 0, StateValues);
					StateValues.Free();
				}

				GameState->WinningTeam = KillerPlayerState->TeamIndex;
				GameState->OnRep_WinningTeam();
				if (GameState->HasWinningPlayerState())
				{
					GameState->WinningPlayerState = KillerPlayerState;
					GameState->OnRep_WinningPlayerState();
				}
			}

		}

		if (PlayerController->Pawn && KillerPawn && KillerPawn->Controller != PlayerController)
			SyndicateApplySiphon(KillerPlayerState, KillerPawn);
	}

	if (VersionInfo.FortniteVersion < 6)
	{
		if (GameState->GamePhase > 2)
		{
			if (GameMode->bAllowSpectateAfterDeath)
			{
				// Teammate first (what the game shows in duos/squads); a different server-side target
				// fed the wrong player's aim to the spectator camera, which made it shake.
				AFortPlayerPawnAthena* Teammate = nullptr;
				for (int I = 0; I < GameMode->AlivePlayers.Num() && !Teammate; ++I)
				{
					auto Other = (AFortPlayerControllerAthena*)GameMode->AlivePlayers[I];
					auto OtherState = Other ? (AFortPlayerStateAthena*)Other->PlayerState : nullptr;
					if (Other && Other != PlayerController && OtherState && OtherState->TeamIndex == PlayerState->TeamIndex && Other->Pawn && !Other->Pawn->bActorIsBeingDestroyed)
						Teammate = (AFortPlayerPawnAthena*)Other->Pawn;
				}
				PlayerController->PlayerToSpectateOnDeath = Teammate ? Teammate : KillerPawn ? KillerPawn : (GameMode->AlivePlayers.Num() > 0 ? ((AFortPlayerControllerAthena*)GameMode->AlivePlayers[rand() % GameMode->AlivePlayers.Num()])->Pawn : nullptr);
				
				UKismetSystemLibrary::K2_SetTimer(PlayerController, FString(L"SpectateOnDeath"), 5.f, false);
			}
		}
	}

	return ClientOnPawnDiedOG(PlayerController, DeathReport);
}

void AFortPlayerControllerAthena::ServerClientIsReadyToRespawn(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();

	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
	auto GameState = (AFortGameStateAthena*)GameMode->GameState;
	auto PlayerState = (AFortPlayerStateAthena*)PlayerController->PlayerState;

	if (PlayerState->RespawnData.bRespawnDataAvailable && PlayerState->RespawnData.bServerIsReady)
	{
		auto RespawnData = PlayerState->RespawnData;
		FTransform SpawnTransform{};

		FQuat Rotation = PlayerState->RespawnData.RespawnRotation;
		SpawnTransform.Translation = PlayerState->RespawnData.RespawnLocation;
		SpawnTransform.Rotation = Rotation;

		auto Scale = FVector(1, 1, 1);
		SpawnTransform.Scale3D = Scale;

		auto NewPawn = GameMode->SpawnDefaultPawnAtTransform(PlayerController, SpawnTransform);
		PlayerController->Possess(NewPawn);
		PlayerController->RespawnPlayerAfterDeath(true);

		NewPawn->SetHealth(100.f);
		NewPawn->SetShield(0.f);

		auto Interface = PlayerController->PlayerState->GetInterface(IFortAbilitySystemInterface::StaticClass());
		if (InitializePlayerGameplayAbilities_ && Interface)
		{
			auto InitializePlayerGameplayAbilities = (void(*&)(const IInterface*))InitializePlayerGameplayAbilities_;

			InitializePlayerGameplayAbilities(Interface);
		}
		else
			for (auto& AbilitySet : AFortGameMode::AbilitySets)
				PlayerController->PlayerState->AbilitySystemComponent->GiveAbilitySet(AbilitySet);
	}

	PlayerState->RespawnData.bClientIsReady = true;
}

struct FCustomCharacterParts
{
public:
	USCRIPTSTRUCT_COMMON_MEMBERS(FCustomCharacterParts);
};

void AFortPlayerControllerAthena::InternalPickup(FFortItemEntry* PickupEntry)
{
	if (!PickupEntry || !PickupEntry->ItemDefinition)
		return;

	auto MaxStack = (int32)PickupEntry->ItemDefinition->GetMaxStackSize();
	int ItemCount = 0;


	if (!PickupEntry->ItemDefinition->HasbForceIntoOverflow() || !PickupEntry->ItemDefinition->bForceIntoOverflow)
		for (int i = 0; i < WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
		{
			auto& Item = WorldInventory->Inventory.ReplicatedEntries.Get(i, FFortItemEntry::Size());

			if (AFortInventory::IsPrimaryQuickbar(Item.ItemDefinition) && (!Item.ItemDefinition->HasbForceIntoOverflow() || !Item.ItemDefinition->bForceIntoOverflow))
				ItemCount += Item.ItemDefinition->HasNumberOfSlotsToTake() ? Item.ItemDefinition->NumberOfSlotsToTake : 1;
		}

	//printf("br: %d\n", ItemCount);
	FVector FinalLoc = Pawn ? Pawn->K2_GetActorLocation() : FVector();

	FVector ForwardVector = Pawn ? Pawn->GetActorForwardVector() : FVector();
	ForwardVector.Z = 0.0f;
	ForwardVector.Normalize();

	FinalLoc = FinalLoc + ForwardVector * 450.f;
	FinalLoc.Z += 50.f;

	const float RandomAngleVariation = ((float)rand() * 0.00109866634f) - 18.f;
	const float FinalAngle = RandomAngleVariation * 0.017453292519943295f;

	FinalLoc.X += cos(FinalAngle) * 100.f;
	FinalLoc.Y += sin(FinalAngle) * 100.f;

	auto _SendStat = [&](int Count)
	{
		FGameplayTagContainer TargetTags{};

		auto Interface = (IGameplayTagAssetInterface*)PickupEntry->ItemDefinition->GetInterface(IGameplayTagAssetInterface::StaticClass());
		if (Interface)
		{
			auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2];
			GetOwnedGameplayTags(Interface, &TargetTags);
			//Interface->GetOwnedGameplayTags(&TargetTags);
		}

		GetQuestManager(1)->SendStatEvent(this, EFortQuestObjectiveStatEvent::GetCollect(), Count, true, (UObject*)PickupEntry->ItemDefinition, TargetTags);
		
		TargetTags.GameplayTags.Free();
		TargetTags.ParentTags.Free();
	};

	auto GiveOrSwap = [&]()
		{
			if (ItemCount >= 5 && AFortInventory::IsPrimaryQuickbar(PickupEntry->ItemDefinition))
			{
				if (!MyFortPawn || !MyFortPawn->CurrentWeapon)
					return;
				
				if (AFortInventory::IsPrimaryQuickbar(((AFortWeapon*)MyFortPawn->CurrentWeapon)->WeaponData))
				{
					auto itemEntry = WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
						{ return entry.ItemGuid == ((AFortWeapon*)MyFortPawn->CurrentWeapon)->ItemEntryGuid; }, FFortItemEntry::Size());


					AFortInventory::SpawnPickup(Pawn->K2_GetActorLocation() + Pawn->GetActorForwardVector() * 70.f + FVector(0, 0, 50), *itemEntry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), MyFortPawn, -1, true, true, true, nullptr, FinalLoc);
					WorldInventory->Remove(((AFortWeapon*)MyFortPawn->CurrentWeapon)->ItemEntryGuid);
					auto Item = WorldInventory->GiveItem(*PickupEntry, PickupEntry->Count, true);
					ServerExecuteInventoryItem(Item->ItemEntry.ItemGuid);
					if (VersionInfo.FortniteVersion < 3)
					{
						auto& QuickBar = (AFortInventory::IsPrimaryQuickbar(Item->ItemEntry.ItemDefinition) || Item->ItemEntry.ItemDefinition->ItemType == EFortItemType::GetWeaponHarvest()) ? QuickBars->PrimaryQuickBar : QuickBars->SecondaryQuickBar;
						int i = 0;
						for (i = 0; i < QuickBar.Slots.Num(); i++)
						{
							auto& Slot = QuickBar.Slots.Get(i, FQuickBarSlot::Size());

							for (auto& SlotItem : Slot.Items)
								if (SlotItem == Item->ItemEntry.ItemGuid)
								{
									QuickBars->ServerActivateSlotInternal(!(AFortInventory::IsPrimaryQuickbar(Item->ItemEntry.ItemDefinition) || Item->ItemEntry.ItemDefinition->ItemType == EFortItemType::GetWeaponHarvest()), i, 0.f, true);
									break;
								}
						}
					}
					else
						ClientEquipItem(Item->ItemEntry.ItemGuid, true);
				}
				else
				{
					AFortInventory::SpawnPickup(Pawn->K2_GetActorLocation() + Pawn->GetActorForwardVector() * 70.f + FVector(0, 0, 50), *PickupEntry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), MyFortPawn, -1, true, true, true, nullptr, FinalLoc);
					return;
				}
			}
			else
				WorldInventory->GiveItem(*PickupEntry, PickupEntry->Count, true);

			_SendStat(PickupEntry->Count);
		};

	auto GiveOrSwapStack = [&](int32 OriginalCount)
		{
			if (PickupEntry->ItemDefinition->bAllowMultipleStacks && ItemCount < 5)
			{
				WorldInventory->GiveItem(*PickupEntry, OriginalCount - MaxStack, true);
				_SendStat(PickupEntry->Count);
			}
			else
				AFortInventory::SpawnPickup(Pawn->K2_GetActorLocation() + Pawn->GetActorForwardVector() * 70.f + FVector(0, 0, 50), *PickupEntry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), MyFortPawn, OriginalCount - MaxStack, true, true, true, nullptr, FinalLoc);
		};

	if (MaxStack > 1)
	{
		auto item = WorldInventory->Inventory.ItemInstances.Search([PickupEntry, MaxStack](UFortWorldItem* entry)
			{ return entry->ItemEntry.ItemDefinition == PickupEntry->ItemDefinition && entry->ItemEntry.Count < MaxStack; });
		auto itemEntry = WorldInventory->Inventory.ReplicatedEntries.Search([PickupEntry, MaxStack](FFortItemEntry& entry)
			{ return entry.ItemDefinition == PickupEntry->ItemDefinition && entry.Count < MaxStack; }, FFortItemEntry::Size());

		if (item)
		{
			bool bFound = false;
			/*for (int i = 0; i < itemEntry->StateValues.Num(); i++)
			{
				auto& StateValue = itemEntry->StateValues.Get(i, FFortItemEntryStateValue::Size());

				if (StateValue.StateType != 2)
					continue;

				bFound = true;
				StateValue.IntValue = 0;
				break;
			}*/

			auto TheRealOriginalCount = itemEntry->Count;
			if ((itemEntry->Count += PickupEntry->Count) > MaxStack)
			{
				auto OriginalCount = itemEntry->Count;
				itemEntry->Count = MaxStack;

				GiveOrSwapStack(OriginalCount);
			}

			// full proper
			for (int i = 0; i < itemEntry->StateValues.Num(); i++)
			{
				auto& StateValue = itemEntry->StateValues.Get(i, FFortItemEntryStateValue::Size());

				if (StateValue.StateType != 2)
					continue;
				
				StateValue.IntValue = 1;
				bFound = true;
				break;
			}

			if (!bFound)
			{
				auto Value = (FFortItemEntryStateValue*)malloc(FFortItemEntryStateValue::Size());
				memset((PBYTE)Value, 0, FFortItemEntryStateValue::Size());

				Value->IntValue = 1;
				Value->StateType = 2;
				itemEntry->StateValues.Add(*Value, FFortItemEntryStateValue::Size());

				free(Value);
			}


			auto Gained = itemEntry->Count - TheRealOriginalCount;

			_SendStat(Gained);
			(*item)->ItemEntry.Count = itemEntry->Count;
			WorldInventory->UpdateEntry(*itemEntry);
		}
		else
		{
			auto itemEntry2 = WorldInventory->Inventory.ReplicatedEntries.Search([PickupEntry, MaxStack](FFortItemEntry& entry)
				{ return entry.ItemDefinition == PickupEntry->ItemDefinition && entry.Count >= MaxStack; }, FFortItemEntry::Size());

			if (!itemEntry && itemEntry2 && !PickupEntry->ItemDefinition->bAllowMultipleStacks)
			{
				AFortInventory::SpawnPickup(Pawn->K2_GetActorLocation() + Pawn->GetActorForwardVector() * 70.f + FVector(0, 0, 50), *PickupEntry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), MyFortPawn, PickupEntry->Count, true, true, true, nullptr, FinalLoc);
				return;
			}

			if (PickupEntry->Count > MaxStack)
			{
				auto OriginalCount = PickupEntry->Count;
				PickupEntry->Count = MaxStack;

				GiveOrSwapStack(OriginalCount);
			}

			GiveOrSwap();
		}
	}
	else
		GiveOrSwap();
}

std::unordered_map<std::string, std::vector<FVector>> Waypoints;

extern uint64_t ApplyCharacterCustomization;
extern uint64_t NotifyGameMemberAdded_;

int32 PlayerBotID = 0;
void AFortPlayerControllerAthena::ServerCheat(UObject* Context, FFrame& Stack)
{
	FString Msg;
	Stack.StepCompiledIn(&Msg);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;

	auto fullCommand = Msg.ToString();

	std::vector<UEAllocatedString> args;

	size_t pos = 0, lastPos = 0;
	while ((pos = fullCommand.find(' ', lastPos)) != std::string::npos)
	{
		args.push_back(fullCommand.substr(lastPos, pos - lastPos));

		lastPos = pos + 1;
	}

	args.push_back(fullCommand.substr(lastPos));

	if (args.size() == 0)
	{
	_help:
		PlayerController->ClientMessage(FString(LR"(Command List:
    cheat startaircraft - Starts the battle bus
    cheat resumesafezone - Resumes the storm
    cheat pausesafezone - Pauses the storm
    cheat skipsafezone - Skips to the next safe zone
    cheat startshrinksafezone - Starts shrinking the safe zone
    cheat infiniteammo - Toggles infinite ammo
    cheat infinitemats - Toggles infinite materials
    cheat gravity <Scale> - Sets the gravity scale
    cheat changename <Name> - Changes your player name
    cheat keepinventory - Toggles keeping inventory on death
    cheat spawnactor <class/path> - Spawns an actor near your location
    cheat sethealth <amount> - Sets your pawn's health (0-100)
    cheat setshield <amount> - Sets your pawn's shield (0-100)
    cheat demospeed <Speed> - Sets the speed of the server
    cheat god - Toggles god mode
    cheat speed <Speed> - Sets the player's movement speed
    cheat timeofday <Hour> - Sets the time of day (0-23)
	cheat pausetimeofday - Pauses/Unpauses the time of day
    cheat spawnbot - Spawns a player bot at your location (WIP)
    cheat startevent - Starts the event for the current version
    cheat tp <X> <Y> <Z> - Teleports to a location
    cheat launch <X> <Y> <Z> - Launches the player
    cheat savewaypoint - Saves your current location as a waypoint
    cheat waypoint <Name> - Loads a saved waypoint
    cheat skydive - Toggles skydiving
    cheat giveitem <WID/path> <Count = 1> - Gives you an item
    cheat spawnpickup <WID/path> <Count = 1> - Spawns a pickup at your player's location
    cheat spawnactor <class/path> - Spawns an actor at your location + 5 meters)"), FName(), 1);
	}
	else
	{
		auto& command = args[0];
		std::transform(command.begin(), command.end(), command.begin(), tolower);

		if (command == "startaircraft")
		{
			if (UFortGameStateComponent_BattleRoyaleGamePhaseLogic::GetDefaultObj())
			{
				auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(UWorld::GetWorld());

				GamePhaseLogic->StartAircraftPhase();
				PlayerController->ClientMessage(FString(L"Started the aircraft!"), FName(), 1.f);
			}
			else
			{
				UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"startaircraft"), nullptr);
				PlayerController->ClientMessage(FString(L"Started the aircraft!"), FName(), 1.f);
			}
		}
		else if (command == "resumesafezone")
		{
			UFortGameStateComponent_BattleRoyaleGamePhaseLogic::bPausedZone = false;
			if (GameMode->HasbSafeZonePaused())
				GameMode->bSafeZonePaused = false;
			UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"startsafezone"), nullptr);
			PlayerController->ClientMessage(FString(L"Resumed the safe zone."), FName(), 1.f);
		}
		else if (command == "pausesafezone")
		{
			UFortGameStateComponent_BattleRoyaleGamePhaseLogic::bPausedZone = true;
			if (GameMode->HasbSafeZonePaused())
				GameMode->bSafeZonePaused = true;
			UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"pausesafezone"), nullptr);
			PlayerController->ClientMessage(FString(L"Paused the safe zone."), FName(), 1.f);
		}
		else if (command == "skipsafezone")
		{
			if (GameMode->HasSafeZoneIndicator())
			{
				if (GameMode->SafeZoneIndicator)
				{
					GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
					GameMode->SafeZoneIndicator->SafeZoneFinishShrinkTime = GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime + 0.05f;
				}
			}
			else
			{
				auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(UWorld::GetWorld());

				if (GamePhaseLogic->SafeZoneIndicator)
				{
					GamePhaseLogic->SafeZoneIndicator->SafeZoneStartShrinkTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
					GamePhaseLogic->SafeZoneIndicator->SafeZoneFinishShrinkTime = GamePhaseLogic->SafeZoneIndicator->SafeZoneStartShrinkTime + 0.05f;
				}
			}

			PlayerController->ClientMessage(FString(L"Currently skipping the zone."), FName(), 1.f);
			//UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"skipsafezone"), nullptr);
		}
		else if (command == "startshrinksafezone")
		{
			auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
			if (GameMode->HasSafeZoneIndicator())
			{
				if (GameMode->SafeZoneIndicator)
					GameMode->SafeZoneIndicator->SafeZoneStartShrinkTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
			}
			else
			{
				auto GamePhaseLogic = UFortGameStateComponent_BattleRoyaleGamePhaseLogic::Get(UWorld::GetWorld());

				if (GamePhaseLogic->SafeZoneIndicator)
					GamePhaseLogic->SafeZoneIndicator->SafeZoneStartShrinkTime = (float)UGameplayStatics::GetTimeSeconds(UWorld::GetWorld());
			}

			PlayerController->ClientMessage(FString(L"Started shrinking the zone."), FName(), 1.f);

			//UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"startshrinksafezone"), nullptr);
		}
		else if (command == "infiniteammo")
			FConfiguration::bInfiniteAmmo ^= 1;
		else if (command == "infinitemats")
			FConfiguration::bInfiniteMats ^= 1;
		else if (command == "demospeed")
		{
			if (args.size() != 2)
			{
				PlayerController->ClientMessage(FString(L"Wrong number of arguments!"), FName(), 1.f);
				return;
			}

			auto ws = L"demospeed " + UEAllocatedWString(args[1].begin(), args[1].end());

			UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(ws.c_str()), nullptr);
			PlayerController->ClientMessage(FString(L"Modified the server's demospeed!"), FName(), 1.f);
		}
		else if (command == "god")
		{
			bool bUseMin = false;

			if (args.size() > 1)
			{
				std::string FullCommand = args[1].c_str();
				std::transform(FullCommand.begin(), FullCommand.end(), FullCommand.begin(), tolower);

				if (FullCommand == "min" || FullCommand == "minimum")
					bUseMin = true;
			}

			float MinValue = bUseMin ? 1.f : 100.f;
			auto& Health = PlayerController->MyFortPawn->HealthSet->Health;

			if (VersionInfo.FortniteVersion >= 21)
			{
				PlayerController->Pawn->bCanBeDamaged ^= 1;
				PlayerController->ClientMessage(FString(L"Toggled god mode!"), FName(), 1.f);
				return;
			}
			else if (Health.Minimum != MinValue)
			{
				Health.Minimum = MinValue;
				PlayerController->MyFortPawn->HealthSet->OnRep_Health(Health);

				if (bUseMin)
					PlayerController->ClientMessage(FString(L"Minimum Health God mode enabled!"), FName(), 1);
				else
					PlayerController->ClientMessage(FString(L"God mode enabled!"), FName(), 1);
			}
			else
			{
				Health.Minimum = 0.f;
				PlayerController->MyFortPawn->HealthSet->OnRep_Health(Health);

				PlayerController->ClientMessage(FString(L"God mode disabled!"), FName(), 1);
			}
		}
		else if (command == "speed")
		{
			float Speed = 1.0f;

			if (args.size() > 1)
			{
				try { Speed = std::stof(std::string(args[1])); }
				catch (...)
				{
					PlayerController->ClientMessage(FString(L"Invalid speed value"), FName(), 1.f);
					return;
				}
			}

			auto Pawn = PlayerController->Pawn;

			if (!Pawn)
			{
				PlayerController->ClientMessage(FString(L"No pawn to set speed"), FName(), 1.f);
				return;
			}

			static auto SetMovementSpeedFn = Pawn->GetFunction("SetMovementSpeed");

			if (!SetMovementSpeedFn)
				SetMovementSpeedFn = Pawn->GetFunction("SetMovementSpeedMultiplier");

			if (!SetMovementSpeedFn)
				return;

			Pawn->ProcessEvent(SetMovementSpeedFn, &Speed);
			PlayerController->ClientMessage(FString(L"Set player speed!"), FName(), 1.f);
		}
		else if (command == "gravity")
		{
			if (args.size() < 2)
			{
				PlayerController->ClientMessage(FString(L"Usage: gravity <multiplier>"), FName(), 1.f);
				return;
			}

			auto Pawn = PlayerController->Pawn;

			if (!Pawn)
				return;

			float Multiplier = 1.0f;

			try
			{
				std::string argStr(args[1].begin(), args[1].end());
				Multiplier = std::stof(argStr);
			}
			catch (...)
			{
				PlayerController->ClientMessage(FString(L"Invalid multiplier!"), FName(), 1.f);
				return;
			}

			Pawn->SetGravityMultiplier(Multiplier);

			PlayerController->ClientMessage(FString(L"Gravity multiplier set!"), FName(), 1.f);
		}
		else if (command == "changename" || command == "name")
		{
			if (args.size() < 2)
			{
				PlayerController->ClientMessage(FString(L"Please include a phrase/name you'd want to change to!"), FName(), 1);
				return;
			}

			std::string nameStr;

			for (size_t i = 1; i < args.size(); i++)
			{
				nameStr += std::string(args[i].begin(), args[i].end());
				if (i + 1 < args.size())
					nameStr += " ";
			}

			if (nameStr.empty())
			{
				PlayerController->ClientMessage(FString(L"Invalid name!"), FName(), 1);
				return;
			}

			std::wstring nameW(nameStr.begin(), nameStr.end());
			FString NewName(nameW.c_str());

			if (!PlayerController)
				return;

			PlayerController->ServerChangeName(NewName);

			PlayerController->ClientMessage(FString(L"Changed the player's name!"), FName(), 1.f);
		}
		else if (command == "pausetimeofday" || command == "pausetime" || command == "pt")
		{
			static bool bIsPaused = false;

			float Speed = bIsPaused ? 1.f : 0.f;

			UFortKismetLibrary::SetTimeOfDaySpeed(UWorld::GetWorld(), Speed);

			if (bIsPaused)
				PlayerController->ClientMessage(FString(L"Unpaused time of day!"), FName(), 1.f);
			else
				PlayerController->ClientMessage(FString(L"Paused time of day!"), FName(), 1.f);

			bIsPaused ^= 1;
		}
		else if (command == "sethealth")
		{
			if (args.size() < 2)
			{
				PlayerController->ClientMessage(FString(L"Please choose an amount to set your health to!"), FName(), 1.f);
				return;
			}

			auto Pawn = PlayerController->Pawn;

			if (!Pawn)
			{
				PlayerController->ClientMessage(FString(L"No pawn!"), FName(), 1.f);
				return;
			}

			float Health = 100.f;

			try { Health = std::stof(std::string(args[1])); }
			catch (...) {}

			Pawn->SetHealth(Health);
			PlayerController->ClientMessage(FString(L"Set pawn health!"), FName(), 1.f);
		}
		else if (command == "setshield")
		{
			if (args.size() < 2)
			{
				PlayerController->ClientMessage(FString(L"Please choose an amount to set your shield to!"), FName(), 1.f);
				return;
			}

			auto Pawn = PlayerController->Pawn;

			if (!Pawn)
			{
				PlayerController->ClientMessage(FString(L"No pawn!"), FName(), 1.f);
				return;
			}

			float Shield = 100.f;

			try { Shield = std::stof(std::string(args[1])); }
			catch (...) {}

			Pawn->SetShield(Shield);
			PlayerController->ClientMessage(FString(L"Set pawn shield!"), FName(), 1.f);
		}
		else if (command == "keepinv" || command == "keepinventory")
		{
			FConfiguration::bKeepInventory ^= 1;
			PlayerController->ClientMessage(FString(L"Toggled keep inventory!"), FName(), 1.f);
		}
		else if (command == "timeofday" || command == "time" || command == "t")
		{
			if (args.size() < 2)
			{
				PlayerController->ClientMessage(FString(L"Wrong number of arguments!"), FName(), 1.f);
				return;

			}

			float NewTOD = 0.f;
			try { NewTOD = std::stof(std::string(args[1])); }
			catch (...) {}

			UFortKismetLibrary::SetTimeOfDay(UWorld::GetWorld(), NewTOD);
			PlayerController->ClientMessage(FString(L"Set time of day!"), FName(), 1.f);
		}
		else if (command == "spawnbot")
		{
			if (!PlayerController->Pawn)
				return;

			auto CallerController = PlayerController;
			int Count = 1;

			if (args.size() >= 2)
			{
				try { Count = std::stoi(args[1].c_str(), nullptr); }
				catch (...) {}
			}

			for (int i = 0; i < Count; i++)
			{
				auto Transform = PlayerController->Pawn->GetTransform();

				auto GameMode = (AFortGameMode*)UWorld::GetWorld()->AuthorityGameMode;
				auto GameState = GameMode->GameState;
				//auto PlayerController = (AFortPlayerControllerAthena*)UWorld::SpawnActor(GameMode->PlayerControllerClass, FVector{});
				auto Pawn = (AFortPlayerPawnAthena*)UWorld::SpawnActor(GameMode->DefaultPawnClass, Transform);
				auto PlayerController = (AFortPlayerControllerAthena*)UWorld::SpawnActor(FindObject<UClass>(L"/Game/Athena/Athena_PlayerController.Athena_PlayerController_C"), Transform);
				//auto PlayerState = PlayerController->PlayerState;

				if (!PlayerController || !Pawn)
					continue;

				PlayerController->Possess(Pawn);
				PlayerController->MyFortPawn = Pawn; // dont't ask, crashes on 27+

				auto PlayerState = (AFortPlayerStateAthena*)UWorld::SpawnActor(AFortPlayerStateAthena::StaticClass(), Transform);

				PlayerState->SetOwner(PlayerController);

				PlayerController->PlayerState = PlayerState;
				PlayerController->OnRep_PlayerState();

				Pawn->PlayerState = PlayerState;
				Pawn->OnRep_PlayerState();

				Pawn->SetMaxHealth(100.f);
				//Pawn->SetHealth(100.f);

				PlayerState->TeamIndex = AFortGameMode::PickTeam(GameMode, 0, PlayerController);
				if (PlayerState->HasSquadId())
					PlayerState->SquadId = PlayerState->TeamIndex - (VersionInfo.FortniteVersion<9.f||VersionInfo.EngineVersion<4.23?0:VersionInfo.EngineVersion<4.24?3:2);
				if (PlayerState->HasbIsABot())
					PlayerState->bIsABot = true;


				if (GameState->HasGameMemberInfoArray())
				{
					auto Member = (FGameMemberInfo*)malloc(FGameMemberInfo::Size());
					memset((PBYTE)Member, 0, FGameMemberInfo::Size());

					Member->MostRecentArrayReplicationKey = -1;
					Member->ReplicationID = -1;
					Member->ReplicationKey = -1;
					Member->TeamIndex = PlayerState->TeamIndex;
					Member->SquadId = PlayerState->SquadId;
					Member->MemberUniqueId = PlayerState->UniqueId;
        // The roster owns a second native shared reference (SDK assignment is a raw copy).
        if((VersionInfo.FortniteVersion==1.11 || VersionInfo.FortniteVersion==4.0 || VersionInfo.FortniteVersion==4.5) && FUniqueNetIdRepl::Size()==40)
            if(auto Ref=GetFromOffset<uint8*>(&Member->MemberUniqueId,16))InterlockedIncrement((volatile LONG*)(Ref+8));


					auto& NewMember = GameState->GameMemberInfoArray.Members.Add(*Member, FGameMemberInfo::Size());
					GameState->GameMemberInfoArray.MarkItemDirty(NewMember);

					auto NotifyGameMemberAdded = (void(*)(AFortGameStateAthena*, uint8_t, uint8_t, FUniqueNetIdRepl*)) NotifyGameMemberAdded_;
					if (NotifyGameMemberAdded)
						NotifyGameMemberAdded(GameState, Member->SquadId, Member->TeamIndex, &Member->MemberUniqueId);

					free(Member);
				}

				for (auto& AbilitySet : AFortGameMode::AbilitySets)
					PlayerState->AbilitySystemComponent->GiveAbilitySet(AbilitySet);

				/*PlayerController->WorldInventory = (AFortInventory*)UWorld::SpawnActor(AFortInventory::StaticClass(), FVector{});
				PlayerController->WorldInventory->SetOwner(PlayerController);
				PlayerController->WorldInventory->InventoryType = 0;*/
				//PlayerController->bHasInitializedWorldInventory = true;

				GameState->PlayersLeft++;
				GameState->OnRep_PlayersLeft();

				GameMode->AlivePlayers.Add(PlayerController);

				static auto Commando = FindObject(L"/Game/Athena/Heroes/HID_001_Athena_Commando_F.HID_001_Athena_Commando_F", nullptr);
				static auto Commando2 = FindObject(L"/Game/Athena/Heroes/HID_Commando_Athena_01.HID_Commando_Athena_01", nullptr);
				PlayerState->HeroType = Commando ? Commando : Commando2;


				static auto CharacterPartsOffset = PlayerState->GetOffset("CharacterParts", 0x100000);

				if (CharacterPartsOffset == -1)
				{
					static auto CharacterPartsOff = PlayerState->GetOffset("CharacterParts");
					if (CharacterPartsOff == -1)
						CharacterPartsOff = PlayerState->GetOffset("LocalCharacterParts");
					auto& CharacterParts = GetFromOffset<const UObject * [0x6]>(PlayerState, CharacterPartsOff);

					static auto Head = FindObject<UObject>(L"/Game/Characters/CharacterParts/Female/Medium/Heads/F_Med_Head1.F_Med_Head1");
					static auto Body = FindObject<UObject>(L"/Game/Characters/CharacterParts/Female/Medium/Bodies/F_Med_Soldier_01.F_Med_Soldier_01");
					static auto Backpack = FindObject<UObject>(L"/Game/Characters/CharacterParts/Backpacks/NoBackpack.NoBackpack");

					CharacterParts[0] = Head;
					CharacterParts[1] = Body;
					CharacterParts[3] = Backpack;
				}
				else
				{
					static auto CharacterPartsOff = PlayerState->GetOffset("CharacterParts");
					auto& CustomCharacterParts = GetFromOffset<FCustomCharacterParts>(PlayerState, CharacterPartsOff);
					static auto PartsOffset = FCustomCharacterParts::StaticStruct()->GetOffset("Parts");
					auto& CharacterParts = GetFromOffset<const UObject * [0x6]>(&CustomCharacterParts, PartsOffset);

					static auto Head = FindObject<UObject>(L"/Game/Characters/CharacterParts/Female/Medium/Heads/F_Med_Head1.F_Med_Head1");
					static auto Body = FindObject<UObject>(L"/Game/Characters/CharacterParts/Female/Medium/Bodies/F_Med_Soldier_01.F_Med_Soldier_01");
					static auto Backpack = FindObject<UObject>(L"/Game/Characters/CharacterParts/Backpacks/NoBackpack.NoBackpack");

					CharacterParts[0] = Head;
					CharacterParts[1] = Body;
					CharacterParts[3] = Backpack;
				}
				
				if (ApplyCharacterCustomization)
					((void (*)(AActor*, AFortPlayerPawnAthena*)) ApplyCharacterCustomization)(PlayerState, Pawn);

				PlayerBotID++;
				std::string Name = "Erbium Bot (#" + std::to_string(PlayerBotID) + ")";

				std::wstring WideName(Name.begin(), Name.end());

				FString BotName = FString(WideName.c_str());

				if (std::floor(VersionInfo.FortniteVersion) < 9)
				{
					PlayerController->ServerChangeName(BotName);
				}
				else
				{
					GameMode->ChangeName(PlayerController, BotName, true);
				}

				PlayerState->OnRep_PlayerName();

				/*static auto DefaultPickaxe = FindObject<UFortItemDefinition>(L"/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01");

				PlayerController->WorldInventory->GiveItem(DefaultPickaxe);

				static auto SmartItem	DefClass = FindClass("FortSmartBuildingItemDefinition");

				for (int i = 0; i < GameMode->StartingItems.Num(); i++)
				{
					auto& StartingItem = GameMode->StartingItems.Get(i, FItemAndCount::Size());

					if (StartingItem.Count && (!SmartItemDefClass || !StartingItem.Item->IsA(SmartItemDefClass)))
						PlayerController->WorldInventory->GiveItem(StartingItem.Item, StartingItem.Count);
				}*/

				//CallerController->ClientMessage(FString(L"Spawned a player bot!"), FName(), 1.f); // todo: fix
			}
		}
		else if (command == "startevent")
		{
			Events::StartEvent();
			PlayerController->ClientMessage(FString(L"Event started!"), FName(), 1);
		}
		else if (command == "bugitgo" || command == "tp")
		{
			if (args.size() != 4)
			{
				PlayerController->ClientMessage(FString(L"Wrong number of arguments!"), FName(), 1.f);
				return;
			}

			double X = 0., Y = 0., Z = 0.;

			X = strtod(args[1].c_str(), nullptr);
			Y = strtod(args[2].c_str(), nullptr);
			Z = strtod(args[3].c_str(), nullptr);

			if (PlayerController->Pawn)
			{
				PlayerController->Pawn->K2_SetActorLocation(FVector(X, Y, Z), false, nullptr, true);
				PlayerController->ClientMessage(FString(L"Teleported to location!"), FName(), 1.f);
			}
		}
		else if (command == "launch" || command == "launchpawn")
		{
			if (args.size() != 4)
			{
				PlayerController->ClientMessage(FString(L"Wrong number of arguments!"), FName(), 1.f);
				return;
			}

			double X = 0., Y = 0., Z = 0.;

			X = strtod(args[1].c_str(), nullptr);
			Y = strtod(args[2].c_str(), nullptr);
			Z = strtod(args[3].c_str(), nullptr);

			if (PlayerController->Pawn)
			{
				PlayerController->Pawn->LaunchCharacterJump(FVector(X, Y, Z), false, nullptr, true);
				PlayerController->ClientMessage(FString(L"Launched player!"), FName(), 1.f);
			}
		}
		else if (command == "savewaypoint" || command == "s")
		{
			if (args.size() < 2)
			{
				PlayerController->ClientMessage(FString(L"Please provide a phrase to save the waypoint to."), FName(), 1.f);
				return;
			}

			auto Pawn = PlayerController->Pawn;

			if (!Pawn)
			{
				PlayerController->ClientMessage(FString(L"Couldn't find a pawn!"), FName(), 1.f);
				return;
			}

			FVector PawnLocation(Pawn->K2_GetActorLocation().X, Pawn->K2_GetActorLocation().Y, Pawn->K2_GetActorLocation().Z);

			if (PawnLocation.X == 0.0f && PawnLocation.Y == 0.0f && PawnLocation.Z == 0.0f)
			{
				PlayerController->ClientMessage(FString(L"Failed to save a waypoint."), FName(), 1.f);
				return;
			}

			std::string Phrase = args[1].c_str();

			auto It = Waypoints.find(Phrase);

			if (It != Waypoints.end())
			{
				if (args.size() >= 3 && (args[2] == "override" || args[2] == "o"))
				{
					It->second.clear();
					It->second.push_back(PawnLocation);

					PlayerController->ClientMessage(FString(L"Waypoint overridden successfully!"), FName(), 1.f);
				}
				else
				{
					PlayerController->ClientMessage(FString(L"A waypoint with this phrase already exists! Use 'waypoint {phrase} override' to override it."), FName(), 1);
				}
			}
			else
			{
				std::vector<FVector> Locations;
				Locations.push_back(PawnLocation);
				Waypoints[Phrase] = Locations;

				PlayerController->ClientMessage(FString(L"Waypoint saved! Use \" cheat waypoint (phrase) \" to teleport to that location!"), FName(), 1);
			}
		}
		else if (command == "waypoint" || command == "w")
		{
			if (args.size() < 2)
			{
				PlayerController->ClientMessage(FString(L"Please provide a waypoint phrase to teleport to."), FName(), 1.f);
				return;
			}

			std::string Phrase = args[1].c_str();

			auto It = Waypoints.find(Phrase);

			if (It == Waypoints.end() || It->second.empty())
			{
				PlayerController->ClientMessage(FString(L"A saved waypoint with this phrase was not found!"), FName(), 1.f);
				return;
			}

			const auto& WaypointList = It->second;

			if (args.size() >= 3 && (args[2] == "previous" || args[2] == "p"))
			{
				if (WaypointList.size() < 2)
				{
					PlayerController->ClientMessage(FString(L"No previous waypoint available for this phrase!"), FName(), 1.f);
					return;
				}

				FVector Destination = Waypoints[Phrase][Waypoints[Phrase].size() - 2];

				auto Pawn = PlayerController->Pawn;

				if (Pawn)
				{
					Pawn->K2_TeleportTo(Destination, Pawn->K2_GetActorRotation(), false, true);
					Pawn->LaunchCharacterJump(FVector(0.0f, 0.0f, -10000000.0f), false, nullptr, true);
					PlayerController->ClientMessage(FString(L"Teleported to previous waypoint!"), FName(), 1.f);
				}
				else
				{
					PlayerController->ClientMessage(FString(L"Couldn't find a pawn to teleport!"), FName(), 1.f);
				}
			}
			else
			{
				FVector Destination = WaypointList.back();

				if (Destination.X == 0.0f && Destination.Y == 0.0f && Destination.Z == 0.0f)
				{
					PlayerController->ClientMessage(FString(L"Waypoint is invalid (0, 0, 0)! Aborting teleport."), FName(), 1.f);
					return;
				}

				auto Pawn = PlayerController->Pawn;

				if (Pawn)
				{
					Pawn->K2_TeleportTo(Destination, Pawn->K2_GetActorRotation(), false, true);
					Pawn->LaunchCharacterJump(FVector(0.0f, 0.0f, -10000000.0f), false, nullptr, true);
					PlayerController->ClientMessage(FString(L"Teleported to waypoint!"), FName(), 1.f);
				}
				else
				{
					PlayerController->ClientMessage(FString(L"Couldn't find a pawn to teleport!"), FName(), 1.f);
				}
			}
		}
		else if (command == "giveitem")
		{
			if (args.size() != 2 && args.size() != 3)
			{
				PlayerController->ClientMessage(FString(L"Wrong number of arguments!"), FName(), 1.f);
				return;
			}

			auto ItemDefinition = FindObject<UFortItemDefinition>(UEAllocatedWString(args[1].begin(), args[1].end()));
			if (!ItemDefinition)
				ItemDefinition = TUObjectArray::FindObject<UFortItemDefinition>(args[1].c_str());

			if (!ItemDefinition)
				return PlayerController->ClientMessage(FString(L"Failed to find item! Try passing it as a path or check your spelling & casing"), FName(), 1);

			int32 Count = 1;

			if (args.size() == 3)
			{
				Count = strtol(args[2].c_str(), nullptr, 10);
				if (Count <= 0)
					Count = 1;
			}

			//auto ItemEntry = AFortInventory::MakeItemEntry(ItemDefinition, Count, 0);
			//PlayerController->InternalPickup(ItemEntry);
			//free(ItemEntry);
			auto Pawn = PlayerController->Pawn;

			if (!Pawn)
				return;

			FVector FinalLoc = Pawn ? Pawn->K2_GetActorLocation() : FVector();

			FVector ForwardVector = Pawn ? Pawn->GetActorForwardVector() : FVector();
			ForwardVector.Z = 0.0f;
			ForwardVector.Normalize();

			FinalLoc = FinalLoc + ForwardVector * 450.f;
			FinalLoc.Z += 50.f;

			const float RandomAngleVariation = ((float)rand() * 0.00109866634f) - 18.f;
			const float FinalAngle = RandomAngleVariation * 0.017453292519943295f;

			FinalLoc.X += cos(FinalAngle) * 100.f;
			FinalLoc.Y += sin(FinalAngle) * 100.f;

			auto Pickup = AFortInventory::SpawnPickup(FinalLoc, ItemDefinition, Count, 0, EFortPickupSourceTypeFlag::GetOther(), EFortPickupSpawnSource::GetUnset(), Pawn);

			Pawn->ServerHandlePickup(Pickup, Pickup->PickupLocationData.FlyTime, FVector(), true);
			PlayerController->ClientMessage(FString(L"Gave item!"), FName(), 1.f);
			//PlayerController->WorldInventory->GiveItem(ItemDefinition, Count);
		}
		else if (command == "spawnpickup")
		{
			if (args.size() != 2 && args.size() != 3)
			{
				PlayerController->ClientMessage(FString(L"Wrong number of arguments!"), FName(), 1.f);
				return;
			}

			auto ItemDefinition = FindObject<UFortItemDefinition>(UEAllocatedWString(args[1].begin(), args[1].end()));
			if (!ItemDefinition)
				ItemDefinition = TUObjectArray::FindObject<UFortItemDefinition>(args[1].c_str());

			if (!ItemDefinition)
				return PlayerController->ClientMessage(FString(L"Failed to find item! Try passing it as a path or check your spelling & casing"), FName(), 1);

			long Count = 1;

			if (args.size() == 3)
				Count = strtol(args[2].c_str(), nullptr, 10);

			if (PlayerController->Pawn)
			{
				AFortInventory::SpawnPickup(PlayerController->Pawn->K2_GetActorLocation(), ItemDefinition, Count, 0, EFortPickupSourceTypeFlag::GetTossed(), EFortPickupSpawnSource::GetUnset(), PlayerController->Pawn);
				PlayerController->ClientMessage(FString(L"Spawned pickup!"), FName(), 1.f);
			}
		}
		else if (command == "spawnactor" || command == "summon")
		{
			if (args.size() != 2)
			{
				PlayerController->ClientMessage(FString(L"Wrong number of arguments!"), FName(), 1.f);
				return;
			}

			auto Loc = PlayerController->Pawn->K2_GetActorLocation();
			Loc.Z += 200.f;

			auto Class = FindObject<UClass>(UEAllocatedWString(args[1].begin(), args[1].end()).c_str());

			if (!Class)
				Class = FindClass(args[1].c_str());

			if (Class)
			{
				UWorld::SpawnActor(Class, Loc);
				PlayerController->ClientMessage(FString(L"Spawned actor!"), FName(), 1.f);
			}
			else
			{
				return PlayerController->ClientMessage(FString(L"Failed to find class! Try passing it as a path or check your spelling & casing"), FName(), 1);
			}
		}
		else if (command == "skydive")
		{
			auto Pawn = PlayerController->Pawn;
			if (!Pawn)
				return;

			static bool bInVortex = false;
			bInVortex ^= 1;

			Pawn->SetInVortex(bInVortex);
		}
		else if (command == "resetbuilds" || command == "reset")
		{
			TArray<ABuildingSMActor*> Builds;
			Utils::GetAll<ABuildingSMActor>(Builds);
			
			for (auto& Build : Builds)
				if (Build->bPlayerPlaced)
					Build->K2_DestroyActor();

			Builds.Free();
		}
		else
			goto _help;
	}
}

extern bool bDidntFind;
void AFortPlayerControllerAthena::ServerAttemptInteract_(UObject* Context, FFrame& Stack)
{
	AActor* ReceivingActor = *(AActor**)Stack.Locals;
	/*AActor* ReceivingActor;
	UObject* InteractComponent;
	uint8_t InteractType;
	UObject* OptionalObjectData;
	uint8_t InteractionBeingAttempted;
	int32 RequestID;

	Stack.StepCompiledIn(&ReceivingActor);
	Stack.StepCompiledIn(&InteractComponent);
	Stack.StepCompiledIn(&InteractType);
	Stack.StepCompiledIn(&OptionalObjectData);
	Stack.StepCompiledIn(&InteractionBeingAttempted);
	Stack.StepCompiledIn(&RequestID);
	Stack.IncrementCode();*/

	AFortPlayerControllerAthena* PlayerController = nullptr;

	static auto bIsComp = Context->IsA(FindClass("FortControllerComponent_Interaction"));
	if (bIsComp)
		PlayerController = (AFortPlayerControllerAthena*)((UActorComponent*)Context)->GetOwner();
	else
		PlayerController = (AFortPlayerControllerAthena*)Context;

	auto Pawn = (AFortPlayerPawnAthena*)PlayerController->Pawn;


    // Season 4 letters need an explicit authoritative quest acknowledgement.
    if(ReceivingActor && Pawn && PlayerController->PlayerState && VersionInfo.FortniteVersion>=4 && VersionInfo.FortniteVersion<5){
        auto ClassName=ReceivingActor->Class->Name.ToString();std::string Name(ClassName.c_str());
        const std::string Prefix="Prop_QuestInteractable_Letters_";
        auto At=Name.find(Prefix);
        if(At!=std::string::npos){
            int Location=atoi(Name.c_str()+At+Prefix.size());
            if(Location>=1 && Location<=20 && !Pawn->IsDBNO() && Pawn->GetDistanceTo(ReceivingActor)<=350.f){
                extern void QueueSyndicateQuestProgress(AFortPlayerControllerAthena*,const char*,const char*,int);
                char Objective[100];snprintf(Objective,sizeof(Objective),"battlepass_interact_athena_fortnite_letters_%02d",Location);
                ServerAttemptInteract_OG(Context,Stack);
                QueueSyndicateQuestProgress(PlayerController,"Quest_BR_Interact_FORTNITE",Objective,1);
                if(auto Interact=ReceivingActor->GetFunction("BlueprintOnInteract"))if(Interact->GetParamsNamed().Size==sizeof(void*))ReceivingActor->Call<void>(Interact,Pawn);
                return;
            }
        }
    }

    // Pre-season to Season 2: holding interact on a knocked bot teammate starts the server-timed revive (these
    // builds may not send ServerDBNOReviveStarted, and never finish a bot's revive themselves).
    if(SyndicateDBNO::LegacyBotRevive() && ReceivingActor && Pawn)
        if(auto Target=ReceivingActor->Cast<AFortPlayerPawnAthena>())
            if(Target->IsDBNO() && SyndicateDBNO::BotReviveTarget(Target)){
                SyndicateDBNO::ReviveLog("legacy: interact on a knocked bot teammate");
                SyndicateDBNO::StartHumanBotRevive(PlayerController,Target);
            }

	auto sendStat = [&]()
	{
		if (!ReceivingActor)
			return;

		FGameplayTagContainer TargetTags{};
		
		auto Interface = (IGameplayTagAssetInterface*)ReceivingActor->GetInterface(IGameplayTagAssetInterface::StaticClass());
		if (Interface)
		{
			auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2];
			GetOwnedGameplayTags(Interface, &TargetTags);
			//Interface->GetOwnedGameplayTags(&TargetTags);
		}

		PlayerController->GetQuestManager(1)->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetInteract(), 1, false, ReceivingActor, TargetTags);

		//TargetTags.GameplayTags.Free();
		//TargetTags.ParentTags.Free();
	};

	if (auto Container = bDidntFind ? ReceivingActor->Cast<ABuildingContainer>() : nullptr)
		UFortLootPackage::SpawnLootHook(Container);
	// The Baller (Season 8, FortOctopusVehicle) is a physics pawn: it took no vehicle path, so its seat weapon was never
	// given and you could not use it from inside.
	else if (auto Vehicle = ReceivingActor->Cast<AFortAthenaVehicle>() ? ReceivingActor->Cast<AFortAthenaVehicle>()
		: (AFortPhysicsPawn::StaticClass() && ReceivingActor->IsA(AFortPhysicsPawn::StaticClass()) ? (AFortAthenaVehicle*)ReceivingActor : nullptr))
	{
		ServerAttemptInteract_OG(Context, Stack);
		sendStat();
		printf("[Vehicle] entered %s\n", ReceivingActor->Class->Name.ToString().c_str());

		UFortVehicleSeatWeaponComponent* SeatWeaponComponent = nullptr;

		if (Vehicle)
			SeatWeaponComponent = (UFortVehicleSeatWeaponComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatWeaponComponent::StaticClass());
		else if (auto CharacterVehicle = Pawn->Cast<AFortCharacterVehicle>())
			SeatWeaponComponent = (UFortVehicleSeatWeaponComponent*)CharacterVehicle->GetComponentByClass(UFortVehicleSeatWeaponComponent::StaticClass());
		if (!SeatWeaponComponent) printf("[Vehicle] no seat weapon component (the vehicle's own controls only)\n");

		if (SeatWeaponComponent)
		{
			UFortVehicleSeatComponent* SeatComponent = nullptr;

			if (Vehicle)
				SeatComponent = (UFortVehicleSeatComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatComponent::StaticClass());
			else if (auto CharacterVehicle = Pawn->Cast<AFortCharacterVehicle>())
				SeatComponent = (UFortVehicleSeatComponent*)CharacterVehicle->GetComponentByClass(UFortVehicleSeatComponent::StaticClass());

			if (!SeatComponent) { printf("[Vehicle] no seat component\n"); return; }
			auto SeatIdx = SeatComponent->FindSeatIndex(Pawn);
			UFortWeaponItemDefinition* Weapon = nullptr;
			printf("[Vehicle] seat %d, %d seat weapons\n", int(SeatIdx), int(SeatWeaponComponent->WeaponSeatDefinitions.Num()));

			for (int i = 0; i < SeatWeaponComponent->WeaponSeatDefinitions.Num(); i++)
			{
				auto& WeaponDefinition = SeatWeaponComponent->WeaponSeatDefinitions.Get(i, FWeaponSeatDefinition::Size());

				if (WeaponDefinition.SeatIndex != SeatIdx)
					continue;

				Weapon = WeaponDefinition.VehicleWeapon;
				break;
			}

			if (Weapon)
			{
				printf("Weapon: %s\n", Weapon->Name.ToString().c_str());
				// A seat weapon without a stats row (the Mounted Turret's guns) has no clip size: GetStats is null there.
				auto SeatStats = AFortInventory::GetStats(Weapon);
				auto Item = PlayerController->WorldInventory->GiveItem(Weapon, 1, SeatStats ? SeatStats->ClipSize : 0);
				auto ItemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
					{ return entry.ItemDefinition == Weapon; }, FFortItemEntry::Size());
				if (!ItemEntry) { printf("[Vehicle] seat weapon %s was not added to the inventory\n", Weapon->Name.ToString().c_str()); return; }

				auto OldWeapon = Pawn->CurrentWeapon;

				PlayerController->ServerExecuteInventoryItem(ItemEntry->ItemGuid);
				PlayerController->ClientEquipItem(ItemEntry->ItemGuid, true);
				if (Pawn->HasPreviousWeapon())
					Pawn->PreviousWeapon = OldWeapon;

				auto Weapon = (AFortWeapon*)Pawn->CurrentWeapon;

				if (Weapon)
				{
					auto RepWeaponInfo = (FMountedWeaponInfoRepped*)calloc(1, FMountedWeaponInfoRepped::Size());

					if (RepWeaponInfo->HasHostVehicleCached())
					{
						RepWeaponInfo->HostVehicleCached.ObjectPointer = Vehicle;
						RepWeaponInfo->HostVehicleCached.InterfacePointer = Vehicle->GetInterface(IFortVehicleInterface::StaticClass());
					}
					else
						RepWeaponInfo->HostVehicleCachedActor = Vehicle;
					RepWeaponInfo->HostVehicleSeatIndexCached = SeatIdx;

					static auto DualClass = FindClass("FortWeaponRangedDualForVehicle");

					if (Weapon->IsA(DualClass))
					{
						static auto MountedWeaponInfoReppedOff = Weapon->GetOffset("MountedWeaponInfoRepped");
						static auto OnRep_MountedWeaponInfoRepped = Weapon->GetFunction("OnRep_MountedWeaponInfoRepped");
						*(FMountedWeaponInfoRepped*)(__int64(Weapon) + MountedWeaponInfoReppedOff) = *RepWeaponInfo;
						Weapon->Call(OnRep_MountedWeaponInfoRepped);
					}
					else
					{
						static auto MountedWeaponInfoReppedOff = Weapon->GetOffset("MountedWeaponInfoRepped");
						static auto OnRep_MountedWeaponInfoRepped = Weapon->GetFunction("OnRep_MountedWeaponInfoRepped");
						*(FMountedWeaponInfoRepped*)(__int64(Weapon) + MountedWeaponInfoReppedOff) = *RepWeaponInfo;
						Weapon->Call(OnRep_MountedWeaponInfoRepped);
					}

					free(RepWeaponInfo);
				}
			}
		}
		return;
	}
	else if (auto CollectorActor = ReceivingActor->Cast<ABuildingItemCollectorActor>())
	{
		CollectorActor->ControllingPlayer = PlayerController;


		auto Collection = CollectorActor->ItemCollections.Search([&](FCollectorUnitInfo& Coll)
			{
				return Coll.InputItem == CollectorActor->ActiveInputItem;
			}, FCollectorUnitInfo::Size());

		if (!Collection)
		{
			CollectorActor->bCurrentInteractionSuccess = false;
			CollectorActor->ControllingPlayer = nullptr;
			CollectorActor->Call(CollectorActor->GetFunction("BlueprintOnInteract"), PlayerController->MyFortPawn);
			ServerAttemptInteract_OG(Context, Stack);
			sendStat();
			return;
		}

		float Cost = Collection->InputCount.Evaluate((float)CollectorActor->StartingGoalLevel);
		if (Cost > 0)
		{
			auto ItemP = PlayerController->WorldInventory->Inventory.ItemInstances.Search([&](UFortWorldItem* entry)
				{
					return entry->ItemEntry.ItemDefinition == Collection->InputItem;
				});

			if (!ItemP || (*ItemP)->ItemEntry.Count < (int)Cost)
			{
				CollectorActor->bCurrentInteractionSuccess = false;
				CollectorActor->ControllingPlayer = nullptr;
				CollectorActor->Call(CollectorActor->GetFunction("BlueprintOnInteract"), Pawn);
				//CollectorActor->PlayVendFailFX();
				ServerAttemptInteract_OG(Context, Stack);
				sendStat();
				return;
			}

			auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
				{
					return entry.ItemDefinition == Collection->InputItem;
				}, FFortItemEntry::Size());
			auto Item = *ItemP;

			/*for (int i = 0; i < itemEntry->StateValues.Num(); i++)
			{
				auto& StateValue = itemEntry->StateValues.Get(i, FFortItemEntryStateValue::Size());

				if (StateValue.StateType != 2)
					continue;

				StateValue.IntValue = 0;
			}*/

			itemEntry->Count -= (int)Cost;
			if (itemEntry->Count <= 0)
				PlayerController->WorldInventory->Remove(itemEntry->ItemGuid);
			else
			{
				/*for (int i = 0; i < itemEntry->StateValues.Num(); i++)
				{
					auto& StateValue = itemEntry->StateValues.Get(i, FFortItemEntryStateValue::Size());

					if (StateValue.StateType != 2)
						continue;

					StateValue.IntValue = 0;
					break;
				}*/

				Item->ItemEntry.Count = itemEntry->Count;
				PlayerController->WorldInventory->UpdateEntry(*itemEntry);
				Item->ItemEntry.bIsDirty = true;
			}
		}

		CollectorActor->ClientPausedActiveInputItem = CollectorActor->ActiveInputItem;
		CollectorActor->bCurrentInteractionSuccess = true;
		CollectorActor->Call(CollectorActor->GetFunction("BlueprintOnInteract"), Pawn);
		//CollectorActor->PlayVendFX();
	}
	else if (auto LockDevice = ReceivingActor->Cast<ABuildingProp_LockDevice>())
	{
		printf("yo %s\n", LockDevice->LockableObject->Name.ToString().c_str());
		LockDevice->CurrentLockState = 1;
		LockDevice->OnRep_CurrentLockState();
	}

	ServerAttemptInteract_OG(Context, Stack);
	sendStat();
	//return bIsComp ? (void)callOG(((UFortControllerComponent_Interaction*)Context), Stack.GetCurrentNativeFunction(), ServerAttemptInteract, ReceivingActor, InteractComponent, InteractType, OptionalObjectData, InteractionBeingAttempted, RequestID) : callOG(PlayerController, Stack.GetCurrentNativeFunction(), ServerAttemptInteract, ReceivingActor, InteractComponent, InteractType, OptionalObjectData, InteractionBeingAttempted, RequestID);
}
 
void AFortPlayerControllerAthena::ServerDropAllItems(UObject* Context, FFrame& Stack)
{
	UFortItemDefinition* IgnoreItemDef;

	Stack.StepCompiledIn(&IgnoreItemDef);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	printf("ServerDropAllItems[Ignore %s]\n", IgnoreItemDef ? IgnoreItemDef->Name.ToString().c_str() : nullptr);

	auto Loc = PlayerController->MyFortPawn->K2_GetActorLocation();
	for (int i = 0; i < PlayerController->WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
	{
		auto& Entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Get(i, FFortItemEntry::Size());

		if (Entry.ItemDefinition != IgnoreItemDef && Entry.ItemDefinition->CanBeDropped())
		{
			AFortInventory::SpawnPickup(Loc, Entry, EFortPickupSourceTypeFlag::GetPlayer(), EFortPickupSpawnSource::GetUnset(), PlayerController->MyFortPawn);
			PlayerController->WorldInventory->Remove(Entry.ItemGuid);
		}
	}
}


void (*OnUnEquipOG)(AFortWeapon*);
void OnUnEquip(AFortWeapon* Weapon)
{
	AFortInventory::RemoveWeaponAbilities(Weapon);

	return OnUnEquipOG(Weapon);
}

class UFortHeldObjectComponent : public UActorComponent
{
public:
	UCLASS_COMMON_MEMBERS(UFortHeldObjectComponent);

	DEFINE_PROP(EquippedWeaponItemDefinition, TSoftObjectPtr<UFortItemDefinition>);
	DEFINE_PROP(HeldObjectState, uint8);
	DEFINE_PROP(OwningPawn, AFortPlayerPawnAthena*);
	DEFINE_PROP(PreviousOwningPawn, TWeakObjectPtr<AFortPlayerPawnAthena>);
	DEFINE_PROP(GrantedWeaponItem, TWeakObjectPtr<UFortWorldItem>);
	DEFINE_PROP(GrantedWeapon, TWeakObjectPtr<AFortWeapon>);
	DEFINE_PROP(OnHeldObjectOwningPawnChanged, TMulticastInlineDelegate<void()>);
	DEFINE_PROP(OnHeldObjectPickedUp, TMulticastInlineDelegate<void()>);
	DEFINE_PROP(OnHeldObjectDropped, TMulticastInlineDelegate<void()>);

	DEFINE_FUNC(OnRep_OwningPawn, void);
};


void SetHeldObject(AFortPlayerPawnAthena* Pawn, AActor* HeldObject)
{
	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;

	Pawn->HeldObject = HeldObject;
	PlayerController->bHoldingObject = HeldObject != nullptr;
}

void SetupOwningPawn(UFortHeldObjectComponent* HeldObjectComponent, AFortPlayerPawnAthena* Pawn)
{
	auto HeldObject = (AActor*)HeldObjectComponent->GetOwner();

	if (!HeldObject)
		return;

	HeldObject->FlushNetDormancy();

	if (Pawn)
		HeldObjectComponent->HeldObjectState = 1;

	AFortPlayerPawnAthena* PreviousPawn = nullptr;
	if (HeldObjectComponent->HasOwningPawn())
	{
		auto OldPawn = HeldObjectComponent->OwningPawn;
		HeldObjectComponent->PreviousOwningPawn = !Pawn ? HeldObjectComponent->OwningPawn : nullptr;
		HeldObjectComponent->OwningPawn = Pawn;
		//HeldObjectComponent->OnRep_OwningPawn(OldPawn);
		PreviousPawn = OldPawn;
	}
	else
	{
		// its a weakobjectptr on older builds
		static auto OwningPawnOff = HeldObjectComponent->GetOffset("OwningPawn", 0x8000000);
		auto& OwningPawn = *(TWeakObjectPtr<AFortPlayerPawnAthena>*)(__int64(HeldObjectComponent) + OwningPawnOff);

		HeldObjectComponent->PreviousOwningPawn = !Pawn ? OwningPawn.Get() : nullptr;
		auto OldPawn = OwningPawn.Get();
		OwningPawn = Pawn;
		//HeldObjectComponent->OnRep_OwningPawn(OldPawn);
		PreviousPawn = OldPawn;
	}

	if (Pawn)
		SetHeldObject(Pawn, HeldObject);

	HeldObject->ForceNetUpdate();
	if (HeldObjectComponent->HasOnHeldObjectOwningPawnChanged())
		HeldObjectComponent->OnHeldObjectOwningPawnChanged.Process();
	if (Pawn->HasOnHeldObjectPickedUp())
	{
		if (Pawn)
			Pawn->OnHeldObjectPickedUp.Process(HeldObject);
		else
			PreviousPawn->OnHeldObjectDropped.Process(HeldObject);
	}
}

void PickupHeldObject(UObject* Context, FFrame& Stack)
{
	AFortPlayerPawnAthena* Pawn;

	Stack.StepCompiledIn(&Pawn);
	Stack.IncrementCode();
	auto HeldObjectComponent = (UFortHeldObjectComponent*)Context;

	auto PlayerController = (AFortPlayerControllerAthena*)Pawn->Controller;

	SetupOwningPawn(HeldObjectComponent, Pawn);

	if (!HeldObjectComponent->GrantedWeaponItem.Get())
	{
		auto ItemDefinition = HeldObjectComponent->EquippedWeaponItemDefinition.Get();

		auto Item = PlayerController->WorldInventory->GiveItem(ItemDefinition, 1, 99999);

		if (!Item)
			return;

		auto Weapon = (AFortWeapon*)Pawn->EquipWeaponDefinition(ItemDefinition, Item->ItemEntry.ItemGuid, FFortItemEntry::HasTrackerGuid() ? Item->ItemEntry.TrackerGuid : FGuid(), false);

		if (!Weapon)
			return;
		PlayerController->ClientEquipItem(Item->ItemEntry.ItemGuid, true);

		HeldObjectComponent->GrantedWeapon = Weapon;
		HeldObjectComponent->GrantedWeaponItem = Item;
	}
	HeldObjectComponent->OnHeldObjectPickedUp.Process();
}


void PlaceHeldObject(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	printf("PlaceHeldObject\n");
}

void ThrowHeldObject(UObject* Context, FFrame& Stack)
{
	FVector DetachLocation;
	FRotator ThrowDirection;

	Stack.StepCompiledIn(&DetachLocation);
	Stack.StepCompiledIn(&ThrowDirection);
	Stack.IncrementCode();
	printf("ThrowHeldObject\n");
}

void DropHeldObject(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	printf("DropHeldObject\n");
	auto HeldObjectComponent = (UFortHeldObjectComponent*)Context;

	AFortPlayerPawnAthena* OwningPawn = nullptr;

	if (HeldObjectComponent->HasOwningPawn())
		OwningPawn = HeldObjectComponent->OwningPawn;
	else
	{
		// its a weakobjectptr on older builds
		static auto OwningPawnOff = HeldObjectComponent->GetOffset("OwningPawn", 0x8000000);
		OwningPawn = (*(TWeakObjectPtr<AFortPlayerPawnAthena>*)(__int64(HeldObjectComponent) + OwningPawnOff)).Get();
	}
	if (!OwningPawn)
		return;


	auto PlayerController = (AFortPlayerControllerAthena*)OwningPawn->Controller;

	/*auto Item = HeldObjectComponent->GrantedWeaponItem.Get();

	if (!Item)HeldObjectComponent->GrantedWeaponItem
		return;*/
		//printf("GUID: %d %d %d %d, ID: %s\n", Item->ItemEntry.ItemGuid.A, Item->ItemEntry.ItemGuid.B, Item->ItemEntry.ItemGuid.C, Item->ItemEntry.ItemGuid.D, Item->ItemEntry.ItemDefinition->Name.ToString().c_str());

		/*PlayerController->WorldInventory->Remove(Item->ItemEntry.ItemGuid);

		HeldObjectComponent->GrantedWeapon = nullptr;
		HeldObjectComponent->GrantedWeaponItem = nullptr;*/

	HeldObjectComponent->HeldObjectState = 4;


	if (HeldObjectComponent->HasOwningPawn())
	{
		auto OldPawn = HeldObjectComponent->OwningPawn;
		printf("%p\n", OldPawn->PreviousWeapon);
		auto PreviousIns = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{
				return entry.ItemGuid == ((AFortWeapon*)OldPawn->PreviousWeapon)->ItemEntryGuid;
			}, FFortItemEntry::Size());

		if (PreviousIns)
		{
			auto Weapon = (AFortWeapon*)OldPawn->EquipWeaponDefinition(PreviousIns->ItemDefinition, PreviousIns->ItemGuid, FFortItemEntry::HasTrackerGuid() ? PreviousIns->TrackerGuid : FGuid(), false);

			PlayerController->ClientEquipItem(Weapon->ItemEntryGuid, true);
		}
	}
	else
	{
		// its a weakobjectptr on older builds
		static auto OwningPawnOff = HeldObjectComponent->GetOffset("OwningPawn", 0x8000000);
		auto& OwningPawn = *(TWeakObjectPtr<AFortPlayerPawnAthena>*)(__int64(Context) + OwningPawnOff);

		auto PreviousIns = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{
				return entry.ItemGuid == ((AFortWeapon*)OwningPawn->PreviousWeapon)->ItemEntryGuid;
			}, FFortItemEntry::Size());

		if (PreviousIns)
		{
			auto Weapon = (AFortWeapon*)OwningPawn->EquipWeaponDefinition(PreviousIns->ItemDefinition, PreviousIns->ItemGuid, FFortItemEntry::HasTrackerGuid() ? PreviousIns->TrackerGuid : FGuid(), false);

			PlayerController->ClientEquipItem(Weapon->ItemEntryGuid, true);
		}
	}

	SetupOwningPawn(HeldObjectComponent, nullptr);
	HeldObjectComponent->OnHeldObjectDropped.Process();
}


void AFortPlayerControllerAthena::SpawnToyInstance(UObject* Context, FFrame& Stack, AActor** Ret)
{
	TSubclassOf<AActor> ToyClass;
	FTransform SpawnPosition;

	Stack.StepCompiledIn(&ToyClass);
	Stack.StepCompiledIn(&SpawnPosition);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	auto Toy = UWorld::SpawnActor(ToyClass, SpawnPosition, PlayerController);
	PlayerController->ActiveToyInstances.Add(Toy);

	*Ret = Toy;
}

void AFortPlayerControllerAthena::EnterAircraft(UObject* Object, AActor* Aircraft)
{
	AFortPlayerControllerAthena* PlayerController = nullptr;

	static auto bIsComp = Object->IsA(FindClass("FortControllerComponent_Aircraft"));
	if (bIsComp)
		PlayerController = (AFortPlayerControllerAthena*)((UActorComponent*)Object)->GetOwner();
	else
		PlayerController = (AFortPlayerControllerAthena*)Object;

	if (!FConfiguration::bKeepInventory && PlayerController->WorldInventory)
	{
		UEAllocatedVector<FGuid> GuidsToRemove;
		for (int i = 0; i < PlayerController->WorldInventory->Inventory.ReplicatedEntries.Num(); i++)
		{
			auto& Entry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Get(i, FFortItemEntry::Size());

			if (Entry.ItemDefinition->CanBeDropped())
			{
				//NewPlayer->WorldInventory->Inventory.ReplicatedEntries.Remove(i, FFortItemEntry::Size());
				//i--;
				GuidsToRemove.push_back(Entry.ItemGuid);
			}
			if (VersionInfo.FortniteVersion < 3 && Entry.ItemDefinition->ItemType == EFortItemType::GetWeaponHarvest())
			{
				//PlayerController->ServerExecuteInventoryItem(Entry.ItemGuid);
				//PlayerController->QuickBars->ServerActivateSlotInternal(0, 0, 0.f, true);
			}
		}

		for (auto& Guid : GuidsToRemove)
			PlayerController->WorldInventory->Remove(Guid);
	}

	return EnterAircraftOG(Object, Aircraft);
}

class AFortPlayerStartCreative : public AActor
{
public:
	UCLASS_COMMON_MEMBERS(AFortPlayerStartCreative);

	DEFINE_PROP(PlayerStartTags, FGameplayTagContainer);
};
void AFortPlayerControllerAthena::ServerTeleportToPlaygroundLobbyIsland(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	auto GameMode = (AFortGameModeAthena*)UWorld::GetWorld()->AuthorityGameMode;

	static auto CreativePhone = FindObject<UFortWeaponItemDefinition>(L"/Game/Athena/Items/Weapons/Prototype/WID_CreativeTool.WID_CreativeTool");

	auto ItemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
		{
			return entry.ItemDefinition == CreativePhone;
		}, FFortItemEntry::Size());
	if (ItemEntry)
		PlayerController->WorldInventory->Remove(ItemEntry->ItemGuid);

	if (PlayerController->HasbIsCreativeQuickbarEnabled())
	{
		auto OldbIsCreativeQuickbarEnabled = PlayerController->bIsCreativeQuickbarEnabled;
		PlayerController->bIsCreativeQuickbarEnabled = false;
		PlayerController->OnRep_IsCreativeQuickbarEnabled(OldbIsCreativeQuickbarEnabled);
	}
	if (PlayerController->HasbIsCreativeQuickmenuEnabled())
		PlayerController->bIsCreativeQuickmenuEnabled = false;
	if (PlayerController->HasbIsCreativeModeEnabled())
	{
		PlayerController->bIsCreativeModeEnabled = false;
		PlayerController->OnRep_IsCreativeModeEnabled();
	}

	AActor* Actor = GameMode->ChoosePlayerStart(PlayerController);
	PlayerController->Pawn->K2_TeleportTo(Actor->K2_GetActorLocation(), Actor->K2_GetActorRotation());

	PlayerController->CreativePlotLinkedVolume = PlayerController->GetCurrentVolume();
	PlayerController->OnRep_CreativePlotLinkedVolume();
}

inline std::string CleanupString(std::string& s)
{
	if (s.rfind("Schematic:", 0) == 0)
	{
		s.erase(0, 10);
	}
	return s;
}

void AFortPlayerControllerAthena::ServerCraftSchematic(UObject* Context, FFrame& Stack)
{
	FString ItemId;
	int32 PostCraftSlot;
	bool bIsQuickCrafted;
	Stack.StepCompiledIn(&ItemId);
	Stack.StepCompiledIn(&PostCraftSlot);
	Stack.StepCompiledIn(&bIsQuickCrafted);
	Stack.IncrementCode();

	auto PlayerController = (AFortPlayerControllerAthena*)Context;
	auto SchematicStr = ItemId.ToString();
	std::string SchematicStdStr = SchematicStr.c_str();

	printf("CraftShit: ItemId='%s'\n", SchematicStdStr.c_str());

	auto CleanedSchematic = CleanupString(SchematicStdStr);

	printf("clean schematic broo: '%s'\n", CleanedSchematic.c_str());

	auto Schematic = TUObjectArray::FindObject<UFortSchematicItemDefinition>(CleanedSchematic.c_str());
	if (Schematic)
	{
		printf("schematic found : %s\n", Schematic->Name.ToString().c_str());

		auto ResultItemDef = Schematic->GetResultWorldItemDefinition();

		printf("item: %s\n", ResultItemDef->Name.ToString().c_str());

		if (ResultItemDef) {

			auto subbed = Schematic->MaxLevel - Schematic->MinLevel;

			if (subbed <= -1)
				subbed = 0;
			else
			{
				auto calc = (int)(((float)rand() / 32767) * (float)(subbed + 1));
				if (calc <= subbed)
					subbed = calc;
			}

			auto NewEntry = AFortInventory::MakeItemEntry(ResultItemDef, Schematic->GetQuantityProduced(), subbed + Schematic->MinLevel);

			PlayerController->InternalPickup(NewEntry);
			free(NewEntry);

			if (Schematic->CraftingRecipe.DataTable)
			{
				auto FoundRecipe = Schematic->CraftingRecipe.DataTable->RowMap.Search([&](FName& Key, uint8_t*& Value)
					{ return Key == Schematic->CraftingRecipe.RowName; });

				if (!FoundRecipe)
				{
					printf("[Crafting] Failed to find recipe!\n");
					return;
				}

				auto Recipe = *(FRecipe**)FoundRecipe;
				auto CostCount = Recipe->RecipeCosts.Num();

				printf("num costs: %d\n", CostCount);

				for (int i = 0; i < Recipe->RecipeCosts.Num(); i++)
				{
					auto& Cost = Recipe->RecipeCosts.Get(i, FFortItemQuantityPair::Size());

					auto ItemDef = Cost.ItemDefinition.Get();

					auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& Entry)
						{ return Entry.ItemDefinition == ItemDef; }, FFortItemEntry::Size());
					auto ItemP = PlayerController->WorldInventory->Inventory.ItemInstances.Search([&](UFortWorldItem*& Item)
						{ return Item->ItemEntry.ItemDefinition == ItemDef; });

					if (itemEntry)
					{
						auto Item = *ItemP;

						itemEntry->Count -= Cost.Quantity;

						if (itemEntry->Count <= 0)
							PlayerController->WorldInventory->Remove(itemEntry->ItemGuid);
						else
						{
							Item->ItemEntry.Count = itemEntry->Count;
							PlayerController->WorldInventory->UpdateEntry(*itemEntry);
							Item->ItemEntry.bIsDirty = true;
						}
					}
				}
			}

			if (Schematic->HasCraftingRequirements() && Schematic->CraftingRequirements.DataTable)
			{
				auto FoundRequirements = Schematic->CraftingRequirements.DataTable->RowMap.Search([&](FName& Key, uint8_t*& Value)
					{ return Key == Schematic->CraftingRequirements.RowName; });

				if (!FoundRequirements)
				{
					printf("[Crafting] Failed to find requirements!\n");
					return;
				}

				auto Reqirements = *(FSchematicRequirements**)FoundRequirements;
				auto CostCount = Reqirements->Requirements.Num();

				printf("num requirements: %d\n", CostCount);

				for (int i = 0; i < Reqirements->Requirements.Num(); i++)
				{
					auto& Requirement = Reqirements->Requirements.Get(i, FSchematicRequirement::Size());
					auto ItemDef = Requirement.ItemDefinition;

					auto itemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& Entry)
						{ return Entry.ItemDefinition == ItemDef; }, FFortItemEntry::Size());
					auto ItemP = PlayerController->WorldInventory->Inventory.ItemInstances.Search([&](UFortWorldItem*& Item)
						{ return Item->ItemEntry.ItemDefinition == ItemDef; });

					if (itemEntry)
					{
						auto Item = *ItemP;

						itemEntry->Count -= Requirement.Count;

						if (itemEntry->Count <= 0)
							PlayerController->WorldInventory->Remove(itemEntry->ItemGuid);
						else
						{
							Item->ItemEntry.Count = itemEntry->Count;
							PlayerController->WorldInventory->UpdateEntry(*itemEntry);
							Item->ItemEntry.bIsDirty = true;
						}
					}
				}
			}
		}
	}
}

void AFortPlayerControllerAthena::ServerGiveCreativeItem(UObject* Context, FFrame& Stack)
{
	auto CreativeItem = (FFortItemEntry*)malloc(FFortItemEntry::Size());
	memset(CreativeItem, 0, FFortItemEntry::Size());
	FGuid ItemToRemoveGuid{};
	Stack.StepCompiledIn(CreativeItem);
	Stack.StepCompiledIn(&ItemToRemoveGuid);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	if (auto WeaponDef = CreativeItem->ItemDefinition->Cast<UFortWeaponItemDefinition>())
		CreativeItem->LoadedAmmo = AFortInventory::GetStats(WeaponDef)->ClipSize;

	PlayerController->InternalPickup(CreativeItem);
	free(CreativeItem);
}

void AFortPlayerControllerAthena::ServerRequestSeatChange_(UObject* Context, FFrame& Stack)
{
	int TargetSeatIndex;

	Stack.StepCompiledIn(&TargetSeatIndex);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	auto Pawn = PlayerController->Pawn;

	static auto GetVehicleFunc = Pawn->GetFunction("GetVehicleActor");
	if (!GetVehicleFunc)
		GetVehicleFunc = Pawn->GetFunction("GetVehicle");
	auto Vehicle = Pawn->Call<AActor*>(GetVehicleFunc);

	if (!Vehicle && Pawn->IsA<AFortCharacterVehicle>())
		Vehicle = Pawn;

	if (!Vehicle)
		return callOG(PlayerController, Stack.GetCurrentNativeFunction(), ServerRequestSeatChange, TargetSeatIndex);
	
	UFortVehicleSeatWeaponComponent* SeatWeaponComponent = (UFortVehicleSeatWeaponComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatWeaponComponent::StaticClass());
	if (!SeatWeaponComponent)
		return callOG(PlayerController, Stack.GetCurrentNativeFunction(), ServerRequestSeatChange, TargetSeatIndex);

	UFortVehicleSeatComponent* SeatComponent = (UFortVehicleSeatComponent*)Vehicle->GetComponentByClass(UFortVehicleSeatComponent::StaticClass());

	auto SeatIdx = SeatComponent->FindSeatIndex(PlayerController->MyFortPawn);

	UFortWeaponItemDefinition* OldWeapon = nullptr;
	UFortWeaponItemDefinition* NewWeapon = nullptr;
	if (SeatWeaponComponent)
	{
		for (int i = 0; i < SeatWeaponComponent->WeaponSeatDefinitions.Num(); i++)
		{
			auto& WeaponDefinition = SeatWeaponComponent->WeaponSeatDefinitions.Get(i, FWeaponSeatDefinition::Size());

			if (WeaponDefinition.SeatIndex != SeatIdx)
				continue;

			OldWeapon = WeaponDefinition.VehicleWeapon;
			break;
		}

		if (OldWeapon)
		{
			auto ItemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
				{ return entry.ItemDefinition == OldWeapon; }, FFortItemEntry::Size());

			if (ItemEntry)
				PlayerController->WorldInventory->Remove(ItemEntry->ItemGuid);
		}

		for (int i = 0; i < SeatWeaponComponent->WeaponSeatDefinitions.Num(); i++)
		{
			auto& WeaponDefinition = SeatWeaponComponent->WeaponSeatDefinitions.Get(i, FWeaponSeatDefinition::Size());

			if (WeaponDefinition.SeatIndex != TargetSeatIndex)
				continue;

			NewWeapon = WeaponDefinition.VehicleWeapon;
			break;
		}
	}

	callOG(PlayerController, Stack.GetCurrentNativeFunction(), ServerRequestSeatChange, TargetSeatIndex);

	if (OldWeapon && !NewWeapon)
	{
		auto LastItem = Pawn->HasPreviousWeapon() ? (AFortWeapon*)Pawn->PreviousWeapon : nullptr;

		if (LastItem)
		{
			PlayerController->ServerExecuteInventoryItem(LastItem->ItemEntryGuid);
			PlayerController->ClientEquipItem(LastItem->ItemEntryGuid, true);
		}
		else
		{
			auto pickaxeEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([](FFortItemEntry& entry)
				{ return entry.ItemDefinition->IsA<UFortWeaponMeleeItemDefinition>(); }, FFortItemEntry::Size());

			if (pickaxeEntry)
			{
				PlayerController->ServerExecuteInventoryItem(pickaxeEntry->ItemGuid);
				PlayerController->ClientEquipItem(pickaxeEntry->ItemGuid, true);
			}
		}
	}

	if (NewWeapon)
	{
		auto SeatStats = AFortInventory::GetStats(NewWeapon);
		auto NewItem = PlayerController->WorldInventory->GiveItem(NewWeapon, 1, SeatStats ? SeatStats->ClipSize : 0);
		auto ItemEntry = PlayerController->WorldInventory->Inventory.ReplicatedEntries.Search([&](FFortItemEntry& entry)
			{ return entry.ItemDefinition == NewWeapon; }, FFortItemEntry::Size());
		if (!ItemEntry) { printf("[Vehicles] seat weapon %s was not added to the inventory\n", NewWeapon->Name.ToString().c_str()); return; }
		auto CurrentWeapon = Pawn->CurrentWeapon;

		PlayerController->ServerExecuteInventoryItem(ItemEntry->ItemGuid);
		PlayerController->ClientEquipItem(ItemEntry->ItemGuid, true);
		if (Pawn->HasPreviousWeapon())
			Pawn->PreviousWeapon = CurrentWeapon;

		auto Weapon = (AFortWeapon*)Pawn->CurrentWeapon;

		if (Weapon)
		{
			printf("[Vehicles] Setting up MountedWeaponInfoRepped\n");
			auto RepWeaponInfo = (FMountedWeaponInfoRepped*)calloc(1, FMountedWeaponInfoRepped::Size());

			if (RepWeaponInfo->HasHostVehicleCached())
			{
				RepWeaponInfo->HostVehicleCached.ObjectPointer = Vehicle;
				RepWeaponInfo->HostVehicleCached.InterfacePointer = Vehicle->GetInterface(IFortVehicleInterface::StaticClass());
			}
			else
				RepWeaponInfo->HostVehicleCachedActor = Vehicle;
			RepWeaponInfo->HostVehicleSeatIndexCached = SeatIdx;

			static auto DualClass = FindClass("FortWeaponRangedDualForVehicle");

			if (Weapon->IsA(DualClass))
			{
				static auto MountedWeaponInfoReppedOff = Weapon->GetOffset("MountedWeaponInfoRepped");
				static auto OnRep_MountedWeaponInfoRepped = Weapon->GetFunction("OnRep_MountedWeaponInfoRepped");
				*(FMountedWeaponInfoRepped*)(__int64(Weapon) + MountedWeaponInfoReppedOff) = *RepWeaponInfo;
				Weapon->Call(OnRep_MountedWeaponInfoRepped);
			}
			else
			{
				static auto MountedWeaponInfoReppedOff = Weapon->GetOffset("MountedWeaponInfoRepped");
				static auto OnRep_MountedWeaponInfoRepped = Weapon->GetFunction("OnRep_MountedWeaponInfoRepped");
				*(FMountedWeaponInfoRepped*)(__int64(Weapon) + MountedWeaponInfoReppedOff) = *RepWeaponInfo;
				Weapon->Call(OnRep_MountedWeaponInfoRepped);
			}

			free(RepWeaponInfo);
		}
	}
}

void AFortPlayerControllerAthena::ServerLoadingScreenDropped_(UObject* Context, FFrame& Stack)
{
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	callOG(PlayerController, Stack.GetCurrentNativeFunction(), ServerLoadingScreenDropped);
}

void AFortPlayerControllerAthena::ServerCreativeSetFlightSpeedIndex(UObject* Context, FFrame& Stack)
{
	int32 Index_0;

	Stack.StepCompiledIn(&Index_0);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	auto bIsFlyingPossible = PlayerController->Validation_IsFlyingPossible();

	if (PlayerController->Validation_IsFlyingPossible__Ptr && !bIsFlyingPossible)
		return;

	PlayerController->FlyingModifierIndex = Index_0;
	PlayerController->OnRep_FlyingModifierIndex();
}

void AFortPlayerControllerAthena::ServerCreativeSetFlightSprint(UObject* Context, FFrame& Stack)
{
	bool bSprint;

	Stack.StepCompiledIn(&bSprint);
	Stack.IncrementCode();
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	PlayerController->bIsFlightSprinting = bSprint;
	PlayerController->OnRep_IsFlightSprinting();
}

struct FIndicatedActorData
{
	// doesnt matter rn
	uint8_t Padding[0x150];
};

void AddActorsToIndicatedList(UObject* Context, FFrame& Stack)
{
	AFortPlayerControllerAthena* InstigatingController;
	bool bAddAsUnique;
	bool bAllowOwningPlayer;
	bool bReplaceExistingEntry;
	bool bRefreshExistingEntry;

	Stack.StepCompiledIn(&InstigatingController);
	auto& IndicatedActors = Stack.StepCompiledInRef<TArray<AActor*>>();
	auto& IndicatedActorData = Stack.StepCompiledInRef<FIndicatedActorData>();
	Stack.StepCompiledIn(&bAddAsUnique);
	Stack.StepCompiledIn(&bAllowOwningPlayer);
	Stack.StepCompiledIn(&bReplaceExistingEntry);
	Stack.StepCompiledIn(&bRefreshExistingEntry);
	Stack.IncrementCode();

	printf("AddActorsToIndicatedList\n");
	for (auto& IndicatedActor : IndicatedActors)
	{
		printf("Indicate the frickin %s\n", IndicatedActor->Name.ToString().c_str());
	}
}

void AFortPlayerControllerAthena::ServerAwardVehicleTrickPoints_(UObject* Context, FFrame& Stack)
{
	int32 InPoints;
	int32 InAirTimeX1000;
	int32 NumberOfTricks = 0;
	float AirDistance = 0.f;
	float AirHeight = 0.f;

	Stack.StepCompiledIn(&InPoints);
	Stack.StepCompiledIn(&InAirTimeX1000);
	Stack.StepCompiledIn(&NumberOfTricks);
	Stack.StepCompiledIn(&AirDistance);
	Stack.StepCompiledIn(&AirHeight);
	auto PlayerController = (AFortPlayerControllerAthena*)Context;

	callOG(PlayerController, Stack.GetCurrentNativeFunction(), ServerAwardVehicleTrickPoints, InPoints, InAirTimeX1000, NumberOfTricks, AirDistance, AirHeight);

	FGameplayTagContainer TargetTags{};

	auto Interface = (IGameplayTagAssetInterface*)PlayerController->Pawn->GetInterface(IGameplayTagAssetInterface::StaticClass());
	if (Interface)
	{
		auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))Interface->Vft[0x2];
		GetOwnedGameplayTags(Interface, &TargetTags);
		//Interface->GetOwnedGameplayTags(&TargetTags);
	}

	static auto GetVehicleFunc = PlayerController->Pawn->GetFunction("GetVehicleActor");
	if (!GetVehicleFunc)
		GetVehicleFunc = PlayerController->Pawn->GetFunction("GetVehicle");
	auto Vehicle = PlayerController->Pawn->Call<AActor*>(GetVehicleFunc);

	auto VehicleInterface = (IGameplayTagAssetInterface*)Vehicle->GetInterface(IGameplayTagAssetInterface::StaticClass());
	if (VehicleInterface)
	{
		auto GetOwnedGameplayTags = (void(*)(IGameplayTagAssetInterface*, FGameplayTagContainer*))VehicleInterface->Vft[0x2];
		GetOwnedGameplayTags(VehicleInterface, &TargetTags);
		//Interface->GetOwnedGameplayTags(&TargetTags);
	}


	auto RealAirTime = (float)InAirTimeX1000 * 0.001f;
	PlayerController->GetQuestManager(1)->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetEarnVehicleTrickPoints(), InPoints, false, PlayerController, TargetTags);
	PlayerController->GetQuestManager(1)->SendStatEvent(PlayerController, EFortQuestObjectiveStatEvent::GetVehicleAirTime(), (int)RealAirTime, false, PlayerController, TargetTags);

	TargetTags.GameplayTags.Free();
	TargetTags.ParentTags.Free();
}


// Season 4's persistent map cursor is replicated through the owning player state.
static void SyndicateUpdateMapCursor(AFortPlayerControllerAthena* PC,const float* Cursor,bool Remove)
{
    auto State=PC?(AFortPlayerStateAthena*)PC->PlayerState:nullptr;if(!State)return;
    auto Pos=State->GetOffset("MapIndicatorPos"),Time=State->GetOffset("MapIndicatorServerTime");
    auto Prop=State->GetProperty("MapIndicatorPos");if(!Prop || Pos==uint32(-1) || GetFromOffset<uint32>(Prop,Offsets::ElementSize)!=8)return;
    float XY[2]={Cursor?Cursor[0]:0.f,Cursor?Cursor[1]:0.f};
    auto Defaults=State->Class->GetDefaultObj();
    if(Remove && Defaults)memcpy(XY,(uint8*)Defaults+Pos,sizeof(XY));
    if(!std::isfinite(XY[0]) || !std::isfinite(XY[1]))return;
    if(!Remove && (fabs(XY[0])>10000000.f || fabs(XY[1])>10000000.f))return;
    if(auto Flush=State->GetFunction("FlushNetDormancy"))State->ProcessEvent(Flush,nullptr);
    State->SetNetDormancy(1);memcpy((uint8*)State+Pos,XY,sizeof(XY));
    if(Time!=uint32(-1))GetFromOffset<float>(State,Time)=Remove && Defaults?GetFromOffset<float>(Defaults,Time):float(UGameplayStatics::GetTimeSeconds(UWorld::GetWorld()));
    auto Local=PC->GetOffset("MapCursor");auto LocalProp=PC->GetProperty("MapCursor");
    if(Local!=uint32(-1) && LocalProp && GetFromOffset<uint32>(LocalProp,Offsets::ElementSize)==12){
        if(Remove){auto CDO=PC->Class->GetDefaultObj();if(CDO)memcpy((uint8*)PC+Local,(uint8*)CDO+Local,12);}
        else memcpy((uint8*)PC+Local,Cursor,12);
    }
    if(auto Rep=State->GetFunction("OnRep_MapIndicatorPos"))State->ProcessEvent(Rep,nullptr);
    State->ForceNetUpdate();PC->ForceNetUpdate();
    // Record real client requests, not just server-side self-test writes.
    wchar_t Ready[32768]{};
    if(GetEnvironmentVariableW(L"TILTED_SERVER_READY_FILE",Ready,32768)){
        std::ofstream Log(std::filesystem::path(Ready).parent_path()/L"map-marker.log",std::ios::app);
        float Readback[2]{};auto Getter=PC->GetFunction("GetMapIndicatorLocation");
        if(Getter && Getter->GetParamsNamed().Size==8)PC->ProcessEvent(Getter,Readback);
        Log<<(Remove?"remove":"place")<<" player="<<State->Name.ToString()<<" requested="<<XY[0]<<","<<XY[1]<<" nativeGetter="<<Readback[0]<<","<<Readback[1]<<" time="<<UGameplayStatics::GetTimeSeconds(UWorld::GetWorld())<<"\n";
    }
}
static void SyndicatePlaceMapCursor(UObject* Context,FFrame& Stack)
{
    float Cursor[3]{};Stack.StepCompiledIn(Cursor);Stack.IncrementCode();
    SyndicateUpdateMapCursor((AFortPlayerControllerAthena*)Context,Cursor,false);
}
static void SyndicateRemoveMapCursor(UObject* Context,FFrame& Stack)
{
    Stack.IncrementCode();SyndicateUpdateMapCursor((AFortPlayerControllerAthena*)Context,nullptr,true);
}

using SyndicateRemoveAliveFn = void (*)(AFortGameMode*, AFortPlayerControllerAthena*, AFortPlayerStateAthena*, AFortPlayerPawnAthena*, UFortItemDefinition*, uint8, char);
static SyndicateRemoveAliveFn SyndicateRemoveAliveOG = nullptr;
// Every removal (also the game's own when a player leaves) goes through the bot-aware roster,
// so the match only ends when one team is left, counting bots, not when a real player quits.
static void SyndicateRemoveFromAlivePlayers(AFortGameMode* Mode, AFortPlayerControllerAthena* PC, AFortPlayerStateAthena* KillerState, AFortPlayerPawnAthena* KillerPawn, UFortItemDefinition* Item, uint8 Cause, char Flag)
{
	if (RemoveSyndicateHumanFromBotMatch(PC)) { printf("[SyndicateMatch] player removed; match continues while other teams remain\n"); return; }
	if (SyndicateRemoveAliveOG) SyndicateRemoveAliveOG(Mode, PC, KillerState, KillerPawn, Item, Cause, Flag);
}

void AFortPlayerControllerAthena::PostLoadHook()
{
	if (VersionInfo.FortniteVersion >= 27)
	{
		CanPlaceBuildableClassInStructuralGrid_ = FindCanPlaceBuildableClassInStructuralGrid();
		//InitializeBuildingActor_ = FindInitializeBuildingActor();
		//PostInitializeSpawnedBuildingActor_ = FindPostInitializeSpawnedBuildingActor();
	}
	CantBuild_ = FindCantBuild();
	ReplaceBuildingActor_ = FindReplaceBuildingActor(); // pre-cache building offsets
	RemoveFromAlivePlayers_ = FindRemoveFromAlivePlayers();
	if (RemoveFromAlivePlayers_) Utils::Hook(RemoveFromAlivePlayers_, SyndicateRemoveFromAlivePlayers, SyndicateRemoveAliveOG);
	GiveAbilityAndActivateOnce = FindGiveAbilityAndActivateOnce();
	CanAffordToPlaceBuildableClass_ = FindCanAffordToPlaceBuildableClass();
	PayBuildableClassPlacementCost_ = FindPayBuildableClassPlacementCost();
	InitializePlayerGameplayAbilities_ = FindInitializePlayerGameplayAbilities();

	auto DefaultFortPC = DefaultObjImpl("FortPlayerController");
    // Syndicate fix: markers only worked in Season 4. Hook every supported build that
    // ships the marker RPCs (the signature checks below skip builds without them).
    if(VersionInfo.FortniteVersion<6){
        auto Place=GetDefaultObj()->GetFunction("ServerPlaceMapCursor"),Remove=GetDefaultObj()->GetFunction("ServerRemoveMapCursor");
        if(Place && Place->GetParamsNamed().Size==12)Utils::ExecHook(Place,SyndicatePlaceMapCursor);
        if(Remove && Remove->GetParamsNamed().Size==0)Utils::ExecHook(Remove,SyndicateRemoveMapCursor);
    }

	Utils::Hook(FindGetPlayerViewPoint(), GetPlayerViewPoint, GetPlayerViewPointOG);
	// they only stripped it on athena for some reason
	//auto ServerAcknowledgePossessionIdx = GetDefaultObj()->GetFunction("ServerAcknowledgePossession")->GetVTableIndex();
	//Utils::Hook<AFortPlayerControllerAthena>(ServerAcknowledgePossessionIdx, DefaultFortPC->Vft[ServerAcknowledgePossessionIdx]);

	if (VersionInfo.FortniteVersion >= 11)
	{
		auto ServerRestartPlayerIdx = GetDefaultObj()->GetFunction("ServerRestartPlayer")->GetVTableIndex();
		auto DefaultFortPCZone = DefaultObjImpl("FortPlayerControllerZone");
		Utils::Hook<AFortPlayerControllerAthena>(ServerRestartPlayerIdx, DefaultFortPCZone->Vft[ServerRestartPlayerIdx]);
	}

	auto ServerSuicideIdx = GetDefaultObj()->GetFunction("ServerSuicide")->GetVTableIndex();
	auto DefaultFortPCZone = DefaultObjImpl("FortPlayerControllerZone");
	Utils::Hook<AFortPlayerControllerAthena>(ServerSuicideIdx, DefaultFortPCZone->Vft[ServerSuicideIdx]);

	//if (VersionInfo.FortniteVersion >= 11)
	//{
	if (VersionInfo.FortniteVersion < 11)
		ServerAttemptAircraftJumpVft = GetDefaultObj()->GetFunction("ServerAttemptAircraftJump")->GetVTableIndex();
	
	auto ServerAttemptAircraftJumpPC = GetDefaultObj()->GetFunction("ServerAttemptAircraftJump");
	if (!ServerAttemptAircraftJumpPC)
		Utils::ExecHook(DefaultObjImpl("FortControllerComponent_Aircraft")->GetFunction("ServerAttemptAircraftJump"), ServerAttemptAircraftJump_, ServerAttemptAircraftJump_OG);
	else
		Utils::ExecHook(ServerAttemptAircraftJumpPC, ServerAttemptAircraftJump_);
	//}

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerAcknowledgePossession"), ServerAcknowledgePossession);
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerExecuteInventoryItem"), ServerExecuteInventoryItem_);
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerExecuteInventoryWeapon"), ServerExecuteInventoryWeapon); // S9 shenanigans

	// same as serveracknowledgepossession
	auto ServerReturnToMainMenuIdx = GetDefaultObj()->GetFunction("ServerReturnToMainMenu")->GetVTableIndex();
	Utils::Hook<AFortPlayerControllerAthena>(ServerReturnToMainMenuIdx, DefaultFortPC->Vft[ServerReturnToMainMenuIdx]);

	//if (VersionInfo.FortniteVersion != 1.72 && VersionInfo.FortniteVersion != 1.8)
	{
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerCreateBuildingActor"), ServerCreateBuildingActor);
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerBeginEditingBuildingActor"), ServerBeginEditingBuildingActor);
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerEditBuildingActor"), ServerEditBuildingActor);
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerEndEditingBuildingActor"), ServerEndEditingBuildingActor);
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerRepairBuildingActor"), ServerRepairBuildingActor);

	}
	auto ServerAttemptInventoryDropFn = GetDefaultObj()->GetFunction("ServerAttemptInventoryDrop");
	if (ServerAttemptInventoryDropFn)
		Utils::ExecHook(ServerAttemptInventoryDropFn, ServerAttemptInventoryDrop);
	else
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerSpawnInventoryDrop"), ServerAttemptInventoryDrop);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerPlayEmoteItem"), ServerPlayEmoteItem);
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerPlaySprayItem"), ServerPlayEmoteItem);

	auto ClientOnPawnDiedAddr = FindFunctionCall(L"ClientOnPawnDied", VersionInfo.EngineVersion == 4.16 ? std::vector<uint8_t>{ 0x48, 0x89, 0x54 } : (VersionInfo.FortniteVersion >= 24 && VersionInfo.FortniteVersion < 25 ? std::vector<uint8_t>{ 0x48, 0x8B, 0xC4 } : std::vector<uint8_t>{ 0x48, 0x89, 0x5C }));
	Utils::Hook(ClientOnPawnDiedAddr, ClientOnPawnDied, ClientOnPawnDiedOG);

	if (VersionInfo.FortniteVersion >= 15)
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerClientIsReadyToRespawn"), ServerClientIsReadyToRespawn);

	if (FConfiguration::bEnableCheats)
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerCheat"), ServerCheat);

    if(SyndicateDBNO::HumanBotRevivePath()){
        auto Start=GetDefaultObj()->GetFunction("ServerDBNOReviveStarted");auto Stop=GetDefaultObj()->GetFunction("ServerDBNOReviveInterrupted");
        if(Start && Stop && Start->GetParamsNamed().Size==sizeof(void*) && Stop->GetParamsNamed().Size==sizeof(void*)){
            Utils::ExecHook(Start,HumanBotReviveStarted,HumanBotReviveStartedOG);
            Utils::ExecHook(Stop,HumanBotReviveInterrupted,HumanBotReviveInterruptedOG);
            printf("[SyndicateRevive] human->bot revive hooks installed\n");
        } else printf("[SyndicateRevive] revive RPCs not found on this season (start=%d stop=%d)\n",Start?1:0,Stop?1:0);
    }
	auto ServerAttemptInteractPC = GetDefaultObj()->GetFunction("ServerAttemptInteract");
	if (!ServerAttemptInteractPC)
		Utils::ExecHook(DefaultObjImpl("FortControllerComponent_Interaction")->GetFunction("ServerAttemptInteract"), ServerAttemptInteract_, ServerAttemptInteract_OG);
	else
		Utils::ExecHook(ServerAttemptInteractPC, ServerAttemptInteract_, ServerAttemptInteract_OG);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerDropAllItems"), ServerDropAllItems);

	auto DefaultWeaponComp = DefaultObjImpl("FortWeaponComponent");

	if (VersionInfo.FortniteVersion >= 14.00)
	{
		auto OnUnEquipAddr = FindFunctionCall(L"K2_OnUnEquip", std::vector<uint8_t>{ 0x48, 0x89, 0x5C });

		Utils::Hook(OnUnEquipAddr, OnUnEquip, OnUnEquipOG);

		//Utils::ExecHook(DefaultWeaponComp->GetFunction("OnUnEquip"), OnUnEquip);
	}

	auto DefaultHeldObjComp = DefaultObjImpl("FortHeldObjectComponent");

	if (DefaultHeldObjComp)
	{
		Utils::ExecHook(DefaultHeldObjComp->GetFunction("PickupHeldObject"), PickupHeldObject);
		Utils::ExecHook(DefaultHeldObjComp->GetFunction("DropHeldObject"), DropHeldObject);
		Utils::ExecHook(DefaultHeldObjComp->GetFunction("PlaceHeldObject"), PlaceHeldObject);
		Utils::ExecHook(DefaultHeldObjComp->GetFunction("ThrowHeldObject"), ThrowHeldObject);
	}

	Utils::ExecHook(GetDefaultObj()->GetFunction("SpawnToyInstance"), SpawnToyInstance);
	Utils::Hook(FindEnterAircraft(), EnterAircraft, EnterAircraftOG);

	if (wcsstr(FConfiguration::Playlist, L"/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2")) // on 24.20+ u get killed for going into water without this
		Utils::ExecHook(GetDefaultObj()->GetFunction("ServerTeleportToPlaygroundLobbyIsland"), ServerTeleportToPlaygroundLobbyIsland);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerCraftSchematic"), ServerCraftSchematic);
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerGiveCreativeItem"), ServerGiveCreativeItem);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerRequestSeatChange"), ServerRequestSeatChange_, ServerRequestSeatChange_OG);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerLoadingScreenDropped"), ServerLoadingScreenDropped_, ServerLoadingScreenDropped_OG);

	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerCreativeSetFlightSpeedIndex"), ServerCreativeSetFlightSpeedIndex);
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerCreativeSetFlightSprint"), ServerCreativeSetFlightSprint);
	Utils::ExecHook(GetDefaultObj()->GetFunction("ServerAwardVehicleTrickPoints"), ServerAwardVehicleTrickPoints_, ServerAwardVehicleTrickPoints_OG);

	auto DefaultIndicatedActorLibrary = DefaultObjImpl("FortIndicatedActorManagementLibrary");

	if (DefaultIndicatedActorLibrary)
		Utils::ExecHook(DefaultIndicatedActorLibrary->GetFunction("AddActorsToIndicatedList"), AddActorsToIndicatedList);
}
